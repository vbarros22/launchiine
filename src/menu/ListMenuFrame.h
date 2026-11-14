#pragma once

#include <gui/GuiButton.h>
#include <gui/GuiFrame.h>
#include <gui/GuiImage.h>
#include <gui/GuiText.h>
#include <gui/GuiTrigger.h>
#include <gui/sigslot.h>
#include <string>
#include <vector>

/**
 * Small modal menu in the style of the Wii U Menu's dialogs: white panel with a dark header,
 * glossy option buttons, cyan corner brackets on the selection and a dark Back button.
 * D-pad / touch to choose, A to confirm, B (or Back) to cancel.
 */
class ListMenuFrame : public GuiFrame, public sigslot::has_slots<> {
public:
    ListMenuFrame(int32_t w, int32_t h, const std::string &title, const std::vector<std::string> &options, int32_t initialSelection = 0);

    ~ListMenuFrame() override;

    //! index of the chosen option
    sigslot::signal2<ListMenuFrame *, int32_t> optionChosen;
    sigslot::signal1<ListMenuFrame *> cancelled;

private:
    void select(int32_t index);

    void OnOptionClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
    void OnMove(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
    void OnConfirm(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
    void OnCancel(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    struct Option {
        GuiImage *background = nullptr;
        GuiText *text        = nullptr;
        GuiButton *button    = nullptr;
    };

    bool done        = false;
    int32_t selected = 0; // options.size() is the Back button
    std::vector<Option> options;

    GuiImageData *topData, *midData, *bottomData, *rowData, *backData, *selectData;
    GuiImage dim;
    GuiImage panelTop, panelMid, panelBottom;
    GuiImage selectImage;
    GuiText titleText;
    GuiImage backImage;
    GuiText backText;
    GuiButton backButton;

    GuiTrigger touchTrigger;
    GuiTrigger wpadTouchTrigger;
    GuiTrigger upTrigger, downTrigger, aTrigger, bTrigger;
    GuiButton upButton, downButton, confirmButton, cancelButton;
};
