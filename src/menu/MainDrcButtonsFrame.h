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
#ifndef _MAIN_DRC_BUTTONS_FRAME_H_
#define _MAIN_DRC_BUTTONS_FRAME_H_

#include "gui/Gui.h"
#include "resources/Resources.h"

class MainDrcButtonsFrame : public GuiFrame, public sigslot::has_slots<> {
public:
    MainDrcButtonsFrame(int32_t w, int32_t h)
        : GuiFrame(w, h), buttonClickSound(Resources::GetSound("settings_click_2.mp3")), screenSwitchSound(Resources::GetSound("screenSwitchSound.mp3")),
          switchIconData(Resources::GetImageData("layoutSwitchButton.png")), settingsIconData(Resources::GetImageData("settingsButton.png")), switchIcon(switchIconData),
          settingsIcon(settingsIconData), switchLayoutButton(switchIcon.getWidth(), switchIcon.getHeight()), settingsButton(settingsIcon.getWidth(), settingsIcon.getHeight()),
          gameListFilterButton(w, h), touchTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH),
          wpadTouchTrigger(GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5, GuiTrigger::BUTTON_A),
          settingsTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_ZL, true), switchLayoutTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_ZR, true),
          plusTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_PLUS, true),
          hintText("A Open  B Back  Y Move / New folder  + Search  X User  L/R Pages  ZL Settings", 22, glm::vec4(1.0f)) {
        settingsButton.setClickable(true);
        settingsButton.setImage(&settingsIcon);
        settingsButton.setTrigger(&touchTrigger);
        settingsButton.setTrigger(&wpadTouchTrigger);
        settingsButton.setTrigger(&settingsTrigger);
        settingsButton.setAlignment(ALIGN_LEFT | ALIGN_TOP);
        settingsButton.setSoundClick(buttonClickSound);
        settingsButton.setEffectGrow();
        settingsButton.clicked.connect(this, &MainDrcButtonsFrame::OnSettingsButtonClick);
        append(&settingsButton);

        //! The layout switch (ZR / bottom right icon) only swapped two identical grids
        //! between TV and GamePad, which looked like a flicker. It stays off until the TV
        //! gets a view of its own.

        //! controls, the A/B/Y/+ actions are not discoverable otherwise. At the top: the
        //! name of the selected title is shown at the bottom.
        hintText.setAlignment(ALIGN_CENTER | ALIGN_TOP);
        hintText.setPosition(0, -18);
        hintText.setMaxWidth(w - 320, GuiText::DOTTED); // keep clear of the settings tab and the user icon
        hintText.setBlurGlowColor(3.0f, glm::vec4(0.109804, 0.6549, 1.0f, 1.0f));
        append(&hintText);

        gameListFilterButton.setClickable(true);
        gameListFilterButton.setSoundClick(buttonClickSound);
        gameListFilterButton.setTrigger(&plusTrigger);
        gameListFilterButton.clicked.connect(this, &MainDrcButtonsFrame::OnGameListFilterButtonClicked);
        append(&gameListFilterButton);
    }

    virtual ~MainDrcButtonsFrame() {
        Resources::RemoveImageData(switchIconData);
        Resources::RemoveImageData(settingsIconData);
        Resources::RemoveSound(buttonClickSound);
        Resources::RemoveSound(screenSwitchSound);
    }

    sigslot::signal1<GuiElement *> settingsButtonClicked;
    sigslot::signal1<GuiElement *> layoutSwitchClicked;
    sigslot::signal1<GuiElement *> gameListFilterClicked;

private:
    void OnSettingsButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *) {
        settingsButtonClicked(this);
    }

    void OnLayoutSwithClick(GuiButton *button, const GuiController *controller, GuiTrigger *) {
        layoutSwitchClicked(this);
    }

    void OnGameListFilterButtonClicked(GuiButton *button, const GuiController *controller, GuiTrigger *) {
        gameListFilterClicked(this);
    }

    GuiSound *buttonClickSound;
    GuiSound *screenSwitchSound;
    GuiImageData *switchIconData;
    GuiImageData *settingsIconData;
    GuiImage switchIcon;
    GuiImage settingsIcon;

    GuiButton switchLayoutButton;
    GuiButton settingsButton;
    GuiButton gameListFilterButton;

    GuiTrigger touchTrigger;
    GuiTrigger wpadTouchTrigger;
    GuiTrigger settingsTrigger;
    GuiTrigger switchLayoutTrigger;
    GuiTrigger plusTrigger;

    GuiText hintText;
};

#endif //_SETTINGS_WINDOW_H_
