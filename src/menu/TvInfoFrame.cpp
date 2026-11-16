#include "TvInfoFrame.h"
#include "game/GameDb.h"
#include "resources/Resources.h"
#include "utils/AsyncExecutor.h"
#include "utils/logger.h"
#include <algorithm>
#include <gui/video/CVideo.h>
#include <gui/video/shaders/Texture2DShader.h>
#include <gx2/clear.h>
#include <gx2/draw.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <malloc.h>

namespace {
    //! layout units (1280x720, origin in the middle)
    constexpr float COVER_X     = -330.0f;
    constexpr float COVER_MAX_W = 430.0f;
    constexpr float COVER_MAX_H = 600.0f;
    constexpr float TEXT_LEFT   = -70.0f; // left edge of the text column
    constexpr int32_t TEXT_W    = 650;
    constexpr float TEXT_TOP    = 110.0f; // from the top of the screen
    //! a cover usually arrives within a few frames; show the icon when it takes longer
    constexpr int32_t ICON_AFTER_FRAMES = 45;
    //! redraw the texts once the selection rests (not on every step while paging)
    constexpr int32_t TEXT_AFTER_FRAMES = 6;

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
    background->setParent(this);

    float textX = TEXT_LEFT + w * 0.5f; // offset from the left edge
    for (auto *text : {&titleText, &metaText, &synopsisText}) {
        text->setAlignment(ALIGN_LEFT | ALIGN_TOP);
        text->setMaxWidth(TEXT_W, GuiText::WRAP);
        text->setPosition(textX, -TEXT_TOP);
        text->setParent(this);
    }
    titleText.setLinesToDraw(2);
    metaText.setLinesToDraw(2);
    synopsisText.setLinesToDraw(12);

    creditText.setAlignment(ALIGN_RIGHT | ALIGN_BOTTOM);
    creditText.setPosition(-24, 16);
    creditText.setVisible(false);
    creditText.setParent(this);
}

TvInfoFrame::~TvInfoFrame() {
    delete background;
    free(layerBuffer.surface.image);
    free(layerContext);
    Resources::RemoveImageData(backgroundData);
    if (coverImage != nullptr) {
        remove(coverImage);
        AsyncExecutor::pushForDelete(coverImage);
    }
    if (iconImage != nullptr) {
        remove(iconImage);
        delete iconImage;
    }
}

void TvInfoFrame::setCover(RawImage *image) {
    if (coverImage != nullptr) {
        remove(coverImage);
        AsyncExecutor::pushForDelete(coverImage); // deleted once the GPU is done with it
        coverImage = nullptr;
    }
    if (image == nullptr) {
        return;
    }
    if (!image->isValid()) {
        delete image;
        return;
    }
    coverImage  = image;
    float scale = std::min(COVER_MAX_W / coverImage->getWidth(), COVER_MAX_H / coverImage->getHeight());
    coverImage->setScale(scale);
    coverImage->setPosition(COVER_X, 0);
    append(coverImage);
}

//! true when the cover is known (shown, or the game has none)
bool TvInfoFrame::tryCover() {
    std::shared_ptr<const GameDb::Cover> cover;
    auto state = GameDb::getCover(gameDbId, cover);
    if (state == GameDb::CoverState::Ready) {
        //! raw pixels from the worker: a row copy, nothing to decode here
        setCover(new RawImage(cover->width, cover->height, cover->pixels.data()));
        coverLoaded = coverImage != nullptr;
        showIcon(!coverLoaded);
        if (creditText.isVisible() != (hasInfo || coverLoaded)) {
            creditText.setVisible(hasInfo || coverLoaded);
            layerDirty = true;
        }
        return true;
    }
    if (state == GameDb::CoverState::None) {
        showIcon(true);
        return true;
    }
    return false;
}

void TvInfoFrame::showIcon(bool show) {
    if (iconImage != nullptr) {
        iconImage->setVisible(show);
    }
}

void TvInfoFrame::showTitle(uint64_t id, const std::string &titleName, const std::string &dbId, GuiImageData *titleIcon) {
    if (id == titleId && dbId == gameDbId && titleIcon == icon && titleName == name) {
        return;
    }
    bool sameTitle   = id == titleId && dbId == gameDbId;
    titleId          = id;
    name             = titleName;
    gameDbId         = dbId;
    icon             = titleIcon;
    if (!sameTitle) {
        framesSinceShown = 0;
        coverLoaded      = false;
        setCover(nullptr);
    }

    if (iconImage != nullptr) {
        remove(iconImage);
        AsyncExecutor::pushForDelete(iconImage);
        iconImage = nullptr;
    }
    if (icon != nullptr) {
        iconImage = new GameIcon(icon);
        iconImage->setScale(300.0f / std::max(1.0f, (float) iconImage->getWidth()));
        iconImage->setPosition(COVER_X, 0);
        append(iconImage);
        //! games: wait a moment for the cover instead of flashing the icon
        showIcon(!coverLoaded && (gameDbId.empty() || framesSinceShown >= ICON_AFTER_FRAMES));
    }
    refreshText();
    if (!sameTitle && !gameDbId.empty()) {
        tryCover(); // usually already in memory: shown in this very frame
    }
}

