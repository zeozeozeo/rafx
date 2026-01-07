#include "rafx.h"
#include "rafx_internal.h"
#include <vector>
#include <map>
#include <string>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <source_location>
#include <fstream>

#include <NRD.h>
#include <NRDIntegration.h>

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__) || defined(__GNUG__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include <NRDIntegration.hpp> /* impl */

#if defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#    pragma GCC diagnostic pop
#endif

struct RfxDenoiserImpl {
    nrd::Integration instance;
    RfxDenoiserType type;
    nrd::Identifier identifier;
    uint32_t width;
    uint32_t height;
    nrd::DenoiserDesc denoiserDesc;
    uint32_t lastFrameIndex = (uint32_t)-1;
};

//
// Helpers
//

static void TransitionAS(RfxCommandList cmd, RfxAccelerationStructureImpl* as, nri::AccessBits nextAccess, nri::StageBits nextStage) {
    if (as->currentAccess == nextAccess && as->currentStage == nextStage)
        return;

    nri::GlobalBarrierDesc& desc = cmd->barriers.globalBarriers.emplace_back();
    desc.before = { as->currentAccess, as->currentStage };
    desc.after = { nextAccess, nextStage };

    as->currentAccess = nextAccess;
    as->currentStage = nextStage;
}

static nri::DispatchUpscaleBits ToNRIUpscaleDispatchBits(RfxUpscaleDispatchFlags flags) {
    nri::DispatchUpscaleBits bits = nri::DispatchUpscaleBits::NONE;
    if (flags & RFX_UPSCALE_DISPATCH_RESET_HISTORY)
        bits |= nri::DispatchUpscaleBits::RESET_HISTORY;
    if (flags & RFX_UPSCALE_DISPATCH_USE_SPECULAR_MOTION)
        bits |= nri::DispatchUpscaleBits::USE_SPECULAR_MOTION;
    return bits;
}

static nri::ShadingRate ToNRIShadingRate(RfxShadingRate rate) {
    switch (rate) {
    case RFX_SHADING_RATE_1X1: return nri::ShadingRate::FRAGMENT_SIZE_1X1;
    case RFX_SHADING_RATE_1X2: return nri::ShadingRate::FRAGMENT_SIZE_1X2;
    case RFX_SHADING_RATE_2X1: return nri::ShadingRate::FRAGMENT_SIZE_2X1;
    case RFX_SHADING_RATE_2X2: return nri::ShadingRate::FRAGMENT_SIZE_2X2;
    case RFX_SHADING_RATE_2X4: return nri::ShadingRate::FRAGMENT_SIZE_2X4;
    case RFX_SHADING_RATE_4X2: return nri::ShadingRate::FRAGMENT_SIZE_4X2;
    case RFX_SHADING_RATE_4X4: return nri::ShadingRate::FRAGMENT_SIZE_4X4;
    default: return nri::ShadingRate::FRAGMENT_SIZE_1X1;
    }
}

static nri::ShadingRateCombiner ToNRIShadingRateCombiner(RfxShadingRateCombiner op) {
    switch (op) {
    case RFX_SHADING_RATE_COMBINER_PASSTHROUGH: return nri::ShadingRateCombiner::KEEP;
    case RFX_SHADING_RATE_COMBINER_OVERRIDE: return nri::ShadingRateCombiner::REPLACE;
    case RFX_SHADING_RATE_COMBINER_MIN: return nri::ShadingRateCombiner::MIN;
    case RFX_SHADING_RATE_COMBINER_MAX: return nri::ShadingRateCombiner::MAX;
    case RFX_SHADING_RATE_COMBINER_SUM: return nri::ShadingRateCombiner::SUM;
    default: return nri::ShadingRateCombiner::KEEP;
    }
}

static nri::StencilOp ToNRIStencilOp(RfxStencilOp op) {
    switch (op) {
    case RFX_STENCIL_OP_KEEP: return nri::StencilOp::KEEP;
    case RFX_STENCIL_OP_ZERO: return nri::StencilOp::ZERO;
    case RFX_STENCIL_OP_REPLACE: return nri::StencilOp::REPLACE;
    case RFX_STENCIL_OP_INCREMENT_AND_CLAMP: return nri::StencilOp::INCREMENT_AND_CLAMP;
    case RFX_STENCIL_OP_DECREMENT_AND_CLAMP: return nri::StencilOp::DECREMENT_AND_CLAMP;
    case RFX_STENCIL_OP_INVERT: return nri::StencilOp::INVERT;
    case RFX_STENCIL_OP_INCREMENT_AND_WRAP: return nri::StencilOp::INCREMENT_AND_WRAP;
    case RFX_STENCIL_OP_DECREMENT_AND_WRAP: return nri::StencilOp::DECREMENT_AND_WRAP;
    default: return nri::StencilOp::KEEP;
    }
}

static nri::CompareOp ToNRICompareOp(RfxCompareOp op) {
    switch (op) {
    case RFX_COMPARE_NEVER: return nri::CompareOp::NEVER;
    case RFX_COMPARE_LESS: return nri::CompareOp::LESS;
    case RFX_COMPARE_EQUAL: return nri::CompareOp::EQUAL;
    case RFX_COMPARE_LESS_EQUAL: return nri::CompareOp::LESS_EQUAL;
    case RFX_COMPARE_GREATER: return nri::CompareOp::GREATER;
    case RFX_COMPARE_NOT_EQUAL: return nri::CompareOp::NOT_EQUAL;
    case RFX_COMPARE_GREATER_EQUAL: return nri::CompareOp::GREATER_EQUAL;
    case RFX_COMPARE_ALWAYS: return nri::CompareOp::ALWAYS;
    default: return nri::CompareOp::LESS;
    }
}

static nri::UpscalerType ToNRIUpscalerType(RfxUpscalerType type) {
    switch (type) {
    case RFX_UPSCALER_NIS: return nri::UpscalerType::NIS;
    case RFX_UPSCALER_FSR: return nri::UpscalerType::FSR;
    case RFX_UPSCALER_XESS: return nri::UpscalerType::XESS;
    case RFX_UPSCALER_DLSR: return nri::UpscalerType::DLSR;
    case RFX_UPSCALER_DLRR: return nri::UpscalerType::DLRR;
    default: return nri::UpscalerType::NIS;
    }
}

static nri::UpscalerMode ToNRIUpscalerMode(RfxUpscalerMode mode) {
    switch (mode) {
    case RFX_UPSCALER_MODE_NATIVE: return nri::UpscalerMode::NATIVE;
    case RFX_UPSCALER_MODE_ULTRA_QUALITY: return nri::UpscalerMode::ULTRA_QUALITY;
    case RFX_UPSCALER_MODE_QUALITY: return nri::UpscalerMode::QUALITY;
    case RFX_UPSCALER_MODE_BALANCED: return nri::UpscalerMode::BALANCED;
    case RFX_UPSCALER_MODE_PERFORMANCE: return nri::UpscalerMode::PERFORMANCE;
    case RFX_UPSCALER_MODE_ULTRA_PERFORMANCE: return nri::UpscalerMode::ULTRA_PERFORMANCE;
    default: return nri::UpscalerMode::NATIVE;
    }
}

static nri::UpscalerBits ToNRIUpscalerBits(RfxUpscalerFlags flags) {
    nri::UpscalerBits bits = nri::UpscalerBits::NONE;
    if (flags & RFX_UPSCALER_HDR)
        bits |= nri::UpscalerBits::HDR;
    if (flags & RFX_UPSCALER_SRGB)
        bits |= nri::UpscalerBits::SRGB;
    if (flags & RFX_UPSCALER_DEPTH_INVERTED)
        bits |= nri::UpscalerBits::DEPTH_INVERTED;
    if (flags & RFX_UPSCALER_DEPTH_INFINITE)
        bits |= nri::UpscalerBits::DEPTH_INFINITE;
    if (flags & RFX_UPSCALER_DEPTH_LINEAR)
        bits |= nri::UpscalerBits::DEPTH_LINEAR;
    if (flags & RFX_UPSCALER_MV_UPSCALED)
        bits |= nri::UpscalerBits::MV_UPSCALED;
    if (flags & RFX_UPSCALER_MV_JITTERED)
        bits |= nri::UpscalerBits::MV_JITTERED;
    return bits;
}

static nri::MicromapFormat ToNRIMicromapFormat(RfxMicromapFormat fmt) {
    return (fmt == RFX_MICROMAP_FORMAT_OPACITY_2_STATE) ? nri::MicromapFormat::OPACITY_2_STATE : nri::MicromapFormat::OPACITY_4_STATE;
}

static nri::MicromapBits ToNRIMicromapBits(RfxBuildMicromapFlags flags) {
    nri::MicromapBits bits = nri::MicromapBits::NONE;
    if (flags & RFX_BUILD_MICROMAP_PREFER_FAST_TRACE)
        bits |= nri::MicromapBits::PREFER_FAST_TRACE;
    if (flags & RFX_BUILD_MICROMAP_PREFER_FAST_BUILD)
        bits |= nri::MicromapBits::PREFER_FAST_BUILD;
    if (flags & RFX_BUILD_MICROMAP_ALLOW_COMPACTION)
        bits |= nri::MicromapBits::ALLOW_COMPACTION;
    return bits;
}

static uint32_t AllocASSlot() {
    uint32_t id;
    if (!CORE.Bindless.freeASSlots.empty()) {
        id = CORE.Bindless.freeASSlots.back();
        CORE.Bindless.freeASSlots.pop_back();
    } else {
        RFX_ASSERT(CORE.Bindless.asHighWaterMark < 2048);
        id = CORE.Bindless.asHighWaterMark++;
    }
    return id;
}

static void FreeASSlot(uint32_t id) {
    CORE.Bindless.freeASSlots.push_back(id);
}

static uint64_t Align(uint64_t size, uint64_t alignment) {
    return (size + (alignment - 1)) & ~(alignment - 1);
}

static inline void MustTransition(
    RfxCommandList cmd
#ifdef RAFX_OPTIMAL_USAGE
    ,
    std::source_location loc = std::source_location::current()
#endif
) {
#ifdef RAFX_OPTIMAL_USAGE
    RFX_ASSERTF(
        !cmd->isRendering, "%s would break current pass; call it outside of rfxCmdBeginRenderPass/rfxCmdEndRenderPass", loc.function_name()
    );
#else
    if (cmd->isRendering) {
        CORE.NRI.CmdEndRendering(*cmd->nriCmd);
        cmd->isRendering = false;
    }
#endif
}

static bool HasStencil(nri::Format format) {
    return format == nri::Format::D24_UNORM_S8_UINT || format == nri::Format::D32_SFLOAT_S8_UINT_X24 ||
           format == nri::Format::X24_G8_UINT || format == nri::Format::X32_G8_UINT_X24;
}

static void GetNRIState(RfxResourceState state, nri::AccessBits& access, nri::Layout& layout, nri::StageBits& stage) {
    layout = nri::Layout::UNDEFINED;

    switch (state) {
    case RFX_STATE_UNDEFINED:
        access = nri::AccessBits::NONE;
        layout = nri::Layout::UNDEFINED;
        stage = nri::StageBits::ALL;
        break;
    case RFX_STATE_PRESENT:
        access = nri::AccessBits::NONE;
        layout = nri::Layout::PRESENT;
        stage = nri::StageBits::NONE;
        break;
    case RFX_STATE_COPY_SRC:
        access = nri::AccessBits::COPY_SOURCE;
        layout = nri::Layout::COPY_SOURCE;
        stage = nri::StageBits::COPY;
        break;
    case RFX_STATE_COPY_DST:
        access = nri::AccessBits::COPY_DESTINATION;
        layout = nri::Layout::COPY_DESTINATION;
        stage = nri::StageBits::COPY;
        break;
    case RFX_STATE_VERTEX_BUFFER:
        access = nri::AccessBits::VERTEX_BUFFER;
        stage = nri::StageBits::VERTEX_SHADER;
        break;
    case RFX_STATE_INDEX_BUFFER:
        access = nri::AccessBits::INDEX_BUFFER;
        stage = nri::StageBits::INDEX_INPUT;
        break;
    case RFX_STATE_INDIRECT_ARGUMENT:
        access = nri::AccessBits::ARGUMENT_BUFFER;
        stage = nri::StageBits::INDIRECT;
        break;
    case RFX_STATE_SHADER_READ:
        access = nri::AccessBits::SHADER_RESOURCE;
        layout = nri::Layout::SHADER_RESOURCE;
        stage = nri::StageBits::ALL;
        break;
    case RFX_STATE_SHADER_WRITE:
        access = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        layout = nri::Layout::SHADER_RESOURCE_STORAGE;
        stage = nri::StageBits::ALL;
        break;
    case RFX_STATE_RENDER_TARGET:
        access = nri::AccessBits::COLOR_ATTACHMENT;
        layout = nri::Layout::COLOR_ATTACHMENT;
        stage = nri::StageBits::COLOR_ATTACHMENT;
        break;
    case RFX_STATE_DEPTH_READ:
        access = nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_READ;
        layout = nri::Layout::DEPTH_STENCIL_READONLY;
        stage = nri::StageBits::DEPTH_STENCIL_ATTACHMENT;
        break;
    case RFX_STATE_DEPTH_WRITE:
        access = nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE;
        layout = nri::Layout::DEPTH_STENCIL_ATTACHMENT;
        stage = nri::StageBits::DEPTH_STENCIL_ATTACHMENT;
        break;
    case RFX_STATE_SCRATCH_BUFFER:
        access = nri::AccessBits::SCRATCH_BUFFER;
        layout = nri::Layout::UNDEFINED;
        stage = nri::StageBits::ACCELERATION_STRUCTURE; // TODO: or MICROMAP?
        break;
    case RFX_STATE_RESOLVE_SRC:
        access = nri::AccessBits::RESOLVE_SOURCE;
        layout = nri::Layout::RESOLVE_SOURCE;
        stage = nri::StageBits::RESOLVE;
        break;
    case RFX_STATE_RESOLVE_DST:
        access = nri::AccessBits::RESOLVE_DESTINATION;
        layout = nri::Layout::RESOLVE_DESTINATION;
        stage = nri::StageBits::RESOLVE;
        break;
    default:
        access = nri::AccessBits::NONE;
        layout = nri::Layout::UNDEFINED;
        stage = nri::StageBits::NONE;
        break;
    }
}

static void UploadToResource(
    RfxCommandList cmd, nri::Buffer* dstBuffer, uint64_t dstOffset, nri::Texture* dstTexture, const nri::TextureRegionDesc* dstRegion,
    const void* data, uint64_t size, uint32_t rowPitch, uint32_t slicePitch, RfxResourceState finalState, RfxBuffer bufferHandle,
    RfxTexture textureHandle
) {
    // stream data
    if (dstBuffer) {
        nri::DataSize chunk = { data, size };
        nri::StreamBufferDataDesc sbd = {};
        sbd.dataChunks = &chunk;
        sbd.dataChunkNum = 1;
        sbd.dstBuffer = dstBuffer;
        sbd.dstOffset = dstOffset;
        sbd.placementAlignment = 1;
        CORE.NRI.StreamBufferData(*CORE.NRIStreamer, sbd);
    } else {
        nri::StreamTextureDataDesc std = {};
        std.data = data;
        std.dataRowPitch = rowPitch;
        std.dataSlicePitch = slicePitch;
        std.dstTexture = dstTexture;
        if (dstRegion)
            std.dstRegion = *dstRegion;
        CORE.NRI.StreamTextureData(*CORE.NRIStreamer, std);
    }

    // sync
    if (bufferHandle) {
        nri::AccessBits finalAccess;
        nri::Layout finalLayout;
        nri::StageBits finalStage;
        GetNRIState(finalState, finalAccess, finalLayout, finalStage);

        auto preBarrier = [=](nri::CommandBuffer& cb) {
            nri::BufferBarrierDesc bbd = {};
            bbd.buffer = dstBuffer;
            bbd.before = { bufferHandle->currentAccess, bufferHandle->currentStage };
            bbd.after = { nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY };
            nri::BarrierDesc bd = {};
            bd.buffers = &bbd;
            bd.bufferNum = 1;
            CORE.NRI.CmdBarrier(cb, bd);
        };

        auto postBarrier = [=](nri::CommandBuffer& cb) {
            nri::BufferBarrierDesc bbd = {};
            bbd.buffer = dstBuffer;
            bbd.before = { nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY };
            bbd.after = { finalAccess, finalStage };
            nri::BarrierDesc bd = {};
            bd.buffers = &bbd;
            bd.bufferNum = 1;
            CORE.NRI.CmdBarrier(cb, bd);
        };

        bufferHandle->currentState = finalState;
        bufferHandle->currentAccess = finalAccess;
        bufferHandle->currentStage = finalStage;

        if (cmd) {
            preBarrier(*cmd->nriCmd);
            CORE.NRI.CmdCopyStreamedData(*cmd->nriCmd, *CORE.NRIStreamer);
            postBarrier(*cmd->nriCmd);
        } else {
            CORE.PendingPreBarriers.push_back(preBarrier);
            CORE.PendingPostBarriers.push_back(postBarrier);
        }
    }

    // texture sync
    if (textureHandle && textureHandle->state) {
        uint32_t mStart = dstRegion ? dstRegion->mipOffset : 0;
        uint32_t mNum = dstRegion ? 1 : textureHandle->mipNum;
        uint32_t lStart = dstRegion ? dstRegion->layerOffset : 0;
        uint32_t lNum = dstRegion ? 1 : textureHandle->layerNum;

        // capture states of the relevant region of the texture
        RfxVector<RfxResourceState> capturedStates;
        capturedStates.reserve(lNum * mNum);
        for (uint32_t l = 0; l < lNum; ++l) {
            for (uint32_t m = 0; m < mNum; ++m) {
                capturedStates.push_back(textureHandle->state->Get(mStart + m, lStart + l));
            }
        }

        auto preBarrier = [=](nri::CommandBuffer& cb) {
            nri::BarrierDesc bd = {};
            RfxVector<nri::TextureBarrierDesc> tbds;

            size_t idx = 0;
            for (uint32_t l = 0; l < lNum; ++l) {
                for (uint32_t m = 0; m < mNum; ++m) {
                    uint32_t absLayer = lStart + l;
                    uint32_t absMip = mStart + m;

                    RfxResourceState oldSt = capturedStates[idx++];

                    if (oldSt == RFX_STATE_COPY_DST)
                        continue;

                    nri::AccessBits acc;
                    nri::Layout lay;
                    nri::StageBits stg;
                    GetNRIState(oldSt, acc, lay, stg);

                    nri::TextureBarrierDesc& d = tbds.emplace_back();
                    d.texture = dstTexture;
                    d.before = { acc, lay, stg };
                    d.after = { nri::AccessBits::COPY_DESTINATION, nri::Layout::COPY_DESTINATION, nri::StageBits::COPY };
                    d.mipOffset = (nri::Dim_t)absMip;
                    d.mipNum = 1;
                    d.layerOffset = (nri::Dim_t)absLayer;
                    d.layerNum = 1;
                    d.planes = nri::PlaneBits::ALL;
                }
            }
            if (!tbds.empty()) {
                bd.textures = tbds.data();
                bd.textureNum = (uint32_t)tbds.size();
                CORE.NRI.CmdBarrier(cb, bd);
            }
        };

        auto postBarrier = [=](nri::CommandBuffer& cb) {
            nri::BarrierDesc bd = {};
            RfxVector<nri::TextureBarrierDesc> tbds;
            nri::AccessBits finAcc;
            nri::Layout finLay;
            nri::StageBits finStg;
            GetNRIState(finalState, finAcc, finLay, finStg);

            for (uint32_t l = 0; l < lNum; ++l) {
                for (uint32_t m = 0; m < mNum; ++m) {
                    uint32_t absLayer = lStart + l;
                    uint32_t absMip = mStart + m;
                    nri::TextureBarrierDesc& d = tbds.emplace_back();
                    d.texture = dstTexture;
                    d.before = { nri::AccessBits::COPY_DESTINATION, nri::Layout::COPY_DESTINATION, nri::StageBits::COPY };
                    d.after = { finAcc, finLay, finStg };
                    d.mipOffset = (nri::Dim_t)absMip;
                    d.mipNum = 1;
                    d.layerOffset = (nri::Dim_t)absLayer;
                    d.layerNum = 1;
                    d.planes = nri::PlaneBits::ALL;
                }
            }
            if (!tbds.empty()) {
                bd.textures = tbds.data();
                bd.textureNum = (uint32_t)tbds.size();
                CORE.NRI.CmdBarrier(cb, bd);
            }
        };

        // update shared state
        for (uint32_t l = 0; l < lNum; ++l) {
            for (uint32_t m = 0; m < mNum; ++m) {
                textureHandle->state->Set(mStart + m, lStart + l, finalState);
            }
        }

        if (cmd) {
            preBarrier(*cmd->nriCmd);
            CORE.NRI.CmdCopyStreamedData(*cmd->nriCmd, *CORE.NRIStreamer);
            postBarrier(*cmd->nriCmd);
        } else {
            CORE.PendingPreBarriers.push_back(preBarrier);
            CORE.PendingPostBarriers.push_back(postBarrier);
        }
    }
}

static uint32_t AllocTextureSlot() {
    uint32_t id;
    if (!CORE.Bindless.freeTextureSlots.empty()) {
        id = CORE.Bindless.freeTextureSlots.back();
        CORE.Bindless.freeTextureSlots.pop_back();
    } else {
        RFX_ASSERT(CORE.Bindless.textureHighWaterMark < RFX_MAX_BINDLESS_TEXTURES);
        id = CORE.Bindless.textureHighWaterMark++;
    }
    return id;
}

static void FreeTextureSlot(uint32_t id) {
    CORE.Bindless.freeTextureSlots.push_back(id);
}

static uint32_t AllocBufferSlot() {
    uint32_t id;
    if (!CORE.Bindless.freeBufferSlots.empty()) {
        id = CORE.Bindless.freeBufferSlots.back();
        CORE.Bindless.freeBufferSlots.pop_back();
    } else {
        RFX_ASSERT(CORE.Bindless.bufferHighWaterMark < RFX_MAX_BINDLESS_TEXTURES);
        id = CORE.Bindless.bufferHighWaterMark++;
    }
    return id;
}

static void SubmitImmediate(std::function<void(nri::CommandBuffer&)> work) {
    nri::CommandAllocator* allocator;
    nri::CommandBuffer* cmd;
    CORE.NRI.CreateCommandAllocator(*CORE.NRIGraphicsQueue, allocator);
    CORE.NRI.CreateCommandBuffer(*allocator, cmd);
    CORE.NRI.BeginCommandBuffer(*cmd, nullptr);

    work(*cmd);

    CORE.NRI.EndCommandBuffer(*cmd);
    nri::QueueSubmitDesc submit = {};
    submit.commandBuffers = &cmd;
    submit.commandBufferNum = 1;
    CORE.NRI.QueueSubmit(*CORE.NRIGraphicsQueue, submit);
    CORE.NRI.QueueWaitIdle(CORE.NRIGraphicsQueue);

    CORE.NRI.DestroyCommandBuffer(cmd);
    CORE.NRI.DestroyCommandAllocator(allocator);
}

static RfxFormat ToRfxFormat(nri::Format fmt) {
    switch (fmt) {
    case nri::Format::RGBA8_UNORM: return RFX_FORMAT_RGBA8_UNORM;
    case nri::Format::RGBA8_SRGB: return RFX_FORMAT_RGBA8_SRGB;
    case nri::Format::BGRA8_UNORM: return RFX_FORMAT_BGRA8_UNORM;
    case nri::Format::BGRA8_SRGB: return RFX_FORMAT_BGRA8_SRGB;
    case nri::Format::RGBA32_SFLOAT: return RFX_FORMAT_RGBA32_FLOAT;
    case nri::Format::RGB32_SFLOAT: return RFX_FORMAT_RGB32_FLOAT;
    case nri::Format::RG32_SFLOAT: return RFX_FORMAT_RG32_FLOAT;
    case nri::Format::D32_SFLOAT: return RFX_FORMAT_D32_FLOAT;
    case nri::Format::D24_UNORM_S8_UINT: return RFX_FORMAT_D24_UNORM_S8_UINT;
    case nri::Format::R32_SFLOAT: return RFX_FORMAT_R32_FLOAT;
    case nri::Format::RGBA16_SFLOAT: return RFX_FORMAT_RGBA16_FLOAT;
    default: return RFX_FORMAT_UNKNOWN;
    }
}

static nri::Format ToNRIFormat(RfxFormat fmt) {
    switch (fmt) {
    case RFX_FORMAT_RGBA8_UNORM: return nri::Format::RGBA8_UNORM;
    case RFX_FORMAT_RGBA8_SRGB: return nri::Format::RGBA8_SRGB;
    case RFX_FORMAT_BGRA8_UNORM: return nri::Format::BGRA8_UNORM;
    case RFX_FORMAT_BGRA8_SRGB: return nri::Format::BGRA8_SRGB;
    case RFX_FORMAT_RGBA32_FLOAT: return nri::Format::RGBA32_SFLOAT;
    case RFX_FORMAT_RGB32_FLOAT: return nri::Format::RGB32_SFLOAT;
    case RFX_FORMAT_RG32_FLOAT: return nri::Format::RG32_SFLOAT;
    case RFX_FORMAT_D32_FLOAT: return nri::Format::D32_SFLOAT;
    case RFX_FORMAT_D24_UNORM_S8_UINT: return nri::Format::D24_UNORM_S8_UINT;
    case RFX_FORMAT_R32_FLOAT: return nri::Format::R32_SFLOAT;
    case RFX_FORMAT_RGBA16_FLOAT: return nri::Format::RGBA16_SFLOAT;
    case RFX_FORMAT_R8_UINT: return nri::Format::R8_UINT;
    case RFX_FORMAT_R8_SINT: return nri::Format::R8_SINT;
    case RFX_FORMAT_RG8_UINT: return nri::Format::RG8_UINT;
    case RFX_FORMAT_RG8_SINT: return nri::Format::RG8_SINT;
    case RFX_FORMAT_RGBA8_UINT: return nri::Format::RGBA8_UINT;
    case RFX_FORMAT_RGBA8_SINT: return nri::Format::RGBA8_SINT;
    case RFX_FORMAT_R16_UINT: return nri::Format::R16_UINT;
    case RFX_FORMAT_R16_SINT: return nri::Format::R16_SINT;
    case RFX_FORMAT_R16_UNORM: return nri::Format::R16_UNORM;
    case RFX_FORMAT_R16_SNORM: return nri::Format::R16_SNORM;
    case RFX_FORMAT_RG16_UINT: return nri::Format::RG16_UINT;
    case RFX_FORMAT_RG16_SINT: return nri::Format::RG16_SINT;
    case RFX_FORMAT_RG16_UNORM: return nri::Format::RG16_UNORM;
    case RFX_FORMAT_RG16_SNORM: return nri::Format::RG16_SNORM;
    case RFX_FORMAT_RGBA16_UINT: return nri::Format::RGBA16_UINT;
    case RFX_FORMAT_RGBA16_SINT: return nri::Format::RGBA16_SINT;
    case RFX_FORMAT_RGBA16_UNORM: return nri::Format::RGBA16_UNORM;
    case RFX_FORMAT_RGBA16_SNORM: return nri::Format::RGBA16_SNORM;
    case RFX_FORMAT_R32_UINT: return nri::Format::R32_UINT;
    case RFX_FORMAT_R32_SINT: return nri::Format::R32_SINT;
    case RFX_FORMAT_RG32_UINT: return nri::Format::RG32_UINT;
    case RFX_FORMAT_RG32_SINT: return nri::Format::RG32_SINT;
    case RFX_FORMAT_RGB32_UINT: return nri::Format::RGB32_UINT;
    case RFX_FORMAT_RGB32_SINT: return nri::Format::RGB32_SINT;
    case RFX_FORMAT_RGBA32_UINT: return nri::Format::RGBA32_UINT;
    case RFX_FORMAT_RGBA32_SINT: return nri::Format::RGBA32_SINT;
    case RFX_FORMAT_R10_G10_B10_A2_UNORM: return nri::Format::R10_G10_B10_A2_UNORM;
    case RFX_FORMAT_R10_G10_B10_A2_UINT: return nri::Format::R10_G10_B10_A2_UINT;
    case RFX_FORMAT_R11_G11_B10_UFLOAT: return nri::Format::R11_G11_B10_UFLOAT;
    case RFX_FORMAT_R9_G9_B9_E5_UFLOAT: return nri::Format::R9_G9_B9_E5_UFLOAT;
    case RFX_FORMAT_BC1_RGBA_UNORM: return nri::Format::BC1_RGBA_UNORM;
    case RFX_FORMAT_BC1_RGBA_SRGB: return nri::Format::BC1_RGBA_SRGB;
    case RFX_FORMAT_BC2_RGBA_UNORM: return nri::Format::BC2_RGBA_UNORM;
    case RFX_FORMAT_BC2_RGBA_SRGB: return nri::Format::BC2_RGBA_SRGB;
    case RFX_FORMAT_BC3_RGBA_UNORM: return nri::Format::BC3_RGBA_UNORM;
    case RFX_FORMAT_BC3_RGBA_SRGB: return nri::Format::BC3_RGBA_SRGB;
    case RFX_FORMAT_BC4_R_UNORM: return nri::Format::BC4_R_UNORM;
    case RFX_FORMAT_BC4_R_SNORM: return nri::Format::BC4_R_SNORM;
    case RFX_FORMAT_BC5_RG_UNORM: return nri::Format::BC5_RG_UNORM;
    case RFX_FORMAT_BC5_RG_SNORM: return nri::Format::BC5_RG_SNORM;
    case RFX_FORMAT_BC6H_RGB_UFLOAT: return nri::Format::BC6H_RGB_UFLOAT;
    case RFX_FORMAT_BC6H_RGB_SFLOAT: return nri::Format::BC6H_RGB_SFLOAT;
    case RFX_FORMAT_BC7_RGBA_UNORM: return nri::Format::BC7_RGBA_UNORM;
    case RFX_FORMAT_BC7_RGBA_SRGB: return nri::Format::BC7_RGBA_SRGB;
    case RFX_FORMAT_D16_UNORM: return nri::Format::D16_UNORM;
    case RFX_FORMAT_D32_FLOAT_S8_UINT_X24: return nri::Format::D32_SFLOAT_S8_UINT_X24;
    default: return nri::Format::UNKNOWN;
    }
}

static nri::BlendFactor ToNRIBlendFactor(RfxBlendFactor f) {
    switch (f) {
    case RFX_BLEND_FACTOR_ZERO: return nri::BlendFactor::ZERO;
    case RFX_BLEND_FACTOR_ONE: return nri::BlendFactor::ONE;
    case RFX_BLEND_FACTOR_SRC_COLOR: return nri::BlendFactor::SRC_COLOR;
    case RFX_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return nri::BlendFactor::ONE_MINUS_SRC_COLOR;
    case RFX_BLEND_FACTOR_DST_COLOR: return nri::BlendFactor::DST_COLOR;
    case RFX_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return nri::BlendFactor::ONE_MINUS_DST_COLOR;
    case RFX_BLEND_FACTOR_SRC_ALPHA: return nri::BlendFactor::SRC_ALPHA;
    case RFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return nri::BlendFactor::ONE_MINUS_SRC_ALPHA;
    case RFX_BLEND_FACTOR_DST_ALPHA: return nri::BlendFactor::DST_ALPHA;
    case RFX_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return nri::BlendFactor::ONE_MINUS_DST_ALPHA;
    case RFX_BLEND_FACTOR_CONSTANT_COLOR: return nri::BlendFactor::CONSTANT_COLOR;
    case RFX_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return nri::BlendFactor::ONE_MINUS_CONSTANT_COLOR;
    case RFX_BLEND_FACTOR_CONSTANT_ALPHA: return nri::BlendFactor::CONSTANT_ALPHA;
    case RFX_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return nri::BlendFactor::ONE_MINUS_CONSTANT_ALPHA;
    case RFX_BLEND_FACTOR_SRC_ALPHA_SATURATE: return nri::BlendFactor::SRC_ALPHA_SATURATE;
    case RFX_BLEND_FACTOR_SRC1_COLOR: return nri::BlendFactor::SRC1_COLOR;
    case RFX_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR: return nri::BlendFactor::ONE_MINUS_SRC1_COLOR;
    case RFX_BLEND_FACTOR_SRC1_ALPHA: return nri::BlendFactor::SRC1_ALPHA;
    case RFX_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return nri::BlendFactor::ONE_MINUS_SRC1_ALPHA;
    default: return nri::BlendFactor::ONE;
    }
}

static nri::BlendOp ToNRIBlendOp(RfxBlendOp op) {
    switch (op) {
    case RFX_BLEND_OP_ADD: return nri::BlendOp::ADD;
    case RFX_BLEND_OP_SUBTRACT: return nri::BlendOp::SUBTRACT;
    case RFX_BLEND_OP_REVERSE_SUBTRACT: return nri::BlendOp::REVERSE_SUBTRACT;
    case RFX_BLEND_OP_MIN: return nri::BlendOp::MIN;
    case RFX_BLEND_OP_MAX: return nri::BlendOp::MAX;
    default: return nri::BlendOp::ADD;
    }
}

static nri::Topology ToNRITopology(RfxTopology topology) {
    switch (topology) {
    case RFX_TOPOLOGY_POINT_LIST: return nri::Topology::POINT_LIST;
    case RFX_TOPOLOGY_LINE_LIST: return nri::Topology::LINE_LIST;
    case RFX_TOPOLOGY_LINE_STRIP: return nri::Topology::LINE_STRIP;
    case RFX_TOPOLOGY_TRIANGLE_LIST: return nri::Topology::TRIANGLE_LIST;
    case RFX_TOPOLOGY_TRIANGLE_STRIP: return nri::Topology::TRIANGLE_STRIP;
    case RFX_TOPOLOGY_LINE_LIST_WITH_ADJACENCY: return nri::Topology::LINE_LIST_WITH_ADJACENCY;
    case RFX_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY: return nri::Topology::LINE_STRIP_WITH_ADJACENCY;
    case RFX_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY: return nri::Topology::TRIANGLE_LIST_WITH_ADJACENCY;
    case RFX_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY: return nri::Topology::TRIANGLE_STRIP_WITH_ADJACENCY;
    case RFX_TOPOLOGY_PATCH_LIST: return nri::Topology::PATCH_LIST;
    default: return nri::Topology::TRIANGLE_LIST;
    }
}

static nri::AccessBits ToNRIAccessBits(RfxBufferUsageFlags usage) {
    nri::AccessBits access = nri::AccessBits::NONE;
    if (usage & RFX_USAGE_SHADER_RESOURCE)
        access |= nri::AccessBits::SHADER_RESOURCE;
    if (usage & RFX_USAGE_SHADER_RESOURCE_STORAGE)
        access |= nri::AccessBits::SHADER_RESOURCE_STORAGE;
    if (usage & RFX_USAGE_VERTEX_BUFFER)
        access |= nri::AccessBits::VERTEX_BUFFER;
    if (usage & RFX_USAGE_INDEX_BUFFER)
        access |= nri::AccessBits::INDEX_BUFFER;
    if (usage & RFX_USAGE_CONSTANT_BUFFER)
        access |= nri::AccessBits::CONSTANT_BUFFER;
    if (usage & RFX_USAGE_ARGUMENT_BUFFER)
        access |= nri::AccessBits::ARGUMENT_BUFFER;
    if (usage & RFX_USAGE_SCRATCH_BUFFER)
        access |= nri::AccessBits::SCRATCH_BUFFER;
    if (usage & RFX_USAGE_SHADER_BINDING_TABLE)
        access |= nri::AccessBits::SHADER_BINDING_TABLE;
    if (usage & RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT)
        access |= nri::AccessBits::ACCELERATION_STRUCTURE_READ;
    if (usage & RFX_USAGE_MICROMAP_BUILD_INPUT)
        access |= nri::AccessBits::MICROMAP_READ;
    if (usage & RFX_USAGE_TRANSFER_SRC)
        access |= nri::AccessBits::COPY_SOURCE;
    if (usage & RFX_USAGE_TRANSFER_DST)
        access |= nri::AccessBits::COPY_DESTINATION;
    return access;
}

static nri::StageBits ToNRIStageBits(SlangStage stage) {
    switch (stage) {
    case SLANG_STAGE_VERTEX: return nri::StageBits::VERTEX_SHADER;
    case SLANG_STAGE_GEOMETRY: return nri::StageBits::GEOMETRY_SHADER;
    case SLANG_STAGE_FRAGMENT: return nri::StageBits::FRAGMENT_SHADER;
    case SLANG_STAGE_COMPUTE: return nri::StageBits::COMPUTE_SHADER;
    case SLANG_STAGE_RAY_GENERATION: return nri::StageBits::RAYGEN_SHADER;
    case SLANG_STAGE_INTERSECTION: return nri::StageBits::INTERSECTION_SHADER;
    case SLANG_STAGE_ANY_HIT: return nri::StageBits::ANY_HIT_SHADER;
    case SLANG_STAGE_CLOSEST_HIT: return nri::StageBits::CLOSEST_HIT_SHADER;
    case SLANG_STAGE_MISS: return nri::StageBits::MISS_SHADER;
    case SLANG_STAGE_CALLABLE: return nri::StageBits::CALLABLE_SHADER;
    case SLANG_STAGE_MESH: return nri::StageBits::MESH_SHADER;
    case SLANG_STAGE_AMPLIFICATION: return nri::StageBits::TASK_SHADER;
    default: return nri::StageBits::NONE;
    }
}

static nri::DescriptorType GetDescriptorType(slang::TypeLayoutReflection* typeLayout) {
    slang::TypeReflection::Kind kind = typeLayout->getKind();

    if (kind == slang::TypeReflection::Kind::SamplerState) {
        return nri::DescriptorType::SAMPLER;
    } else if (kind == slang::TypeReflection::Kind::ConstantBuffer) {
        return nri::DescriptorType::CONSTANT_BUFFER;
    } else if (kind == slang::TypeReflection::Kind::Resource) {
        SlangResourceShape shape = typeLayout->getResourceShape();
        SlangResourceAccess access = typeLayout->getResourceAccess();
        bool isUAV = (access == SLANG_RESOURCE_ACCESS_READ_WRITE || access == SLANG_RESOURCE_ACCESS_WRITE);

        switch (shape & SLANG_RESOURCE_BASE_SHAPE_MASK) {
        case SLANG_TEXTURE_2D:
        case SLANG_TEXTURE_CUBE:
        case SLANG_TEXTURE_1D:
        case SLANG_TEXTURE_3D: return isUAV ? nri::DescriptorType::STORAGE_TEXTURE : nri::DescriptorType::TEXTURE;
        case SLANG_STRUCTURED_BUFFER:
            return isUAV ? nri::DescriptorType::STORAGE_STRUCTURED_BUFFER : nri::DescriptorType::STRUCTURED_BUFFER;
        case SLANG_BYTE_ADDRESS_BUFFER: return isUAV ? nri::DescriptorType::STORAGE_BUFFER : nri::DescriptorType::BUFFER;
        case SLANG_ACCELERATION_STRUCTURE: return nri::DescriptorType::ACCELERATION_STRUCTURE;
        default: return nri::DescriptorType::TEXTURE;
        }
    }
    return nri::DescriptorType::TEXTURE;
}

//
// Barrier batcher
//

void BarrierBatcher::RequireState(RfxBuffer buffer, RfxResourceState state) {
    if (!buffer)
        return;

    if (buffer->currentState == state)
        return;

    nri::AccessBits nextAccess;
    nri::Layout nextLayout;
    nri::StageBits nextStage;
    GetNRIState(state, nextAccess, nextLayout, nextStage);

    bool found = false;
    for (auto& barrier : bufferBarriers) {
        if (barrier.buffer == buffer->buffer) {
            barrier.after = { nextAccess, nextStage };
            found = true;
            break;
        }
    }

    if (!found) {
        nri::BufferBarrierDesc& desc = bufferBarriers.emplace_back();
        desc.buffer = buffer->buffer;
        desc.before = { buffer->currentAccess, buffer->currentStage };
        desc.after = { nextAccess, nextStage };
    }

    buffer->currentState = state;
    buffer->currentAccess = nextAccess;
    buffer->currentStage = nextStage;
}

void BarrierBatcher::RequireState(RfxTexture texture, RfxResourceState nextState) {
    if (!texture || !texture->state)
        return;

    for (uint32_t l = 0; l < texture->layerNum; ++l) {
        uint32_t absLayer = texture->layerOffset + l;

        uint32_t currentStartMip = 0;
        uint32_t currentMipCount = 0;
        RfxResourceState batchOldState = RFX_STATE_UNDEFINED;
        bool inBatch = false;

        auto FlushBatch = [&](uint32_t layerIdx) {
            if (!inBatch)
                return;

            nri::AccessBits oldAccess, newAccess;
            nri::Layout oldLayout, newLayout;
            nri::StageBits oldStage, newStage;

            GetNRIState(batchOldState, oldAccess, oldLayout, oldStage);
            GetNRIState(nextState, newAccess, newLayout, newStage);

            nri::TextureBarrierDesc& desc = textureBarriers.emplace_back();
            desc.texture = texture->texture;
            desc.before = { oldAccess, oldLayout, oldStage };
            desc.after = { newAccess, newLayout, newStage };
            desc.mipOffset = (nri::Dim_t)currentStartMip;
            desc.mipNum = (nri::Dim_t)currentMipCount;
            desc.layerOffset = (nri::Dim_t)layerIdx;
            desc.layerNum = 1;
            desc.planes = nri::PlaneBits::ALL;

            for (uint32_t m = 0; m < currentMipCount; ++m) {
                texture->state->Set(currentStartMip + m, layerIdx, nextState);
            }

            inBatch = false;
            currentMipCount = 0;
        };

        for (uint32_t m = 0; m < texture->mipNum; ++m) {
            uint32_t absMip = texture->mipOffset + m;
            RfxResourceState currentSubState = texture->state->Get(absMip, absLayer);

            if (currentSubState == nextState) {
                FlushBatch(absLayer);
                continue;
            }

            if (!inBatch) {
                inBatch = true;
                currentStartMip = absMip;
                currentMipCount = 1;
                batchOldState = currentSubState;
            } else {
                if (currentSubState == batchOldState) {
                    currentMipCount++;
                } else {
                    FlushBatch(absLayer);
                    inBatch = true;
                    currentStartMip = absMip;
                    currentMipCount = 1;
                    batchOldState = currentSubState;
                }
            }
        }
        FlushBatch(absLayer);
    }
}

void BarrierBatcher::Flush(nri::CommandBuffer& cmd) {
    if (bufferBarriers.empty() && textureBarriers.empty() && globalBarriers.empty())
        return;

    nri::BarrierDesc desc = {};
    desc.bufferNum = (uint32_t)bufferBarriers.size();
    desc.buffers = bufferBarriers.data();
    desc.textureNum = (uint32_t)textureBarriers.size();
    desc.textures = textureBarriers.data();
    desc.globalNum = (uint32_t)globalBarriers.size();
    desc.globals = globalBarriers.data();

    CORE.NRI.CmdBarrier(cmd, desc);

    bufferBarriers.clear();
    textureBarriers.clear();
    globalBarriers.clear();
}

//
// Command list
//

void RfxCommandListImpl::PrepareForDraw() {
    if (currentVertexBuffer)
        barriers.RequireState(currentVertexBuffer, RFX_STATE_VERTEX_BUFFER);
    if (currentIndexBuffer)
        barriers.RequireState(currentIndexBuffer, RFX_STATE_INDEX_BUFFER);
}

void RfxCommandListImpl::BindDrawBuffers() {
    if (currentPipeline && currentPipeline->vertexStride > 0 && currentVertexBuffer) {
        if (currentVertexBuffer != lastBoundVertexBuffer) {
            nri::VertexBufferDesc vbd = { currentVertexBuffer->buffer, 0, currentPipeline->vertexStride };
            CORE.NRI.CmdSetVertexBuffers(*nriCmd, 0, &vbd, 1);
            lastBoundVertexBuffer = currentVertexBuffer;
        }
    }

    if (currentIndexBuffer) {
        if (currentIndexBuffer != lastBoundIndexBuffer) {
            CORE.NRI.CmdSetIndexBuffer(*nriCmd, *currentIndexBuffer->buffer, 0, currentIndexType);
            lastBoundIndexBuffer = currentIndexBuffer;
        }
    }
}

void RfxCommandListImpl::FlushBarriers() {
    if (!barriers.HasPending())
        return;

    if (isRendering) {
        // FIXME: this should not be legal
        // RFX_ASSERT(false && "TODO would break rp");
        CORE.NRI.CmdEndRendering(*nriCmd);
        barriers.Flush(*nriCmd);
        CORE.NRI.CmdBeginRendering(*nriCmd, currentRenderingDesc);

        // restore state
        CORE.NRI.CmdSetViewports(*nriCmd, &currentViewport, 1);
        if (scissorSet) {
            CORE.NRI.CmdSetScissors(*nriCmd, &currentScissor, 1);
        } else {
            nri::Rect r = { (int16_t)currentViewport.x, (int16_t)currentViewport.y, (nri::Dim_t)currentViewport.width,
                            (nri::Dim_t)currentViewport.height };
            CORE.NRI.CmdSetScissors(*nriCmd, &r, 1);
        }
    } else {
        barriers.Flush(*nriCmd);
    }
}

//
// Command list
//

RfxCommandList rfxGetCommandList() {
    uint32_t idx = CORE.FrameIndex % GetQueuedFrameNum();
    return &CORE.QueuedFrames[idx].wrapper;
}

static void RecreateSwapChain(int w, int h) {
    CORE.NRI.QueueWaitIdle(CORE.NRIGraphicsQueue);
    for (auto& s : CORE.SwapChainTextures) {
        CORE.NRI.DestroyFence(s.acquireSemaphore);
        CORE.NRI.DestroyFence(s.releaseSemaphore);
        CORE.NRI.DestroyDescriptor(s.colorAttachment);
    }
    CORE.SwapChainTextures.clear();
    if (CORE.NRISwapChain)
        CORE.NRI.DestroySwapChain(CORE.NRISwapChain);

    nri::SwapChainDesc scd = {};
    scd.window = CORE.NRIWindow;
    scd.queue = CORE.NRIGraphicsQueue;
    scd.format = nri::SwapChainFormat::BT709_G22_8BIT;
    scd.flags = (CORE.VsyncEnable ? nri::SwapChainBits::VSYNC : nri::SwapChainBits::NONE) | nri::SwapChainBits::ALLOW_TEARING;
    if (CORE.AllowLowLatency)
        scd.flags |= nri::SwapChainBits::ALLOW_LOW_LATENCY;
    scd.width = (uint16_t)w;
    scd.height = (uint16_t)h;
    scd.textureNum = 3;
    scd.queuedFrameNum = GetQueuedFrameNum();
    CORE.NRI.CreateSwapChain(*CORE.NRIDevice, scd, CORE.NRISwapChain);

    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled) {
        nri::LatencySleepMode mode = {};
        mode.lowLatencyMode = true;
        mode.lowLatencyBoost = CORE.LowLatencyBoost;
        mode.minIntervalUs = 0;
        CORE.NRI.SetLatencySleepMode(*CORE.NRISwapChain, mode);
    }

    uint32_t count;
    nri::Texture* const* textures = CORE.NRI.GetSwapChainTextures(*CORE.NRISwapChain, count);
    nri::Format fmt = CORE.NRI.GetTextureDesc(*textures[0]).format;

    for (uint32_t i = 0; i < count; ++i) {
        SwapChainTexture& s = CORE.SwapChainTextures.emplace_back();
        s.texture = textures[i];
        s.attachmentFormat = fmt;

        nri::Texture2DViewDesc vd = {};
        vd.texture = textures[i];
        vd.viewType = nri::Texture2DViewType::COLOR_ATTACHMENT;
        vd.format = fmt;

        NRI_CHECK(CORE.NRI.CreateTexture2DView(vd, s.colorAttachment));

        CORE.NRI.CreateFence(*CORE.NRIDevice, nri::SWAPCHAIN_SEMAPHORE, s.acquireSemaphore);
        CORE.NRI.CreateFence(*CORE.NRIDevice, nri::SWAPCHAIN_SEMAPHORE, s.releaseSemaphore);
    }
    CORE.SwapChainWidth = w;
    CORE.SwapChainHeight = h;
}

