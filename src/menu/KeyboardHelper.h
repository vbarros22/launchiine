#pragma once

#include <coreinit/filesystem.h>
#include <string>

class KeyboardHelper {
public:
    KeyboardHelper();

    ~KeyboardHelper();

    /**
     * Must be called once per frame while isReady(), also after the keyboard was closed so
     * its fade out animation can finish. Returns true once, in the frame the user pressed
     * OK or Cancel. Use wasConfirmed() to tell them apart.
     */
    bool checkResult();

    static void drawTV();

    static void drawDRC();

    bool openKeyboard();

    bool isReady() const {
        return keyboardCreated;
    }

    bool isOpen() const {
        return keyboardOpen;
    }

    bool wasConfirmed() const {
        return confirmed;
    }

    //! UTF-8
    std::string getResult();

private:
    void *workMemory      = nullptr;
    FSClient *fsClient    = nullptr;
    bool keyboardOpen     = false;
    bool keyboardCreated  = false;
    bool confirmed        = false;
    std::string resultStr = "";
};
