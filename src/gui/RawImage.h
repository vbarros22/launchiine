#pragma once

#include <gui/GuiElement.h>
#include <gx2/sampler.h>
#include <gx2/texture.h>

/**
 * Image made directly from RGB565 pixels (expanded to an RGBA8 texture), no image decoding
 * on the render thread (GuiImageData converts every pixel through libgd, which stalls the
 * menu for large pictures like covers). Create, draw and delete on the render thread; delete
 * through AsyncExecutor::pushForDelete so the GPU is done with the texture.
 */
class RawImage : public GuiElement {
public:
    RawImage(uint32_t width, uint32_t height, const uint16_t *rgb565);
    ~RawImage() override;

    bool isValid() const {
        return texture.surface.image != nullptr;
    }

    void draw(CVideo *pVideo) override;

private:
    GX2Texture texture{};
    GX2Sampler sampler{};
};
