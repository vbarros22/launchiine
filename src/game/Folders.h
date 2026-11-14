#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Folders on the grid:
 *  - fixed ones: "System" (System Settings, eShop, ...) and "Homebrew" (sd:/wiiu/apps),
 *    which titles belong to by their type ("natural" folder)
 *  - folders the user creates
 * Every title can be moved: to the main grid, back to its natural folder or into a user
 * folder. Everything is stored in sd:/wiiu/launchiine/folders.txt.
 * Render thread only.
 */
namespace Folders {
    //! pseudo title ids for the folder icons, never used by real titles
    constexpr uint64_t FOLDER_SYSTEM    = 0xFFFF000000000001ULL;
    constexpr uint64_t FOLDER_HOMEBREW  = 0xFFFF000000000002ULL;
    constexpr uint64_t USER_FOLDER_BASE = 0xFFFF000100000000ULL;

    struct UserFolder {
        uint64_t id;
        std::string name;
    };

    inline bool isUserFolder(uint64_t titleId) {
        return (titleId & 0xFFFFFFFF00000000ULL) == USER_FOLDER_BASE;
    }

    inline bool isFolder(uint64_t titleId) {
        return titleId == FOLDER_SYSTEM || titleId == FOLDER_HOMEBREW || isUserFolder(titleId);
    }

    //! the fixed folder a title belongs to by its type, 0 for none
    uint64_t naturalFolder(uint64_t titleId);

    //! the folder a title is shown in, 0 when it is on the main grid
    uint64_t folderOf(uint64_t titleId);

    //! move a title: 0 = main grid, a fixed folder or a user folder
    void moveTo(uint64_t titleId, uint64_t folder);

    //! user folders, in creation order
    std::vector<UserFolder> userFolders();

    //! returns the id of the new folder
    uint64_t createFolder(const std::string &name);

    void renameFolder(uint64_t folder, const std::string &name);

    //! its titles go back to the main grid
    void deleteFolder(uint64_t folder);

    //! delete user folders without titles (titles is every title on the console)
    void deleteEmptyFolders(const std::vector<uint64_t> &titles);

    std::string folderName(uint64_t folder);

    //! icon color (0xRRGGBB) of a folder
    uint32_t folderColor(uint64_t folder);

    struct ColorChoice {
        const char *name;
        uint32_t rgb;
    };
    //! colors offered by "Change color"
    const std::vector<ColorChoice> &colorChoices();

    //! any folder, the built-in ones too
    void setFolderColor(uint64_t folder, uint32_t rgb);

    //! 128x128 32 bit TGA of a folder in the given color (0xRRGGBB)
    std::vector<uint8_t> makeFolderIconTga(uint32_t rgb);
} // namespace Folders