//
// Commands
//

void rfxCmdBeginRenderPass(
    RfxCommandList cmd, RfxTexture* colors, uint32_t colorCount, RfxTexture depth, RfxColor clearColor, uint32_t viewMask
) {
    if (cmd->isRendering)
        rfxCmdEndRenderPass(cmd);

    uint32_t width = 0;
    uint32_t height = 0;

    cmd->activeColorAttachments.clear();
    cmd->activeColorTextures.clear();

    for (uint32_t i = 0; i < colorCount; ++i) {
        RfxTexture tex = colors[i];
        if (!tex)
            continue;
        if (width == 0) {
            width = tex->width;
            height = tex->height;
        }

        cmd->barriers.RequireState(tex, RFX_STATE_RENDER_TARGET);
        cmd->activeColorTextures.push_back(tex);

        nri::AttachmentDesc& desc = cmd->activeColorAttachments.emplace_back();
        desc.descriptor = tex->descriptorAttachment;
        desc.loadOp = nri::LoadOp::CLEAR;
        desc.storeOp = nri::StoreOp::STORE;
        desc.clearValue.color.f = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
    }

    cmd->currentRenderingDesc = {};
    cmd->currentRenderingDesc.colors = cmd->activeColorAttachments.data();
    cmd->currentRenderingDesc.colorNum = (uint32_t)cmd->activeColorAttachments.size();
    cmd->currentRenderingDesc.viewMask = viewMask;

    if (depth) {
        if (width == 0) {
            width = depth->width;
            height = depth->height;
        }

        cmd->barriers.RequireState(depth, RFX_STATE_DEPTH_WRITE);
        cmd->activeDepthTexture = depth;

        cmd->currentRenderingDesc.depth.descriptor = depth->descriptorAttachment;
        cmd->currentRenderingDesc.depth.loadOp = nri::LoadOp::CLEAR;
        cmd->currentRenderingDesc.depth.storeOp = nri::StoreOp::STORE;
        cmd->currentRenderingDesc.depth.clearValue.depthStencil.depth = 1.0f;
        cmd->currentRenderingDesc.depth.clearValue.depthStencil.stencil = 0;
        if (HasStencil(depth->format)) {
            cmd->currentRenderingDesc.stencil = cmd->currentRenderingDesc.depth;
        }
    }

    cmd->barriers.Flush(*cmd->nriCmd);
    CORE.NRI.CmdBeginRendering(*cmd->nriCmd, cmd->currentRenderingDesc);
    cmd->isRendering = true;

    nri::Viewport vp = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f, false };
    cmd->currentViewport = vp;
    CORE.NRI.CmdSetViewports(*cmd->nriCmd, &vp, 1);

    nri::Rect r = { 0, 0, (nri::Dim_t)width, (nri::Dim_t)height };
    CORE.NRI.CmdSetScissors(*cmd->nriCmd, &r, 1);
    cmd->scissorSet = false;
}

void rfxCmdBeginSwapchainRenderPass(RfxCommandList cmd, RfxFormat depthStencilFormat, RfxColor clearColor) {
    if (cmd->isRendering)
        rfxCmdEndRenderPass(cmd);

    uint32_t width = CORE.SwapChainWidth;
    uint32_t height = CORE.SwapChainHeight;

    if (width == 0 || height == 0) {
        width = (uint32_t)CORE.FramebufferWidth;
        height = (uint32_t)CORE.FramebufferHeight;
    }

    int samples = CORE.SampleCount;

    if (samples > 1) {
        if (!CORE.MSAAColorBuffer.handle || CORE.MSAAColorBuffer.width != width || CORE.MSAAColorBuffer.height != height) {
            if (CORE.MSAAColorBuffer.handle)
                rfxDestroyTexture(CORE.MSAAColorBuffer.handle);
            CORE.MSAAColorBuffer.width = width;
            CORE.MSAAColorBuffer.height = height;
            RfxFormat scFormat = rfxGetSwapChainFormat();
            CORE.MSAAColorBuffer.handle = rfxCreateTexture(width, height, scFormat, samples, RFX_TEXTURE_USAGE_RENDER_TARGET, nullptr);
        }
    }

    bool useDepth = (depthStencilFormat != RFX_FORMAT_UNKNOWN);
    nri::Format requestedDepthFmt = useDepth ? ToNRIFormat(depthStencilFormat) : nri::Format::UNKNOWN;

    if (useDepth) {
        bool recreate = !CORE.DepthBuffer.handle;
        if (!recreate) {
            if (CORE.DepthBuffer.width != width || CORE.DepthBuffer.height != height ||
                CORE.DepthBuffer.handle->sampleCount != (uint32_t)samples || CORE.DepthBuffer.handle->format != requestedDepthFmt) {
                recreate = true;
            }
        }
        if (recreate) {
            if (CORE.DepthBuffer.handle)
                rfxDestroyTexture(CORE.DepthBuffer.handle);
            CORE.DepthBuffer.width = width;
            CORE.DepthBuffer.height = height;
            CORE.DepthBuffer.handle =
                rfxCreateTexture(width, height, depthStencilFormat, samples, RFX_TEXTURE_USAGE_DEPTH_STENCIL, nullptr);
        }
        cmd->barriers.RequireState(CORE.DepthBuffer.handle, RFX_STATE_DEPTH_WRITE);
    }

    RfxTexture colorTarget = nullptr;
    nri::Descriptor* resolveDstDescriptor = nullptr;

    if (samples > 1) {
        cmd->barriers.RequireState(CORE.MSAAColorBuffer.handle, RFX_STATE_RENDER_TARGET);
        cmd->barriers.RequireState(&CORE.SwapChainWrapper, RFX_STATE_RENDER_TARGET);
        colorTarget = CORE.MSAAColorBuffer.handle;
        resolveDstDescriptor = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].colorAttachment;
    } else {
        cmd->barriers.RequireState(&CORE.SwapChainWrapper, RFX_STATE_RENDER_TARGET);
        colorTarget = nullptr;
    }

    cmd->FlushBarriers();

    cmd->activeColorAttachments.clear();
    nri::AttachmentDesc& colorDesc = cmd->activeColorAttachments.emplace_back();

    if (samples > 1) {
        colorDesc.descriptor = colorTarget->descriptorAttachment;
        colorDesc.resolveDst = resolveDstDescriptor;
        colorDesc.resolveOp = nri::ResolveOp::AVERAGE;
        colorDesc.storeOp = nri::StoreOp::DISCARD;
    } else {
        colorDesc.descriptor = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].colorAttachment;
        colorDesc.resolveDst = nullptr;
        colorDesc.resolveOp = nri::ResolveOp::AVERAGE;
        colorDesc.storeOp = nri::StoreOp::STORE;
    }

    colorDesc.loadOp = nri::LoadOp::CLEAR;
    colorDesc.clearValue.color.f = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };

    cmd->currentRenderingDesc = {};
    cmd->currentRenderingDesc.colors = cmd->activeColorAttachments.data();
    cmd->currentRenderingDesc.colorNum = 1;

    if (useDepth && CORE.DepthBuffer.handle) {
        cmd->currentRenderingDesc.depth.descriptor = CORE.DepthBuffer.handle->descriptorAttachment;
        cmd->currentRenderingDesc.depth.loadOp = nri::LoadOp::CLEAR;
        cmd->currentRenderingDesc.depth.storeOp = nri::StoreOp::STORE;
        cmd->currentRenderingDesc.depth.clearValue.depthStencil.depth = 1.0f;
        cmd->currentRenderingDesc.depth.clearValue.depthStencil.stencil = 0;
        if (HasStencil(CORE.DepthBuffer.handle->format)) {
            cmd->currentRenderingDesc.stencil = cmd->currentRenderingDesc.depth;
        }
    }

    CORE.NRI.CmdBeginRendering(*cmd->nriCmd, cmd->currentRenderingDesc);
    cmd->isRendering = true;

    nri::Viewport vp = { 0.0f, 0.0f, (float)width, (float)height, 0.0f, 1.0f, false };
    cmd->currentViewport = vp;
    CORE.NRI.CmdSetViewports(*cmd->nriCmd, &vp, 1);
    nri::Rect r = { 0, 0, (nri::Dim_t)width, (nri::Dim_t)height };
    CORE.NRI.CmdSetScissors(*cmd->nriCmd, &r, 1);
    cmd->scissorSet = false;
}

void rfxCmdEndRenderPass(RfxCommandList cmd) {
    if (!cmd->isRendering)
        return;

    CORE.NRI.CmdEndRendering(*cmd->nriCmd);
    cmd->isRendering = false;

    cmd->barriers.Flush(*cmd->nriCmd);

    // cleanup
    cmd->activeColorTextures.clear();
    cmd->activeDepthTexture = nullptr;
}

void rfxCmdClear(RfxCommandList cmd, RfxColor color) {
    if (!cmd->isRendering)
        return;

    RfxVector<nri::ClearAttachmentDesc> clears;
    for (uint32_t i = 0; i < cmd->currentRenderingDesc.colorNum; ++i) {
        nri::ClearAttachmentDesc clr = {};
        clr.planes = nri::PlaneBits::COLOR;
        clr.colorAttachmentIndex = (uint8_t)i;
        clr.value.color.f = { color.r, color.g, color.b, color.a };
        clears.push_back(clr);
    }

    if (cmd->currentRenderingDesc.depth.descriptor) {
        nri::ClearAttachmentDesc dclr = {};
        dclr.planes = nri::PlaneBits::DEPTH;

        if (cmd->currentRenderingDesc.stencil.descriptor) {
            dclr.planes |= nri::PlaneBits::STENCIL;
        }

        dclr.value.depthStencil.depth = 1.0f;
        dclr.value.depthStencil.stencil = 0;
        clears.push_back(dclr);
    }

    nri::Rect rect = { (int16_t)cmd->currentViewport.x, (int16_t)cmd->currentViewport.y, (nri::Dim_t)cmd->currentViewport.width,
                       (nri::Dim_t)cmd->currentViewport.height };

    CORE.NRI.CmdClearAttachments(*cmd->nriCmd, clears.data(), (uint32_t)clears.size(), &rect, 1);
}

