#include "ps5_buttons.hpp"

#ifdef __PROSPERO__
#include "JSystem/J2DGraph/J2DPicture.h"
#include "dusk/settings.h"
#include <xxhash.h>
#include <algorithm>
#include <atomic>
#include <iterator>

namespace dusk::ps5_buttons {
namespace {
enum class Style { Face, Shoulder, InlineFace, InlineShoulder };
struct Resource {
    unsigned width, height, format;
    unsigned long long hash;
    Style style;
};
constexpr Resource resources[] = {
#include "../../ps5/art/generated-button-textures.inc"
};
std::atomic_bool available{false};

const Resource* identify(const ResTIMG* info) {
    if (info == nullptr) return nullptr;
    // Width/height/format gate avoids hashing ordinary UI and scene textures.
    for (const auto& resource : resources) {
        if (info->width != resource.width || info->height != resource.height ||
            info->format != resource.format) continue;
        const unsigned block_height = info->format == 0 || info->format == 8 ? 8 : 4;
        const std::size_t bytes = ((resource.width + 7) / 8) *
                                  ((resource.height + block_height - 1) / block_height) * 32;
        const auto* data = reinterpret_cast<const unsigned char*>(info) + info->imageOffset;
        const auto hash = XXH64(data, bytes, 0);
        // Hash once even when multiple prompts have the same dimensions.
        for (const auto& candidate : resources) {
            if (candidate.width == resource.width && candidate.height == resource.height &&
                candidate.format == resource.format && candidate.hash == hash) return &candidate;
        }
        return nullptr;
    }
    return nullptr;
}
}

void set_available(std::size_t registrations) {
    available.store(registrations == std::size(resources), std::memory_order_relaxed);
}

namespace {
// Adapted from Gary Riches' PS5_Dusklight colour pass (MIT). Match the exact A
// label, then recolour its green backing within that button's pane hierarchy.
bool green(const JUtility::TColor& c) {
    return c.g >= 60 && c.g > c.r + c.r / 3 && c.g > c.b + c.b / 3;
}
JUtility::TColor blue(const JUtility::TColor& c) {
    return {u8(c.r * 0.55f), u8(c.g * 0.62f), c.g, c.a};
}
void recolour(J2DPane& pane, J2DPane& letter) {
    if (&pane != &letter && pane.getTypeID() == 18) {
        auto& picture = static_cast<J2DPicture&>(pane);
        const auto black = picture.getBlack(), white = picture.getWhite();
        if (green(black) || green(white))
            picture.setBlackWhite(green(black) ? blue(black) : black,
                                  green(white) ? blue(white) : white);
    }
    for (auto* child = pane.getFirstChildPane(); child; child = child->getNextChildPane())
        recolour(*child,letter);
}
void button_colours(J2DPane& pane) {
    for (auto* child = pane.getFirstChildPane(); child; child = child->getNextChildPane()) {
        if (child->getTypeID() == 18) {
            auto* texture = static_cast<J2DPicture*>(child)->getTexture(0);
            const auto* resource = identify(texture ? texture->getTexInfo() : nullptr);
            if (resource == &resources[0]) recolour(pane,*child);
        }
        button_colours(*child);
    }
}
}

void prepare_screen(J2DPane& pane) {
    if (available.load(std::memory_order_relaxed) &&
        getSettings().game.enableTextureReplacements) button_colours(pane);
}

bool draw(J2DPicture& picture, float x, float y, float width, float height, Mtx* parent) {
    if (!available.load(std::memory_order_relaxed) ||
        !getSettings().game.enableTextureReplacements) return false;
    auto* texture = picture.getTexture(0);
    const auto* resource = identify(texture != nullptr ? texture->getTexInfo() : nullptr);
    if (resource == nullptr || (resource->style != Style::InlineFace &&
                                resource->style != Style::InlineShoulder)) return false;

    Mtx matrix;
    const auto alpha = parent != nullptr ? picture.mColorAlpha : picture.mAlpha;
    if (parent != nullptr) {
        MTXConcat(*parent, picture.mGlobalMtx, matrix);
    } else {
        picture.makeMatrix(x, y, 0.0f, 0.0f);
        MTXCopy(picture.mPositionMtx, matrix);
        x = y = 0.0f;
    }

    // Only inline icons contain a body and letter in the same source texture.
    // Preserve their existing layout dimensions and draw our shaded replacement
    // without the Nintendo-specific tint. The normal HUD renderer stays intact.
    const float cap_width = width, cap_height = height;

    texture->load(GX_TEXMAP0);
    GXLoadPosMtxImm(matrix, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);
    GXSetNumTexGens(1);
    GXSetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL,
                  GX_DF_NONE, GX_AF_NONE);
    GXSetNumIndStages(0);
    GXSetNumTevStages(1);
    GXSetTevDirect(GX_TEVSTAGE0);
    GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    constexpr unsigned corners[] = {0,1,3,2};
    for (unsigned i = 0; i < 4; ++i) {
        const float u = i == 1 || i == 2 ? 1.0f : 0.0f;
        const float v = i >= 2 ? 1.0f : 0.0f;
        GXPosition3f32(x + u * cap_width, y + v * cap_height, 0.0f);
        GXColor4u8(255,255,255, unsigned(picture.corner(corners[i]).a) * alpha / 255);
        GXTexCoord2f32(u,v);
    }
    GXEnd();
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_U16, 15);
    return true;
}
}
#endif
