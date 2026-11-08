#include <algorithm>
#include <coreinit/cache.h>
#include <coreinit/mcp.h>
#include <coreinit/userconfig.h>
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

    bool readTitleName(uint64_t titleId, std::string &outName) {
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
                // the fields are fixed size arrays, never trust them to be terminated
                outName = std::string(name, strnlen(name, sizeof(meta->shortname_en)));
                result  = true;
            }
        }
        free(meta);
        return result;
    }

    GuiImageData *readTitleIcon(const std::string &gamePath) {
        std::string filepath = "fs:" + gamePath + META_PATH + "/iconTex.tga";
        uint8_t *buffer      = nullptr;
        uint32_t bufferSize  = 0;
        if (FSUtils::LoadFileToMem(filepath.c_str(), &buffer, &bufferSize) <= 0 || buffer == nullptr) {
            return nullptr;
        }
        auto *imageData = new GuiImageData(buffer, bufferSize, GX2_TEX_CLAMP_MODE_MIRROR);
        //! free original image buffer which is converted to texture now and not needed anymore
        free(buffer);
        return imageData;
    }
} // namespace

GameList::GameList() = default;

GameList::~GameList() {
    //! MainWindow has already stopped and joined the loader at this point
    stopAsyncLoading = true;

    pendingLock.lock();
    for (auto &event : pendingEvents) {
        delete event.imageData;
    }
    pendingEvents.clear();
    pendingLock.unlock();

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
    std::vector<PendingEvent> events;
    pendingLock.lock();
    events.swap(pendingEvents);
    pendingLock.unlock();

    for (auto &event : events) {
        switch (event.type) {
            case PendingEvent::LIST_CHANGED:
                titleListChanged(this);
                break;
            case PendingEvent::TITLE_UPDATED: {
                gameInfo *info = event.info;
                if (event.hasName) {
                    info->name = event.name;
                }
                if (event.imageData != nullptr) {
                    if (info->imageData == nullptr) {
                        info->imageData = event.imageData;
                    } else {
                        // should not happen, but never leak or replace a texture that might be drawn
                        delete event.imageData;
                    }
                    event.imageData = nullptr;
                }
                titleUpdated(info);
                break;
            }
        }
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

void GameList::loadTitleDetails() {
    //! Take a snapshot so we don't hold the list lock while doing slow IO.
    //! The gameInfo pointers stay valid as long as the list is not cleared, which only
    //! happens in the destructor after this loader has stopped.
    struct Job {
        gameInfo *info;
        uint64_t titleId;
        std::string gamePath;
    };
    std::vector<Job> jobs;
    lock();
    for (auto *info : fullGameList) {
        jobs.push_back({info, info->titleId, info->gamePath});
    }
    unlock();

    //! Pass 1: names only. Cheap compared to decoding icons, and it makes search work
    //! for every title as early as possible.
    for (auto &job : jobs) {
        if (stopAsyncLoading) {
            DEBUG_FUNCTION_LINE("Stop async title loading (names)");
            return;
        }
        PendingEvent event{PendingEvent::TITLE_UPDATED};
        event.info    = job.info;
        event.hasName = readTitleName(job.titleId, event.name);
        DEBUG_FUNCTION_LINE("%016llX \"%s\"", job.titleId, event.hasName ? event.name.c_str() : "?");
        if (event.hasName) {
            queueEvent(std::move(event));
        }
    }
    DEBUG_FUNCTION_LINE("All names loaded");

    //! Pass 2: icons, in list order so the first pages fill up first.
    for (auto &job : jobs) {
        if (stopAsyncLoading) {
            DEBUG_FUNCTION_LINE("Stop async title loading (icons)");
            return;
        }
        PendingEvent event{PendingEvent::TITLE_UPDATED};
        event.info      = job.info;
        event.imageData = readTitleIcon(job.gamePath);
        if (event.imageData != nullptr) {
            queueEvent(std::move(event));
        } else {
            DEBUG_FUNCTION_LINE("%016llX has no icon", job.titleId);
        }
    }
    DEBUG_FUNCTION_LINE("All icons loaded");
}

int32_t GameList::load() {
    int32_t res = 0;
    if (!stopAsyncLoading) {
        lock();
        bool empty = fullGameList.empty();
        unlock();

        if (empty) {
            readGameList();
        }
        res = size();
        loadTitleDetails();
    }
    DEBUG_FUNCTION_LINE("Title loader finished (%d titles, stopped=%d)", res, (int) stopAsyncLoading);
    return res;
}
