#include "Updates.h"
#include "fs/FSUtils.h"
#include "utils/logger.h"

#include <atomic>
#include <coreinit/mcp.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <curl/curl.h>
#include <future>
#include <map>
#include <mutex>
#include <nn/ac.h>
#include <nsysnet/nssl.h>
#include <set>
#include <string>
#include <vector>

//! not declared in this wut version; the Wii U Menu uses client certificate 3 for tagaya
extern "C" NSSLError NSSLSetClientPKI(NSSLContextHandle context, int32_t clientPki);

namespace {
    constexpr const char *SETTINGS_DIR = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *CACHE_FILE   = "fs:/vol/external01/wiiu/launchiine/updates.txt";
    constexpr const char *TAGAYA_URL   = "https://tagaya.wup.shop.nintendo.net/tagaya/versionlist";
    //! the version list changes rarely, do not download it every time the menu comes back
    constexpr int64_t RECHECK_SECONDS = 6 * 60 * 60;

    constexpr uint32_t GAME_UPPER   = 0x00050000;
    constexpr uint32_t UPDATE_UPPER = 0x0005000E;

    std::mutex resultLock;
    std::set<uint64_t> withUpdate;
    std::atomic<uint32_t> resultGeneration{0};

    std::future<void> worker;
    std::atomic<bool> stopRequested{false};

    struct Installed {
        std::set<uint32_t> games;                  // lower title id of installed games
        std::map<uint32_t, uint16_t> updateVersion; // lower title id -> installed update version
    };

    struct Cache {
        int64_t checkedAt = 0;
        std::map<uint32_t, uint16_t> latest; // lower title id -> latest update version
    };

    Installed readInstalled() {
        Installed result;
        int32_t mcp = MCP_Open();
        if (mcp < 0) {
            DEBUG_FUNCTION_LINE("MCP_Open failed");
            return result;
        }
        int32_t count = MCP_TitleCount(mcp);
        if (count < 0) {
            MCP_Close(mcp);
            return result;
        }
        //! the Aroma homebrew_on_menu plugin appends its titles without checking the size
        std::vector<MCPTitleListType> titles(count + 1024);
        uint32_t listed = 0;
        MCPError err    = MCP_TitleList(mcp, &listed, titles.data(), titles.size() * sizeof(MCPTitleListType));
        MCP_Close(mcp);
        if (err < 0) {
            DEBUG_FUNCTION_LINE("MCP_TitleList failed: %08X", err);
            return result;
        }
        for (uint32_t i = 0; i < listed && i < titles.size(); i++) {
            uint32_t upper = titles[i].titleId >> 32;
            uint32_t lower = (uint32_t) titles[i].titleId;
            if (upper == GAME_UPPER) {
                result.games.insert(lower);
            } else if (upper == UPDATE_UPPER) {
                //! the same update can be on MLC and USB, keep the newest
                uint16_t &v = result.updateVersion[lower];
                if (titles[i].titleVersion > v) {
                    v = titles[i].titleVersion;
                }
            }
        }
        return result;
    }

