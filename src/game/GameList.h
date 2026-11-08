#ifndef GAME_LIST_H_
#define GAME_LIST_H_

#include <atomic>
#include <cstdio>
#include <deque>
#include <coreinit/cache.h>
#include <coreinit/mcp.h>
#include <gui/GuiImageData.h>
#include <gui/sigslot.h>
#include <mutex>
#include <stdint.h>
#include <string>
#include <vector>

typedef struct _gameInfo {
    uint64_t titleId;
    MCPAppType appType;
    std::string name;     // short name, shown in the menu
    std::string longName; // full name, only used for search ("The Legend of Zelda: ...")
    std::string productCode; // "WUP-P-ALZE", used to sort titles into folders
    std::string gamePath;
    GuiImageData *imageData;
    MCPTitleListType mcpInfo; // as reported by MCP_TitleList, needed to launch homebrew
} gameInfo;

//! Homebrew (.rpx/.wuhb in sd:/wiiu/apps) added by the Aroma homebrew_on_menu plugin
static inline bool isHomebrewTitle(uint64_t titleId) {
    return (titleId >> 32) == 0x0005000F;
}

//! System Settings, eShop, Miiverse, ... (00050010, 00050030)
static inline bool isSystemTitleId(uint64_t titleId) {
    uint32_t upper = titleId >> 32;
    return upper == 0x00050010 || upper == 0x00050030;
}

//! Directory with iconTex.tga, bootTvTex.tga, ... For homebrew this is a virtual path the
//! plugin serves through FSOpenFile, read it with FSUtils::LoadFileRawFS.
static inline std::string metaDirectoryFor(const gameInfo *info) {
    if (isHomebrewTitle(info->titleId)) {
        char path[64];
        snprintf(path, sizeof(path), "/vol/storage_mlc01/sys/title/%08X/%08X/meta", (uint32_t) (info->titleId >> 32), (uint32_t) info->titleId);
        return path;
    }
    return info->gamePath + "/meta";
}

/**
 * Threading model:
 *  - load() runs on a worker thread. It enumerates titles and loads names/icons,
 *    but it never touches the GUI and never mutates a gameInfo that is already
 *    visible to the GUI. Instead it queues events.
 *  - pollEvents() must be called from the render thread (MainWindow::process,
 *    which runs while the GUI lock is held). It applies the queued results to the
 *    gameInfo structs and emits the signals, so every slot runs on the render thread.
 */
class GameList {
public:
    GameList();

    ~GameList();

    int32_t size() {
        lock();
        int32_t res = fullGameList.size();
        unlock();
        return res;
    }

    int32_t gameCount() {
        return size();
    }

    gameInfo *at(int32_t i) {
        return operator[](i);
    }

    gameInfo *operator[](int32_t i) {
        lock();
        gameInfo *res = nullptr;
        if (i >= 0 && i < (int32_t) fullGameList.size()) {
            res = fullGameList[i];
        }
        unlock();
        return res;
    }

    gameInfo *getGameInfo(uint64_t titleId);

    void clear();

    std::vector<gameInfo *> &getFullGameList(void) {
        return fullGameList;
    }

    //! Worker thread only. Returns early once stopLoading() was called.
    //! Details are loaded starting at the page that holds priorityTitleId, then outwards
    //! from whatever page the focus title (see setFocusTitle) is on.
    int32_t load(uint64_t priorityTitleId = 0);

    //! Ask a running load() to stop after the current title. The caller has to wait for
    //! load() to return (MainWindow keeps the future of the loader thread).
    void stopLoading() {
        stopAsyncLoading = true;
    }

    //! Render thread only.
    void pollEvents();

    //! The loader prefers titles close to this one (the selection), any thread.
    void setFocusTitle(uint64_t titleId) {
        std::lock_guard<std::mutex> guard(focusLock);
        focusTitle = titleId;
    }

    uint64_t getFocusTitle() {
        std::lock_guard<std::mutex> guard(focusLock);
        return focusTitle;
    }

    //! Order of the icons on screen, so the loader can work page by page
    void setDisplayOrder(const std::vector<uint64_t> &order) {
        std::lock_guard<std::mutex> guard(focusLock);
        displayOrder = order;
    }

    std::vector<uint64_t> getDisplayOrder() {
        std::lock_guard<std::mutex> guard(focusLock);
        return displayOrder;
    }

    sigslot::signal1<GameList *> titleListChanged;
    sigslot::signal1<gameInfo *> titleUpdated;
    sigslot::signal1<gameInfo *> titleAdded;

    void lock() {
        _lock.lock();
    }

    void unlock() {
        _lock.unlock();
    }

protected:
    struct PendingEvent {
        enum Type {
            LIST_CHANGED,
            TITLE_UPDATED,
        } type;
        gameInfo *info          = nullptr;
        std::string name        = "";
        std::string longName    = "";
        std::string productCode = "";
        bool hasName            = false;
        //! Sanitized TGA. The texture is created from it on the render thread: creating a
        //! GuiImageData calls GX2Invalidate, which writes into the GX2 command buffer and must
        //! never run concurrently with rendering (that hung the GPU at random icons).
        std::vector<uint8_t> iconTga{};
    };

    int32_t readGameList();

    void loadTitleDetails(uint64_t priorityTitleId);

    void queueEvent(PendingEvent &&event);

    std::vector<gameInfo *> fullGameList;

    std::recursive_mutex _lock;

    std::mutex pendingLock;
    std::vector<PendingEvent> pendingEvents;

    //! render thread only: events taken from pendingEvents but not applied yet
    std::deque<PendingEvent> renderQueue;

    std::atomic<bool> stopAsyncLoading{false};
    //! no std::atomic<uint64_t>: 64 bit atomics need libatomic on the 32 bit PowerPC
    std::mutex focusLock;
    uint64_t focusTitle = 0;
    std::vector<uint64_t> displayOrder;
};

#endif
