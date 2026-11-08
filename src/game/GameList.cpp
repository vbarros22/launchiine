#include <algorithm>
#include <coreinit/cache.h>
#include <coreinit/mcp.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/time.h>
#include <coreinit/userconfig.h>
#include <map>
#include <malloc.h>
#include <nn/acp/nn_acp_types.h>
#include <nn/acp/title.h>
#include <string.h>
#include <string>

#include "GameList.h"
#include "common/common.h"
#include "utils/AsyncExecutor.h"

#include "fs/FSUtils.h"
#include "utils/logger.h"

namespace {
    //! Size of the buffer handed to ACPGetTitleMetaXml. The struct in wut is smaller than
    //! what the system writes, so keep the generous size from the original code.
    constexpr uint32_t META_XML_BUFFER_SIZE = 0x4000;

    //! cafe.language: 0=ja 1=en 2=fr 3=de 4=it 5=es 6=zhs 7=ko 8=nl 9=pt 10=ru 11=zht
    int32_t getSystemLanguage() {
        static int32_t cachedLanguage = -1;
        if (cachedLanguage >= 0) {
            return cachedLanguage;
        }

        cachedLanguage = 1; // English as fallback
        int32_t handle = UCOpen();
        if (handle >= 0) {
            alignas(0x40) uint32_t language = 1;
            alignas(0x40) UCSysConfig config;
            memset(&config, 0, sizeof(config));
            strncpy(config.name, "cafe.language", sizeof(config.name) - 1);
            config.dataType = UC_DATATYPE_UNSIGNED_INT;
            config.dataSize = sizeof(language);
            config.data     = &language;
            if (UCReadSysConfig(handle, 1, &config) == 0 && language < 12) {
                cachedLanguage = (int32_t) language;
            }
            UCClose(handle);
        }
        DEBUG_FUNCTION_LINE("System language: %d", cachedLanguage);
        return cachedLanguage;
    }

    std::string fixedString(const char *str, size_t maxLen) {
        // the fields are fixed size arrays, never trust them to be terminated
        return std::string(str, strnlen(str, maxLen));
    }

    bool readTitleName(uint64_t titleId, std::string &outName, std::string &outLongName) {
        auto *meta = (ACPMetaXml *) memalign(0x40, META_XML_BUFFER_SIZE);
        if (!meta) {
            return false;
        }
        memset(meta, 0, META_XML_BUFFER_SIZE);

        bool result = false;
        if (ACPGetTitleMetaXml(titleId, meta) >= 0) {
            const char *names[12] = {
                    meta->shortname_ja, meta->shortname_en, meta->shortname_fr,
                    meta->shortname_de, meta->shortname_it, meta->shortname_es,
                    meta->shortname_zhs, meta->shortname_ko, meta->shortname_nl,
                    meta->shortname_pt, meta->shortname_ru, meta->shortname_zht};
            const char *name = names[getSystemLanguage()];
            if (name[0] == '\0') {
                name = meta->shortname_en;
            }
            if (name[0] == '\0') {
                name = meta->shortname_ja;
            }
            if (name[0] != '\0') {
                outName = fixedString(name, sizeof(meta->shortname_en));
                result  = true;
            }

            const char *longNames[12] = {
                    meta->longname_ja, meta->longname_en, meta->longname_fr,
                    meta->longname_de, meta->longname_it, meta->longname_es,
                    meta->longname_zhs, meta->longname_ko, meta->longname_nl,
                    meta->longname_pt, meta->longname_ru, meta->longname_zht};
            const char *longName = longNames[getSystemLanguage()];
            if (longName[0] == '\0') {
                longName = meta->longname_en;
            }
            outLongName = fixedString(longName, sizeof(meta->longname_en));
        }
        free(meta);
        return result;
    }

    constexpr const char *NAME_CACHE_DIR  = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *NAME_CACHE_FILE = "fs:/vol/external01/wiiu/launchiine/names.txt";

    struct CachedName {
        std::string name;
        std::string longName;
    };

    std::string sanitizeCacheField(const std::string &in) {
        std::string out = in;
        for (auto &c : out) {
            if (c == '\t' || c == '\n' || c == '\r') {
                c = ' ';
            }
        }
        return out;
    }

