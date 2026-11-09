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
#include <coreinit/title.h>
#include <cinttypes>
#include <cstdio>
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

    userButton = new GuiButton(96, 96);
    if (!account->miiTga.empty()) {
        userMiiData = new GuiImageData(account->miiTga.data(), account->miiTga.size(), GX2_TEX_CLAMP_MODE_MIRROR);
        if (userMiiData->getTexture()) {
            userMiiImage = new GuiImage(userMiiData);
            userMiiImage->setScale(0.75f);
            userButton->setImage(userMiiImage);
        }
    }
    userNameText = new GuiText(account->name.c_str(), 26, glm::vec4(1.0f));
    userNameText->setAlignment(ALIGN_RIGHT | ALIGN_MIDDLE);
    userNameText->setPosition(-100, 0);
    userNameText->setMaxWidth(170, GuiText::DOTTED);
    userButton->setLabel(userNameText);

    userButton->setAlignment(ALIGN_RIGHT | ALIGN_TOP);
    userButton->setPosition(-12, -6);
    userButton->setTrigger(&userTouchTrigger);
    userButton->setTrigger(&userWpadTrigger);
    userButton->setTrigger(&userXTrigger);
    userButton->setEffectGrow();
    userButton->clicked.connect(this, &MainWindow::OnUserButtonClicked);
    append(userButton);
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
    if (userButton) {
        remove(userButton);
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

    if (keyboardInstance != nullptr && keyboardInstance->isReady()) {
        //! must run every frame, also while the keyboard is fading out
        if (keyboardInstance->checkResult()) {
            if (keyboardInstance->wasConfirmed()) {
                std::string result = keyboardInstance->getResult();
                DEBUG_FUNCTION_LINE("Search filter: \"%s\"", result.c_str());
                currentTvFrame->setFilter(result);
                if (currentDrcFrame != currentTvFrame) {
                    currentDrcFrame->setFilter(result);
                }
            }

            currentTvFrame->clearState(GuiElement::STATE_DISABLED);
            currentDrcFrame->clearState(GuiElement::STATE_DISABLED);
            mainSwitchButtonFrame->clearState(GuiElement::STATE_DISABLED);
            inputCooldown = 15;
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

    if (keyboardInstance == nullptr) {
        keyboardInstance = new KeyboardHelper();
    }
    if (keyboardInstance->isReady() && !keyboardInstance->isOpen()) {
        if (keyboardInstance->openKeyboard()) {
            currentTvFrame->setState(GuiElement::STATE_DISABLED);
            currentDrcFrame->setState(GuiElement::STATE_DISABLED);
            mainSwitchButtonFrame->setState(GuiElement::STATE_DISABLED);
        }
    }
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
        frame->folderOpened.connect(this, &MainWindow::OnFolderOpened);
        frame->folderMembershipChanged.connect(this, &MainWindow::OnFolderMembershipChanged);
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
