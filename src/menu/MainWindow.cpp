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
    titleLoader = std::async(std::launch::async, [this] { gameList.load(); });
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

    if (controller->chan & GuiTrigger::CHANNEL_1) {
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
    if (launchInProgress) {
        return;
    }
    DEBUG_FUNCTION_LINE("Open System Settings");
    launchInProgress = true;
    stopTitleLoader();
    saveLastTitle(currentDrcFrame ? currentDrcFrame->getSelectedGame() : 0);
    //! the system switches to System Settings once we exit, ProcUI tells us to do so
    _SYSLaunchSettings();
}

void MainWindow::OnGameSelectionChange(GuiTitleBrowser *element, uint64_t selectedIdx) {
    if (!currentDrcFrame || !currentTvFrame)
        return;

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
            launchInProgress = false;
        }
    }
    if (element) {
        // immediately remove the splashScreen
        MainWindow::OnCloseEffectFinish(element);
    }
}

extern "C" int32_t SYSSwitchToBrowser(void *);
extern "C" int32_t SYSSwitchToEShop(void *);
extern "C" int32_t _SYSSwitchTo(uint32_t pfid);

bool MainWindow::OnGameLaunch(uint64_t titleId) {
    DEBUG_FUNCTION_LINE("Launch GAME!!");

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

    MCPTitleListType titleInfo;
    int32_t handle = MCP_Open();
    auto err       = MCP_GetTitleInfo(handle, titleId, &titleInfo);
    MCP_Close(handle);
    if (err == 0) {
        //! IPC from the loader thread while the system switches titles can hang the exit,
        //! so stop it first. This takes at most the time to load one icon.
        stopTitleLoader();
        saveLastTitle(titleId);
        ACPAssignTitlePatch(&titleInfo);
        DEBUG_FUNCTION_LINE("Launching %016llX", titleId);
        _SYSLaunchTitleWithStdArgsInNoSplash(titleId, nullptr);
        return true;
    }

    DEBUG_FUNCTION_LINE("Failed launch titleId %016llX", titleId);
    return false;
}
