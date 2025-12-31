#include "HandmadeMath.h"
#include "rafx.h"
#include <imgui.h>

#include <cmath>
#include <cstdio>

const char* kSceneShader = R"(
#include "rafx.slang"

struct SceneUniforms {
    float4x4 viewProj;
    float4x4 prevViewProj;
    float4   skyColorHorizon;
    float4   skyColorZenith;
    float4   sunDir;
    float2   jitter;
    float    mipBias;
    float    time;
};
RFX_PUSH_CONSTANTS(SceneUniforms, ubo);

struct VSIn {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOut {
    float4 pos      : SV_Position;
    float4 curPos   : POSITION0;
    float4 prevPos  : POSITION1;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float3 worldPos : POSITION2;
};

float3 GetAlbedo(float2 uv) {
    float2 cells = uv * 4.0;
    float2 width = fwidth(cells);
    float2 edge = smoothstep(0.5 - width, 0.5 + width, frac(cells));
    float checker = abs(edge.x - edge.y);
    return lerp(float3(0.1, 0.1, 0.12), float3(0.4, 0.4, 0.45), checker);
}

float hash(float n) { return frac(sin(n) * 43758.5453123); }

[shader("vertex")]
VSOut vsMain(VSIn input, uint instanceId : SV_InstanceID) {
    VSOut o;

    // procedural grid
    float blockSize = 1.0;
    float gap = 0.2;
    float stride = blockSize + gap;
    int gridSize = 40;

    int x = instanceId % gridSize;
    int z = instanceId / gridSize;

    float hRand = hash(float(instanceId));
    float heightScale = 1.0 + pow(hRand, 3.0) * 15.0;

    float3 worldPos = input.position;

    // scale
    worldPos.y += 0.5;
    worldPos.y *= heightScale;

    // width
    float widthScale = 0.4 + (hash(float(instanceId) * 13.0) * 0.5);
    worldPos.x *= widthScale;
    worldPos.z *= widthScale;

    // grid
    float3 offset = float3((x - gridSize/2) * stride, 0, (z - gridSize/2) * stride);
    worldPos += offset;

    o.worldPos = worldPos;
    o.normal = input.normal;
    o.uv = input.uv * float2(1.0, heightScale);

    o.pos = mul(ubo.viewProj, float4(worldPos, 1.0));
    o.curPos = o.pos; // current jittered position
    o.prevPos = mul(ubo.prevViewProj, float4(worldPos, 1.0)); // previous unjittered position

    return o;
}

struct PSOut {
    float4 color  : SV_Target0;
    float4 motion : SV_Target1;
};

[shader("fragment")]
PSOut psMain(VSOut i) {
    PSOut o;

    float3 N = normalize(i.normal);
    float3 L = normalize(ubo.sunDir.xyz);

    float NdotL = max(dot(N, L), 0.0);
    float3 albedo = GetAlbedo(i.uv);

    // blue stripes
    if (i.worldPos.y > 1.0 && abs(N.y) < 0.1) {
        float stripe = step(0.95, frac(i.worldPos.y * 0.5 + ubo.time * 0.1));
        albedo += stripe * float3(0.0, 0.8, 1.0) * 2.0;
    }

    float3 ambient = float3(0.02, 0.02, 0.05);
    float3 litColor = albedo * (NdotL + ambient);

    // fog
    float dist = length(i.worldPos.xz);
    float fogDensity = 0.04;
    float fogFactor = 1.0 / exp(pow(dist * fogDensity, 2.0));
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    float3 skyColor = lerp(ubo.skyColorHorizon.rgb, ubo.skyColorZenith.rgb, 0.5);
    float3 finalColor = lerp(skyColor, litColor, fogFactor);

    o.color = float4(finalColor, 1.0);

    // MV
    // NDC [-1, 1]
    float2 curNDC = i.curPos.xy / i.curPos.w;
    float2 prevNDC = i.prevPos.xy / i.prevPos.w;

    // remove jitter from current
    float2 unjitteredCurNDC = curNDC - ubo.jitter;

    // velocity in NDC [-2, 2]
    float2 velocityNDC = unjitteredCurNDC - prevNDC;

    // output UV delta
    o.motion = float4(velocityNDC * 0.5, 0.0, 0.0);

    return o;
}
)";