void rfxCmdBindPipeline(RfxCommandList cmd, RfxPipeline pipeline) {
    cmd->currentPipeline = (RfxPipelineImpl*)pipeline;
    CORE.NRI.CmdSetPipelineLayout(*cmd->nriCmd, pipeline->bindPoint, *cmd->currentPipeline->shader->pipelineLayout);
    CORE.NRI.CmdSetPipeline(*cmd->nriCmd, *cmd->currentPipeline->pipeline);

    nri::SetDescriptorSetDesc bindlessSet = {};
    bindlessSet.setIndex = cmd->currentPipeline->shader->bindlessSetIndex;
    bindlessSet.descriptorSet = CORE.Bindless.globalDescriptorSet;
    bindlessSet.bindPoint = pipeline->bindPoint;
    CORE.NRI.CmdSetDescriptorSet(*cmd->nriCmd, bindlessSet);
}

void rfxCmdSetScissor(RfxCommandList cmd, int x, int y, int width, int height) {
    cmd->currentScissor = { (int16_t)x, (int16_t)y, (nri::Dim_t)width, (nri::Dim_t)height };
    cmd->scissorSet = true;
    if (cmd->isRendering)
        CORE.NRI.CmdSetScissors(*cmd->nriCmd, &cmd->currentScissor, 1);
}

void rfxCmdSetBlendConstants(RfxCommandList cmd, RfxColor color) {
    nri::Color32f c = { color.r, color.g, color.b, color.a };
    CORE.NRI.CmdSetBlendConstants(*cmd->nriCmd, c);
}

void rfxCmdBindVertexBuffer(RfxCommandList cmd, RfxBuffer buffer) {
    cmd->currentVertexBuffer = buffer;
}

void rfxCmdBindIndexBuffer(RfxCommandList cmd, RfxBuffer buffer, RfxIndexType indexType) {
    cmd->currentIndexBuffer = buffer;
    cmd->currentIndexType = (indexType == RFX_INDEX_UINT32) ? nri::IndexType::UINT32 : nri::IndexType::UINT16;
}

void rfxCmdPushConstants(RfxCommandList cmd, const void* data, size_t size) {
    if (!cmd->currentPipeline)
        return;
    nri::SetRootConstantsDesc desc = {};
    desc.rootConstantIndex = 0;
    desc.data = data;
    desc.size = (uint32_t)size;
    desc.bindPoint = cmd->currentPipeline->bindPoint;
    CORE.NRI.CmdSetRootConstants(*cmd->nriCmd, desc);
}

void rfxCmdDraw(RfxCommandList cmd, uint32_t vc, uint32_t ic) {
    cmd->PrepareForDraw();
    cmd->FlushBarriers();
    cmd->BindDrawBuffers();

    nri::DrawDesc d = { vc, ic, 0, 0 };
    CORE.NRI.CmdDraw(*cmd->nriCmd, d);
}

void rfxCmdDrawIndexed(RfxCommandList cmd, uint32_t ic, uint32_t instanceCount) {
    cmd->PrepareForDraw();
    cmd->FlushBarriers();
    cmd->BindDrawBuffers();

    nri::DrawIndexedDesc d = { ic, instanceCount, 0, 0, 0 };
    CORE.NRI.CmdDrawIndexed(*cmd->nriCmd, d);
}

void rfxCmdDispatch(RfxCommandList cmd, uint32_t x, uint32_t y, uint32_t z) {
    MustTransition(cmd);
    cmd->FlushBarriers();

    nri::DispatchDesc d = { x, y, z };
    CORE.NRI.CmdDispatch(*cmd->nriCmd, d);
}

void rfxCmdDrawIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset, uint32_t drawCount, uint32_t stride) {
    cmd->PrepareForDraw();
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();
    cmd->BindDrawBuffers();

    CORE.NRI.CmdDrawIndirect(*cmd->nriCmd, *buffer->buffer, offset, drawCount, stride, nullptr, 0);
}

void rfxCmdDrawIndexedIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset, uint32_t drawCount, uint32_t stride) {
    cmd->PrepareForDraw();
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();
    cmd->BindDrawBuffers();

    CORE.NRI.CmdDrawIndexedIndirect(*cmd->nriCmd, *buffer->buffer, offset, drawCount, stride, nullptr, 0);
}

void rfxCmdDispatchIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset) {
    MustTransition(cmd);
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();

    CORE.NRI.CmdDispatchIndirect(*cmd->nriCmd, *buffer->buffer, offset);
}

void rfxCmdDrawMeshTasks(RfxCommandList cmd, uint32_t x, uint32_t y, uint32_t z) {
    cmd->FlushBarriers();

    nri::DrawMeshTasksDesc d = { x, y, z };
    CORE.NRI.CmdDrawMeshTasks(*cmd->nriCmd, d);
}

void rfxCmdDrawMeshTasksIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset, uint32_t drawCount, uint32_t stride) {
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();

    CORE.NRI.CmdDrawMeshTasksIndirect(*cmd->nriCmd, *buffer->buffer, offset, drawCount, stride, nullptr, 0);
}

void rfxCmdDrawIndirectCount(
    RfxCommandList cmd, RfxBuffer buffer, size_t offset, RfxBuffer countBuffer, size_t countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride
) {
    cmd->PrepareForDraw();
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    rfxCmdTransitionBuffer(cmd, countBuffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();
    cmd->BindDrawBuffers();

    CORE.NRI.CmdDrawIndirect(*cmd->nriCmd, *buffer->buffer, offset, maxDrawCount, stride, countBuffer->buffer, countBufferOffset);
}

void rfxCmdDrawIndexedIndirectCount(
    RfxCommandList cmd, RfxBuffer buffer, size_t offset, RfxBuffer countBuffer, size_t countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride
) {
    cmd->PrepareForDraw();
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    rfxCmdTransitionBuffer(cmd, countBuffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();
    cmd->BindDrawBuffers();

    CORE.NRI.CmdDrawIndexedIndirect(*cmd->nriCmd, *buffer->buffer, offset, maxDrawCount, stride, countBuffer->buffer, countBufferOffset);
}

void rfxCmdDrawMeshTasksIndirectCount(
    RfxCommandList cmd, RfxBuffer buffer, size_t offset, RfxBuffer countBuffer, size_t countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride
) {
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_INDIRECT_ARGUMENT);
    rfxCmdTransitionBuffer(cmd, countBuffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();

    CORE.NRI.CmdDrawMeshTasksIndirect(*cmd->nriCmd, *buffer->buffer, offset, maxDrawCount, stride, countBuffer->buffer, countBufferOffset);
}

void rfxCmdCopyBuffer(RfxCommandList cmd, RfxBuffer src, size_t srcOffset, RfxBuffer dst, size_t dstOffset, size_t size) {
    MustTransition(cmd);
    rfxCmdTransitionBuffer(cmd, src, RFX_STATE_COPY_SRC);
    rfxCmdTransitionBuffer(cmd, dst, RFX_STATE_COPY_DST);
    cmd->FlushBarriers();
    CORE.NRI.CmdCopyBuffer(*cmd->nriCmd, *dst->buffer, dstOffset, *src->buffer, srcOffset, size);
}

void rfxCmdCopyTexture(RfxCommandList cmd, RfxTexture src, RfxTexture dst) {
    MustTransition(cmd);
    rfxCmdTransitionTexture(cmd, src, RFX_STATE_COPY_SRC);
    rfxCmdTransitionTexture(cmd, dst, RFX_STATE_COPY_DST);
    cmd->FlushBarriers();
    CORE.NRI.CmdCopyTexture(*cmd->nriCmd, *dst->texture, nullptr, *src->texture, nullptr);
}

//
// Resource creation
//

template <typename T, typename BindDesc, typename GetDescFunc, typename BindFunc>
static void AllocateAndBind(T* resource, nri::MemoryLocation loc, nri::Memory*& outMemory, GetDescFunc getDesc, BindFunc bind) {
    nri::MemoryDesc memReq = {};
    getDesc(*resource, loc, memReq);

    nri::AllocateMemoryDesc allocDesc = {};
    allocDesc.size = memReq.size;
    allocDesc.type = memReq.type;
    allocDesc.priority = 0.0f;
    allocDesc.vma = { true, 0 };

    // TODO: not sure if this correct for anything but d3d12
    if (memReq.alignment > 65536) {
        allocDesc.allowMultisampleTextures = true;
    }

    NRI_CHECK(CORE.NRI.AllocateMemory(*CORE.NRIDevice, allocDesc, outMemory));

    BindDesc bindDesc = {};
    bindDesc.memory = outMemory;
    bindDesc.offset = 0;
    if constexpr (std::is_same_v<T, nri::Buffer>) {
        bindDesc.buffer = resource;
    } else if constexpr (std::is_same_v<T, nri::Texture>) {
        bindDesc.texture = resource;
    } else if constexpr (std::is_same_v<T, nri::AccelerationStructure>) {
        bindDesc.accelerationStructure = resource;
    } else if constexpr (std::is_same_v<T, nri::Micromap>) {
        bindDesc.micromap = resource;
    }

    NRI_CHECK(bind(&bindDesc, 1));
}

RfxBuffer rfxCreateBuffer(size_t size, size_t stride, RfxBufferUsageFlags usage, RfxMemoryType memType, const void* initialData) {
    RfxBufferImpl* impl = RfxNew<RfxBufferImpl>(nullptr, nullptr, nullptr, nullptr, (uint64_t)size, (uint32_t)stride, 0);
    impl->bindlessIndex = AllocBufferSlot();

    nri::BufferDesc bd = {};
    bd.size = size;
    bd.structureStride = 4; // allows "typed", "byte address (raw)" and "structured" views
    bd.usage = nri::BufferUsageBits::SHADER_RESOURCE;

    if (usage & RFX_USAGE_VERTEX_BUFFER)
        bd.usage |= nri::BufferUsageBits::VERTEX_BUFFER;
    if (usage & RFX_USAGE_INDEX_BUFFER)
        bd.usage |= nri::BufferUsageBits::INDEX_BUFFER;
    if (usage & RFX_USAGE_CONSTANT_BUFFER)
        bd.usage |= nri::BufferUsageBits::CONSTANT_BUFFER;
    if (usage & RFX_USAGE_ARGUMENT_BUFFER)
        bd.usage |= nri::BufferUsageBits::ARGUMENT_BUFFER;
    if (usage & RFX_USAGE_SCRATCH_BUFFER)
        bd.usage |= nri::BufferUsageBits::SCRATCH_BUFFER;
    if (usage & RFX_USAGE_SHADER_BINDING_TABLE)
        bd.usage |= nri::BufferUsageBits::SHADER_BINDING_TABLE;
    if (usage & RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT)
        bd.usage |= nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT;
    if (usage & RFX_USAGE_TRANSFER_SRC)
        bd.usage |= nri::BufferUsageBits::SHADER_RESOURCE;
    if (usage & RFX_USAGE_SHADER_RESOURCE_STORAGE)
        bd.usage |= nri::BufferUsageBits::SHADER_RESOURCE_STORAGE;
    if (usage & RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT) {
        bd.usage |= nri::BufferUsageBits::ACCELERATION_STRUCTURE_BUILD_INPUT;
        bd.usage |= nri::BufferUsageBits::SHADER_RESOURCE;
    }

    NRI_CHECK(CORE.NRI.CreateBuffer(*CORE.NRIDevice, bd, impl->buffer));

    nri::MemoryLocation loc = (memType == RFX_MEM_CPU_TO_GPU)
                                  ? nri::MemoryLocation::HOST_UPLOAD
                                  : (memType == RFX_MEM_GPU_TO_CPU ? nri::MemoryLocation::HOST_READBACK : nri::MemoryLocation::DEVICE);

    AllocateAndBind<nri::Buffer, nri::BindBufferMemoryDesc>(
        impl->buffer, loc, impl->memory,
        [&](nri::Buffer& b, nri::MemoryLocation l, nri::MemoryDesc& d) { CORE.NRI.GetBufferMemoryDesc(b, l, d); },
        [&](const nri::BindBufferMemoryDesc* d, uint32_t n) { return CORE.NRI.BindBufferMemory(d, n); }
    );

    if (usage & RFX_USAGE_SHADER_RESOURCE_STORAGE) {
        nri::BufferViewDesc uavDesc = {};
        uavDesc.buffer = impl->buffer;
        uavDesc.viewType = nri::BufferViewType::SHADER_RESOURCE_STORAGE;
        uavDesc.format = nri::Format::UNKNOWN;
        uavDesc.size = size;
        uavDesc.structureStride = 0;
        NRI_CHECK(CORE.NRI.CreateBufferView(uavDesc, impl->descriptorUAV));

        nri::UpdateDescriptorRangeDesc uavUpdate = {};
        uavUpdate.descriptorSet = CORE.Bindless.globalDescriptorSet;
        uavUpdate.rangeIndex = 3;
        uavUpdate.baseDescriptor = impl->bindlessIndex;
        uavUpdate.descriptorNum = 1;
        uavUpdate.descriptors = &impl->descriptorUAV;
        CORE.NRI.UpdateDescriptorRanges(&uavUpdate, 1);
    }

    nri::BufferViewDesc vd = {};
    vd.buffer = impl->buffer;
    vd.viewType = nri::BufferViewType::SHADER_RESOURCE;
    vd.format = nri::Format::UNKNOWN;
    vd.size = size;
    vd.structureStride = 0;
    NRI_CHECK(CORE.NRI.CreateBufferView(vd, impl->descriptorSRV));

    nri::UpdateDescriptorRangeDesc update = {};
    update.descriptorSet = CORE.Bindless.globalDescriptorSet;
    update.rangeIndex = 2;
    update.baseDescriptor = impl->bindlessIndex;
    update.descriptorNum = 1;
    update.descriptors = &impl->descriptorSRV;
    CORE.NRI.UpdateDescriptorRanges(&update, 1);

    // init
    if (initialData) {
        if (memType == RFX_MEM_GPU_ONLY) {
            // use staging buffer
            UploadToResource(nullptr, impl->buffer, 0, nullptr, nullptr, initialData, size, 0, 0, RFX_STATE_SHADER_READ, impl, nullptr);
        } else {
            // map now
            void* p = CORE.NRI.MapBuffer(*impl->buffer, 0, size);
            memcpy(p, initialData, size);
            CORE.NRI.UnmapBuffer(*impl->buffer);

            impl->currentAccess = nri::AccessBits::SHADER_RESOURCE;
            impl->currentStage = nri::StageBits::ALL;
            impl->currentState = RFX_STATE_SHADER_READ;
        }
    } else {
        impl->currentAccess = nri::AccessBits::SHADER_RESOURCE;
        impl->currentStage = nri::StageBits::ALL;
        impl->currentState = RFX_STATE_SHADER_READ;
    }

    return impl;
}

uint32_t rfxGetBufferId(RfxBuffer buffer) {
    return buffer ? buffer->bindlessIndex : 0;
}

void rfxDestroyBuffer(RfxBuffer buffer) {
    if (!buffer)
        return;
    RfxBufferImpl* ptr = buffer;
    rfxDeferDestruction([=]() {
        if (ptr->descriptorSRV)
            CORE.NRI.DestroyDescriptor(ptr->descriptorSRV);
        if (ptr->descriptorUAV)
            CORE.NRI.DestroyDescriptor(ptr->descriptorUAV);
        CORE.NRI.DestroyBuffer(ptr->buffer);
        CORE.NRI.FreeMemory(ptr->memory);
        RfxDelete(ptr);
    });
}

void* rfxMapBuffer(RfxBuffer buffer) {
    if (!buffer)
        return nullptr;
    return CORE.NRI.MapBuffer(*buffer->buffer, 0, buffer->size);
}

void rfxUnmapBuffer(RfxBuffer buffer) {
    if (!buffer)
        return;
    CORE.NRI.UnmapBuffer(*buffer->buffer);
}

RfxTexture rfxCreateTexture(int width, int height, RfxFormat format, int sampleCount, RfxTextureUsageFlags usage, const void* initialData) {
    RfxTextureDesc desc = {};
    desc.width = width;
    desc.height = height;
    desc.depth = 1;
    desc.mipLevels = 1;
    desc.arrayLayers = 1;
    desc.format = format;
    desc.sampleCount = sampleCount;
    desc.usage = usage;
    desc.initialData = initialData;
    return rfxCreateTextureEx(&desc);
}

RfxTexture rfxCreateTextureEx(const RfxTextureDesc* desc) {
    if (!desc)
        return nullptr;

    int sampleCount = (desc->sampleCount <= 0) ? 1 : desc->sampleCount;
    uint32_t depth = (desc->depth <= 0) ? 1 : desc->depth;
    uint32_t mips = (desc->mipLevels <= 0) ? 1 : desc->mipLevels;
    uint32_t layers = (desc->arrayLayers <= 0) ? 1 : desc->arrayLayers;

    RfxTextureImpl* impl = RfxNew<RfxTextureImpl>();
    impl->format = ToNRIFormat(desc->format);
    impl->width = desc->width;
    impl->height = desc->height;
    impl->sampleCount = (uint32_t)sampleCount;
    impl->bindlessIndex = AllocTextureSlot();

    impl->mipOffset = 0;
    impl->mipNum = mips;
    impl->layerOffset = 0;
    impl->layerNum = layers;

    impl->state = RfxNew<RfxTextureSharedState>();
    impl->state->totalMips = mips;
    impl->state->totalLayers = layers;
    impl->state->subresourceStates.resize(mips * layers, RFX_STATE_UNDEFINED);

    nri::TextureDesc td = {};
    td.type = (depth > 1) ? nri::TextureType::TEXTURE_3D : nri::TextureType::TEXTURE_2D;
    td.format = impl->format;
    td.width = (uint16_t)desc->width;
    td.height = (uint16_t)desc->height;
    td.depth = (uint16_t)depth;
    td.mipNum = (nri::Dim_t)mips;
    td.layerNum = (nri::Dim_t)layers;
    td.sampleNum = (nri::Sample_t)sampleCount;
    td.usage = nri::TextureUsageBits::NONE;

    if (desc->usage & RFX_TEXTURE_USAGE_SHADER_RESOURCE)
        td.usage |= nri::TextureUsageBits::SHADER_RESOURCE;
    if (desc->usage & RFX_TEXTURE_USAGE_RENDER_TARGET)
        td.usage |= nri::TextureUsageBits::COLOR_ATTACHMENT;
    if (desc->usage & RFX_TEXTURE_USAGE_DEPTH_STENCIL)
        td.usage |= nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
    if (desc->usage & RFX_TEXTURE_USAGE_STORAGE)
        td.usage |= nri::TextureUsageBits::SHADER_RESOURCE_STORAGE;

    NRI_CHECK(CORE.NRI.CreateTexture(*CORE.NRIDevice, td, impl->texture));

    AllocateAndBind<nri::Texture, nri::BindTextureMemoryDesc>(
        impl->texture, nri::MemoryLocation::DEVICE, impl->memory,
        [&](nri::Texture& t, nri::MemoryLocation l, nri::MemoryDesc& d) { CORE.NRI.GetTextureMemoryDesc(t, l, d); },
        [&](const nri::BindTextureMemoryDesc* d, uint32_t n) { return CORE.NRI.BindTextureMemory(d, n); }
    );

    // SRV
    if (desc->usage & RFX_TEXTURE_USAGE_SHADER_RESOURCE && sampleCount == 1) {
        if (td.type == nri::TextureType::TEXTURE_3D) {
            nri::Texture3DViewDesc vd = {};
            vd.texture = impl->texture;
            vd.format = impl->format;
            vd.viewType = nri::Texture3DViewType::SHADER_RESOURCE;
            vd.mipNum = nri::REMAINING;
            vd.sliceNum = nri::REMAINING;
            NRI_CHECK(CORE.NRI.CreateTexture3DView(vd, impl->descriptor));
        } else {
            nri::Texture2DViewDesc vd = {};
            vd.texture = impl->texture;
            vd.format = impl->format;
            vd.viewType = nri::Texture2DViewType::SHADER_RESOURCE;
            vd.mipNum = nri::REMAINING;
            vd.layerNum = nri::REMAINING;
            NRI_CHECK(CORE.NRI.CreateTexture2DView(vd, impl->descriptor));
        }

        nri::UpdateDescriptorRangeDesc update = {};
        update.descriptorSet = CORE.Bindless.globalDescriptorSet;
        update.rangeIndex = 0;
        update.baseDescriptor = impl->bindlessIndex;
        update.descriptorNum = 1;
        update.descriptors = &impl->descriptor;
        CORE.NRI.UpdateDescriptorRanges(&update, 1);
    }

    // UAV
    if (desc->usage & RFX_TEXTURE_USAGE_STORAGE) {
        if (td.type == nri::TextureType::TEXTURE_3D) {
            nri::Texture3DViewDesc uav = {};
            uav.texture = impl->texture;
            uav.format = impl->format;
            uav.viewType = nri::Texture3DViewType::SHADER_RESOURCE_STORAGE;
            uav.mipNum = nri::REMAINING;
            uav.sliceNum = nri::REMAINING;
            NRI_CHECK(CORE.NRI.CreateTexture3DView(uav, impl->descriptorUAV));
        } else {
            nri::Texture2DViewDesc uav = {};
            uav.texture = impl->texture;
            uav.format = impl->format;
            uav.viewType = nri::Texture2DViewType::SHADER_RESOURCE_STORAGE;
            uav.mipNum = nri::REMAINING;
            uav.layerNum = nri::REMAINING;
            NRI_CHECK(CORE.NRI.CreateTexture2DView(uav, impl->descriptorUAV));
        }

        nri::UpdateDescriptorRangeDesc update = {};
        update.descriptorSet = CORE.Bindless.globalDescriptorSet;
        update.rangeIndex = 4;
        update.baseDescriptor = impl->bindlessIndex;
        update.descriptorNum = 1;
        update.descriptors = &impl->descriptorUAV;
        CORE.NRI.UpdateDescriptorRanges(&update, 1);
    }

    // RTV / DSV
    if (desc->usage & (RFX_TEXTURE_USAGE_RENDER_TARGET | RFX_TEXTURE_USAGE_DEPTH_STENCIL)) {
        nri::Texture2DViewDesc avd = {};
        avd.texture = impl->texture;
        avd.format = impl->format;
        if (desc->usage & RFX_TEXTURE_USAGE_DEPTH_STENCIL)
            avd.viewType = nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT;
        else
            avd.viewType = nri::Texture2DViewType::COLOR_ATTACHMENT;

        avd.mipNum = nri::REMAINING;
        avd.layerNum = nri::REMAINING;

        NRI_CHECK(CORE.NRI.CreateTexture2DView(avd, impl->descriptorAttachment));
    }

    // only transition if we have data to upload
    if (desc->initialData && sampleCount == 1) {
        RfxResourceState finalState = RFX_STATE_SHADER_READ;

        const nri::FormatProps* props = nri::nriGetFormatProps(impl->format);
        uint32_t bpp = props->stride;

        nri::TextureRegionDesc region = {};
        region.width = (nri::Dim_t)desc->width;
        region.height = (nri::Dim_t)desc->height;
        region.depth = (nri::Dim_t)depth;
        region.planes = nri::PlaneBits::ALL;

        uint64_t sliceBytes = desc->width * desc->height * bpp;

        UploadToResource(
            nullptr, nullptr, 0, impl->texture, &region, desc->initialData, sliceBytes * depth, desc->width * bpp, sliceBytes, finalState,
            nullptr, impl
        );
    }

    return impl;
}

void rfxDestroyTexture(RfxTexture texture) {
    if (!texture)
        return;
    RfxTextureImpl* ptr = texture;

    if (!ptr->isView && ptr->bindlessIndex != 0)
        FreeTextureSlot(ptr->bindlessIndex);
    else if (ptr->isView && ptr->bindlessIndex != 0)
        FreeTextureSlot(ptr->bindlessIndex);

    rfxDeferDestruction([=]() {
        if (ptr->descriptor)
            CORE.NRI.DestroyDescriptor(ptr->descriptor);
        if (ptr->descriptorAttachment)
            CORE.NRI.DestroyDescriptor(ptr->descriptorAttachment);
        if (ptr->descriptorUAV)
            CORE.NRI.DestroyDescriptor(ptr->descriptorUAV);

        if (!ptr->isView) {
            CORE.NRI.DestroyTexture(ptr->texture);
            CORE.NRI.FreeMemory(ptr->memory);
        }

        if (ptr->state)
            ptr->state->Release();
        RfxDelete(ptr);
    });
}

uint32_t rfxGetTextureId(RfxTexture texture) {
    return texture ? texture->bindlessIndex : 0;
}

RfxSampler rfxCreateSampler(RfxFilter filter, RfxAddressMode addressMode) {
    RfxSamplerImpl* impl = RfxNew<RfxSamplerImpl>();
    nri::SamplerDesc sd = {};
    nri::Filter f = (filter == RFX_FILTER_LINEAR) ? nri::Filter::LINEAR : nri::Filter::NEAREST;
    sd.filters = { f, f, f, nri::FilterOp::AVERAGE };
    nri::AddressMode m = (addressMode == RFX_WRAP_CLAMP)
                             ? nri::AddressMode::CLAMP_TO_EDGE
                             : (addressMode == RFX_WRAP_MIRROR ? nri::AddressMode::MIRRORED_REPEAT : nri::AddressMode::REPEAT);
    sd.addressModes = { m, m, m };
    sd.anisotropy = 1;
    sd.mipMax = 16.0f;
    NRI_CHECK(CORE.NRI.CreateSampler(*CORE.NRIDevice, sd, impl->descriptor));
    return impl;
}

void rfxDestroySampler(RfxSampler sampler) {
    if (!sampler)
        return;
    RfxSamplerImpl* ptr = sampler;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyDescriptor(ptr->descriptor);
        RfxDelete(ptr);
    });
}

//
// Slang
//

static const char* s_RafxSlangContent = R"(#ifndef RAFX_SLANG_H
#define RAFX_SLANG_H

#ifdef RFX_BACKEND_D3D12
    // D3D12/DXIL
    Texture2D g_Textures[RFX_MAX_BINDLESS_TEXTURES] : register(t0, space1);
    SamplerState g_Samplers[4] : register(s0, space1);
    ByteAddressBuffer g_Buffers[RFX_MAX_BINDLESS_TEXTURES] : register(t4096, space1);
    RWByteAddressBuffer g_RWBuffers[RFX_MAX_BINDLESS_TEXTURES] : register(u0, space1);
    RWTexture2D<float4> g_RWTextures[RFX_MAX_BINDLESS_TEXTURES] : register(u4096, space1);
#ifdef RFX_RAY_TRACING_SUPPORTED
    RaytracingAccelerationStructure g_AccelerationStructures[2048] : register(t8192, space1);
#endif

    #define RFX_PUSH_CONSTANTS(StructName, Name) \
        [[vk::push_constant]] cbuffer Name##_RootConstants : register(b0, space0) { StructName Name; }

#else
    // Vulkan/SPIR-V
    [[vk::binding(0, 1)]] Texture2D g_Textures[RFX_MAX_BINDLESS_TEXTURES];
    [[vk::binding(1, 1)]] SamplerState g_Samplers[4];
    [[vk::binding(2, 1)]] ByteAddressBuffer g_Buffers[RFX_MAX_BINDLESS_TEXTURES];
    [[vk::binding(3, 1)]] RWByteAddressBuffer g_RWBuffers[RFX_MAX_BINDLESS_TEXTURES];
    [[vk::binding(4, 1)]] RWTexture2D<float4> g_RWTextures[RFX_MAX_BINDLESS_TEXTURES];
#ifdef RFX_RAY_TRACING_SUPPORTED
    [[vk::binding(5, 1)]] RaytracingAccelerationStructure g_AccelerationStructures[2048];
#endif

    #define RFX_PUSH_CONSTANTS(StructName, Name) \
        [[vk::push_constant]] StructName Name

#endif

Texture2D GetTexture(uint id) { return g_Textures[id]; }
ByteAddressBuffer GetBuffer(uint id) { return g_Buffers[id]; }
RWByteAddressBuffer GetRWBuffer(uint id) { return g_RWBuffers[id]; }
RWTexture2D<float4> GetRWTexture(uint id) { return g_RWTextures[id]; }
#ifdef RFX_RAY_TRACING_SUPPORTED
RaytracingAccelerationStructure GetAccelerationStructure(uint id) { return g_AccelerationStructures[id]; }
#endif

SamplerState GetSamplerLinearClamp() { return g_Samplers[0]; }
SamplerState GetSamplerLinearWrap() { return g_Samplers[1]; }
SamplerState GetSamplerNearestClamp() { return g_Samplers[2]; }
SamplerState GetSamplerNearestWrap() { return g_Samplers[3]; }

#endif
)";

struct RafxMemoryBlob : public ISlangBlob {
    const void* m_Data;
    size_t m_Size;
    bool m_OwnsData;
    int m_RefCount;

    RafxMemoryBlob(const void* data, size_t size, bool ownsData) : m_Data(data), m_Size(size), m_OwnsData(ownsData), m_RefCount(1) {}

    virtual ~RafxMemoryBlob() {
        if (m_OwnsData)
            RfxFree((char*)m_Data);
    }

