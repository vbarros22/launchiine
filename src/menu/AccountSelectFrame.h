#pragma once

#include "game/Accounts.h"
#include <gui/GuiButton.h>
#include <gui/GuiFrame.h>
#include <gui/GuiImage.h>
#include <gui/GuiImageData.h>
#include <gui/GuiText.h>
#include <gui/GuiTrigger.h>
#include <gui/sigslot.h>
#include <vector>

/**
 * Full screen "who is using the Wii U?" picker: the Miis of all accounts, D-pad or touch to
 * choose, A to confirm, B to cancel (when allowed).
 */
class AccountSelectFrame : public GuiFrame, public sigslot::has_slots<> {
public:
    AccountSelectFrame(int32_t w, int32_t h, const std::vector<Accounts::Account> &accounts, uint8_t currentSlot, bool allowCancel);

    ~AccountSelectFrame() override;

    void process() override;

    sigslot::signal2<AccountSelectFrame *, uint8_t> accountChosen;
    sigslot::signal1<AccountSelectFrame *> cancelled;

private:
    struct Entry {
        uint8_t slot           = 0;
        GuiImageData *miiData  = nullptr;
        GuiImage *miiImage     = nullptr;
        GuiImage *placeholder  = nullptr;
        GuiText *nameText      = nullptr;
        GuiButton *button      = nullptr;
    };

    void select(int32_t index);

    void OnEntryClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
    void OnMove(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
    void OnConfirm(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);
    void OnCancel(GuiButton *button, const GuiController *controller, GuiTrigger *trigger);

    static constexpr int32_t COLUMNS = 6;

    bool allowCancel;
    bool done        = false;
    int32_t selected = 0;
    std::vector<Entry> entries;

    GuiImage background;
    GuiText titleText;
    GuiText hintText;

    GuiTrigger touchTrigger;
    GuiTrigger wpadTouchTrigger;
    GuiTrigger leftTrigger, rightTrigger, upTrigger, downTrigger;
    GuiTrigger aTrigger, bTrigger;
    GuiButton leftButton, rightButton, upButton, downButton, confirmButton, cancelButton;
};
