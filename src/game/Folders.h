#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Fixed folders: "System" (System Settings, eShop, ...) and "Homebrew" (sd:/wiiu/apps).
 * Single titles can be taken out of their folder; that choice is stored on the SD card.
 * Render thread only.
 */
namespace Folders {
    //! pseudo title ids for the folder icons, never used by real titles
    constexpr uint64_t FOLDER_SYSTEM   = 0xFFFF000000000001ULL;
    constexpr uint64_t FOLDER_HOMEBREW = 0xFFFF000000000002ULL;

    inline bool isFolder(uint64_t titleId) {
        return titleId == FOLDER_SYSTEM || titleId == FOLDER_HOMEBREW;
    }

    //! the folder a title belongs to by its type, 0 for none
    uint64_t naturalFolder(uint64_t titleId);

    //! the folder a title is shown in, 0 when it is on the main grid
    uint64_t folderOf(uint64_t titleId);

    bool isTakenOut(uint64_t titleId);

    //! take a title out of its folder (or put it back) and save the choice
    void setTakenOut(uint64_t titleId, bool takenOut);

    const char *folderName(uint64_t folder);

    //! 128x128 32 bit TGA of a folder in the given color (0xRRGGBB)
    std::vector<uint8_t> makeFolderIconTga(uint32_t rgb);
} // namespace Folders
