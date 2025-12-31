// A GPU compute particle simulation that mimics the flocking behavior of birds. A compute shader updates two ping-pong buffers which store
// particle data. The data is used to draw instanced particles.
//
// Uses the Rafx ImGui extension (rfxCmdDrawImGui) for UI.
//
// https://en.wikipedia.org/wiki/Boids

#include "rafx.h"
#include <imgui.h>
#include <vector>
#include <random>
#include <stdio.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct Particle {
    float2 position;
    float2 velocity;
    float4 color;
};

struct Uniforms {
    float2 targetPos;
    float dt;
    float time;
    uint particleCount;
    float mouseInteractionStrength;

    float separationRadius;
    float alignmentRadius;
    float cohesionRadius;
    float aspectRatio;

    uint readBufferId;  // source in compute or vertex
    uint writeBufferId; // dest in compute
};

RFX_PUSH_CONSTANTS(Uniforms, g_Uniforms);

#define BLOCK_SIZE 256
groupshared Particle sharedParticles[BLOCK_SIZE];

float2 limit(float2 v, float maxLen) {
    float len = length(v);
    if (len > maxLen && len > 0.0) return (v / len) * maxLen;
    return v;
}

float3 palette(float t) {
    float3 a = float3(0.5, 0.5, 0.5);
    float3 b = float3(0.5, 0.5, 0.5);
    float3 c = float3(1.0, 1.0, 1.0);
    float3 d = float3(0.263, 0.416, 0.557);
    return a + b * cos(6.28318 * (c * t + d));
}