    // ISlangUnknown
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() || uuid == ISlangBlob::getTypeGuid()) {
            addRef();
            *outObject = this;
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override {
        return ++m_RefCount;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        uint32_t r = --m_RefCount;
        if (r == 0)
            RfxDelete(this);
        return r;
    }

    // ISlangBlob
    SLANG_NO_THROW void const* SLANG_MCALL getBufferPointer() override {
        return m_Data;
    }
    SLANG_NO_THROW size_t SLANG_MCALL getBufferSize() override {
        return m_Size;
    }
};

struct RafxFileSystem : public ISlangFileSystem {
    std::map<std::string, std::string> m_VirtualFiles;

    void addFile(const char* name, const char* content) {
        std::lock_guard<std::mutex> lock(CORE.VirtualFSMutex);
        m_VirtualFiles[name] = content;
    }

    void removeFile(const char* name) {
        std::lock_guard<std::mutex> lock(CORE.VirtualFSMutex);
        m_VirtualFiles.erase(name);
    }

    // ISlangUnknown
    SLANG_NO_THROW SlangResult SLANG_MCALL queryInterface(SlangUUID const& uuid, void** outObject) override {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() || uuid == ISlangFileSystem::getTypeGuid()) {
            addRef();
            *outObject = this;
            return SLANG_OK;
        }
        return SLANG_E_NO_INTERFACE;
    }

    SLANG_NO_THROW uint32_t SLANG_MCALL addRef() override {
        return 1;
    }
    SLANG_NO_THROW uint32_t SLANG_MCALL release() override {
        return 1;
    }

    // ISlangCastable
    SLANG_NO_THROW void* SLANG_MCALL castAs(const SlangUUID& uuid) override {
        if (uuid == ISlangUnknown::getTypeGuid() || uuid == ISlangCastable::getTypeGuid() || uuid == ISlangFileSystem::getTypeGuid()) {
            return this;
        }
        return nullptr;
    }

    // ISlangFileSystem
    SLANG_NO_THROW SlangResult SLANG_MCALL loadFile(char const* path, ISlangBlob** outBlob) override {
        if (!outBlob)
            return SLANG_E_INVALID_ARG;

        namespace fs = std::filesystem;
        fs::path p(path);

        // check vfs
        {
            std::lock_guard<std::mutex> lock(CORE.VirtualFSMutex);
            auto it = m_VirtualFiles.find(path);
            if (it != m_VirtualFiles.end()) {
                size_t len = it->second.size();
                char* buf = (char*)RfxAlloc(len * sizeof(char));
                memcpy(buf, it->second.c_str(), len);
                *outBlob = RfxNew<RafxMemoryBlob>(buf, len, true);
                return SLANG_OK;
            }
        }

        // check for embedded rafx.slang (always present)
        if (p.filename() == "rafx.slang") {
            *outBlob = RfxNew<RafxMemoryBlob>(s_RafxSlangContent, strlen(s_RafxSlangContent), false);
            return SLANG_OK;
        }

        if (!fs::exists(p) || !fs::is_regular_file(p))
            return SLANG_E_NOT_FOUND;

        std::ifstream file(p, std::ios::binary);
        if (!file.is_open())
            return SLANG_E_CANNOT_OPEN;

        size_t size = static_cast<size_t>(fs::file_size(p));
        char* buffer = (char*)RfxAlloc(size * sizeof(char));

        if (!file.read(buffer, size)) {
            RfxFree(buffer);
            return SLANG_E_CANNOT_OPEN;
        }

        *outBlob = RfxNew<RafxMemoryBlob>(buffer, size, true);
        return SLANG_OK;
    }
};

static RafxFileSystem s_FileSystem;

static void ParseConstSampler(slang::UserAttribute* attr, nri::SamplerDesc& desc) {
    int magFilter = 0, minFilter = 0, mipFilter = 0, wrap = 0;
    if (attr->getArgumentCount() >= 4) {
        attr->getArgumentValueInt(0, &magFilter);
        attr->getArgumentValueInt(1, &minFilter);
        attr->getArgumentValueInt(2, &mipFilter);
        attr->getArgumentValueInt(3, &wrap);
    }
    desc.filters.mag = (magFilter == 0) ? nri::Filter::NEAREST : nri::Filter::LINEAR;
    desc.filters.min = (minFilter == 0) ? nri::Filter::NEAREST : nri::Filter::LINEAR;
    desc.filters.mip = (mipFilter == 0) ? nri::Filter::NEAREST : nri::Filter::LINEAR;
    desc.filters.op = nri::FilterOp::AVERAGE;

    if (mipFilter == 2)
        desc.anisotropy = 8;

    nri::AddressMode mode;
    switch (wrap) {
    case 1: mode = nri::AddressMode::CLAMP_TO_EDGE; break;
    case 2: mode = nri::AddressMode::MIRRORED_REPEAT; break;
    case 3: mode = nri::AddressMode::MIRROR_CLAMP_TO_EDGE; break;
    default: mode = nri::AddressMode::REPEAT; break;
    }
    desc.addressModes = { mode, mode, mode };
    desc.mipMax = 16.0f;
}

// FNV-1a 64-bit hash
static uint64_t Hash64(const void* data, size_t size, uint64_t seed = 0xcbf29ce484222325ULL) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++) {
        seed ^= p[i];
        seed *= 0x100000001b3ULL;
    }
    return seed;
}

static uint64_t ComputeShaderHash(
    const char* path, const char* source, const char** defines, int numDefines, const char** includeDirs, int numIncludeDirs, bool isD3D12
) {
    uint64_t hash = 0;
    // hash source/content
    if (source) {
        hash = Hash64(source, strlen(source), hash);
    } else if (path) {
        // try VFS first
        bool foundInVfs = false;
        {
            std::lock_guard<std::mutex> lock(CORE.VirtualFSMutex);
            auto it = s_FileSystem.m_VirtualFiles.find(path);
            if (it != s_FileSystem.m_VirtualFiles.end()) {
                hash = Hash64(it->second.c_str(), it->second.size(), hash);
                foundInVfs = true;
            }
        }

        if (!foundInVfs) {
            // read file
            std::ifstream t(path, std::ios::binary);
            if (t.is_open()) {
                std::stringstream buffer;
                buffer << t.rdbuf();
                std::string content = buffer.str();
                hash = Hash64(content.data(), content.size(), hash);
            } else {
                hash = Hash64(path, strlen(path), hash);
            }
        }
    }

    // hash defines/includes/backend
    for (int i = 0; i < numDefines; i++)
        hash = Hash64(defines[i], strlen(defines[i]), hash);
    for (int i = 0; i < numIncludeDirs; i++)
        hash = Hash64(includeDirs[i], strlen(includeDirs[i]), hash);
    uint8_t backend = isD3D12 ? 1 : 0;
    hash = Hash64(&backend, 1, hash);
    return hash;
}

struct CacheHeader {
    uint32_t magic; // 'RAFX'
    uint32_t version;
    uint32_t stageCount;
    uint32_t bindlessSetIndex;
    uint32_t descriptorSetCount;
    uint32_t bindingCount;
    uint32_t rootConstantCount;
    uint32_t rootSamplerCount;
    uint32_t stageMask;
};

static std::filesystem::path GetCacheFilePath(uint64_t hash) {
    if (CORE.ShaderCachePath.empty()) {
        auto tmp = std::filesystem::temp_directory_path() / "rafx-shdcache";
        std::filesystem::create_directories(tmp);
        CORE.ShaderCachePath = tmp.string();
    }
    char name[32];
    snprintf(name, 32, "%llx.bin", (unsigned long long)hash);
    return std::filesystem::path(CORE.ShaderCachePath) / name;
}

static RfxShaderImpl* TryLoadFromCache(uint64_t hash) {
    if (!CORE.ShaderCacheEnabled)
        return nullptr;

    RfxVector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(CORE.ShaderCacheMutex);
        if (CORE.CacheLoadCb) {
            void* ptr = nullptr;
            size_t size = 0;
            if (CORE.CacheLoadCb(hash, &ptr, &size, CORE.CacheUserPtr) && ptr && size > 0) {
                data.resize(size);
                memcpy(data.data(), ptr, size);
            }
        } else {
            std::filesystem::path p = GetCacheFilePath(hash);
            if (std::filesystem::exists(p)) {
                std::ifstream file(p, std::ios::binary);
                if (file)
                    data = RfxVector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            }
        }
    }

    if (data.empty())
        return nullptr;
    if (data.size() < sizeof(CacheHeader))
        return nullptr;

    CacheHeader* h = (CacheHeader*)data.data();
    if (h->magic != 0x58464152) // 'RAFX'
        return nullptr;

    size_t offset = sizeof(CacheHeader);

    auto Check = [&](size_t size) { return (offset + size <= data.size()); };
    if (!Check(0))
        return nullptr;

    RfxShaderImpl* impl = RfxNew<RfxShaderImpl>();
    impl->bindlessSetIndex = h->bindlessSetIndex;
    impl->descriptorSetCount = h->descriptorSetCount;
    impl->stageMask = (nri::StageBits)h->stageMask;
    impl->fromCache = true;

    auto ReadString = [&](std::string& out) {
        if (!Check(4))
            return;
        uint32_t len = 0;
        memcpy(&len, data.data() + offset, 4);
        offset += 4;
        if (len > 0) {
            if (!Check(len))
                return;
            out.assign((const char*)(data.data() + offset), len);
            offset += len;
        }
    };

    // load stages
    for (uint32_t i = 0; i < h->stageCount; ++i) {
        RfxShaderImpl::Stage s;
        if (!Check(sizeof(nri::StageBits)))
            break;
        memcpy(&s.stageBits, data.data() + offset, sizeof(nri::StageBits));
        offset += sizeof(nri::StageBits);
        ReadString(s.entryPoint);
        ReadString(s.sourceEntryPoint);

        if (!Check(4))
            break;
        uint32_t codeLen = 0;
        memcpy(&codeLen, data.data() + offset, 4);
        offset += 4;

        if (!Check(codeLen))
            break;
        s.bytecode.resize(codeLen);
        memcpy(s.bytecode.data(), data.data() + offset, codeLen);
        offset += codeLen;
        impl->stages.push_back(s);
    }

    // load bindings
    for (uint32_t i = 0; i < h->bindingCount; ++i) {
        if (!Check(sizeof(RfxShaderImpl::BindingRange)))
            break;
        RfxShaderImpl::BindingRange b;
        memcpy(&b, data.data() + offset, sizeof(RfxShaderImpl::BindingRange));
        offset += sizeof(RfxShaderImpl::BindingRange);
        impl->bindings.push_back(b);
    }

    // load RootConstants
    for (uint32_t i = 0; i < h->rootConstantCount; ++i) {
        if (!Check(sizeof(nri::RootConstantDesc)))
            break;
        nri::RootConstantDesc rc;
        memcpy(&rc, data.data() + offset, sizeof(nri::RootConstantDesc));
        offset += sizeof(nri::RootConstantDesc);
        impl->rootConstants.push_back(rc);
    }

    // load RootSamplers
    for (uint32_t i = 0; i < h->rootSamplerCount; ++i) {
        if (!Check(sizeof(nri::RootSamplerDesc)))
            break;
        nri::RootSamplerDesc rs;
        memcpy(&rs, data.data() + offset, sizeof(nri::RootSamplerDesc));
        offset += sizeof(nri::RootSamplerDesc);
        impl->rootSamplers.push_back(rs);
    }

    return impl;
}

static void SaveToCache(uint64_t hash, RfxShaderImpl* impl) {
    if (!CORE.ShaderCacheEnabled)
        return;

    RfxVector<uint8_t> blob;
    CacheHeader h = {};
    h.magic = 0x58464152; // 'RAFX'
    h.version = 1;
    h.stageCount = (uint32_t)impl->stages.size();
    h.bindlessSetIndex = impl->bindlessSetIndex;
    h.descriptorSetCount = impl->descriptorSetCount;
    h.bindingCount = (uint32_t)impl->bindings.size();
    h.rootConstantCount = (uint32_t)impl->rootConstants.size();
    h.rootSamplerCount = (uint32_t)impl->rootSamplers.size();
    h.stageMask = (uint32_t)impl->stageMask;

    auto Write = [&](const void* d, size_t s) {
        size_t cur = blob.size();
        blob.resize(cur + s);
        memcpy(blob.data() + cur, d, s);
    };
    auto WriteString = [&](const std::string& s) {
        uint32_t len = (uint32_t)s.size();
        Write(&len, 4);
        if (len > 0)
            Write(s.data(), len);
    };

    Write(&h, sizeof(h));

    for (const auto& s : impl->stages) {
        Write(&s.stageBits, sizeof(s.stageBits));
        WriteString(s.entryPoint);
        WriteString(s.sourceEntryPoint);
        uint32_t codeLen = (uint32_t)s.bytecode.size();
        Write(&codeLen, 4);
        Write(s.bytecode.data(), codeLen);
    }

    for (const auto& b : impl->bindings)
        Write(&b, sizeof(b));
    for (const auto& rc : impl->rootConstants)
        Write(&rc, sizeof(rc));
    for (const auto& rs : impl->rootSamplers)
        Write(&rs, sizeof(rs));

    {
        std::lock_guard<std::mutex> lock(CORE.ShaderCacheMutex);
        if (CORE.CacheSaveCb) {
            CORE.CacheSaveCb(hash, blob.data(), blob.size(), CORE.CacheUserPtr);
        } else {
            std::ofstream file(GetCacheFilePath(hash), std::ios::binary);
            if (file)
                file.write((const char*)blob.data(), blob.size());
        }
    }
}

static bool CreatePipelineLayoutFromImpl(RfxShaderImpl* impl, bool isD3D12, bool hasRT) {
    // reconstruct descriptor sets from bindings
    RfxVector<RfxVector<nri::DescriptorRangeDesc>> rangeStorage;
    std::map<uint32_t, RfxVector<nri::DescriptorRangeDesc>> setBuilders;

    for (const auto& b : impl->bindings) {
        nri::DescriptorRangeDesc range = {};
        range.baseRegisterIndex = b.baseRegister;
        range.descriptorNum = b.count;
        range.descriptorType = b.type;
        range.shaderStages = impl->stageMask;
        setBuilders[b.setIndex].push_back(range);
    }

    RfxVector<nri::DescriptorSetDesc> allSets;
    for (auto& [space, ranges] : setBuilders) {
        if (space == 1)
            continue;
        rangeStorage.push_back(std::move(ranges));
        allSets.push_back({ space, rangeStorage.back().data(), (uint32_t)rangeStorage.back().size(), nri::DescriptorSetBits::NONE });
    }

    // bindless set (space 1)
    nri::DescriptorRangeDesc bindlessRanges[6] = {};
    nri::DescriptorRangeBits bindlessFlags =
        nri::DescriptorRangeBits::PARTIALLY_BOUND | nri::DescriptorRangeBits::ARRAY | nri::DescriptorRangeBits::ALLOW_UPDATE_AFTER_SET;

    // 0 = textures
    bindlessRanges[0] = { 0, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::TEXTURE, nri::StageBits::ALL, bindlessFlags };

    // 1 = samplers
    bindlessRanges[1] = { isD3D12 ? 0u : 1u, 4, nri::DescriptorType::SAMPLER, nri::StageBits::ALL, bindlessFlags };

    // 2 = buffers
    bindlessRanges[2] = { isD3D12 ? RFX_MAX_BINDLESS_TEXTURES : 2u, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::STRUCTURED_BUFFER,
                          nri::StageBits::ALL, bindlessFlags };

    // 3 = RW buffers
    bindlessRanges[3] = { isD3D12 ? 0u : 3u, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::ALL,
                          bindlessFlags };

    // 4 = RW textures
    bindlessRanges[4] = { isD3D12 ? RFX_MAX_BINDLESS_TEXTURES : 4u, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::STORAGE_TEXTURE,
                          nri::StageBits::ALL, bindlessFlags };

    uint32_t bindlessRangeCount = 5;

    if (hasRT) {
        // 5 = AS
        bindlessRanges[5] = { isD3D12 ? (RFX_MAX_BINDLESS_TEXTURES * 2) : 5u, 2048, nri::DescriptorType::ACCELERATION_STRUCTURE,
                              nri::StageBits::ALL, bindlessFlags };
        bindlessRangeCount = 6;
    }

    allSets.push_back({ 1, bindlessRanges, bindlessRangeCount, nri::DescriptorSetBits::ALLOW_UPDATE_AFTER_SET });

    impl->bindlessSetIndex = (uint32_t)allSets.size() - 1;
    impl->descriptorSetCount = (uint32_t)allSets.size();

    nri::PipelineLayoutDesc layoutDesc = {};
    layoutDesc.descriptorSets = allSets.data();
    layoutDesc.descriptorSetNum = impl->descriptorSetCount;
    layoutDesc.rootConstants = impl->rootConstants.data();
    layoutDesc.rootConstantNum = (uint32_t)impl->rootConstants.size();
    layoutDesc.rootSamplers = impl->rootSamplers.data();
    layoutDesc.rootSamplerNum = (uint32_t)impl->rootSamplers.size();
    layoutDesc.shaderStages = impl->stageMask;
    layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;

    return (CORE.NRI.CreatePipelineLayout(*CORE.NRIDevice, layoutDesc, impl->pipelineLayout) == nri::Result::SUCCESS);
}

static RfxShader CompileShaderInternal(
    const char* path /* nullable */, const char* sourceCode /* nullable */, const char** defines, int numDefines, const char** includeDirs,
    int numIncludeDirs
) {
    RFX_ASSERT(numDefines % 2 == 0 && "rfxCompileShader: Number of defines must be even");
    RFX_ASSERT((sourceCode != nullptr || path != nullptr) && "rfxCompileShader: Source code or path must be provided");

    std::lock_guard<std::mutex> compileLock(CORE.ShaderCompileMutex);

    nri::GraphicsAPI graphicsAPI = CORE.NRI.GetDeviceDesc(*CORE.NRIDevice).graphicsAPI;
    bool isD3D12 = (graphicsAPI == nri::GraphicsAPI::D3D12);
    bool hasRT = (CORE.FeatureSupportFlags & RFX_FEATURE_RAY_TRACING) != 0;

    // check cache
    uint64_t hash = 0;
    if (CORE.ShaderCacheEnabled) {
        hash = ComputeShaderHash(path, sourceCode, defines, numDefines, includeDirs, numIncludeDirs, isD3D12);
        RfxShaderImpl* cached = TryLoadFromCache(hash);
        if (cached) {
            if (CreatePipelineLayoutFromImpl(cached, isD3D12, hasRT)) {
                if (path)
                    cached->filepath = path;
                return (RfxShader)cached;
            }
            RfxDelete(cached);
        }
    }

    // setup compiler session
    RfxVector<slang::CompilerOptionEntry> sessionOpts;
    sessionOpts.push_back({ slang::CompilerOptionName::DebugInformation, { .intValue0 = SLANG_DEBUG_INFO_LEVEL_STANDARD } });
    sessionOpts.push_back({ slang::CompilerOptionName::Optimization, { .intValue0 = SLANG_OPTIMIZATION_LEVEL_DEFAULT } });

    sessionOpts.push_back(
        { slang::CompilerOptionName::Capability, { .intValue0 = CORE.SlangSession->findCapability(isD3D12 ? "sm_6_0" : "spirv_1_6") } }
    );

    RfxVector<slang::PreprocessorMacroDesc> prepMacros;
    for (int i = 0; i < numDefines; i += 2) {
        prepMacros.push_back({ defines[i], defines[i + 1] });
    }

    if (isD3D12)
        prepMacros.push_back({ "RFX_BACKEND_D3D12", "1" });
    else
        prepMacros.push_back({ "RFX_BACKEND_SPIRV", "1" });

    if (hasRT)
        prepMacros.push_back({ "RFX_RAY_TRACING_SUPPORTED", "1" });

    char maxBindlessStr[32];
    snprintf(maxBindlessStr, sizeof(maxBindlessStr), "%d", RFX_MAX_BINDLESS_TEXTURES);
    prepMacros.push_back({ "RFX_MAX_BINDLESS_TEXTURES", maxBindlessStr });

    slang::TargetDesc targetDesc = {};
    targetDesc.format = isD3D12 ? SLANG_DXIL : SLANG_SPIRV;
    targetDesc.profile = CORE.SlangSession->findProfile(isD3D12 ? "sm_6_0" : "glsl_460");
    if (!isD3D12)
        targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

    slang::SessionDesc sessionDesc = {
        .targets = &targetDesc,
        .targetCount = 1,
        .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
        .searchPaths = includeDirs,
        .searchPathCount = numIncludeDirs,
        .preprocessorMacros = prepMacros.data(),
        .preprocessorMacroCount = (uint32_t)prepMacros.size(),
        .fileSystem = &s_FileSystem,
        .compilerOptionEntries = sessionOpts.data(),
        .compilerOptionEntryCount = (uint32_t)sessionOpts.size(),
    };

    Slang::ComPtr<slang::ISession> session;
    if (SLANG_FAILED(CORE.SlangSession->createSession(sessionDesc, session.writeRef())))
        return nullptr;

    // compile and link
    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = nullptr;
    if (sourceCode) {
        module = session->loadModuleFromSourceString("shader", path ? path : "memory", sourceCode, diagnostics.writeRef());
    } else if (path) {
        module = session->loadModule(path, diagnostics.writeRef());
    }

    if (diagnostics && diagnostics->getBufferSize() > 0) {
        printf("[Slang Compile Log]: %s\n", (const char*)diagnostics->getBufferPointer());
    }
    if (!module)
        return nullptr;

    RfxVector<slang::IComponentType*> components = { module };
    uint32_t definedEPCount = module->getDefinedEntryPointCount();
    uint32_t accumulatedStages = 0;

    for (uint32_t i = 0; i < definedEPCount; i++) {
        Slang::ComPtr<slang::IEntryPoint> ep;
        module->getDefinedEntryPoint(i, ep.writeRef());
        slang::EntryPointReflection* epReflect = ep->getLayout()->getEntryPointByIndex(0);
        accumulatedStages |= (uint32_t)ToNRIStageBits(epReflect->getStage());
        components.push_back(ep.get());
    }

    nri::StageBits actualShaderStages = (nri::StageBits)accumulatedStages;
    if (actualShaderStages == nri::StageBits::NONE)
        actualShaderStages = nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

    Slang::ComPtr<slang::IComponentType> program;
    session->createCompositeComponentType(components.data(), (SlangInt)components.size(), program.writeRef(), diagnostics.writeRef());

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    program->link(linkedProgram.writeRef(), diagnostics.writeRef());

    if (diagnostics && diagnostics->getBufferSize() > 0) {
        printf("[Slang Link Log]: %s\n", (const char*)diagnostics->getBufferPointer());
    }
    if (!linkedProgram)
        return nullptr;

    RfxShaderImpl* impl = RfxNew<RfxShaderImpl>();
    if (path)
        impl->filepath = path;
    for (int i = 0; i < numDefines; i++)
        impl->defines.push_back(defines[i]);
    for (int i = 0; i < numIncludeDirs; i++)
        impl->includeDirs.push_back(includeDirs[i]);
    slang::ProgramLayout* layout = linkedProgram->getLayout();
    impl->stageMask = actualShaderStages;

    // reflection
    std::map<uint32_t, uint32_t> setRangeCounts;

    for (uint32_t j = 0; j < layout->getParameterCount(); j++) {
        slang::VariableLayoutReflection* par = layout->getParameterByIndex(j);
        slang::TypeLayoutReflection* typeLayout = par->getTypeLayout();

        if (par->getName() && memcmp(par->getName(), "rafx__", 6) == 0)
            continue;

        slang::ParameterCategory category = par->getCategory();

        if (category == slang::ParameterCategory::PushConstantBuffer) {
            uint32_t size = (uint32_t)typeLayout->getElementTypeLayout()->getSize();

            bool found = false;
            for (auto& existing : impl->rootConstants) {
                if (existing.registerIndex == 0) {
                    existing.size = std::max(existing.size, size);
                    existing.shaderStages |= actualShaderStages;
                    found = true;
                    break;
                }
            }

            if (!found) {
                nri::RootConstantDesc rc = {};
                rc.registerIndex = 0;
                rc.size = size;
                rc.shaderStages = actualShaderStages;
                impl->rootConstants.push_back(rc);
            }
        } else if (category == slang::ParameterCategory::ConstantBuffer) {
            // handle UBOs
            uint32_t binding = par->getBindingIndex();
            if (binding == 0) {
                uint32_t size = (uint32_t)typeLayout->getElementTypeLayout()->getSize();
                bool found = false;
                for (auto& existing : impl->rootConstants) {
                    if (existing.registerIndex == 0) {
                        existing.size = std::max(existing.size, size);
                        existing.shaderStages |= actualShaderStages;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    nri::RootConstantDesc rc = {};
                    rc.registerIndex = 0;
                    rc.size = size;
                    rc.shaderStages = actualShaderStages;
                    impl->rootConstants.push_back(rc);
                }
            } else {
                // descriptor table UBO
                uint32_t space = par->getBindingSpace();
                uint32_t rangeIdx = setRangeCounts[space]++;
                impl->bindings.push_back({ space, rangeIdx, binding, 1, nri::DescriptorType::CONSTANT_BUFFER });
            }
        } else if (category == slang::ParameterCategory::DescriptorTableSlot) {
            // handle descriptors (texture, buffer, sampler)
            uint32_t binding = par->getBindingIndex();
            uint32_t space = par->getBindingSpace();
            if (space == 1)
                continue;

            slang::TypeReflection::Kind kind = typeLayout->getKind();

            if (kind == slang::TypeReflection::Kind::SamplerState) {
                slang::UserAttribute* descAttr = par->getVariable()->findUserAttributeByName(CORE.SlangSession, "SamplerDesc");
                if (descAttr) {
                    nri::SamplerDesc samplerDesc = {};
                    ParseConstSampler(descAttr, samplerDesc);
                    nri::RootSamplerDesc rs = {};
                    rs.desc = samplerDesc;
                    rs.registerIndex = binding;
                    rs.shaderStages = actualShaderStages;
                    impl->rootSamplers.push_back(rs);
                    continue;
                }
            }

            nri::DescriptorType type = GetDescriptorType(typeLayout);
            uint32_t rangeIdx = setRangeCounts[space]++;
            impl->bindings.push_back({ space, rangeIdx, binding, 1, type });
        }
    }

    if (!CreatePipelineLayoutFromImpl(impl, isD3D12, hasRT)) {
        fprintf(stderr, "Error: Failed to create pipeline layout.\n");
        RfxDelete(impl);
        return nullptr;
    }

    // get bytecode
    SlangUInt layoutEPCount = layout->getEntryPointCount();
    for (SlangUInt i = 0; i < layoutEPCount; i++) {
        Slang::ComPtr<slang::IBlob> code;
        Slang::ComPtr<slang::IBlob> codeDiag;
        SlangResult res = linkedProgram->getEntryPointCode(i, 0, code.writeRef(), codeDiag.writeRef());

        if (codeDiag && codeDiag->getBufferSize() > 0) {
            printf("[Slang EntryPoint Log]: %s\n", (const char*)codeDiag->getBufferPointer());
        }

        if (SLANG_FAILED(res) || !code) {
            fprintf(stderr, "Error: Failed to generate bytecode for entry point %llu.\n", (unsigned long long)i);
            if (isD3D12)
                fprintf(stderr, "Hint: Ensure dxcompiler.dll and dxil.dll are present.\n");
            continue;
        }

        slang::EntryPointReflection* reflect = layout->getEntryPointByIndex(i);
        nri::StageBits stageBit = ToNRIStageBits(reflect->getStage());
        const char* sourceName = reflect->getName();
        const char* finalEntryPoint = isD3D12 ? (sourceName ? sourceName : "main") : "main";

        impl->stages.push_back(
            { .bytecode =
                  RfxVector<uint8_t>((uint8_t*)code->getBufferPointer(), (uint8_t*)code->getBufferPointer() + code->getBufferSize()),
              .stageBits = stageBit,
              .entryPoint = finalEntryPoint,
              .sourceEntryPoint = sourceName ? sourceName : "main" }
        );
    }

    if (impl->stages.empty()) {
        RfxDelete(impl);
        return nullptr;
    }

    // save to cache
    if (CORE.ShaderCacheEnabled) {
        SaveToCache(hash, impl);
    }

    return (RfxShader)impl;
}

RfxShader rfxCompileShader(const char* filepath, const char** defines, int numDefines, const char** includeDirs, int numIncludeDirs) {
    return CompileShaderInternal(filepath, nullptr, defines, numDefines, includeDirs, numIncludeDirs);
}
RfxShader rfxCompileShaderMem(const char* source, const char** defines, int numDefines, const char** includeDirs, int numIncludeDirs) {
    return CompileShaderInternal(nullptr, source, defines, numDefines, includeDirs, numIncludeDirs);
}

void rfxDestroyShader(RfxShader shader) {
    if (!shader)
        return;
    RfxShaderImpl* ptr = shader;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyPipelineLayout(ptr->pipelineLayout);
        RfxDelete(ptr);
    });
}

