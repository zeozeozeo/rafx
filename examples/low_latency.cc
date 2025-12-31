#include "rafx.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct VertexInput {
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

struct PushConstants {
    float rotation;
    float aspectRatio;
    float2 padding;
};

RFX_PUSH_CONSTANTS(PushConstants, g_Push);

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;

    float c = cos(g_Push.rotation);
    float s = sin(g_Push.rotation);

    float x = input.position.x;
    float y = input.position.y;

    float rx = x * c - y * s;
    float ry = x * s + y * c;

    rx /= g_Push.aspectRatio;

    output.position = float4(rx, ry, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    return input.color;
}
)";

typedef struct {
    float x, y, z;
    float r, g, b, a;
} Vertex;

typedef struct {
    float rotation;
    float aspectRatio;
    float pad[2];
} PushConsts;

int main(void) {
    if (!rfxOpenWindow("Rafx Low Latency Demo", 1280, 720))
        return 1;

    Vertex vertices[] = {
        { 0.0f, 0.5f, 0.0f, 1.0f, 0.2f, 0.2f, 1.0f },
        { 0.4f, -0.5f, 0.0f, 0.2f, 1.0f, 0.2f, 1.0f },
        { -0.4f, -0.5f, 0.0f, 0.2f, 0.2f, 1.0f, 1.0f },
    };

    RfxBuffer vertexBuffer = rfxCreateBuffer(sizeof(vertices), 0, RFX_USAGE_VERTEX_BUFFER, RFX_MEM_GPU_ONLY, vertices);

    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);
    RfxVertexLayoutElement layout[] = {
        { 0, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, x), "POSITION" },
        { 1, RFX_FORMAT_RGBA32_FLOAT, offsetof(Vertex, r), "COLOR" },
    };

    RfxPipelineDesc pipelineDesc = {};
    pipelineDesc.shader = shader;
    pipelineDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.cullMode = RFX_CULL_NONE;
    pipelineDesc.blendState = { .writeMask = RFX_COLOR_WRITE_ALL };
    pipelineDesc.vertexLayout = layout;
    pipelineDesc.vertexLayoutCount = 2;
    pipelineDesc.vertexStride = sizeof(Vertex);
    pipelineDesc.colorFormat = rfxGetSwapChainFormat();

    RfxPipeline pipeline = rfxCreatePipeline(&pipelineDesc);

    bool lowLatencyMode = false;
    bool boostMode = false;
    double lastReportTime = rfxGetTime();

    printf("Controls: [SPACE] Toggle Low Latency\n");
    rfxSetLowLatencyMode(lowLatencyMode, boostMode);

    while (!rfxWindowShouldClose()) {
        rfxLatencySleep();
        rfxPollInputEvents();

        if (rfxIsKeyPressed(RFX_KEY_SPACE)) {
            lowLatencyMode = !lowLatencyMode;
            rfxSetLowLatencyMode(lowLatencyMode, boostMode);
            printf(">> Low Latency: %s\n", lowLatencyMode ? "ON" : "OFF");
        }

        int winW, winH;
        rfxGetWindowSize(&winW, &winH);
        float mx, my;
        rfxGetMousePos(&mx, &my);
        float cx = winW * 0.5f;
        float cy = winH * 0.5f;
        float angle = atan2f(cy - my, mx - cx) - (3.14159f / 2.0f);
        float aspect = (float)winW / (float)winH;

        double currentTime = rfxGetTime();
        if (currentTime - lastReportTime > 1.0) {
            float fps = 1.0f / rfxGetDeltaTime();
            printf("[Stats] FPS: %4.0f | LL: %-3s", fps, lowLatencyMode ? "ON" : "OFF");

            if (lowLatencyMode) {
                RfxLatencyReport report;
                if (rfxGetLatencyReport(&report)) {
                    if (report.presentEndTimeUs > report.inputSampleTimeUs && report.inputSampleTimeUs > 0) {
                        double pcl = (double)(report.presentEndTimeUs - report.inputSampleTimeUs) / 1000.0;
                        double gpu = 0.0;
                        if (report.gpuRenderEndTimeUs > report.gpuRenderStartTimeUs)
                            gpu = (double)(report.gpuRenderEndTimeUs - report.gpuRenderStartTimeUs) / 1000.0;
                        printf(" | PCL: %5.2f ms | GPU: %5.2f ms", pcl, gpu);
                    } else {
                        printf(" | (Wait...)");
                    }
                }
            }
            printf("\n");
            lastReportTime = currentTime;
        }

        // render
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(10, 10, 15, 255));
        rfxCmdBindPipeline(cmd, pipeline);
        rfxCmdBindVertexBuffer(cmd, vertexBuffer);

        PushConsts push;
        push.rotation = -angle;
        push.aspectRatio = aspect;
        rfxCmdPushConstants(cmd, &push, sizeof(push));

        rfxCmdDraw(cmd, 3, 1);
        rfxCmdEndRenderPass(cmd);
        rfxEndFrame();
    }

    rfxDestroyPipeline(pipeline);
    rfxDestroyShader(shader);
    rfxDestroyBuffer(vertexBuffer);
    return 0;
}