    //! "TITLEID\tshort name\tlong name\n" per line
    std::map<uint64_t, CachedName> loadNameCache() {
        std::map<uint64_t, CachedName> result;
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(NAME_CACHE_FILE, &buffer, &size) <= 0 || buffer == nullptr) {
            return result;
        }
        std::string text((const char *) buffer, size);
        free(buffer);

        size_t lineStart = 0;
        while (lineStart < text.size()) {
            size_t lineEnd = text.find('\n', lineStart);
            if (lineEnd == std::string::npos) {
                lineEnd = text.size();
            }
            std::string line = text.substr(lineStart, lineEnd - lineStart);
            lineStart        = lineEnd + 1;

            size_t tab1 = line.find('\t');
            size_t tab2 = tab1 == std::string::npos ? std::string::npos : line.find('\t', tab1 + 1);
            if (tab2 == std::string::npos) {
                continue;
            }
            uint64_t titleId = strtoull(line.substr(0, tab1).c_str(), nullptr, 16);
            if (titleId != 0) {
                result[titleId] = {line.substr(tab1 + 1, tab2 - tab1 - 1), line.substr(tab2 + 1)};
            }
        }
        DEBUG_FUNCTION_LINE("Loaded %d cached names", (int) result.size());
        return result;
    }

    void saveNameCache(const std::map<uint64_t, CachedName> &names) {
        std::string text;
        char id[20];
        for (auto &entry : names) {
            snprintf(id, sizeof(id), "%016llX", (unsigned long long) entry.first);
            text += id;
            text += '\t';
            text += sanitizeCacheField(entry.second.name);
            text += '\t';
            text += sanitizeCacheField(entry.second.longName);
            text += '\n';
        }
        FSUtils::CreateSubfolder(NAME_CACHE_DIR);
        if (FSUtils::saveBufferToFile(NAME_CACHE_FILE, (void *) text.data(), text.size()) != (int32_t) text.size()) {
            DEBUG_FUNCTION_LINE("Failed to save the name cache");
        } else {
            DEBUG_FUNCTION_LINE("Saved %d names to the cache", (int) names.size());
        }
    }

    //! Re-encodes any TGA (uncompressed or RLE, 24/32 bit, any origin) as a plain
    //! uncompressed 32 bit TGA with every read bounds checked. libgd's TGA reader (the one
    //! libgui uses) can crash or hang on RLE or unusual files, so it only ever sees the
    //! simple case. Returns false for files that are not valid icons.
    bool sanitizeTga(const uint8_t *in, uint32_t inSize, std::vector<uint8_t> &out) {
        if (in == nullptr || inSize < 18) {
            return false;
        }
        uint32_t idLength     = in[0];
        uint32_t colorMapType = in[1];
        uint32_t imageType    = in[2];
        uint32_t cmapLength   = in[5] | (in[6] << 8);
        uint32_t cmapEntryBit = in[7];
        uint32_t width        = in[12] | (in[13] << 8);
        uint32_t height       = in[14] | (in[15] << 8);
        uint32_t bpp          = in[16];
        uint32_t descriptor   = in[17];

        if (imageType != 2 && imageType != 10) {
            return false; // only true color, raw or RLE
        }
        if (bpp != 24 && bpp != 32) {
            return false;
        }
        if (width == 0 || height == 0 || width > 1024 || height > 1024) {
            return false;
        }

        uint32_t offset = 18 + idLength;
        if (colorMapType == 1) {
            offset += cmapLength * ((cmapEntryBit + 7) / 8);
        }
        if (offset > inSize) {
            return false;
        }

        const uint32_t srcBytes   = bpp / 8;
        const uint32_t pixelCount = width * height;

        out.assign(18 + pixelCount * 4, 0);
        uint8_t *hdr = out.data();
        hdr[2]       = 2; // uncompressed true color
        hdr[12]      = width & 0xFF;
        hdr[13]      = width >> 8;
        hdr[14]      = height & 0xFF;
        hdr[15]      = height >> 8;
        hdr[16]      = 32;
        hdr[17]      = (descriptor & 0x30) | 8; // keep the origin, 8 alpha bits

        uint8_t *dst   = out.data() + 18;
        auto putPixel  = [&](const uint8_t *src) {
            dst[0] = src[0]; // B
            dst[1] = src[1]; // G
            dst[2] = src[2]; // R
            dst[3] = srcBytes == 4 ? src[3] : 0xFF;
            dst += 4;
        };

        uint32_t pos  = offset;
        uint32_t done = 0;
        if (imageType == 2) {
            if (pos + pixelCount * srcBytes > inSize) {
                return false;
            }
            for (; done < pixelCount; done++) {
                putPixel(in + pos);
                pos += srcBytes;
            }
            return true;
        }

        while (done < pixelCount) {
            if (pos >= inSize) {
                return false;
            }
            uint8_t packet = in[pos++];
            uint32_t count = (packet & 0x7F) + 1;
            if (count > pixelCount - done) {
                return false;
            }
            if (packet & 0x80) {
                if (pos + srcBytes > inSize) {
                    return false;
                }
                for (uint32_t i = 0; i < count; i++) {
                    putPixel(in + pos);
                }
                pos += srcBytes;
            } else {
                if (pos + count * srcBytes > inSize) {
                    return false;
                }
                for (uint32_t i = 0; i < count; i++) {
                    putPixel(in + pos);
                    pos += srcBytes;
                }
            }
            done += count;
        }
        return true;
    }

    //! Loader thread: reads and sanitizes the icon. No GX2 calls here.
    bool readTitleIcon(uint64_t titleId, const std::string &gamePath, std::vector<uint8_t> &outTga) {
        std::string filepath = "fs:" + gamePath + META_PATH + "/iconTex.tga";
        uint8_t *buffer      = nullptr;
        uint32_t bufferSize  = 0;
        if (FSUtils::LoadFileToMem(filepath.c_str(), &buffer, &bufferSize) <= 0 || buffer == nullptr) {
            DEBUG_FUNCTION_LINE("%016llX: no icon file", titleId);
            return false;
        }

        DEBUG_FUNCTION_LINE("%016llX: icon %u bytes, type %u, %ux%u, %u bpp, free MEM2 %u KiB", titleId, bufferSize,
                            bufferSize >= 18 ? buffer[2] : 0,
                            bufferSize >= 18 ? (buffer[12] | (buffer[13] << 8)) : 0,
                            bufferSize >= 18 ? (buffer[14] | (buffer[15] << 8)) : 0,
                            bufferSize >= 18 ? buffer[16] : 0,
                            MEMGetTotalFreeSizeForExpHeap(MEMGetBaseHeapHandle(MEM_BASE_HEAP_MEM2)) / 1024);

        bool valid = sanitizeTga(buffer, bufferSize, outTga);
        free(buffer);
        if (!valid) {
            DEBUG_FUNCTION_LINE("%016llX: unsupported or broken icon, skipped", titleId);
            outTga.clear();
            return false;
        }
        return true;
    }
} // namespace