void rfxWatchShader(RfxShader shader, bool watch) {
    if (!shader)
        return;
    RfxShaderImpl* impl = (RfxShaderImpl*)shader;

    if (!watch) {
        impl->watcher.reset();
        return;
    }

    if (impl->filepath.empty()) {
        fprintf(stderr, "[Rafx] Warning: Cannot watch shader created from memory.\n");
        return;
    }

    if (impl->watcher)
        return;

    std::filesystem::path shaderPath(impl->filepath);

    std::error_code ec;
    if (!std::filesystem::exists(shaderPath, ec)) {
        shaderPath = std::filesystem::absolute(shaderPath, ec);
    } else {
        shaderPath = std::filesystem::canonical(shaderPath, ec);
    }

    std::filesystem::path watchDir = shaderPath.parent_path();
    std::string targetFilename = shaderPath.filename().string();

    auto callback = [impl, targetFilename](const wtr::event& e) {
        if (e.path_type == wtr::event::path_type::watcher)
            return;

        bool shouldReload = false;

        if (e.effect_type == wtr::event::effect_type::modify || e.effect_type == wtr::event::effect_type::create) {
            if (e.path_name.filename().string() == targetFilename) {
                shouldReload = true;
            }
        } else if (e.effect_type == wtr::event::effect_type::rename) {
            // handle atomic saves (rename temp file -> target file)
            if (e.associated && e.associated->path_name.filename().string() == targetFilename) {
                shouldReload = true;
            }
        }

        if (shouldReload) {
            std::lock_guard<std::mutex> lock(CORE.HotReloadMutex);
            CORE.ShadersToReload.insert((RfxShader)impl);
        }
    };

    impl->watcher = std::make_unique<wtr::watch>(watchDir, callback);
}

void rfxSetShaderCacheEnabled(bool enabled) {
    CORE.ShaderCacheEnabled = enabled;
}

void rfxSetShaderCachePath(const char* path) {
    std::lock_guard<std::mutex> lock(CORE.ShaderCacheMutex);
    if (path)
        CORE.ShaderCachePath = path;
}

void rfxSetShaderCacheCallbacks(RfxShaderCacheLoadCallback load, RfxShaderCacheSaveCallback save, void* user) {
    std::lock_guard<std::mutex> lock(CORE.ShaderCacheMutex);
    CORE.CacheLoadCb = load;
    CORE.CacheSaveCb = save;
    CORE.CacheUserPtr = user;
}

void rfxAddVirtualShaderFile(const char* filename, const char* content) {
    s_FileSystem.addFile(filename, content);
}

void rfxRemoveVirtualShaderFile(const char* filename) {
    s_FileSystem.removeFile(filename);
}

bool rfxWasShaderCached(RfxShader shader) {
    if (!shader)
        return false;
    return ((RfxShaderImpl*)shader)->fromCache;
}

void rfxPrecompileShader(
    const char* sourceOrPath, const char** defines, int numDefines, const char** includeDirs, int numIncludeDirs, bool fromMemory
) {
    rfxSetShaderCacheEnabled(true);
    RfxShader s = nullptr;
    if (fromMemory) {
        s = rfxCompileShaderMem(sourceOrPath, defines, numDefines, includeDirs, numIncludeDirs);
    } else {
        s = rfxCompileShader(sourceOrPath, defines, numDefines, includeDirs, numIncludeDirs);
    }
    if (s)
        rfxDestroyShader(s);
}

static CachedGraphics CacheGraphicsDesc(const RfxPipelineDesc* src) {
    CachedGraphics cache;
    cache.desc = *src;
    if (src->vsEntryPoint) {
        cache.vsEntryStorage = src->vsEntryPoint;
        cache.desc.vsEntryPoint = cache.vsEntryStorage.c_str();
    }
    if (src->psEntryPoint) {
        cache.psEntryStorage = src->psEntryPoint;
        cache.desc.psEntryPoint = cache.psEntryStorage.c_str();
    }
    if (src->attachmentCount > 0 && src->attachments) {
        cache.attachmentStorage.assign(src->attachments, src->attachments + src->attachmentCount);
        cache.desc.attachments = cache.attachmentStorage.data();
    }
    if (src->vertexLayoutCount > 0 && src->vertexLayout) {
        cache.layoutStorage.assign(src->vertexLayout, src->vertexLayout + src->vertexLayoutCount);
        cache.desc.vertexLayout = cache.layoutStorage.data();
    }
    return cache;
}

static CachedCompute CacheComputeDesc(const RfxComputePipelineDesc* src) {
    CachedCompute cache;
    cache.desc = *src;
    if (src->entryPoint) {
        cache.entryStorage = src->entryPoint;
        cache.desc.entryPoint = cache.entryStorage.c_str();
    }
    return cache;
}

static CachedRT CacheRTDesc(const RfxRayTracingPipelineDesc* src) {
    CachedRT cache;
    cache.desc = *src;
    if (src->groupCount > 0 && src->groups) {
        cache.groupStorage.assign(src->groups, src->groups + src->groupCount);
        for (auto& g : cache.groupStorage) {
            if (g.generalShader) {
                cache.nameStorage.push_back(g.generalShader);
                g.generalShader = cache.nameStorage.back().c_str();
            }
            if (g.closestHitShader) {
                cache.nameStorage.push_back(g.closestHitShader);
                g.closestHitShader = cache.nameStorage.back().c_str();
            }
            if (g.anyHitShader) {
                cache.nameStorage.push_back(g.anyHitShader);
                g.anyHitShader = cache.nameStorage.back().c_str();
            }
            if (g.intersectionShader) {
                cache.nameStorage.push_back(g.intersectionShader);
                g.intersectionShader = cache.nameStorage.back().c_str();
            }
        }
        cache.desc.groups = cache.groupStorage.data();
    }
    return cache;
}

RfxPipeline rfxCreatePipeline(const RfxPipelineDesc* desc) {
    RfxPipelineImpl* impl = RfxNew<RfxPipelineImpl>();
    impl->shader = desc->shader;
    impl->vertexStride = desc->vertexStride;
    impl->bindPoint = nri::BindPoint::GRAPHICS;

    impl->type = RfxPipelineImpl::GRAPHICS;
    impl->cache = CacheGraphicsDesc(desc);
    {
        std::lock_guard<std::mutex> lock(CORE.HotReloadMutex);
        impl->shader->dependentPipelines.insert(impl);
    }

    nri::GraphicsPipelineDesc gpd = {};
    gpd.pipelineLayout = impl->shader->pipelineLayout;
    gpd.inputAssembly.topology = ToNRITopology(desc->topology);
    gpd.inputAssembly.tessControlPointNum = (uint8_t)desc->patchControlPoints;

    gpd.rasterization.fillMode = desc->wireframe ? nri::FillMode::WIREFRAME : nri::FillMode::SOLID;
    gpd.rasterization.cullMode = (desc->cullMode == RFX_CULL_BACK)
                                     ? nri::CullMode::BACK
                                     : ((desc->cullMode == RFX_CULL_FRONT) ? nri::CullMode::FRONT : nri::CullMode::NONE);
    gpd.rasterization.frontCounterClockwise = true;
    gpd.rasterization.depthBias.constant = desc->depthBiasConstant;
    gpd.rasterization.depthBias.clamp = desc->depthBiasClamp;
    gpd.rasterization.depthBias.slope = desc->depthBiasSlope;
    gpd.rasterization.shadingRate = desc->shadingRate;

    uint8_t samples = (desc->sampleCount > 0) ? (uint8_t)desc->sampleCount : (uint8_t)CORE.SampleCount;
    if (samples == 0)
        samples = 1;

    nri::MultisampleDesc ms = {};
    ms.sampleNum = (nri::Sample_t)samples;
    ms.sampleMask = nri::ALL;
    gpd.multisample = &ms;

    RfxVector<nri::ColorAttachmentDesc> colorDescs;
    if (desc->attachmentCount > 0 && desc->attachments) {
        colorDescs.resize(desc->attachmentCount);
        for (uint32_t i = 0; i < desc->attachmentCount; ++i) {
            nri::ColorAttachmentDesc& cad = colorDescs[i];
            const RfxAttachmentDesc& src = desc->attachments[i];
            cad.format = ToNRIFormat(src.format);

            nri::ColorWriteBits mask = (nri::ColorWriteBits)src.blend.writeMask;
            cad.colorWriteMask = (mask == nri::ColorWriteBits::NONE) ? nri::ColorWriteBits::RGBA : mask;

            cad.blendEnabled = src.blend.blendEnabled;
            cad.colorBlend.srcFactor = ToNRIBlendFactor(src.blend.srcColor);
            cad.colorBlend.dstFactor = ToNRIBlendFactor(src.blend.dstColor);
            cad.colorBlend.op = ToNRIBlendOp(src.blend.colorOp);
            cad.alphaBlend.srcFactor = ToNRIBlendFactor(src.blend.srcAlpha);
            cad.alphaBlend.dstFactor = ToNRIBlendFactor(src.blend.dstAlpha);
            cad.alphaBlend.op = ToNRIBlendOp(src.blend.alphaOp);
        }
    } else if (desc->colorFormat != RFX_FORMAT_UNKNOWN) {
        colorDescs.resize(1);
        nri::ColorAttachmentDesc& cad = colorDescs[0];
        cad.format = ToNRIFormat(desc->colorFormat);

        nri::ColorWriteBits mask = (nri::ColorWriteBits)desc->blendState.writeMask;
        cad.colorWriteMask = (mask == nri::ColorWriteBits::NONE) ? nri::ColorWriteBits::RGBA : mask;

        cad.blendEnabled = desc->blendState.blendEnabled;
        cad.colorBlend.srcFactor = ToNRIBlendFactor(desc->blendState.srcColor);
        cad.colorBlend.dstFactor = ToNRIBlendFactor(desc->blendState.dstColor);
        cad.colorBlend.op = ToNRIBlendOp(desc->blendState.colorOp);
        cad.alphaBlend.srcFactor = ToNRIBlendFactor(desc->blendState.srcAlpha);
        cad.alphaBlend.dstFactor = ToNRIBlendFactor(desc->blendState.dstAlpha);
        cad.alphaBlend.op = ToNRIBlendOp(desc->blendState.alphaOp);
    }

    if (!colorDescs.empty()) {
        gpd.outputMerger.colors = colorDescs.data();
        gpd.outputMerger.colorNum = (uint32_t)colorDescs.size();
    }

    // depth stencil
    if (desc->depthFormat != RFX_FORMAT_UNKNOWN) {
        gpd.outputMerger.depthStencilFormat = ToNRIFormat(desc->depthFormat);
        if (desc->depthCompareOp != 0) {
            gpd.outputMerger.depth.compareOp = ToNRICompareOp(desc->depthCompareOp);
        } else {
            gpd.outputMerger.depth.compareOp = desc->depthTest ? nri::CompareOp::LESS : nri::CompareOp::NONE;
        }
        gpd.outputMerger.depth.write = desc->depthWrite;
        gpd.outputMerger.depth.boundsTest = desc->depthBoundsTest;

        if (desc->stencil.enabled) {
            gpd.outputMerger.stencil.front.compareOp = ToNRICompareOp(desc->stencil.front.compareOp);
            gpd.outputMerger.stencil.front.failOp = ToNRIStencilOp(desc->stencil.front.failOp);
            gpd.outputMerger.stencil.front.passOp = ToNRIStencilOp(desc->stencil.front.passOp);
            gpd.outputMerger.stencil.front.depthFailOp = ToNRIStencilOp(desc->stencil.front.depthFailOp);
            gpd.outputMerger.stencil.front.compareMask = desc->stencil.readMask;
            gpd.outputMerger.stencil.front.writeMask = desc->stencil.writeMask;

            gpd.outputMerger.stencil.back.compareOp = ToNRICompareOp(desc->stencil.back.compareOp);
            gpd.outputMerger.stencil.back.failOp = ToNRIStencilOp(desc->stencil.back.failOp);
            gpd.outputMerger.stencil.back.passOp = ToNRIStencilOp(desc->stencil.back.passOp);
            gpd.outputMerger.stencil.back.depthFailOp = ToNRIStencilOp(desc->stencil.back.depthFailOp);
            gpd.outputMerger.stencil.back.compareMask = desc->stencil.readMask;
            gpd.outputMerger.stencil.back.writeMask = desc->stencil.writeMask;
        }
    }

    if (desc->viewMask != 0) {
        gpd.outputMerger.viewMask = desc->viewMask;
        gpd.outputMerger.multiview = nri::Multiview::FLEXIBLE;
    }

    bool explicitVertex = (desc->vsEntryPoint != nullptr);

    // filter entrypoints
    RfxVector<nri::ShaderDesc> sds;
    for (auto& s : impl->shader->stages) {
        if (s.stageBits & nri::StageBits::VERTEX_SHADER) {
            if (desc->vsEntryPoint && s.sourceEntryPoint != desc->vsEntryPoint)
                continue;
            sds.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
        } else if (s.stageBits & nri::StageBits::FRAGMENT_SHADER) {
            if (explicitVertex && desc->psEntryPoint == nullptr)
                continue;
            if (desc->psEntryPoint && s.sourceEntryPoint != desc->psEntryPoint)
                continue;
            sds.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
        } else if (s.stageBits & nri::StageBits::GRAPHICS_SHADERS) {
            sds.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
        }
    }

    gpd.shaders = sds.data();
    gpd.shaderNum = (uint32_t)sds.size();

    nri::VertexInputDesc vid = {};
    nri::VertexStreamDesc vs = { 0, nri::VertexStreamStepRate::PER_VERTEX };
    RfxVector<nri::VertexAttributeDesc> vads;

    // check if we actually found a vertex shader before trying to setup input layout
    bool hasVertexStage = (impl->shader->stageMask & nri::StageBits::VERTEX_SHADER);

    if (desc->vertexLayout && hasVertexStage) {
        for (int i = 0; i < desc->vertexLayoutCount; ++i) {
            const auto& el = desc->vertexLayout[i];
            nri::VertexAttributeDesc ad = {};
            ad.d3d = { el.semanticName ? el.semanticName : "POSITION", 0 };
            ad.vk = { el.location };
            ad.offset = el.offset;
            ad.format = ToNRIFormat(el.format);
            ad.streamIndex = 0;
            vads.push_back(ad);
        }
        vid.attributes = vads.data();
        vid.attributeNum = (uint8_t)vads.size();
        vid.streams = &vs;
        vid.streamNum = 1;
        gpd.vertexInput = &vid;
    }
    NRI_CHECK(CORE.NRI.CreateGraphicsPipeline(*CORE.NRIDevice, gpd, impl->pipeline));
    return impl;
}

void rfxDestroyPipeline(RfxPipeline pipeline) {
    if (!pipeline)
        return;
    RfxPipelineImpl* ptr = pipeline;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyPipeline(ptr->pipeline);
        RfxDelete(ptr);
    });
}

RfxPipeline rfxCreateComputePipeline(const RfxComputePipelineDesc* desc) {
    RfxPipelineImpl* impl = RfxNew<RfxPipelineImpl>();
    impl->shader = desc->shader;
    impl->bindPoint = nri::BindPoint::COMPUTE;

    impl->type = RfxPipelineImpl::COMPUTE;
    impl->cache = CacheComputeDesc(desc);
    {
        std::lock_guard<std::mutex> lock(CORE.HotReloadMutex);
        impl->shader->dependentPipelines.insert(impl);
    }

    nri::ComputePipelineDesc cpd = {};
    cpd.pipelineLayout = impl->shader->pipelineLayout;
    for (auto& s : impl->shader->stages) {
        if (s.stageBits & nri::StageBits::COMPUTE_SHADER) {
            if (desc->entryPoint && s.sourceEntryPoint != desc->entryPoint)
                continue;

            cpd.shader = { s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() };
            break; // got entrypoint
        }
    }
    NRI_CHECK(CORE.NRI.CreateComputePipeline(*CORE.NRIDevice, cpd, impl->pipeline));
    return impl;
}

//
// ImGui
//

bool rfxInitImGui() {
    nri::ImguiDesc desc = {};
    return CORE.NRI.CreateImgui(*CORE.NRIDevice, desc, CORE.ImguiRenderer) == nri::Result::SUCCESS;
}
void rfxShutdownImGui() {
    if (CORE.ImguiRenderer) {
        nri::Imgui* ptr = CORE.ImguiRenderer;
        CORE.ImguiRenderer = nullptr;
        rfxDeferDestruction([=]() { CORE.NRI.DestroyImgui(ptr); });
    }
}

void rfxCmdDrawImGui(RfxCommandList cmd, const RfxImGuiDrawData* data) {
    if (!CORE.ImguiRenderer || !data)
        return;

    MustTransition(cmd);

    nri::CopyImguiDataDesc copy = {};
    copy.drawLists = (const ImDrawList* const*)data->drawLists;
    copy.drawListNum = data->drawListCount;
    copy.textures = (ImTextureData* const*)data->textures;
    copy.textureNum = data->textureCount;
    CORE.NRI.CmdCopyImguiData(*cmd->nriCmd, *CORE.NRIStreamer, *CORE.ImguiRenderer, copy);

    nri::Format fmt = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].attachmentFormat;

    // restart RP
    cmd->activeColorAttachments.clear();
    nri::AttachmentDesc& colorDesc = cmd->activeColorAttachments.emplace_back();
    colorDesc.descriptor = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].colorAttachment;
    colorDesc.loadOp = nri::LoadOp::LOAD;
    colorDesc.storeOp = nri::StoreOp::STORE;
    colorDesc.resolveOp = nri::ResolveOp::AVERAGE;

    cmd->currentRenderingDesc = {};
    cmd->currentRenderingDesc.colors = cmd->activeColorAttachments.data();
    cmd->currentRenderingDesc.colorNum = 1;

    CORE.NRI.CmdBeginRendering(*cmd->nriCmd, cmd->currentRenderingDesc);
    cmd->isRendering = true;

    nri::DrawImguiDesc did = {};
    did.drawLists = (const ImDrawList* const*)data->drawLists;
    did.drawListNum = data->drawListCount;
    did.displaySize = { (nri::Dim_t)data->displayWidth, (nri::Dim_t)data->displayHeight };
    did.hdrScale = data->hdrScale;
    did.attachmentFormat = fmt;
    did.linearColor = data->linearColor;
    CORE.NRI.CmdDrawImgui(*cmd->nriCmd, *CORE.ImguiRenderer, did);

    cmd->currentPipeline = nullptr;

    CORE.NRI.CmdSetDescriptorPool(*cmd->nriCmd, *CORE.Bindless.descriptorPool);
}

RfxFormat rfxGetSwapChainFormat() {
    if (CORE.SwapChainTextures.empty()) {
        // make sure swapchain is inited
        int w = rfxGetWindowWidth();
        int h = rfxGetWindowHeight();
        if (w > 0 && h > 0)
            RecreateSwapChain(w, h);
    }
    if (CORE.SwapChainTextures.empty())
        return RFX_FORMAT_UNKNOWN;
    return ToRfxFormat(CORE.SwapChainTextures[0].attachmentFormat);
}

void rfxCmdTransitionBuffer(RfxCommandList cmd, RfxBuffer buffer, RfxResourceState state) {
    if (!buffer)
        return;

    // handle UAV->UAV barriers
    if (state == RFX_STATE_SHADER_WRITE && buffer->currentState == RFX_STATE_SHADER_WRITE) {
        nri::BufferBarrierDesc d = {};
        d.buffer = buffer->buffer;
        d.before = { nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::StageBits::ALL };
        d.after = d.before;
        cmd->barriers.bufferBarriers.push_back(d);
        return;
    }

    if (buffer->currentState == state)
        return;

    nri::AccessBits nextAccess;
    nri::Layout nextLayout;
    nri::StageBits nextStage;
    GetNRIState(state, nextAccess, nextLayout, nextStage);

    nri::BufferBarrierDesc desc = {};
    desc.buffer = buffer->buffer;
    desc.before = { buffer->currentAccess, buffer->currentStage };
    desc.after = { nextAccess, nextStage };
    cmd->barriers.bufferBarriers.push_back(desc);

    buffer->currentState = state;
    buffer->currentAccess = nextAccess;
    buffer->currentStage = nextStage;
}

void rfxCmdTransitionTexture(RfxCommandList cmd, RfxTexture texture, RfxResourceState state) {
    if (texture)
        cmd->barriers.RequireState(texture, state);
}

void* rfxGetTextureDescriptor(RfxTexture texture) {
    if (!texture)
        return nullptr;
    return (void*)texture->descriptor;
}

//
// NRD integration
//

static nrd::ResourceType ToNRDResourceType(RfxDenoiserResourceId id) {
    switch (id) {
    case RFX_DENOISER_IN_VIEWZ: return nrd::ResourceType::IN_VIEWZ;
    case RFX_DENOISER_IN_MV: return nrd::ResourceType::IN_MV;
    case RFX_DENOISER_IN_NORMAL_ROUGHNESS: return nrd::ResourceType::IN_NORMAL_ROUGHNESS;
    case RFX_DENOISER_IN_DIFF_RADIANCE: return nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST;
    case RFX_DENOISER_IN_SPEC_RADIANCE: return nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST;
    case RFX_DENOISER_IN_SHADOW_DATA: return nrd::ResourceType::IN_PENUMBRA;
    case RFX_DENOISER_OUT_DIFF_RADIANCE: return nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST;
    case RFX_DENOISER_OUT_SPEC_RADIANCE: return nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST;
    case RFX_DENOISER_OUT_SHADOW: return nrd::ResourceType::OUT_SHADOW_TRANSLUCENCY;
    case RFX_DENOISER_OUT_VALIDATION: return nrd::ResourceType::OUT_VALIDATION;
    default: return nrd::ResourceType::MAX_NUM;
    }
}

RfxDenoiser rfxCreateDenoiser(RfxDenoiserType type, int width, int height) {
    RfxDenoiserImpl* impl = RfxNew<RfxDenoiserImpl>();
    impl->type = type;
    impl->width = width;
    impl->height = height;

    // denoiser
    nrd::Denoiser nrdDenoiser;
    switch (type) {
    case RFX_DENOISER_REBLUR_DIFFUSE: nrdDenoiser = nrd::Denoiser::REBLUR_DIFFUSE; break;
    case RFX_DENOISER_REBLUR_DIFFUSE_SPECULAR: nrdDenoiser = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR; break;
    case RFX_DENOISER_RELAX_DIFFUSE: nrdDenoiser = nrd::Denoiser::RELAX_DIFFUSE; break;
    case RFX_DENOISER_RELAX_DIFFUSE_SPECULAR: nrdDenoiser = nrd::Denoiser::RELAX_DIFFUSE_SPECULAR; break;
    default: RfxDelete(impl); return nullptr;
    }

    impl->identifier = nrd::Identifier(nrdDenoiser);
    impl->denoiserDesc = { impl->identifier, nrdDenoiser };

    // creation
    nrd::InstanceCreationDesc instanceCreationDesc = {};
    instanceCreationDesc.denoisers = &impl->denoiserDesc;
    instanceCreationDesc.denoisersNum = 1;

    instanceCreationDesc.allocationCallbacks.Allocate = InternalNriAlloc;
    instanceCreationDesc.allocationCallbacks.Reallocate = InternalNriRealloc;
    instanceCreationDesc.allocationCallbacks.Free = InternalNriFree;
    instanceCreationDesc.allocationCallbacks.userArg = &g_Allocator;

    nrd::IntegrationCreationDesc integrationDesc = {};
    integrationDesc.queuedFrameNum = GetQueuedFrameNum();
    integrationDesc.resourceWidth = (uint16_t)width;
    integrationDesc.resourceHeight = (uint16_t)height;
    integrationDesc.enableWholeLifetimeDescriptorCaching = true;

    // recreate NRD
    if (impl->instance.Recreate(integrationDesc, instanceCreationDesc, CORE.NRIDevice) != nrd::Result::SUCCESS) {
        fprintf(stderr, "Failed to initialize NRD\n");
        RfxDelete(impl);
        return nullptr;
    }

    return impl;
}

void rfxDestroyDenoiser(RfxDenoiser denoiser) {
    if (!denoiser)
        return;

    RfxDenoiserImpl* ptr = denoiser;

    rfxDeferDestruction([=]() {
        ptr->instance.Destroy();
        RfxDelete(ptr);
    });
}

void rfxCmdDenoise(
    RfxCommandList cmd, RfxDenoiser denoiser, const RfxDenoiserSettings* settings, RfxTexture* resources, uint32_t resourceCount
) {
    if (!denoiser || !settings)
        return;

    if (denoiser->lastFrameIndex != CORE.FrameIndex) {
        denoiser->instance.NewFrame();
        denoiser->lastFrameIndex = CORE.FrameIndex;
    }

    MustTransition(cmd);

    cmd->barriers.Flush(*cmd->nriCmd);

    nrd::CommonSettings common = {};
    memcpy(common.viewToClipMatrix, settings->viewToClip, sizeof(float) * 16);
    memcpy(common.viewToClipMatrixPrev, settings->viewToClipPrev, sizeof(float) * 16);
    memcpy(common.worldToViewMatrix, settings->worldToView, sizeof(float) * 16);
    memcpy(common.worldToViewMatrixPrev, settings->worldToViewPrev, sizeof(float) * 16);

    common.motionVectorScale[0] = settings->motionVectorScale[0];
    common.motionVectorScale[1] = settings->motionVectorScale[1];
    common.isMotionVectorInWorldSpace = settings->isMotionVectorInWorldSpace;

    common.cameraJitter[0] = settings->jitter[0];
    common.cameraJitter[1] = settings->jitter[1];
    common.cameraJitterPrev[0] = settings->jitterPrev[0];
    common.cameraJitterPrev[1] = settings->jitterPrev[1];

    common.resourceSize[0] = (uint16_t)denoiser->width;
    common.resourceSize[1] = (uint16_t)denoiser->height;
    common.resourceSizePrev[0] = (uint16_t)denoiser->width;
    common.resourceSizePrev[1] = (uint16_t)denoiser->height;

    common.rectSize[0] = (uint16_t)denoiser->width;
    common.rectSize[1] = (uint16_t)denoiser->height;
    common.rectSizePrev[0] = (uint16_t)denoiser->width;
    common.rectSizePrev[1] = (uint16_t)denoiser->height;

    common.frameIndex = settings->frameIndex;
    common.accumulationMode = settings->resetHistory ? nrd::AccumulationMode::CLEAR_AND_RESTART : nrd::AccumulationMode::CONTINUE;

    float perspectiveX = settings->viewToClip[0]; // proj.Elements[0][0]
    float perspectiveY = settings->viewToClip[5]; // proj.Elements[1][1]

    common.denoisingRange = settings->denoisingRange;
    common.viewZScale = settings->viewZScale;
    common.disocclusionThreshold = settings->disocclusionThreshold;
    common.enableValidation = settings->enableValidation;

    denoiser->instance.SetCommonSettings(common);

    if (denoiser->type == RFX_DENOISER_REBLUR_DIFFUSE_SPECULAR || denoiser->type == RFX_DENOISER_REBLUR_DIFFUSE) {
        nrd::ReblurSettings reblurSettings = {};

        reblurSettings.maxBlurRadius = 15.0f;
        reblurSettings.minBlurRadius = 0.5f;
        reblurSettings.hitDistanceParameters.A = 0.1f;

        denoiser->instance.SetDenoiserSettings(denoiser->identifier, &reblurSettings);
    } else if (denoiser->type == RFX_DENOISER_RELAX_DIFFUSE_SPECULAR || denoiser->type == RFX_DENOISER_RELAX_DIFFUSE) {
        nrd::RelaxSettings s = {};
        denoiser->instance.SetDenoiserSettings(denoiser->identifier, &s);
    }

    nrd::ResourceSnapshot snapshot = {};
    snapshot.restoreInitialState = false;

    for (uint32_t i = 0; i < resourceCount; ++i) {
        if (!resources[i])
            continue;
        nrd::ResourceType nrdType = ToNRDResourceType((RfxDenoiserResourceId)i);
        if (nrdType == nrd::ResourceType::MAX_NUM)
            continue;

        RfxTexture texture = resources[i];
        nrd::Resource resource = {};
        resource.nri.texture = texture->texture;

        RfxResourceState st = texture->state->Get(texture->mipOffset, texture->layerOffset);
        nri::AccessBits acc;
        nri::Layout lay;
        nri::StageBits stg;
        GetNRIState(st, acc, lay, stg);

        resource.state = { acc, lay, stg };
        resource.userArg = texture;

        snapshot.SetResource(nrdType, resource);
    }

    denoiser->instance.Denoise(&denoiser->identifier, 1, *cmd->nriCmd, snapshot);
    CORE.NRI.CmdSetDescriptorPool(*cmd->nriCmd, *CORE.Bindless.descriptorPool);
    cmd->currentPipeline = nullptr;

    // sync state after NRD messed with it
    for (uint32_t i = 0; i < snapshot.uniqueNum; i++) {
        const nrd::Resource& res = snapshot.unique[i];
        RfxTextureImpl* texture = (RfxTextureImpl*)res.userArg;
        if (texture) {
            RfxResourceState newState = RFX_STATE_UNDEFINED;
            if (res.state.layout == nri::Layout::SHADER_RESOURCE)
                newState = RFX_STATE_SHADER_READ;
            else if (res.state.layout == nri::Layout::SHADER_RESOURCE_STORAGE)
                newState = RFX_STATE_SHADER_WRITE;

            for (uint32_t l = 0; l < texture->layerNum; ++l) {
                for (uint32_t m = 0; m < texture->mipNum; ++m) {
                    texture->state->Set(texture->mipOffset + m, texture->layerOffset + l, newState);
                }
            }
        }
    }
}

