#pragma once

#include <cstdint>

/**
 * Game update notice.
 *
 * The Wii U Menu asks Nintendo's version list server ("tagaya") which update version is the
 * latest one for every title and compares that with the update that is installed. launchiine
 * does the same on a worker thread:
 *  - the installed update versions come from MCP_TitleList (0005000E titles),
 *  - the latest versions are cached on the SD card, so the notice is there right after boot,
 *  - the version list is downloaded again at most every few hours.
 *
 * Nothing is installed here, it only tells which titles have an update.
 */
namespace Updates {
    //! Starts the worker thread, call once. Any thread.
    void start();

    //! Asks the worker to stop and waits for it (before launching a title / exiting).
    void stop();

    //! true if a newer update than the installed one exists for this game. Any thread.
    bool hasUpdate(uint64_t titleId);

    //! Changes whenever the result of hasUpdate may have changed. Any thread.
    uint32_t generation();
} // namespace Updates
