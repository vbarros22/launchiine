#pragma once

#include "game/GameList.h"
#include <gui/Gui.h>
#include <gui/sigslot.h>
#include <string>

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

    sigslot::signal2<GuiTitleBrowser *, uint64_t> gameLaunchClicked;
    sigslot::signal2<GuiTitleBrowser *, uint64_t> gameSelectionChanged;
};