void rfxCmdBeginEvent(RfxCommandList cmd, const char* name) {
    CORE.NRI.CmdBeginAnnotation(*cmd->nriCmd, name, 0);
}

void rfxCmdEndEvent(RfxCommandList cmd) {
    CORE.NRI.CmdEndAnnotation(*cmd->nriCmd);
}

void rfxCmdMarker(RfxCommandList cmd, const char* name) {
    CORE.NRI.CmdAnnotation(*cmd->nriCmd, name, 0);
}

void rfxBeginMarker(const char* name) {
    nri::nriBeginAnnotation(name, 0);
}

void rfxEndMarker() {
    nri::nriEndAnnotation();
}

void rfxMarker(const char* name) {
    nri::nriAnnotation(name, 0);
}

void rfxCmdBeginProfile(RfxCommandList cmd, const char* name) {
    uint32_t frameIdx = CORE.FrameIndex % GetQueuedFrameNum();
    QueuedFrame& qf = CORE.QueuedFrames[frameIdx];

    if (qf.queryCount + 2 > RFX_MAX_TIMESTAMP_QUERIES)
        return;

    uint32_t qIdx = qf.queryCount++;
    uint32_t globalIdx = frameIdx * RFX_MAX_TIMESTAMP_QUERIES + qIdx;

    ProfileRegion region = {};
    region.name = name;
    region.startIndex = qIdx;
    region.endIndex = 0;
    region.parentIndex = qf.profileStack.empty() ? -1 : qf.profileStack.back();

    qf.profileStack.push_back((int)qf.profileRegions.size());
    qf.profileRegions.push_back(region);

    CORE.NRI.CmdEndQuery(*cmd->nriCmd, *CORE.TimestampPool, globalIdx);
}

void rfxCmdEndProfile(RfxCommandList cmd) {
    uint32_t frameIdx = CORE.FrameIndex % GetQueuedFrameNum();
    QueuedFrame& qf = CORE.QueuedFrames[frameIdx];

    if (qf.profileStack.empty())
        return;
    if (qf.queryCount >= RFX_MAX_TIMESTAMP_QUERIES)
        return;

    int regionIdx = qf.profileStack.back();
    qf.profileStack.pop_back();

    uint32_t qIdx = qf.queryCount++;
    uint32_t globalIdx = frameIdx * RFX_MAX_TIMESTAMP_QUERIES + qIdx;

    qf.profileRegions[regionIdx].endIndex = qIdx;

    CORE.NRI.CmdEndQuery(*cmd->nriCmd, *CORE.TimestampPool, globalIdx);
}

uint32_t rfxGetGpuTimestamps(RfxGpuTimestamp* outTimestamps, uint32_t maxCount) {
    uint32_t count = (uint32_t)CORE.LastFrameTimestamps.size();
    if (count > maxCount)
        count = maxCount;
    if (count > 0)
        memcpy(outTimestamps, CORE.LastFrameTimestamps.data(), count * sizeof(RfxGpuTimestamp));
    return count;
}

RfxAccelerationStructure rfxCreateAccelerationStructure(const RfxAccelerationStructureDesc* desc) {
    RfxAccelerationStructureImpl* impl = RfxNew<RfxAccelerationStructureImpl>();
    bool isTLAS = (desc->type == RFX_AS_TOP_LEVEL);
    impl->bindlessIndex = isTLAS ? AllocASSlot() : 0;
    impl->descriptor = nullptr;

    impl->nriDesc = {};
    impl->nriDesc.type = isTLAS ? nri::AccelerationStructureType::TOP_LEVEL : nri::AccelerationStructureType::BOTTOM_LEVEL;
    impl->nriDesc.flags = (nri::AccelerationStructureBits)desc->flags;
    impl->nriDesc.geometryOrInstanceNum = desc->count;

    if (!isTLAS && desc->geometries) {
        impl->geometries.reserve(desc->count);
        impl->micromapDescs.reserve(desc->count);

        for (uint32_t i = 0; i < desc->count; ++i) {
            const RfxGeometryDesc& src = desc->geometries[i];
            nri::BottomLevelGeometryDesc& dst = impl->geometries.emplace_back();

            nri::BottomLevelGeometryBits geoFlags =
                src.opaque ? nri::BottomLevelGeometryBits::OPAQUE_GEOMETRY : nri::BottomLevelGeometryBits::NONE;
            dst.flags = geoFlags;

            if (!src.isAABB) {
                dst.type = nri::BottomLevelGeometryType::TRIANGLES;
                dst.triangles.vertexBuffer = src.triangles.vertexBuffer->buffer;
                dst.triangles.vertexOffset = src.triangles.vertexOffset;
                dst.triangles.vertexNum = src.triangles.vertexCount;
                dst.triangles.vertexStride = (uint16_t)src.triangles.vertexStride;
                dst.triangles.vertexFormat = ToNRIFormat(src.triangles.vertexFormat);

                dst.triangles.indexBuffer = src.triangles.indexBuffer ? src.triangles.indexBuffer->buffer : nullptr;
                dst.triangles.indexOffset = src.triangles.indexBuffer ? src.triangles.indexOffset : 0;
                dst.triangles.indexNum = src.triangles.indexBuffer ? src.triangles.indexCount : 0;
                dst.triangles.indexType = (src.triangles.indexType == RFX_INDEX_UINT32) ? nri::IndexType::UINT32 : nri::IndexType::UINT16;

                dst.triangles.transformBuffer = src.triangles.transformBuffer ? src.triangles.transformBuffer->buffer : nullptr;
                dst.triangles.transformOffset = src.triangles.transformBuffer ? src.triangles.transformOffset : 0;

                if (src.triangles.micromap) {
                    nri::BottomLevelMicromapDesc& blmd = impl->micromapDescs.emplace_back();
                    blmd.micromap = src.triangles.micromap->micromap;
                    blmd.indexBuffer = src.triangles.micromapIndexBuffer ? src.triangles.micromapIndexBuffer->buffer : nullptr;
                    blmd.indexOffset = src.triangles.micromapIndexOffset;
                    blmd.indexType =
                        (src.triangles.micromapIndexType == RFX_INDEX_UINT32) ? nri::IndexType::UINT32 : nri::IndexType::UINT16;
                    blmd.baseTriangle = src.triangles.micromapBaseTriangle;

                    dst.triangles.micromap = &blmd;
                } else {
                    dst.triangles.micromap = nullptr;
                }
            } else {
                dst.type = nri::BottomLevelGeometryType::AABBS;
                dst.aabbs.buffer = src.aabbs.aabbBuffer->buffer;
                dst.aabbs.offset = src.aabbs.offset;
                dst.aabbs.num = src.aabbs.count;
                dst.aabbs.stride = src.aabbs.stride;
            }
        }
        impl->nriDesc.geometries = impl->geometries.data();
    }

    NRI_CHECK(CORE.NRI.CreateAccelerationStructure(*CORE.NRIDevice, impl->nriDesc, impl->as));

    nri::MemoryDesc memDesc = {};
    CORE.NRI.GetAccelerationStructureMemoryDesc(*impl->as, nri::MemoryLocation::DEVICE, memDesc);
    nri::AllocateMemoryDesc allocDesc = { memDesc.size, memDesc.type, 0.0f, { true, 0 }, false };
    NRI_CHECK(CORE.NRI.AllocateMemory(*CORE.NRIDevice, allocDesc, impl->memory));

    const nri::BindAccelerationStructureMemoryDesc bind = { impl->as, impl->memory, 0 };
    NRI_CHECK(CORE.NRI.BindAccelerationStructureMemory(&bind, 1));

    if (isTLAS) {
        NRI_CHECK(CORE.NRI.CreateAccelerationStructureDescriptor(*impl->as, impl->descriptor));
        nri::UpdateDescriptorRangeDesc update = {};
        update.descriptorSet = CORE.Bindless.globalDescriptorSet;
        update.rangeIndex = 5;
        update.baseDescriptor = impl->bindlessIndex;
        update.descriptorNum = 1;
        update.descriptors = &impl->descriptor;
        CORE.NRI.UpdateDescriptorRanges(&update, 1);
    }

    return impl;
}

void rfxDestroyAccelerationStructure(RfxAccelerationStructure as) {
    if (!as)
        return;
    if (as->descriptor)
        FreeASSlot(as->bindlessIndex);
    rfxDeferDestruction([=]() {
        if (as->descriptor)
            CORE.NRI.DestroyDescriptor(as->descriptor);
        CORE.NRI.DestroyAccelerationStructure(as->as);
        CORE.NRI.FreeMemory(as->memory);
        RfxDelete(as);
    });
}

uint32_t rfxGetAccelerationStructureId(RfxAccelerationStructure as) {
    return as ? as->bindlessIndex : 0;
}
uint64_t rfxGetAccelerationStructureScratchSize(RfxAccelerationStructure as) {
    return as ? CORE.NRI.GetAccelerationStructureBuildScratchBufferSize(*as->as) : 0;
}

void rfxCmdUploadInstances(RfxCommandList cmd, RfxBuffer dstBuffer, const RfxInstance* instances, uint32_t instanceCount) {
    RfxVector<nri::TopLevelInstance> nriInstances(instanceCount);
    for (uint32_t i = 0; i < instanceCount; ++i) {
        memcpy(nriInstances[i].transform, instances[i].transform, sizeof(float) * 12);
        nriInstances[i].instanceId = instances[i].instanceId;
        nriInstances[i].mask = instances[i].mask;
        nriInstances[i].shaderBindingTableLocalOffset = instances[i].instanceContributionToHitGroupIndex;
        nriInstances[i].flags = (nri::TopLevelInstanceBits)instances[i].flags;
        nriInstances[i].accelerationStructureHandle = CORE.NRI.GetAccelerationStructureHandle(*instances[i].blas->as);
    }

    rfxCmdTransitionBuffer(cmd, dstBuffer, RFX_STATE_COPY_DST);
    cmd->barriers.Flush(*cmd->nriCmd);

    nri::DataSize chunk = { nriInstances.data(), instanceCount * sizeof(nri::TopLevelInstance) };
    nri::StreamBufferDataDesc sbd = {};
    sbd.dstBuffer = dstBuffer->buffer;
    sbd.dstOffset = 0;
    sbd.dataChunks = &chunk;
    sbd.dataChunkNum = 1;
    CORE.NRI.StreamBufferData(*CORE.NRIStreamer, sbd);
    CORE.NRI.CmdCopyStreamedData(*cmd->nriCmd, *CORE.NRIStreamer);

    nri::BufferBarrierDesc bbd = {};
    bbd.buffer = dstBuffer->buffer;
    bbd.before = { nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY };
    bbd.after = { nri::AccessBits::SHADER_RESOURCE, nri::StageBits::ACCELERATION_STRUCTURE };
    nri::BarrierDesc bd = {};
    bd.buffers = &bbd;
    bd.bufferNum = 1;
    CORE.NRI.CmdBarrier(*cmd->nriCmd, bd);

    dstBuffer->currentAccess = nri::AccessBits::SHADER_RESOURCE;
    dstBuffer->currentStage = nri::StageBits::ACCELERATION_STRUCTURE;
}

void rfxCmdBuildAccelerationStructure(RfxCommandList cmd, RfxAccelerationStructure dst, RfxBuffer scratch, RfxBuffer instanceBuffer) {
    MustTransition(cmd);

    RfxAccelerationStructureImpl* dstImpl = (RfxAccelerationStructureImpl*)dst;

    cmd->barriers.RequireState(scratch, RFX_STATE_SCRATCH_BUFFER);

    if (dstImpl->nriDesc.type == nri::AccelerationStructureType::TOP_LEVEL && instanceBuffer) {
        cmd->barriers.RequireState(instanceBuffer, RFX_STATE_SHADER_READ);
    }

    // AS->write
    TransitionAS(cmd, dstImpl, nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::ACCELERATION_STRUCTURE);

    cmd->FlushBarriers();

    // build
    if (dstImpl->nriDesc.type == nri::AccelerationStructureType::BOTTOM_LEVEL) {
        nri::BuildBottomLevelAccelerationStructureDesc build = {};
        build.dst = dstImpl->as;
        build.geometries = dstImpl->geometries.data();
        build.geometryNum = (uint32_t)dstImpl->geometries.size();
        build.scratchBuffer = scratch->buffer;
        CORE.NRI.CmdBuildBottomLevelAccelerationStructures(*cmd->nriCmd, &build, 1);
    } else {
        nri::BuildTopLevelAccelerationStructureDesc build = {};
        build.dst = dstImpl->as;
        build.instanceBuffer = instanceBuffer ? instanceBuffer->buffer : nullptr;
        build.instanceNum = dstImpl->nriDesc.geometryOrInstanceNum;
        build.scratchBuffer = scratch->buffer;
        CORE.NRI.CmdBuildTopLevelAccelerationStructures(*cmd->nriCmd, &build, 1);
    }

    // TODO: build->trace for now
    TransitionAS(
        cmd, dstImpl, nri::AccessBits::ACCELERATION_STRUCTURE_READ | nri::AccessBits::SHADER_RESOURCE, nri::StageBits::RAY_TRACING_SHADERS
    );
    cmd->FlushBarriers();
}

RfxPipeline rfxCreateRayTracingPipeline(const RfxRayTracingPipelineDesc* desc) {
    RfxPipelineImpl* impl = RfxNew<RfxPipelineImpl>();
    impl->shader = (RfxShaderImpl*)desc->shader;
    impl->bindPoint = nri::BindPoint::RAY_TRACING;
    impl->shaderGroupCount = desc->groupCount;

    impl->type = RfxPipelineImpl::RAY_TRACING;
    impl->cache = CacheRTDesc(desc);
    {
        std::lock_guard<std::mutex> lock(CORE.HotReloadMutex);
        impl->shader->dependentPipelines.insert(impl);
    }

    nri::StageBits rtMask = nri::StageBits::RAYGEN_SHADER | nri::StageBits::ANY_HIT_SHADER | nri::StageBits::CLOSEST_HIT_SHADER |
                            nri::StageBits::MISS_SHADER | nri::StageBits::INTERSECTION_SHADER | nri::StageBits::CALLABLE_SHADER;

    RfxVector<nri::ShaderDesc> stageDescs;
    RfxVector<uint32_t> stageToLibraryIndex(impl->shader->stages.size(), 0);

    for (size_t i = 0; i < impl->shader->stages.size(); ++i) {
        const auto& s = impl->shader->stages[i];
        if ((s.stageBits & rtMask) != 0) {
            stageDescs.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
            stageToLibraryIndex[i] = (uint32_t)stageDescs.size();
        }
    }

    nri::ShaderLibraryDesc library = {};
    library.shaders = stageDescs.data();
    library.shaderNum = (uint32_t)stageDescs.size();

    auto FindLibraryIndex = [&](const char* name) -> uint32_t {
        if (!name)
            return 0;
        for (size_t i = 0; i < impl->shader->stages.size(); ++i) {
            if (impl->shader->stages[i].sourceEntryPoint == name)
                return stageToLibraryIndex[i];
        }
        return 0;
    };

    RfxVector<nri::ShaderGroupDesc> groups(desc->groupCount);
    for (uint32_t i = 0; i < desc->groupCount; ++i) {
        const auto& src = desc->groups[i];
        if (src.type == RFX_SHADER_GROUP_GENERAL) {
            groups[i].shaderIndices[0] = FindLibraryIndex(src.generalShader);
        } else if (src.type == RFX_SHADER_GROUP_TRIANGLES) {
            groups[i].shaderIndices[0] = FindLibraryIndex(src.closestHitShader);
            groups[i].shaderIndices[1] = FindLibraryIndex(src.anyHitShader);
        } else if (src.type == RFX_SHADER_GROUP_PROCEDURAL) {
            groups[i].shaderIndices[0] = FindLibraryIndex(src.closestHitShader);
            groups[i].shaderIndices[1] = FindLibraryIndex(src.anyHitShader);
            groups[i].shaderIndices[2] = FindLibraryIndex(src.intersectionShader);
        }
    }

    nri::RayTracingPipelineDesc rtp = {};
    rtp.pipelineLayout = impl->shader->pipelineLayout;
    rtp.shaderLibrary = &library;
    rtp.shaderGroups = groups.data();
    rtp.shaderGroupNum = (uint32_t)groups.size();
    rtp.recursionMaxDepth = desc->maxRecursionDepth;
    rtp.rayPayloadMaxSize = desc->maxPayloadSize;
    rtp.rayHitAttributeMaxSize = desc->maxAttributeSize;

    rtp.flags = nri::RayTracingPipelineBits::NONE;
    if (desc->flags & RFX_RT_PIPELINE_SKIP_TRIANGLES)
        rtp.flags |= nri::RayTracingPipelineBits::SKIP_TRIANGLES;
    if (desc->flags & RFX_RT_PIPELINE_SKIP_AABBS)
        rtp.flags |= nri::RayTracingPipelineBits::SKIP_AABBS;
    if (desc->flags & RFX_RT_PIPELINE_ALLOW_MICROMAPS)
        rtp.flags |= nri::RayTracingPipelineBits::ALLOW_MICROMAPS;

    NRI_CHECK(CORE.NRI.CreateRayTracingPipeline(*CORE.NRIDevice, rtp, impl->pipeline));
    return impl;
}

RfxShaderBindingTable rfxCreateShaderBindingTable(RfxPipeline pipeline) {
    RfxPipelineImpl* pipelineImpl = (RfxPipelineImpl*)pipeline;
    RfxShaderBindingTableImpl* impl = RfxNew<RfxShaderBindingTableImpl>();

    const nri::DeviceDesc& dev = CORE.NRI.GetDeviceDesc(*CORE.NRIDevice);
    uint64_t identifierSize = dev.shaderStage.rayTracing.shaderGroupIdentifierSize;
    uint64_t tableAlign = dev.memoryAlignment.shaderBindingTable;

    uint32_t groupCount = pipelineImpl->shaderGroupCount;
    impl->stride = Align(identifierSize, tableAlign);
    impl->size = impl->stride * groupCount;

    nri::BufferDesc bd = { impl->size, 0, nri::BufferUsageBits::SHADER_BINDING_TABLE | nri::BufferUsageBits::SHADER_RESOURCE };
    NRI_CHECK(CORE.NRI.CreateBuffer(*CORE.NRIDevice, bd, impl->buffer));

    nri::MemoryDesc md = {};
    CORE.NRI.GetBufferMemoryDesc(*impl->buffer, nri::MemoryLocation::DEVICE, md);
    nri::AllocateMemoryDesc amd = {};
    amd.size = md.size;
    amd.type = md.type;
    amd.vma.enable = true;
    NRI_CHECK(CORE.NRI.AllocateMemory(*CORE.NRIDevice, amd, impl->memory));
    nri::BindBufferMemoryDesc bmd = { impl->buffer, impl->memory, 0 };
    NRI_CHECK(CORE.NRI.BindBufferMemory(&bmd, 1));

    RfxVector<uint8_t> rawIds(groupCount * identifierSize);
    CORE.NRI.WriteShaderGroupIdentifiers(*pipelineImpl->pipeline, 0, groupCount, rawIds.data());

    RfxVector<uint8_t> alignedData(impl->size);
    for (uint32_t i = 0; i < groupCount; ++i) {
        memcpy(alignedData.data() + (i * impl->stride), rawIds.data() + (i * identifierSize), identifierSize);
    }

    nri::CommandAllocator* allocator;
    nri::CommandBuffer* cmd;
    CORE.NRI.CreateCommandAllocator(*CORE.NRIGraphicsQueue, allocator);
    CORE.NRI.CreateCommandBuffer(*allocator, cmd);
    CORE.NRI.BeginCommandBuffer(*cmd, nullptr);

    nri::BufferBarrierDesc pre = {};
    pre.buffer = impl->buffer;
    pre.before = { nri::AccessBits::NONE, nri::StageBits::NONE };
    pre.after = { nri::AccessBits::COPY_DESTINATION, nri::StageBits::COPY };
    nri::BarrierDesc bd1 = {};
    bd1.buffers = &pre;
    bd1.bufferNum = 1;
    CORE.NRI.CmdBarrier(*cmd, bd1);

    nri::DataSize chunk = { alignedData.data(), impl->size };
    nri::StreamBufferDataDesc sbd = {};
    sbd.dstBuffer = impl->buffer;
    sbd.dataChunks = &chunk;
    sbd.dataChunkNum = 1;
    CORE.NRI.StreamBufferData(*CORE.NRIStreamer, sbd);
    CORE.NRI.CmdCopyStreamedData(*cmd, *CORE.NRIStreamer);

    nri::BufferBarrierDesc post = pre;
    post.before = pre.after;
    post.after = { nri::AccessBits::SHADER_BINDING_TABLE, nri::StageBits::RAY_TRACING_SHADERS };
    nri::BarrierDesc bd2 = {};
    bd2.buffers = &post;
    bd2.bufferNum = 1;
    CORE.NRI.CmdBarrier(*cmd, bd2);

    CORE.NRI.EndCommandBuffer(*cmd);
    nri::QueueSubmitDesc sub = {};
    sub.commandBuffers = &cmd;
    sub.commandBufferNum = 1;
    CORE.NRI.QueueSubmit(*CORE.NRIGraphicsQueue, sub);
    CORE.NRI.QueueWaitIdle(CORE.NRIGraphicsQueue);
    CORE.NRI.DestroyCommandBuffer(cmd);
    CORE.NRI.DestroyCommandAllocator(allocator);

    return impl;
}

void rfxDestroyShaderBindingTable(RfxShaderBindingTable sbt) {
    if (!sbt)
        return;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyBuffer(sbt->buffer);
        CORE.NRI.FreeMemory(sbt->memory);
        RfxDelete(sbt);
    });
}

void rfxCmdTraceRays(RfxCommandList cmd, const RfxTraceRaysDesc* desc, uint32_t width, uint32_t height, uint32_t depth) {
    MustTransition(cmd);

    // TODO: we kinda want to know the state of the tlas to transition correctly, not implemented for now ...

    cmd->FlushBarriers();

    RfxShaderBindingTableImpl* sbt = desc->sbt;
    uint64_t stride = sbt->stride;

    nri::DispatchRaysDesc d = {};
    d.raygenShader = { sbt->buffer, desc->rayGenIndex * stride, stride, stride };
    if (desc->missCount > 0)
        d.missShaders = { sbt->buffer, desc->missIndex * stride, desc->missCount * stride, stride };
    if (desc->hitCount > 0)
        d.hitShaderGroups = { sbt->buffer, desc->hitIndex * stride, desc->hitCount * stride, stride };
    if (desc->callableCount > 0)
        d.callableShaders = { sbt->buffer, desc->callableIndex * stride, desc->callableCount * stride, stride };
    d.x = width;
    d.y = height;
    d.z = depth;

    CORE.NRI.CmdDispatchRays(*cmd->nriCmd, d);
}

void rfxCmdDispatchRaysIndirect(RfxCommandList cmd, RfxBuffer argsBuffer, uint64_t argsOffset) {
    MustTransition(cmd);
    rfxCmdTransitionBuffer(cmd, argsBuffer, RFX_STATE_INDIRECT_ARGUMENT);
    cmd->FlushBarriers();
    CORE.NRI.CmdDispatchRaysIndirect(*cmd->nriCmd, *argsBuffer->buffer, argsOffset);
}

RfxMicromap rfxCreateMicromap(const RfxMicromapDesc* desc) {
    RfxMicromapImpl* impl = RfxNew<RfxMicromapImpl>();

    RfxVector<nri::MicromapUsageDesc> usages(desc->usageCount);
    for (uint32_t i = 0; i < desc->usageCount; ++i) {
        usages[i].triangleNum = desc->usages[i].count;
        usages[i].subdivisionLevel = desc->usages[i].subdivisionLevel;
        usages[i].format = ToNRIMicromapFormat(desc->usages[i].format);
    }

    nri::MicromapDesc md = {};
    md.usages = usages.data();
    md.usageNum = (uint32_t)usages.size();
    md.flags = ToNRIMicromapBits(desc->flags);

    NRI_CHECK(CORE.NRI.CreateMicromap(*CORE.NRIDevice, md, impl->micromap));

    AllocateAndBind<nri::Micromap, nri::BindMicromapMemoryDesc>(
        impl->micromap, nri::MemoryLocation::DEVICE, impl->memory,
        [&](nri::Micromap& m, nri::MemoryLocation l, nri::MemoryDesc& d) { CORE.NRI.GetMicromapMemoryDesc(m, l, d); },
        [&](const nri::BindMicromapMemoryDesc* d, uint32_t n) { return CORE.NRI.BindMicromapMemory(d, n); }
    );

    impl->barrierBuffer = CORE.NRI.GetMicromapBuffer(*impl->micromap);

    return impl;
}

void rfxDestroyMicromap(RfxMicromap micromap) {
    if (!micromap)
        return;
    RfxMicromapImpl* ptr = micromap;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyMicromap(ptr->micromap);
        CORE.NRI.FreeMemory(ptr->memory);
        RfxDelete(ptr);
    });
}

uint64_t rfxGetMicromapScratchSize(RfxMicromap micromap) {
    return micromap ? CORE.NRI.GetMicromapBuildScratchBufferSize(*micromap->micromap) : 0;
}

void rfxCmdBuildMicromaps(RfxCommandList cmd, const RfxBuildMicromapDesc* desc) {
    MustTransition(cmd);

    // transition inputs
    if (desc->data)
        cmd->barriers.RequireState(desc->data, RFX_STATE_SHADER_READ);
    if (desc->triangleIndices)
        cmd->barriers.RequireState(desc->triangleIndices, RFX_STATE_SHADER_READ);
    if (desc->scratch)
        cmd->barriers.RequireState(desc->scratch, RFX_STATE_SCRATCH_BUFFER); // [FIX]

    // transition dst
    RfxMicromapImpl* dst = desc->dst;
    if (dst->currentAccess != nri::AccessBits::MICROMAP_WRITE || dst->currentStage != nri::StageBits::MICROMAP) {
        nri::BufferBarrierDesc bbd = {};
        bbd.buffer = dst->barrierBuffer;
        bbd.before = { dst->currentAccess, dst->currentStage };
        bbd.after = { nri::AccessBits::MICROMAP_WRITE, nri::StageBits::MICROMAP };
        cmd->barriers.bufferBarriers.push_back(bbd);

        dst->currentAccess = nri::AccessBits::MICROMAP_WRITE;
        dst->currentStage = nri::StageBits::MICROMAP;
    }

    cmd->FlushBarriers();

    // build
    nri::BuildMicromapDesc buildDesc = {};
    buildDesc.dst = dst->micromap;
    buildDesc.dataBuffer = desc->data ? desc->data->buffer : nullptr;
    buildDesc.dataOffset = desc->dataOffset;
    buildDesc.triangleBuffer = desc->triangleIndices ? desc->triangleIndices->buffer : nullptr;
    buildDesc.triangleOffset = desc->triangleIndicesOffset;
    buildDesc.scratchBuffer = desc->scratch ? desc->scratch->buffer : nullptr;
    buildDesc.scratchOffset = desc->scratchOffset;

    CORE.NRI.CmdBuildMicromaps(*cmd->nriCmd, &buildDesc, 1);

    // transition dst to read
    {
        nri::BufferBarrierDesc bbd = {};
        bbd.buffer = dst->barrierBuffer;
        bbd.before = { nri::AccessBits::MICROMAP_WRITE, nri::StageBits::MICROMAP };
        bbd.after = { nri::AccessBits::MICROMAP_READ, nri::StageBits::ACCELERATION_STRUCTURE };

        cmd->barriers.bufferBarriers.push_back(bbd);
        cmd->FlushBarriers();

        dst->currentAccess = nri::AccessBits::MICROMAP_READ;
        dst->currentStage = nri::StageBits::ACCELERATION_STRUCTURE;
    }
}

bool rfxIsUpscalerSupported(RfxUpscalerType type) {
    return CORE.NRI.IsUpscalerSupported(*CORE.NRIDevice, ToNRIUpscalerType(type));
}

RfxUpscaler rfxCreateUpscaler(const RfxUpscalerDesc* desc) {
    RfxUpscalerImpl* impl = RfxNew<RfxUpscalerImpl>();
    impl->type = desc->type;

    nri::UpscalerDesc ud = {};
    ud.upscaleResolution = { (nri::Dim_t)desc->outputWidth, (nri::Dim_t)desc->outputHeight };
    ud.type = ToNRIUpscalerType(desc->type);
    ud.mode = ToNRIUpscalerMode(desc->mode);
    ud.flags = ToNRIUpscalerBits(desc->flags);
    ud.preset = desc->preset;
    ud.commandBuffer = nullptr;

    if (CORE.NRI.CreateUpscaler(*CORE.NRIDevice, ud, impl->upscaler) != nri::Result::SUCCESS) {
        RfxDelete(impl);
        return nullptr;
    }

    return impl;
}

