// A textured spinning cube with simple diffuse lighting, 8X MSAA and 16x anisotropic filtering

#include "HandmadeMath.h"
#include "rafx.h"
#include <stdlib.h>
#include <math.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct Uniforms {
    float4x4 mvp;
    float4x4 model;
    float3 cameraPos;
    uint textureId;
};

RFX_PUSH_CONSTANTS(Uniforms, g_PushConstants);

struct VertexInput {
    float3 pos      : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD;
};

struct VertexOutput {
    float4 pos          : SV_Position;
    float3 worldNormal  : TEXCOORD0;
    float3 worldPos     : TEXCOORD1;
    float2 uv           : TEXCOORD2;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float4 worldPos = mul(g_PushConstants.model, float4(input.pos, 1.0));
    output.worldPos = worldPos.xyz;
    output.pos = mul(g_PushConstants.mvp, float4(input.pos, 1.0));
    output.worldNormal = mul((float3x3)g_PushConstants.model, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float3 L = normalize(float3(0.5, 1.0, 0.7));
    float3 N = normalize(input.worldNormal);

    Texture2D t = GetTexture(g_PushConstants.textureId);
    SamplerState s = GetSamplerLinearWrap();

    float4 albedo = t.Sample(s, input.uv);

    float diff = max(dot(N, L), 0.0);
    float ambient = 0.2;
    float3 finalColor = albedo.rgb * (diff + ambient);

    return float4(finalColor, albedo.a);
}
)";

void GenerateBrickTexture(uint8_t* p, int w, int h) {
    float W = 32, H = 16, M = 3, R = .6, G = .25, B = .15, r = .9, g = .88, b = .85, V = .1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float a, c, d, pw = W + M, ph = H + M, ox, px, py;
            int i = y / ph, j = (x + ((i & 1) ? pw / 2 : 0)) / pw, k = (y * w + x) * 4;
            ox = (i & 1) ? pw / 2 : 0;
            px = fmod(x + ox, pw);
            py = fmod(y, ph);
            if (px < M || py < M)
                a = r, c = g, d = b;
            else {
                float v = R + (float)rand() / RAND_MAX * 2 * V - V, n = (float)rand() / RAND_MAX * .1 - .05;
                a = v + n;
                c = G + v - R + n;
                d = B + v - R + n;
            }
            a = fmax(0, fmin(1, a));
            c = fmax(0, fmin(1, c));
            d = fmax(0, fmin(1, d));
            p[k] = a * 255;
            p[k + 1] = c * 255;
            p[k + 2] = d * 255;
            p[k + 3] = 255;
        }
}

void GenerateCheckerTexture(uint8_t* p, int w, int h) {
    int S = 16;
    float R1 = .9, G1 = .9, B1 = .9, R2 = .2, G2 = .2, B2 = .2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int k = (y * w + x) * 4, c = ((x / S) + (y / S)) & 1;
            float r = c ? R1 : R2, g = c ? G1 : G2, b = c ? B1 : B2;
            p[k] = r * 255;
            p[k + 1] = g * 255;
            p[k + 2] = b * 255;
            p[k + 3] = 255;
        }
}

void GenerateWoodTexture(uint8_t* p, int w, int h) {
    float R = .45, G = .28, B = .15, N = .08;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float d = sqrtf(powf(x - w / 2.0f, 2) + powf((y - h / 2.0f) * 0.3f, 2));
            float v = sinf(d * 0.15f) * 0.5f + 0.5f, n = (float)rand() / RAND_MAX * N;
            int k = (y * w + x) * 4;
            p[k] = fmin(255, (R + v * .2f + n) * 255);
            p[k + 1] = fmin(255, (G + v * .15f + n) * 255);
            p[k + 2] = fmin(255, (B + v * .1f + n) * 255);
            p[k + 3] = 255;
        }
}

void GenerateNoiseTexture(uint8_t* p, int w, int h) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int k = (y * w + x) * 4;
            uint8_t v = rand() % 256;
            p[k] = v;
            p[k + 1] = v;
            p[k + 2] = v;
            p[k + 3] = 255;
        }
}

void GenerateDotsTexture(uint8_t* p, int w, int h) {
    int S = 24;
    float R1 = .1, G1 = .5, B1 = .8, R2 = .9, G2 = .9, B2 = .8;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int cx = (x / S) * S + S / 2, cy = (y / S) * S + S / 2, k = (y * w + x) * 4;
            float d = sqrtf(powf(x - cx, 2) + powf(y - cy, 2)), t = d < S * 0.3f ? 0 : 1;
            p[k] = (t ? R2 : R1) * 255;
            p[k + 1] = (t ? G2 : G1) * 255;
            p[k + 2] = (t ? B2 : B1) * 255;
            p[k + 3] = 255;
        }
}

