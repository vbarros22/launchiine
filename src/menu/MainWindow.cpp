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
#include "MainWindow.h"
#include "Application.h"
#include "utils/StringTools.h"
#include "utils/logger.h"

#include "GameSplashScreen.h"
#include "gui/GuiIconGrid.h"
#include "gui/GuiTitleBrowser.h"
#include "resources/Resources.h"
#include "fs/FSUtils.h"
#include "utils/AsyncExecutor.h"
#include "game/Folders.h"
#include "game/Updates.h"
#include <coreinit/title.h>
#include <cinttypes>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <malloc.h>
#include <future>
#include <nn/acp/title.h>
#include <sysapp/launch.h>

MainWindow::MainWindow(int32_t w, int32_t h)
    : width(w), height(h), gameClickSound(Resources::GetSound("game_click.mp3")), mainSwitchButtonFrame(nullptr), currentTvFrame(nullptr), currentDrcFrame(nullptr) {
    for (int32_t i = 0; i < 4; i++) {
        std::string filename = StringTools::strfmt("player%i_point.png", i + 1);
        pointerImgData[i]    = Resources::GetImageData(filename.c_str());
        pointerImg[i]        = new GuiImage(pointerImgData[i]);
        pointerImg[i]->setScale(1.5f);
        pointerValid[i] = false;
    }
    SetupMainView();
    gameList.titleListChanged.connect(this, &MainWindow::OnGameTitleListChanged);
    gameList.titleUpdated.connect(this, &MainWindow::OnGameTitleUpdated);
    gameList.titleAdded.connect(this, &MainWindow::OnGameTitleAdded);
    //! own thread (not the AsyncExecutor) so we can stop and join it before launching a title
    titleLoader = std::async(std::launch::async, [this] { gameList.load(startTitle); });
    Updates::start();

    setupUserButton();
    //! like the Wii U Menu: no default user, ask who is playing (once per boot)
    if (Accounts::shouldAskAtStart()) {
        openAccountSelect(false);
    }
}

void MainWindow::setupUserButton() {
    auto accounts   = Accounts::list();
    uint8_t current = Accounts::currentSlot();
    DEBUG_FUNCTION_LINE("%d accounts, current slot %u, default slot %u", (int) accounts.size(), current, Accounts::defaultSlot());
    const Accounts::Account *account = nullptr;
    for (auto &a : accounts) {
        if (a.slot == current) {
            account = &a;
        }
    }
    if (account == nullptr) {
        return;
    }

    //! a framed tile with the Mii, like the user icon of the Wii U Menu
    userFrameData  = Resources::GetImageData("userFrame.png");
    userFrameImage = new GuiImage(userFrameData);
    userButton     = new GuiButton(userFrameImage->getWidth(), userFrameImage->getHeight());
    userButton->setImage(userFrameImage);
    if (!account->miiTga.empty()) {
        userMiiData = new GuiImageData(account->miiTga.data(), account->miiTga.size(), GX2_TEX_CLAMP_MODE_MIRROR);
        if (userMiiData->getTexture()) {
            userMiiImage = new GuiImage(userMiiData);
            userMiiImage->setScale(0.47f);
            userMiiImage->setAlignment(ALIGN_CENTER | ALIGN_MIDDLE);
            userButton->setIcon(userMiiImage);
        }
    }

    userButton->setAlignment(ALIGN_RIGHT | ALIGN_TOP);
    userButton->setPosition(-16, -4);
    userButton->setTrigger(&userTouchTrigger);
    userButton->setTrigger(&userWpadTrigger);
    userButton->setTrigger(&userXTrigger);
    userButton->setEffectGrow();
    userButton->clicked.connect(this, &MainWindow::OnUserButtonClicked);
    //! alignment is relative to the parent: without a full screen frame the button would
    //! sit in the middle of the screen
    userFrame = new GuiFrame(width, height);
    userFrame->append(userButton);
    append(userFrame);
}

void MainWindow::OnUserButtonClicked(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    openAccountSelect(true);
}

void MainWindow::openAccountSelect(bool allowCancel) {
    if (accountFrame != nullptr || launchInProgress) {
        return;
    }
    auto accounts = Accounts::list();
    if (accounts.empty()) {
        DEBUG_FUNCTION_LINE("No accounts found");
        return;
    }
    accountFrame = new AccountSelectFrame(width, height, accounts, Accounts::currentSlot(), allowCancel);
    accountFrame->accountChosen.connect(this, &MainWindow::OnAccountChosen);
    accountFrame->cancelled.connect(this, &MainWindow::OnAccountSelectCancelled);
    append(accountFrame);
}

void MainWindow::closeAccountSelect() {
    if (accountFrame == nullptr) {
        return;
    }
    accountFrame->accountChosen.disconnect(this);
    accountFrame->cancelled.disconnect(this);
    remove(accountFrame);
    AsyncExecutor::pushForDelete(accountFrame);
    accountFrame = nullptr;
    //! the A/B press that closed the picker must not reach the grid
    inputCooldown = 15;
}

