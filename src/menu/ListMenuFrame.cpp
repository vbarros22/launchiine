#include "ListMenuFrame.h"
#include "resources/Resources.h"
#include "utils/logger.h"
#include <algorithm>

namespace {
    //! layout units; the textures are 1.5x for 1080p
    constexpr float TEX_SCALE   = 1.0f / 1.5f;
    constexpr int32_t ROW_W     = 560;
    constexpr int32_t ROW_H     = 62;
    constexpr int32_t ROW_GAP   = 14;
    constexpr int32_t MAX_ROWS  = 6; // more than that does not fit, the list scrolls with the selection
    constexpr int32_t HEADER_H  = 64;
    constexpr int32_t BOTTOM_H  = 40;
    constexpr int32_t BACK_H    = 56;
    constexpr int32_t PADDING   = 24;
    constexpr int32_t BACK_GAP  = 30;

    GX2Color rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        GX2Color c;
        c.r = r;
        c.g = g;
        c.b = b;
        c.a = a;
        return c;
    }

    int32_t panelHeight(size_t count) {
        int32_t rows = (int32_t) std::min<size_t>(std::max<size_t>(count, 1), MAX_ROWS);
        return HEADER_H + PADDING + rows * (ROW_H + ROW_GAP) - ROW_GAP + BACK_GAP + BACK_H + PADDING;
    }
} // namespace

ListMenuFrame::ListMenuFrame(int32_t w, int32_t h, const std::string &title, const std::vector<std::string> &labels, int32_t initialSelection)
    : GuiFrame(w, h),
      topData(Resources::GetImageData("dialogTop.png")), midData(Resources::GetImageData("dialogMid.png")),
      bottomData(Resources::GetImageData("dialogBottom.png")), rowData(Resources::GetImageData("dialogRow.png")),
      backData(Resources::GetImageData("dialogBack.png")), selectData(Resources::GetImageData("dialogSelect.png")),
      dim(w, h, rgba(40, 46, 52, 120)),
      panelTop(topData), panelMid(midData), panelBottom(bottomData), selectImage(selectData),
      titleText(title.c_str(), 30, glm::vec4(1.0f)),
      backImage(backData), backText("Back", 30, glm::vec4(1.0f)), backButton(ROW_W, BACK_H),
      touchTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH),
      wpadTouchTrigger(GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5, GuiTrigger::BUTTON_A),
      upTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_UP | GuiTrigger::STICK_L_UP, true),
      downTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_DOWN | GuiTrigger::STICK_L_DOWN, true),
      aTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A, true),
      bTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_B, true),
      upButton(w, h), downButton(w, h), confirmButton(w, h), cancelButton(w, h) {

    append(&dim);

    //! panel: header, a white middle stretched to the height, rounded bottom
    float height = (float) panelHeight(labels.size());
    float top    = height * 0.5f;
    panelTop.setScale(TEX_SCALE);
    panelTop.setPosition(0, top - HEADER_H * 0.5f);
    panelBottom.setScale(TEX_SCALE);
    panelBottom.setPosition(0, -top + BOTTOM_H * 0.5f);
    float midHeight = height - HEADER_H - BOTTOM_H + 2.0f;
    panelMid.setScaleX(TEX_SCALE);
    panelMid.setScaleY(midHeight / panelMid.getHeight());
    panelMid.setPosition(0, (top - HEADER_H + (-top + BOTTOM_H)) * 0.5f);
    append(&panelMid);
    append(&panelTop);
    append(&panelBottom);

    titleText.setPosition(0, top - HEADER_H * 0.5f);
    titleText.setMaxWidth(ROW_W + 20, GuiText::DOTTED);
    append(&titleText);

    for (size_t i = 0; i < labels.size(); i++) {
        Option option;
        option.button     = new GuiButton(ROW_W, ROW_H);
        option.background = new GuiImage(rowData);
        option.background->setScale(TEX_SCALE);
        option.button->setImage(option.background);
        option.text = new GuiText(labels[i].c_str(), 30, glm::vec4(0.2f, 0.21f, 0.23f, 1.0f));
        option.text->setMaxWidth(ROW_W - 30, GuiText::DOTTED);
        option.button->setLabel(option.text);
        option.button->setTrigger(&touchTrigger);
        option.button->setTrigger(&wpadTouchTrigger);
        option.button->clicked.connect(this, &ListMenuFrame::OnOptionClick);
        append(option.button);
        options.push_back(option);
    }

    backImage.setScale(TEX_SCALE);
    backButton.setImage(&backImage);
    backButton.setLabel(&backText);
    backButton.setPosition(0, -top + PADDING + BACK_H * 0.5f);
    backButton.setTrigger(&touchTrigger);
    backButton.setTrigger(&wpadTouchTrigger);
    backButton.clicked.connect(this, &ListMenuFrame::OnCancel);
    append(&backButton);

    selectImage.setScale(TEX_SCALE);
    append(&selectImage);

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
    for (auto *data : {topData, midData, bottomData, rowData, backData, selectData}) {
        Resources::RemoveImageData(data);
    }
}

void ListMenuFrame::select(int32_t index) {
    int32_t count = (int32_t) options.size();
    //! 0..count-1 are the options, count is the Back button
    selected = std::max(0, std::min(index, count));

    //! show a window of MAX_ROWS rows that contains the selection
    int32_t focus = std::min(selected, count - 1);
    int32_t first = 0;
    if (count > MAX_ROWS) {
        first = std::min(std::max(0, focus - MAX_ROWS / 2), count - MAX_ROWS);
    }
    int32_t rows = std::min(count, MAX_ROWS);
    float top    = panelHeight(options.size()) * 0.5f - HEADER_H - PADDING;

    for (int32_t i = 0; i < count; i++) {
        bool visible = i >= first && i < first + rows;
        options[i].button->setVisible(visible);
        if (visible) {
            options[i].button->clearState(GuiElement::STATE_DISABLED);
        } else {
            options[i].button->setState(GuiElement::STATE_DISABLED);
        }
        options[i].button->setPosition(0, top - (i - first) * (ROW_H + ROW_GAP) - ROW_H * 0.5f);
    }

    //! cyan corner brackets around the selection
    if (selected < count) {
        selectImage.setScaleY(TEX_SCALE);
        selectImage.setPosition(0, options[selected].button->getOffsetY());
    } else {
        selectImage.setScaleY(TEX_SCALE * (BACK_H + 24.0f) / (ROW_H + 24.0f));
        selectImage.setPosition(0, backButton.getOffsetY());
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
    if (selected >= (int32_t) options.size()) {
        OnCancel(button, controller, trigger); // Back
        return;
    }
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
