// A triangle generated via Mesh Shaders.

#include "rafx.h"
#include <stddef.h>

const char* shaderSource = R"(
struct VertexOut {
    float4 position : SV_Position;
    float4 color    : COLOR;
};

// numthreads: Number of threads per workgroup (meshlet)
// outputtopology: "triangle" or "line"
[shader("mesh")]
[numthreads(1, 1, 1)]
[outputtopology("triangle")]
void meshMain(
    out indices uint3 primIndices[1],
    out vertices VertexOut verts[3],
    uint3 dispatchThreadID : SV_DispatchThreadID
) {
    SetMeshOutputCounts(3, 1);

    // top
    verts[0].position = float4(0.0, 0.5, 0.0, 1.0);
    verts[0].color    = float4(1.0, 0.0, 0.0, 1.0);

    // bottom right
    verts[1].position = float4(0.5, -0.5, 0.0, 1.0);
    verts[1].color    = float4(0.0, 1.0, 0.0, 1.0);

    // bottom left
    verts[2].position = float4(-0.5, -0.5, 0.0, 1.0);
    verts[2].color    = float4(0.0, 0.0, 1.0, 1.0);

    // indices
    primIndices[0] = uint3(0, 1, 2);
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    return input.color;
}
)";

int main(void) {
    if (!rfxOpenWindow("Rafx Mesh Shader Triangle", 1280, 720))
        return 1;

    RfxShader shader = rfxCompileShaderMem(shaderSource, NULL, 0);
    if (!shader)
        return 1;

    RfxPipelineDesc pipelineDesc = {};
    pipelineDesc.shader = shader;
    pipelineDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    pipelineDesc.cullMode = RFX_CULL_NONE;
    pipelineDesc.colorFormat = rfxGetSwapChainFormat();

    RfxPipeline pipeline = rfxCreatePipeline(&pipelineDesc);

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();

        RfxCommandList cmd = rfxGetCommandList();

        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(20, 20, 20, 255));

        rfxCmdBindPipeline(cmd, pipeline);

        // emit exactly one triangle ([numthreads(1,1,1)] => x=1 y=1 z=1)
        rfxCmdDrawMeshTasks(cmd, 1, 1, 1);

        rfxCmdEndRenderPass(cmd);

        rfxEndFrame();
    }

    // cleanup
    rfxDestroyPipeline(pipeline);
    rfxDestroyShader(shader);

    return 0;
}