[shader("compute")]
[numthreads(BLOCK_SIZE, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex) {
    uint index = id.x;
    if (index >= g_Uniforms.particleCount) return;

    // previous frame buffer
    ByteAddressBuffer srcBuffer = GetBuffer(g_Uniforms.readBufferId);
    Particle self = srcBuffer.Load<Particle>(index * sizeof(Particle));

    float2 pos = self.position;
    float2 vel = self.velocity;

    float2 forceSep = float2(0, 0);
    float2 forceAlign = float2(0, 0);
    float2 centerOfMass = float2(0, 0);
    uint countSep = 0;
    uint countAlign = 0;
    uint countCohesion = 0;

    uint numTiles = (g_Uniforms.particleCount + BLOCK_SIZE - 1) / BLOCK_SIZE;

    for (uint tile = 0; tile < numTiles; tile++) {
        uint loadIndex = tile * BLOCK_SIZE + groupIndex;
        if (loadIndex < g_Uniforms.particleCount) {
            sharedParticles[groupIndex] = srcBuffer.Load<Particle>(loadIndex * sizeof(Particle));
        } else {
            sharedParticles[groupIndex].position = float2(10000.0, 10000.0);
            sharedParticles[groupIndex].velocity = float2(0.0, 0.0);
        }

        GroupMemoryBarrierWithGroupSync();

        for (uint i = 0; i < BLOCK_SIZE; i++) {
            uint otherIndex = tile * BLOCK_SIZE + i;
            if (otherIndex == index || otherIndex >= g_Uniforms.particleCount) continue;

            Particle other = sharedParticles[i];
            float2 diff = pos - other.position;
            float distSq = dot(diff, diff);

            float sepR2 = g_Uniforms.separationRadius * g_Uniforms.separationRadius;
            float alignR2 = g_Uniforms.alignmentRadius * g_Uniforms.alignmentRadius;
            float coheR2 = g_Uniforms.cohesionRadius * g_Uniforms.cohesionRadius;

            if (distSq < sepR2 && distSq > 0.000001) {
                forceSep += diff / distSq;
                countSep++;
            }
            if (distSq < alignR2) {
                forceAlign += other.velocity;
                countAlign++;
            }
            if (distSq < coheR2) {
                centerOfMass += other.position;
                countCohesion++;
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }

    float2 acc = float2(0, 0);

    // separation
    if (countSep > 0) {
        forceSep /= float(countSep);
        if (length(forceSep) > 0) {
            forceSep = normalize(forceSep) * 2.0;
            forceSep -= vel;
            acc += forceSep * 1.8;
        }
    }
    // alignment
    if (countAlign > 0) {
        forceAlign /= float(countAlign);
        if (length(forceAlign) > 0) {
            forceAlign = normalize(forceAlign) * 2.0;
            forceAlign -= vel;
            acc += forceAlign * 1.2;
        }
    }
    // cohesion
    if (countCohesion > 0) {
        centerOfMass /= float(countCohesion);
        float2 dir = centerOfMass - pos;
        if (length(dir) > 0) {
            dir = normalize(dir) * 2.0;
            dir -= vel;
            acc += dir * 0.6;
        }
    }

    // mouse
    float2 toMouse = g_Uniforms.targetPos - pos;
    float distToMouse = length(toMouse);

    if (g_Uniforms.mouseInteractionStrength > 0.0) {
        float2 dir = normalize(toMouse);
        acc += dir * 1.5 * g_Uniforms.mouseInteractionStrength;
    } else if (g_Uniforms.mouseInteractionStrength < 0.0) {
        if (distToMouse < 0.4) {
            float strength = (1.0 - (distToMouse / 0.4));
            acc -= normalize(toMouse) * strength * 15.0;
        }
    }

    // boundary wrapping
    float limitX = 1.05 * g_Uniforms.aspectRatio;
    if (pos.x > limitX) pos.x = -limitX;
    if (pos.x < -limitX) pos.x = limitX;
    if (pos.y > 1.05) pos.y = -1.05;
    if (pos.y < -1.05) pos.y = 1.05;

    vel += acc * g_Uniforms.dt;
    vel = limit(vel, 0.8);
    pos += vel * g_Uniforms.dt;

    float speed = length(vel);
    float3 col = palette((speed * 0.4) + (float(index) * 0.00005) + (g_Uniforms.time * 0.1));
    float glow = 1.0 - smoothstep(0.0, 0.3, distToMouse);
    col += float3(0.5, 0.8, 1.0) * glow * 0.8;

    self.position = pos;
    self.velocity = vel;
    self.color = float4(col, 1.0);

    // current frame buffer
    RWByteAddressBuffer dstBuffer = GetRWBuffer(g_Uniforms.writeBufferId);
    dstBuffer.Store<Particle>(index * sizeof(Particle), self);
}

struct VSOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;

    ByteAddressBuffer buf = GetBuffer(g_Uniforms.readBufferId);
    Particle p = buf.Load<Particle>(instanceID * sizeof(Particle));

    float2 localPos;
    if (vertexID == 0) localPos = float2(0.0, 0.008);
    else if (vertexID == 1) localPos = float2(0.005, -0.008);
    else localPos = float2(-0.005, -0.008);

    float2 v = normalize(p.velocity + 1e-5);
    float angle = atan2(v.y, v.x) - 1.57079;
    float c = cos(angle);
    float s = sin(angle);

    float2 rotatedPos = float2(
        localPos.x * c - localPos.y * s,
        localPos.x * s + localPos.y * c
    );

    float2 finalPos = p.position + rotatedPos;
    finalPos.x /= g_Uniforms.aspectRatio;
    finalPos.y *= -1.0;

    output.position = float4(finalPos, 0.0, 1.0);
    output.color = p.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return input.color;
}
)";

struct Particle {
    float x, y;
    float vx, vy;
    float r, g, b, a;
};

struct Uniforms {
    float targetX, targetY; // float2
    float dt;
    float time;
    uint32_t particleCount;
    float mouseInteractionStrength;

    float separationRadius;
    float alignmentRadius;
    float cohesionRadius;
    float aspectRatio;

    uint32_t readBufferId;
    uint32_t writeBufferId;
};

const uint32_t PARTICLE_COUNT = 8192;
const uint32_t THREAD_GROUP_SIZE = 256;

int main(void) {
    if (!rfxOpenWindow("Rafx Compute Boids", 1280, 720))
        return 1;

    rfxSetWindowFlags(RFX_WINDOW_ALWAYS_ACTIVE | RFX_WINDOW_VSYNC);

    // imgui setup
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::StyleColorsDark();

    if (!rfxInitImGui()) {
        printf("Failed to init ImGui renderer\n");
        return 1;
    }

    std::vector<Particle> initialParticles(PARTICLE_COUNT);
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> distPos(-0.9f, 0.9f);
    std::uniform_real_distribution<float> distVel(-0.5f, 0.5f);

    for (auto& p : initialParticles) {
        p.x = distPos(rng);
        p.y = distPos(rng);
        p.vx = distVel(rng);
        p.vy = distVel(rng);
        p.r = 1.0f;
        p.g = 1.0f;
        p.b = 1.0f;
        p.a = 1.0f;
    }

    // ping pong buffers
    RfxBuffer particleBuffers[2];
    particleBuffers[0] = rfxCreateBuffer(
        initialParticles.size() * sizeof(Particle), sizeof(Particle), RFX_USAGE_SHADER_RESOURCE_STORAGE | RFX_USAGE_SHADER_RESOURCE,
        RFX_MEM_GPU_ONLY, initialParticles.data()
    );
    particleBuffers[1] = rfxCreateBuffer(
        initialParticles.size() * sizeof(Particle), sizeof(Particle), RFX_USAGE_SHADER_RESOURCE_STORAGE | RFX_USAGE_SHADER_RESOURCE,
        RFX_MEM_GPU_ONLY, initialParticles.data()
    );

    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);

    // pipelines
    RfxComputePipelineDesc computeDesc = {};
    computeDesc.shader = shader;
    RfxPipeline computePipeline = rfxCreateComputePipeline(&computeDesc);

    RfxPipelineDesc graphicsDesc = {};
    graphicsDesc.shader = shader;
    graphicsDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    graphicsDesc.colorFormat = rfxGetSwapChainFormat();
    graphicsDesc.blendState = { .blendEnabled = true,
                                .srcColor = RFX_BLEND_FACTOR_ONE,
                                .dstColor = RFX_BLEND_FACTOR_ONE,
                                .colorOp = RFX_BLEND_OP_ADD,
                                .srcAlpha = RFX_BLEND_FACTOR_ONE,
                                .dstAlpha = RFX_BLEND_FACTOR_ONE,
                                .alphaOp = RFX_BLEND_OP_ADD,
                                .writeMask = RFX_COLOR_WRITE_ALL };

    RfxPipeline graphicsPipeline = rfxCreatePipeline(&graphicsDesc);

    Uniforms uniforms = {};
    uniforms.particleCount = PARTICLE_COUNT;
    uniforms.separationRadius = 0.03f;
    uniforms.alignmentRadius = 0.10f;
    uniforms.cohesionRadius = 0.15f;

    uint64_t frameIndex = 0;
    bool vsync = true;

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();

        int winW = rfxGetWindowWidth();
        int winH = rfxGetWindowHeight();
        float aspectRatio = (float)winW / (float)winH;

        float dt = rfxGetDeltaTime();
        if (dt > 0.05f)
            dt = 0.05f;
        uniforms.dt = dt < 0.03f ? dt : 0.03f;

        float time = (float)rfxGetTime();
        uniforms.time = time;
        uniforms.aspectRatio = aspectRatio;

        float mx, my;
        rfxGetMousePos(&mx, &my);

        // UI
        io.DisplaySize = ImVec2((float)winW, (float)winH);
        io.DeltaTime = rfxGetDeltaTime();
        io.MousePos = ImVec2(mx, my);
        io.MouseDown[0] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT);
        io.MouseDown[1] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_RIGHT);

        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 185), ImGuiCond_FirstUseEver);
        ImGui::Begin("Boids Settings");
        ImGui::Text("FPS: %.1f", 1.0f / rfxGetDeltaTime());
        ImGui::Separator();
        ImGui::SliderFloat("Separation", &uniforms.separationRadius, 0.0f, 0.2f);
        ImGui::SliderFloat("Alignment", &uniforms.alignmentRadius, 0.0f, 0.2f);
        ImGui::SliderFloat("Cohesion", &uniforms.cohesionRadius, 0.0f, 0.2f);
        if (ImGui::Button("Reset")) {
            uniforms.separationRadius = 0.03f;
            uniforms.alignmentRadius = 0.10f;
            uniforms.cohesionRadius = 0.15f;
        }
        if (ImGui::Checkbox("VSync", &vsync))
            rfxSetWindowFlags(vsync ? RFX_WINDOW_ALWAYS_ACTIVE | RFX_WINDOW_VSYNC : RFX_WINDOW_ALWAYS_ACTIVE);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Controls: LMB = Attract, RMB = Repel");
        ImGui::End();
        ImGui::Render();
        //

        uniforms.targetX = ((mx / (float)winW) * 2.0f - 1.0f) * aspectRatio;
        uniforms.targetY = (my / (float)winH) * 2.0f - 1.0f;

        if (!io.WantCaptureMouse) {
            if (rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_RIGHT)) {
                uniforms.mouseInteractionStrength = -1.5f; // repel
            } else if (rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT)) {
                uniforms.mouseInteractionStrength = 2.0f; // attract
            } else {
                uniforms.mouseInteractionStrength = 0.0f;
            }
        } else {
            uniforms.mouseInteractionStrength = 0.0f;
        }

        uint32_t readIdx = frameIndex % 2;
        uint32_t writeIdx = (frameIndex + 1) % 2;

        // compute pass
        rfxCmdBindPipeline(cmd, computePipeline);

        uniforms.readBufferId = rfxGetBufferId(particleBuffers[readIdx]);
        uniforms.writeBufferId = rfxGetBufferId(particleBuffers[writeIdx]);

        rfxCmdPushConstants(cmd, &uniforms, sizeof(Uniforms));
        rfxCmdDispatch(cmd, (PARTICLE_COUNT + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE, 1, 1);

        // graphics pass
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(10, 12, 20, 255));

        rfxCmdBindPipeline(cmd, graphicsPipeline);

        uniforms.readBufferId = uniforms.writeBufferId;
        rfxCmdPushConstants(cmd, &uniforms, sizeof(Uniforms));

        rfxCmdDraw(cmd, 3, PARTICLE_COUNT);
        rfxCmdEndRenderPass(cmd);

        // imgui
        ImDrawData* drawData = ImGui::GetDrawData();
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();

        RfxImGuiDrawData rfxImguiData = {};
        rfxImguiData.drawLists = (void* const*)drawData->CmdLists.Data;
        rfxImguiData.drawListCount = drawData->CmdLists.Size;
        rfxImguiData.textures = (void* const*)platformIO.Textures.Data;
        rfxImguiData.textureCount = platformIO.Textures.Size;
        rfxImguiData.displayWidth = drawData->DisplaySize.x;
        rfxImguiData.displayHeight = drawData->DisplaySize.y;
        rfxImguiData.hdrScale = 1.0f;
        rfxImguiData.linearColor = false;

        rfxCmdDrawImGui(cmd, &rfxImguiData);

        rfxEndFrame();
        frameIndex++;
    }

    // 4. Cleanup
    rfxDestroyBuffer(particleBuffers[0]);
    rfxDestroyBuffer(particleBuffers[1]);
    rfxDestroyPipeline(computePipeline);
    rfxDestroyPipeline(graphicsPipeline);
    rfxDestroyShader(shader);

    rfxShutdownImGui();
    ImGui::DestroyContext();

    return 0;
}
