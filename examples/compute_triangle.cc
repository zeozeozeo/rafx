// A triangle, vertices of which are computed by a compute shader.

#include "rafx.h"
#include <stddef.h>
#include <stdint.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct Vertex {
    float3 position;
    float3 color;
};

struct PushConstants {
    uint vertexBufferId;
};

RFX_PUSH_CONSTANTS(PushConstants, g_Push);

[shader("compute")]
[numthreads(3, 1, 1)]
void computeMain(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 3) return;

    RWByteAddressBuffer buf = GetRWBuffer(g_Push.vertexBufferId);

    Vertex v;
    if (id.x == 0) {
        v.position = float3(0.0, 0.5, 0.0);
        v.color = float3(1.0, 0.0, 0.0);
    } else if (id.x == 1) {
        v.position = float3(0.5, -0.5, 0.0);
        v.color = float3(0.0, 1.0, 0.0);
    } else {
        v.position = float3(-0.5, -0.5, 0.0);
        v.color = float3(0.0, 0.0, 1.0);
    }

    buf.Store<Vertex>(id.x * sizeof(Vertex), v);
}

struct VSInput {
    float3 position : POSITION;
    float3 color    : COLOR;
};

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
    VSOutput output;
    output.position = float4(input.position, 1.0);
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
)";

typedef struct {
    float position[3];
    float color[3];
} Vertex;

int main(void) {
    if (!rfxOpenWindow("Rafx Compute Triangle", 1280, 720))
        return 1;

    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);
    if (!shader)
        return 1;

    size_t vertexCount = 3;
    size_t stride = sizeof(Vertex);
    size_t bufferSize = vertexCount * stride;

    RfxBuffer vertexBuffer = rfxCreateBuffer(
        bufferSize, stride, (RfxBufferUsageFlags)(RFX_USAGE_SHADER_RESOURCE_STORAGE | RFX_USAGE_VERTEX_BUFFER), RFX_MEM_GPU_ONLY, NULL
    );

    RfxComputePipelineDesc computeDesc = { shader };
    RfxPipeline computePipeline = rfxCreateComputePipeline(&computeDesc);

    RfxVertexLayoutElement vertexLayout[] = {
        { 0, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, position), "POSITION" },
        { 1, RFX_FORMAT_RGB32_FLOAT, offsetof(Vertex, color), "COLOR" },
    };

    RfxPipelineDesc graphicsDesc = { 0 };
    graphicsDesc.shader = shader;
    graphicsDesc.vertexLayout = vertexLayout;
    graphicsDesc.vertexLayoutCount = 2;
    graphicsDesc.vertexStride = sizeof(Vertex);
    graphicsDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    graphicsDesc.colorFormat = rfxGetSwapChainFormat();

    RfxPipeline graphicsPipeline = rfxCreatePipeline(&graphicsDesc);

    struct {
        uint32_t vertexBufferId;
    } push;

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();

        // compute (generate vertices)
        rfxCmdBindPipeline(cmd, computePipeline);

        push.vertexBufferId = rfxGetBufferId(vertexBuffer);
        rfxCmdPushConstants(cmd, &push, sizeof(push));

        rfxCmdDispatch(cmd, 1, 1, 1);

        // draw vertices
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(40, 40, 45, 255));

        rfxCmdBindPipeline(cmd, graphicsPipeline);
        rfxCmdBindVertexBuffer(cmd, vertexBuffer);

        rfxCmdDraw(cmd, 3, 1);

        rfxCmdEndRenderPass(cmd);

        rfxEndFrame();
    }

    rfxDestroyBuffer(vertexBuffer);
    rfxDestroyPipeline(computePipeline);
    rfxDestroyPipeline(graphicsPipeline);
    rfxDestroyShader(shader);

    return 0;
}
