/****************************************************************************
 * Copyright (C) 2015 Dimok
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#pragma once

#include "gui/GameIcon.h"
#include "gui/GuiDragListener.h"
#include "gui/GuiTitleBrowser.h"
#include "utils/AsyncExecutor.h"
#include "utils/logger.h"
#include <gui/GuiParticleImage.h>
#include <map>

class GuiIconGrid : public GuiTitleBrowser, public sigslot::has_slots<> {
public:
    GuiIconGrid(int32_t w, int32_t h, uint64_t selectedTitleId, bool sortByName);

    virtual ~GuiIconGrid();

    void setSelectedGame(uint64_t idx);

    uint64_t getSelectedGame(void);

    void update(GuiController *t);

    void draw(CVideo *pVideo);

    void process();

    void OnGameTitleListUpdated(GameList *list);

    void OnAddGameTitle(gameInfo *info);

    void OnGameTitleUpdated(gameInfo *info);

    void OnGameTitleAdded(gameInfo *info);

    void setFilter(const std::string &filter) override;

    void openFolder(uint64_t folder) override;

    void refreshFolders(const std::vector<uint64_t> &preferred = {}) override;

    uint64_t neighborOf(uint64_t titleId) override;

    void showMessage(const std::string &message) override {
        setTitleText(message);
    }

    std::vector<uint64_t> getDisplayOrder() override {
        return currentLayout();
    }

    uint32_t getDisplayOrderVersion() override {
        return displayOrderVersion;
    }

private:
    //! titles in display order; the user's order, or only the matches while a filter is active
    std::vector<uint64_t> currentLayout();

    uint64_t firstTitleInLayout();

    //! scroll by whole pages and take the selection along
    void changePage(int32_t delta);

    //! alphabetical order (system apps first), recomputed when names change
    void resortPositions();

    void setTitleText(const std::string &text);

    bool bResort                 = false;
    uint32_t displayOrderVersion = 0;

    //! folder whose content is shown, 0 for the main grid
    uint64_t currentFolder = 0;
    std::map<uint64_t, gameInfo *> folderInfos;      // fixed and user folders
    std::map<uint32_t, GuiImageData *> folderImages; // by color

    //! create / rename / remove the folder icons to match Folders
    void setupFolders();

    bool folderHasTitles(uint64_t folder);

    //! after the folder content changed: valid selection, page and redraw
    void afterFolderChange(uint64_t preferredSelection);

    void OnBackClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnFolderMenuClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    bool matchesFilter(const gameInfo *info) const;

    std::string filterText;
    std::vector<std::string> filterWords;

    static const int32_t MAX_ROWS = 3;
    static const int32_t MAX_COLS = 5;
    //! distance between two pages: a bit more than 5 columns, so the neighbouring pages
    //! peek in at the screen edges like in the Wii U Menu
    static const int32_t PAGE_STRIDE = MAX_COLS * 192 + 30;

    GuiImageData *backgroundData = nullptr;
    GuiImage *backgroundImage    = nullptr;
    GuiParticleImage particleBgImage;
    //! translucent wash in the open folder's color, like the Wii U Menu
    GuiImage folderTint;
    float folderTintAlpha  = 0.0f;
    uint64_t folderTintFor = 0;

    GuiSound *buttonClickSound;

    GuiText gameTitle;
    GuiText updateNotice; // "Update available" under the selected title
    GuiText bubbleText;   // name of the selected title, in the bubble
    GuiTrigger touchTrigger;
    GuiTrigger wpadTouchTrigger;
    GuiTrigger leftTrigger;
    GuiTrigger rightTrigger;
    GuiTrigger downTrigger;
    GuiTrigger upTrigger;
    GuiTrigger buttonATrigger;
    GuiTrigger buttonLTrigger;
    GuiTrigger buttonRTrigger;
    GuiTrigger buttonBTrigger;
    GuiTrigger buttonYTrigger;
    GuiButton backButton;
    GuiButton folderMenuButton;
    GuiButton leftButton;
    GuiButton rightButton;
    GuiButton downButton;
    GuiButton upButton;
    GuiButton launchButton;

    GuiImageData *arrowRightImageData;
    GuiImageData *arrowLeftImageData;
    GuiImage arrowRightImage;
    GuiImage arrowLeftImage;
    GuiButton arrowRightButton;
    GuiButton arrowLeftButton;

    GuiImageData noIcon;
    GuiImageData emptyIcon;

    void OnLeftArrowClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnRightArrowClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnLeftClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnRightClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnDownClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnUpClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnLaunchClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnGameButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnGameButtonPointedOn(GuiButton *button, const GuiController *controller);

    void OnGameButtonPointedOff(GuiButton *button, const GuiController *controller);

    void updateButtonPositions();

    int32_t offsetForTitleId(uint64_t titleId);


    int32_t curPage = 0;
    uint32_t pageCount = 1;
    GuiImageData *pageDotData       = nullptr;
    GuiImageData *pageDotActiveData = nullptr;
    GuiImage *pageDot               = nullptr;
    GuiImage *pageDotActive         = nullptr;
    void drawPageDots(CVideo *pVideo);
    int32_t listOffset;
    uint64_t selectedGame;
    int32_t currentLeftPosition;
    int32_t targetLeftPosition;
    uint32_t gameLaunchTimer;
    bool bUpdatePositions         = false;

    class GameInfoContainer {
    public:
        GameInfoContainer(GuiButton *button, GameIcon *image, gameInfo *info) {
            this->image  = image;
            this->info   = info;
            this->button = button;
        }

        ~GameInfoContainer() {
            if (button != nullptr) {
                AsyncExecutor::pushForDelete(button);
            }
            if (image != nullptr) {
                AsyncExecutor::pushForDelete(image);
            }
            if (badge != nullptr) {
                AsyncExecutor::pushForDelete(badge);
            }
        }

        void updateImageData() {
            if (image != nullptr && info != nullptr && info->imageData != nullptr) {
                image->setImageData(info->imageData);
            }
        }

        GameIcon *image;
        gameInfo *info;
        GuiButton *button;
        GuiImage *badge = nullptr; // "update available", drawn as the button icon
    };

    //! name bubble above (top row: below) the selected icon, like the Wii U Menu
    void drawNameBubble(CVideo *pVideo);
    GuiImageData *bubbleData[5] = {};
    GuiImage *bubbleLeft     = nullptr;
    GuiImage *bubbleMid      = nullptr;
    GuiImage *bubbleRight    = nullptr;
    GuiImage *bubblePtrDown  = nullptr;
    GuiImage *bubblePtrUp    = nullptr;
    std::string bubbleName;      // what the bubble should show
    std::string bubbleShownName; // what bubbleText holds

    //! shows or hides the update badge of a title, render thread
    void setUpdateBadge(GameInfoContainer *container, bool show);

    GuiImageData *updateBadgeData = nullptr;
    uint32_t updatesSeen = 0;
    bool noticeShown     = false;
    bool messageShown    = false;

    std::recursive_mutex positionMutex;
    std::recursive_mutex containerMutex;
    std::map<uint64_t, GameInfoContainer *> gameInfoContainers;
    std::vector<uint64_t> position;
    std::vector<GuiButton *> positionButtons;

    std::vector<GuiImage *> emptyIcons;
    std::vector<GuiButton *> emptyButtons;
};
