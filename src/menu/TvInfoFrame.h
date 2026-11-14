#pragma once

#include <gui/GuiFrame.h>
#include <gui/GuiImage.h>
#include <gui/GuiImageData.h>
#include <gui/GuiText.h>
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
    void setCover(GuiImageData *data);
    void releaseOld(bool all);

    GuiImageData *backgroundData = nullptr;
    GuiImage *background         = nullptr;
    GuiImageData *tileData       = nullptr;
    GuiImage *tile               = nullptr;

    GuiImageData *coverData = nullptr;
    GuiImage *coverImage    = nullptr;
    GuiImage *iconImage     = nullptr;
    //! textures the GPU may still read from the last frames, freed a bit later
    std::vector<std::pair<GuiImageData *, int32_t>> oldData;
    std::vector<GuiImage *> oldImages;

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
    bool coverLoaded         = false;
    bool hasInfo             = false;
};
