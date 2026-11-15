#pragma once

#include <gui/GuiFrame.h>
#include <gui/GuiImage.h>
#include <gui/GuiImageData.h>
#include <gui/GuiText.h>
#include "gui/GameIcon.h"
#include "gui/RawImage.h"
#include <string>
#include <utility>
#include <vector>

/**
 * TV view: the selected game's cover with its name, publisher, year, genre, players and the
 * official description from GameTDB. Titles without GameTDB data (homebrew, system apps,
 * folders) show their icon and name.
 */
class TvInfoFrame : public GuiFrame {
public:
    TvInfoFrame(int32_t w, int32_t h);
    ~TvInfoFrame() override;

    //! render thread
    void showTitle(uint64_t titleId, const std::string &name, const std::string &gameDbId, GuiImageData *icon);

    void process() override;
    void draw(CVideo *pVideo) override;

private:
    void refreshText();
    void setCover(RawImage *image);
    bool tryCover();

    GuiImageData *backgroundData = nullptr;
    GuiImage *background         = nullptr;

    RawImage *coverImage    = nullptr;
    GameIcon *iconImage     = nullptr; // when there is no cover: the icon, rounded like on the grid

    GuiText titleText;
    GuiText metaText;
    GuiText synopsisText;
    GuiText creditText;

    uint64_t titleId = 0;
    std::string name;
    std::string gameDbId;
    GuiImageData *icon       = nullptr;
    uint32_t seenGeneration  = 0;
    int32_t framesSinceShown = 0;
    void showIcon(bool show);
    bool coverLoaded         = false;
    bool hasInfo             = false;
};
