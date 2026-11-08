#include "KeyboardHelper.h"
#include "utils/logger.h"
#include <coreinit/memdefaultheap.h>
#include <nn/swkbd.h>
#include <string.h>
#include <vpad/input.h>

namespace {
    //! Input form text is UTF-16. Convert to UTF-8 so non ASCII names (é, ã, Japanese...) survive.
    std::string utf16ToUtf8(const char16_t *str, size_t maxLen) {
        std::string out;
        if (str == nullptr) {
            return out;
        }
        for (size_t i = 0; i < maxLen && str[i] != 0; ++i) {
            uint32_t cp = str[i];
            // surrogate pair
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < maxLen && str[i + 1] >= 0xDC00 && str[i + 1] <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (str[i + 1] - 0xDC00);
                ++i;
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
} // namespace

KeyboardHelper::KeyboardHelper() {
    fsClient = (FSClient *) MEMAllocFromDefaultHeap(sizeof(FSClient));
    if (fsClient == nullptr) {
        DEBUG_FUNCTION_LINE("Failed to allocate FSClient");
        return;
    }
    FSAddClient(fsClient, FS_ERROR_FLAG_ALL);

    uint32_t workMemorySize = nn::swkbd::GetWorkMemorySize(0);
    workMemory              = MEMAllocFromDefaultHeap(workMemorySize);
    if (workMemory == nullptr) {
        DEBUG_FUNCTION_LINE("Failed to allocate swkbd work memory");
        FSDelClient(fsClient, FS_ERROR_FLAG_ALL);
        MEMFreeToDefaultHeap(fsClient);
        fsClient = nullptr;
        return;
    }
    memset(workMemory, 0, workMemorySize);

    nn::swkbd::CreateArg createArg;
    createArg.regionType = nn::swkbd::RegionType::Europe;
    createArg.workMemory = workMemory;
    createArg.fsClient   = fsClient;
    DEBUG_FUNCTION_LINE("Calling create");
    if (!nn::swkbd::Create(createArg)) {
        DEBUG_FUNCTION_LINE("Failed to create keyboard");
        MEMFreeToDefaultHeap(workMemory);
        workMemory = nullptr;
        FSDelClient(fsClient, FS_ERROR_FLAG_ALL);
        MEMFreeToDefaultHeap(fsClient);
        fsClient = nullptr;
        return;
    }

    keyboardCreated = true;
}

KeyboardHelper::~KeyboardHelper() {
    if (keyboardCreated) {
        nn::swkbd::Destroy();
        MEMFreeToDefaultHeap(this->workMemory);
        this->workMemory = nullptr;

        FSDelClient(fsClient, FS_ERROR_FLAG_ALL);
        MEMFreeToDefaultHeap(this->fsClient);
        this->fsClient  = nullptr;
        keyboardCreated = false;
    }
}

bool KeyboardHelper::openKeyboard() {
    if (!keyboardCreated || keyboardOpen) {
        return false;
    }
    nn::swkbd::AppearArg appearArg;
    appearArg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::English;
    if (!nn::swkbd::AppearInputForm(appearArg)) {
        DEBUG_FUNCTION_LINE("nn::swkbd::AppearInputForm failed");
        return false;
    }
    confirmed    = false;
    keyboardOpen = true;
    return true;
}

std::string KeyboardHelper::getResult() {
    return resultStr;
}

bool KeyboardHelper::checkResult() {
    if (!keyboardCreated) {
        return false;
    }

    //! Never hand uninitialized input to swkbd. While closed it only needs Calc() for its
    //! fade out, so an empty status is enough. While open we are the only VPADRead caller,
    //! see Application::executeThread.
    VPADStatus vpadStatus;
    memset(&vpadStatus, 0, sizeof(vpadStatus));
    if (keyboardOpen) {
        VPADReadError error = VPAD_READ_NO_SAMPLES;
        VPADRead(VPAD_CHAN_0, &vpadStatus, 1, &error);
        if (error != VPAD_READ_SUCCESS) {
            memset(&vpadStatus, 0, sizeof(vpadStatus));
        } else {
            VPADGetTPCalibratedPoint(VPAD_CHAN_0, &vpadStatus.tpNormal, &vpadStatus.tpNormal);
        }
    }

    nn::swkbd::ControllerInfo controllerInfo;
    controllerInfo.vpad    = &vpadStatus;
    controllerInfo.kpad[0] = nullptr;
    controllerInfo.kpad[1] = nullptr;
    controllerInfo.kpad[2] = nullptr;
    controllerInfo.kpad[3] = nullptr;
    nn::swkbd::Calc(controllerInfo);

    if (nn::swkbd::IsNeedCalcSubThreadFont()) {
        nn::swkbd::CalcSubThreadFont();
    }

    if (nn::swkbd::IsNeedCalcSubThreadPredict()) {
        nn::swkbd::CalcSubThreadPredict();
    }

    if (!keyboardOpen) {
        return false;
    }

    bool ok     = nn::swkbd::IsDecideOkButton(nullptr);
    bool cancel = !ok && nn::swkbd::IsDecideCancelButton(nullptr);
    if (!ok && !cancel) {
        return false;
    }

    confirmed = ok;
    if (ok) {
        resultStr = utf16ToUtf8(nn::swkbd::GetInputFormString(), 256);
    }
    keyboardOpen = false;
    nn::swkbd::DisappearInputForm();
    return true;
}

void KeyboardHelper::drawDRC() {
    nn::swkbd::DrawDRC();
}

void KeyboardHelper::drawTV() {
    nn::swkbd::DrawTV();
}
