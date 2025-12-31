#include "rafx.h"

// minified for readme sake
const char* src = "struct V { float3 p:POSITION; float4 c:COLOR; }; "
                  "struct O { float4 p:SV_Position; float4 c:COLOR; }; "
                  "[shader(\"vertex\")] O vs(V i) { O o; o.p=float4(i.p,1); o.c=i.c; return o; } "
                  "[shader(\"fragment\")] float4 ps(O i):SV_Target { return i.c; }";

struct Vertex {
    float x, y, z, r, g, b, a;
};

int main() {
    rfxOpenWindow("Rafx", 1280, 720);

    struct Vertex data[] = { { 0, .5, 0, 1, 0, 0, 1 }, { .5, -.5, 0, 0, 1, 0, 1 }, { -.5, -.5, 0, 0, 0, 1, 1 } };
    RfxBuffer vb = rfxCreateBuffer(sizeof(data), sizeof(data[0]), RFX_USAGE_VERTEX_BUFFER, RFX_MEM_GPU_ONLY, data);
    RfxShader s = rfxCompileShaderMem(src, NULL, 0);

    RfxVertexLayoutElement layout[] = { { 0, RFX_FORMAT_RGB32_FLOAT, 0, "POSITION" }, { 1, RFX_FORMAT_RGBA32_FLOAT, 12, "COLOR" } };
    RfxPipeline pip = rfxCreatePipeline(&(RfxPipelineDesc){
        .shader = s,
        .vertexLayout = layout,
        .vertexLayoutCount = 2,
        .colorFormat = rfxGetSwapChainFormat(),
        .vertexStride = sizeof(struct Vertex),
    });

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(20, 20, 20, 255));
        rfxCmdBindPipeline(cmd, pip);
        rfxCmdBindVertexBuffer(cmd, vb);
        rfxCmdDraw(cmd, 3, 1);
        rfxCmdEndRenderPass(cmd);

        rfxEndFrame();
    }

    rfxDestroyPipeline(pip);
    rfxDestroyShader(s);
    rfxDestroyBuffer(vb);

    return 0;
}
