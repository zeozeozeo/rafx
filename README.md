# rafx

Rafx is a C graphics abstraction library designed around modern graphics workflows.

It is based on the [NVIDIA Render Interface (NRI)](https://github.com/NVIDIA-RTX/NRI).

## Features

- Fully bindless rendering approach (no CPU bindgroups, descriptor sets, ...)
- Automatic tracking of resource states, barrier placement and transitions
- Full parity between Vulkan, D3D12 and Metal (through MoltenVK/KosmicKrisp)
- Support for [Enhanced Barriers](https://microsoft.github.io/DirectX-Specs/d3d/D3D12EnhancedBarriers.html) on DirectX 12 Ultimate
- Built-in windowing (using [RGFW](http://github.com/ColleagueRiley/RGFW), GLFW or SDL3), fully cross-platform
- Built-in support for [NRD](https://github.com/NVIDIA-RTX/NRD) denoisers (ReBLUR, ReLAX, Sigma) `rfxCmdDenoise`
- Native integration with the [Slang](https://shader-slang.org/) shader language
- Graphics, compute, raytracing and mesh shaders
- Support for hardware RT and opacity micromaps
- Support for common upscalers (NVIDIA DLSS, AMD FSR, Intel XeSS, NVIDIA DLSS Ray Reconstruction, NIS)
- `printf` inside shaders, toggleable hot-reloading `rfxWatchShader`
- Suitable for tile-based rendering architectures
- Tight integration with AMD's Virtual Memory Allocator (VMA) for Vulkan and D3D12 for optimal memory reuse
- Async compute (parallel execution of graphics and compute workloads)
- Variable rate shading (VRS) support
- MSAA, anisotropic filtering, mipmapping and BCn texture compression with a simple toggle
- Multidraw
- Occlusion queries
- Low latency support (aka NVIDIA Reflex)
- GPU profiler, timeline annotations (GAPI, Nsight, PIX), resource naming
- ImGui extension `rfxCmdDrawImGui`
- Shader cache / precompilation
- Honored user-provided memory allocator
- Lots of [examples](./examples) to get you started

## Examples

| <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/async_compute_d68YXxjvO3.png" width="1280" height="720" alt="async compute">       | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/bloom_fr433htUlT.png" width="1280" height="720" alt="bloom">                     | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/compute_boids_IWEF7MMLaN.png" width="1280" height="720" alt="compute boids"> |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [async compute](./examples/async_compute.cc)                                                                                                                                                | [bloom](./examples/bloom.cc)                                                                                                                                                              | [compute boids](./examples/compute_boids.cc)                                                                                                                                          |
| <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/compute_triangle_w6ryKPRoxT.png" width="1280" height="720" alt="compute triangle"> | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/compute_voronoi_HdWdvFuSgU.png" width="1280" height="720" alt="compute voronoi"> | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/cube_gEw25ZCHEE.png" width="1280" height="720" alt="cube">                   |
| [compute triangle](./examples/compute_triangle.cc)                                                                                                                                          | [compute voronoi](./examples/compute_voronoi.cc)                                                                                                                                          | [cube](./examples/cube.cc)                                                                                                                                                            |
| <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/denoise_dG7tVi9DQC.png" width="1280" height="720" alt="denoise">                   | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/hot_reloading_JU4rDmpI1z.png" width="1280" height="720" alt="hot reloading">     | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/low_latency_eMI8kwVvFq.png" width="1280" height="720" alt="low latency">     |
| [denoise](./examples/denoise.cc)                                                                                                                                                            | [hot reloading](./examples/hot_reloading.cc)                                                                                                                                              | [low latency](./examples/low_latency.cc)                                                                                                                                              |
| <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/mesh_triangle_LfwStysBtQ.png" width="1280" height="720" alt="mesh triangle">       | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/rt_boxes_FwuUFARfv0.png" width="1280" height="720" alt="rt boxes">               | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/rt_triangle_2m6QuGMHF5.png" width="1280" height="720" alt="rt triangle">     |
| [mesh triangle](./examples/mesh_triangle.cc)                                                                                                                                                | [rt boxes](./examples/rt_boxes.cc)                                                                                                                                                        | [rt triangle](./examples/rt_triangle.cc)                                                                                                                                              |
| <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/shadow_mapping_wNZoIGlDXu.png" width="1280" height="720" alt="shadow mapping">     | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/texcube_icehFTknYv.png" width="1280" height="720" alt="texcube">                 | <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/triangle_2DgAXnLSTO.png" width="1280" height="720" alt="triangle">           |
| [shadow mapping](./examples/shadow_mapping.cc)                                                                                                                                              | [texcube](./examples/texcube.cc)                                                                                                                                                          | [triangle](./examples/triangle.cc)                                                                                                                                                    |
| <img src="https://raw.githubusercontent.com/zeozeozeo/media/f2a484a84b4bfa33d195455f7a8ad72f49fc0dea/opt/upscaler_msJQEipQIS.png" width="1280" height="720" alt="upscaler">                 |                                                                                                                                                                                           |                                                                                                                                                                                       |
| [upscaler](./examples/upscaler.cc)                                                                                                                                                          |                                                                                                                                                                                           |                                                                                                                                                                                       |

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
