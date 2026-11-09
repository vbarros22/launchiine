#include "Accounts.h"
#include "fs/FSUtils.h"
#include "utils/TgaUtils.h"
#include "utils/logger.h"
#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <nn/act.h>
#include <sysapp/launch.h>

namespace {
    constexpr const char *SETTINGS_DIR = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *BOOT_FILE    = "fs:/vol/external01/wiiu/launchiine/accountChosen.txt";
    constexpr uint8_t MAX_SLOTS        = 12;

    //! the system time at which the console was turned on, in OSTime ticks
    OSTime bootTime() {
        return OSGetTime() - OSGetSystemTime();
    }

    std::string utf16ToUtf8(const int16_t *str, size_t maxLen) {
        std::string out;
        for (size_t i = 0; i < maxLen && str[i] != 0; ++i) {
            uint32_t cp = (uint16_t) str[i];
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < maxLen) {
                uint32_t low = (uint16_t) str[i + 1];
                if (low >= 0xDC00 && low <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    ++i;
                }
            }
            if (cp < 0x80) {
                out += (char) cp;
            } else if (cp < 0x800) {
                out += (char) (0xC0 | (cp >> 6));
                out += (char) (0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += (char) (0xE0 | (cp >> 12));
                out += (char) (0x80 | ((cp >> 6) & 0x3F));
                out += (char) (0x80 | (cp & 0x3F));
            } else {
                out += (char) (0xF0 | (cp >> 18));
                out += (char) (0x80 | ((cp >> 12) & 0x3F));
                out += (char) (0x80 | ((cp >> 6) & 0x3F));
                out += (char) (0x80 | (cp & 0x3F));
            }
        }
        return out;
    }

    struct ActSession {
        ActSession() {
            nn::act::Initialize();
        }
        ~ActSession() {
            nn::act::Finalize();
        }
    };
} // namespace

std::vector<Accounts::Account> Accounts::list() {
    ActSession act;
    std::vector<Account> result;
    uint8_t defaultSlot = nn::act::GetDefaultAccount();

    //! GetMiiImageEx writes by DMA: 0x40 aligned buffer with a 0x40 multiple size
    constexpr uint32_t IMAGE_BUFFER_SIZE = 0x10040;
    auto *imageBuffer                    = (uint8_t *) memalign(0x40, IMAGE_BUFFER_SIZE);

    for (uint8_t slot = 1; slot <= MAX_SLOTS; slot++) {
        if (!nn::act::IsSlotOccupied(slot)) {
            continue;
        }
        Account account;
        account.slot      = slot;
        account.isDefault = slot == defaultSlot;

        int16_t name[nn::act::MiiNameSize + 1] = {};
        if (nn::act::GetMiiNameEx(name, slot).IsSuccess()) {
            account.name = utf16ToUtf8(name, nn::act::MiiNameSize);
        }
        if (account.name.empty()) {
            account.name = "User " + std::to_string(slot);
        }

        if (imageBuffer) {
            //! the image is written by IOS behind the CPU cache: no dirty lines may be written
            //! back over it later and no stale lines may be read afterwards
            memset(imageBuffer, 0, IMAGE_BUFFER_SIZE);
            DCFlushRange(imageBuffer, IMAGE_BUFFER_SIZE);
            size_t imageSize = 0;
            auto res         = nn::act::GetMiiImageEx(&imageSize, imageBuffer, IMAGE_BUFFER_SIZE, 0, slot);
            DCInvalidateRange(imageBuffer, IMAGE_BUFFER_SIZE);
            if (res.IsSuccess() && imageSize > 0 && imageSize <= IMAGE_BUFFER_SIZE) {
                DEBUG_FUNCTION_LINE("Slot %u: Mii TGA header %02X %02X %02X | %ux%u %u bpp, descriptor %02X", slot, imageBuffer[0], imageBuffer[1], imageBuffer[2],
                                    imageBuffer[12] | (imageBuffer[13] << 8), imageBuffer[14] | (imageBuffer[15] << 8), imageBuffer[16], imageBuffer[17]);
                //! raw copy for debugging
                FSUtils::CreateSubfolder(SETTINGS_DIR);
                FSUtils::saveBufferToFile("fs:/vol/external01/wiiu/launchiine/mii-debug.tga", imageBuffer, imageSize);
                if (!sanitizeTga(imageBuffer, imageSize, account.miiTga)) {
                    DEBUG_FUNCTION_LINE("Slot %u: Mii image is not a supported TGA (%u bytes)", slot, (uint32_t) imageSize);
                    account.miiTga.clear();
                }
            } else {
                DEBUG_FUNCTION_LINE("Slot %u: no Mii image (%08X)", slot, (unsigned) ((NNResult) res).value);
            }
        }

        DEBUG_FUNCTION_LINE("Account slot %u \"%s\"%s, Mii image %u bytes", slot, account.name.c_str(), account.isDefault ? " (default)" : "",
                            (uint32_t) account.miiTga.size());
        result.push_back(std::move(account));
    }
    free(imageBuffer);
    return result;
}

uint8_t Accounts::currentSlot() {
    ActSession act;
    return nn::act::GetSlotNo();
}

uint8_t Accounts::defaultSlot() {
    ActSession act;
    return nn::act::GetDefaultAccount();
}

bool Accounts::shouldAskAtStart() {
    if (defaultSlot() != 0) {
        return false;
    }
    //! the menu restarts after every game; only ask once per boot
    uint8_t *buffer = nullptr;
    uint32_t size   = 0;
    if (FSUtils::LoadFileToMem(BOOT_FILE, &buffer, &size) > 0 && buffer) {
        std::string text((const char *) buffer, size);
        free(buffer);
        OSTime saved = (OSTime) strtoull(text.c_str(), nullptr, 16);
        OSTime diff  = saved > bootTime() ? saved - bootTime() : bootTime() - saved;
        if (diff < (OSTime) OSSecondsToTicks(5)) {
            return false;
        }
    }
    return true;
}

void Accounts::rememberChosenThisBoot() {
    char text[32];
    int len = snprintf(text, sizeof(text), "%016llX\n", (unsigned long long) bootTime());
    FSUtils::CreateSubfolder(SETTINGS_DIR);
    FSUtils::saveBufferToFile(BOOT_FILE, text, len);
}

bool Accounts::switchTo(uint8_t slot) {
    ActSession act;
    auto res = nn::act::LoadConsoleAccount(slot, 0, nullptr, false);
    DEBUG_FUNCTION_LINE("LoadConsoleAccount(%u): %s (%08X)", slot, res.IsSuccess() ? "ok" : "failed", (unsigned) ((NNResult) res).value);
    if (!res.IsSuccess()) {
        return false;
    }
    //! restart the menu so everything (save data, friends, ...) uses the new account
    SYSLaunchMenu();
    return true;
}
