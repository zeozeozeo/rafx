#include "rafx.h"
#include <math.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct Uniforms {
    float time;
    uint width;
    uint height;
    uint textureId;
};
RFX_PUSH_CONSTANTS(Uniforms, g_Uniforms);

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_Uniforms.width || id.y >= g_Uniforms.height) return;
    float2 uv = float2(id.xy) / float2(g_Uniforms.width, g_Uniforms.height);
    float t = g_Uniforms.time;
    float v = sin(uv.x * 10.0 + t) + sin((uv.y * 10.0 + t) * 0.5) + sin((uv.x + uv.y) * 10.0 - t);
    float2 c = uv * 2.0 - 1.0;
    v += sin(sqrt(c.x*c.x + c.y*c.y) * 10.0 + t);
    float3 color = float3(sin(v * 3.14), sin(v * 3.14 + 2.0), sin(v * 3.14 + 4.0));
    RWTexture2D<float4> outTex = GetRWTexture(g_Uniforms.textureId);
    outTex[id.xy] = float4(color, 1.0);
}

struct VSOutput { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

[shader("vertex")]
VSOutput vertexMain(uint id : SV_VertexID) {
    VSOutput output;
    float2 pos = float2((id << 1) & 2, id & 2);
    output.pos = float4(pos * float2(2, -2) + float2(-1, 1), 0, 1);
    output.uv = pos;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return GetTexture(g_Uniforms.textureId).Sample(GetSamplerLinearClamp(), input.uv);
}
)";

struct Uniforms {
    float time;
    uint32_t width;
    uint32_t height;
    uint32_t textureId;
};

const int FRAMES_IN_FLIGHT = 3;

int main() {
    rfxSetWindowFlags(RFX_WINDOW_VSYNC | RFX_WINDOW_NO_RESIZE);
    if (!rfxOpenWindow("Rafx Async Compute", 1280, 720))
        return 1;

    RfxTexture computeTextures[FRAMES_IN_FLIGHT];
    RfxFence computeFences[FRAMES_IN_FLIGHT];
    uint64_t computeFenceValues[FRAMES_IN_FLIGHT] = { 0 };

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        computeTextures[i] =
            rfxCreateTexture(1280, 720, RFX_FORMAT_RGBA8_UNORM, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        computeFences[i] = rfxCreateFence(0);
    }

    RfxCommandList computeCmd = rfxCreateCommandList(RFX_QUEUE_COMPUTE);

    RfxShader shader = rfxCompileShaderMem(shaderSource, nullptr, 0);

    RfxComputePipelineDesc cpd = {};
    cpd.shader = shader;
    cpd.entryPoint = "computeMain";
    RfxPipeline computePipeline = rfxCreateComputePipeline(&cpd);

    RfxPipelineDesc gpd = {};
    gpd.shader = shader;
    gpd.vsEntryPoint = "vertexMain";
    gpd.psEntryPoint = "fragmentMain";
    gpd.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    gpd.colorFormat = rfxGetSwapChainFormat();
    RfxPipeline graphicsPipeline = rfxCreatePipeline(&gpd);

    uint64_t frameIndex = 0;

    while (!rfxWindowShouldClose()) {
        int frameSlot = frameIndex % FRAMES_IN_FLIGHT;
        RfxTexture currentTexture = computeTextures[frameSlot];
        RfxFence currentFence = computeFences[frameSlot];

        // async commandlist
        rfxBeginCommandList(computeCmd);

        rfxCmdTransitionTexture(computeCmd, currentTexture, RFX_STATE_SHADER_WRITE);
        rfxCmdBindPipeline(computeCmd, computePipeline);

        Uniforms u = { (float)rfxGetTime(), 1280, 720, rfxGetTextureId(currentTexture) };
        rfxCmdPushConstants(computeCmd, &u, sizeof(u));
        rfxCmdDispatch(computeCmd, (1280 + 15) / 16, (720 + 15) / 16, 1);

        rfxCmdTransitionTexture(computeCmd, currentTexture, RFX_STATE_SHADER_READ);
        rfxEndCommandList(computeCmd);

        // submit commandlist
        uint64_t signalValue = ++computeFenceValues[frameSlot];
        rfxSubmitCommandListAsync(computeCmd, nullptr, nullptr, 0, &currentFence, &signalValue, 1);

        // graphics
        rfxBeginFrame();
        RfxCommandList gfxCmd = rfxGetCommandList();

        // wait for compute
        rfxSubmitCommandListAsync(nullptr, &currentFence, &signalValue, 1, nullptr, nullptr, 0);

        rfxCmdBeginSwapchainRenderPass(gfxCmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(0, 0, 0, 1));
        rfxCmdBindPipeline(gfxCmd, graphicsPipeline);
        rfxCmdPushConstants(gfxCmd, &u, sizeof(u));
        rfxCmdDraw(gfxCmd, 3, 1);
        rfxCmdEndRenderPass(gfxCmd);

        rfxEndFrame();
        frameIndex++;
    }

    rfxDestroyCommandList(computeCmd);
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        rfxDestroyTexture(computeTextures[i]);
        rfxDestroyFence(computeFences[i]);
    }
    rfxDestroyPipeline(computePipeline);
    rfxDestroyPipeline(graphicsPipeline);
    rfxDestroyShader(shader);

    return 0;
}
