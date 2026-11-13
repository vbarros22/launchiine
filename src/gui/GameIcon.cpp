#include "GameIcon.h"
#include "Application.h"
#include "GameIconModel.h"
#include "resources/Resources.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include <gui/video/CVideo.h>
#include <gui/video/shaders/Shader3D.h>
#include <gui/video/shaders/ShaderFractalColor.h>

static const float cfIconMirrorScale = 1.15f;
static const float cfIconMirrorAlpha = 0.45f;

GameIcon::GameIcon(GuiImageData *preloadImage)
    : GuiImage(preloadImage) {
    loadTheme();
    bSelected                        = false;
    bRenderStroke                    = true;
    bRenderReflection                = false;
    bIconLast                        = false;
    strokeFractalEnable              = 1;
    strokeBlurBorder                 = 0.0f;
    distanceFadeout                  = 0.0f;
    rotationX                        = 0.0f;
    reflectionAlpha                  = 0.4f;
    strokeWidth                      = 2.35f;
    colorIntensity                   = glm::vec4(1.0f);
    colorIntensityMirror             = colorIntensity;
    alphaFadeOutNorm                 = glm::vec4(0.0f);
    alphaFadeOutRefl                 = glm::vec4(-1.0f, 0.0f, 0.9f, 1.0f);
    selectionBlurOuterColorIntensity = glm::vec4(0.09411764f * 1.15f, 0.56862745f * 1.15f, 0.96862745098f * 1.15f, 1.0f);
    selectionBlurOuterSize           = 1.65f;
    selectionBlurOuterBorderSize     = 0.5f;
    selectionBlurInnerColorIntensity = glm::vec4(0.46666667f, 0.90588235f, 1.0f, 1.0f);
    selectionBlurInnerSize           = 1.45f;
    selectionBlurInnerBorderSize     = 0.95f;

    vtxCount = sizeof(cfGameIconPosVtxs) / (Shader3D::cuVertexAttrSize);

    //! texture and vertex coordinates
    posVtxs   = (float *) memalign(GX2_VERTEX_BUFFER_ALIGNMENT, sizeof(cfGameIconPosVtxs));
    texCoords = (float *) memalign(GX2_VERTEX_BUFFER_ALIGNMENT, sizeof(cfGameIconTexCoords));

    if (posVtxs) {
        memcpy((float *) posVtxs, cfGameIconPosVtxs, sizeof(cfGameIconPosVtxs));
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, (float *) posVtxs, sizeof(cfGameIconPosVtxs));
    }
    if (texCoords) {
        memcpy((float *) texCoords, cfGameIconTexCoords, sizeof(cfGameIconTexCoords));
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, (float *) texCoords, sizeof(cfGameIconTexCoords));
    }

    //! create vertexes for the mirror frame
    texCoordsMirror = (float *) memalign(GX2_VERTEX_BUFFER_ALIGNMENT, sizeof(cfGameIconTexCoords));

    if (texCoordsMirror) {
        for (uint32_t i = 0; i < vtxCount; i++) {
            texCoordsMirror[i * 2 + 0] = texCoords[i * 2 + 0] * cfIconMirrorScale - ((cfIconMirrorScale - 1.0f) - (cfIconMirrorScale - 1.0f) * 0.5f);
            texCoordsMirror[i * 2 + 1] = texCoords[i * 2 + 1] * cfIconMirrorScale - ((cfIconMirrorScale - 1.0f) - (cfIconMirrorScale - 1.0f) * 0.5f);
        }
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, texCoordsMirror, sizeof(cfGameIconTexCoords));
    }

    //! setup stroke of the icon
    strokePosVtxs = (float *) memalign(GX2_VERTEX_BUFFER_ALIGNMENT, sizeof(cfGameIconStrokeVtxs));
    if (strokePosVtxs) {
        memcpy(strokePosVtxs, cfGameIconStrokeVtxs, sizeof(cfGameIconStrokeVtxs));
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, strokePosVtxs, sizeof(cfGameIconStrokeVtxs));
    }
    strokeTexCoords = (float *) memalign(GX2_VERTEX_BUFFER_ALIGNMENT, cuGameIconStrokeVtxCount * Shader::cuTexCoordAttrSize);
    if (strokeTexCoords) {
        for (size_t i = 0, n = 0; i < cuGameIconStrokeVtxCount; n += 2, i += 3) {
            strokeTexCoords[n]     = (1.0f + strokePosVtxs[i]) * 0.5f;
            strokeTexCoords[n + 1] = 1.0f - (1.0f + strokePosVtxs[i + 1]) * 0.5f;
        }
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, strokeTexCoords, cuGameIconStrokeVtxCount * Shader::cuTexCoordAttrSize);
    }
    strokeColorVtxs = (uint8_t *) memalign(GX2_VERTEX_BUFFER_ALIGNMENT, cuGameIconStrokeVtxCount * Shader::cuColorAttrSize);
    if (strokeColorVtxs) {
        for (size_t i = 0; i < (cuGameIconStrokeVtxCount * Shader::cuColorAttrSize); i++)
            strokeColorVtxs[i] = 0xff;
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, strokeColorVtxs, cuGameIconStrokeVtxCount * Shader::cuColorAttrSize);
    }
}