GameList::GameList() = default;

GameList::~GameList() {
    //! MainWindow has already stopped and joined the loader at this point
    stopAsyncLoading = true;

    pendingLock.lock();
    pendingEvents.clear();
    pendingLock.unlock();
    renderQueue.clear();

    clear();
}

void GameList::clear() {
    lock();
    for (auto const &x : fullGameList) {
        if (x != nullptr) {
            if (x->imageData != nullptr) {
                AsyncExecutor::pushForDelete(x->imageData);
                x->imageData = nullptr;
            }
            delete x;
        }
    }
    fullGameList.clear();
    //! Clear memory of the vector completely
    std::vector<gameInfo *>().swap(fullGameList);
    unlock();
}

gameInfo *GameList::getGameInfo(uint64_t titleId) {
    gameInfo *result = nullptr;
    lock();
    for (auto &info : fullGameList) {
        if (titleId == info->titleId) {
            result = info;
            break;
        }
    }
    unlock();
    return result;
}

void GameList::queueEvent(PendingEvent &&event) {
    pendingLock.lock();
    pendingEvents.push_back(std::move(event));
    pendingLock.unlock();
}

void GameList::pollEvents() {
    pendingLock.lock();
    for (auto &event : pendingEvents) {
        renderQueue.push_back(std::move(event));
    }
    pendingEvents.clear();
    pendingLock.unlock();

    //! Creating a texture costs a few ms (decode + convert), limit it per frame
    constexpr int MAX_TEXTURES_PER_FRAME = 3;
    int texturesCreated                  = 0;

    while (!renderQueue.empty()) {
        PendingEvent &event = renderQueue.front();
        if (!event.iconTga.empty() && texturesCreated >= MAX_TEXTURES_PER_FRAME) {
            break; // next frame
        }

        switch (event.type) {
            case PendingEvent::LIST_CHANGED:
                titleListChanged(this);
                break;
            case PendingEvent::TITLE_UPDATED: {
                gameInfo *info = event.info;
                if (event.hasName) {
                    info->name     = event.name;
                    info->longName = event.longName;
                }
                if (!event.iconTga.empty()) {
                    texturesCreated++;
                    if (info->imageData == nullptr) {
                        auto *imageData = new GuiImageData(event.iconTga.data(), event.iconTga.size(), GX2_TEX_CLAMP_MODE_MIRROR);
                        if (imageData->getTexture() != nullptr) {
                            info->imageData = imageData;
                        } else {
                            DEBUG_FUNCTION_LINE("%016llX: creating the texture failed", info->titleId);
                            delete imageData;
                        }
                    }
                }
                titleUpdated(info);
                break;
            }
        }
        renderQueue.pop_front();
    }
}