const char* kFullscreenTriShader = R"(
#include "rafx.slang"
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };

struct PC {
    uint texID;
    float4 skyHorizon;
    float4 skyZenith;
};
RFX_PUSH_CONSTANTS(PC, pc);

[shader("vertex")]
VSOut vsMain(uint vI : SV_VertexID) {
    VSOut o;
    o.uv = float2((vI << 1) & 2, vI & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 psMain(VSOut i) : SV_Target {
    float3 col = GetTexture(pc.texID).SampleLevel(GetSamplerLinearClamp(), i.uv, 0).rgb;

    // Reinhard
    col = col / (col + 1.0);
    // Gamma
    col = pow(col, 1.0/2.2);
    return float4(col, 1.0);
}
)";

struct SceneUniforms {
    HMM_Mat4 viewProj;
    HMM_Mat4 prevViewProj;
    HMM_Vec4 skyColorHorizon;
    HMM_Vec4 skyColorZenith;
    HMM_Vec4 sunDir;
    HMM_Vec2 jitter;
    float mipBias;
    float time;
};

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
};

const Vertex kCubeVertices[] = {
    // Front
    { -0.5f, -0.5f, 0.5f, 0, 0, 1, 0, 1 },
    { 0.5f, -0.5f, 0.5f, 0, 0, 1, 1, 1 },
    { 0.5f, 0.5f, 0.5f, 0, 0, 1, 1, 0 },
    { -0.5f, 0.5f, 0.5f, 0, 0, 1, 0, 0 },
    // Back
    { 0.5f, -0.5f, -0.5f, 0, 0, -1, 0, 1 },
    { -0.5f, -0.5f, -0.5f, 0, 0, -1, 1, 1 },
    { -0.5f, 0.5f, -0.5f, 0, 0, -1, 1, 0 },
    { 0.5f, 0.5f, -0.5f, 0, 0, -1, 0, 0 },
    // Top
    { -0.5f, 0.5f, 0.5f, 0, 1, 0, 0, 1 },
    { 0.5f, 0.5f, 0.5f, 0, 1, 0, 1, 1 },
    { 0.5f, 0.5f, -0.5f, 0, 1, 0, 1, 0 },
    { -0.5f, 0.5f, -0.5f, 0, 1, 0, 0, 0 },
    // Bottom
    { -0.5f, -0.5f, -0.5f, 0, -1, 0, 0, 1 },
    { 0.5f, -0.5f, -0.5f, 0, -1, 0, 1, 1 },
    { 0.5f, -0.5f, 0.5f, 0, -1, 0, 1, 0 },
    { -0.5f, -0.5f, 0.5f, 0, -1, 0, 0, 0 },
    // Right
    { 0.5f, -0.5f, 0.5f, 1, 0, 0, 0, 1 },
    { 0.5f, -0.5f, -0.5f, 1, 0, 0, 1, 1 },
    { 0.5f, 0.5f, -0.5f, 1, 0, 0, 1, 0 },
    { 0.5f, 0.5f, 0.5f, 1, 0, 0, 0, 0 },
    // Left
    { -0.5f, -0.5f, -0.5f, -1, 0, 0, 0, 1 },
    { -0.5f, -0.5f, 0.5f, -1, 0, 0, 1, 1 },
    { -0.5f, 0.5f, 0.5f, -1, 0, 0, 1, 0 },
    { -0.5f, 0.5f, -0.5f, -1, 0, 0, 0, 0 },
};

const uint16_t kCubeIndices[] = { 0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
                                  12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20 };

float Halton(int index, int base) {
    float f = 1, r = 0;
    while (index > 0) {
        f = f / (float)base;
        r = r + f * (float)(index % base);
        index = index / base;
    }
    return r;
}

struct App {
    int displayWidth = 1600;
    int displayHeight = 900;
    int renderWidth = 0;
    int renderHeight = 0;

