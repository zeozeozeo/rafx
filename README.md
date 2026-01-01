# rafx

Rafx is a C graphics abstraction library designed around modern graphics workflows.

It is based on the [NVIDIA Render Interface (NRI)](https://github.com/NVIDIA-RTX/NRI).

## Features

- Fully bindless rendering approach (no CPU bindgroups, descriptor sets, ...)
- Automatic tracking of resource states, barrier placement and transitions
- Full parity between Vulkan, D3D12 and Metal (through MoltenVK/KosmicKrisp)
- Built-in windowing (using RGFW or GLFW), fully cross-platform
- ImGui extension `rfxCmdDrawImGui`
- Built-in support for [NRD](https://github.com/NVIDIA-RTX/NRD) denoisers (ReBLUR, ReLAX, Sigma) `rfxCmdDenoise`
- Native integration with the [Slang](https://shader-slang.org/) shader language
- Graphics, compute, raytracing and mesh shaders
- Support for hardware RT and opacity micromaps
- Support for common upscalers (NVIDIA DLSS, AMD FSR, Intel XeSS, NVIDIA DLSS Ray Reconstruction, NIS)
- `printf` inside shaders
- Suitable for tile-based rendering architectures
- Tight integration with AMD's Virtual Memory Allocator (VMA) for Vulkan and D3D12 for optimal memory reuse
- Async compute (parallel execution of graphics and compute workloads)
- Variable rate shading (VRS) support
- MSAA, anisotropic filtering, mipmapping and BCn texture compression with a simple toggle
- Multidraw
- Occlusion queries
- Low latency support (aka NVIDIA Reflex)
- GPU profiler, timeline annotations (GAPI, Nsight, PIX), resource naming
- Lots of [examples](./examples) to get you started

## Examples

| ![async compute](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/async_compute_d68YXxjvO3.png)       | ![bloom](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/bloom_fr433htUlT.png)                     | ![compute boids](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/compute_boids_IWEF7MMLaN.png)   |
| --------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| [async compute](./examples/async_compute.cc)                                                                                                        | [bloom](./examples/bloom.cc)                                                                                                                      | [compute boids](./examples/compute_boids.cc)                                                                                                    |
| ![compute triangle](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/compute_triangle_w6ryKPRoxT.png) | ![compute voronoi](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/compute_voronoi_HdWdvFuSgU.png) | ![cube](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/cube_gEw25ZCHEE.png)                     |
| [compute triangle](./examples/compute_triangle.cc)                                                                                                  | [compute voronoi](./examples/compute_voronoi.cc)                                                                                                  | [cube](./examples/cube.cc)                                                                                                                      |
| ![denoise](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/denoise_dG7tVi9DQC.png)                   | ![low latency](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/low_latency_eMI8kwVvFq.png)         | ![mesh triangle](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/mesh_triangle_LfwStysBtQ.png)   |
| [denoise](./examples/denoise.cc)                                                                                                                    | [low latency](./examples/low_latency.cc)                                                                                                          | [mesh triangle](./examples/mesh_triangle.cc)                                                                                                    |
| ![rt boxes](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/rt_boxes_FwuUFARfv0.png)                 | ![rt triangle](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/rt_triangle_2m6QuGMHF5.png)         | ![texcube](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/texcube_icehFTknYv.png)               |
| [rt boxes](./examples/rt_boxes.cc)                                                                                                                  | [rt triangle](./examples/rt_triangle.cc)                                                                                                          | [texcube](./examples/texcube.cc)                                                                                                                |
| ![triangle](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/triangle_2DgAXnLSTO.png)                 | ![upscaler](https://raw.githubusercontent.com/zeozeozeo/media/e7d64f16d8001dd530c1af7f42fd0da09de1db5d/opt/upscaler_msJQEipQIS.png)               | ![shadow mapping](https://raw.githubusercontent.com/zeozeozeo/media/99d1488b41b5271cdc98325c065e3a7a002d7e5e/opt/shadow_mapping_wNZoIGlDXu.png) |
| [triangle](./examples/triangle.cc)                                                                                                                  | [upscaler](./examples/upscaler.cc)                                                                                                                | [shadow mapping](./examples/shadow_mapping.cc)                                                                                                  |

A triangle:

```c
#include <rafx.h>

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
    RfxShader s = rfxCompileShaderMem(src, NULL, 0, NULL, 0);

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
```
