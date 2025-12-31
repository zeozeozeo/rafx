// A triangle.

#include "rafx.h"
#include <stddef.h>

const char* shaderSource = R"(
struct VertexInput {
    float3 position : POSITION;
    float4 color    : COLOR;
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target {
    // print at center
    int x = (int)input.position.x;
    int y = (int)input.position.y;
    if (x == 640 && y == 360) {
        printf("Hello from Fragment Shader! Pixel: %d, %d | RGB: %f, %f, %f\n",
                x, y, input.color.r, input.color.g, input.color.b);
    }

    return input.color;
}
)";

typedef struct {
    float x, y, z;
    float r, g, b, a;
} Vertex;

int main(void) {
    if (!rfxOpenWindow("Rafx Triangle", 1280, 720))
        return 1;

    Vertex vertices[] = {
        // x, y, z, r, g, b, a
        { 0.0f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },
        { 0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f },
    };

    RfxBuffer vertexBuffer = rfxCreateBuffer(sizeof(vertices), 0, RFX_USAGE_VERTEX_BUFFER, RFX_MEM_GPU_ONLY, vertices);

    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);
    if (!shader)
        return 1;

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