static void SetupUpscalerResource(RfxCommandList cmd, RfxTexture tex, nri::UpscalerResource& outRes, bool isStorage) {
    if (!tex) {
        outRes = {};
        return;
    }

    if (isStorage) {
        // output needs to be UAV
        rfxCmdTransitionTexture(cmd, tex, RFX_STATE_SHADER_WRITE);
        outRes.descriptor = tex->descriptorUAV;
        RFX_ASSERT(outRes.descriptor != nullptr && "Texture usage must include RFX_TEXTURE_USAGE_STORAGE for upscaler output");
    } else {
        // input need to be SRV
        rfxCmdTransitionTexture(cmd, tex, RFX_STATE_SHADER_READ);
        outRes.descriptor = tex->descriptor;
        RFX_ASSERT(outRes.descriptor != nullptr && "Texture usage must include RFX_TEXTURE_USAGE_SHADER_RESOURCE for upscaler inputs");
    }

    outRes.texture = tex->texture;
}

void rfxCmdUpscale(RfxCommandList cmd, RfxUpscaler upscaler, const RfxUpscaleDesc* desc) {
    if (!upscaler || !desc)
        return;

    MustTransition(cmd);

    nri::DispatchUpscaleDesc dud = {};

    SetupUpscalerResource(cmd, desc->input, dud.input, false);
    SetupUpscalerResource(cmd, desc->output, dud.output, true);

    cmd->barriers.Flush(*cmd->nriCmd);

    // guides
    if (upscaler->type == RFX_UPSCALER_DLRR) {
        SetupUpscalerResource(cmd, desc->depth, dud.guides.denoiser.depth, false);
        SetupUpscalerResource(cmd, desc->motionVectors, dud.guides.denoiser.mv, false);
        SetupUpscalerResource(cmd, desc->exposure, dud.guides.denoiser.exposure, false);
        SetupUpscalerResource(cmd, desc->reactive, dud.guides.denoiser.reactive, false);

        SetupUpscalerResource(cmd, desc->normalRoughness, dud.guides.denoiser.normalRoughness, false);
        SetupUpscalerResource(cmd, desc->diffuseAlbedo, dud.guides.denoiser.diffuseAlbedo, false);
        SetupUpscalerResource(cmd, desc->specularAlbedo, dud.guides.denoiser.specularAlbedo, false);
        SetupUpscalerResource(cmd, desc->specularMvOrHitT, dud.guides.denoiser.specularMvOrHitT, false);
        SetupUpscalerResource(cmd, desc->sss, dud.guides.denoiser.sss, false);
    } else {
        SetupUpscalerResource(cmd, desc->depth, dud.guides.upscaler.depth, false);
        SetupUpscalerResource(cmd, desc->motionVectors, dud.guides.upscaler.mv, false);
        SetupUpscalerResource(cmd, desc->exposure, dud.guides.upscaler.exposure, false);
        SetupUpscalerResource(cmd, desc->reactive, dud.guides.upscaler.reactive, false);
    }

    cmd->barriers.Flush(*cmd->nriCmd);

    // params
    dud.currentResolution = { (nri::Dim_t)desc->input->width, (nri::Dim_t)desc->input->height };
    dud.cameraJitter = { desc->jitter[0], desc->jitter[1] };
    dud.mvScale = { desc->motionVectorScale[0], desc->motionVectorScale[1] };
    dud.flags = ToNRIUpscaleDispatchBits(desc->dispatchFlags);

    if (upscaler->type == RFX_UPSCALER_NIS) {
        dud.settings.nis.sharpness = desc->sharpness;
    } else if (upscaler->type == RFX_UPSCALER_FSR) {
        dud.settings.fsr.sharpness = desc->sharpness;
        dud.settings.fsr.zNear = desc->zNear;
        dud.settings.fsr.zFar = desc->zFar;
        dud.settings.fsr.verticalFov = desc->verticalFov;
        dud.settings.fsr.frameTime = CORE.DeltaTime * 1000.0f; // s to ms
        dud.settings.fsr.viewSpaceToMetersFactor = (desc->viewSpaceToMetersFactor > 0.0f) ? desc->viewSpaceToMetersFactor : 1.0f;
    } else if (upscaler->type == RFX_UPSCALER_DLRR) {
        memcpy(dud.settings.dlrr.viewToClipMatrix, desc->viewToClip, sizeof(float) * 16);
        memcpy(dud.settings.dlrr.worldToViewMatrix, desc->worldToView, sizeof(float) * 16);
    }

    // dispatch
    CORE.NRI.CmdDispatchUpscale(*cmd->nriCmd, *upscaler->upscaler, dud);

    // restore state
    CORE.NRI.CmdSetDescriptorPool(*cmd->nriCmd, *CORE.Bindless.descriptorPool);
    cmd->currentPipeline = nullptr;
}

void rfxDestroyUpscaler(RfxUpscaler upscaler) {
    if (!upscaler)
        return;
    RfxUpscalerImpl* ptr = upscaler;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyUpscaler(ptr->upscaler);
        RfxDelete(ptr);
    });
}

void rfxGetUpscalerProps(RfxUpscaler upscaler, RfxUpscalerProps* outProps) {
    if (!upscaler || !outProps)
        return;
    nri::UpscalerProps props = {};
    CORE.NRI.GetUpscalerProps(*upscaler->upscaler, props);

    outProps->scalingFactor = props.scalingFactor;
    outProps->mipBias = props.mipBias;
    outProps->renderWidth = props.renderResolution.w;
    outProps->renderHeight = props.renderResolution.h;
    outProps->outputWidth = props.upscaleResolution.w;
    outProps->outputHeight = props.upscaleResolution.h;
    outProps->jitterPhaseCount = props.jitterPhaseNum;
}

void rfxCmdSetStencilReference(RfxCommandList cmd, uint8_t frontRef, uint8_t backRef) {
    if (cmd->isRendering) {
        CORE.NRI.CmdSetStencilReference(*cmd->nriCmd, frontRef, backRef);
    }
}

void rfxCmdSetViewports(RfxCommandList cmd, float* viewports, uint32_t count) {
    if (count == 0 || !viewports)
        return;

    nri::Viewport* vp = (nri::Viewport*)alloca(sizeof(nri::Viewport) * count);
    for (uint32_t i = 0; i < count; ++i) {
        vp[i].x = viewports[i * 4 + 0];
        vp[i].y = viewports[i * 4 + 1];
        vp[i].width = viewports[i * 4 + 2];
        vp[i].height = viewports[i * 4 + 3];
        vp[i].depthMin = 0.0f;
        vp[i].depthMax = 1.0f;
        vp[i].originBottomLeft = false;
    }

    cmd->currentViewport = vp[0];
    CORE.NRI.CmdSetViewports(*cmd->nriCmd, vp, count);
}

void rfxCmdUploadTexture(RfxCommandList cmd, RfxTexture dst, const void* data, uint32_t mip, uint32_t layer) {
    if (!dst || !data)
        return;

    const nri::FormatProps* props = nri::nriGetFormatProps(dst->format);
    uint32_t w = std::max(1u, dst->width >> mip);
    uint32_t h = std::max(1u, dst->height >> mip);
    uint32_t blockWidth = props->blockWidth;
    uint32_t blockHeight = props->blockHeight;
    uint32_t stride = props->stride;

    uint32_t rowPitch = (w + blockWidth - 1) / blockWidth * stride;
    uint32_t slicePitch = rowPitch * ((h + blockHeight - 1) / blockHeight);
    uint64_t size = (uint64_t)slicePitch;

    nri::TextureRegionDesc region = {};
    region.mipOffset = (nri::Dim_t)(dst->mipOffset + mip);
    region.layerOffset = (nri::Dim_t)(dst->layerOffset + layer);
    region.width = (nri::Dim_t)w;
    region.height = (nri::Dim_t)h;
    region.depth = 1;
    region.planes = nri::PlaneBits::ALL;

    RfxResourceState restoreState = dst->state->Get(region.mipOffset, region.layerOffset);

    UploadToResource(cmd, nullptr, 0, dst->texture, &region, data, size, rowPitch, slicePitch, restoreState, nullptr, dst);
}

void rfxCmdSetDepthBias(RfxCommandList cmd, float constant, float clamp, float slope) {
    if (cmd->isRendering) {
        nri::DepthBiasDesc dbd = { constant, clamp, slope };
        CORE.NRI.CmdSetDepthBias(*cmd->nriCmd, dbd);
    }
}

void rfxCmdSetDepthBounds(RfxCommandList cmd, float minBound, float maxBound) {
    if (cmd->isRendering) {
        CORE.NRI.CmdSetDepthBounds(*cmd->nriCmd, minBound, maxBound);
    }
}

void rfxCmdSetShadingRate(
    RfxCommandList cmd, RfxShadingRate rate, RfxShadingRateCombiner primitiveCombiner, RfxShadingRateCombiner attachmentCombiner
) {
    if (cmd->isRendering) {
        nri::ShadingRateDesc srd = {};
        srd.shadingRate = ToNRIShadingRate(rate);
        srd.primitiveCombiner = ToNRIShadingRateCombiner(primitiveCombiner);
        srd.attachmentCombiner = ToNRIShadingRateCombiner(attachmentCombiner);
        CORE.NRI.CmdSetShadingRate(*cmd->nriCmd, srd);
    }
}

RfxCommandList rfxCreateCommandList(RfxQueueType queueType) {
    RfxCommandListImpl* impl = RfxNew<RfxCommandListImpl>();
    impl->queueType = queueType;
    impl->isSecondary = true;

    nri::Queue* queue = (queueType == RFX_QUEUE_COMPUTE) ? CORE.NRIComputeQueue
                                                         : ((queueType == RFX_QUEUE_COPY) ? CORE.NRICopyQueue : CORE.NRIGraphicsQueue);

    uint32_t frames = GetQueuedFrameNum();
    impl->allocators.resize(frames);
    impl->buffers.resize(frames);

    for (uint32_t i = 0; i < frames; ++i) {
        NRI_CHECK(CORE.NRI.CreateCommandAllocator(*queue, impl->allocators[i]));
        NRI_CHECK(CORE.NRI.CreateCommandBuffer(*impl->allocators[i], impl->buffers[i]));
    }

    // will be updated in Begin
    impl->nriCmd = impl->buffers[0];

    impl->ResetCache();
    return impl;
}

void rfxDestroyCommandList(RfxCommandList cmd) {
    if (!cmd || !cmd->isSecondary)
        return;

    RfxVector<nri::CommandBuffer*> buffers = std::move(cmd->buffers);
    RfxVector<nri::CommandAllocator*> allocators = std::move(cmd->allocators);

    rfxDeferDestruction([=]() {
        for (auto* cb : buffers)
            CORE.NRI.DestroyCommandBuffer(cb);
        for (auto* ca : allocators)
            CORE.NRI.DestroyCommandAllocator(ca);
    });
    RfxDelete(cmd);
}

void rfxBeginCommandList(RfxCommandList cmd) {
    if (!cmd)
        return;

    uint32_t framesInFlight = GetQueuedFrameNum();
    if (CORE.FrameIndex >= framesInFlight) {
        uint64_t waitValue = CORE.FrameIndex - framesInFlight + 1;
        CORE.NRI.Wait(*CORE.NRIFrameFence, waitValue);
    }

    uint32_t frameSlot = CORE.FrameIndex % framesInFlight;

    nri::CommandAllocator* allocator = cmd->allocators[frameSlot];
    nri::CommandBuffer* buffer = cmd->buffers[frameSlot];

    cmd->nriCmd = buffer;

    CORE.NRI.ResetCommandAllocator(*allocator);
    CORE.NRI.BeginCommandBuffer(*buffer, CORE.Bindless.descriptorPool);

    cmd->ResetCache();
}

void rfxEndCommandList(RfxCommandList cmd) {
    if (!cmd)
        return;
    cmd->FlushBarriers();
    CORE.NRI.EndCommandBuffer(*cmd->nriCmd);
}

void rfxSubmitCommandListAsync(
    RfxCommandList cmd, RfxFence* waitFences, uint64_t* waitValues, uint32_t waitCount, RfxFence* signalFences, uint64_t* signalValues,
    uint32_t signalCount
) {
    RfxVector<nri::FenceSubmitDesc> waits(waitCount);
    for (uint32_t i = 0; i < waitCount; ++i) {
        waits[i].fence = waitFences[i]->fence;
        waits[i].value = waitValues[i];
        waits[i].stages = nri::StageBits::ALL;
    }

    RfxVector<nri::FenceSubmitDesc> signals(signalCount);
    for (uint32_t i = 0; i < signalCount; ++i) {
        signals[i].fence = signalFences[i]->fence;
        signals[i].value = signalValues[i];
        signals[i].stages = nri::StageBits::ALL;
        signalFences[i]->value = signalValues[i];
    }

    nri::Queue* queue = nullptr;
    if (cmd) {
        queue = (cmd->queueType == RFX_QUEUE_COMPUTE) ? CORE.NRIComputeQueue
                                                      : ((cmd->queueType == RFX_QUEUE_COPY) ? CORE.NRICopyQueue : CORE.NRIGraphicsQueue);
    } else {
        queue = CORE.NRIGraphicsQueue;
    }

    nri::QueueSubmitDesc submit = {};
    if (cmd) {
        submit.commandBuffers = &cmd->nriCmd;
        submit.commandBufferNum = 1;
    }
    if (waitCount > 0) {
        submit.waitFences = waits.data();
        submit.waitFenceNum = waitCount;
    }
    if (signalCount > 0) {
        submit.signalFences = signals.data();
        submit.signalFenceNum = signalCount;
    }

    CORE.NRI.QueueSubmit(*queue, submit);
}

void rfxCmdClearStorageBuffer(RfxCommandList cmd, RfxBuffer buffer, uint32_t value) {
    if (!buffer)
        return;

    MustTransition(cmd);
    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_SHADER_WRITE);
    cmd->FlushBarriers();

    nri::ClearStorageDesc clear = {};
    clear.descriptor = buffer->descriptorUAV;
    clear.setIndex = 1;
    clear.rangeIndex = 3; // RW buffers
    clear.descriptorIndex = buffer->bindlessIndex;
    clear.value.ui = { value, value, value, value };

    CORE.NRI.CmdClearStorage(*cmd->nriCmd, clear);
}

void rfxCmdClearStorageTexture(RfxCommandList cmd, RfxTexture texture, RfxColor value) {
    if (!texture)
        return;

    MustTransition(cmd);
    rfxCmdTransitionTexture(cmd, texture, RFX_STATE_SHADER_WRITE);
    cmd->FlushBarriers();

    nri::ClearStorageDesc clear = {};
    clear.descriptor = texture->descriptorUAV;
    clear.setIndex = 1;
    clear.rangeIndex = 4; // RW textures
    clear.descriptorIndex = texture->bindlessIndex;
    clear.value.f = { value.r, value.g, value.b, value.a };

    CORE.NRI.CmdClearStorage(*cmd->nriCmd, clear);
}

RfxFence rfxCreateFence(uint64_t initialValue) {
    RfxFenceImpl* impl = RfxNew<RfxFenceImpl>();
    impl->value = initialValue;
    NRI_CHECK(CORE.NRI.CreateFence(*CORE.NRIDevice, initialValue, impl->fence));
    return impl;
}

void rfxDestroyFence(RfxFence fence) {
    if (!fence)
        return;
    RfxFenceImpl* ptr = fence;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyFence(ptr->fence);
        RfxDelete(ptr);
    });
}

void rfxWaitFence(RfxFence fence, uint64_t value) {
    if (fence)
        CORE.NRI.Wait(*fence->fence, value);
}

uint64_t rfxGetFenceValue(RfxFence fence) {
    return fence ? CORE.NRI.GetFenceValue(*fence->fence) : 0;
}

RfxTexture rfxGetBackbufferTexture() {
    return &CORE.SwapChainWrapper;
}

RfxTexture
rfxCreateTextureView(RfxTexture original, RfxFormat format, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount) {
    if (!original)
        return nullptr;

    if (mip + mipCount > original->state->totalMips)
        mipCount = original->state->totalMips - mip;
    if (layer + layerCount > original->state->totalLayers)
        layerCount = original->state->totalLayers - layer;

    RfxTextureImpl* impl = RfxNew<RfxTextureImpl>();
    impl->texture = original->texture;
    impl->memory = nullptr;
    impl->isView = true;

    impl->mipOffset = original->mipOffset + mip;
    impl->mipNum = mipCount;
    impl->layerOffset = original->layerOffset + layer;
    impl->layerNum = layerCount;

    // shared state
    impl->state = original->state;
    impl->state->AddRef();

    impl->format = (format == RFX_FORMAT_UNKNOWN) ? original->format : ToNRIFormat(format);
    impl->width = std::max(1u, original->width >> mip);
    impl->height = std::max(1u, original->height >> mip);
    impl->sampleCount = original->sampleCount;

    // SRV
    if (original->descriptor) {
        impl->bindlessIndex = AllocTextureSlot();
        nri::Texture2DViewDesc vd = {};
        vd.texture = impl->texture;
        vd.format = impl->format;
        vd.viewType = nri::Texture2DViewType::SHADER_RESOURCE;
        vd.mipOffset = (nri::Dim_t)mip;
        vd.mipNum = (nri::Dim_t)mipCount;
        vd.layerOffset = (nri::Dim_t)layer;
        vd.layerNum = (nri::Dim_t)layerCount;
        NRI_CHECK(CORE.NRI.CreateTexture2DView(vd, impl->descriptor));

        nri::UpdateDescriptorRangeDesc update = {};
        update.descriptorSet = CORE.Bindless.globalDescriptorSet;
        update.rangeIndex = 0;
        update.baseDescriptor = impl->bindlessIndex;
        update.descriptorNum = 1;
        update.descriptors = &impl->descriptor;
        CORE.NRI.UpdateDescriptorRanges(&update, 1);
    }

    // RTV / DSV
    if (original->descriptorAttachment) {
        nri::Texture2DViewDesc avd = {};
        avd.texture = impl->texture;
        avd.format = impl->format;
        bool isDepth = HasStencil(impl->format) || impl->format == nri::Format::D32_SFLOAT || impl->format == nri::Format::D16_UNORM;
        avd.viewType = isDepth ? nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT : nri::Texture2DViewType::COLOR_ATTACHMENT;
        avd.mipOffset = (nri::Dim_t)mip;
        avd.mipNum = (nri::Dim_t)mipCount;
        avd.layerOffset = (nri::Dim_t)layer;
        avd.layerNum = (nri::Dim_t)layerCount;
        NRI_CHECK(CORE.NRI.CreateTexture2DView(avd, impl->descriptorAttachment));
    }

    // UAV
    if (original->descriptorUAV) {
        if (impl->bindlessIndex == 0)
            impl->bindlessIndex = AllocTextureSlot();

        nri::Texture2DViewDesc uav = {};
        uav.texture = impl->texture;
        uav.format = impl->format;
        uav.viewType = nri::Texture2DViewType::SHADER_RESOURCE_STORAGE;
        uav.mipOffset = (nri::Dim_t)mip;
        uav.mipNum = (nri::Dim_t)mipCount;
        uav.layerOffset = (nri::Dim_t)layer;
        uav.layerNum = (nri::Dim_t)layerCount;
        NRI_CHECK(CORE.NRI.CreateTexture2DView(uav, impl->descriptorUAV));

        nri::UpdateDescriptorRangeDesc update = {};
        update.descriptorSet = CORE.Bindless.globalDescriptorSet;
        update.rangeIndex = 4; // uav
        update.baseDescriptor = impl->bindlessIndex;
        update.descriptorNum = 1;
        update.descriptors = &impl->descriptorUAV;
        CORE.NRI.UpdateDescriptorRanges(&update, 1);
    }

    return impl;
}

RfxQueryPool rfxCreateQueryPool(RfxQueryType type, uint32_t capacity) {
    RfxQueryPoolImpl* impl = RfxNew<RfxQueryPoolImpl>();
    impl->type = type;
    nri::QueryPoolDesc qpd = {};
    qpd.queryType = (type == RFX_QUERY_TYPE_TIMESTAMP) ? nri::QueryType::TIMESTAMP : nri::QueryType::OCCLUSION;
    qpd.capacity = capacity;
    NRI_CHECK(CORE.NRI.CreateQueryPool(*CORE.NRIDevice, qpd, impl->pool));
    return impl;
}

void rfxDestroyQueryPool(RfxQueryPool pool) {
    if (!pool)
        return;
    RfxQueryPoolImpl* ptr = pool;
    rfxDeferDestruction([=]() {
        CORE.NRI.DestroyQueryPool(ptr->pool);
        RfxDelete(ptr);
    });
}

void rfxCmdResetQueries(RfxCommandList cmd, RfxQueryPool pool, uint32_t offset, uint32_t count) {
    CORE.NRI.CmdResetQueries(*cmd->nriCmd, *pool->pool, offset, count);
}

void rfxCmdBeginQuery(RfxCommandList cmd, RfxQueryPool pool, uint32_t queryIndex) {
    CORE.NRI.CmdBeginQuery(*cmd->nriCmd, *pool->pool, queryIndex);
}

void rfxCmdEndQuery(RfxCommandList cmd, RfxQueryPool pool, uint32_t queryIndex) {
    CORE.NRI.CmdEndQuery(*cmd->nriCmd, *pool->pool, queryIndex);
}

void rfxCmdCopyQueries(RfxCommandList cmd, RfxQueryPool pool, uint32_t offset, uint32_t count, RfxBuffer dstBuffer, uint64_t dstOffset) {
    rfxCmdTransitionBuffer(cmd, dstBuffer, RFX_STATE_COPY_DST);
    cmd->FlushBarriers();
    CORE.NRI.CmdCopyQueries(*cmd->nriCmd, *pool->pool, offset, count, *dstBuffer->buffer, dstOffset);
}

void rfxCmdReadbackTextureToBuffer(RfxCommandList cmd, RfxTexture src, RfxBuffer dst, uint64_t dstOffset) {
    MustTransition(cmd);
    rfxCmdTransitionTexture(cmd, src, RFX_STATE_COPY_SRC);
    rfxCmdTransitionBuffer(cmd, dst, RFX_STATE_COPY_DST);
    cmd->FlushBarriers();

    const nri::FormatProps* props = nri::nriGetFormatProps(src->format);
    uint32_t blockWidth = props->blockWidth;
    uint32_t stride = props->stride;

    // align to block size
    uint32_t nbBlocks = (src->width + blockWidth - 1) / blockWidth;
    uint32_t rowPitch = nbBlocks * stride;

    // align to 256 bytes
    uint32_t alignedRowPitch = (rowPitch + 255) & ~255;

    nri::TextureDataLayoutDesc layout = {};
    layout.offset = dstOffset;
    layout.rowPitch = alignedRowPitch;
    layout.slicePitch = alignedRowPitch * src->height;

    nri::TextureRegionDesc region = {};
    region.mipOffset = src->mipOffset;
    region.layerOffset = src->layerOffset;
    region.width = src->width;
    region.height = src->height;
    region.depth = 1;
    region.planes = nri::PlaneBits::ALL;

    CORE.NRI.CmdReadbackTextureToBuffer(*cmd->nriCmd, *dst->buffer, layout, *src->texture, region);
}

void rfxSetBufferName(RfxBuffer buffer, const char* name) {
    if (buffer)
        CORE.NRI.SetDebugName(buffer->buffer, name);
}

void rfxSetTextureName(RfxTexture texture, const char* name) {
    if (texture)
        CORE.NRI.SetDebugName(texture->texture, name);
}

void rfxSetPipelineName(RfxPipeline pipeline, const char* name) {
    if (pipeline)
        CORE.NRI.SetDebugName(((RfxPipelineImpl*)pipeline)->pipeline, name);
}

void rfxSetLowLatencyMode(bool enabled, bool boost) {
    if (!CORE.AllowLowLatency || !CORE.NRISwapChain)
        return;

    if (CORE.LowLatencyEnabled != enabled || CORE.LowLatencyBoost != boost) {
        CORE.LowLatencyEnabled = enabled;
        CORE.LowLatencyBoost = boost;

        nri::LatencySleepMode mode = {};
        mode.lowLatencyMode = enabled;
        mode.lowLatencyBoost = boost;
        mode.minIntervalUs = 0;
        CORE.NRI.SetLatencySleepMode(*CORE.NRISwapChain, mode);
    }
}

void rfxLatencySleep() {
    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled && CORE.NRISwapChain) {
        CORE.NRI.SetLatencyMarker(*CORE.NRISwapChain, nri::LatencyMarker::SIMULATION_START);
        CORE.NRI.LatencySleep(*CORE.NRISwapChain);
        CORE.NRI.SetLatencyMarker(*CORE.NRISwapChain, nri::LatencyMarker::INPUT_SAMPLE);
    }
}

void rfxSetLatencyMarker(RfxLatencyMarker marker) {
    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled && CORE.NRISwapChain) {
        nri::LatencyMarker nm = nri::LatencyMarker::SIMULATION_START;
        switch (marker) {
        case RFX_LATENCY_MARKER_SIMULATION_START: nm = nri::LatencyMarker::SIMULATION_START; break;
        case RFX_LATENCY_MARKER_SIMULATION_END: nm = nri::LatencyMarker::SIMULATION_END; break;
        case RFX_LATENCY_MARKER_RENDER_SUBMIT_START: nm = nri::LatencyMarker::RENDER_SUBMIT_START; break;
        case RFX_LATENCY_MARKER_RENDER_SUBMIT_END: nm = nri::LatencyMarker::RENDER_SUBMIT_END; break;
        case RFX_LATENCY_MARKER_INPUT_SAMPLE: nm = nri::LatencyMarker::INPUT_SAMPLE; break;
        }
        CORE.NRI.SetLatencyMarker(*CORE.NRISwapChain, nm);
    }
}

bool rfxGetLatencyReport(RfxLatencyReport* outReport) {
    if (!CORE.AllowLowLatency || !CORE.NRISwapChain || !outReport)
        return false;

    nri::LatencyReport report = {};
    if (CORE.NRI.GetLatencyReport(*CORE.NRISwapChain, report) != nri::Result::SUCCESS)
        return false;

    outReport->inputSampleTimeUs = report.inputSampleTimeUs;
    outReport->simulationStartTimeUs = report.simulationStartTimeUs;
    outReport->simulationEndTimeUs = report.simulationEndTimeUs;
    outReport->renderSubmitStartTimeUs = report.renderSubmitStartTimeUs;
    outReport->renderSubmitEndTimeUs = report.renderSubmitEndTimeUs;
    outReport->presentStartTimeUs = report.presentStartTimeUs;
    outReport->presentEndTimeUs = report.presentEndTimeUs;
    outReport->driverStartTimeUs = report.driverStartTimeUs;
    outReport->driverEndTimeUs = report.driverEndTimeUs;
    outReport->osRenderQueueStartTimeUs = report.osRenderQueueStartTimeUs;
    outReport->osRenderQueueEndTimeUs = report.osRenderQueueEndTimeUs;
    outReport->gpuRenderStartTimeUs = report.gpuRenderStartTimeUs;
    outReport->gpuRenderEndTimeUs = report.gpuRenderEndTimeUs;

    return true;
}

void rfxCmdZeroBuffer(RfxCommandList cmd, RfxBuffer buffer, size_t offset, size_t size) {
    if (!buffer)
        return;
    MustTransition(cmd);

    rfxCmdTransitionBuffer(cmd, buffer, RFX_STATE_COPY_DST);
    cmd->FlushBarriers();

    CORE.NRI.CmdZeroBuffer(*cmd->nriCmd, *buffer->buffer, offset, (size == 0) ? nri::WHOLE_SIZE : size);
}
void rfxCmdResolveTexture(RfxCommandList cmd, RfxTexture dst, RfxTexture src, RfxResolveOp op) {
    if (!dst || !src)
        return;
    MustTransition(cmd);

    rfxCmdTransitionTexture(cmd, src, RFX_STATE_RESOLVE_SRC);
    rfxCmdTransitionTexture(cmd, dst, RFX_STATE_RESOLVE_DST);
    cmd->FlushBarriers();

    nri::ResolveOp nriOp = nri::ResolveOp::AVERAGE;
    if (op == RFX_RESOLVE_OP_MIN)
        nriOp = nri::ResolveOp::MIN;
    if (op == RFX_RESOLVE_OP_MAX)
        nriOp = nri::ResolveOp::MAX;

    CORE.NRI.CmdResolveTexture(*cmd->nriCmd, *dst->texture, nullptr, *src->texture, nullptr, nriOp);
}

void rfxCmdCopyMicromap(RfxCommandList cmd, RfxMicromap dst, RfxMicromap src, RfxCopyMode mode) {
    if (!dst || !src)
        return;
    MustTransition(cmd);

    RfxMicromapImpl* dstImpl = (RfxMicromapImpl*)dst;
    RfxMicromapImpl* srcImpl = (RfxMicromapImpl*)src;

    // dest->copy dest
    if (dstImpl->currentAccess != nri::AccessBits::MICROMAP_WRITE || dstImpl->currentStage != nri::StageBits::COPY) {
        nri::BufferBarrierDesc& bbd = cmd->barriers.bufferBarriers.emplace_back();
        bbd.buffer = dstImpl->barrierBuffer;
        bbd.before = { dstImpl->currentAccess, dstImpl->currentStage };
        bbd.after = { nri::AccessBits::MICROMAP_WRITE, nri::StageBits::COPY };

        dstImpl->currentAccess = nri::AccessBits::MICROMAP_WRITE;
        dstImpl->currentStage = nri::StageBits::COPY;
    }

    // src->copy src
    if (srcImpl->currentAccess != nri::AccessBits::MICROMAP_READ || srcImpl->currentStage != nri::StageBits::COPY) {
        nri::BufferBarrierDesc& bbd = cmd->barriers.bufferBarriers.emplace_back();
        bbd.buffer = srcImpl->barrierBuffer;
        bbd.before = { srcImpl->currentAccess, srcImpl->currentStage };
        bbd.after = { nri::AccessBits::MICROMAP_READ, nri::StageBits::COPY };

        srcImpl->currentAccess = nri::AccessBits::MICROMAP_READ;
        srcImpl->currentStage = nri::StageBits::COPY;
    }

    cmd->FlushBarriers();

    nri::CopyMode nriMode = (mode == RFX_COPY_MODE_COMPACT) ? nri::CopyMode::COMPACT : nri::CopyMode::CLONE;

    CORE.NRI.CmdCopyMicromap(*cmd->nriCmd, *dstImpl->micromap, *srcImpl->micromap, nriMode);
}