void MainWindow::OnAccountChosen(AccountSelectFrame *frame, uint8_t slot) {
    Accounts::rememberChosenThisBoot();
    if (slot == Accounts::currentSlot()) {
        closeAccountSelect();
        return;
    }
    DEBUG_FUNCTION_LINE("Switching to account slot %u", slot);
    launchInProgress = true;
    gameList.setPaused(true);
    stopTitleLoader();
    if (!Accounts::switchTo(slot)) {
        DEBUG_FUNCTION_LINE("Switching account failed");
        closeAccountSelect();
        launchInProgress = false;
    }
}

void MainWindow::OnAccountSelectCancelled(AccountSelectFrame *frame) {
    closeAccountSelect();
}

namespace {
    constexpr const char *SETTINGS_DIR    = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *LAST_TITLE_FILE = "fs:/vol/external01/wiiu/launchiine/lastTitle.txt";
} // namespace

uint64_t MainWindow::loadLastTitle() {
    uint8_t *buffer = nullptr;
    uint32_t size   = 0;
    if (FSUtils::LoadFileToMem(LAST_TITLE_FILE, &buffer, &size) <= 0 || buffer == nullptr) {
        return 0;
    }
    std::string text((const char *) buffer, size);
    free(buffer);
    uint64_t titleId = strtoull(text.c_str(), nullptr, 16);
    DEBUG_FUNCTION_LINE("Last title: %016llX", titleId);
    return titleId;
}

void MainWindow::saveLastTitle(uint64_t titleId) {
    if (titleId == 0) {
        return;
    }
    FSUtils::CreateSubfolder(SETTINGS_DIR);
    char text[32];
    int len = snprintf(text, sizeof(text), "%016" PRIX64 "\n", titleId);
    if (FSUtils::saveBufferToFile(LAST_TITLE_FILE, text, len) != len) {
        DEBUG_FUNCTION_LINE("Failed to save last title");
    }
}

void MainWindow::stopTitleLoader() {
    gameList.stopLoading();
    Updates::stop();
    if (titleLoader.valid()) {
        DEBUG_FUNCTION_LINE("Waiting for the title loader to stop");
        titleLoader.wait();
        DEBUG_FUNCTION_LINE("Title loader stopped");
    }
}

MainWindow::~MainWindow() {
    stopTitleLoader();
    gameList.titleListChanged.disconnect(this);
    gameList.titleUpdated.disconnect(this);
    gameList.titleAdded.disconnect(this);
    while (!tvElements.empty()) {
        delete tvElements[0];
        remove(tvElements[0]);
    }
    while (!drcElements.empty()) {
        delete drcElements[0];
        remove(drcElements[0]);
    }
    for (int32_t i = 0; i < 4; i++) {
        delete pointerImg[i];
        Resources::RemoveImageData(pointerImgData[i]);
    }

    Resources::RemoveSound(gameClickSound);

    delete keyboardInstance;
    keyboardInstance = nullptr;

    //! user button and picker; textures are freed with the process on exit anyway
    if (accountFrame) {
        remove(accountFrame);
        accountFrame = nullptr;
    }
    if (userFrame) {
        remove(userFrame);
    }
}

void MainWindow::updateEffects() {
    //! dont read behind the initial elements in case one was added
    uint32_t tvSize  = tvElements.size();
    uint32_t drcSize = drcElements.size();

    for (uint32_t i = 0; (i < drcSize) && (i < drcElements.size()); ++i) {
        drcElements[i]->updateEffects();
    }

    //! only update TV elements that are not updated yet because they are on DRC
    for (uint32_t i = 0; (i < tvSize) && (i < tvElements.size()); ++i) {
        uint32_t n;
        for (n = 0; (n < drcSize) && (n < drcElements.size()); n++) {
            if (tvElements[i] == drcElements[n])
                break;
        }
        if (n == drcElements.size()) {
            tvElements[i]->updateEffects();
        }
    }
}

void MainWindow::process() {
    //! apply results of the background title loader on this (the render) thread
    gameList.pollEvents();

    if (currentDrcFrame && currentDrcFrame->getDisplayOrderVersion() != displayOrderVersionSent) {
        displayOrderVersionSent = currentDrcFrame->getDisplayOrderVersion();
        gameList.setDisplayOrder(currentDrcFrame->getDisplayOrder());
    }

    if (inputCooldown > 0) {
        inputCooldown--;
    }

    //! dont read behind the initial elements in case one was added
    uint32_t tvSize  = tvElements.size();
    uint32_t drcSize = drcElements.size();

    for (uint32_t i = 0; (i < drcSize) && (i < drcElements.size()); ++i) {
        drcElements[i]->process();
    }

    //! only update TV elements that are not updated yet because they are on DRC
    for (uint32_t i = 0; (i < tvSize) && (i < tvElements.size()); ++i) {
        uint32_t n;
        for (n = 0; (n < drcSize) && (n < drcElements.size()); n++) {
            if (tvElements[i] == drcElements[n])
                break;
        }
        if (n == drcElements.size()) {
            tvElements[i]->process();
        }
    }

    if (pendingKeyboardFrames > 0 && --pendingKeyboardFrames == 0) {
        openTextKeyboard(pendingKeyboardMaxLength);
    }

    if (keyboardInstance != nullptr && keyboardInstance->isReady()) {
        //! must run every frame, also while the keyboard is fading out
        if (keyboardInstance->checkResult()) {
            std::string result = keyboardInstance->getResult();
            bool confirmed     = keyboardInstance->wasConfirmed();

            currentTvFrame->clearState(GuiElement::STATE_DISABLED);
            currentDrcFrame->clearState(GuiElement::STATE_DISABLED);
            mainSwitchButtonFrame->clearState(GuiElement::STATE_DISABLED);
            inputCooldown = 15;

            if (keyboardPurpose == KeyboardPurpose::Search) {
                if (confirmed) {
                    DEBUG_FUNCTION_LINE("Search filter: \"%s\"", result.c_str());
                    currentTvFrame->setFilter(result);
                    if (currentDrcFrame != currentTvFrame) {
                        currentDrcFrame->setFilter(result);
                    }
                }
            } else {
                //! folder names: one line, no surrounding spaces
                std::string name;
                for (char c : result) {
                    name += (c == '\t' || c == '\n' || c == '\r') ? ' ' : c;
                }
                name.erase(0, name.find_first_not_of(' '));
                name.erase(name.find_last_not_of(' ') + 1);

                if (!confirmed || name.empty()) {
                    // nothing to do
                } else if (keyboardPurpose == KeyboardPurpose::NewFolder) {
                    uint64_t folder = Folders::createFolder(name);
                    moveTitle(menuTarget, folder);
                } else {
                    Folders::renameFolder(menuTarget, name);
                    refreshFolderViews({menuTarget}, "Renamed to \"" + name + "\"");
                }
            }
            keyboardPurpose = KeyboardPurpose::Search;
        }
    }
}

