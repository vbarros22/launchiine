#include "Folders.h"
#include "fs/FSUtils.h"
#include "game/GameList.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <malloc.h>
#include <set>

namespace {
    constexpr const char *SETTINGS_DIR   = "fs:/vol/external01/wiiu/launchiine";
    constexpr const char *FOLDERS_FILE   = "fs:/vol/external01/wiiu/launchiine/folders.txt";
    constexpr const char *TAKEN_OUT_FILE = "fs:/vol/external01/wiiu/launchiine/outsideFolders.txt"; // v0.2

    bool sLoaded = false;
    std::vector<Folders::UserFolder> sUserFolders;
    //! titles that are not in their natural folder: title id -> folder (0 = main grid)
    std::map<uint64_t, uint64_t> sMoved;

    std::string readFile(const char *path) {
        uint8_t *buffer = nullptr;
        uint32_t size   = 0;
        if (FSUtils::LoadFileToMem(path, &buffer, &size) <= 0 || buffer == nullptr) {
            return "";
        }
        std::string text((const char *) buffer, size);
        free(buffer);
        return text;
    }

    std::vector<std::string> splitLines(const std::string &text) {
        std::vector<std::string> lines;
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                end = text.size();
            }
            std::string line = text.substr(start, end - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(line);
            start = end + 1;
        }
        return lines;
    }

    bool userFolderExists(uint64_t id) {
        for (auto &f : sUserFolders) {
            if (f.id == id) {
                return true;
            }
        }
        return false;
    }

    void load() {
        if (sLoaded) {
            return;
        }
        sLoaded = true;

        std::string text = readFile(FOLDERS_FILE);
        if (text.empty()) {
            //! v0.2 only stored titles taken out of their folder
            for (auto &line : splitLines(readFile(TAKEN_OUT_FILE))) {
                uint64_t titleId = strtoull(line.c_str(), nullptr, 16);
                if (titleId != 0) {
                    sMoved[titleId] = 0;
                }
            }
            DEBUG_FUNCTION_LINE("Imported %d titles from outsideFolders.txt", (int) sMoved.size());
            return;
        }

        //! "folder\t<id>\t<name>" and "move\t<title id>\t<folder id>"
        for (auto &line : splitLines(text)) {
            size_t tab1 = line.find('\t');
            size_t tab2 = tab1 == std::string::npos ? std::string::npos : line.find('\t', tab1 + 1);
            if (tab2 == std::string::npos) {
                continue;
            }
            std::string kind = line.substr(0, tab1);
            uint64_t a       = strtoull(line.substr(tab1 + 1, tab2 - tab1 - 1).c_str(), nullptr, 16);
            std::string rest = line.substr(tab2 + 1);
            if (kind == "folder" && Folders::isUserFolder(a) && !rest.empty()) {
                sUserFolders.push_back({a, rest});
            } else if (kind == "move" && a != 0) {
                sMoved[a] = strtoull(rest.c_str(), nullptr, 16);
            }
        }
        //! moves into folders that no longer exist go to the main grid
        for (auto &entry : sMoved) {
            if (Folders::isUserFolder(entry.second) && !userFolderExists(entry.second)) {
                entry.second = 0;
            }
        }
        DEBUG_FUNCTION_LINE("%d user folders, %d moved titles", (int) sUserFolders.size(), (int) sMoved.size());
    }

    void save() {
        std::string text;
        char line[64];
        for (auto &f : sUserFolders) {
            std::string name = f.name;
            std::replace(name.begin(), name.end(), '\t', ' ');
            std::replace(name.begin(), name.end(), '\n', ' ');
            snprintf(line, sizeof(line), "folder\t%016llX\t", (unsigned long long) f.id);
            text += line + name + "\n";
        }
        for (auto &entry : sMoved) {
            snprintf(line, sizeof(line), "move\t%016llX\t%016llX\n", (unsigned long long) entry.first, (unsigned long long) entry.second);
            text += line;
        }
        FSUtils::CreateSubfolder(SETTINGS_DIR);
        if (FSUtils::saveBufferToFile(FOLDERS_FILE, (void *) text.data(), text.size()) != (int32_t) text.size()) {
            DEBUG_FUNCTION_LINE("Failed to save %s", FOLDERS_FILE);
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
    if (isFolder(titleId)) {
        return 0;
    }
    load();
    auto it = sMoved.find(titleId);
    if (it != sMoved.end()) {
        return it->second;
    }
    return naturalFolder(titleId);
}

void Folders::moveTo(uint64_t titleId, uint64_t folder) {
    if (isFolder(titleId)) {
        return;
    }
    load();
    if (folder == naturalFolder(titleId)) {
        sMoved.erase(titleId);
    } else {
        sMoved[titleId] = folder;
    }
    DEBUG_FUNCTION_LINE("%016llX moved to \"%s\"", titleId, folder ? folderName(folder).c_str() : "main grid");
    save();
}

std::vector<Folders::UserFolder> Folders::userFolders() {
    load();
    return sUserFolders;
}

uint64_t Folders::createFolder(const std::string &name) {
    load();
    uint64_t id = USER_FOLDER_BASE + 1;
    for (auto &f : sUserFolders) {
        id = std::max(id, f.id + 1);
    }
    sUserFolders.push_back({id, name});
    DEBUG_FUNCTION_LINE("Created folder \"%s\"", name.c_str());
    save();
    return id;
}

void Folders::renameFolder(uint64_t folder, const std::string &name) {
    load();
    for (auto &f : sUserFolders) {
        if (f.id == folder) {
            f.name = name;
        }
    }
    save();
}

void Folders::deleteFolder(uint64_t folder) {
    load();
    sUserFolders.erase(std::remove_if(sUserFolders.begin(), sUserFolders.end(), [folder](const UserFolder &f) { return f.id == folder; }),
                       sUserFolders.end());
    for (auto it = sMoved.begin(); it != sMoved.end();) {
        if (it->second == folder) {
            //! back to the main grid; titles with a natural folder stay out of it
            if (naturalFolder(it->first) == 0) {
                it = sMoved.erase(it);
                continue;
            }
            it->second = 0;
        }
        ++it;
    }
    DEBUG_FUNCTION_LINE("Deleted folder %016llX", folder);
    save();
}

void Folders::deleteEmptyFolders(const std::vector<uint64_t> &titles) {
    load();
    std::set<uint64_t> used;
    for (auto titleId : titles) {
        auto it = sMoved.find(titleId);
        if (it != sMoved.end()) {
            used.insert(it->second);
        }
    }
    std::vector<uint64_t> empty;
    for (auto &f : sUserFolders) {
        if (used.count(f.id) == 0) {
            empty.push_back(f.id);
        }
    }
    for (auto id : empty) {
        deleteFolder(id);
    }
}

std::string Folders::folderName(uint64_t folder) {
    switch (folder) {
        case FOLDER_SYSTEM:
            return "System";
        case FOLDER_HOMEBREW:
            return "Homebrew";
        default:
            break;
    }
    load();
    for (auto &f : sUserFolders) {
        if (f.id == folder) {
            return f.name;
        }
    }
    return "";
}

uint32_t Folders::folderColor(uint64_t folder) {
    if (folder == FOLDER_SYSTEM) {
        return 0x3A7BD5;
    }
    if (folder == FOLDER_HOMEBREW) {
        return 0xE8892B;
    }
    //! user folders cycle through a few colors that differ from the fixed ones
    static const uint32_t palette[] = {0x4CAF50, 0xAB47BC, 0xE53935, 0x26A69A, 0xFDD835, 0x8D6E63};
    return palette[(folder - USER_FOLDER_BASE) % (sizeof(palette) / sizeof(palette[0]))];
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
