#include "RawImage.h"
#include <cstring>
#include <gui/video/CVideo.h>
#include <gui/video/shaders/Texture2DShader.h>
#include <gx2/mem.h>
#include <malloc.h>

RawImage::RawImage(uint32_t w, uint32_t h, const uint16_t *rgb565) : GuiElement() {
    width  = w;
    height = h;
    GX2InitTexture(&texture, w, h, 1, 0, GX2_SURFACE_FORMAT_UNORM_R5_G6_B5, GX2_SURFACE_DIM_TEXTURE_2D, GX2_TILE_MODE_LINEAR_ALIGNED);
    if (texture.surface.imageSize == 0) {
        return;
    }
    texture.surface.image = memalign(texture.surface.alignment, texture.surface.imageSize);
    if (texture.surface.image == nullptr) {
        return;
    }
    //! the GPU rows are padded to the pitch
    auto *dst = (uint16_t *) texture.surface.image;
    for (uint32_t y = 0; y < h; y++) {
        memcpy(dst + y * texture.surface.pitch, rgb565 + y * w, w * sizeof(uint16_t));
    }
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, texture.surface.image, texture.surface.imageSize);
    GX2InitSampler(&sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);
}

RawImage::~RawImage() {
    if (texture.surface.image != nullptr) {
        free(texture.surface.image);
        texture.surface.image = nullptr;
    }
}

void RawImage::draw(CVideo *pVideo) {
    if (!isVisible() || !isValid()) {
        return;
    }
    glm::vec3 offset(getCenterX() * pVideo->getWidthScaleFactor() * 2.0f, getCenterY() * pVideo->getHeightScaleFactor() * 2.0f,
                     getDepth() * pVideo->getDepthScaleFactor() * 2.0f);
    glm::vec3 scale(getScaleX() * getWidth() * pVideo->getWidthScaleFactor(), getScaleY() * getHeight() * pVideo->getHeightScaleFactor(), getScaleZ());
    glm::vec4 color(1.0f, 1.0f, 1.0f, getAlpha());

    Texture2DShader::instance()->setShaders();
    Texture2DShader::instance()->setAttributeBuffer(); // default quad
    Texture2DShader::instance()->setAngle(0.0f);
    Texture2DShader::instance()->setOffset(offset);
    Texture2DShader::instance()->setScale(scale);
    Texture2DShader::instance()->setColorIntensity(color);
    Texture2DShader::instance()->setBlurring(glm::vec3(0.0f));
    Texture2DShader::instance()->setTextureAndSampler(&texture, &sampler);
    Texture2DShader::instance()->draw(GX2_PRIMITIVE_MODE_QUADS, 4);
}