void GenerateStripeTexture(uint8_t* p, int w, int h) {
    int S = 20;
    float R1 = .8, G1 = .3, B1 = .2, R2 = .95, G2 = .85, B2 = .7;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int k = (y * w + x) * 4, c = (y / S) & 1;
            float r = c ? R1 : R2, g = c ? G1 : G2, b = c ? B1 : B2;
            p[k] = r * 255;
            p[k + 1] = g * 255;
            p[k + 2] = b * 255;
            p[k + 3] = 255;
        }
}

typedef struct {
    HMM_Vec3 pos;
    HMM_Vec3 normal;
    HMM_Vec2 uv;
} Vertex;

int main(void) {
    if (!rfxOpenWindow("Rafx Texture Cube", 1280, 720))
        return 1;
    rfxSetSampleCount(8); // 8X MSAA
    rfxSetAnisotropy(16); // 16x anisotropic filtering

    Vertex vertices[] = {
        { { -1, -1, 1 }, { 0, 0, 1 }, { 0, 1 } },   { { 1, -1, 1 }, { 0, 0, 1 }, { 1, 1 } },   { { 1, 1, 1 }, { 0, 0, 1 }, { 1, 0 } },
        { { -1, 1, 1 }, { 0, 0, 1 }, { 0, 0 } },    { { 1, -1, -1 }, { 0, 0, -1 }, { 0, 1 } }, { { -1, -1, -1 }, { 0, 0, -1 }, { 1, 1 } },
        { { -1, 1, -1 }, { 0, 0, -1 }, { 1, 0 } },  { { 1, 1, -1 }, { 0, 0, -1 }, { 0, 0 } },  { { -1, 1, 1 }, { 0, 1, 0 }, { 0, 1 } },
        { { 1, 1, 1 }, { 0, 1, 0 }, { 1, 1 } },     { { 1, 1, -1 }, { 0, 1, 0 }, { 1, 0 } },   { { -1, 1, -1 }, { 0, 1, 0 }, { 0, 0 } },
        { { -1, -1, -1 }, { 0, -1, 0 }, { 0, 1 } }, { { 1, -1, -1 }, { 0, -1, 0 }, { 1, 1 } }, { { 1, -1, 1 }, { 0, -1, 0 }, { 1, 0 } },
        { { -1, -1, 1 }, { 0, -1, 0 }, { 0, 0 } },  { { 1, -1, 1 }, { 1, 0, 0 }, { 0, 1 } },   { { 1, -1, -1 }, { 1, 0, 0 }, { 1, 1 } },
        { { 1, 1, -1 }, { 1, 0, 0 }, { 1, 0 } },    { { 1, 1, 1 }, { 1, 0, 0 }, { 0, 0 } },    { { -1, -1, -1 }, { -1, 0, 0 }, { 0, 1 } },
        { { -1, -1, 1 }, { -1, 0, 0 }, { 1, 1 } },  { { -1, 1, 1 }, { -1, 0, 0 }, { 1, 0 } },  { { -1, 1, -1 }, { -1, 0, 0 }, { 0, 0 } },
    };

    uint16_t indices[36];
    for (int i = 0; i < 6; i++) {
        indices[i * 6 + 0] = i * 4 + 0;
        indices[i * 6 + 1] = i * 4 + 1;
        indices[i * 6 + 2] = i * 4 + 2;
        indices[i * 6 + 3] = i * 4 + 2;
        indices[i * 6 + 4] = i * 4 + 3;
        indices[i * 6 + 5] = i * 4 + 0;
    }

    RfxBuffer vbo = rfxCreateBuffer(sizeof(vertices), 0, RFX_USAGE_VERTEX_BUFFER, RFX_MEM_GPU_ONLY, vertices);
    RfxBuffer ibo = rfxCreateBuffer(sizeof(indices), 0, RFX_USAGE_INDEX_BUFFER, RFX_MEM_GPU_ONLY, indices);

    // textures
    uint8_t* texData = (uint8_t*)malloc(256 * 256 * 4);
    RfxTexture textures[6];
    GenerateBrickTexture(texData, 256, 256);
    textures[0] = rfxCreateTexture(256, 256, RFX_FORMAT_RGBA8_UNORM, 0, RFX_TEXTURE_USAGE_SHADER_RESOURCE, texData);
    GenerateCheckerTexture(texData, 256, 256);
    textures[1] = rfxCreateTexture(256, 256, RFX_FORMAT_RGBA8_UNORM, 0, RFX_TEXTURE_USAGE_SHADER_RESOURCE, texData);
    GenerateWoodTexture(texData, 256, 256);
    textures[2] = rfxCreateTexture(256, 256, RFX_FORMAT_RGBA8_UNORM, 0, RFX_TEXTURE_USAGE_SHADER_RESOURCE, texData);
    GenerateNoiseTexture(texData, 256, 256);
    textures[3] = rfxCreateTexture(256, 256, RFX_FORMAT_RGBA8_UNORM, 0, RFX_TEXTURE_USAGE_SHADER_RESOURCE, texData);
    GenerateDotsTexture(texData, 256, 256);
    textures[4] = rfxCreateTexture(256, 256, RFX_FORMAT_RGBA8_UNORM, 0, RFX_TEXTURE_USAGE_SHADER_RESOURCE, texData);
    GenerateStripeTexture(texData, 256, 256);
    textures[5] = rfxCreateTexture(256, 256, RFX_FORMAT_RGBA8_UNORM, 0, RFX_TEXTURE_USAGE_SHADER_RESOURCE, texData);
    free(texData);

    // pipeline
    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);

    RfxVertexLayoutElement layout[] = {
        { 0, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, pos), "POSITION" },
        { 1, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, normal), "NORMAL" },
        { 2, RFX_FORMAT_RG32_FLOAT, offsetof(Vertex, uv), "TEXCOORD" },
    };

    RfxPipelineDesc pDesc = {};
    pDesc.shader = shader;
    pDesc.vertexLayout = layout;
    pDesc.vertexLayoutCount = 3;
    pDesc.vertexStride = sizeof(Vertex);
    pDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    pDesc.cullMode = RFX_CULL_BACK;
    pDesc.depthTest = true;
    pDesc.depthWrite = true;
    pDesc.colorFormat = rfxGetSwapChainFormat();
    pDesc.depthFormat = RFX_FORMAT_D32_FLOAT;

    RfxPipeline pipeline = rfxCreatePipeline(&pDesc);

    struct {
        HMM_Mat4 mvp;
        HMM_Mat4 model;
        HMM_Vec3 cameraPos;
        uint32_t textureId;
    } push;

    float rotation = 0.0f, timer = 0.0f;
    int currentTexture = 0;

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();
        float dt = rfxGetDeltaTime();

        rotation += 0.5f * dt;
        timer += dt;
        if (timer >= 1.5f) {
            timer = 0.0f;
            currentTexture = (currentTexture + 1) % 6;
        }

        float aspect = (float)rfxGetWindowWidth() / (float)rfxGetWindowHeight();
        HMM_Mat4 proj = HMM_Perspective_RH_ZO(HMM_ToRad(45.0f), aspect, 0.1f, 100.0f);
        HMM_Vec3 camPos = HMM_V3(3.0f, 2.5f, 4.0f);
        HMM_Mat4 view = HMM_LookAt_RH(camPos, HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));
        HMM_Mat4 model = HMM_Rotate_RH(rotation, HMM_V3(0.5f, 1.0f, 0.0f));

        push.model = model;
        push.mvp = HMM_MulM4(proj, HMM_MulM4(view, model));
        push.cameraPos = camPos;
        push.textureId = rfxGetTextureId(textures[currentTexture]);

        // render
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_D32_FLOAT, RFX_COLOR(20, 18, 15, 255));

        rfxCmdBindPipeline(cmd, pipeline);
        rfxCmdBindVertexBuffer(cmd, vbo);
        rfxCmdBindIndexBuffer(cmd, ibo, RFX_INDEX_UINT16);

        rfxCmdPushConstants(cmd, &push, sizeof(push));

        rfxCmdDrawIndexed(cmd, 36, 1);

        rfxCmdEndRenderPass(cmd);
        rfxEndFrame();
    }

    // cleanup
    for (int i = 0; i < 6; ++i)
        rfxDestroyTexture(textures[i]);
    rfxDestroyPipeline(pipeline);
    rfxDestroyShader(shader);
    rfxDestroyBuffer(vbo);
    rfxDestroyBuffer(ibo);

    return 0;
}
