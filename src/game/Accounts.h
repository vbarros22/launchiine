#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Wii U user accounts (nn::act). Render thread only.
 */
namespace Accounts {
    struct Account {
        uint8_t slot = 0;
        std::string name;            // Mii name, UTF-8
        std::vector<uint8_t> miiTga; // sanitized 32 bit TGA of the Mii, empty if unavailable
        bool isDefault = false;
    };

    //! every account on the console, in slot order
    std::vector<Account> list();

    //! slot of the account that is loaded now (0 if unknown)
    uint8_t currentSlot();

    //! slot of the account that starts automatically (0 if none is set)
    uint8_t defaultSlot();

    //! No default account and nobody was picked since the console was turned on.
    //! The Wii U Menu asks "who is using" in that case.
    bool shouldAskAtStart();

    //! remember that a user was picked during this boot
    void rememberChosenThisBoot();

    //! load another account and restart the menu with it; false if loading failed
    bool switchTo(uint8_t slot);
} // namespace Accounts
