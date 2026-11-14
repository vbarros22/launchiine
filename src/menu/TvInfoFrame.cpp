#include "TvInfoFrame.h"
#include "fs/FSUtils.h"
#include "game/GameDb.h"
#include "resources/Resources.h"
#include "utils/AsyncExecutor.h"
#include "utils/logger.h"
#include <algorithm>
#include <malloc.h>

namespace {
    //! layout units (1280x720, origin in the middle)
    constexpr float COVER_X     = -330.0f;
    constexpr float COVER_MAX_W = 430.0f;
    constexpr float COVER_MAX_H = 600.0f;
    constexpr float TEXT_LEFT   = -70.0f; // left edge of the text column
    constexpr int32_t TEXT_W    = 650;
    constexpr float TEXT_TOP    = 110.0f; // from the top of the screen
    //! wait until the selection rests before decoding a cover (a large JPEG takes a moment)
    constexpr int32_t COVER_DELAY_FRAMES = 12;

    std::string singleLine(const std::string &in) {
        std::string out = in;
        std::replace(out.begin(), out.end(), '\n', ' ');
        return out;
    }
} // namespace

TvInfoFrame::TvInfoFrame(int32_t w, int32_t h)
    : GuiFrame(w, h),
      titleText((char *) nullptr, 46, glm::vec4(0.2f, 0.22f, 0.24f, 1.0f)),
      metaText((char *) nullptr, 26, glm::vec4(0.42f, 0.45f, 0.48f, 1.0f)),
      synopsisText((char *) nullptr, 25, glm::vec4(0.25f, 0.27f, 0.29f, 1.0f)),
      creditText("Info & covers: GameTDB.com", 18, glm::vec4(0.5f, 0.53f, 0.56f, 1.0f)) {
    backgroundData = Resources::GetImageData("background.png");
    background     = new GuiImage(backgroundData);
    background->setScaleX((float) w / background->getWidth());
    background->setScaleY((float) h / background->getHeight());
    append(background);

    tileData = Resources::GetImageData("tile.png");
    tile     = new GuiImage(tileData);
    tile->setPosition(COVER_X, 0);
    tile->setVisible(false);
    append(tile);

    float textX = TEXT_LEFT + w * 0.5f; // offset from the left edge
    for (auto *text : {&titleText, &metaText, &synopsisText}) {
        text->setAlignment(ALIGN_LEFT | ALIGN_TOP);
        text->setMaxWidth(TEXT_W, GuiText::WRAP);
        text->setPosition(textX, -TEXT_TOP);
        append(text);
    }
    titleText.setLinesToDraw(2);
    metaText.setLinesToDraw(2);
    synopsisText.setLinesToDraw(12);

    creditText.setAlignment(ALIGN_RIGHT | ALIGN_BOTTOM);
    creditText.setPosition(-24, 16);
    creditText.setVisible(false);
    append(&creditText);
}

TvInfoFrame::~TvInfoFrame() {
    remove(background);
    remove(tile);
    delete background;
    delete tile;
    Resources::RemoveImageData(backgroundData);
    Resources::RemoveImageData(tileData);
    if (coverImage != nullptr) {
        remove(coverImage);
        delete coverImage;
    }
    if (iconImage != nullptr) {
        remove(iconImage);
        delete iconImage;
    }
    delete coverData;
    releaseOld(true);
}

void TvInfoFrame::releaseOld(bool all) {
    for (auto it = oldData.begin(); it != oldData.end();) {
        if (all || --it->second <= 0) {
            delete it->first;
            it = oldData.erase(it);
        } else {
            ++it;
        }
    }
}

void TvInfoFrame::setCover(GuiImageData *data) {
    if (coverImage != nullptr) {
        remove(coverImage);
        AsyncExecutor::pushForDelete(coverImage);
        coverImage = nullptr;
    }
    if (coverData != nullptr) {
        oldData.emplace_back(coverData, 5); // the GPU may still use it for a few frames
        coverData = nullptr;
    }
    if (data == nullptr || data->getTexture() == nullptr) {
        delete data;
        return;
    }
    coverData  = data;
    coverImage = new GuiImage(coverData);
    float scale = std::min(COVER_MAX_W / coverImage->getWidth(), COVER_MAX_H / coverImage->getHeight());
    coverImage->setScale(scale);
    coverImage->setPosition(COVER_X, 0);
    append(coverImage);
}

