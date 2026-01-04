#include "rafx.h"
#include <stddef.h>
#include <stdio.h>

typedef struct {
    float x, y, z;
    float r, g, b, a;
} Vertex;

int main(void) {
    if (!rfxOpenWindow("Rafx Hot Reloading", 1280, 720))
        return 1;

    Vertex vertices[] = {
        // x, y, z, r, g, b, a
        { 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        { 0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
    };

    RfxBuffer vertexBuffer = rfxCreateBuffer(sizeof(vertices), 0, RFX_USAGE_VERTEX_BUFFER, RFX_MEM_GPU_ONLY, vertices);

    const char* shaderPath = "examples/hot_reloading.slang"; // assuming ran from root
    RfxShader shader = rfxCompileShader(shaderPath, NULL, 0, NULL, 0);

    if (!shader) {
        printf("Failed to compile initial shader. Make sure you're in the root directory of the project and `%s` exists.\n", shaderPath);
        return 1;
    }

    // ENABLE WATCHING:
    rfxWatchShader(shader, true);

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

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(20, 20, 20, 255));

        rfxCmdBindPipeline(cmd, pipeline);

        rfxCmdBindVertexBuffer(cmd, vertexBuffer);
        rfxCmdDraw(cmd, 3, 1);
        rfxCmdEndRenderPass(cmd);

        rfxEndFrame();
    }

    // cleanup
    rfxDestroyPipeline(pipeline);
    rfxDestroyShader(shader);
    rfxDestroyBuffer(vertexBuffer);

    return 0;
}