GameIcon::~GameIcon() {
    //! remove image so it can not be drawn anymore from this point on
    imageData = nullptr;

    //! main image vertexes
    if (posVtxs) {
        free((void *) posVtxs);
        posVtxs = nullptr;
    }
    if (texCoords) {
        free((void *) texCoords);
        texCoords = nullptr;
    }
    //! mirror image vertexes
    if (texCoordsMirror) {
        free(texCoordsMirror);
        texCoordsMirror = nullptr;
    }
    //! stroke image vertexes
    if (strokePosVtxs) {
        free(strokePosVtxs);
        strokePosVtxs = nullptr;
    }
    if (strokeTexCoords) {
        free(strokeTexCoords);
        strokeTexCoords = nullptr;
    }
    if (strokeColorVtxs) {
        free(strokeColorVtxs);
        strokeColorVtxs = nullptr;
    }
}

bool GameIcon::checkRayIntersection(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirFrac) {
    //! since we always face the camera we can just check the AABB intersection
    //! otherwise an OOB intersection would be required

    float currPosX   = getCenterX() * Application::instance()->getVideo()->getWidthScaleFactor() * 2.0f;
    float currPosY   = getCenterY() * Application::instance()->getVideo()->getHeightScaleFactor() * 2.0f;
    float currPosZ   = getDepth() * Application::instance()->getVideo()->getDepthScaleFactor() * 2.0f;
    float currScaleX = getScaleX() * (float) getWidth() * Application::instance()->getVideo()->getWidthScaleFactor();
    float currScaleY = getScaleY() * (float) getHeight() * Application::instance()->getVideo()->getHeightScaleFactor();
    float currScaleZ = getScaleZ() * (float) getWidth() * Application::instance()->getVideo()->getDepthScaleFactor();
    //! lb is the corner of AABB with minimal coordinates - left bottom, rt is maximal corner
    glm::vec3 lb(currPosX - currScaleX, currPosY - currScaleY, currPosZ - currScaleZ);
    glm::vec3 rt(currPosX + currScaleX, currPosY + currScaleY, currPosZ + currScaleZ);

    float t1 = (lb.x - rayOrigin.x) * rayDirFrac.x;
    float t2 = (rt.x - rayOrigin.x) * rayDirFrac.x;
    float t3 = (lb.y - rayOrigin.y) * rayDirFrac.y;
    float t4 = (rt.y - rayOrigin.y) * rayDirFrac.y;
    float t5 = (lb.z - rayOrigin.z) * rayDirFrac.z;
    float t6 = (rt.z - rayOrigin.z) * rayDirFrac.z;

    float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
    float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

    //! if tmax < 0, ray (line) is intersecting AABB, but whole AABB is behing us
    if (tmax < 0) {
        //t = tmax;
        return false;
    }

    //! if tmin > tmax, ray doesn't intersect AABB
    if (tmin > tmax) {
        //t = tmax;
        return false;
    }

    //t = tmin;
    return true;
}

namespace {
    //! Light theme tile, shared by every icon. The textures are 256 px and the icon model
    //! (about +-0.93) covers the middle ~164 px, so the quad is the icon scaled by this:
    constexpr float cfTileScale = 1.45f;
    GuiImageData *tileData            = nullptr;
    GuiImageData *tileSelectedData    = nullptr;
    GuiImageData *tileSelectedOverlay = nullptr;

