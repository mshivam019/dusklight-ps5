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
enum class Style { Backing, Face, Shoulder, InlineFace, InlineShoulder };
struct Resource {
    unsigned width, height, format;
    unsigned long long hash;
    Style style;
};
constexpr Resource resources[] = {
#include "../../ps5/art/generated-button-textures.inc"
};
constexpr std::size_t prompt_count() {
    std::size_t count = 0;
    for (const auto& resource : resources) count += resource.style != Style::Backing;
    return count;
}
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
    // Never hide original button bodies if the complete independent artwork is missing.
    available.store(registrations == prompt_count(), std::memory_order_relaxed);
}

bool draw(J2DPicture& picture, float x, float y, float width, float height, Mtx* parent) {
    if (!available.load(std::memory_order_relaxed) ||
        !getSettings().game.enableTextureReplacements) return false;
    auto* texture = picture.getTexture(0);
    const auto* resource = identify(texture != nullptr ? texture->getTexInfo() : nullptr);
    if (resource == nullptr) return false;
    if (resource->style == Style::Backing) return true;

    Mtx matrix;
    const auto alpha = parent != nullptr ? picture.mColorAlpha : picture.mAlpha;
    if (parent != nullptr) {
        MTXConcat(*parent, picture.mGlobalMtx, matrix);
    } else {
        picture.makeMatrix(x, y, 0.0f, 0.0f);
        MTXCopy(picture.mPositionMtx, matrix);
        x = y = 0.0f;
    }

    // Complete caps replace both the separate GC letter and its bean/coloured
    // backing. Centre a round cap (or wide shoulder) instead of stretching the
    // PS5 artwork into the original narrow letter's quad.
    float cap_width, cap_height;
    if (resource->style == Style::Face) {
        cap_width = cap_height = std::max(width, height) * 1.15f;
    } else if (resource->style == Style::Shoulder) {
        cap_width = height * 1.5f;
        cap_height = cap_width; // artwork has transparent top/bottom margins
    } else {
        cap_width = cap_height = std::max(width, height);
        if (resource->style == Style::InlineShoulder) cap_width = cap_height = height * 1.25f;
    }
    x += (width - cap_width) * 0.5f;
    y += (height - cap_height) * 0.5f;

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
