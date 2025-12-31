#include "HandmadeMath.h"
#include "rafx.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

const char* bloomShaderSource = R"(
#include "rafx.slang"

struct ScenePush {
    float4x4 transform;
    float4 color;
    float shape;
    float3 _pad;
};
RFX_PUSH_CONSTANTS(ScenePush, g_Scene);

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

[shader("vertex")]
VSOutput vsMain(uint id : SV_VertexID) {
    float2 verts[4] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5), float2(0.5, 0.5) };
    VSOutput output;
    output.pos = mul(g_Scene.transform, float4(verts[id], 0, 1));
    output.uv = verts[id];
    return output;
}

[shader("fragment")]
float4 fsMain(VSOutput input) : SV_Target {
    float2 p = input.uv;
    float d = 1.0;
    if (g_Scene.shape < 0.5) d = length(p) - 0.45;
    else if (g_Scene.shape < 1.5) {
        float2 q = abs(p) - 0.35;
        d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - 0.05;
    }
    else if (g_Scene.shape < 2.5) d = abs(length(p) - 0.35) - 0.05;
    else {
        float2 q = abs(p);
        d = (q.x + q.y) * 0.707 - 0.3;
        d = max(d, -max(0.4 - q.x, 0.4 - q.y));
        d = min(d, length(p) - 0.1);
    }
    float alpha = 1.0 - smoothstep(0.0, 0.02, d);
    if (alpha <= 0.0) discard;
    return float4(g_Scene.color.rgb * g_Scene.color.a, alpha);
}

struct BloomPush {
    uint inputId;
    uint outputId;
    float2 texelSize;
    float threshold;
};
RFX_PUSH_CONSTANTS(BloomPush, g_Bloom);

[shader("compute")]
[numthreads(8, 8, 1)]
void csBloom(uint32_t3 dtid : SV_DispatchThreadID) {
    Texture2D input = GetTexture(g_Bloom.inputId);
    RWTexture2D<float4> output = GetRWTexture(g_Bloom.outputId);
    SamplerState s = GetSamplerLinearClamp();
    uint w, h;
    output.GetDimensions(w, h);
    if (dtid.x >= w || dtid.y >= h) return;
    float2 uv = (float2(dtid.xy) + 0.5) * g_Bloom.texelSize;

#ifdef PASS_DOWN
    float2 off = g_Bloom.texelSize * 0.5;
    float3 A = input.SampleLevel(s, uv + float2(-off.x, -off.y) * 2.0, 0).rgb;
    float3 B = input.SampleLevel(s, uv + float2( 0.0,   -off.y) * 2.0, 0).rgb;
    float3 C = input.SampleLevel(s, uv + float2( off.x, -off.y) * 2.0, 0).rgb;
    float3 D = input.SampleLevel(s, uv + float2(-off.x,  0.0)   * 2.0, 0).rgb;
    float3 E = input.SampleLevel(s, uv, 0).rgb;
    float3 F = input.SampleLevel(s, uv + float2( off.x,  0.0)   * 2.0, 0).rgb;
    float3 G = input.SampleLevel(s, uv + float2(-off.x,  off.y) * 2.0, 0).rgb;
    float3 H = input.SampleLevel(s, uv + float2( 0.0,    off.y) * 2.0, 0).rgb;
    float3 I = input.SampleLevel(s, uv + float2( off.x,  off.y) * 2.0, 0).rgb;
    float3 color = (E * 0.125) + (A+C+G+I)*0.03125 + (B+D+F+H)*0.0625;
    if (dot(color, float3(0.2126, 0.7152, 0.0722)) < g_Bloom.threshold) color = float3(0.0);
    output[dtid.xy] = float4(color, 1.0);
#endif

#ifdef PASS_UP
    float2 off = g_Bloom.texelSize;
    float3 c = input.SampleLevel(s, uv, 0).rgb * 4.0;
    c += input.SampleLevel(s, uv + float2(-off.x, 0), 0).rgb * 2.0;
    c += input.SampleLevel(s, uv + float2( off.x, 0), 0).rgb * 2.0;
    c += input.SampleLevel(s, uv + float2(0, -off.y), 0).rgb * 2.0;
    c += input.SampleLevel(s, uv + float2(0,  off.y), 0).rgb * 2.0;
    c += input.SampleLevel(s, uv + float2(-off.x, -off.y), 0).rgb;
    c += input.SampleLevel(s, uv + float2( off.x, -off.y), 0).rgb;
    c += input.SampleLevel(s, uv + float2(-off.x,  off.y), 0).rgb;
    c += input.SampleLevel(s, uv + float2( off.x,  off.y), 0).rgb;
    output[dtid.xy] = float4(output[dtid.xy].rgb + (c * 0.0625), 1.0);
#endif
}

