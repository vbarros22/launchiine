/****************************************************************************
 * Copyright (C) 2015 Dimok
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#include "Application.h"
#include "common/common.h"
#include "gui/GameIcon.h"
#include "game/Folders.h"
#include "game/Updates.h"
#include "utils/logger.h"
#include <algorithm>
#include <coreinit/cache.h>
#include <gui/GuiController.h>
#include <gui/GuiIconGrid.h>
#include <gui/video/CVideo.h>
#include <map>

GuiIconGrid::GuiIconGrid(int32_t w, int32_t h, uint64_t GameIndex, bool sortByName)
    : GuiTitleBrowser(w, h, GameIndex),
      particleBgImage(w, h, 50, 60.0f, 90.0f, 0.6f, 1.0f), folderTint(w, h, (GX2Color){0, 0, 0, 255}), buttonClickSound(Resources::GetSound("button_click.mp3")), gameTitle((char *) nullptr, 52, glm::vec4(1.0f)), updateNotice((char *) nullptr, 26, glm::vec4(1.0f, 0.75f, 0.3f, 1.0f)),
      touchTrigger(GuiTrigger::CHANNEL_1, GuiTrigger::VPAD_TOUCH),
      wpadTouchTrigger(GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5, GuiTrigger::BUTTON_A),
      leftTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_LEFT | GuiTrigger::STICK_L_LEFT, true),
      rightTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_RIGHT | GuiTrigger::STICK_L_RIGHT, true),
      downTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_DOWN | GuiTrigger::STICK_L_DOWN, true), upTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_UP | GuiTrigger::STICK_L_UP, true),
      buttonATrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_A, true), buttonLTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_L, true),
      buttonRTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_R, true),
      buttonBTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_B, true),
      buttonYTrigger(GuiTrigger::CHANNEL_ALL, GuiTrigger::BUTTON_Y, true),
      backButton(w, h), folderMenuButton(w, h), leftButton(w, h), rightButton(w, h), downButton(w, h), upButton(w, h), launchButton(w, h),
      arrowRightImageData(Resources::GetImageData("rightArrow.png")), arrowLeftImageData(Resources::GetImageData("leftArrow.png")), arrowRightImage(arrowRightImageData),
      arrowLeftImage(arrowLeftImageData), arrowRightButton(arrowRightImage.getWidth(), arrowRightImage.getHeight()), arrowLeftButton(arrowLeftImage.getWidth(), arrowLeftImage.getHeight()),
      noIcon(Resources::GetFile("noGameIcon.png"), Resources::GetFileSize("noGameIcon.png"), GX2_TEX_CLAMP_MODE_MIRROR),
      emptyIcon(Resources::GetFile("iconEmpty.png"), Resources::GetFileSize("iconEmpty.png"), GX2_TEX_CLAMP_MODE_MIRROR) {

    particleBgImage.setParent(this);
    folderTint.setParent(this);
    folderTint.setAlpha(0.0f);
    //! GameIndex is the title id to select once the list is loaded
    setSelectedGame(GameIndex);
    listOffset          = 0;
    targetLeftPosition  = 0;
    currentLeftPosition = 0;

    leftButton.setTrigger(&leftTrigger);
    leftButton.clicked.connect(this, &GuiIconGrid::OnLeftClick);
    this->append(&leftButton);

    rightButton.setTrigger(&rightTrigger);
    rightButton.clicked.connect(this, &GuiIconGrid::OnRightClick);
    this->append(&rightButton);

    downButton.setTrigger(&downTrigger);
    downButton.clicked.connect(this, &GuiIconGrid::OnDownClick);
    this->append(&downButton);

    upButton.setTrigger(&upTrigger);
    upButton.clicked.connect(this, &GuiIconGrid::OnUpClick);
    this->append(&upButton);

    launchButton.setTrigger(&buttonATrigger);
    launchButton.setSoundClick(buttonClickSound);
    launchButton.clicked.connect(this, &GuiIconGrid::OnLaunchClick);
    this->append(&launchButton);

    backButton.setTrigger(&buttonBTrigger);
    backButton.clicked.connect(this, &GuiIconGrid::OnBackClick);
    this->append(&backButton);

    folderMenuButton.setTrigger(&buttonYTrigger);
    folderMenuButton.clicked.connect(this, &GuiIconGrid::OnFolderMenuClick);
    this->append(&folderMenuButton);

    arrowLeftButton.setImage(&arrowLeftImage);
    arrowLeftButton.setEffectGrow();
    arrowLeftButton.setPosition(40, 0);
    arrowLeftButton.setAlignment(ALIGN_LEFT | ALIGN_MIDDLE);
    arrowLeftButton.setTrigger(&touchTrigger);
    arrowLeftButton.setTrigger(&wpadTouchTrigger);
    arrowLeftButton.setTrigger(&buttonLTrigger);
    arrowLeftButton.setSoundClick(buttonClickSound);
    arrowLeftButton.clicked.connect(this, &GuiIconGrid::OnLeftArrowClick);

    append(&arrowLeftButton);

    arrowRightButton.setImage(&arrowRightImage);
    arrowRightButton.setEffectGrow();
    arrowRightButton.setPosition(-40, 0);
    arrowRightButton.setAlignment(ALIGN_RIGHT | ALIGN_MIDDLE);
    arrowRightButton.setTrigger(&touchTrigger);
    arrowRightButton.setTrigger(&wpadTouchTrigger);
    arrowRightButton.setTrigger(&buttonRTrigger);
    arrowRightButton.setSoundClick(buttonClickSound);
    arrowRightButton.clicked.connect(this, &GuiIconGrid::OnRightArrowClick);
    append(&arrowRightButton);

    // at most we are rendering 2 screens at the same time
    for (int i = 0; i < MAX_COLS * MAX_ROWS * 2; i++) {
        GameIcon *image = new GameIcon(&emptyIcon);
        emptyIcons.push_back(image);
        GuiButton *button = new GuiButton(emptyIcon.getWidth(), emptyIcon.getHeight());
        button->setImage(image);
        button->setPosition(0, 0);
        //button->setEffectGrow();
        button->setTrigger(&touchTrigger);
        emptyButtons.push_back(button);
    }

    gameTitle.setPosition(0, -320);
    gameTitle.setBlurGlowColor(5.0f, glm::vec4(0.109804, 0.6549, 1.0f, 1.0f));
    setTitleText("");
    append(&gameTitle);

    updateBadgeData = Resources::GetImageData("updateBadge.png");
    updateNotice.setPosition(0, -272);
    updateNotice.setText("");
    append(&updateNotice);
}

GuiIconGrid::~GuiIconGrid() {
    containerMutex.lock();
    for (auto const &x : gameInfoContainers) {
        remove(x.second->button);
        delete x.second;
    }
    gameInfoContainers.clear();
    containerMutex.unlock();

    for (auto const &x : emptyButtons) {
        delete x;
    }

    for (auto const &x : emptyIcons) {
        delete x;
    }

    emptyButtons.clear();
    emptyIcons.clear();

    for (auto &x : folderInfos) {
        delete x.second;
    }
    folderInfos.clear();
    for (auto &x : folderImages) {
        delete x.second;
    }
    folderImages.clear();

    Resources::RemoveImageData(updateBadgeData);
}

void GuiIconGrid::setupFolders() {
    std::vector<Folders::UserFolder> wanted = {{Folders::FOLDER_SYSTEM, Folders::folderName(Folders::FOLDER_SYSTEM)},
                                               {Folders::FOLDER_HOMEBREW, Folders::folderName(Folders::FOLDER_HOMEBREW)}};
    for (auto &folder : Folders::userFolders()) {
        wanted.push_back(folder);
    }

    //! folders that were deleted
    for (auto it = folderInfos.begin(); it != folderInfos.end();) {
        bool keep = false;
        for (auto &folder : wanted) {
            keep |= folder.id == it->first;
        }
        if (keep) {
            ++it;
            continue;
        }
        DEBUG_FUNCTION_LINE("Removing folder icon %016llX", it->first);
        containerMutex.lock();
        auto container = gameInfoContainers.find(it->first);
        if (container != gameInfoContainers.end()) {
            remove(container->second->button);
            delete container->second;
            gameInfoContainers.erase(container);
        }
        containerMutex.unlock();
        positionMutex.lock();
        position.erase(std::remove(position.begin(), position.end(), it->first), position.end());
        positionMutex.unlock();
        if (currentFolder == it->first) {
            currentFolder = 0;
        }
        delete it->second;
        it               = folderInfos.erase(it);
        bResort          = true;
        bUpdatePositions = true;
    }

    for (auto &folder : wanted) {
        uint32_t color       = Folders::folderColor(folder.id);
        GuiImageData *&image = folderImages[color];
        if (image == nullptr) {
            auto tga = Folders::makeFolderIconTga(color);
            image    = new GuiImageData(tga.data(), tga.size(), GX2_TEX_CLAMP_MODE_MIRROR);
        }
        gameInfo *&info = folderInfos[folder.id];
        if (info == nullptr) {
            info          = new gameInfo{};
            info->titleId = folder.id;
            info->appType = (MCPAppType) 0;
        }
        if (info->name != folder.name) {
            info->name = folder.name;
            bResort    = true;
        }
        info->imageData = image;

        containerMutex.lock();
        bool exists = gameInfoContainers.find(folder.id) != gameInfoContainers.end();
        containerMutex.unlock();
        if (!exists) {
            OnGameTitleAdded(info);
        }
    }
}

bool GuiIconGrid::folderHasTitles(uint64_t folder) {
    std::lock_guard<std::recursive_mutex> positionLock(positionMutex);
    for (auto titleId : position) {
        if (!Folders::isFolder(titleId) && Folders::folderOf(titleId) == folder) {
            return true;
        }
    }
    return false;
}

void GuiIconGrid::afterFolderChange(uint64_t preferredSelection) {
    //! a folder that became empty closes
    if (currentFolder != 0 && !folderHasTitles(currentFolder)) {
        preferredSelection = currentFolder;
        currentFolder      = 0;
    }
    displayOrderVersion++;
    bUpdatePositions = true;

    uint64_t selection = preferredSelection;
    if (offsetForTitleId(selection) < 0) {
        selection = firstTitleInLayout();
    }
    setSelectedGame(selection);
    gameSelectionChanged(this, selectedGame);

    int32_t offset = offsetForTitleId(selectedGame);
    curPage        = offset >= 0 ? offset / (MAX_COLS * MAX_ROWS) : 0;
}

void GuiIconGrid::openFolder(uint64_t folder) {
    if (folder == currentFolder) {
        return;
    }
    uint64_t previous = currentFolder;
    currentFolder     = folder;
    //! entering: first title of the folder; leaving: the folder icon
    afterFolderChange(folder != 0 ? 0 : previous);
}

void GuiIconGrid::refreshFolders(const std::vector<uint64_t> &preferred) {
    setupFolders();
    resortPositions();
    bResort = false;

    //! a folder that became empty closes
    if (currentFolder != 0 && !folderHasTitles(currentFolder)) {
        currentFolder = 0;
    }
    uint64_t selection = selectedGame;
    for (auto titleId : preferred) {
        if (offsetForTitleId(titleId) >= 0) {
            selection = titleId;
            break;
        }
    }
    afterFolderChange(selection);
}

uint64_t GuiIconGrid::neighborOf(uint64_t titleId) {
    auto layout = currentLayout();
    for (size_t i = 0; i < layout.size(); i++) {
        if (layout[i] == titleId) {
            return i + 1 < layout.size() ? layout[i + 1] : (i > 0 ? layout[i - 1] : 0);
        }
    }
    return 0;
}

void GuiIconGrid::OnBackClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    if (currentFolder == 0) {
        return;
    }
    openFolder(0);
    folderOpened(this, 0);
}

void GuiIconGrid::OnFolderMenuClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    uint64_t titleId = selectedGame;
    if (titleId == 0) {
        return;
    }
    if (titleId == Folders::FOLDER_SYSTEM || titleId == Folders::FOLDER_HOMEBREW) {
        setTitleText("Built-in folders can't be renamed or deleted");
        return;
    }
    folderMenuRequested(this, titleId);
}

int32_t GuiIconGrid::offsetForTitleId(uint64_t titleId) {
    if (titleId == 0) {
        return -1;
    }
    auto layout = currentLayout();
    for (uint32_t i = 0; i < layout.size(); i++) {
        if (layout[i] == titleId) {
            return i;
        }
    }
    return -1;
}

//! Lower case, strip accents of Latin-1 letters (é -> e, ç -> c), and turn punctuation and
//! whitespace into single spaces. "The Legend of Zelda: Breath" -> " the legend of zelda breath "
static std::string normalizeForSearch(const std::string &in) {
    static const char latin1Fold[64 + 1] =
            "aaaaaaaceeeeiiii"  // C0-CF
            "dnooooo*ouuuuyts"  // D0-DF
            "aaaaaaaceeeeiiii"  // E0-EF
            "dnooooo/ouuuuyty"; // F0-FF
    std::string out = " ";
    for (size_t i = 0; i < in.size(); i++) {
        auto c   = (unsigned char) in[i];
        char add = 0;
        if (c >= 'A' && c <= 'Z') {
            add = (char) (c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            add = (char) c;
        } else if (c == 0xC3 && i + 1 < in.size()) {
            //! UTF-8 for U+00C0..U+00FF
            auto next = (unsigned char) in[i + 1];
            if (next >= 0x80 && next <= 0xBF) {
                add = latin1Fold[(next - 0x80) + 0x00];
                if (add == '*' || add == '/') {
                    add = ' ';
                }
                i++;
            }
        } else if (c >= 0x80) {
            //! other non ASCII (Japanese, symbols like ™): keep as is so it can still match
            out += (char) c;
            continue;
        }
        if (add == 0) {
            add = ' ';
        }
        if (add == ' ' && out.back() == ' ') {
            continue;
        }
        out += add;
    }
    if (out.back() != ' ') {
        out += ' ';
    }
    return out;
}

static std::vector<std::string> splitWords(const std::string &normalized) {
    std::vector<std::string> words;
    std::string current;
    for (char c : normalized) {
        if (c == ' ') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

//! full name for the title bar, line breaks of the two line long name become spaces.
//! Falls back to the short name.
static std::string displayName(const gameInfo *info) {
    std::string result;
    for (char c : info->longName) {
        if (c == '\n' || c == '\r' || c == '\t') {
            c = ' ';
        }
        if (c == ' ' && (result.empty() || result.back() == ' ')) {
            continue;
        }
        result += c;
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result.empty() ? info->name : result;
}

//! Shrinks the font until the whole name fits above the grid, cutting it with "..." only
//! when even the smallest size is too wide. Sizes are even numbers so the font keeps few
//! glyph caches.
void GuiIconGrid::setTitleText(const std::string &text) {
    constexpr int32_t MAX_WIDTH = 1150;
    constexpr int32_t MAX_SIZE  = 52;
    constexpr int32_t MIN_SIZE  = 30;

    gameTitle.setMaxWidth(0, GuiText::DOTTED); // measure without clamping
    gameTitle.setFontSize(MAX_SIZE);
    gameTitle.setText(text.c_str());

    int32_t width = gameTitle.getTextWidth();
    if (width > MAX_WIDTH) {
        int32_t size = (MAX_SIZE * MAX_WIDTH / width) & ~1;
        size         = std::max(size, MIN_SIZE);
        gameTitle.setFontSize(size);
        while (size > MIN_SIZE && gameTitle.getTextWidth() > MAX_WIDTH) {
            size -= 2;
            gameTitle.setFontSize(size);
        }
    }
    gameTitle.setMaxWidth(MAX_WIDTH, GuiText::DOTTED);
}

//! System apps (System Settings, eShop, ...) first, then games, each group sorted by the
//! full name.
static bool isSystemTitle(const gameInfo *info) {
    //! by title id: homebrew without splash screens reports a system app type
    return isSystemTitleId(info->titleId);
}

void GuiIconGrid::resortPositions() {
    struct Entry {
        int group; // 0 folders, 1 system apps, 2 everything else
        std::string key;
        uint64_t titleId;
    };
    std::vector<Entry> entries;
    containerMutex.lock();
    for (auto &x : gameInfoContainers) {
        const gameInfo *info = x.second->info;
        int group            = Folders::isFolder(x.first) ? 0 : (isSystemTitle(info) ? 1 : 2);
        //! folders: System, Homebrew, then the user's folders by name
        std::string key;
        if (x.first == Folders::FOLDER_SYSTEM) {
            key = "\x01";
        } else if (x.first == Folders::FOLDER_HOMEBREW) {
            key = "\x02";
        } else {
            key = (group == 0 ? "\x03" : "") + normalizeForSearch(displayName(info));
        }
        entries.push_back({group, key, x.first});
    }
    containerMutex.unlock();

    std::sort(entries.begin(), entries.end(), [](const Entry &l, const Entry &r) {
        if (l.group != r.group) {
            return l.group < r.group;
        }
        if (l.key != r.key) {
            return l.key < r.key;
        }
        return l.titleId < r.titleId;
    });

    std::vector<uint64_t> newPosition;
    newPosition.reserve(entries.size());
    for (auto &entry : entries) {
        newPosition.push_back(entry.titleId);
    }

    positionMutex.lock();
    bool changed = newPosition != position;
    position.swap(newPosition);
    positionMutex.unlock();

    if (changed) {
        displayOrderVersion++;
        bUpdatePositions = true;
        //! keep the selection on screen when it moved to another page
        setSelectedGame(selectedGame);
    }
}

bool GuiIconGrid::matchesFilter(const gameInfo *info) const {
    if (filterWords.empty()) {
        return true;
    }
    if (info == nullptr) {
        return false;
    }
    //! every typed word has to show up somewhere in the name, in any order
    std::string name = normalizeForSearch(info->name + " " + info->longName);
    for (auto &word : filterWords) {
        if (name.find(word) == std::string::npos) {
            return false;
        }
    }
    return true;
}

std::vector<uint64_t> GuiIconGrid::currentLayout() {
    std::lock_guard<std::recursive_mutex> positionLock(positionMutex);
    std::vector<uint64_t> result;
    if (!filterText.empty()) {
        //! search: every matching title, also those inside folders, without the folder icons
        std::lock_guard<std::recursive_mutex> containerLock(containerMutex);
        for (auto titleId : position) {
            if (titleId == 0 || Folders::isFolder(titleId)) {
                continue;
            }
            auto it = gameInfoContainers.find(titleId);
            if (it != gameInfoContainers.end() && matchesFilter(it->second->info)) {
                result.push_back(titleId);
            }
        }
        return result;
    }
    if (currentFolder != 0) {
        for (auto titleId : position) {
            if (!Folders::isFolder(titleId) && Folders::folderOf(titleId) == currentFolder) {
                result.push_back(titleId);
            }
        }
        return result;
    }
    //! main grid: folders that have something in them, and every title outside of folders
    for (auto titleId : position) {
        if (Folders::isFolder(titleId)) {
            if (folderHasTitles(titleId)) {
                result.push_back(titleId);
            }
        } else if (Folders::folderOf(titleId) == 0) {
            result.push_back(titleId);
        }
    }
    return result;
}

uint64_t GuiIconGrid::firstTitleInLayout() {
    for (auto titleId : currentLayout()) {
        if (titleId != 0) {
            return titleId;
        }
    }
    return 0;
}

void GuiIconGrid::setFilter(const std::string &filter) {
    std::string newFilter = normalizeForSearch(filter);
    if (newFilter == " ") {
        newFilter.clear();
    }
    if (newFilter == filterText) {
        return;
    }
    filterText  = newFilter;
    filterWords = splitWords(newFilter);
    displayOrderVersion++;
    DEBUG_FUNCTION_LINE_VERBOSE("Filter words: %d", (int) filterWords.size());

    curPage          = 0;
    bUpdatePositions = true;

    uint64_t first = firstTitleInLayout();
    setSelectedGame(first);
    if (first == 0 && !filterText.empty()) {
        std::string text = "No matches for \"" + filter + "\"";
        setTitleText(text);
    }
}

void GuiIconGrid::setSelectedGame(uint64_t idx) {
    this->selectedGame = idx;
    if (idx == 0) {
        setTitleText("");
    }

    containerMutex.lock();
    GameInfoContainer *container = nullptr;
    for (auto const &x : gameInfoContainers) {
        container = x.second;
        if (x.first == idx) {
            container->image->setSelected(true);
            setTitleText(displayName(container->info));
        } else {
            container->image->setSelected(false);
        }
    }
    containerMutex.unlock();

    int32_t offset = offsetForTitleId(getSelectedGame());
    if (offset >= 0) {
        uint32_t newPage = offset / (MAX_COLS * MAX_ROWS);
        if (newPage != (uint32_t) curPage) {
            curPage          = newPage;
            bUpdatePositions = true;
        }
    }
}

uint64_t GuiIconGrid::getSelectedGame(void) {
    return selectedGame;
}

void GuiIconGrid::OnGameTitleListUpdated(GameList *gameList) {
    gameList->lock();
    containerMutex.lock();
    positionMutex.lock();
    // At first delete the ones that were deleted;
    auto it = gameInfoContainers.begin();
    while (it != gameInfoContainers.end()) {
        bool wasFound = Folders::isFolder(it->first);
        for (int32_t i = 0; i < gameList->size() && !wasFound; i++) {
            gameInfo *info = gameList->at(i);
            if (info != nullptr && info->titleId == it->first) {
                wasFound = true;
                break;
            }
        }

        if (!wasFound) {
            DEBUG_FUNCTION_LINE("Removing %016llX", it->first);
            remove(it->second->button);
            delete it->second;
            it = gameInfoContainers.erase(it);
        } else {
            ++it;
        }
    }

    for (int32_t i = 0; i < gameList->size(); i++) {
        gameInfo *info               = gameList->at(i);
        GameInfoContainer *container = nullptr;

        for (auto const &x : gameInfoContainers) {
            if (info->titleId == x.first) {
                container = x.second;
                break;
            }
        }
        if (container == nullptr) {
            OnGameTitleAdded(info);
        }
    }
    positionMutex.unlock();
    containerMutex.unlock();
    gameList->unlock();

    setupFolders();

    bResort = false;
    resortPositions();

    //! the menu opens on the last launched title, open its folder if it is in one
    currentFolder = Folders::folderOf(selectedGame);
    displayOrderVersion++;

    uint64_t newSelection = selectedGame;
    containerMutex.lock();
    bool selectionExists = gameInfoContainers.find(selectedGame) != gameInfoContainers.end();
    containerMutex.unlock();
    if (!selectionExists || offsetForTitleId(selectedGame) < 0) {
        newSelection = firstTitleInLayout();
    }
    setSelectedGame(newSelection);
    gameSelectionChanged(this, selectedGame);

    //! show the page of the selected title right away, without scrolling there
    int32_t offset      = offsetForTitleId(selectedGame);
    curPage             = offset >= 0 ? offset / (MAX_COLS * MAX_ROWS) : 0;
    targetLeftPosition  = -curPage * getWidth();
    currentLeftPosition = targetLeftPosition;
    bUpdatePositions    = true;
}

void GuiIconGrid::changePage(int32_t delta) {
    const int32_t perPage = MAX_COLS * MAX_ROWS;
    auto layout           = currentLayout();

    //! last page that holds at least one title
    int32_t lastPage = 0;
    for (int32_t i = (int32_t) layout.size() - 1; i >= 0; i--) {
        if (layout[i] != 0) {
            lastPage = i / perPage;
            break;
        }
    }

    int32_t newPage = curPage + delta;
    if (newPage < 0 || newPage > lastPage) {
        return;
    }

    //! keep the selection in the same slot (row/column) on the new page. If that slot is
    //! empty, take the closest title before it, then after it.
    int32_t offset = offsetForTitleId(selectedGame);
    int32_t slot   = offset >= 0 ? offset % perPage : 0;
    int32_t start  = newPage * perPage;
    int32_t end    = std::min<int32_t>(start + perPage, layout.size());

    uint64_t newSelection = 0;
    for (int32_t i = std::min(start + slot, end - 1); i >= start && newSelection == 0; i--) {
        newSelection = layout[i];
    }
    for (int32_t i = start + slot + 1; i < end && newSelection == 0; i++) {
        newSelection = layout[i];
    }

    curPage          = newPage;
    bUpdatePositions = true;
    if (newSelection != 0) {
        setSelectedGame(newSelection);
        gameSelectionChanged(this, selectedGame);
    }
}

void GuiIconGrid::OnLeftArrowClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    changePage(-1);
}

void GuiIconGrid::OnRightArrowClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    changePage(1);
}

void GuiIconGrid::OnLeftClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    auto layout    = currentLayout();
    int32_t offset = offsetForTitleId(getSelectedGame());
    if (offset < 0) {
        return;
    }
    if ((offset % MAX_COLS) == 0) {
        offset -= ((MAX_COLS * MAX_ROWS) - MAX_COLS) + 1;
    } else {
        offset--;
    }
    if (offset < 0 || layout.empty()) {
        return;
    }
    uint64_t newTitleId = layout.at(offset);
    if (newTitleId > 0) {
        setSelectedGame(newTitleId);
        gameSelectionChanged(this, selectedGame);
    }
}

void GuiIconGrid::OnRightClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    auto layout    = currentLayout();
    int32_t offset = offsetForTitleId(getSelectedGame());
    if (offset < 0) {
        return;
    }
    if ((offset % MAX_COLS) == MAX_COLS - 1) {
        offset += ((MAX_COLS * MAX_ROWS) - MAX_COLS) + 1;
    } else {
        offset++;
    }
    if ((uint32_t) offset >= layout.size()) {
        return;
    }
    uint64_t newTitleId = layout.at(offset);
    if (newTitleId > 0) {
        setSelectedGame(newTitleId);
        gameSelectionChanged(this, selectedGame);
    }
}

void GuiIconGrid::OnDownClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    auto layout    = currentLayout();
    int32_t offset = offsetForTitleId(getSelectedGame());
    if (offset < 0) {
        return;
    }
    if (offset % (MAX_COLS * MAX_ROWS) < (MAX_COLS * MAX_ROWS) - MAX_COLS) {
        offset = offset + MAX_COLS;
    } else {
        return;
    }

    if ((uint32_t) offset >= layout.size()) {
        return;
    }
    uint64_t newTitleId = layout.at(offset);
    if (newTitleId > 0) {
        setSelectedGame(newTitleId);
        gameSelectionChanged(this, selectedGame);
    }
}

void GuiIconGrid::OnUpClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    auto layout    = currentLayout();
    int32_t offset = offsetForTitleId(getSelectedGame());
    if (offset < 0) {
        return;
    }
    if (offset % (MAX_COLS * MAX_ROWS) >= MAX_COLS) {
        offset = offset - MAX_COLS;
    } else {
        return;
    }

    if (offset < 0) {
        return;
    }
    uint64_t newTitleId = layout.at(offset);
    if (newTitleId > 0) {
        setSelectedGame(newTitleId);
        gameSelectionChanged(this, selectedGame);
    }
}

void GuiIconGrid::OnLaunchClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    //! do not auto launch when wiimote is pointing to screen and presses A
    if ((trigger == &buttonATrigger) && (controller->chan & (GuiTrigger::CHANNEL_2 | GuiTrigger::CHANNEL_3 | GuiTrigger::CHANNEL_4 | GuiTrigger::CHANNEL_5)) && controller->data.validPointer) {
        return;
    }
    uint64_t titleId = getSelectedGame();
    if (Folders::isFolder(titleId)) {
        openFolder(titleId);
        folderOpened(this, titleId);
        return;
    }
    containerMutex.lock();
    auto it          = gameInfoContainers.find(titleId);
    bool valid       = it != gameInfoContainers.end() && it->second->info != nullptr;
    containerMutex.unlock();
    if (!valid) {
        DEBUG_FUNCTION_LINE("Nothing selected (%016llX), ignoring launch", titleId);
        return;
    }
    DEBUG_FUNCTION_LINE("Tried to launch %s (%016llX)", it->second->info->name.c_str(), titleId);
    gameLaunchClicked(this, titleId);
}


void GuiIconGrid::OnGameButtonPointedOn(GuiButton *button, const GuiController *controller) {
}

void GuiIconGrid::OnGameButtonPointedOff(GuiButton *button, const GuiController *controller) {
}

void GuiIconGrid::OnGameButtonClick(GuiButton *button, const GuiController *controller, GuiTrigger *trigger) {
    containerMutex.lock();
    for (auto const &x : gameInfoContainers) {
        if (x.second->button == button) {
            if (selectedGame == (x.second->info->titleId)) {
                if (gameLaunchTimer < 30)
                    OnLaunchClick(button, controller, trigger);
            } else {
                setSelectedGame(x.second->info->titleId);
                gameSelectionChanged(this, selectedGame);
            }
            gameLaunchTimer = 0;
            break;
        }
    }
    containerMutex.unlock();
}

void GuiIconGrid::OnGameTitleAdded(gameInfo *info) {
    DEBUG_FUNCTION_LINE_VERBOSE("Adding %016llX", info->titleId);
    GuiImageData *imageData = &noIcon;
    if (info->imageData != nullptr) {
        imageData = info->imageData;
    }
    GameIcon *image = new GameIcon(imageData);
    image->setRenderReflection(false);
    image->setStrokeRender(false);
    image->setSelected(info->titleId == selectedGame);
    image->setRenderIconLast(true);

    GuiButton *button = new GuiButton(noIcon.getWidth(), noIcon.getHeight());
    button->setImage(image);
    button->setPosition(0, 0);
    button->setEffectGrow();
    button->setTrigger(&touchTrigger);
    button->setTrigger(&wpadTouchTrigger);
    button->setSoundClick(buttonClickSound);
    //button->setClickable( (idx < gameList->size()) );
    //button->setSelectable( (idx < gameList->size()) );
    button->clicked.connect(this, &GuiIconGrid::OnGameButtonClick);
    button->pointedOn.connect(this, &GuiIconGrid::OnGameButtonPointedOn);
    button->pointedOff.connect(this, &GuiIconGrid::OnGameButtonPointedOff);

    GameInfoContainer *container = new GameInfoContainer(button, image, info);
    setUpdateBadge(container, Updates::hasUpdate(info->titleId));
    containerMutex.lock();
    gameInfoContainers[info->titleId] = container;
    containerMutex.unlock();
    this->append(button);

    positionMutex.lock();
    position.push_back(info->titleId);
    positionMutex.unlock();

    bResort          = true;
    bUpdatePositions = true;
}

void GuiIconGrid::setUpdateBadge(GameInfoContainer *container, bool show) {
    if (container == nullptr || container->button == nullptr || updateBadgeData == nullptr) {
        return;
    }
    if (show && container->badge == nullptr) {
        container->badge = new GuiImage(updateBadgeData);
        container->badge->setAlignment(ALIGN_RIGHT | ALIGN_TOP);
        container->badge->setPosition(10, 10);
        container->button->setIcon(container->badge);
    } else if (!show && container->badge != nullptr) {
        container->button->setIcon(nullptr);
        AsyncExecutor::pushForDelete(container->badge);
        container->badge = nullptr;
    }
}

void GuiIconGrid::OnGameTitleUpdated(gameInfo *info) {
    DEBUG_FUNCTION_LINE_VERBOSE("Updating infos of %016llX", info->titleId);
    GameInfoContainer *container = nullptr;
    containerMutex.lock();
    for (auto const &x : gameInfoContainers) {
        if (info->titleId == x.first) {
            container = x.second;
            break;
        }
    }

    // keep the lock to delay the draw() until the image data is ready.
    if (container != nullptr) {
        container->updateImageData();
        if (info->titleId == selectedGame) {
            setTitleText(displayName(info));
        }
    }

    containerMutex.unlock();

    bResort          = true;
    bUpdatePositions = true;
}

void GuiIconGrid::process() {
    if (bResort) {
        bResort = false;
        resortPositions();
    }

    if (currentLeftPosition < targetLeftPosition) {
        currentLeftPosition += 35;

        if (currentLeftPosition > targetLeftPosition)
            currentLeftPosition = targetLeftPosition;

        bUpdatePositions = true;
    } else if (currentLeftPosition > targetLeftPosition) {
        currentLeftPosition -= 35;

        if (currentLeftPosition < targetLeftPosition)
            currentLeftPosition = targetLeftPosition;

        bUpdatePositions = true;
    }

    if (bUpdatePositions) {
        bUpdatePositions = false;
        updateButtonPositions();
    }
    gameLaunchTimer++;

    //! fade the folder color in when a folder opens and out when it closes
    if (currentFolder != 0 && currentFolder != folderTintFor) {
        uint32_t rgb = Folders::folderColor(currentFolder);
        folderTint.setImageColor((GX2Color){(uint8_t) (rgb >> 16), (uint8_t) (rgb >> 8), (uint8_t) rgb, 255});
        folderTintFor = currentFolder;
    }
    float tintTarget = currentFolder != 0 ? 0.35f : 0.0f;
    if (folderTintAlpha < tintTarget) {
        folderTintAlpha = std::min(tintTarget, folderTintAlpha + 0.035f);
    } else if (folderTintAlpha > tintTarget) {
        folderTintAlpha = std::max(tintTarget, folderTintAlpha - 0.035f);
    }
    folderTint.setAlpha(folderTintAlpha);

    //! game update notice: badges when the result of the check changes, text for the selection
    uint32_t updates = Updates::generation();
    if (updates != updatesSeen) {
        updatesSeen = updates;
        containerMutex.lock();
        for (auto const &x : gameInfoContainers) {
            setUpdateBadge(x.second, Updates::hasUpdate(x.first));
        }
        containerMutex.unlock();
    }
    bool showNotice = Updates::hasUpdate(selectedGame);
    if (showNotice != noticeShown) {
        noticeShown = showNotice;
        updateNotice.setText(showNotice ? "Update available" : "");
    }

    GuiFrame::process();
}

void GuiIconGrid::update(GuiController *c) {
    GuiFrame::update(c);
}

void GuiIconGrid::updateButtonPositions() {
    positionMutex.lock();
    arrowRightButton.setState(GuiElement::STATE_DISABLED);
    arrowRightButton.setVisible(false);
    arrowLeftButton.setState(GuiElement::STATE_DISABLED);
    arrowLeftButton.setVisible(false);

    int32_t col = 0, row = 0, listOff = 0;

    // create an empty vector of pairs
    std::vector<std::pair<uint64_t, GameInfoContainer *>> vec;

    containerMutex.lock();

    // copy key-value pairs from the map to the vector
    std::copy(gameInfoContainers.begin(), gameInfoContainers.end(), std::back_inserter<std::vector<std::pair<uint64_t, GameInfoContainer *>>>(vec));

    containerMutex.unlock();

    for (auto const &x : vec) {
        remove(x.second->button);
    }

    for (auto const &x : emptyButtons) {
        remove(x);
    }

    //! always the same list the navigation uses: search results, folder content or the
    //! main grid (which hides what is inside folders)
    const std::vector<uint64_t> layout = currentLayout();

    uint32_t elementSize = layout.size();
    uint32_t pages       = (elementSize / (MAX_COLS * MAX_ROWS)) + 1;
    if (elementSize % (MAX_COLS * MAX_ROWS) == 0 && pages > 1) {
        pages--;
    }

    uint32_t emptyIconUse = 0;

    if (curPage < 0) {
        curPage = 0;
    }
    if ((uint32_t) curPage >= pages) {
        curPage = pages - 1;
    }

    targetLeftPosition = -curPage * getWidth();

    if ((uint32_t) curPage < (pages - 1)) {
        arrowRightButton.clearState(GuiElement::STATE_DISABLED);
        arrowRightButton.setVisible(true);
        bringToFront(&arrowRightButton);
    }
    if (curPage > 0) {
        arrowLeftButton.clearState(GuiElement::STATE_DISABLED);
        arrowLeftButton.setVisible(true);
        bringToFront(&arrowLeftButton);
    }

    uint32_t startPage = -(currentLeftPosition / getWidth());
    uint32_t endPage   = startPage;

    if (targetLeftPosition != currentLeftPosition) {
        endPage++;
        if (endPage > pages) {
            endPage = pages;
        }
    }
    uint32_t startValue = startPage * (MAX_COLS * MAX_ROWS);

    positionButtons.clear();
    for (uint32_t i = 0; i < startValue; i++) {
        positionButtons.push_back(nullptr);
    }

    for (uint32_t i = startPage * (MAX_COLS * MAX_ROWS); i < (endPage + 1) * (MAX_COLS * MAX_ROWS); i++) {
        listOff            = i / (MAX_COLS * MAX_ROWS);
        GuiButton *element = nullptr;
        float posX         = currentLeftPosition + listOff * width + (col * (noIcon.getWidth() + noIcon.getWidth() * 0.5f) - (MAX_COLS * 0.5f - 0.5f) * (noIcon.getWidth() + noIcon.getWidth() * 0.5f));
        float posY         = -row * (noIcon.getHeight() + noIcon.getHeight() * 0.5f) + (MAX_ROWS * 0.5f - 0.5f) * (noIcon.getHeight() + noIcon.getHeight() * 0.5f) + 30.0f;

        if (i < layout.size()) {
            uint64_t titleID = layout.at(i);
            if (titleID > 0) {
                GameInfoContainer *container = nullptr;
                containerMutex.lock();
                if (gameInfoContainers.find(titleID) != gameInfoContainers.end()) {
                    container = gameInfoContainers[titleID];
                }
                containerMutex.unlock();
                if (container != nullptr) {
                    element = container->button;
                }
            }
        }

        //! Empty slots mark free space on the main grid like in the Wii U Menu; in search
        //! results and folders they only bury the few real entries, leave the slot blank.
        bool showEmptySlot = filterText.empty() && currentFolder == 0;
        if (element == nullptr && showEmptySlot) {
            if (emptyButtons.size() <= emptyIconUse) {
                break;
            }
            element = emptyButtons.at(emptyIconUse);
            emptyIconUse++;
        }
        positionButtons.push_back(element);
        if (element == nullptr) {
            col++;
            if (col >= MAX_COLS) {
                col = 0;
                row++;
            }
            if (row >= MAX_ROWS) {
                row = 0;
            }
            continue;
        }
        element->setPosition(posX, posY);
        append(element);

        col++;
        if (col >= MAX_COLS) {
            col = 0;
            row++;
        }
        if (row >= MAX_ROWS) {
            row = 0;
        }
    }

    //! The arrows have to be drawn after the icons: they write depth for their whole
    //! (partly transparent) rectangle, so icons drawn later and scrolling under them were
    //! cut out, which showed up as a dark band behind the arrows while changing pages.
    if (arrowRightButton.isVisible()) {
        bringToFront(&arrowRightButton);
    }
    if (arrowLeftButton.isVisible()) {
        bringToFront(&arrowLeftButton);
    }
    positionMutex.unlock();
}

void GuiIconGrid::draw(CVideo *pVideo) {
    //! the BG needs to be rendered to stencil
    pVideo->setStencilRender(true);
    particleBgImage.draw(pVideo);
    //! drawn with the BG, where depth writes are off; with depth on this full screen
    //! quad would hide every icon behind it
    if (folderTintAlpha > 0.0f) {
        folderTint.draw(pVideo);
    }
    pVideo->setStencilRender(false);

    containerMutex.lock();
    positionMutex.lock();
    GuiFrame::draw(pVideo);
    positionMutex.unlock();
    containerMutex.unlock();
}
