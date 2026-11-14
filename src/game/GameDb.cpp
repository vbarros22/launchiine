#include "GameDb.h"
#include "fs/FSUtils.h"
#include "utils/logger.h"

#include <atomic>
#include <condition_variable>
#include <coreinit/userconfig.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <curl/curl.h>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <nn/ac.h>
#include <set>
#include <sys/stat.h>
#include <zlib.h>

namespace {
    constexpr const char *DIR_PATH     = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *COVER_DIR    = "fs:/vol/external01/wiiu/launchiine/covers";
    constexpr const char *CACHE_FILE   = "fs:/vol/external01/wiiu/launchiine/gamedb.txt";
    constexpr const char *MANUAL_ZIP   = "fs:/vol/external01/wiiu/launchiine/wiiutdb.zip";
    constexpr const char *DB_URL       = "http://www.gametdb.com/wiiutdb.zip?LANG=";
    constexpr const char *COVER_URL    = "http://art.gametdb.com/wiiu/";
    //! the database changes slowly; look again for missing games after a week
    constexpr int64_t RECHECK_SECONDS = 7 * 24 * 60 * 60;

    std::mutex lock;
    std::condition_variable wake;
    std::map<std::string, GameDb::Info> infos;
    std::set<std::string> wantedIds;
    std::deque<std::string> coverQueue;
    std::set<std::string> coverTried; // asked for in this run, found or not
    std::atomic<uint32_t> changeCounter{0};
    std::atomic<bool> stopRequested{false};
    std::future<void> worker;
    bool networkFailed = false;

    std::string coverFile(const std::string &id) {
        return std::string(COVER_DIR) + "/" + id + ".jpg";
    }

    bool fileExists(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0;
    }

    //! GameTDB language code of the console language
    const char *languageCode() {
        static const char *codes[12] = {"JA", "EN", "FR", "DE", "IT", "ES", "ZHCN", "KO", "NL", "PT", "RU", "ZHTW"};
        uint32_t language = 1;
        int32_t handle    = UCOpen();
        if (handle >= 0) {
            alignas(0x40) uint32_t value = 1;
            alignas(0x40) UCSysConfig config;
            memset(&config, 0, sizeof(config));
            strncpy(config.name, "cafe.language", sizeof(config.name) - 1);
            config.dataType = UC_DATATYPE_UNSIGNED_INT;
            config.dataSize = sizeof(value);
            config.data     = &value;
            if (UCReadSysConfig(handle, 1, &config) == 0 && value < 12) {
                language = value;
            }
            UCClose(handle);
        }
        return codes[language];
    }

    // ---- cache file: "ID\ttitle\tsynopsis\tdeveloper\tpublisher\tyear\tgenre\tplayers\n" ----

    std::string clean(const std::string &in) {
        std::string out = in;
        for (auto &c : out) {
            if (c == '\t' || c == '\r') {
                c = ' ';
            } else if (c == '\n') {
                c = '\x1F'; // keep line breaks of the synopsis
            }
        }
        return out;
    }

    std::vector<std::string> splitTabs(const std::string &line) {
        std::vector<std::string> fields;
        size_t start = 0;
        while (true) {
            size_t tab = line.find('\t', start);
            fields.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
            if (tab == std::string::npos) {
                break;
            }
            start = tab + 1;
        }
        return fields;
    }

