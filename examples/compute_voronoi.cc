// An animated Voronoi diagram
//
// https://en.wikipedia.org/wiki/Voronoi_diagram

#include "rafx.h"
#include "imgui.h"
#include <vector>
#include <random>

const char* shaderSource = R"(
#include "rafx.slang"

struct Seed {
    float2 pos;   // 0-8
    float2 vel;   // 8-16
    float4 color; // 16-32
};

struct Uniforms {
    float2 resolution;
    float time;
    float dt;
    uint seedCount;
    float borderThickness;
    uint bufferId;
    uint distanceMode; // 0 = Euclidean, 1 = Manhattan
};

RFX_PUSH_CONSTANTS(Uniforms, g_Uniforms);

[shader("compute")]
[numthreads(64, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    uint index = id.x;
    if (index >= g_Uniforms.seedCount) return;

    RWByteAddressBuffer buffer = GetRWBuffer(g_Uniforms.bufferId);
    Seed s = buffer.Load<Seed>(index * sizeof(Seed));

    s.pos += s.vel * g_Uniforms.dt;

    if (s.pos.x < 0.0 || s.pos.x > 1.0) {
        s.vel.x *= -1.0;
        s.pos.x = clamp(s.pos.x, 0.0, 1.0);
    }
    if (s.pos.y < 0.0 || s.pos.y > 1.0) {
        s.vel.y *= -1.0;
        s.pos.y = clamp(s.pos.y, 0.0, 1.0);
    }

    buffer.Store<Seed>(index * sizeof(Seed), s);
}

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 grid = float2(float((vertexID << 1) & 2), float(vertexID & 2));
    float2 pos = grid * float2(2.0, -2.0) + float2(-1.0, 1.0);

    output.pos = float4(pos, 0.0, 1.0);
    output.uv = grid;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float2 uv = input.pos.xy / g_Uniforms.resolution;

    float aspect = g_Uniforms.resolution.x / g_Uniforms.resolution.y;
    float2 aspectCorrection = float2(aspect, 1.0);

    ByteAddressBuffer buffer = GetBuffer(g_Uniforms.bufferId);

    float minDist = 1000.0;
    float4 cellColor = float4(0,0,0,1);
    float2 cellPos = float2(0,0);

    for (uint i = 0; i < g_Uniforms.seedCount; i++) {
        Seed s = buffer.Load<Seed>(i * sizeof(Seed));

        float2 diff = (uv - s.pos) * aspectCorrection;

        float dist = 0.0;
        if (g_Uniforms.distanceMode == 1) {
            // manhattan
            dist = abs(diff.x) + abs(diff.y);
        } else {
            // euclidean
            dist = length(diff);
        }

        if (dist < minDist) {
            minDist = dist;
            cellColor = s.color;
            cellPos = s.pos;
        }
    }

    float centerDot = 1.0 - smoothstep(0.0, 0.01, minDist);
    float vignette = 1.0 - (minDist * 2.0);
    float3 finalColor = cellColor.rgb * vignette + centerDot;

    return float4(finalColor, 1.0);
}
)";

struct Seed {
    float x, y;       // float2 pos
    float vx, vy;     // float2 vel
    float r, g, b, a; // float4 color
};

struct Uniforms {
    float resX, resY;
    float time;
    float dt;
    uint32_t seedCount;
    float borderThickness;
    uint32_t bufferId;
    uint32_t distanceMode;
};

const int MAX_SEEDS = 512;