bool MainWindow::isKeyboardOpen() const {
    return keyboardInstance != nullptr && keyboardInstance->isOpen();
}

void MainWindow::OnGameTitleListChanged(GameList *list) {
    currentTvFrame->OnGameTitleListUpdated(list);
    if (currentTvFrame != currentDrcFrame) {
        currentDrcFrame->OnGameTitleListUpdated(list);
    }
}

void MainWindow::OnGameTitleUpdated(gameInfo *info) {
    currentTvFrame->OnGameTitleUpdated(info);
    if (currentTvFrame != currentDrcFrame) {
        currentDrcFrame->OnGameTitleUpdated(info);
    }
}

void MainWindow::OnGameTitleAdded(gameInfo *info) {
    currentTvFrame->OnGameTitleAdded(info);
    if (currentTvFrame != currentDrcFrame) {
        currentDrcFrame->OnGameTitleAdded(info);
    }
}

void MainWindow::update(GuiController *controller) {
    //! dont read behind the initial elements in case one was added
    //uint32_t tvSize = tvElements.size();

    if (accountFrame != nullptr) {
        //! the user picker takes all input while it is open
        accountFrame->update(controller);
    } else if (listMenu != nullptr) {
        listMenu->update(controller);
    } else if (controller->chan & GuiTrigger::CHANNEL_1) {
        uint32_t drcSize = drcElements.size();

        for (uint32_t i = 0; (i < drcSize) && (i < drcElements.size()); ++i) {
            drcElements[i]->update(controller);
        }
    } else {
        uint32_t tvSize = tvElements.size();

        for (uint32_t i = 0; (i < tvSize) && (i < tvElements.size()); ++i) {
            tvElements[i]->update(controller);
        }
    }

    //    //! only update TV elements that are not updated yet because they are on DRC
    //    for(uint32_t i = 0; (i < tvSize) && (i < tvElements.size()); ++i)
    //    {
    //        uint32_t n;
    //        for(n = 0; (n < drcSize) && (n < drcElements.size()); n++)
    //        {
    //            if(tvElements[i] == drcElements[n])
    //                break;
    //        }
    //        if(n == drcElements.size())
    //        {
    //            tvElements[i]->update(controller);
    //        }
    //    }

    if (controller->chanIdx >= 1 && controller->chanIdx <= 4 && controller->data.validPointer) {
        int32_t wpadIdx = controller->chanIdx - 1;
        float posX      = controller->data.x;
        float posY      = controller->data.y;
        pointerImg[wpadIdx]->setPosition(posX, posY);
        pointerImg[wpadIdx]->setAngle(controller->data.pointerAngle);
        pointerValid[wpadIdx] = true;
    }
}

void MainWindow::drawDrc(CVideo *video) {
    for (uint32_t i = 0; i < drcElements.size(); ++i) {
        drcElements[i]->draw(video);
    }

    for (int32_t i = 0; i < 4; i++) {
        if (pointerValid[i]) {
            pointerImg[i]->setAlpha(0.5f);
            pointerImg[i]->draw(video);
            pointerImg[i]->setAlpha(1.0f);
        }
    }

    if (keyboardInstance != nullptr && keyboardInstance->isReady()) {
        keyboardInstance->drawDRC();
    }
}

void MainWindow::drawTv(CVideo *video) {
    for (uint32_t i = 0; i < tvElements.size(); ++i) {
        tvElements[i]->draw(video);
    }

    for (int32_t i = 0; i < 4; i++) {
        if (pointerValid[i]) {
            pointerImg[i]->draw(video);
            pointerValid[i] = false;
        }
    }
    if (keyboardInstance != nullptr && keyboardInstance->isReady()) {
        keyboardInstance->drawTV();
    }
}