int32_t GameList::readGameList() {
    MCPError mcp = MCP_Open();
    if (mcp < 0) {
        DEBUG_FUNCTION_LINE("MCP_Open failed");
        return 0;
    }

    MCPError titleCount = MCP_TitleCount(mcp);
    if (titleCount <= 0) {
        MCP_Close(mcp);
        return 0;
    }

    std::vector<struct MCPTitleListType> titles(titleCount);
    uint32_t realTitleCount = 0;

    static const std::vector<MCPAppType> menuAppTypes{
            MCP_APP_TYPE_GAME,
            MCP_APP_TYPE_GAME_WII,
            MCP_APP_TYPE_SYSTEM_APPS,
            MCP_APP_TYPE_SYSTEM_SETTINGS,
            MCP_APP_TYPE_FRIEND_LIST,
            MCP_APP_TYPE_MIIVERSE,
            MCP_APP_TYPE_ESHOP,
            MCP_APP_TYPE_BROWSER,
            MCP_APP_TYPE_DOWNLOAD_MANAGEMENT,
            MCP_APP_TYPE_ACCOUNT_APPS,
    };

    for (auto appType : menuAppTypes) {
        if (realTitleCount >= titles.size()) {
            break;
        }
        uint32_t titleCountByType = 0;
        MCPError err              = MCP_TitleListByAppType(mcp, appType, &titleCountByType, titles.data() + realTitleCount,
                                                           (titles.size() - realTitleCount) * sizeof(decltype(titles)::value_type));
        if (err < 0) {
            DEBUG_FUNCTION_LINE("MCP_TitleListByAppType failed for %08X", appType);
            continue; // skip this type instead of losing the whole list
        }
        realTitleCount += titleCountByType;
    }
    MCP_Close(mcp);

    titles.resize(std::min<uint32_t>(realTitleCount, titles.size()));

    std::vector<gameInfo *> newList;
    newList.reserve(titles.size());
    for (auto &title_candidate : titles) {
        // the same title can show up in more than one list, keep the first one
        bool duplicate = false;
        for (auto *existing : newList) {
            if (existing->titleId == title_candidate.titleId) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        auto *newGameInfo      = new gameInfo;
        newGameInfo->titleId   = title_candidate.titleId;
        newGameInfo->appType   = title_candidate.appType;
        newGameInfo->gamePath  = title_candidate.path;
        newGameInfo->name      = "<unknown>";
        newGameInfo->imageData = nullptr;
        newList.push_back(newGameInfo);
    }

    //! names from the last run, so the menu and search have them right away
    auto cache = loadNameCache();
    for (auto *info : newList) {
        auto it = cache.find(info->titleId);
        if (it != cache.end()) {
            info->name     = it->second.name;
            info->longName = it->second.longName;
        }
    }

    lock();
    fullGameList.swap(newList);
    int32_t cnt = fullGameList.size();
    unlock();

    // newList now holds the previous entries (normally none, load() only runs once)
    for (auto *old : newList) {
        delete old;
    }

    queueEvent(PendingEvent{PendingEvent::LIST_CHANGED});
    return cnt;
}

void GameList::loadTitleDetails(uint64_t priorityTitleId) {
    //! Take a snapshot so we don't hold the list lock while doing slow IO.
    //! The gameInfo pointers stay valid as long as the list is not cleared, which only
    //! happens in the destructor after this loader has stopped.
    struct Job {
        gameInfo *info;
        uint64_t titleId;
        std::string gamePath;
        std::string cachedName;
        std::string cachedLongName;
    };
    std::vector<Job> jobs;
    lock();
    for (auto *info : fullGameList) {
        jobs.push_back({info, info->titleId, info->gamePath, info->name, info->longName});
    }
    unlock();

    //! Load the page that is on screen first, then spread outwards (next page, previous
    //! page, ...). The focus follows the selection, so flipping pages while loading moves
    //! the loader along. Jobs are in list order, which is the order of the icons.
    constexpr int32_t ICONS_PER_PAGE = 15;
    if (getFocusTitle() == 0) {
        setFocusTitle(priorityTitleId);
    }
    std::vector<bool> doneJob(jobs.size(), false);
    auto pickNextJob = [&]() -> int32_t {
        uint64_t focus     = getFocusTitle();
        int32_t focusIndex = 0;
        for (size_t i = 0; i < jobs.size(); i++) {
            if (jobs[i].titleId == focus) {
                focusIndex = (int32_t) i;
                break;
            }
        }
        int32_t focusPage = focusIndex / ICONS_PER_PAGE;
        int32_t best      = -1;
        int32_t bestScore = 0;
        for (size_t i = 0; i < jobs.size(); i++) {
            if (doneJob[i]) {
                continue;
            }
            int32_t page = (int32_t) i / ICONS_PER_PAGE;
            int32_t diff = page - focusPage;
            //! same page first, then +1, -1, +2, -2, ... and within a page left to right
            int32_t rank  = diff == 0 ? 0 : (diff > 0 ? diff * 2 - 1 : -diff * 2);
            int32_t score = rank * ICONS_PER_PAGE + (int32_t) (i % ICONS_PER_PAGE);
            if (best < 0 || score < bestScore) {
                best      = (int32_t) i;
                bestScore = score;
            }
        }
        return best;
    };

    std::map<uint64_t, CachedName> freshNames;
    bool cacheChanged = false;

    OSTime start      = OSGetTime();
    OSTime nameTicks  = 0;
    OSTime iconTicks  = 0;
    uint32_t iconsOk  = 0;

    for (int32_t next = pickNextJob(); next >= 0; next = pickNextJob()) {
        if (stopAsyncLoading) {
            DEBUG_FUNCTION_LINE("Stop async title loading");
            return;
        }
        doneJob[next] = true;
        Job &job      = jobs[next];
        PendingEvent event{PendingEvent::TITLE_UPDATED};
        event.info = job.info;

        OSTime t0     = OSGetTime();
        event.hasName = readTitleName(job.titleId, event.name, event.longName);
        OSTime t1     = OSGetTime();
        bool hasIcon    = readTitleIcon(job.titleId, job.gamePath, event.iconTga);
        OSTime t2       = OSGetTime();
        nameTicks += t1 - t0;
        iconTicks += t2 - t1;
        if (hasIcon) {
            iconsOk++;
        }

        if (event.hasName) {
            freshNames[job.titleId] = {event.name, event.longName};
            if (event.name != job.cachedName || event.longName != job.cachedLongName) {
                cacheChanged = true;
            }
        }
        if (event.hasName || hasIcon) {
            queueEvent(std::move(event));
        }
    }

    DEBUG_FUNCTION_LINE("Loaded %d titles in %lld ms (names %lld ms, icons %lld ms), %u icons", (int) jobs.size(),
                        OSTicksToMilliseconds(OSGetTime() - start), OSTicksToMilliseconds(nameTicks),
                        OSTicksToMilliseconds(iconTicks), iconsOk);

    //! also rewrite when titles were removed
    if (cacheChanged || freshNames.size() != jobs.size()) {
        saveNameCache(freshNames);
    }
}

int32_t GameList::load(uint64_t priorityTitleId) {
    int32_t res = 0;
    if (!stopAsyncLoading) {
        lock();
        bool empty = fullGameList.empty();
        unlock();

        if (empty) {
            readGameList();
        }
        res = size();
        loadTitleDetails(priorityTitleId);
    }
    DEBUG_FUNCTION_LINE("Title loader finished (%d titles, stopped=%d)", res, (int) stopAsyncLoading);
    return res;
}
