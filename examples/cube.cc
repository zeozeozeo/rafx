// A tumbling cube with Blinn-Phong lighting.

#include "HandmadeMath.h"
#include "rafx.h"
#include <stdio.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct Uniforms {
    float4x4 mvp;
    float4x4 model;
    float3 cameraPos;
};

RFX_PUSH_CONSTANTS(Uniforms, ubo);

struct VertexInput {
    float3 pos      : POSITION;
    float3 normal   : NORMAL;
    float4 col      : COLOR;
};

struct VertexOutput {
    float4 pos          : SV_Position;
    float3 worldNormal  : TEXCOORD0;
    float3 worldPos     : TEXCOORD1;
    float4 col          : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    float4 worldPos = mul(ubo.model, float4(input.pos, 1.0));

    output.pos = mul(ubo.mvp, float4(input.pos, 1.0));
    output.worldPos = worldPos.xyz;
    output.worldNormal = mul((float3x3)ubo.model, input.normal);
    output.col = input.col;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    float3 L = normalize(float3(0.5, 1.0, 0.7));
    float3 N = normalize(input.worldNormal);
    float3 V = normalize(ubo.cameraPos - input.worldPos);
    float3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float shininess = 64.0;
    float spec = pow(max(dot(N, H), 0.0), shininess);
    float specularStrength = 0.8;
    float ambient = 0.15;

    float3 finalColor = input.col.rgb * (diff + ambient) + (spec * specularStrength);
    return float4(finalColor, input.col.a);
}
)";

typedef struct {
    HMM_Vec3 pos;
    HMM_Vec3 normal;
    RfxColor col;
} Vertex;

