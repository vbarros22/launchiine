#include "Folders.h"
#include "fs/FSUtils.h"
#include "game/GameList.h"
#include "utils/logger.h"
#include <cstdlib>
#include <malloc.h>
#include <set>

namespace {
    constexpr const char *SETTINGS_DIR     = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *TAKEN_OUT_FILE   = "fs:/vol/external01/wiiu/launchiine/outsideFolders.txt";
    bool sLoaded                           = false;
    std::set<uint64_t> sTakenOut;

    void loadTakenOut() {
        if (sLoaded) {
            return;
        }
        sLoaded         = true;
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(TAKEN_OUT_FILE, &buffer, &size) <= 0 || buffer == nullptr) {
            return;
        }
        std::string text((const char *) buffer, size);
        free(buffer);
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                end = text.size();
            }
            uint64_t titleId = strtoull(text.substr(start, end - start).c_str(), nullptr, 16);
            if (titleId != 0) {
                sTakenOut.insert(titleId);
            }
            start = end + 1;
        }
        DEBUG_FUNCTION_LINE("%d titles are outside their folder", (int) sTakenOut.size());
    }

    void saveTakenOut() {
        std::string text;
        char line[20];
        for (auto titleId : sTakenOut) {
            snprintf(line, sizeof(line), "%016llX\n", (unsigned long long) titleId);
            text += line;
        }
        FSUtils::CreateSubfolder(SETTINGS_DIR);
        if (FSUtils::saveBufferToFile(TAKEN_OUT_FILE, (void *) text.data(), text.size()) != (int32_t) text.size()) {
            DEBUG_FUNCTION_LINE("Failed to save %s", TAKEN_OUT_FILE);
        }
    }
} // namespace

uint64_t Folders::naturalFolder(uint64_t titleId) {
    if (isFolder(titleId)) {
        return 0;
    }
    if (isSystemTitleId(titleId)) {
        return FOLDER_SYSTEM;
    }
    if (isHomebrewTitle(titleId)) {
        return FOLDER_HOMEBREW;
    }
    return 0;
}

uint64_t Folders::folderOf(uint64_t titleId) {
    uint64_t folder = naturalFolder(titleId);
    if (folder == 0 || isTakenOut(titleId)) {
        return 0;
    }
    return folder;
}

bool Folders::isTakenOut(uint64_t titleId) {
    loadTakenOut();
    return sTakenOut.count(titleId) != 0;
}

void Folders::setTakenOut(uint64_t titleId, bool takenOut) {
    loadTakenOut();
    if (takenOut) {
        sTakenOut.insert(titleId);
    } else {
        sTakenOut.erase(titleId);
    }
    saveTakenOut();
}

const char *Folders::folderName(uint64_t folder) {
    switch (folder) {
        case FOLDER_SYSTEM:
            return "System";
        case FOLDER_HOMEBREW:
            return "Homebrew";
        default:
            return "";
    }
}

std::vector<uint8_t> Folders::makeFolderIconTga(uint32_t rgb) {
    constexpr int SIZE = 128;
    std::vector<uint8_t> tga(18 + SIZE * SIZE * 4, 0);
    tga[2]  = 2; // uncompressed true color
    tga[12] = SIZE;
    tga[14] = SIZE;
    tga[16] = 32;
    tga[17] = 0x28; // top left origin, 8 alpha bits

    const int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

    //! inside a rounded rectangle?
    auto inRounded = [](int x, int y, int x0, int y0, int x1, int y1, int radius) {
        if (x < x0 || x > x1 || y < y0 || y > y1) {
            return false;
        }
        int cx = x < x0 + radius ? x0 + radius : (x > x1 - radius ? x1 - radius : x);
        int cy = y < y0 + radius ? y0 + radius : (y > y1 - radius ? y1 - radius : y);
        int dx = x - cx, dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    };

    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            uint8_t *px = &tga[18 + (y * SIZE + x) * 4];
            float shade = 0.0f;
            bool filled = false;
            if (inRounded(x, y, 16, 34, 112, 106, 8)) {
                //! front, slightly lighter towards the top
                shade  = 1.0f - 0.25f * (float) (y - 34) / 72.0f;
                filled = true;
            } else if (inRounded(x, y, 16, 20, 112, 50, 6) || inRounded(x, y, 16, 14, 58, 30, 5)) {
                //! back panel and tab
                shade  = 0.65f;
                filled = true;
            }
            if (!filled) {
                continue;
            }
            px[0] = (uint8_t) (b * shade);
            px[1] = (uint8_t) (g * shade);
            px[2] = (uint8_t) (r * shade);
            px[3] = 0xFF;
        }
    }
    return tga;
}