void TvInfoFrame::refreshText() {
    GameDb::Info info;
    hasInfo = !gameDbId.empty() && GameDb::get(gameDbId, info);

    std::string title = hasInfo && !info.title.empty() ? info.title : name;

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
    std::string synopsis = hasInfo ? singleLine(info.synopsis) : "";
    bool credit          = hasInfo || coverLoaded;
    if (title == shownTitle && meta == shownMeta && synopsis == shownSynopsis && credit == creditText.isVisible()) {
        return; // nothing to redraw
    }
    shownTitle    = title;
    shownMeta     = meta;
    shownSynopsis = synopsis;
    titleText.setText(title.c_str());
    metaText.setText(meta.c_str());
    synopsisText.setText(synopsis.c_str());

    //! stack the texts under each other
    float y = -TEXT_TOP;
    titleText.setPosition(titleText.getOffsetX(), y);
    y -= titleText.getTextHeight() + 14.0f;
    metaText.setPosition(metaText.getOffsetX(), y);
    if (!meta.empty()) {
        y -= metaText.getTextHeight() + 26.0f;
    }
    synopsisText.setPosition(synopsisText.getOffsetX(), y);
    creditText.setVisible(credit);
    layerDirty            = true;
    layerValid            = false; // no stale texts meanwhile
    framesSinceTextChange = 0;
}

void TvInfoFrame::process() {
    uint32_t generation = GameDb::generation();
    if (generation != seenGeneration) {
        seenGeneration = generation;
        refreshText();
        if (!coverLoaded) {
            framesSinceShown = std::min(framesSinceShown, ICON_AFTER_FRAMES); // a cover may have arrived
        }
    }

    if (!coverLoaded && !gameDbId.empty() && framesSinceShown <= ICON_AFTER_FRAMES) {
        framesSinceShown++;
        if (!tryCover() && framesSinceShown == ICON_AFTER_FRAMES) {
            showIcon(true);
        }
    }

    framesSinceTextChange++;
    //! here, before this frame's GamePad and TV passes start (they set their own GPU state)
    if (layerDirty && video != nullptr && isVisible() && framesSinceTextChange >= TEXT_AFTER_FRAMES) {
        renderStaticLayer(video);
    }
    GuiFrame::process();
}

void TvInfoFrame::renderStaticLayer(CVideo *pVideo) {
    if (layerContext == nullptr) {
        //! a render target with the TV's size, sampled as a texture afterwards
        uint32_t w = pVideo->getTvWidth(), h = pVideo->getTvHeight();
        GX2InitColorBuffer(&layerBuffer, GX2_SURFACE_DIM_TEXTURE_2D, w, h, 1, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_AA_MODE1X);
        layerBuffer.surface.image = memalign(layerBuffer.surface.alignment, layerBuffer.surface.imageSize);
        layerContext              = (GX2ContextState *) memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
        if (layerBuffer.surface.image == nullptr || layerContext == nullptr) {
            DEBUG_FUNCTION_LINE("No memory for the TV text layer");
            free(layerBuffer.surface.image);
            free(layerContext);
            layerBuffer.surface.image = nullptr;
            layerContext              = nullptr;
            layerDirty                = false; // texts are drawn directly then
            return;
        }
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, layerBuffer.surface.image, layerBuffer.surface.imageSize);
        GX2SetupContextStateEx(layerContext, GX2_TRUE);
        GX2SetContextState(layerContext);
        GX2SetColorBuffer(&layerBuffer, GX2_RENDER_TARGET_0);

        GX2InitTexture(&layerTexture, w, h, 1, 0, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_SURFACE_DIM_TEXTURE_2D, GX2_TILE_MODE_DEFAULT);
        layerTexture.surface.image     = layerBuffer.surface.image;
        layerTexture.surface.imageSize = layerBuffer.surface.imageSize;
        layerTexture.surface.mipmaps   = layerBuffer.surface.mipmaps;
        GX2InitSampler(&layerSampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
    }

    GX2SetContextState(layerContext);
    GX2SetViewport(0.0f, 0.0f, layerBuffer.surface.width, layerBuffer.surface.height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, layerBuffer.surface.width, layerBuffer.surface.height);
    GX2SetDepthOnlyControl(GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, GX2_DISABLE, GX2_ENABLE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD, GX2_ENABLE,
                       GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, GX2_DISABLE, GX2_ENABLE);

    background->draw(pVideo); // opaque, covers the whole layer
    titleText.draw(pVideo);
    metaText.draw(pVideo);
    synopsisText.draw(pVideo);
    creditText.draw(pVideo);

    GX2Invalidate((GX2InvalidateMode) (GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), layerBuffer.surface.image, layerBuffer.surface.imageSize);
    layerDirty = false;
    layerValid = true;
}

void TvInfoFrame::drawStaticLayer(CVideo *pVideo) {
    Texture2DShader::instance()->setShaders();
    Texture2DShader::instance()->setAttributeBuffer(); // full screen quad
    Texture2DShader::instance()->setAngle(0.0f);
    Texture2DShader::instance()->setOffset(glm::vec3(0.0f));
    Texture2DShader::instance()->setScale(glm::vec3(1.0f, 1.0f, 1.0f));
    Texture2DShader::instance()->setColorIntensity(glm::vec4(1.0f));
    Texture2DShader::instance()->setBlurring(glm::vec3(0.0f));
    Texture2DShader::instance()->setTextureAndSampler(&layerTexture, &layerSampler);
    Texture2DShader::instance()->draw();
}

void TvInfoFrame::draw(CVideo *pVideo) {
    if (!isVisible()) {
        return;
    }
    video = pVideo;
    //! the background must not write depth: the icon (a 3D model) is drawn behind z = 0
    GX2SetDepthOnlyControl(GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS);
    if (layerValid) {
        drawStaticLayer(pVideo);
    } else {
        background->draw(pVideo);
        if (layerContext == nullptr && !layerDirty) {
            //! no layer (out of memory): the texts directly
            titleText.draw(pVideo);
            metaText.draw(pVideo);
            synopsisText.draw(pVideo);
            creditText.draw(pVideo);
        }
    }
    GX2SetDepthOnlyControl(GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_LEQUAL);
    GuiFrame::draw(pVideo); // cover / icon
}
