// A triangle rendered with hardware RT.

#include "rafx.h"
#include <stdio.h>
#include <string.h>

const char* shaderSource = R"(
#include "rafx.slang"

struct Payload {
    float3 color;
};

struct RTPush {
    uint outputTexID;
    uint tlasID;
};
RFX_PUSH_CONSTANTS(RTPush, pushRT);

[shader("raygeneration")]
void rayGen() {
    uint2 launchID = DispatchRaysIndex().xy;
    uint2 launchSize = DispatchRaysDimensions().xy;

    float2 pixelCenter = float2(launchID) + float2(0.5);
    float2 inUV = pixelCenter / float2(launchSize);
    float2 d = inUV * 2.0 - 1.0;

    // camera
    float aspectRatio = float(launchSize.x) / float(launchSize.y);
    float3 origin = float3(0, 0, -2.0);
    float3 direction = normalize(float3(d.x * aspectRatio, d.y, 1.0));

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    Payload payload;
    payload.color = float3(0.0, 0.0, 0.0);

    TraceRay(
        GetAccelerationStructure(pushRT.tlasID),
        RAY_FLAG_NONE,
        0xFF,
        0, 1, 0,
        ray,
        payload
    );

    GetRWTexture(pushRT.outputTexID)[launchID] = float4(payload.color, 1.0);
}

[shader("miss")]
void miss(inout Payload payload) {
    payload.color = float3(0.1, 0.1, 0.15);
}

