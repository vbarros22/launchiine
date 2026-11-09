#pragma once

#include "game/GameList.h"
#include <gui/Gui.h>
#include <gui/sigslot.h>
#include <string>
#include <vector>

class GuiTitleBrowser : public GuiFrame {
public:
    GuiTitleBrowser(int32_t w, int32_t h, uint64_t GameIndex) : GuiFrame(w, h) {}

    virtual ~GuiTitleBrowser() {}

    virtual void setSelectedGame(uint64_t idx) = 0;

    virtual uint64_t getSelectedGame(void) = 0;

    virtual void OnGameTitleListUpdated(GameList *list) = 0;

    virtual void OnGameTitleUpdated(gameInfo *info) = 0;

    virtual void OnGameTitleAdded(gameInfo *info) = 0;

    //! show only titles whose name contains the filter (case insensitive), empty shows all
    virtual void setFilter(const std::string &filter) {}

    //! order of the titles on screen; the version changes whenever the order does
    virtual std::vector<uint64_t> getDisplayOrder() {
        return {};
    }

    virtual uint32_t getDisplayOrderVersion() {
        return 0;
    }

    //! show the content of a folder (0: main grid) without emitting folderOpened
    virtual void openFolder(uint64_t folder) {}

    //! folders or their content changed; select the first of preferred that is visible
    virtual void refreshFolders(const std::vector<uint64_t> &preferred = {}) {}

    //! the title next to titleId in the current view (to select once it moved away), 0 if none
    virtual uint64_t neighborOf(uint64_t titleId) {
        return 0;
    }

    //! short message in the title bar, until the selection changes
    virtual void showMessage(const std::string &message) {}

    //! Y on a title or a user folder
    sigslot::signal2<GuiTitleBrowser *, uint64_t> folderMenuRequested;

    sigslot::signal2<GuiTitleBrowser *, uint64_t> folderOpened;
    sigslot::signal1<GuiTitleBrowser *> folderMembershipChanged;

    sigslot::signal2<GuiTitleBrowser *, uint64_t> gameLaunchClicked;
    sigslot::signal2<GuiTitleBrowser *, uint64_t> gameSelectionChanged;
};
