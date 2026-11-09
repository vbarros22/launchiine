#include "ListMenuFrame.h"
#include "utils/logger.h"
#include <algorithm>

namespace {
    constexpr int32_t ROW_W     = 560;
    constexpr int32_t ROW_H     = 58;
    constexpr int32_t ROW_GAP   = 8;
    constexpr int32_t MAX_ROWS  = 8; // more than that does not fit, the list scrolls with the selection

    GX2Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        GX2Color c;
        c.r = r;
        c.g = g;
        c.b = b;
        c.a = a;
        return c;
    }

    int32_t panelHeight(size_t count) {
        int32_t rows = (int32_t) std::min<size_t>(count, MAX_ROWS);
        return 150 + rows * (ROW_H + ROW_GAP);
    }
} // namespace

ListMenuFrame::ListMenuFrame(int32_t w, int32_t h, const std::string &title, const std::vector<std::string> &labels, int32_t initialSelection)
    : GuiFrame(w, h),
      dim(w, h, rgba(0, 0, 0, 150)),
      panel(ROW_W + 60, panelHeight(labels.size()), rgba(22, 26, 44, 245)),
      titleText(title.c_str(), 36, glm::vec4(1.0f)),
      hintText("A Select    B Cancel", 24, glm::vec4(1.0f, 1.0f, 1.0f, 0.7f)),
      touchTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH),
      wpadTouchTrigger(GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5, GuiTrigger::BUTTON_A),
      upTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_UP | GuiTrigger::STICK_L_UP, true),
      downTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_DOWN | GuiTrigger::STICK_L_DOWN, true),
      aTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A, true),
      bTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_B, true),
      upButton(w, h), downButton(w, h), confirmButton(w, h), cancelButton(w, h) {

    append(&dim);
    panel.setAlignment(ALIGN_CENTER | ALIGN_MIDDLE);
    append(&panel);

    int32_t top = panelHeight(labels.size()) / 2;
    titleText.setAlignment(ALIGN_CENTER | ALIGN_MIDDLE);
    titleText.setPosition(0, top - 45);
    titleText.setMaxWidth(ROW_W, GuiText::DOTTED);
    titleText.setBlurGlowColor(5.0f, glm::vec4(0.109804, 0.6549, 1.0f, 1.0f));
    append(&titleText);

    hintText.setAlignment(ALIGN_CENTER | ALIGN_MIDDLE);
    hintText.setPosition(0, -top + 30);
    append(&hintText);

    for (size_t i = 0; i < labels.size(); i++) {
        Option option;
        option.button     = new GuiButton(ROW_W, ROW_H);
        option.background = new GuiImage(ROW_W, ROW_H, rgba(255, 255, 255, 30));
        option.button->setImage(option.background);
        option.text = new GuiText(labels[i].c_str(), 30, glm::vec4(1.0f));
        option.text->setMaxWidth(ROW_W - 30, GuiText::DOTTED);
        option.button->setLabel(option.text);
        option.button->setAlignment(ALIGN_CENTER | ALIGN_MIDDLE);
        option.button->setTrigger(&touchTrigger);
        option.button->setTrigger(&wpadTouchTrigger);
        option.button->clicked.connect(this, &ListMenuFrame::OnOptionClick);
        append(option.button);
        options.push_back(option);
    }

    upButton.setTrigger(&upTrigger);
    upButton.clicked.connect(this, &ListMenuFrame::OnMove);
    append(&upButton);
    downButton.setTrigger(&downTrigger);
    downButton.clicked.connect(this, &ListMenuFrame::OnMove);
    append(&downButton);
    confirmButton.setTrigger(&aTrigger);
    confirmButton.clicked.connect(this, &ListMenuFrame::OnConfirm);
    append(&confirmButton);
    cancelButton.setTrigger(&bTrigger);
    cancelButton.clicked.connect(this, &ListMenuFrame::OnCancel);
    append(&cancelButton);

    select(initialSelection);
}

ListMenuFrame::~ListMenuFrame() {
    for (auto &option : options) {
        remove(option.button);
        delete option.button;
        delete option.background;
        delete option.text;
    }
    options.clear();
}

void ListMenuFrame::select(int32_t index) {
    if (options.empty()) {
        return;
    }
    selected = std::max(0, std::min(index, (int32_t) options.size() - 1));

    //! show a window of MAX_ROWS rows that contains the selection
    int32_t count = (int32_t) options.size();
    int32_t first = 0;
    if (count > MAX_ROWS) {
        first = std::min(std::max(0, selected - MAX_ROWS / 2), count - MAX_ROWS);
    }
    int32_t rows = std::min(count, MAX_ROWS);
    int32_t top  = panelHeight(options.size()) / 2 - 95;

    for (int32_t i = 0; i < count; i++) {
        bool visible = i >= first && i < first + rows;
        options[i].button->setVisible(visible);
        if (visible) {
            options[i].button->clearState(GuiElement::STATE_DISABLED);
        } else {
            options[i].button->setState(GuiElement::STATE_DISABLED);
        }
        options[i].button->setPosition(0, top - (i - first) * (ROW_H + ROW_GAP) - ROW_H / 2);

        bool isSelected = i == selected;
        options[i].background->setImageColor(isSelected ? rgba(28, 167, 255, 200) : rgba(255, 255, 255, 30));
    }
}

void ListMenuFrame::OnOptionClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    for (int32_t i = 0; i < (int32_t) options.size(); i++) {
        if (options[i].button == button && options[i].button->isVisible()) {
            select(i);
            OnConfirm(nullptr, controller, trigger);
            return;
        }
    }
}

void ListMenuFrame::OnMove(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    select(button == &upButton ? selected - 1 : selected + 1);
}

void ListMenuFrame::OnConfirm(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    if (done || options.empty()) {
        return;
    }
    done = true;
    optionChosen(this, selected);
}

void ListMenuFrame::OnCancel(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    if (done) {
        return;
    }
    done = true;
    cancelled(this);
}