[shader("closesthit")]
void closestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs) {
    float3 barycentrics = float3(1.0 - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    payload.color = barycentrics;
}

struct BlitPush {
    uint texID;
};
RFX_PUSH_CONSTANTS(BlitPush, pushBlit);

struct VSOutput {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
VSOutput vsMain(uint id : SV_VertexID) {
    VSOutput output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

[shader("fragment")]
float4 psMain(VSOutput input) : SV_Target {
    return GetTexture(pushBlit.texID).Sample(GetSamplerLinearClamp(), input.uv);
}
)";

int main(void) {
    rfxSetWindowFlags(RFX_WINDOW_NO_RESIZE);
    if (!rfxOpenWindow("Rafx RT Triangle", 1280, 720))
        return 1;

    // triangle vertices
    float vertices[] = { -0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f, 0.5f, -0.5f, 0.0f };
    RfxBuffer vbo = rfxCreateBuffer(
        sizeof(vertices), 12, RFX_USAGE_VERTEX_BUFFER | RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT, RFX_MEM_CPU_TO_GPU, vertices
    );

    // BLAS
    RfxGeometryTriangles tris = {};
    tris.vertexBuffer = vbo;
    tris.vertexCount = 3;
    tris.vertexStride = 12;
    tris.vertexFormat = RFX_FORMAT_RGB32_FLOAT;

    RfxGeometryDesc geo = {};
    geo.isAABB = false;
    geo.opaque = true;
    geo.triangles = tris;

    RfxAccelerationStructureDesc blasDesc = {};
    blasDesc.type = RFX_AS_BOTTOM_LEVEL;
    blasDesc.flags = RFX_BUILD_AS_PREFER_FAST_TRACE;
    blasDesc.count = 1;
    blasDesc.geometries = &geo;

    RfxAccelerationStructure blas = rfxCreateAccelerationStructure(&blasDesc);

    // TLAS
    RfxAccelerationStructureDesc tlasDesc = {};
    tlasDesc.type = RFX_AS_TOP_LEVEL;
    tlasDesc.flags = RFX_BUILD_AS_PREFER_FAST_TRACE;
    tlasDesc.count = 1; // max instances

    RfxAccelerationStructure tlas = rfxCreateAccelerationStructure(&tlasDesc);

    // scratch buffer for AS
    uint64_t s1 = rfxGetAccelerationStructureScratchSize(blas);
    uint64_t s2 = rfxGetAccelerationStructureScratchSize(tlas);
    RfxBuffer scratch = rfxCreateBuffer((s1 > s2 ? s1 : s2), 0, RFX_USAGE_SCRATCH_BUFFER, RFX_MEM_GPU_ONLY, NULL);

    // instance buffer
    RfxBuffer instances = rfxCreateBuffer(sizeof(RfxInstance), 0, RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT, RFX_MEM_GPU_ONLY, NULL);

    // build once
    {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        // build BLAS
        rfxCmdBuildAccelerationStructure(cmd, blas, scratch, NULL);

        // upload instance
        RfxInstance inst = {};
        inst.transform[0][0] = 1.0f;
        inst.transform[1][1] = 1.0f;
        inst.transform[2][2] = 1.0f;
        inst.instanceId = 0;
        inst.mask = 0xFF;
        inst.flags = RFX_INSTANCE_FORCE_OPAQUE;
        inst.blas = blas;
        rfxCmdUploadInstances(cmd, instances, &inst, 1);

        // build TLAS
        rfxCmdBuildAccelerationStructure(cmd, tlas, scratch, instances);

        rfxEndFrame();
    }

    // pipeline and SBT
    RfxShader shader = rfxCompileShaderMem(shaderSource, nullptr, 0);

    // rt pipeline
    RfxShaderGroup groups[3] = {};
    groups[0].type = RFX_SHADER_GROUP_GENERAL;
    groups[0].generalShader = "rayGen";
    groups[1].type = RFX_SHADER_GROUP_GENERAL;
    groups[1].generalShader = "miss";
    groups[2].type = RFX_SHADER_GROUP_TRIANGLES;
    groups[2].closestHitShader = "closestHit";

    RfxRayTracingPipelineDesc rtpDesc = {};
    rtpDesc.shader = shader;
    rtpDesc.groups = groups;
    rtpDesc.groupCount = 3;
    rtpDesc.maxRecursionDepth = 1;
    rtpDesc.maxPayloadSize = 16;  // float3 + padding
    rtpDesc.maxAttributeSize = 8; // float2 barycentrics

    RfxPipeline rtPipeline = rfxCreateRayTracingPipeline(&rtpDesc);
    RfxShaderBindingTable sbt = rfxCreateShaderBindingTable(rtPipeline);

    // blit
    RfxPipelineDesc blitDesc = {};
    blitDesc.shader = shader;
    blitDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    blitDesc.colorFormat = rfxGetSwapChainFormat();
    blitDesc.vsEntryPoint = "vsMain";
    blitDesc.psEntryPoint = "psMain";

    RfxPipeline blitPipeline = rfxCreatePipeline(&blitDesc);

    // output texture
    RfxTexture outputTex =
        rfxCreateTexture(1280, 720, RFX_FORMAT_RGBA32_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, NULL);

    struct RTPush {
        uint32_t outID;
        uint32_t tlasID;
    } pcRT;
    pcRT.outID = rfxGetTextureId(outputTex);
    pcRT.tlasID = rfxGetAccelerationStructureId(tlas);

    struct BlitPush {
        uint32_t texID;
    } pcBlit;
    pcBlit.texID = pcRT.outID;

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        // RT pass
        rfxCmdBindPipeline(cmd, rtPipeline);
        rfxCmdPushConstants(cmd, &pcRT, sizeof(pcRT));

        // trace (1 raygen, 1 miss, 1 hit)
        RfxTraceRaysDesc trace = {};
        trace.sbt = sbt;
        trace.rayGenIndex = 0;
        trace.missIndex = 1;
        trace.missCount = 1;
        trace.hitIndex = 2;
        trace.hitCount = 1;
        rfxCmdTraceRays(cmd, &trace, 1280, 720, 1);

        // blit pass
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(0, 0, 0, 1));

        rfxCmdBindPipeline(cmd, blitPipeline);
        rfxCmdPushConstants(cmd, &pcBlit, sizeof(pcBlit));
        rfxCmdDraw(cmd, 3, 1); // fullscreen tri

        rfxCmdEndRenderPass(cmd);

        rfxEndFrame();
    }

    // cleanup
    rfxDestroyPipeline(rtPipeline);
    rfxDestroyPipeline(blitPipeline);
    rfxDestroyShaderBindingTable(sbt);
    rfxDestroyShader(shader);
    rfxDestroyTexture(outputTex);
    rfxDestroyAccelerationStructure(blas);
    rfxDestroyAccelerationStructure(tlas);
    rfxDestroyBuffer(vbo);
    rfxDestroyBuffer(scratch);
    rfxDestroyBuffer(instances);

    return 0;
}