int main(void) {
    if (!rfxOpenWindow("Rafx Lit Cube", 1280, 720))
        return 1;

    rfxSetWindowFlags(RFX_WINDOW_ALWAYS_ACTIVE);

    Vertex vertices[] = {
        // front face (red)
        { { -1, -1, 1 }, { 0, 0, 1 }, RFX_COLOR(200, 50, 50, 255) },
        { { 1, -1, 1 }, { 0, 0, 1 }, RFX_COLOR(200, 50, 50, 255) },
        { { 1, 1, 1 }, { 0, 0, 1 }, RFX_COLOR(200, 50, 50, 255) },
        { { -1, 1, 1 }, { 0, 0, 1 }, RFX_COLOR(200, 50, 50, 255) },
        // back face (green)
        { { 1, -1, -1 }, { 0, 0, -1 }, RFX_COLOR(50, 200, 50, 255) },
        { { -1, -1, -1 }, { 0, 0, -1 }, RFX_COLOR(50, 200, 50, 255) },
        { { -1, 1, -1 }, { 0, 0, -1 }, RFX_COLOR(50, 200, 50, 255) },
        { { 1, 1, -1 }, { 0, 0, -1 }, RFX_COLOR(50, 200, 50, 255) },
        // top face (blue)
        { { -1, 1, 1 }, { 0, 1, 0 }, RFX_COLOR(50, 50, 200, 255) },
        { { 1, 1, 1 }, { 0, 1, 0 }, RFX_COLOR(50, 50, 200, 255) },
        { { 1, 1, -1 }, { 0, 1, 0 }, RFX_COLOR(50, 50, 200, 255) },
        { { -1, 1, -1 }, { 0, 1, 0 }, RFX_COLOR(50, 50, 200, 255) },
        // bottom face (yellow)
        { { -1, -1, -1 }, { 0, -1, 0 }, RFX_COLOR(200, 200, 50, 255) },
        { { 1, -1, -1 }, { 0, -1, 0 }, RFX_COLOR(200, 200, 50, 255) },
        { { 1, -1, 1 }, { 0, -1, 0 }, RFX_COLOR(200, 200, 50, 255) },
        { { -1, -1, 1 }, { 0, -1, 0 }, RFX_COLOR(200, 200, 50, 255) },
        // right face (magenta)
        { { 1, -1, 1 }, { 1, 0, 0 }, RFX_COLOR(200, 50, 200, 255) },
        { { 1, -1, -1 }, { 1, 0, 0 }, RFX_COLOR(200, 50, 200, 255) },
        { { 1, 1, -1 }, { 1, 0, 0 }, RFX_COLOR(200, 50, 200, 255) },
        { { 1, 1, 1 }, { 1, 0, 0 }, RFX_COLOR(200, 50, 200, 255) },
        // left face (cyan)
        { { -1, -1, -1 }, { -1, 0, 0 }, RFX_COLOR(50, 200, 200, 255) },
        { { -1, -1, 1 }, { -1, 0, 0 }, RFX_COLOR(50, 200, 200, 255) },
        { { -1, 1, 1 }, { -1, 0, 0 }, RFX_COLOR(50, 200, 200, 255) },
        { { -1, 1, -1 }, { -1, 0, 0 }, RFX_COLOR(50, 200, 200, 255) },
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
    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);

    RfxVertexLayoutElement layout[] = {
        { 0, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, pos), "POSITION" },
        { 1, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, normal), "NORMAL" },
        { 2, RFX_FORMAT_RGBA32_FLOAT, offsetof(Vertex, col), "COLOR" },
    };

    RfxPipelineDesc pDesc = {};
    pDesc.shader = shader;
    pDesc.vertexLayout = layout;
    pDesc.vertexLayoutCount = 3;
    pDesc.vertexStride = sizeof(Vertex);
    pDesc.colorFormat = rfxGetSwapChainFormat();
    pDesc.depthFormat = RFX_FORMAT_D32_FLOAT;
    pDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    pDesc.cullMode = RFX_CULL_BACK;
    pDesc.depthTest = true;
    pDesc.depthWrite = true;

    RfxPipeline pipeline = rfxCreatePipeline(&pDesc);

    struct {
        HMM_Mat4 mvp;
        HMM_Mat4 model;
        HMM_Vec3 cameraPos;
        float _pad;
    } push;

    float rotation = 0.0f;
    float lastFpsPrint = 0.0f;

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();
        float dt = rfxGetDeltaTime();

        rotation += 1.0f * dt;

        if ((lastFpsPrint += dt) > 1.0f) {
            printf("FPS: %.2f\n", 1.0f / dt);
            lastFpsPrint = 0.0f;
        }

        float aspect = (float)rfxGetWindowWidth() / (float)rfxGetWindowHeight();
        HMM_Mat4 proj = HMM_Perspective_RH_ZO(HMM_ToRad(45.0f), aspect, 0.1f, 100.0f);
        HMM_Vec3 camPos = HMM_V3(2.5f, 2.0f, 3.5f);
        HMM_Mat4 view = HMM_LookAt_RH(camPos, HMM_V3(0, 0, 0), HMM_V3(0, 1, 0));

        HMM_Mat4 model =
            HMM_MulM4(HMM_Rotate_RH(rotation * 1.3f, HMM_V3(1.0f, 0.3f, 0.2f)), HMM_Rotate_RH(rotation * 0.8f, HMM_V3(0.2f, 1.0f, 0.5f)));

        push.model = model;
        push.mvp = HMM_MulM4(proj, HMM_MulM4(view, model));
        push.cameraPos = camPos;

        // render
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_D32_FLOAT, RFX_COLOR(15, 15, 18, 255));

        rfxCmdBindPipeline(cmd, pipeline);
        rfxCmdBindVertexBuffer(cmd, vbo);
        rfxCmdBindIndexBuffer(cmd, ibo, RFX_INDEX_UINT16);

        rfxCmdPushConstants(cmd, &push, 140);

        rfxCmdDrawIndexed(cmd, 36, 1);

        rfxCmdEndRenderPass(cmd);
        rfxEndFrame();
    }

    rfxDestroyPipeline(pipeline);
    rfxDestroyShader(shader);
    rfxDestroyBuffer(vbo);
    rfxDestroyBuffer(ibo);

    return 0;
}
