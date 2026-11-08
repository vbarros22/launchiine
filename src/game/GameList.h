#ifndef GAME_LIST_H_
#define GAME_LIST_H_

#include <atomic>
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
    std::string gamePath;
    GuiImageData *imageData;
} gameInfo;

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
        focusTitle = titleId;
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
        bool hasName            = false;
        GuiImageData *imageData = nullptr;
    };

    int32_t readGameList();

    void loadTitleDetails(uint64_t priorityTitleId);

    void queueEvent(PendingEvent &&event);

    std::vector<gameInfo *> fullGameList;

    std::recursive_mutex _lock;

    std::mutex pendingLock;
    std::vector<PendingEvent> pendingEvents;

    std::atomic<bool> stopAsyncLoading{false};
    std::atomic<uint64_t> focusTitle{0};
};

#endif
