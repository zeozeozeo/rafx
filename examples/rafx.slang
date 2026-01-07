// This file is here solely for syntax highlighting - not actually compiled (Rafx will override this with a virtual file)
// Do not modify!

#ifndef RAFX_SLANG_H
#define RAFX_SLANG_H

#define RFX_MAX_BINDLESS_TEXTURES 4096
#define RFX_RAY_TRACING_SUPPORTED 1

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