void MainWindow::SetupMainView() {
    //! start on the title that was launched last
    uint64_t lastTitle = loadLastTitle();
    startTitle         = lastTitle;

    currentTvFrame = new GuiIconGrid(width, height, lastTitle, true);

    currentTvFrame->setEffect(EFFECT_FADE, 10, 255);
    currentTvFrame->setState(GuiElement::STATE_DISABLED);
    currentTvFrame->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);

    appendTv(currentTvFrame);

    currentDrcFrame = new GuiIconGrid(width, height, lastTitle, false);
    currentDrcFrame->setEffect(EFFECT_FADE, 10, 255);
    currentDrcFrame->setState(GuiElement::STATE_DISABLED);
    currentDrcFrame->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);

    if (currentTvFrame != currentDrcFrame) {
        currentDrcFrame->setEffect(EFFECT_FADE, 10, 255);
        currentDrcFrame->setState(GuiElement::STATE_DISABLED);
        currentDrcFrame->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
    }

    //! reconnect only to DRC game selection change
    currentTvFrame->gameSelectionChanged.disconnect(this);
    currentDrcFrame->gameSelectionChanged.disconnect(this);
    currentTvFrame->gameLaunchClicked.disconnect(this);
    currentDrcFrame->gameLaunchClicked.disconnect(this);


    if (currentTvFrame != currentDrcFrame) {
        currentTvFrame->gameSelectionChanged.connect(this, &MainWindow::OnGameSelectionChange);
        currentTvFrame->gameLaunchClicked.connect(this, &MainWindow::OnGameLaunchSplashScreen);
    }

    currentDrcFrame->gameSelectionChanged.connect(this, &MainWindow::OnGameSelectionChange);
    currentDrcFrame->gameLaunchClicked.connect(this, &MainWindow::OnGameLaunchSplashScreen);
    connectFolderSignals();

    mainSwitchButtonFrame = new MainDrcButtonsFrame(width, height);
    mainSwitchButtonFrame->settingsButtonClicked.connect(this, &MainWindow::OnSettingsButtonClicked);
    mainSwitchButtonFrame->layoutSwitchClicked.connect(this, &MainWindow::OnLayoutSwitchClicked);
    mainSwitchButtonFrame->gameListFilterClicked.connect(this, &MainWindow::OnGameListFilterButtonClicked);
    mainSwitchButtonFrame->setState(GuiElement::STATE_DISABLED);
    mainSwitchButtonFrame->setEffect(EFFECT_FADE, 10, 255);
    mainSwitchButtonFrame->setState(GuiElement::STATE_DISABLED);
    mainSwitchButtonFrame->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);

    appendDrc(currentDrcFrame);
    append(mainSwitchButtonFrame);
}

void MainWindow::OnLayoutSwitchClicked(GuiElement *element) {
    if (!currentTvFrame || !currentDrcFrame || !mainSwitchButtonFrame) {
        return;
    }

    if (currentTvFrame == currentDrcFrame) {
        return;
    }

    currentTvFrame->setState(GuiElement::STATE_DISABLED);
    currentTvFrame->setEffect(EFFECT_FADE, -15, 0);
    currentTvFrame->effectFinished.connect(this, &MainWindow::OnLayoutSwitchEffectFinish);

    currentDrcFrame->setState(GuiElement::STATE_DISABLED);
    currentDrcFrame->setEffect(EFFECT_FADE, -15, 0);

    mainSwitchButtonFrame->setState(GuiElement::STATE_DISABLED);
}

void MainWindow::OnGameListFilterButtonClicked(GuiElement *element) {
    if (!currentTvFrame || !currentDrcFrame || !mainSwitchButtonFrame) {
        return;
    }

    if (listMenu != nullptr || pendingKeyboardFrames > 0) {
        return;
    }
    keyboardPurpose = KeyboardPurpose::Search;
    openTextKeyboard(-1);
}

void MainWindow::openTextKeyboard(int32_t maxTextLength) {
    if (keyboardInstance == nullptr) {
        keyboardInstance = new KeyboardHelper();
    }
    if (keyboardInstance->isReady() && !keyboardInstance->isOpen()) {
        if (keyboardInstance->openKeyboard(maxTextLength)) {
            currentTvFrame->setState(GuiElement::STATE_DISABLED);
            currentDrcFrame->setState(GuiElement::STATE_DISABLED);
            mainSwitchButtonFrame->setState(GuiElement::STATE_DISABLED);
            return;
        }
    }
    keyboardPurpose = KeyboardPurpose::Search;
}

