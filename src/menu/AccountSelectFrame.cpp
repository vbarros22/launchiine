#include "AccountSelectFrame.h"
#include "utils/logger.h"

namespace {
    constexpr int32_t ENTRY_W = 170;
    constexpr int32_t ENTRY_H = 210;
    constexpr int32_t MII_SIZE = 128;

    GX2Color backgroundColor() {
        GX2Color c;
        c.r = 240; // light theme
        c.g = 243;
        c.b = 246;
        c.a = 250;
        return c;
    }

    GX2Color placeholderColor() {
        GX2Color c;
        c.r = 210;
        c.g = 216;
        c.b = 222;
        c.a = 255;
        return c;
    }
} // namespace

AccountSelectFrame::AccountSelectFrame(int32_t w, int32_t h, const std::vector<Accounts::Account> &accounts, uint8_t currentSlot, bool allowCancel)
    : GuiFrame(w, h),
      allowCancel(allowCancel),
      background(w, h, backgroundColor()),
      titleText("Who will be using the Wii U?", 46, glm::vec4(0.22f, 0.24f, 0.26f, 1.0f)),
      hintText(allowCancel ? "A Select    B Cancel" : "A Select", 28, glm::vec4(0.3f, 0.32f, 0.34f, 1.0f)),
      touchTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH),
      wpadTouchTrigger(GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5, GuiTrigger::BUTTON_A),
      leftTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_LEFT | GuiTrigger::STICK_L_LEFT, true),
      rightTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_RIGHT | GuiTrigger::STICK_L_RIGHT, true),
      upTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_UP | GuiTrigger::STICK_L_UP, true),
      downTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_DOWN | GuiTrigger::STICK_L_DOWN, true),
      aTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A, true),
      bTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_B, true),
      leftButton(w, h), rightButton(w, h), upButton(w, h), downButton(w, h), confirmButton(w, h), cancelButton(w, h) {

    append(&background);

    titleText.setAlignment(ALIGN_CENTER | ALIGN_TOP);
    titleText.setPosition(0, -60);
    append(&titleText);

    hintText.setAlignment(ALIGN_CENTER | ALIGN_BOTTOM);
    hintText.setPosition(0, 40);
    append(&hintText);

    //! layout: up to COLUMNS per row, rows centered
    int32_t count = (int32_t) accounts.size();
    int32_t rows  = (count + COLUMNS - 1) / COLUMNS;
    for (int32_t i = 0; i < count; i++) {
        const auto &account = accounts[i];
        Entry entry;
        entry.slot = account.slot;

        entry.button = new GuiButton(ENTRY_W, ENTRY_H);
        if (!account.miiTga.empty()) {
            entry.miiData = new GuiImageData(account.miiTga.data(), account.miiTga.size(), GX2_TEX_CLAMP_MODE_MIRROR);
        }
        if (entry.miiData && entry.miiData->getTexture()) {
            entry.miiImage = new GuiImage(entry.miiData);
            entry.miiImage->setAlignment(ALIGN_CENTER | ALIGN_TOP);
            entry.button->setImage(entry.miiImage);
        } else {
            entry.placeholder = new GuiImage(MII_SIZE, MII_SIZE, placeholderColor());
            entry.placeholder->setAlignment(ALIGN_CENTER | ALIGN_TOP);
            entry.button->setImage(entry.placeholder);
        }

        entry.nameText = new GuiText(account.name.c_str(), 30, glm::vec4(0.22f, 0.24f, 0.26f, 1.0f));
        entry.nameText->setAlignment(ALIGN_CENTER | ALIGN_BOTTOM);
        entry.nameText->setMaxWidth(ENTRY_W, GuiText::DOTTED);
        entry.button->setLabel(entry.nameText);

        int32_t row     = i / COLUMNS;
        int32_t col     = i % COLUMNS;
        int32_t inRow   = std::min(COLUMNS, count - row * COLUMNS);
        float x         = (col - (inRow - 1) / 2.0f) * (ENTRY_W + 20);
        float y         = ((rows - 1) / 2.0f - row) * (ENTRY_H + 30) - 10;
        entry.button->setAlignment(ALIGN_CENTER | ALIGN_MIDDLE);
        entry.button->setPosition(x, y);
        entry.button->setTrigger(&touchTrigger);
        entry.button->setTrigger(&wpadTouchTrigger);
        entry.button->clicked.connect(this, &AccountSelectFrame::OnEntryClick);
        append(entry.button);

        if (account.slot == currentSlot) {
            selected = i;
        }
        entries.push_back(entry);
    }

    for (auto *button : {&leftButton, &rightButton, &upButton, &downButton}) {
        button->clicked.connect(this, &AccountSelectFrame::OnMove);
        append(button);
    }
    leftButton.setTrigger(&leftTrigger);
    rightButton.setTrigger(&rightTrigger);
    upButton.setTrigger(&upTrigger);
    downButton.setTrigger(&downTrigger);

    confirmButton.setTrigger(&aTrigger);
    confirmButton.clicked.connect(this, &AccountSelectFrame::OnConfirm);
    append(&confirmButton);

    cancelButton.setTrigger(&bTrigger);
    cancelButton.clicked.connect(this, &AccountSelectFrame::OnCancel);
    append(&cancelButton);

    select(selected);
}

AccountSelectFrame::~AccountSelectFrame() {
    for (auto &entry : entries) {
        remove(entry.button);
        delete entry.button;
        delete entry.miiImage;
        delete entry.placeholder;
        delete entry.nameText;
        delete entry.miiData;
    }
    entries.clear();
}

void AccountSelectFrame::process() {
    GuiFrame::process();
}

void AccountSelectFrame::select(int32_t index) {
    if (entries.empty()) {
        return;
    }
    if (index < 0) {
        index = 0;
    }
    if (index >= (int32_t) entries.size()) {
        index = (int32_t) entries.size() - 1;
    }
    selected = index;
    for (int32_t i = 0; i < (int32_t) entries.size(); i++) {
        bool isSelected = i == selected;
        entries[i].button->setScale(isSelected ? 1.15f : 0.9f);
        entries[i].button->setAlpha(isSelected ? 1.0f : 0.6f);
        if (isSelected) {
            entries[i].nameText->setBlurGlowColor(5.0f, glm::vec4(0.45f, 0.85f, 1.0f, 1.0f));
        } else {
            entries[i].nameText->setBlurGlowColor(0.0f, glm::vec4(0.0f));
        }
    }
}

void AccountSelectFrame::OnEntryClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    for (int32_t i = 0; i < (int32_t) entries.size(); i++) {
        if (entries[i].button == button) {
            if (i == selected) {
                //! second tap on the same Mii confirms, like the icon grid
                OnConfirm(nullptr, controller, trigger);
            } else {
                select(i);
            }
            return;
        }
    }
}

void AccountSelectFrame::OnMove(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    int32_t next = selected;
    if (button == &leftButton) {
        next = selected - 1;
    } else if (button == &rightButton) {
        next = selected + 1;
    } else if (button == &upButton) {
        next = selected - COLUMNS;
    } else if (button == &downButton) {
        next = selected + COLUMNS;
    }
    if (next >= 0 && next < (int32_t) entries.size()) {
        select(next);
    }
}

void AccountSelectFrame::OnConfirm(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    if (done || entries.empty()) {
        return;
    }
    done = true;
    DEBUG_FUNCTION_LINE("Account slot %u chosen", entries[selected].slot);
    accountChosen(this, entries[selected].slot);
}

void AccountSelectFrame::OnCancel(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    if (done || !allowCancel) {
        return;
    }
    done = true;
    cancelled(this);
}
