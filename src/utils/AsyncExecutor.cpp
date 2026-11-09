#include "AsyncExecutor.h"
#include "utils/logger.h"

AsyncExecutor *AsyncExecutor::instance = nullptr;

void AsyncExecutor::pushForDeleteInternal(GuiElement *ptr) {
    deleteListMutex.lock();
    deleteList.push_back({ptr, frameCounter});
    deleteListMutex.unlock();
}

void AsyncExecutor::processDeletesInternal() {
    deleteListMutex.lock();
    frameCounter++;
    //! a destructor may push more elements, those wait for the next frames
    while (!deleteList.empty() && frameCounter - deleteList.front().frame >= 2) {
        GuiElement *ptr = deleteList.front().element;
        deleteList.pop_front();
        delete ptr;
    }
    deleteListMutex.unlock();
}

void AsyncExecutor::flushDeletesInternal() {
    deleteListMutex.lock();
    //! destructors may push more elements, the loop picks them up as well
    while (!deleteList.empty()) {
        GuiElement *ptr = deleteList.front().element;
        deleteList.pop_front();
        delete ptr;
    }
    deleteListMutex.unlock();
}

AsyncExecutor::AsyncExecutor() {
    thread = new std::thread([&]() {
        while (!exitThread) {
            mutex.lock();
            bool emptyList = elements.empty();
            auto it        = elements.begin();
            while (it != elements.end()) {
                auto future = it;
                auto status = future->wait_for(std::chrono::seconds(0));
                if (status == std::future_status::ready) {
                    it = elements.erase(it);
                } else {
                    ++it;
                }
            }
            if (!emptyList && elements.empty()) {
                DEBUG_FUNCTION_LINE("All tasks are done");
            }
            mutex.unlock();

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            DCFlushRange((void *) &exitThread, sizeof(exitThread));
        }
    });
}

AsyncExecutor::~AsyncExecutor() {
    exitThread = true;
    DCFlushRange((void *) &exitThread, sizeof(exitThread));
    thread->join();
    delete thread;

    //! normally empty already, see Application
    flushDeletesInternal();
}

void AsyncExecutor::executeInternal(std::function<void()> func) {
    if (elements.size() > 10) {
        DEBUG_FUNCTION_LINE("Warning, many tasks running currently");
        //std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    DEBUG_FUNCTION_LINE("Add new task");
    mutex.lock();
    elements.push_back(std::async(std::launch::async, func));
    mutex.unlock();
}