void MainWindow::OnLayoutSwitchEffectFinish(GuiElement *element) {
    if (!currentTvFrame || !currentDrcFrame || !mainSwitchButtonFrame)
        return;

    element->effectFinished.disconnect(this);
    remove(currentDrcFrame);
    remove(currentTvFrame);

    GuiTitleBrowser *tmpElement = currentDrcFrame;
    currentDrcFrame             = currentTvFrame;
    currentTvFrame              = tmpElement;

    appendTv(currentTvFrame);
    appendDrc(currentDrcFrame);
    //! re-append on top
    append(mainSwitchButtonFrame);
    if (userFrame) {
        append(userFrame);
    }

    currentTvFrame->resetState();
    currentTvFrame->setEffect(EFFECT_FADE, 15, 255);

    currentDrcFrame->resetState();
    currentDrcFrame->setEffect(EFFECT_FADE, 15, 255);

    mainSwitchButtonFrame->clearState(GuiElement::STATE_DISABLED);

    //! reconnect only to DRC game selection change
    currentTvFrame->gameSelectionChanged.disconnect(this);
    currentDrcFrame->gameSelectionChanged.disconnect(this);
    currentTvFrame->gameLaunchClicked.disconnect(this);
    currentDrcFrame->gameLaunchClicked.disconnect(this);

    currentTvFrame->gameSelectionChanged.connect(this, &MainWindow::OnGameSelectionChange);
    currentTvFrame->gameLaunchClicked.connect(this, &MainWindow::OnGameLaunchSplashScreen);
    currentDrcFrame->gameSelectionChanged.connect(this, &MainWindow::OnGameSelectionChange);
    currentDrcFrame->gameLaunchClicked.connect(this, &MainWindow::OnGameLaunchSplashScreen);
    connectFolderSignals();
}

void MainWindow::OnOpenEffectFinish(GuiElement *element) {
    //! once the menu is open reset its state and allow it to be "clicked/hold"
    element->effectFinished.disconnect(this);
    element->clearState(GuiElement::STATE_DISABLED);
}

void MainWindow::OnCloseEffectFinish(GuiElement *element) {
    DEBUG_FUNCTION_LINE("Remove %08X", element);
    //! remove element from draw list and push to delete queue
    remove(element);
    AsyncExecutor::pushForDelete(element);
}

void MainWindow::OnSettingsButtonClicked(GuiElement *element) {
    if (listMenu != nullptr || accountFrame != nullptr || launchInProgress || isKeyboardOpen() || pendingKeyboardFrames > 0) {
        return;
    }
    menuKind = MenuKind::Settings;
    openListMenu("launchiine", {"System Settings", "Original Wii U Menu"});
}

namespace {
    constexpr const char *ENVIRONMENTS_DIR = "fs:/vol/external01/wiiu/environments";
    constexpr const char *RESTORE_MARKER   = "fs:/vol/external01/wiiu/launchiine/restore-men.txt";
    constexpr const char *HELPER_PLUGIN    = "launchiine_helper.wps";

    bool fileExists(const std::string &path) {
        struct stat st {};
        return stat(path.c_str(), &st) == 0;
    }

    //! the environment launchiine runs from: aroma, or the only one with a men.rpx
    std::string launchiineEnvironment() {
        std::string aroma = std::string(ENVIRONMENTS_DIR) + "/aroma";
        if (fileExists(aroma + "/men.rpx")) {
            return aroma;
        }
        std::string found;
        DIR *dir = opendir(ENVIRONMENTS_DIR);
        if (dir != nullptr) {
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string path = std::string(ENVIRONMENTS_DIR) + "/" + entry->d_name;
                if (entry->d_name[0] != '.' && fileExists(path + "/men.rpx")) {
                    found = path;
                    break;
                }
            }
            closedir(dir);
        }
        return found;
    }
} // namespace

void MainWindow::openOriginalMenu() {
    std::string environment = launchiineEnvironment();
    if (environment.empty()) {
        refreshFolderViews({}, "men.rpx not found on the SD card");
        return;
    }
    //! without the helper nothing would rename men.rpx back: the console would stay on the
    //! original menu until the file is renamed on a PC
    if (!fileExists(environment + "/plugins/" + HELPER_PLUGIN)) {
        refreshFolderViews({}, std::string("Needs ") + HELPER_PLUGIN + " in the plugins folder (see README)");
        return;
    }
    std::string men      = environment + "/men.rpx";
    std::string disabled = environment + "/men.rpx.launchiine-off";
    std::string marker   = disabled + "\n" + men + "\n";
    FSUtils::CreateSubfolder("fs:/vol/external01/wiiu/launchiine");
    if (FSUtils::saveBufferToFile(RESTORE_MARKER, (void *) marker.data(), marker.size()) != (int32_t) marker.size()) {
        refreshFolderViews({}, "Could not write to the SD card");
        return;
    }
    if (rename(men.c_str(), disabled.c_str()) != 0) {
        ::remove(RESTORE_MARKER);
        refreshFolderViews({}, "Could not rename men.rpx");
        return;
    }
    DEBUG_FUNCTION_LINE("Starting the original Wii U Menu (%s renamed, the helper plugin restores it)", men.c_str());
    launchInProgress = true;
    gameList.setPaused(true);
    stopTitleLoader();
    SYSLaunchMenu();
}

void MainWindow::openSystemSettings() {
    //! _SYSLaunchSettings() blocks forever when called from the menu. Launch the System
    //! Settings title the same way as tapping its icon instead.
    uint64_t settingsTitleId = 0;
    gameList.lock();
    for (auto *info : gameList.getFullGameList()) {
        if (info->appType == MCP_APP_TYPE_SYSTEM_SETTINGS) {
            settingsTitleId = info->titleId;
            break;
        }
    }
    gameList.unlock();

    if (settingsTitleId == 0) {
        DEBUG_FUNCTION_LINE("System Settings not found");
        return;
    }
    DEBUG_FUNCTION_LINE("Open System Settings %016llX", settingsTitleId);
    OnGameLaunchSplashScreen(nullptr, settingsTitleId);
}