    RfxBuffer vBuffer = nullptr;
    RfxBuffer iBuffer = nullptr;
    RfxShader shScene = nullptr;
    RfxPipeline psoScene = nullptr;
    RfxShader shPresent = nullptr;
    RfxPipeline psoPresent = nullptr;

    RfxTexture rtColor = nullptr;
    RfxTexture rtMotion = nullptr;
    RfxTexture rtDepth = nullptr;
    RfxTexture rtUpscaled = nullptr;

    RfxUpscaler upscaler = nullptr;
    RfxUpscalerType upscalerType = RFX_UPSCALER_NIS; // guaranteed to be supported everywhere
    RfxUpscalerMode upscalerMode = RFX_UPSCALER_MODE_QUALITY;
    float currentMipBias = 0.0f;
    bool upscalerSupported[5] = { false };

    // Settings
    float sharpness = 0.3f;
    bool enableJitter = false; // nis
    bool upscalerEnabled = true;
    bool recreatePending = false;

    // Camera
    float camYaw = 0.7f;
    float camPitch = 0.4f;
    float camDist = 25.0f;
    HMM_Mat4 prevViewProj = HMM_M4D(1.0f);
    int frameIndex = 0;

    void Init() {
        rfxSetWindowFlags(RFX_WINDOW_ALWAYS_ACTIVE);
        if (!rfxOpenWindow("Rafx Upscaler Demo", displayWidth, displayHeight))
            abort();

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::StyleColorsDark();
        rfxInitImGui();

        vBuffer = rfxCreateBuffer(sizeof(kCubeVertices), sizeof(Vertex), RFX_USAGE_VERTEX_BUFFER, RFX_MEM_GPU_ONLY, kCubeVertices);
        iBuffer = rfxCreateBuffer(sizeof(kCubeIndices), sizeof(uint16_t), RFX_USAGE_INDEX_BUFFER, RFX_MEM_GPU_ONLY, kCubeIndices);

        shScene = rfxCompileShaderMem(kSceneShader, nullptr, 0);

        RfxVertexLayoutElement layout[] = {
            { 0, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, x), "POSITION" },
            { 1, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, nx), "NORMAL" },
            { 2, RFX_FORMAT_RG32_FLOAT, offsetof(Vertex, u), "TEXCOORD" },
        };

        // gbuffer: hdr color, MV
        RfxAttachmentDesc attachments[] = { { RFX_FORMAT_RGBA16_FLOAT, { .blendEnabled = false, .writeMask = RFX_COLOR_WRITE_ALL } },
                                            { RFX_FORMAT_RG32_FLOAT, { .blendEnabled = false, .writeMask = RFX_COLOR_WRITE_ALL } } };

        RfxPipelineDesc pd = {};
        pd.shader = shScene;
        pd.vertexLayout = layout;
        pd.vertexLayoutCount = 3;
        pd.vertexStride = sizeof(Vertex);
        pd.attachments = attachments;
        pd.attachmentCount = 2;
        pd.depthFormat = RFX_FORMAT_D32_FLOAT;
        pd.depthTest = true;
        pd.depthWrite = true;
        pd.cullMode = RFX_CULL_FRONT;
        psoScene = rfxCreatePipeline(&pd);

        // present pipeline
        shPresent = rfxCompileShaderMem(kFullscreenTriShader, nullptr, 0);
        RfxPipelineDesc blitPd = {};
        blitPd.shader = shPresent;
        blitPd.colorFormat = rfxGetSwapChainFormat();
        blitPd.cullMode = RFX_CULL_NONE;
        psoPresent = rfxCreatePipeline(&blitPd);

        RecreateResources();

        for (int i = 0; i < 5; i++) {
            upscalerSupported[i] = rfxIsUpscalerSupported((RfxUpscalerType)i);
        }
    }

    void RecreateResources() {
        if (upscaler)
            rfxDestroyUpscaler(upscaler);
        if (rtColor) {
            rfxDestroyTexture(rtColor);
            rfxDestroyTexture(rtMotion);
            rfxDestroyTexture(rtDepth);
            rfxDestroyTexture(rtUpscaled);
        }

        if (upscalerEnabled) {
            RfxUpscalerDesc ud = {};
            ud.type = upscalerType;
            ud.mode = upscalerMode;
            ud.outputWidth = displayWidth;
            ud.outputHeight = displayHeight;
            ud.flags = RFX_UPSCALER_HDR | RFX_UPSCALER_MV_JITTERED;

            upscaler = rfxCreateUpscaler(&ud);

            if (upscaler) {
                RfxUpscalerProps props = {};
                rfxGetUpscalerProps(upscaler, &props);
                renderWidth = props.renderWidth;
                renderHeight = props.renderHeight;
                currentMipBias = props.mipBias;
                printf(
                    "Upscaler Enabled: Input %dx%d -> Output %dx%d, MipBias %.2f\n", renderWidth, renderHeight, displayWidth, displayHeight,
                    currentMipBias
                );
            } else {
                printf("Upscaler creation failed! Fallback to Native.\n");
                renderWidth = displayWidth;
                renderHeight = displayHeight;
                currentMipBias = 0.0f;
            }
        } else {
            upscaler = nullptr;
            renderWidth = displayWidth;
            renderHeight = displayHeight;
            currentMipBias = 0.0f;
        }

        // render targets at input res
        rtColor = rfxCreateTexture(
            renderWidth, renderHeight, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_RENDER_TARGET | RFX_TEXTURE_USAGE_SHADER_RESOURCE,
            nullptr
        );
        rtMotion = rfxCreateTexture(
            renderWidth, renderHeight, RFX_FORMAT_RG32_FLOAT, 1, RFX_TEXTURE_USAGE_RENDER_TARGET | RFX_TEXTURE_USAGE_SHADER_RESOURCE,
            nullptr
        );
        rtDepth = rfxCreateTexture(
            renderWidth, renderHeight, RFX_FORMAT_D32_FLOAT, 1, RFX_TEXTURE_USAGE_DEPTH_STENCIL | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr
        );

        // output target at display res
        rtUpscaled = rfxCreateTexture(
            displayWidth, displayHeight, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr
        );
    }

    void Update() {
        if (recreatePending) {
            RecreateResources();
            recreatePending = false;
        }

        int w = rfxGetWindowWidth();
        int h = rfxGetWindowHeight();
        if ((w != displayWidth || h != displayHeight) && w > 0 && h > 0) {
            displayWidth = w;
            displayHeight = h;
            RecreateResources();
        }

        // camera
        if (!ImGui::GetIO().WantCaptureMouse && rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT)) {
            float dx, dy;
            rfxGetMouseDelta(&dx, &dy);
            camYaw -= dx * 0.005f;
            camPitch -= dy * 0.005f;
            camPitch = HMM_Clamp(0.1f, camPitch, 1.5f);
        }
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            float speed = 20.0f * rfxGetDeltaTime();
            if (rfxIsKeyDown(RFX_KEY_W))
                camDist -= speed;
            if (rfxIsKeyDown(RFX_KEY_S))
                camDist += speed;
            if (camDist < 2.0f)
                camDist = 2.0f;
        }
    }

    void Render() {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        HMM_Vec3 camPos = { sinf(camYaw) * cosf(camPitch) * camDist, sinf(camPitch) * camDist, cosf(camYaw) * cosf(camPitch) * camDist };
        HMM_Mat4 view = HMM_LookAt_RH(camPos, { 0, 0, 0 }, { 0, 1, 0 });

        float fov = 45.0f * HMM_DegToRad;
        float aspect = (float)renderWidth / (float)renderHeight;
        HMM_Mat4 proj = HMM_Perspective_RH_ZO(fov, aspect, 0.1f, 1000.0f);
        proj.Elements[1][1] *= -1.0f;

        // jitter
        float jx = 0.0f, jy = 0.0f;
        if (enableJitter && upscaler) {
            int phaseCount = 16;
            int phase = frameIndex % phaseCount;
            jx = (Halton(phase + 1, 2) - 0.5f);
            jy = (Halton(phase + 1, 3) - 0.5f);
        }

        HMM_Mat4 jitteredProj = proj;
        jitteredProj.Elements[2][0] += (jx * 2.0f) / renderWidth;
        jitteredProj.Elements[2][1] += (jy * 2.0f) / renderHeight;

        HMM_Mat4 viewProj = HMM_MulM4(jitteredProj, view);
        HMM_Mat4 unjitteredViewProj = HMM_MulM4(proj, view);

        // render scene at input res
        {
            RfxTexture targets[] = { rtColor, rtMotion };
            rfxCmdBeginRenderPass(cmd, targets, 2, rtDepth, RFX_COLOR(0.05f, 0.05f, 0.1f, 0.0f), 0);
            rfxCmdBindPipeline(cmd, psoScene);
            rfxCmdBindVertexBuffer(cmd, vBuffer);
            rfxCmdBindIndexBuffer(cmd, iBuffer, RFX_INDEX_UINT16);

            SceneUniforms ubo = {};
            ubo.viewProj = viewProj;
            ubo.prevViewProj = prevViewProj;
            ubo.skyColorHorizon = { 0.05f, 0.05f, 0.15f, 1.0f };
            ubo.skyColorZenith = { 0.01f, 0.01f, 0.05f, 1.0f };
            ubo.sunDir = { 0.5f, 0.8f, 0.2f, 0.0f };
            ubo.jitter = { (jx * 2.0f) / renderWidth, (jy * 2.0f) / renderHeight };
            ubo.mipBias = currentMipBias;
            ubo.time = (float)rfxGetTime();

            rfxCmdPushConstants(cmd, &ubo, sizeof(ubo));
            rfxCmdDrawIndexed(cmd, 36, 1600);
            rfxCmdEndRenderPass(cmd);
        }

        // upscale input res to display res
        if (upscaler) {
            rfxCmdBeginProfile(cmd, "Upscale");

            RfxUpscaleDesc ud = {};
            ud.input = rtColor;
            ud.output = rtUpscaled;
            ud.depth = rtDepth;
            ud.motionVectors = rtMotion;
            ud.sharpness = sharpness;
            ud.jitter[0] = jx;
            ud.jitter[1] = jy;
            ud.motionVectorScale[0] = 1.0f;
            ud.motionVectorScale[1] = 1.0f;
            ud.dispatchFlags = RFX_UPSCALE_DISPATCH_NONE;
            if (frameIndex == 0)
                ud.dispatchFlags |= RFX_UPSCALE_DISPATCH_RESET_HISTORY;
            ud.verticalFov = fov;
            ud.zNear = 0.1f;
            ud.zFar = 1000.0f;
            ud.viewSpaceToMetersFactor = 1.0f;

            rfxCmdUpscale(cmd, upscaler, &ud);
            rfxCmdEndProfile(cmd);
        } else {
            rfxCmdCopyTexture(cmd, rtColor, rtUpscaled);
        }

        // present
        {
            rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(0, 0, 0, 1));
            rfxCmdBindPipeline(cmd, psoPresent);

            struct PC {
                uint32_t id;
                float pad[3];
            } pc = { rfxGetTextureId(rtUpscaled) };

            rfxCmdPushConstants(cmd, &pc, sizeof(pc));
            rfxCmdDraw(cmd, 3, 1);

            RenderUI();

            ImGui::Render();
            RfxImGuiDrawData d = {};
            ImDrawData* id = ImGui::GetDrawData();
            d.drawLists = (void**)id->CmdLists.Data;
            d.drawListCount = id->CmdLists.Size;
            d.displayWidth = id->DisplaySize.x;
            d.displayHeight = id->DisplaySize.y;
            d.textures = (void**)ImGui::GetPlatformIO().Textures.Data;
            d.textureCount = ImGui::GetPlatformIO().Textures.Size;
            d.hdrScale = 1.0f;
            rfxCmdEndRenderPass(cmd);
            rfxCmdDrawImGui(cmd, &d);

            rfxCmdEndRenderPass(cmd);
        }

        rfxEndFrame();
        prevViewProj = unjitteredViewProj;
        frameIndex++;
    }

    void RenderUI() {
        int winW = rfxGetWindowWidth();
        int winH = rfxGetWindowHeight();
        float mx, my;
        rfxGetMousePos(&mx, &my);
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)winW, (float)winH);
        io.DeltaTime = rfxGetDeltaTime();
        io.MousePos = ImVec2(mx, my);
        io.MouseDown[0] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT);
        io.MouseDown[1] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_RIGHT);
        ImGui::NewFrame();
        ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Separator();

        ImGui::Text("Resolution");
        ImGui::Text("Render:  %dx%d", renderWidth, renderHeight);
        ImGui::Text("Display: %dx%d", displayWidth, displayHeight);
        ImGui::Text("Ratio:   %.2f%%", (float)(renderWidth * renderHeight) / (float)(displayWidth * displayHeight) * 100.0f);
        ImGui::Separator();

        const char* typeNames[] = { "Off", "NIS", "FSR", "XeSS", "DLSS", "DLRR" };
        const char* currentName = upscalerEnabled ? typeNames[(int)upscalerType + 1] : typeNames[0];

        if (ImGui::BeginCombo("Upscaler", currentName)) {
            if (ImGui::Selectable(typeNames[0], !upscalerEnabled)) {
                upscalerEnabled = false;
                recreatePending = true;
                frameIndex = 0;
            }

            for (int i = 0; i < 5; i++) {
                RfxUpscalerType type = (RfxUpscalerType)i;
                bool supported = upscalerSupported[i];
                bool selected = upscalerEnabled && (upscalerType == type);

                ImGuiSelectableFlags flags = supported ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled;

                char label[64];
                if (supported)
                    sprintf(label, "%s", typeNames[i + 1]);
                else
                    sprintf(label, "%s (Unsupported)", typeNames[i + 1]);

                if (ImGui::Selectable(label, selected, flags)) {
                    upscalerEnabled = true;
                    enableJitter = (type != RFX_UPSCALER_NIS);
                    upscalerType = type;
                    recreatePending = true;
                    frameIndex = 0;
                }
                if (!supported)
                    ImGui::SetItemTooltip("Could be due to unsupported backend (XeSS requires D3D12), unsupported hardware or missing DLL");
            }
            ImGui::EndCombo();
        }

        if (upscalerEnabled) {
            const char* modes[] = { "Native", "Ultra Quality", "Quality", "Balanced", "Performance", "Ultra Performance" };
            int modeIdx = (int)upscalerMode;
            if (ImGui::Combo("Mode", &modeIdx, modes, 6)) {
                upscalerMode = (RfxUpscalerMode)modeIdx;
                frameIndex = 0;
                recreatePending = true;
            }

            ImGui::SliderFloat("Sharpness", &sharpness, 0.0f, 1.0f);
            if (ImGui::Checkbox("Enable Jitter", &enableJitter))
                frameIndex = 0;
            ImGui::TextDisabled("MipBias: %.2f", currentMipBias);
        }
        ImGui::BeginDisabled();
        ImGui::TextWrapped("Note: This scene is not for performance comparisons (deliberately simple)");
        ImGui::EndDisabled();
        ImGui::End();
    }

    void Cleanup() {
        rfxDestroyUpscaler(upscaler);
        rfxDestroyBuffer(vBuffer);
        rfxDestroyBuffer(iBuffer);
        rfxDestroyTexture(rtColor);
        rfxDestroyTexture(rtMotion);
        rfxDestroyTexture(rtDepth);
        rfxDestroyTexture(rtUpscaled);
        rfxDestroyPipeline(psoScene);
        rfxDestroyPipeline(psoPresent);
        rfxDestroyShader(shScene);
        rfxDestroyShader(shPresent);
        rfxShutdownImGui();
        ImGui::DestroyContext();
    }
};

int main() {
    App app;
    app.Init();
    while (!rfxWindowShouldClose()) {
        app.Update();
        app.Render();
    }
    app.Cleanup();
    return 0;
}