void TvInfoFrame::showTitle(uint64_t id, const std::string &titleName, const std::string &dbId, GuiImageData *titleIcon) {
    if (id == titleId && dbId == gameDbId && titleIcon == icon && titleName == name) {
        return;
    }
    titleId          = id;
    name             = titleName;
    gameDbId         = dbId;
    icon             = titleIcon;
    framesSinceShown = 0;
    coverLoaded      = false;
    setCover(nullptr);

    //! until a cover is there: the icon on a tile
    if (iconImage != nullptr) {
        remove(iconImage);
        AsyncExecutor::pushForDelete(iconImage);
        iconImage = nullptr;
    }
    if (icon != nullptr) {
        iconImage = new GuiImage(icon);
        iconImage->setScale(300.0f / std::max(1.0f, (float) iconImage->getWidth()));
        iconImage->setPosition(COVER_X, 0);
        append(iconImage);
    }
    tile->setScale(300.0f / 246.0f); // the tile texture fits an icon of ~246 px
    tile->setVisible(icon != nullptr);
    refreshText();
}

void TvInfoFrame::refreshText() {
    GameDb::Info info;
    hasInfo = !gameDbId.empty() && GameDb::get(gameDbId, info);

    std::string title = hasInfo && !info.title.empty() ? info.title : name;
    titleText.setText(title.c_str());

    std::string meta;
    if (hasInfo) {
        auto add = [&meta](const std::string &part) {
            if (!part.empty()) {
                meta += (meta.empty() ? "" : "  ·  ") + part;
            }
        };
        add(info.publisher.empty() ? info.developer : info.publisher);
        add(info.year);
        add(info.genre);
        if (!info.players.empty() && info.players != "0") {
            add(info.players == "1" ? std::string("1 player") : "1-" + info.players + " players");
        }
    }
    metaText.setText(meta.c_str());
    synopsisText.setText(hasInfo ? singleLine(info.synopsis).c_str() : "");

    //! stack the texts under each other
    float y = -TEXT_TOP;
    titleText.setPosition(titleText.getOffsetX(), y);
    y -= titleText.getTextHeight() + 14.0f;
    metaText.setPosition(metaText.getOffsetX(), y);
    if (!meta.empty()) {
        y -= metaText.getTextHeight() + 26.0f;
    }
    synopsisText.setPosition(synopsisText.getOffsetX(), y);
    creditText.setVisible(hasInfo || coverLoaded);
}

void TvInfoFrame::process() {
    releaseOld(false);

    uint32_t generation = GameDb::generation();
    if (generation != seenGeneration) {
        seenGeneration = generation;
        refreshText();
        if (!coverLoaded) {
            framesSinceShown = std::min(framesSinceShown, COVER_DELAY_FRAMES - 1); // try the cover again
        }
    }

    if (framesSinceShown < COVER_DELAY_FRAMES) {
        framesSinceShown++;
        if (framesSinceShown == COVER_DELAY_FRAMES && !coverLoaded && !gameDbId.empty()) {
            std::string path = GameDb::coverPath(gameDbId);
            if (!path.empty()) {
                uint8_t *buffer = nullptr;
                uint32_t size   = 0;
                if (FSUtils::LoadFileToMem(path.c_str(), &buffer, &size) > 0 && buffer != nullptr) {
                    setCover(new GuiImageData(buffer, size));
                    free(buffer);
                    coverLoaded = coverImage != nullptr;
                    if (coverLoaded) {
                        if (iconImage != nullptr) {
                            iconImage->setVisible(false);
                        }
                        tile->setVisible(false);
                        creditText.setVisible(true);
                    }
                }
            }
        }
    }
    GuiFrame::process();
}

void TvInfoFrame::draw(CVideo *pVideo) {
    if (!isVisible()) {
        return;
    }
    GuiFrame::draw(pVideo);
}