void MainWindow::connectFolderSignals() {
    for (auto *frame : {currentTvFrame, currentDrcFrame}) {
        frame->folderOpened.disconnect(this);
        frame->folderMembershipChanged.disconnect(this);
        frame->folderMenuRequested.disconnect(this);
        frame->folderOpened.connect(this, &MainWindow::OnFolderOpened);
        frame->folderMembershipChanged.connect(this, &MainWindow::OnFolderMembershipChanged);
        frame->folderMenuRequested.connect(this, &MainWindow::OnFolderMenuRequested);
    }
}

std::string MainWindow::titleName(uint64_t titleId) {
    if (Folders::isFolder(titleId)) {
        return Folders::folderName(titleId);
    }
    std::string name;
    gameList.lock();
    for (int32_t i = 0; i < gameList.size(); i++) {
        gameInfo *info = gameList.at(i);
        if (info != nullptr && info->titleId == titleId) {
            name = info->name;
            break;
        }
    }
    gameList.unlock();
    return name;
}

void MainWindow::OnFolderMenuRequested(GuiTitleBrowser *element, uint64_t titleId) {
    if (listMenu != nullptr || accountFrame != nullptr || launchInProgress || isKeyboardOpen() || pendingKeyboardFrames > 0) {
        return;
    }
    menuTarget   = titleId;
    menuNeighbor = element->neighborOf(titleId);
    menuDestinations.clear();

    if (Folders::isFolder(titleId)) {
        //! like the Wii U Menu's folder settings; the built-in folders only change color
        menuKind = MenuKind::FolderOptions;
        std::vector<std::string> labels = {"Change color"};
        menuDestinations.push_back(FOLDER_ACTION_COLOR);
        if (Folders::isUserFolder(titleId)) {
            labels.emplace_back("Rename");
            menuDestinations.push_back(FOLDER_ACTION_RENAME);
            labels.emplace_back("Delete folder");
            menuDestinations.push_back(FOLDER_ACTION_DELETE);
        }
        openListMenu("Folder settings: " + Folders::folderName(titleId), labels);
        return;
    }

    menuKind         = MenuKind::MoveTitle;
    uint64_t current = Folders::folderOf(titleId);
    uint64_t natural = Folders::naturalFolder(titleId);
    std::vector<std::string> labels;
    if (current != 0) {
        menuDestinations.push_back(0);
        labels.emplace_back("Main grid");
    }
    if (natural != 0 && current != natural) {
        menuDestinations.push_back(natural);
        labels.push_back(Folders::folderName(natural) + " folder");
    }
    for (auto &folder : Folders::userFolders()) {
        if (folder.id != current) {
            menuDestinations.push_back(folder.id);
            labels.push_back(folder.name);
        }
    }
    menuDestinations.push_back(NEW_FOLDER);
    labels.emplace_back("New folder...");

    std::string name = titleName(titleId);
    openListMenu(name.empty() ? "Move to" : "Move \"" + name + "\" to", labels);
}

void MainWindow::openListMenu(const std::string &title, const std::vector<std::string> &options) {
    closeListMenu();
    listMenu = new ListMenuFrame(width, height, title, options);
    listMenu->optionChosen.connect(this, &MainWindow::OnListMenuChosen);
    listMenu->cancelled.connect(this, &MainWindow::OnListMenuCancelled);
    append(listMenu);
}

void MainWindow::closeListMenu() {
    if (listMenu == nullptr) {
        return;
    }
    listMenu->optionChosen.disconnect(this);
    listMenu->cancelled.disconnect(this);
    remove(listMenu);
    AsyncExecutor::pushForDelete(listMenu);
    listMenu = nullptr;
    //! the A/B press that closed the menu must not reach the grid
    inputCooldown = 15;
}

void MainWindow::OnListMenuCancelled(ListMenuFrame *menu) {
    closeListMenu();
    menuKind = MenuKind::None;
}

void MainWindow::OnListMenuChosen(ListMenuFrame *menu, int32_t index) {
    MenuKind kind = menuKind;
    closeListMenu();
    menuKind = MenuKind::None;

    switch (kind) {
        case MenuKind::MoveTitle: {
            if (index < 0 || index >= (int32_t) menuDestinations.size()) {
                return;
            }
            uint64_t destination = menuDestinations[index];
            if (destination == NEW_FOLDER) {
                keyboardPurpose          = KeyboardPurpose::NewFolder;
                pendingKeyboardMaxLength = 32;
                pendingKeyboardFrames    = 10;
            } else {
                moveTitle(menuTarget, destination);
            }
            break;
        }
        case MenuKind::FolderOptions: {
            if (index < 0 || index >= (int32_t) menuDestinations.size()) {
                return;
            }
            uint64_t action = menuDestinations[index];
            if (action == FOLDER_ACTION_COLOR) {
                menuKind = MenuKind::FolderColor;
                std::vector<std::string> names;
                for (auto &choice : Folders::colorChoices()) {
                    names.emplace_back(choice.name);
                }
                openListMenu("Color of " + Folders::folderName(menuTarget), names);
            } else if (action == FOLDER_ACTION_RENAME) {
                keyboardPurpose          = KeyboardPurpose::RenameFolder;
                pendingKeyboardMaxLength = 32;
                pendingKeyboardFrames    = 10;
            } else {
                menuKind = MenuKind::ConfirmDelete;
                openListMenu("Delete \"" + Folders::folderName(menuTarget) + "\"?", {"Delete (games go to the main grid)", "Cancel"});
            }
            break;
        }
        case MenuKind::Settings:
            if (index == 0) {
                openSystemSettings();
            } else if (index == 1) {
                openOriginalMenu();
            }
            break;
        case MenuKind::FolderColor: {
            auto &choices = Folders::colorChoices();
            if (index < 0 || index >= (int32_t) choices.size()) {
                return;
            }
            Folders::setFolderColor(menuTarget, choices[index].rgb);
            refreshFolderViews({menuTarget}, "");
            break;
        }
        case MenuKind::ConfirmDelete:
            if (index == 0) {
                std::string name = Folders::folderName(menuTarget);
                Folders::deleteFolder(menuTarget);
                refreshFolderViews({menuNeighbor}, "Deleted the folder \"" + name + "\"");
            }
            break;
        default:
            break;
    }
}