struct CompPush { uint hdrId; };
RFX_PUSH_CONSTANTS(CompPush, g_Comp);

[shader("vertex")]
void vsQuad(uint id : SV_VertexID, out float4 pos : SV_Position, out float2 uv : TEXCOORD) {
    float2 verts[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
    pos = float4(verts[id], 0, 1);
    uv = verts[id] * 0.5 + 0.5;
}

[shader("fragment")]
float4 fsComp(float2 uv : TEXCOORD) : SV_Target {
    float3 hdr = GetTexture(g_Comp.hdrId).Sample(GetSamplerLinearClamp(), uv).rgb;
    float3 ldr = hdr / (hdr + 1.0);
    return float4(pow(ldr, 1.0/2.2), 1.0);
}
)";

struct ScenePush {
    HMM_Mat4 transform;
    float r, g, b, intensity;
    float shape;
    float _pad[3];
};

int main(void) {
    const int WIDTH = 1280, HEIGHT = 720, MIP_COUNT = 7;
    if (!rfxOpenWindow("Rafx 2D Bloom", WIDTH, HEIGHT))
        return 1;

    RfxTextureDesc hdrDesc = {};
    hdrDesc.width = WIDTH;
    hdrDesc.height = HEIGHT;
    hdrDesc.mipLevels = MIP_COUNT;
    hdrDesc.format = RFX_FORMAT_RGBA16_FLOAT;
    hdrDesc.usage = RFX_TEXTURE_USAGE_SHADER_RESOURCE | RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_RENDER_TARGET;
    RfxTexture hdrTarget = rfxCreateTextureEx(&hdrDesc);

    const char *defDown[] = { "PASS_DOWN", "1" }, *defUp[] = { "PASS_UP", "1" };
    RfxShader sBase = rfxCompileShaderMem(bloomShaderSource, NULL, 0);
    RfxShader sDown = rfxCompileShaderMem(bloomShaderSource, defDown, 2);
    RfxShader sUp = rfxCompileShaderMem(bloomShaderSource, defUp, 2);

    RfxPipelineDesc pSceneDesc = {};
    pSceneDesc.shader = sBase;
    pSceneDesc.vsEntryPoint = "vsMain";
    pSceneDesc.psEntryPoint = "fsMain";
    pSceneDesc.colorFormat = RFX_FORMAT_RGBA16_FLOAT;
    pSceneDesc.topology = RFX_TOPOLOGY_TRIANGLE_STRIP;
    RfxPipeline pScene = rfxCreatePipeline(&pSceneDesc);

    RfxComputePipelineDesc cpDesc = {};
    cpDesc.shader = sDown;
    cpDesc.entryPoint = "csBloom";
    RfxPipeline pDown = rfxCreateComputePipeline(&cpDesc);
    cpDesc.shader = sUp;
    RfxPipeline pUp = rfxCreateComputePipeline(&cpDesc);

    RfxPipelineDesc pCompDesc = {};
    pCompDesc.shader = sBase;
    pCompDesc.vsEntryPoint = "vsQuad";
    pCompDesc.psEntryPoint = "fsComp";
    pCompDesc.colorFormat = rfxGetSwapChainFormat();
    pCompDesc.topology = RFX_TOPOLOGY_TRIANGLE_STRIP;
    RfxPipeline pComp = rfxCreatePipeline(&pCompDesc);

    RfxTexture views[MIP_COUNT];
    for (int i = 0; i < MIP_COUNT; i++)
        views[i] = rfxCreateTextureView(hdrTarget, RFX_FORMAT_RGBA16_FLOAT, i, 1, 0, 1);

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();
        float time = (float)rfxGetTime();
        float aspect = (float)WIDTH / HEIGHT;

        rfxCmdBeginRenderPass(cmd, &views[0], 1, NULL, RFX_COLORF(0, 0, 0, 1), 0);
        rfxCmdBindPipeline(cmd, pScene);

        auto draw = [&](HMM_Vec3 pos, HMM_Vec3 scale, HMM_Vec4 col, float intens, float shape, float rot = 0) {
            HMM_Mat4 m = HMM_Translate(pos);
            if (rot != 0)
                m = HMM_MulM4(m, HMM_Rotate_RH(rot, HMM_V3(0, 0, 1)));
            m = HMM_MulM4(m, HMM_Scale(HMM_V3(scale.X, scale.Y * aspect, 1.0f)));
            ScenePush push = { m, col.X, col.Y, col.Z, intens, shape };
            rfxCmdPushConstants(cmd, &push, sizeof(push));
            rfxCmdDraw(cmd, 4, 1);
        };

        draw(HMM_V3(-0.6f, 0.3f, 0), HMM_V3(0.3f, 0.3f, 1), HMM_V4(1, 0.2f, 0.2f, 1), 0.8f, 1.0f);
        draw(HMM_V3(0.5f, -0.2f + sinf(time * 1.5f) * 0.2f, 0), HMM_V3(0.4f, 0.4f, 1), HMM_V4(0, 1, 1, 1), 8.0f, 2.0f);
        draw(HMM_V3(-0.4f + sinf(time * 0.5f) * 0.1f, -0.4f, 0), HMM_V3(0.35f, 0.35f, 1), HMM_V4(1, 0.9f, 0, 1), 15.0f, 3.0f, time);
        draw(
            HMM_V3(0.5f + cosf(time * 2) * 0.15f, 0.4f + sinf(time * 2) * 0.15f, 0), HMM_V3(0.1f, 0.1f, 1), HMM_V4(1, 1, 1, 1), 4.0f, 0.0f
        );
        rfxCmdEndRenderPass(cmd);

        rfxCmdBindPipeline(cmd, pDown);
        for (int i = 0; i < MIP_COUNT - 1; i++) {
            uint32_t w = WIDTH >> (i + 1), h = HEIGHT >> (i + 1);
            struct {
                uint32_t i, o;
                HMM_Vec2 s;
                float t, p;
            } p = { rfxGetTextureId(views[i]), rfxGetTextureId(views[i + 1]), HMM_V2(1.0f / w, 1.0f / h), (i == 0) ? 1.0f : 0.0f };
            rfxCmdTransitionTexture(cmd, views[i], RFX_STATE_SHADER_READ);
            rfxCmdTransitionTexture(cmd, views[i + 1], RFX_STATE_SHADER_WRITE);
            rfxCmdPushConstants(cmd, &p, sizeof(p));
            rfxCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
        }

        rfxCmdBindPipeline(cmd, pUp);
        for (int i = MIP_COUNT - 1; i > 0; i--) {
            uint32_t w = WIDTH >> (i - 1), h = HEIGHT >> (i - 1);
            struct {
                uint32_t i, o;
                HMM_Vec2 s;
                float t, p;
            } p = { rfxGetTextureId(views[i]), rfxGetTextureId(views[i - 1]), HMM_V2(1.0f / w, 1.0f / h) };
            rfxCmdTransitionTexture(cmd, views[i], RFX_STATE_SHADER_READ);
            rfxCmdTransitionTexture(cmd, views[i - 1], RFX_STATE_SHADER_WRITE);
            rfxCmdPushConstants(cmd, &p, sizeof(p));
            rfxCmdDispatch(cmd, (w + 7) / 8, (h + 7) / 8, 1);
        }

        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLORF(0, 0, 0, 1));
        rfxCmdBindPipeline(cmd, pComp);
        rfxCmdTransitionTexture(cmd, views[0], RFX_STATE_SHADER_READ);
        uint32_t id = rfxGetTextureId(views[0]);
        rfxCmdPushConstants(cmd, &id, sizeof(id));
        rfxCmdDraw(cmd, 4, 1);
        rfxCmdEndRenderPass(cmd);
        rfxEndFrame();
    }

    for (int i = 0; i < MIP_COUNT; i++)
        rfxDestroyTexture(views[i]);
    rfxDestroyTexture(hdrTarget);
    rfxDestroyPipeline(pScene);
    rfxDestroyPipeline(pDown);
    rfxDestroyPipeline(pUp);
    rfxDestroyPipeline(pComp);
    rfxDestroyShader(sBase);
    rfxDestroyShader(sDown);
    rfxDestroyShader(sUp);
    return 0;
}