    Cache loadCache() {
        Cache cache;
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(CACHE_FILE, &buffer, &size) <= 0 || buffer == nullptr) {
            return cache;
        }
        std::string text((const char *) buffer, size);
        free(buffer);

        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) {
                end = text.size();
            }
            std::string line = text.substr(pos, end - pos);
            pos              = end + 1;
            if (line.rfind("checked ", 0) == 0) {
                cache.checkedAt = strtoll(line.c_str() + 8, nullptr, 10);
                continue;
            }
            char *rest     = nullptr;
            uint32_t lower = strtoul(line.c_str(), &rest, 16);
            if (rest == line.c_str() || *rest != ' ') {
                continue;
            }
            cache.latest[lower] = (uint16_t) strtoul(rest + 1, nullptr, 10);
        }
        return cache;
    }

    void saveCache(const Cache &cache) {
        std::string text = "checked " + std::to_string((long long) cache.checkedAt) + "\n";
        char line[32];
        for (auto const &x : cache.latest) {
            snprintf(line, sizeof(line), "%08X %u\n", x.first, x.second);
            text += line;
        }
        FSUtils::CreateSubfolder(SETTINGS_DIR);
        if (FSUtils::saveBufferToFile(CACHE_FILE, (void *) text.data(), text.size()) != (int32_t) text.size()) {
            DEBUG_FUNCTION_LINE("Failed to write %s", CACHE_FILE);
        }
    }

    void publish(const Installed &installed, const Cache &cache) {
        std::set<uint64_t> found;
        for (uint32_t lower : installed.games) {
            auto latest = cache.latest.find(lower);
            if (latest == cache.latest.end()) {
                continue;
            }
            auto have          = installed.updateVersion.find(lower);
            uint16_t installedV = have != installed.updateVersion.end() ? have->second : 0;
            if (latest->second > installedV) {
                found.insert(((uint64_t) GAME_UPPER << 32) | lower);
                DEBUG_FUNCTION_LINE_VERBOSE("Update for %08X: v%u installed, v%u available", lower, installedV, latest->second);
            }
        }
        DEBUG_FUNCTION_LINE("%d games have an update", (int) found.size());
        {
            std::lock_guard<std::mutex> guard(resultLock);
            withUpdate.swap(found);
        }
        resultGeneration++;
    }

    //! value of the first <tag>...</tag> at or after pos
    std::string xmlValue(const std::string &xml, const char *tag, size_t pos = 0) {
        std::string open  = std::string("<") + tag + ">";
        std::string close = std::string("</") + tag + ">";
        size_t start      = xml.find(open, pos);
        if (start == std::string::npos) {
            return "";
        }
        start += open.size();
        size_t end = xml.find(close, start);
        if (end == std::string::npos) {
            return "";
        }
        return xml.substr(start, end - start);
    }

    size_t onData(char *data, size_t size, size_t count, void *user) {
        auto *out = (std::string *) user;
        if (out->size() > 4 * 1024 * 1024) {
            return 0; // the list is a few hundred KB, stop on anything absurd
        }
        out->append(data, size * count);
        return size * count;
    }

    int onProgress(void *, double, double, double, double) {
        return stopRequested ? 1 : 0; // non zero aborts the transfer
    }

    bool download(NSSLContextHandle ssl, const std::string &url, std::string &out) {
        CURL *curl = curl_easy_init();
        if (curl == nullptr) {
            DEBUG_FUNCTION_LINE("curl_easy_init failed");
            return false;
        }
        out.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_NSSL_CONTEXT, ssl);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, onProgress);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res  = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        DEBUG_FUNCTION_LINE("GET %s -> curl %d, HTTP %ld, %d bytes", url.c_str(), (int) res, httpCode, (int) out.size());
        return res == CURLE_OK && httpCode == 200;
    }

    const char *regionName() {
        alignas(0x40) static MCPSysProdSettings settings;
        int32_t mcp = MCP_Open();
        if (mcp < 0) {
            return "USA";
        }
        memset(&settings, 0, sizeof(settings));
        MCPError err = MCP_GetSysProdSettings(mcp, &settings);
        MCP_Close(mcp);
        if (err < 0) {
            DEBUG_FUNCTION_LINE("MCP_GetSysProdSettings failed: %08X", err);
            return "USA";
        }
        switch (settings.product_area) {
            case MCP_REGION_JAPAN:
                return "JPN";
            case MCP_REGION_EUROPE:
                return "EUR";
            default:
                return "USA";
        }
    }

    //! Downloads the version list, true with cache.latest filled
    bool fetchLatest(Cache &cache) {
        NNResult acInit = ACInitialize();
        if (NNResult_IsFailure(acInit)) {
            DEBUG_FUNCTION_LINE("ACInitialize failed: %08X", acInit.value);
        }
        NNResult connected = ACConnect();
        if (NNResult_IsFailure(connected)) {
            DEBUG_FUNCTION_LINE("ACConnect failed: %08X", connected.value);
            ACFinalize();
            return false;
        }

        bool ok = false;
        NSSLInit();
        NSSLContextHandle ssl = NSSLCreateContext(0);
        if (ssl < 0) {
            DEBUG_FUNCTION_LINE("NSSLCreateContext failed: %08X", ssl);
        } else {
            for (int32_t ca = NSSL_SERVER_CERT_GROUP_NINTENDO_FIRST; ca <= NSSL_SERVER_CERT_GROUP_NINTENDO_LAST; ca++) {
                NSSLAddServerPKI(ssl, (NSSLServerCertId) ca);
            }
            NSSLError certRes = NSSLSetClientPKI(ssl, 3);
            if (certRes != NSSL_ERROR_OK) {
                DEBUG_FUNCTION_LINE("NSSLSetClientPKI failed: %08X", certRes);
            }

            //! the country part does not change the versions, "NN" is what Cemu uses
            std::string base = std::string(TAGAYA_URL) + "/" + regionName() + "/NN";
            std::string xml;
            if (download(ssl, base + "/latest_version", xml)) {
                std::string listVersion = xmlValue(xml, "version");
                std::string fqdn        = xmlValue(xml, "fqdn");
                DEBUG_FUNCTION_LINE("Version list %s on %s", listVersion.c_str(), fqdn.c_str());
                if (!listVersion.empty() && !fqdn.empty() && !stopRequested) {
                    std::string listUrl = "https://" + fqdn + "/tagaya/versionlist/" + regionName() + "/NN/list/" + listVersion + ".versionlist";
                    if (download(ssl, listUrl, xml)) {
                        std::map<uint32_t, uint16_t> latest;
                        size_t pos = 0;
                        while ((pos = xml.find("<title>", pos)) != std::string::npos) {
                            std::string id      = xmlValue(xml, "id", pos);
                            std::string version = xmlValue(xml, "version", pos);
                            pos += 7;
                            uint64_t titleId = strtoull(id.c_str(), nullptr, 16);
                            if ((uint32_t) (titleId >> 32) == UPDATE_UPPER && !version.empty()) {
                                latest[(uint32_t) titleId] = (uint16_t) strtoul(version.c_str(), nullptr, 10);
                            }
                        }
                        DEBUG_FUNCTION_LINE("Version list has %d updates", (int) latest.size());
                        if (!latest.empty()) {
                            cache.latest.swap(latest);
                            ok = true;
                        }
                    }
                }
            }
            NSSLDestroyContext(ssl);
        }
        NSSLFinish();
        ACFinalize();
        return ok;
    }

    void run() {
        Installed installed = readInstalled();
        Cache cache         = loadCache();
        DEBUG_FUNCTION_LINE("%d games, %d updates installed, %d cached versions", (int) installed.games.size(), (int) installed.updateVersion.size(), (int) cache.latest.size());
        if (!cache.latest.empty()) {
            publish(installed, cache);
        }

        int64_t now = (int64_t) time(nullptr);
        if (cache.checkedAt > 0 && now >= cache.checkedAt && now - cache.checkedAt < RECHECK_SECONDS) {
            DEBUG_FUNCTION_LINE("Version list checked %lld s ago, skipping", (long long) (now - cache.checkedAt));
            return;
        }
        if (stopRequested) {
            return;
        }

        static bool curlReady = false;
        if (!curlReady) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curlReady = true;
        }
        if (fetchLatest(cache) && !stopRequested) {
            cache.checkedAt = now;
            saveCache(cache);
            publish(installed, cache);
        }
    }
} // namespace

void Updates::start() {
    if (worker.valid()) {
        return;
    }
    stopRequested = false;
    worker        = std::async(std::launch::async, run);
}

void Updates::stop() {
    stopRequested = true;
    if (worker.valid()) {
        DEBUG_FUNCTION_LINE("Waiting for the update check to stop");
        worker.wait();
    }
}

bool Updates::hasUpdate(uint64_t titleId) {
    std::lock_guard<std::mutex> guard(resultLock);
    return withUpdate.count(titleId) != 0;
}

uint32_t Updates::generation() {
    return resultGeneration;
}