void MainWindow::moveTitle(uint64_t titleId, uint64_t folder) {
    uint64_t source = Folders::folderOf(titleId);
    Folders::moveTo(titleId, folder);
    DEBUG_FUNCTION_LINE("Moved %016llX from %016llX to %016llX", titleId, source, folder);

    //! a user folder that is now empty is removed
    if (Folders::isUserFolder(source)) {
        bool empty = true;
        gameList.lock();
        for (int32_t i = 0; i < gameList.size() && empty; i++) {
            gameInfo *info = gameList.at(i);
            if (info != nullptr && Folders::folderOf(info->titleId) == source) {
                empty = false;
            }
        }
        gameList.unlock();
        if (empty) {
            Folders::deleteFolder(source);
        }
    }

    std::string message = folder == 0 ? "Moved to the main grid" : "Moved to \"" + Folders::folderName(folder) + "\"";
    //! still visible (e.g. search results): keep it selected; otherwise the title next to it,
    //! or the folder it went into
    refreshFolderViews({titleId, menuNeighbor, folder}, message);
}

void MainWindow::refreshFolderViews(const std::vector<uint64_t> &preferred, const std::string &message) {
    currentDrcFrame->refreshFolders(preferred);
    if (currentTvFrame != currentDrcFrame) {
        currentTvFrame->refreshFolders(preferred);
    }
    currentDrcFrame->showMessage(message);
    if (currentTvFrame != currentDrcFrame) {
        currentTvFrame->showMessage(message);
    }
}

void MainWindow::OnFolderOpened(GuiTitleBrowser *element, uint64_t folder) {
    GuiTitleBrowser *other = element == currentDrcFrame ? currentTvFrame : currentDrcFrame;
    if (other && other != element) {
        other->openFolder(folder);
    }
}

void MainWindow::OnFolderMembershipChanged(GuiTitleBrowser *element) {
    GuiTitleBrowser *other = element == currentDrcFrame ? currentTvFrame : currentDrcFrame;
    if (other && other != element) {
        other->refreshFolders();
    }
}

void MainWindow::OnGameSelectionChange(GuiTitleBrowser *element, uint64_t selectedIdx) {
    if (!currentDrcFrame || !currentTvFrame)
        return;

    //! load icons around what the user is looking at first
    gameList.setFocusTitle(selectedIdx);

    if (element == currentDrcFrame && currentDrcFrame != currentTvFrame) {
        currentTvFrame->setSelectedGame(selectedIdx);
    } else if (element == currentTvFrame && currentDrcFrame != currentTvFrame) {
        currentDrcFrame->setSelectedGame(selectedIdx);
    }
}

void MainWindow::OnGameLaunchSplashScreen(GuiTitleBrowser *element, uint64_t titleID) {
    if (launchInProgress) {
        //! A pressed twice, or touch + button in the same frame
        DEBUG_FUNCTION_LINE("Launch already in progress, ignoring %016llX", titleID);
        return;
    }
    gameInfo *info = gameList.getGameInfo(titleID);
    if (info != nullptr) {
        launchInProgress = true;
        //! nothing else heavy while launching: the splash screen and the launch get the
        //! render thread and the file system for themselves
        gameList.setPaused(true);
        DEBUG_FUNCTION_LINE("Launch requested for %016llX, loader paused", titleID);
        auto *splashScreenDRC = new GameSplashScreen(width, height, info, false);
        splashScreenDRC->setEffect(EFFECT_FADE, 15, 255);
        splashScreenDRC->setState(GuiElement::STATE_DISABLED);
        splashScreenDRC->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
        splashScreenDRC->gameGameSplashScreenFinished.connect(this, &MainWindow::OnGameLaunchSplashScreenFinished);
        appendDrc(splashScreenDRC);

        auto *splashScreenTV = new GameSplashScreen(width, height, info, true);
        splashScreenTV->setEffect(EFFECT_FADE, 15, 255);
        splashScreenTV->setState(GuiElement::STATE_DISABLED);
        splashScreenTV->effectFinished.connect(this, &MainWindow::OnOpenEffectFinish);
        splashScreenTV->gameGameSplashScreenFinished.connect(this, &MainWindow::OnGameLaunchSplashScreenFinished);
        appendTv(splashScreenTV);
    } else {
        DEBUG_FUNCTION_LINE("Failed to find gameInfo for titleId %016llX", titleID);
    }
}

