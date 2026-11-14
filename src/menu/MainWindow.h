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
#ifndef _MAIN_WINDOW_H_
#define _MAIN_WINDOW_H_

#include "AccountSelectFrame.h"
#include "KeyboardHelper.h"
#include "ListMenuFrame.h"
#include "game/Accounts.h"
#include "MainDrcButtonsFrame.h"
#include "game/GameList.h"
#include "gui/GuiTitleBrowser.h"
#include <gui/Gui.h>
#include <future>
#include <queue>
#include <vector>

class CVideo;

class MainWindow : public sigslot::has_slots<> {
public:
    MainWindow(int32_t w, int32_t h);

    virtual ~MainWindow();

    void appendTv(GuiElement *e) {
        if (!e)
            return;

        removeTv(e);
        tvElements.push_back(e);
    }

    void appendDrc(GuiElement *e) {
        if (!e)
            return;

        removeDrc(e);
        drcElements.push_back(e);
    }

    void append(GuiElement *e) {
        appendTv(e);
        appendDrc(e);
    }

    void insertTv(uint32_t pos, GuiElement *e) {
        if (!e)
            return;

        removeTv(e);
        tvElements.insert(tvElements.begin() + pos, e);
    }

    void insertDrc(uint32_t pos, GuiElement *e) {
        if (!e)
            return;

        removeDrc(e);
        drcElements.insert(drcElements.begin() + pos, e);
    }

    void insert(uint32_t pos, GuiElement *e) {
        insertTv(pos, e);
        insertDrc(pos, e);
    }

    void removeTv(GuiElement *e) {
        for (uint32_t i = 0; i < tvElements.size(); ++i) {
            if (e == tvElements[i]) {
                tvElements.erase(tvElements.begin() + i);
                break;
            }
        }
    }

    void removeDrc(GuiElement *e) {
        for (uint32_t i = 0; i < drcElements.size(); ++i) {
            if (e == drcElements[i]) {
                drcElements.erase(drcElements.begin() + i);
                break;
            }
        }
    }

    void remove(GuiElement *e) {
        removeTv(e);
        removeDrc(e);
    }

    void removeAll() {
        tvElements.clear();
        drcElements.clear();
    }

    void drawDrc(CVideo *video);

    void drawTv(CVideo *video);

    void update(GuiController *controller);

    void updateEffects();

    void process();

    bool isKeyboardOpen() const;

    bool isInputBlocked() const {
        return inputCooldown > 0;
    }

    void lockGUI() {
        guiMutex.lock();
    }

    void unlockGUI() {
        guiMutex.unlock();
    }

private:
    void SetupMainView(void);

    void OnOpenEffectFinish(GuiElement *element);

    void OnCloseEffectFinish(GuiElement *element);

    bool OnGameLaunch(uint64_t titleId);

    //! stop the background title loader and wait for it, must happen before we leave the foreground for good
    void stopTitleLoader();

    static uint64_t loadLastTitle();

    static void saveLastTitle(uint64_t titleId);

    void OnGameLaunchSplashScreenFinished(GuiElement *element, gameInfo *info, bool launchGame);

    void OnGameLaunchSplashScreen(GuiTitleBrowser *element, uint64_t titleId);

    void OnGameSelectionChange(GuiTitleBrowser *element, uint64_t titleId);

    void connectFolderSignals();

    //! current user (top right) and the user picker
    void setupUserButton();

    void openAccountSelect(bool allowCancel);

    void closeAccountSelect();

    void OnUserButtonClicked(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    void OnAccountChosen(AccountSelectFrame *frame, uint8_t slot);

    void OnAccountSelectCancelled(AccountSelectFrame *frame);

    void OnFolderOpened(GuiTitleBrowser *element, uint64_t folder);

    void OnFolderMembershipChanged(GuiTitleBrowser *element);

    //! Y: move a title / rename or delete a user folder
    void OnFolderMenuRequested(GuiTitleBrowser *element, uint64_t titleId);

    void openListMenu(const std::string &title, const std::vector<std::string> &options);

    void closeListMenu();

    void OnListMenuChosen(ListMenuFrame *menu, int32_t index);

    void OnListMenuCancelled(ListMenuFrame *menu);

    void moveTitle(uint64_t titleId, uint64_t folder);

    //! refresh both screens and select the first visible of preferred
    void refreshFolderViews(const std::vector<uint64_t> &preferred, const std::string &message);

    std::string titleName(uint64_t titleId);

    void openTextKeyboard(int32_t maxTextLength);

    void OnSettingsButtonClicked(GuiElement *element);
    void openSystemSettings();
    //! starts the original Wii U Menu once (needs the launchiine helper plugin to come back)
    void openOriginalMenu();

    void OnLayoutSwitchClicked(GuiElement *element);

    void OnLayoutSwitchEffectFinish(GuiElement *element);

    void OnGameListFilterButtonClicked(GuiElement *element);

    void OnGameTitleListChanged(GameList *list);

    void OnGameTitleUpdated(gameInfo *info);

    void OnGameTitleAdded(gameInfo *info);

    int32_t width, height;
    std::vector<GuiElement *> drcElements;
    std::vector<GuiElement *> tvElements;

    GuiSound *gameClickSound;

    MainDrcButtonsFrame *mainSwitchButtonFrame;

    GuiTitleBrowser *currentTvFrame;
    GuiTitleBrowser *currentDrcFrame;

    GuiImageData *pointerImgData[4];
    GuiImage *pointerImg[4];
    bool pointerValid[4];

    GameList gameList;
    std::future<void> titleLoader;
    uint64_t startTitle              = 0;
    uint32_t displayOrderVersionSent = 0xFFFFFFFF;

    std::recursive_mutex guiMutex;
    KeyboardHelper *keyboardInstance = nullptr;

    AccountSelectFrame *accountFrame = nullptr;

    enum class MenuKind { None, MoveTitle, FolderOptions, FolderColor, ConfirmDelete, Settings };
    enum class KeyboardPurpose { Search, NewFolder, RenameFolder };
    //! special destination in the "Move to" menu
    static constexpr uint64_t NEW_FOLDER = 1;
    //! actions of the folder settings menu (kept in menuDestinations)
    static constexpr uint64_t FOLDER_ACTION_COLOR  = 1;
    static constexpr uint64_t FOLDER_ACTION_RENAME = 2;
    static constexpr uint64_t FOLDER_ACTION_DELETE = 3;

    ListMenuFrame *listMenu          = nullptr;
    MenuKind menuKind                = MenuKind::None;
    uint64_t menuTarget              = 0; // the title or folder the menu is about
    uint64_t menuNeighbor            = 0; // selected after the title moved away
    std::vector<uint64_t> menuDestinations;
    KeyboardPurpose keyboardPurpose  = KeyboardPurpose::Search;
    int32_t pendingKeyboardFrames    = 0; // open the keyboard after the A press is released
    int32_t pendingKeyboardMaxLength = -1;
    GuiFrame *userFrame              = nullptr;
    GuiButton *userButton            = nullptr;
    GuiImageData *userMiiData        = nullptr;
    GuiImage *userMiiImage           = nullptr;
    GuiImageData *userFrameData      = nullptr;
    GuiImage *userFrameImage         = nullptr;
    GuiTrigger userTouchTrigger{GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH};
    GuiTrigger userWpadTrigger{GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5, GuiTrigger::BUTTON_A};
    GuiTrigger userXTrigger{GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_X, true};
    bool launchInProgress            = false;
    uint32_t inputCooldown           = 0;
};

#endif //_MAIN_WINDOW_H_