    void drawQuad(GuiImageData *data, const glm::mat4 &projectionMtx, const glm::mat4 &viewMtx, const glm::mat4 &view,
                  const glm::vec4 &color, const glm::vec4 &alphaFadeOut) {
        if (data == nullptr || data->getTexture() == nullptr) {
            return;
        }
        Shader3D::instance()->setShaders();
        Shader3D::instance()->setProjectionMtx(projectionMtx);
        Shader3D::instance()->setViewMtx(viewMtx);
        Shader3D::instance()->setTextureAndSampler(data->getTexture(), data->getSampler());
        Shader3D::instance()->setAlphaFadeOut(alphaFadeOut);
        Shader3D::instance()->setDistanceFadeOut(0.0f);
        Shader3D::instance()->setModelViewMtx(view);
        Shader3D::instance()->setColorIntensity(color);
        Shader3D::instance()->setAttributeBuffer(); // default quad
        Shader3D::instance()->draw(GX2_PRIMITIVE_MODE_QUADS, 4);
    }
} // namespace

void GameIcon::loadTheme() {
    //! render thread (GameIcons are created there); kept for the whole run
    if (tileData == nullptr) {
        tileData            = Resources::GetImageData("tile.png");
        tileSelectedData    = Resources::GetImageData("tileSelected.png");
        tileSelectedOverlay = Resources::GetImageData("tileSelectedOverlay.png");
    }
}

void GameIcon::draw(CVideo *pVideo, const glm::mat4 &projectionMtx, const glm::mat4 &viewMtx, const glm::mat4 &modelView) {
    if (imageData == nullptr) {
        return;
    }
    float currPosX   = getCenterX() * pVideo->getWidthScaleFactor() * 2.0f;
    float currPosY   = getCenterY() * pVideo->getHeightScaleFactor() * 2.0f;
    float currPosZ   = getDepth() * pVideo->getDepthScaleFactor() * 2.0f;
    float currScaleX = getScaleX() * (float) getWidth() * pVideo->getWidthScaleFactor();
    float currScaleY = getScaleY() * (float) getHeight() * pVideo->getHeightScaleFactor();
    float currScaleZ = getScaleZ() * (float) getWidth() * pVideo->getDepthScaleFactor();

    glm::mat4 iconView = glm::translate(modelView, glm::vec3(currPosX, currPosY, currPosZ));
    iconView           = glm::rotate(iconView, DegToRad(rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 tileView = glm::scale(iconView, glm::vec3(currScaleX * cfTileScale, currScaleY * cfTileScale, 1.0f));
    iconView           = glm::scale(iconView, glm::vec3(currScaleX, currScaleY, currScaleZ));

    colorIntensity[3] = getAlpha();
    glm::vec4 tileColor(1.0f, 1.0f, 1.0f, getAlpha());

    //! 1. the glass tile behind the icon, without depth writes so it never hides an icon
    GX2SetDepthOnlyControl(GX2_ENABLE, GX2_DISABLE, GX2_COMPARE_FUNC_LEQUAL);
    drawQuad(bSelected ? tileSelectedData : tileData, projectionMtx, viewMtx, tileView, tileColor, alphaFadeOutNorm);
    GX2SetDepthOnlyControl(GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_LEQUAL);

    //! 2. the icon
    Shader3D::instance()->setShaders();
    Shader3D::instance()->setProjectionMtx(projectionMtx);
    Shader3D::instance()->setViewMtx(viewMtx);
    Shader3D::instance()->setTextureAndSampler(imageData->getTexture(), imageData->getSampler());
    Shader3D::instance()->setAlphaFadeOut(alphaFadeOutNorm);
    Shader3D::instance()->setDistanceFadeOut(distanceFadeout);
    Shader3D::instance()->setModelViewMtx(iconView);
    Shader3D::instance()->setColorIntensity(colorIntensity);
    Shader3D::instance()->setAttributeBuffer(vtxCount, posVtxs, texCoords);
    Shader3D::instance()->draw(GX2_PRIMITIVE_MODE_QUADS, vtxCount);

    //! 3. selection: corner brackets and the "Start" band on top of the icon
    if (bSelected) {
        GX2SetDepthOnlyControl(GX2_DISABLE, GX2_DISABLE, GX2_COMPARE_FUNC_ALWAYS);
        drawQuad(tileSelectedOverlay, projectionMtx, viewMtx, tileView, tileColor, alphaFadeOutNorm);
        GX2SetDepthOnlyControl(GX2_ENABLE, GX2_ENABLE, GX2_COMPARE_FUNC_LEQUAL);
    }
}