void MainWindow::OnGameLaunchSplashScreenFinished(GuiElement *element, gameInfo *info, bool launchGame) {
    if (info == nullptr) {
        return;
    }
    if (launchGame) {
        //! when a full title launch was started this process is about to exit.
        //! For everything else (failure, applets that put us into background) allow launching again.
        if (!OnGameLaunch(info->titleId)) {
            //! applets (eShop, browser, ...) come back to us, a failed launch too
            launchInProgress = false;
            gameList.setPaused(false);
            DEBUG_FUNCTION_LINE("No full launch, loader resumed");
        }
    }
    if (element) {
        // immediately remove the splashScreen
        MainWindow::OnCloseEffectFinish(element);
    }
}

extern "C" int32_t SYSSwitchToBrowser(void *);
extern "C" int32_t ACPCheckTitleLaunchByTitleListTypeEx(MCPTitleListType *title, uint32_t unk);
extern "C" int32_t _SYSLaunchTitleByPathFromLauncher(const char *path, uint32_t pathLength);
extern "C" int32_t SYSSwitchToEShop(void *);
extern "C" int32_t _SYSSwitchTo(uint32_t pfid);

bool MainWindow::OnGameLaunch(uint64_t titleId) {

    if (titleId == 0x0005001010040000L ||
        titleId == 0x0005001010040100L ||
        titleId == 0x0005001010040200L) {
        DEBUG_FUNCTION_LINE("Skip launching the Wii U Menu");
        return false;
    }

    if (titleId == 0x000500301001220AL ||
        titleId == 0x000500301001210AL ||
        titleId == 0x000500301001200AL) {
        DEBUG_FUNCTION_LINE("Launching the browser");
        SYSSwitchToBrowser(nullptr);
        return false;
    }
    if (titleId == 0x000500301001400AL ||
        titleId == 0x000500301001410AL ||
        titleId == 0x000500301001420AL) {
        DEBUG_FUNCTION_LINE("Launching the Eshop");
        SYSSwitchToEShop(nullptr);

        return false;
    }
    if (titleId == 0x000500301001800AL ||
        titleId == 0x000500301001810AL ||
        titleId == 0x000500301001820AL) {
        DEBUG_FUNCTION_LINE("Launching the Download Management");
        _SYSSwitchTo(12);
        return false;
    }
    if (titleId == 0x000500301001600AL ||
        titleId == 0x000500301001610AL ||
        titleId == 0x000500301001620AL) {
        DEBUG_FUNCTION_LINE("Launching Miiverse");
        _SYSSwitchTo(9);
        return false;
    }
    if (titleId == 0x000500301001500AL ||
        titleId == 0x000500301001510AL ||
        titleId == 0x000500301001520AL) {
        DEBUG_FUNCTION_LINE("Launching Friendlist");
        _SYSSwitchTo(11);
        return false;
    }
    if (titleId == 0x000500301001300AL ||
        titleId == 0x000500301001310AL ||
        titleId == 0x000500301001320AL) {
        DEBUG_FUNCTION_LINE("Launching TVii");
        _SYSSwitchTo(3);
        return false;
    }

    if (isHomebrewTitle(titleId)) {
        //! Same calls as the Wii U Menu. The homebrew_on_menu plugin hooks both: the first
        //! prepares the RPX loader, the second starts it in place of Health & Safety.
        gameInfo *info = gameList.getGameInfo(titleId);
        if (info == nullptr) {
            DEBUG_FUNCTION_LINE("Unknown homebrew %016llX", titleId);
            return false;
        }
        MCPTitleListType homebrewInfo = info->mcpInfo;
        stopTitleLoader();
        saveLastTitle(titleId);
        int32_t check = ACPCheckTitleLaunchByTitleListTypeEx(&homebrewInfo, 0);
        DEBUG_FUNCTION_LINE("Launching homebrew %016llX (%s), check %d", titleId, homebrewInfo.path, check);
        if (check != 0) {
            return false;
        }
        char path[sizeof(homebrewInfo.path) + 1] = {};
        memcpy(path, homebrewInfo.path, sizeof(homebrewInfo.path));
        _SYSLaunchTitleByPathFromLauncher(path, strlen(path));
        return true;
    }

    MCPTitleListType titleInfo;
    int32_t handle = MCP_Open();
    auto err       = MCP_GetTitleInfo(handle, titleId, &titleInfo);
    MCP_Close(handle);
    if (err == 0) {
        //! IPC from the loader thread while the system switches titles can hang the exit,
        //! so stop it first. This takes at most the time to load one icon.
        stopTitleLoader();
        //! remember games, not system titles like System Settings
        if (!isSystemTitleId(titleId)) {
            saveLastTitle(titleId);
        }
        ACPAssignTitlePatch(&titleInfo);
        DEBUG_FUNCTION_LINE("Launching %016llX", titleId);
        _SYSLaunchTitleWithStdArgsInNoSplash(titleId, nullptr);
        return true;
    }

    DEBUG_FUNCTION_LINE("Failed launch titleId %016llX", titleId);
    return false;
}