int main() {
    if (!rfxOpenWindow("Rafx Voronoi", 1280, 720))
        return 1;
    rfxSetWindowFlags(RFX_WINDOW_VSYNC | RFX_WINDOW_ALWAYS_ACTIVE);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();

    rfxInitImGui();

    auto generateSeeds = []() {
        std::vector<Seed> seeds(MAX_SEEDS);
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> distPos(0.0f, 1.0f);
        std::uniform_real_distribution<float> distVel(-0.2f, 0.2f);
        std::uniform_real_distribution<float> distCol(0.2f, 0.9f);

        for (auto& s : seeds) {
            s.x = distPos(rng);
            s.y = distPos(rng);
            s.vx = distVel(rng);
            s.vy = distVel(rng);
            s.r = distCol(rng);
            s.g = distCol(rng);
            s.b = distCol(rng);
            s.a = 1.0f;
        }
        return seeds;
    };

    std::vector<Seed> initialSeeds = generateSeeds();

    RfxBuffer seedBuffer = rfxCreateBuffer(
        sizeof(Seed) * MAX_SEEDS, sizeof(Seed), RFX_USAGE_SHADER_RESOURCE | RFX_USAGE_SHADER_RESOURCE_STORAGE | RFX_USAGE_TRANSFER_DST,
        RFX_MEM_GPU_ONLY, initialSeeds.data()
    );

    RfxShader shader = rfxCompileShaderMem(shaderSource, nullptr, 0);

    RfxComputePipelineDesc computeDesc = { .shader = shader, .entryPoint = "computeMain" };
    RfxPipeline computePipeline = rfxCreateComputePipeline(&computeDesc);

    RfxPipelineDesc graphicsDesc = {};
    graphicsDesc.shader = shader;
    graphicsDesc.vsEntryPoint = "vertexMain";
    graphicsDesc.psEntryPoint = "fragmentMain";
    graphicsDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    graphicsDesc.colorFormat = rfxGetSwapChainFormat();
    graphicsDesc.vertexLayout = nullptr;
    graphicsDesc.vertexLayoutCount = 0;
    graphicsDesc.depthFormat = RFX_FORMAT_D32_FLOAT;

    RfxPipeline graphicsPipeline = rfxCreatePipeline(&graphicsDesc);

    Uniforms uniforms = {};
    uniforms.seedCount = 64;
    uniforms.borderThickness = 0.002f;
    uniforms.distanceMode = 0;

    int currentDistanceMode = 0; // 0 = Euclidean, 1 = Manhattan

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();

        uniforms.resX = (float)rfxGetWindowWidth();
        uniforms.resY = (float)rfxGetWindowHeight();
        uniforms.time = (float)rfxGetTime();
        uniforms.dt = rfxGetDeltaTime();
        uniforms.bufferId = rfxGetBufferId(seedBuffer);
        uniforms.distanceMode = (uint32_t)currentDistanceMode;

        // UI
        float mx, my;
        rfxGetMousePos(&mx, &my);
        io.DisplaySize = ImVec2((float)rfxGetWindowWidth(), (float)rfxGetWindowHeight());
        io.DeltaTime = rfxGetDeltaTime();
        io.MousePos = ImVec2(mx, my);
        io.MouseDown[0] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT);
        io.MouseDown[1] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_RIGHT);
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(0, 0));
        ImGui::Begin("Compute Voronoi");
        ImGui::SliderInt("Seed Count", (int*)&uniforms.seedCount, 2, MAX_SEEDS);

        ImGui::Text("Distance Metric:");
        ImGui::RadioButton("Euclidean", &currentDistanceMode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Manhattan", &currentDistanceMode, 1);

        if (ImGui::Button("Reset Seeds")) {
            std::vector<Seed> newSeeds = generateSeeds();
            RfxBuffer stagingBuffer =
                rfxCreateBuffer(sizeof(Seed) * MAX_SEEDS, sizeof(Seed), RFX_USAGE_TRANSFER_SRC, RFX_MEM_CPU_TO_GPU, newSeeds.data());
            rfxCmdCopyBuffer(cmd, stagingBuffer, 0, seedBuffer, 0, sizeof(Seed) * MAX_SEEDS);
            rfxDestroyBuffer(stagingBuffer);
        }
        ImGui::End();
        ImGui::Render();

        rfxCmdBindPipeline(cmd, computePipeline);
        rfxCmdPushConstants(cmd, &uniforms, sizeof(Uniforms));
        rfxCmdDispatch(cmd, (uniforms.seedCount + 63) / 64, 1, 1);

        // render
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(0, 0, 0, 255));

        rfxCmdBindPipeline(cmd, graphicsPipeline);
        rfxCmdPushConstants(cmd, &uniforms, sizeof(Uniforms));
        rfxCmdDraw(cmd, 3, 1);
        rfxCmdEndRenderPass(cmd);

        // render ui
        RfxImGuiDrawData uiData = {};
        ImDrawData* drawData = ImGui::GetDrawData();
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        uiData.drawLists = (void* const*)drawData->CmdLists.Data;
        uiData.drawListCount = drawData->CmdLists.Size;
        uiData.textures = (void* const*)platformIO.Textures.Data;
        uiData.textureCount = platformIO.Textures.Size;
        uiData.displayWidth = drawData->DisplaySize.x;
        uiData.displayHeight = drawData->DisplaySize.y;
        uiData.linearColor = false;

        rfxCmdDrawImGui(cmd, &uiData);

        rfxEndFrame();
    }

    rfxDestroyBuffer(seedBuffer);
    rfxDestroyPipeline(computePipeline);
    rfxDestroyPipeline(graphicsPipeline);
    rfxDestroyShader(shader);
    rfxShutdownImGui();
    ImGui::DestroyContext();

    return 0;
}
