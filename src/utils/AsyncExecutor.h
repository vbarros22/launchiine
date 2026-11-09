#pragma once

#include "utils/logger.h"
#include <coreinit/cache.h>
#include <future>
#include <gui/GuiElement.h>
#include <deque>
#include <queue>
#include <thread>
#include <vector>

class AsyncExecutor {
public:
    static void pushForDelete(GuiElement *element) {
        if (!instance) {
            instance = new AsyncExecutor();
        }
        instance->pushForDeleteInternal(element);
    }

    static void execute(std::function<void()> func) {
        if (!instance) {
            instance = new AsyncExecutor();
        }
        instance->executeInternal(func);
    }

    //! Render thread, once per frame: deletes what was pushed at least two frames ago.
    //! GUI objects must not be deleted on another thread: their sigslot connections are
    //! not thread safe, and the GPU may still read a texture of the current frame.
    static void processDeletes() {
        if (instance) {
            instance->processDeletesInternal();
        }
    }

    //! Deletes everything that is queued right away. Only when nothing is rendered anymore,
    //! and before the subsystems the objects need (sound, libgui memory) are shut down.
    static void flushDeletes() {
        if (instance) {
            instance->flushDeletesInternal();
        }
    }

    static void destroyInstance() {
        if (instance) {
            delete instance;
            instance = nullptr;
        }
    }

private:
    static AsyncExecutor *instance;

    AsyncExecutor();

    ~AsyncExecutor();

    void pushForDeleteInternal(GuiElement *element);

    void processDeletesInternal();

    void flushDeletesInternal();

    void executeInternal(std::function<void()> func);

    std::recursive_mutex mutex;
    std::thread *thread;
    volatile bool exitThread = false;

    std::vector<std::future<void>> elements;

    std::recursive_mutex deleteListMutex;
    struct PendingDelete {
        GuiElement *element;
        uint32_t frame;
    };
    std::deque<PendingDelete> deleteList;
    uint32_t frameCounter = 0;
};