    int64_t loadCache(std::map<std::string, GameDb::Info> &out) {
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(CACHE_FILE, &buffer, &size) <= 0 || buffer == nullptr) {
            return 0;
        }
        std::string text((const char *) buffer, size);
        free(buffer);
        int64_t checkedAt = 0;
        size_t pos        = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) {
                end = text.size();
            }
            std::string line = text.substr(pos, end - pos);
            pos              = end + 1;
            if (line.rfind("checked ", 0) == 0) {
                checkedAt = strtoll(line.c_str() + 8, nullptr, 10);
                continue;
            }
            auto f = splitTabs(line);
            if (f.size() < 8 || f[0].empty()) {
                continue;
            }
            for (auto &c : f[2]) {
                if (c == '\x1F') {
                    c = '\n';
                }
            }
            out[f[0]] = {f[1], f[2], f[3], f[4], f[5], f[6], f[7]};
        }
        return checkedAt;
    }

    void saveCache(const std::map<std::string, GameDb::Info> &data, int64_t checkedAt) {
        std::string text = "checked " + std::to_string((long long) checkedAt) + "\n";
        for (auto const &x : data) {
            const auto &i = x.second;
            text += x.first + "\t" + clean(i.title) + "\t" + clean(i.synopsis) + "\t" + clean(i.developer) + "\t" + clean(i.publisher) + "\t" +
                    clean(i.year) + "\t" + clean(i.genre) + "\t" + clean(i.players) + "\n";
        }
        FSUtils::CreateSubfolder(DIR_PATH);
        if (FSUtils::saveBufferToFile(CACHE_FILE, (void *) text.data(), text.size()) != (int32_t) text.size()) {
            DEBUG_FUNCTION_LINE("Failed to write %s", CACHE_FILE);
        }
    }

    // ---- network ----

    size_t onData(char *data, size_t size, size_t count, void *user) {
        auto *out = (std::string *) user;
        if (out->size() > 32 * 1024 * 1024) {
            return 0;
        }
        out->append(data, size * count);
        return size * count;
    }

    int onProgress(void *, double, double, double, double) {
        return stopRequested ? 1 : 0;
    }

    bool networkUp() {
        static bool connected = false;
        if (connected) {
            return true;
        }
        if (networkFailed) {
            return false;
        }
        ACInitialize();
        NNResult res = ACConnect();
        if (NNResult_IsFailure(res)) {
            DEBUG_FUNCTION_LINE("ACConnect failed: %08X", res.value);
            networkFailed = true;
            return false;
        }
        static bool curlReady = false;
        if (!curlReady) {
            curl_global_init(CURL_GLOBAL_DEFAULT);
            curlReady = true;
        }
        connected = true;
        return true;
    }

    bool download(const std::string &url, std::string &out) {
        if (!networkUp()) {
            return false;
        }
        CURL *curl = curl_easy_init();
        if (curl == nullptr) {
            return false;
        }
        out.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, onProgress);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "launchiine (Wii U)");
        CURLcode res  = curl_easy_perform(curl);
        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);
        DEBUG_FUNCTION_LINE("GET %s -> curl %d, HTTP %ld, %d bytes", url.c_str(), (int) res, httpCode, (int) out.size());
        return res == CURLE_OK && httpCode == 200 && !out.empty();
    }

    // ---- zip: the first file of the archive, stored or deflated ----

    bool unzipFirst(const std::string &zip, std::string &out) {
        auto u16 = [&](size_t p) { return (uint32_t) (uint8_t) zip[p] | ((uint32_t) (uint8_t) zip[p + 1] << 8); };
        auto u32 = [&](size_t p) { return u16(p) | (u16(p + 2) << 16); };
        if (zip.size() < 30 || u32(0) != 0x04034b50) {
            return false;
        }
        uint32_t method = u16(8);
        uint32_t flags  = u16(6);
        uint32_t csize  = u32(18);
        uint32_t usize  = u32(22);
        size_t data     = 30 + u16(26) + u16(28);
        if (flags & 0x08) {
            //! sizes are in the central directory
            size_t eocd = zip.rfind(std::string("PK\x05\x06", 4));
            if (eocd == std::string::npos) {
                return false;
            }
            size_t cd = u32(eocd + 16);
            if (cd + 46 > zip.size() || u32(cd) != 0x02014b50) {
                return false;
            }
            csize = u32(cd + 20);
            usize = u32(cd + 24);
        }
        if (data + csize > zip.size()) {
            return false;
        }
        if (method == 0) {
            out = zip.substr(data, csize);
            return true;
        }
        if (method != 8) {
            return false;
        }
        out.resize(usize);
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            return false;
        }
        stream.next_in   = (Bytef *) zip.data() + data;
        stream.avail_in  = csize;
        stream.next_out  = (Bytef *) &out[0];
        stream.avail_out = usize;
        int res          = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        return res == Z_STREAM_END;
    }

    // ---- xml ----

    std::string decodeEntities(const std::string &in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); i++) {
            if (in[i] != '&') {
                out += in[i];
                continue;
            }
            size_t semi = in.find(';', i);
            if (semi == std::string::npos || semi - i > 10) {
                out += in[i];
                continue;
            }
            std::string e = in.substr(i + 1, semi - i - 1);
            if (e == "amp") {
                out += '&';
            } else if (e == "lt") {
                out += '<';
            } else if (e == "gt") {
                out += '>';
            } else if (e == "quot") {
                out += '"';
            } else if (e == "apos") {
                out += '\'';
            } else if (!e.empty() && e[0] == '#') {
                uint32_t cp = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? strtoul(e.c_str() + 2, nullptr, 16) : strtoul(e.c_str() + 1, nullptr, 10);
                //! UTF-8
                if (cp < 0x80) {
                    out += (char) cp;
                } else if (cp < 0x800) {
                    out += (char) (0xC0 | (cp >> 6));
                    out += (char) (0x80 | (cp & 0x3F));
                } else {
                    out += (char) (0xE0 | (cp >> 12));
                    out += (char) (0x80 | ((cp >> 6) & 0x3F));
                    out += (char) (0x80 | (cp & 0x3F));
                }
            } else {
                out += in.substr(i, semi - i + 1);
            }
            i = semi;
        }
        return out;
    }

    //! text of <tag>..</tag> inside [from, to)
    std::string tagText(const std::string &xml, const char *tag, size_t from, size_t to) {
        std::string open = std::string("<") + tag + ">";
        size_t start     = xml.find(open, from);
        if (start == std::string::npos || start >= to) {
            return "";
        }
        start += open.size();
        size_t end = xml.find(std::string("</") + tag + ">", start);
        if (end == std::string::npos || end > to) {
            return "";
        }
        return decodeEntities(xml.substr(start, end - start));
    }

    //! value of attr="..." of the first <tag ...> inside [from, to)
    std::string tagAttribute(const std::string &xml, const char *tag, const char *attr, size_t from, size_t to) {
        size_t start = xml.find(std::string("<") + tag + " ", from);
        if (start == std::string::npos || start >= to) {
            return "";
        }
        size_t close = xml.find('>', start);
        std::string key = std::string(attr) + "=\"";
        size_t a        = xml.find(key, start);
        if (a == std::string::npos || a > close) {
            return "";
        }
        a += key.size();
        size_t b = xml.find('"', a);
        return b == std::string::npos ? "" : xml.substr(a, b - a);
    }

    void parseDatabase(const std::string &xml, const std::set<std::string> &ids, const char *lang, std::map<std::string, GameDb::Info> &out) {
        size_t pos   = 0;
        int found    = 0;
        std::string localeWanted = std::string("<locale lang=\"") + lang + "\">";
        while ((pos = xml.find("<game ", pos)) != std::string::npos) {
            size_t end = xml.find("</game>", pos);
            if (end == std::string::npos) {
                break;
            }
            std::string id = tagText(xml, "id", pos, end);
            if (ids.count(id) != 0) {
                GameDb::Info info;
                //! the console language first, English otherwise
                size_t locale = xml.find(localeWanted, pos);
                if (locale == std::string::npos || locale > end) {
                    locale = xml.find("<locale lang=\"EN\">", pos);
                }
                if (locale != std::string::npos && locale < end) {
                    size_t localeEnd = xml.find("</locale>", locale);
                    info.title       = tagText(xml, "title", locale, localeEnd);
                    info.synopsis    = tagText(xml, "synopsis", locale, localeEnd);
                }
                if (info.synopsis.empty()) {
                    size_t en = xml.find("<locale lang=\"EN\">", pos);
                    if (en != std::string::npos && en < end) {
                        info.synopsis = tagText(xml, "synopsis", en, xml.find("</locale>", en));
                    }
                }
                info.developer = tagText(xml, "developer", pos, end);
                info.publisher = tagText(xml, "publisher", pos, end);
                info.year      = tagAttribute(xml, "date", "year", pos, end);
                info.genre     = tagText(xml, "genre", pos, end);
                info.players   = tagAttribute(xml, "input", "players", pos, end);
                out[id]        = info;
                found++;
            }
            pos = end + 7;
        }
        DEBUG_FUNCTION_LINE("GameTDB: %d of %d games found", found, (int) ids.size());
    }

    bool loadDatabase(const std::set<std::string> &ids, std::map<std::string, GameDb::Info> &out) {
        std::string zip;
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(MANUAL_ZIP, &buffer, &size) > 0 && buffer != nullptr) {
            DEBUG_FUNCTION_LINE("Using %s", MANUAL_ZIP);
            zip.assign((const char *) buffer, size);
            free(buffer);
        } else if (!download(std::string(DB_URL) + languageCode(), zip)) {
            return false;
        }
        std::string xml;
        if (!unzipFirst(zip, xml)) {
            DEBUG_FUNCTION_LINE("Could not unpack the GameTDB database (%d bytes)", (int) zip.size());
            return false;
        }
        zip.clear();
        zip.shrink_to_fit();
        parseDatabase(xml, ids, languageCode(), out);
        return true;
    }

    //! front cover, by region of the id (4th letter), larger first
    bool downloadCover(const std::string &id) {
        char region = id.size() >= 4 ? id[3] : 'E';
        std::vector<std::string> regions;
        if (region == 'J') {
            regions = {"JA", "US"};
        } else if (region == 'P') {
            regions = {"EN", "PT", "FR", "DE", "ES", "IT", "US"};
        } else {
            regions = {"US", "EN"};
        }
        for (const char *kind : {"coverHQ", "coverM", "cover"}) {
            for (auto &r : regions) {
                if (stopRequested) {
                    return false;
                }
                std::string data;
                if (download(std::string(COVER_URL) + kind + "/" + r + "/" + id + ".jpg", data)) {
                    FSUtils::CreateSubfolder(COVER_DIR);
                    std::string path = coverFile(id);
                    if (FSUtils::saveBufferToFile(path.c_str(), (void *) data.data(), data.size()) == (int32_t) data.size()) {
                        return true;
                    }
                    return false;
                }
                if (networkFailed) {
                    return false;
                }
            }
        }
        return false;
    }

    void run() {
        std::set<std::string> ids;
        {
            std::lock_guard<std::mutex> guard(lock);
            ids = wantedIds;
        }
        std::map<std::string, GameDb::Info> cached;
        int64_t checkedAt = loadCache(cached);
        {
            std::lock_guard<std::mutex> guard(lock);
            infos = cached;
        }
        changeCounter++;

        bool missing = false;
        for (auto &id : ids) {
            missing |= cached.count(id) == 0;
        }
        int64_t now = (int64_t) time(nullptr);
        if (missing && (checkedAt == 0 || now - checkedAt > RECHECK_SECONDS || now < checkedAt) && !stopRequested) {
            std::map<std::string, GameDb::Info> fresh;
            if (loadDatabase(ids, fresh) && !stopRequested) {
                for (auto &x : fresh) {
                    cached[x.first] = x.second;
                }
                saveCache(cached, now);
                std::lock_guard<std::mutex> guard(lock);
                infos = cached;
                changeCounter++;
            }
        }

        //! covers, as the TV view asks for them
        while (!stopRequested) {
            std::string id;
            {
                std::unique_lock<std::mutex> guard(lock);
                wake.wait(guard, [] { return stopRequested || !coverQueue.empty(); });
                if (stopRequested) {
                    break;
                }
                id = coverQueue.front();
                coverQueue.pop_front();
            }
            if (!fileExists(coverFile(id)) && !networkFailed && downloadCover(id)) {
                changeCounter++;
            }
        }
    }
} // namespace