void rfxCmdWriteAccelerationStructureSize(
    RfxCommandList cmd, RfxAccelerationStructure* asArray, uint32_t count, RfxQueryPool pool, uint32_t queryOffset
) {
    if (count == 0 || !pool)
        return;

    // AS->read
    for (uint32_t i = 0; i < count; ++i) {
        TransitionAS(
            cmd, (RfxAccelerationStructureImpl*)asArray[i], nri::AccessBits::ACCELERATION_STRUCTURE_READ,
            nri::StageBits::ACCELERATION_STRUCTURE
        );
    }
    cmd->FlushBarriers();

    RfxVector<const nri::AccelerationStructure*> nriHandles(count);
    for (uint32_t i = 0; i < count; ++i) {
        nriHandles[i] = ((RfxAccelerationStructureImpl*)asArray[i])->as;
    }

    CORE.NRI.CmdWriteAccelerationStructuresSizes(*cmd->nriCmd, nriHandles.data(), count, *pool->pool, queryOffset);
}

void rfxCmdCopyAccelerationStructure(RfxCommandList cmd, RfxAccelerationStructure dst, RfxAccelerationStructure src, RfxCopyMode mode) {
    if (!dst || !src)
        return;
    MustTransition(cmd);

    RfxAccelerationStructureImpl* dstImpl = (RfxAccelerationStructureImpl*)dst;
    RfxAccelerationStructureImpl* srcImpl = (RfxAccelerationStructureImpl*)src;

    // dest->copy dest
    TransitionAS(cmd, dstImpl, nri::AccessBits::ACCELERATION_STRUCTURE_WRITE, nri::StageBits::COPY);

    // src->copy src
    TransitionAS(cmd, srcImpl, nri::AccessBits::ACCELERATION_STRUCTURE_READ, nri::StageBits::COPY);

    cmd->FlushBarriers();

    nri::CopyMode nriMode = (mode == RFX_COPY_MODE_COMPACT) ? nri::CopyMode::COMPACT : nri::CopyMode::CLONE;

    CORE.NRI.CmdCopyAccelerationStructure(*cmd->nriCmd, *dstImpl->as, *srcImpl->as, nriMode);
    // TODO: fix TraceRays state tracking from this point on
}

void rfxCmdSetSampleLocations(RfxCommandList cmd, const RfxSampleLocation* locations, uint32_t locationCount, uint32_t sampleCount) {
    if (!locations || locationCount == 0)
        return;

    static_assert(sizeof(RfxSampleLocation) == sizeof(nri::SampleLocation), "RfxSampleLocation size mismatch");

    CORE.NRI.CmdSetSampleLocations(
        *cmd->nriCmd, (const nri::SampleLocation*)locations, (nri::Sample_t)locationCount, (nri::Sample_t)sampleCount
    );
}

//
// Frame
//

static void BuildNRIPipeline(RfxPipelineImpl* impl) {
    if (impl->type == RfxPipelineImpl::GRAPHICS) {
        const auto& cache = std::get<CachedGraphics>(impl->cache);
        const RfxPipelineDesc* desc = &cache.desc;

        nri::GraphicsPipelineDesc gpd = {};
        gpd.pipelineLayout = impl->shader->pipelineLayout;
        gpd.inputAssembly.topology = ToNRITopology(desc->topology);
        gpd.inputAssembly.tessControlPointNum = (uint8_t)desc->patchControlPoints;

        gpd.rasterization.fillMode = desc->wireframe ? nri::FillMode::WIREFRAME : nri::FillMode::SOLID;
        gpd.rasterization.cullMode = (desc->cullMode == RFX_CULL_BACK)
                                         ? nri::CullMode::BACK
                                         : ((desc->cullMode == RFX_CULL_FRONT) ? nri::CullMode::FRONT : nri::CullMode::NONE);
        gpd.rasterization.frontCounterClockwise = true;
        gpd.rasterization.depthBias.constant = desc->depthBiasConstant;
        gpd.rasterization.depthBias.clamp = desc->depthBiasClamp;
        gpd.rasterization.depthBias.slope = desc->depthBiasSlope;
        gpd.rasterization.shadingRate = desc->shadingRate;

        uint8_t samples = (desc->sampleCount > 0) ? (uint8_t)desc->sampleCount : (uint8_t)CORE.SampleCount;
        if (samples == 0)
            samples = 1;

        nri::MultisampleDesc ms = {};
        ms.sampleNum = (nri::Sample_t)samples;
        ms.sampleMask = nri::ALL;
        gpd.multisample = &ms;

        // colors
        RfxVector<nri::ColorAttachmentDesc> colorDescs;
        if (desc->attachmentCount > 0 && desc->attachments) {
            colorDescs.resize(desc->attachmentCount);
            for (uint32_t i = 0; i < desc->attachmentCount; ++i) {
                nri::ColorAttachmentDesc& cad = colorDescs[i];
                const RfxAttachmentDesc& src = desc->attachments[i];
                cad.format = ToNRIFormat(src.format);

                nri::ColorWriteBits mask = (nri::ColorWriteBits)src.blend.writeMask;
                cad.colorWriteMask = (mask == nri::ColorWriteBits::NONE) ? nri::ColorWriteBits::RGBA : mask;

                cad.blendEnabled = src.blend.blendEnabled;
                cad.colorBlend.srcFactor = ToNRIBlendFactor(src.blend.srcColor);
                cad.colorBlend.dstFactor = ToNRIBlendFactor(src.blend.dstColor);
                cad.colorBlend.op = ToNRIBlendOp(src.blend.colorOp);
                cad.alphaBlend.srcFactor = ToNRIBlendFactor(src.blend.srcAlpha);
                cad.alphaBlend.dstFactor = ToNRIBlendFactor(src.blend.dstAlpha);
                cad.alphaBlend.op = ToNRIBlendOp(src.blend.alphaOp);
            }
        } else if (desc->colorFormat != RFX_FORMAT_UNKNOWN) {
            colorDescs.resize(1);
            nri::ColorAttachmentDesc& cad = colorDescs[0];
            cad.format = ToNRIFormat(desc->colorFormat);

            nri::ColorWriteBits mask = (nri::ColorWriteBits)desc->blendState.writeMask;
            cad.colorWriteMask = (mask == nri::ColorWriteBits::NONE) ? nri::ColorWriteBits::RGBA : mask;

            cad.blendEnabled = desc->blendState.blendEnabled;
            cad.colorBlend.srcFactor = ToNRIBlendFactor(desc->blendState.srcColor);
            cad.colorBlend.dstFactor = ToNRIBlendFactor(desc->blendState.dstColor);
            cad.colorBlend.op = ToNRIBlendOp(desc->blendState.colorOp);
            cad.alphaBlend.srcFactor = ToNRIBlendFactor(desc->blendState.srcAlpha);
            cad.alphaBlend.dstFactor = ToNRIBlendFactor(desc->blendState.dstAlpha);
            cad.alphaBlend.op = ToNRIBlendOp(desc->blendState.alphaOp);
        }

        if (!colorDescs.empty()) {
            gpd.outputMerger.colors = colorDescs.data();
            gpd.outputMerger.colorNum = (uint32_t)colorDescs.size();
        }

        // depth stencil
        if (desc->depthFormat != RFX_FORMAT_UNKNOWN) {
            gpd.outputMerger.depthStencilFormat = ToNRIFormat(desc->depthFormat);
            if (desc->depthCompareOp != 0) {
                gpd.outputMerger.depth.compareOp = ToNRICompareOp(desc->depthCompareOp);
            } else {
                gpd.outputMerger.depth.compareOp = desc->depthTest ? nri::CompareOp::LESS : nri::CompareOp::NONE;
            }
            gpd.outputMerger.depth.write = desc->depthWrite;
            gpd.outputMerger.depth.boundsTest = desc->depthBoundsTest;

            if (desc->stencil.enabled) {
                gpd.outputMerger.stencil.front.compareOp = ToNRICompareOp(desc->stencil.front.compareOp);
                gpd.outputMerger.stencil.front.failOp = ToNRIStencilOp(desc->stencil.front.failOp);
                gpd.outputMerger.stencil.front.passOp = ToNRIStencilOp(desc->stencil.front.passOp);
                gpd.outputMerger.stencil.front.depthFailOp = ToNRIStencilOp(desc->stencil.front.depthFailOp);
                gpd.outputMerger.stencil.front.compareMask = desc->stencil.readMask;
                gpd.outputMerger.stencil.front.writeMask = desc->stencil.writeMask;

                gpd.outputMerger.stencil.back.compareOp = ToNRICompareOp(desc->stencil.back.compareOp);
                gpd.outputMerger.stencil.back.failOp = ToNRIStencilOp(desc->stencil.back.failOp);
                gpd.outputMerger.stencil.back.passOp = ToNRIStencilOp(desc->stencil.back.passOp);
                gpd.outputMerger.stencil.back.depthFailOp = ToNRIStencilOp(desc->stencil.back.depthFailOp);
                gpd.outputMerger.stencil.back.compareMask = desc->stencil.readMask;
                gpd.outputMerger.stencil.back.writeMask = desc->stencil.writeMask;
            }
        }

        if (desc->viewMask != 0) {
            gpd.outputMerger.viewMask = desc->viewMask;
            gpd.outputMerger.multiview = nri::Multiview::FLEXIBLE;
        }

        // shaders
        RfxVector<nri::ShaderDesc> sds;
        bool explicitVertex = (desc->vsEntryPoint != nullptr);

        for (auto& s : impl->shader->stages) {
            if (s.stageBits & nri::StageBits::VERTEX_SHADER) {
                if (desc->vsEntryPoint && s.sourceEntryPoint != desc->vsEntryPoint)
                    continue;
                sds.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
            } else if (s.stageBits & nri::StageBits::FRAGMENT_SHADER) {
                if (explicitVertex && desc->psEntryPoint == nullptr)
                    continue;
                if (desc->psEntryPoint && s.sourceEntryPoint != desc->psEntryPoint)
                    continue;
                sds.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
            } else if (s.stageBits & nri::StageBits::GRAPHICS_SHADERS) {
                sds.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
            }
        }

        gpd.shaders = sds.data();
        gpd.shaderNum = (uint32_t)sds.size();

        // vertex input
        nri::VertexInputDesc vid = {};
        nri::VertexStreamDesc vs = { 0, nri::VertexStreamStepRate::PER_VERTEX };
        RfxVector<nri::VertexAttributeDesc> vads;

        bool hasVertexStage = (impl->shader->stageMask & nri::StageBits::VERTEX_SHADER);

        if (desc->vertexLayout && hasVertexStage) {
            for (int i = 0; i < desc->vertexLayoutCount; ++i) {
                const auto& el = desc->vertexLayout[i];
                nri::VertexAttributeDesc ad = {};
                ad.d3d = { el.semanticName ? el.semanticName : "POSITION", 0 };
                ad.vk = { el.location };
                ad.offset = el.offset;
                ad.format = ToNRIFormat(el.format);
                ad.streamIndex = 0;
                vads.push_back(ad);
            }
            vid.attributes = vads.data();
            vid.attributeNum = (uint8_t)vads.size();
            vid.streams = &vs;
            vid.streamNum = 1;
            gpd.vertexInput = &vid;
        }

        NRI_CHECK(CORE.NRI.CreateGraphicsPipeline(*CORE.NRIDevice, gpd, impl->pipeline));
    } else if (impl->type == RfxPipelineImpl::COMPUTE) {
        const auto& cache = std::get<CachedCompute>(impl->cache);
        const RfxComputePipelineDesc* desc = &cache.desc;

        nri::ComputePipelineDesc cpd = {};
        cpd.pipelineLayout = impl->shader->pipelineLayout;
        for (auto& s : impl->shader->stages) {
            if (s.stageBits & nri::StageBits::COMPUTE_SHADER) {
                if (desc->entryPoint && s.sourceEntryPoint != desc->entryPoint)
                    continue;

                cpd.shader = { s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() };
                break;
            }
        }
        NRI_CHECK(CORE.NRI.CreateComputePipeline(*CORE.NRIDevice, cpd, impl->pipeline));
    } else if (impl->type == RfxPipelineImpl::RAY_TRACING) {
        const auto& cache = std::get<CachedRT>(impl->cache);
        const RfxRayTracingPipelineDesc* desc = &cache.desc;

        nri::StageBits rtMask = nri::StageBits::RAYGEN_SHADER | nri::StageBits::ANY_HIT_SHADER | nri::StageBits::CLOSEST_HIT_SHADER |
                                nri::StageBits::MISS_SHADER | nri::StageBits::INTERSECTION_SHADER | nri::StageBits::CALLABLE_SHADER;

        RfxVector<nri::ShaderDesc> stageDescs;
        RfxVector<uint32_t> stageToLibraryIndex(impl->shader->stages.size(), 0);

        for (size_t i = 0; i < impl->shader->stages.size(); ++i) {
            const auto& s = impl->shader->stages[i];
            if ((s.stageBits & rtMask) != 0) {
                stageDescs.push_back({ s.stageBits, s.bytecode.data(), s.bytecode.size(), s.entryPoint.c_str() });
                stageToLibraryIndex[i] = (uint32_t)stageDescs.size();
            }
        }

        nri::ShaderLibraryDesc library = {};
        library.shaders = stageDescs.data();
        library.shaderNum = (uint32_t)stageDescs.size();

        // TODO: this is horrible but idk how to implement it better
        auto FindLibraryIndex = [&](const char* name) -> uint32_t {
            if (!name)
                return 0;
            for (size_t i = 0; i < stageDescs.size(); ++i) {
                for (const auto& s : impl->shader->stages) {
                    if ((s.stageBits & rtMask) != 0 && s.sourceEntryPoint == name) {
                        for (size_t j = 0; j < stageDescs.size(); ++j) {
                            if (stageDescs[j].bytecode == s.bytecode.data())
                                return (uint32_t)j;
                        }
                    }
                }
            }
            return 0;
        };

        RfxVector<nri::ShaderGroupDesc> groups(desc->groupCount);
        for (uint32_t i = 0; i < desc->groupCount; ++i) {
            const auto& src = desc->groups[i];
            if (src.type == RFX_SHADER_GROUP_GENERAL) {
                groups[i].shaderIndices[0] = FindLibraryIndex(src.generalShader);
            } else if (src.type == RFX_SHADER_GROUP_TRIANGLES) {
                groups[i].shaderIndices[0] = FindLibraryIndex(src.closestHitShader);
                groups[i].shaderIndices[1] = FindLibraryIndex(src.anyHitShader);
            } else if (src.type == RFX_SHADER_GROUP_PROCEDURAL) {
                groups[i].shaderIndices[0] = FindLibraryIndex(src.closestHitShader);
                groups[i].shaderIndices[1] = FindLibraryIndex(src.anyHitShader);
                groups[i].shaderIndices[2] = FindLibraryIndex(src.intersectionShader);
            }
        }

        nri::RayTracingPipelineDesc rtp = {};
        rtp.pipelineLayout = impl->shader->pipelineLayout;
        rtp.shaderLibrary = &library;
        rtp.shaderGroups = groups.data();
        rtp.shaderGroupNum = (uint32_t)groups.size();
        rtp.recursionMaxDepth = desc->maxRecursionDepth;
        rtp.rayPayloadMaxSize = desc->maxPayloadSize;
        rtp.rayHitAttributeMaxSize = desc->maxAttributeSize;

        rtp.flags = nri::RayTracingPipelineBits::NONE;
        if (desc->flags & RFX_RT_PIPELINE_SKIP_TRIANGLES)
            rtp.flags |= nri::RayTracingPipelineBits::SKIP_TRIANGLES;
        if (desc->flags & RFX_RT_PIPELINE_SKIP_AABBS)
            rtp.flags |= nri::RayTracingPipelineBits::SKIP_AABBS;
        if (desc->flags & RFX_RT_PIPELINE_ALLOW_MICROMAPS)
            rtp.flags |= nri::RayTracingPipelineBits::ALLOW_MICROMAPS;

        NRI_CHECK(CORE.NRI.CreateRayTracingPipeline(*CORE.NRIDevice, rtp, impl->pipeline));
    }
}

static void ProcessShaderReloads() {
    RfxSet<RfxShader> toReload;
    {
        std::lock_guard<std::mutex> lock(CORE.HotReloadMutex);
        if (CORE.ShadersToReload.empty())
            return;
        toReload = std::move(CORE.ShadersToReload);
        CORE.ShadersToReload.clear();
    }

    for (RfxShader shader : toReload) {
        RfxShaderImpl* impl = (RfxShaderImpl*)shader;
        printf("[Rafx] Reloading shader: %s...\n", impl->filepath.c_str());

        RfxVector<const char*> definesPtrs;
        for (const auto& s : impl->defines)
            definesPtrs.push_back(s.c_str());

        RfxVector<const char*> includesPtrs;
        for (const auto& s : impl->includeDirs)
            includesPtrs.push_back(s.c_str());

        // recompile
        RfxShader newShaderHandle = CompileShaderInternal(
            impl->filepath.c_str(), nullptr, definesPtrs.data(), (int)definesPtrs.size(), includesPtrs.data(), (int)includesPtrs.size()
        );

        RfxShaderImpl* newImpl = (RfxShaderImpl*)newShaderHandle;

        if (newImpl) {
            // swap resources
            nri::PipelineLayout* oldLayout = impl->pipelineLayout;
            rfxDeferDestruction([=]() { CORE.NRI.DestroyPipelineLayout(oldLayout); });

            impl->pipelineLayout = newImpl->pipelineLayout;
            impl->stages = std::move(newImpl->stages);
            impl->stageMask = newImpl->stageMask;
            impl->descriptorSetCount = newImpl->descriptorSetCount;
            impl->bindlessSetIndex = newImpl->bindlessSetIndex;
            impl->bindings = std::move(newImpl->bindings);

            newImpl->pipelineLayout = nullptr;
            RfxDelete(newImpl);

            for (auto* pipeline : impl->dependentPipelines) {
                nri::Pipeline* oldPipe = pipeline->pipeline;
                rfxDeferDestruction([=]() { CORE.NRI.DestroyPipeline(oldPipe); });

                BuildNRIPipeline(pipeline);
            }

            printf("[Rafx] Shader reload successful.\n");
        } else {
            fprintf(stderr, "[Rafx] Shader reload failed.\n");
        }
    }
}

void rfxBeginFrame() {
    bool wasSleeping = false;

    ProcessShaderReloads();

    // wait until swapchain is valid
    while (true) {
        bool hasExtent = (CORE.FramebufferWidth > 0 && CORE.FramebufferHeight > 0);
        bool active = !CORE.IsMinimized && (CORE.IsFocused || (CORE.WindowFlags & RFX_WINDOW_ALWAYS_ACTIVE));

        if (!hasExtent || !active) {
            wasSleeping = true;
            rfxEventSleep();
            if (rfxWindowShouldClose())
                return;
            rfxPollInputEvents();
            continue;
        }
        rfxPollInputEvents();
        if (CORE.FramebufferWidth == 0 || CORE.FramebufferHeight == 0 || CORE.IsMinimized) {
            continue;
        }
        break;
    }

    // time
    double currentTime = rfxGetTime();
    if (CORE.LastTime == 0.0 || wasSleeping)
        CORE.LastTime = currentTime - 0.01666;

    CORE.DeltaTime = (float)(currentTime - CORE.LastTime);
    CORE.LastTime = currentTime;
    if (CORE.DeltaTime <= 0.000001f)
        CORE.DeltaTime = 0.000001f;

    // recreate swapchain
    int currentW = CORE.FramebufferWidth;
    int currentH = CORE.FramebufferHeight;

    if (currentW > 0 && currentH > 0 && ((uint32_t)currentW != CORE.SwapChainWidth || (uint32_t)currentH != CORE.SwapChainHeight)) {
        RecreateSwapChain(currentW, currentH);
    }

    if (CORE.SwapChainWidth == 0 || CORE.SwapChainHeight == 0)
        return;

    if (CORE.FrameIndex >= GetQueuedFrameNum()) {
        CORE.NRI.Wait(*CORE.NRIFrameFence, 1 + CORE.FrameIndex - GetQueuedFrameNum());

        // process timestamps ...
        uint32_t completedFrameIdx = (CORE.FrameIndex - GetQueuedFrameNum());
        uint32_t qfIdx = completedFrameIdx % GetQueuedFrameNum();
        QueuedFrame& oldQf = CORE.QueuedFrames[qfIdx];

        if (oldQf.queryCount > 0) {
            uint64_t* data = (uint64_t*)CORE.NRI.MapBuffer(*CORE.TimestampBuffer, 0, nri::WHOLE_SIZE);
            if (data) {
                uint64_t* frameData = data + (qfIdx * RFX_MAX_TIMESTAMP_QUERIES);
                uint64_t freq = CORE.NRI.GetDeviceDesc(*CORE.NRIDevice).other.timestampFrequencyHz;
                double periodUs = 1e6 / (double)freq;

                CORE.LastFrameTimestamps.clear();
                for (const auto& reg : oldQf.profileRegions) {
                    uint64_t t0 = frameData[reg.startIndex];
                    uint64_t t1 = frameData[reg.endIndex];
                    if (t1 >= t0) {
                        float duration = (float)((t1 - t0) * periodUs);
                        CORE.LastFrameTimestamps.push_back({ reg.name, duration });
                    }
                }
                CORE.NRI.UnmapBuffer(*CORE.TimestampBuffer);
            }
        }
    }

    // process graveyard ...
    uint32_t frameIdx = CORE.FrameIndex % GetQueuedFrameNum();
    {
        auto& q = CORE.Graveyard[frameIdx];
        RfxVector<std::function<void()>> readyTasks = std::move(q.tasks);
        q.tasks.clear();

        for (auto& task : readyTasks)
            task();
    }

    // begin implicit commandbuffer
    QueuedFrame& qf = CORE.QueuedFrames[frameIdx];
    CORE.NRI.ResetCommandAllocator(*qf.commandAllocator);

    qf.queryCount = 0;
    qf.profileRegions.clear();
    qf.profileStack.clear();

    uint32_t semIdx = CORE.FrameIndex % (uint32_t)CORE.SwapChainTextures.size();
    CORE.NRI.AcquireNextTexture(*CORE.NRISwapChain, *CORE.SwapChainTextures[semIdx].acquireSemaphore, CORE.CurrentSwapChainTextureIndex);

    CORE.NRI.BeginCommandBuffer(*qf.commandBuffer, CORE.Bindless.descriptorPool);
    CORE.NRI.CmdResetQueries(*qf.commandBuffer, *CORE.TimestampPool, frameIdx * RFX_MAX_TIMESTAMP_QUERIES, RFX_MAX_TIMESTAMP_QUERIES);

    qf.wrapper.ResetCache();

    // run init work ...
    if (!CORE.PendingPreBarriers.empty() || !CORE.PendingPostBarriers.empty()) {
        for (auto& work : CORE.PendingPreBarriers)
            work(*qf.commandBuffer);
        CORE.PendingPreBarriers.clear();

        CORE.NRI.CmdCopyStreamedData(*qf.commandBuffer, *CORE.NRIStreamer);

        for (auto& work : CORE.PendingPostBarriers)
            work(*qf.commandBuffer);
        CORE.PendingPostBarriers.clear();
    }

    qf.wrapper.isRendering = false;
    qf.wrapper.currentPipeline = nullptr;
    qf.wrapper.currentVertexBuffer = nullptr;
    qf.wrapper.currentIndexBuffer = nullptr;
    qf.wrapper.scissorSet = false;
    qf.wrapper.activeColorAttachments.clear();
    qf.wrapper.currentRenderingDesc = {};
    qf.wrapper.activeColorTextures.clear();
    qf.wrapper.activeDepthTexture = nullptr;
    qf.wrapper.tempDescriptors.clear();
    qf.wrapper.barriers.bufferBarriers.clear();
    qf.wrapper.barriers.textureBarriers.clear();
    qf.wrapper.barriers.globalBarriers.clear();

    CORE.SwapChainWrapper.texture = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].texture;
    CORE.SwapChainWrapper.format = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].attachmentFormat;
    CORE.SwapChainWrapper.width = CORE.SwapChainWidth;
    CORE.SwapChainWrapper.height = CORE.SwapChainHeight;
    CORE.SwapChainWrapper.sampleCount = 1;
    CORE.SwapChainWrapper.mipNum = 1;
    CORE.SwapChainWrapper.layerNum = 1;
    CORE.SwapChainWrapper.mipOffset = 0;
    CORE.SwapChainWrapper.layerOffset = 0;

    if (!CORE.SwapChainWrapper.state) {
        CORE.SwapChainWrapper.state = RfxNew<RfxTextureSharedState>();
        CORE.SwapChainWrapper.state->totalMips = 1;
        CORE.SwapChainWrapper.state->totalLayers = 1;
        CORE.SwapChainWrapper.state->subresourceStates.resize(1);
    }

    if (CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].initialized) {
        CORE.SwapChainWrapper.state->Set(0, 0, RFX_STATE_PRESENT);
    } else {
        CORE.SwapChainWrapper.state->Set(0, 0, RFX_STATE_UNDEFINED);
        CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex].initialized = true;
    }

    CORE.FrameStarted = true;
}

void rfxEndFrame() {
    if (!CORE.FrameStarted)
        return;
    CORE.FrameStarted = false;

    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled && CORE.NRISwapChain) {
        CORE.NRI.SetLatencyMarker(*CORE.NRISwapChain, nri::LatencyMarker::SIMULATION_END);
    }

    uint32_t frameIdx = CORE.FrameIndex % GetQueuedFrameNum();
    QueuedFrame& qf = CORE.QueuedFrames[frameIdx];
    RfxCommandList cmd = &qf.wrapper;

    if (cmd->isRendering)
        CORE.NRI.CmdEndRendering(*qf.commandBuffer);

    // swapchain->present
    cmd->barriers.RequireState(&CORE.SwapChainWrapper, RFX_STATE_PRESENT);
    cmd->barriers.Flush(*qf.commandBuffer);

    if (qf.queryCount > 0) {
        CORE.NRI.CmdCopyQueries(
            *qf.commandBuffer, *CORE.TimestampPool, frameIdx * RFX_MAX_TIMESTAMP_QUERIES, qf.queryCount, *CORE.TimestampBuffer,
            (frameIdx * RFX_MAX_TIMESTAMP_QUERIES) * sizeof(uint64_t)
        );
    }

    CORE.NRI.EndCommandBuffer(*qf.commandBuffer);

    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled && CORE.NRISwapChain) {
        CORE.NRI.SetLatencyMarker(*CORE.NRISwapChain, nri::LatencyMarker::RENDER_SUBMIT_START);
    }

    SwapChainTexture& sc = CORE.SwapChainTextures[CORE.CurrentSwapChainTextureIndex];
    nri::FenceSubmitDesc wait = { CORE.SwapChainTextures[CORE.FrameIndex % CORE.SwapChainTextures.size()].acquireSemaphore, 0,
                                  nri::StageBits::COLOR_ATTACHMENT };
    nri::FenceSubmitDesc signal = { sc.releaseSemaphore, 0, nri::StageBits::NONE };
    nri::QueueSubmitDesc submit = {};
    submit.waitFences = &wait;
    submit.waitFenceNum = 1;
    submit.signalFences = &signal;
    submit.signalFenceNum = 1;
    submit.commandBuffers = &qf.commandBuffer;
    submit.commandBufferNum = 1;

    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled) {
        submit.swapChain = CORE.NRISwapChain;
    }

    CORE.NRI.QueueSubmit(*CORE.NRIGraphicsQueue, submit);

    if (CORE.AllowLowLatency && CORE.LowLatencyEnabled && CORE.NRISwapChain) {
        CORE.NRI.SetLatencyMarker(*CORE.NRISwapChain, nri::LatencyMarker::RENDER_SUBMIT_END);
    }

    CORE.NRI.QueuePresent(*CORE.NRISwapChain, *sc.releaseSemaphore);

    nri::FenceSubmitDesc frameSig = { CORE.NRIFrameFence, 1 + CORE.FrameIndex, nri::StageBits::NONE };
    nri::QueueSubmitDesc frameSub = {};
    frameSub.signalFences = &frameSig;
    frameSub.signalFenceNum = 1;
    CORE.NRI.QueueSubmit(*CORE.NRIGraphicsQueue, frameSub);

    CORE.NRI.EndStreamerFrame(*CORE.NRIStreamer);
    CORE.FrameIndex++;
}