std::string GameDb::idFromProductCode(const std::string &productCode) {
    //! "WUP-P-AMKE/0001": the last 4 characters of the product code and of the company code
    size_t slash = productCode.find('/');
    if (slash == std::string::npos || slash < 4 || productCode.size() < slash + 3) {
        return "";
    }
    std::string product = productCode.substr(slash - 4, 4);
    std::string company = productCode.substr(productCode.size() - 2);
    for (char c : product + company) {
        if (!isalnum((unsigned char) c)) {
            return "";
        }
    }
    return product + company;
}

void GameDb::start(const std::vector<std::string> &ids) {
    if (worker.valid()) {
        return;
    }
    {
        std::lock_guard<std::mutex> guard(lock);
        wantedIds.clear();
        for (auto &id : ids) {
            if (!id.empty()) {
                wantedIds.insert(id);
            }
        }
    }
    stopRequested = false;
    worker        = std::async(std::launch::async, run);
}

void GameDb::stop() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopRequested = true;
    }
    wake.notify_all();
    if (worker.valid()) {
        worker.wait();
    }
}

bool GameDb::get(const std::string &id, Info &out) {
    std::lock_guard<std::mutex> guard(lock);
    auto it = infos.find(id);
    if (it == infos.end()) {
        return false;
    }
    out = it->second;
    return true;
}

std::string GameDb::coverPath(const std::string &id) {
    if (id.empty()) {
        return "";
    }
    std::string path = coverFile(id);
    if (fileExists(path)) {
        return path;
    }
    std::lock_guard<std::mutex> guard(lock);
    if (coverTried.insert(id).second) {
        coverQueue.push_front(id); // the newest request first
        wake.notify_all();
    }
    return "";
}

uint32_t GameDb::generation() {
    return changeCounter;
}
