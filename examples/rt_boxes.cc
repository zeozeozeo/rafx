// 100,000 instanced boxes rendered via hardware raytracing

#include "rafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h> // memset
#include <vector>

const char* shaderSource = R"(
#include "rafx.slang"

struct Payload {
    float3 color;
};

struct RTPush {
    uint outputTexID;
    uint tlasID;
    uint indexBufferID;
    uint uvBufferID;
    float time;
};
RFX_PUSH_CONSTANTS(RTPush, pushRT);

[shader("raygeneration")]
void rayGen() {
    uint2 launchID = DispatchRaysIndex().xy;
    uint2 launchSize = DispatchRaysDimensions().xy;

    float2 pixelCenter = float2(launchID) + float2(0.5);
    float2 inUV = pixelCenter / float2(launchSize);
    float2 d = inUV * 2.0 - 1.0;

    // looking down +z
    float aspectRatio = float(launchSize.x) / float(launchSize.y);
    float t = pushRT.time;

    float speed = 60.0;
    float loopLength = 1300.0;
    float progress = fmod(t * speed, loopLength);
    float3 pathCenter = float3(0.0, -10.0 + progress, 10.0 + progress);

    float orbitRadius = 67.0;
    float orbitSpeed = t * 0.6;
    float3 origin;
    origin.x = cos(orbitSpeed) * orbitRadius;
    origin.y = pathCenter.y + sin(orbitSpeed) * orbitRadius;
    origin.z = pathCenter.z - 20.0;

    float3 lookAt = float3(0.0, pathCenter.y + 60.0, pathCenter.z + 60.0);
    float3 forward = normalize(lookAt - origin);
    float3 worldUp = float3(sin(t * 0.5) * 0.4, 1.0, 0.0);
    float3 right = normalize(cross(worldUp, forward));
    float3 up = cross(forward, right);

    float3 direction = normalize(d.x * right * aspectRatio + -d.y * up + forward);

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.1;
    ray.TMax = 10000.0;

    Payload payload;
    payload.color = float3(0.0, 0.05, 0.1); // background color

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

float hash13(float3 p3) {
    p3  = frac(p3 * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float noise(float3 p) {
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(lerp(hash13(i + float3(0, 0, 0)), hash13(i + float3(1, 0, 0)), f.x),
                   lerp(hash13(i + float3(0, 1, 0)), hash13(i + float3(1, 1, 0)), f.x), f.y),
               lerp(lerp(hash13(i + float3(0, 0, 1)), hash13(i + float3(1, 0, 1)), f.x),
                   lerp(hash13(i + float3(0, 1, 1)), hash13(i + float3(1, 1, 1)), f.x), f.y), f.z);
}

[shader("miss")]
void miss(inout Payload payload) {
    float3 dir = WorldRayDirection();

    float3 color = float3(0.002, 0.003, 0.01);

    float n = noise(dir * 2.5) * 0.5 + noise(dir * 5.0) * 0.2;
    color += hsv2rgb(float3(0.65, 0.8, 0.08 * n));

    // stars
    float starDensity = 300.0;
    float3 starPos = dir * starDensity;
    float3 cellId = floor(starPos);
    float3 cellSub = frac(starPos) - 0.5;

    float starHash = hash13(cellId);

    if (starHash > 0.96) {
        float dist = length(cellSub);
        float glow = 0.0015 / (dist * dist + 0.0001);
        float twinkle = sin(pushRT.time * (2.0 + starHash * 3.0) + starHash * 10.0) * 0.1 + 0.5;
        float3 starColor = lerp(float3(0.8, 0.9, 1.0), float3(1.0, 0.7, 0.7), starHash);
        color += starColor * glow * (0.2 + 0.8 * twinkle) * 15.0;
    }

    payload.color = color;
}

float3 hsv2rgb(float3 c) {
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

[shader("closesthit")]
void closestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attribs) {
    uint faceIndex = PrimitiveIndex() / 2;
    float3 n;
    if (faceIndex == 0)      n = float3(0, 0, -1);
    else if (faceIndex == 1) n = float3(0, 0, 1);
    else if (faceIndex == 2) n = float3(-1, 0, 0);
    else if (faceIndex == 3) n = float3(1, 0, 0);
    else if (faceIndex == 4) n = float3(0, 1, 0);
    else                     n = float3(0, -1, 0);

    uint id = InstanceIndex();
    float x = float(id % 100);
    float y = float(id / 100);

    float wave = sin(x * 0.1 + pushRT.time) + cos(y * 0.1 + pushRT.time);
    float hue = frac(wave * 0.5);
    hue += sin(x * 0.1 + pushRT.time) * cos(y * 0.1);
    float sat = 0.7 + sin(x * 0.5 + y * 0.3 + pushRT.time * 2.0) * 0.8;
    float3 baseColor = hsv2rgb(float3(frac(hue), sat, 1.0));

    // fog
    float3 lightDir = normalize(float3(1.0, 2.0, -1.0));
    float diff = max(dot(n, lightDir), 0.3);
    float dist = RayTCurrent();
    float fog = 1.0 / (1.0 + dist * 0.005);

    payload.color = baseColor * diff * fog;
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
    float4 color = GetTexture(pushBlit.texID).Sample(GetSamplerLinearClamp(), input.uv);
    color.rgb = color.rgb / (color.rgb + 1.0); // Reinhard
    color.rgb = pow(color.rgb, 1.0/2.2);       // Gamma
    return color;
}
)";

constexpr uint32_t BOX_NUM = 100000;
constexpr float BOX_HALF_SIZE = 0.5f;

static const float positions[] = {
    // Front (-Z)
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    // Back (+Z)
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    // Left (-X)
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    // Right (+X)
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    // Top (+Y)
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    // Bottom (-Y)
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
    -BOX_HALF_SIZE,
    BOX_HALF_SIZE,
};

static const uint16_t indices[] = {
    0,  1,  2,  0,  2,  3,  // Front
    4,  5,  6,  4,  6,  7,  // Back
    8,  9,  10, 8,  10, 11, // Left
    12, 13, 14, 12, 14, 15, // Right
    16, 17, 18, 16, 18, 19, // Top
    20, 21, 22, 20, 22, 23  // Bottom
};

int main(void) {
    rfxSetWindowFlags(RFX_WINDOW_NO_RESIZE);
    if (!rfxOpenWindow("Rafx RT Boxes", 1280, 720))
        return 1;

    // vertex
    RfxBuffer vbo = rfxCreateBuffer(
        sizeof(positions), 3 * sizeof(float),
        RFX_USAGE_VERTEX_BUFFER | RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT | RFX_USAGE_SHADER_RESOURCE, RFX_MEM_CPU_TO_GPU, positions
    );

    // index
    RfxBuffer ibo = rfxCreateBuffer(
        sizeof(indices), sizeof(uint16_t),
        RFX_USAGE_INDEX_BUFFER | RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT | RFX_USAGE_SHADER_RESOURCE, RFX_MEM_CPU_TO_GPU, indices
    );

    // blas
    RfxGeometryTriangles tris = {};
    tris.vertexBuffer = vbo;
    tris.vertexCount = sizeof(positions) / (3 * sizeof(float));
    tris.vertexStride = 3 * sizeof(float);
    tris.vertexFormat = RFX_FORMAT_RGB32_FLOAT;
    tris.indexBuffer = ibo;
    tris.indexCount = sizeof(indices) / sizeof(uint16_t);
    tris.indexOffset = 0;
    tris.indexType = RFX_INDEX_UINT16;

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

    // tlas
    RfxAccelerationStructureDesc tlasDesc = {};
    tlasDesc.type = RFX_AS_TOP_LEVEL;
    tlasDesc.flags = RFX_BUILD_AS_PREFER_FAST_TRACE;
    tlasDesc.count = BOX_NUM; // Max instances

    RfxAccelerationStructure tlas = rfxCreateAccelerationStructure(&tlasDesc);

    // build as
    uint64_t s1 = rfxGetAccelerationStructureScratchSize(blas);
    uint64_t s2 = rfxGetAccelerationStructureScratchSize(tlas);
    RfxBuffer scratch = rfxCreateBuffer((s1 > s2 ? s1 : s2), 0, RFX_USAGE_SCRATCH_BUFFER, RFX_MEM_GPU_ONLY, NULL);

    // instances
    std::vector<RfxInstance> instanceData(BOX_NUM);
    const float lineWidth = 120.0f;
    const uint32_t lineSize = 100;
    const float step = lineWidth / (lineSize - 1);

    for (uint32_t i = 0; i < BOX_NUM; ++i) {
        RfxInstance& inst = instanceData[i];

        memset(inst.transform, 0, sizeof(inst.transform));
        inst.transform[0][0] = 1.0f;
        inst.transform[1][1] = 1.0f;
        inst.transform[2][2] = 1.0f;

        inst.transform[0][3] = -lineWidth * 0.5f + (i % lineSize) * step;
        inst.transform[1][3] = -10.0f + ((float)i / lineSize) * step;
        inst.transform[2][3] = 10.0f + ((float)i / lineSize) * step;

        inst.instanceId = i;
        inst.mask = 0xFF;
        inst.instanceContributionToHitGroupIndex = 0;
        inst.flags = RFX_INSTANCE_FORCE_OPAQUE;
        inst.blas = blas;
    }

    RfxBuffer instanceBuffer = rfxCreateBuffer(
        instanceData.size() * sizeof(RfxInstance), 0, RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT, RFX_MEM_CPU_TO_GPU, nullptr
    );

    // build tlas
    {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        rfxCmdUploadInstances(cmd, instanceBuffer, instanceData.data(), (uint32_t)instanceData.size());
        rfxCmdBuildAccelerationStructure(cmd, blas, scratch, nullptr);
        rfxCmdBuildAccelerationStructure(cmd, tlas, scratch, instanceBuffer);

        rfxEndFrame();
    }

    RfxShader shader = rfxCompileShaderMem(shaderSource, nullptr, 0);

    // RT pipeline
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
    rtpDesc.maxPayloadSize = 16;
    rtpDesc.maxAttributeSize = 8;

    RfxPipeline rtPipeline = rfxCreateRayTracingPipeline(&rtpDesc);
    RfxShaderBindingTable sbt = rfxCreateShaderBindingTable(rtPipeline);

    // Blit pipeline
    RfxPipelineDesc blitDesc = {};
    blitDesc.shader = shader;
    blitDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
    blitDesc.colorFormat = rfxGetSwapChainFormat();
    blitDesc.vsEntryPoint = "vsMain";
    blitDesc.psEntryPoint = "psMain";
    blitDesc.depthFormat = RFX_FORMAT_UNKNOWN;

    RfxPipeline blitPipeline = rfxCreatePipeline(&blitDesc);

    // Output tex
    RfxTexture outputTex =
        rfxCreateTexture(1280, 720, RFX_FORMAT_RGBA32_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, NULL);

    struct RTPush {
        uint32_t outputTexID;
        uint32_t tlasID;
        uint32_t indexBufferID;
        uint32_t uvBufferID; // unused
        float time;
    } pcRT;

    struct BlitPush {
        uint32_t texID;
    } pcBlit;

    pcRT.outputTexID = rfxGetTextureId(outputTex);
    pcRT.tlasID = rfxGetAccelerationStructureId(tlas);
    pcRT.indexBufferID = rfxGetBufferId(ibo);
    pcRT.uvBufferID = 0;

    pcBlit.texID = pcRT.outputTexID;

    while (!rfxWindowShouldClose()) {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        pcRT.time = (float)rfxGetTime();

        // RT
        rfxCmdBindPipeline(cmd, rtPipeline);
        rfxCmdPushConstants(cmd, &pcRT, sizeof(pcRT));

        RfxTraceRaysDesc trace = {};
        trace.sbt = sbt;
        trace.rayGenIndex = 0;
        trace.missIndex = 1;
        trace.missCount = 1;
        trace.hitIndex = 2;
        trace.hitCount = 1;

        rfxCmdTraceRays(cmd, &trace, 1280, 720, 1);

        // Blit
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(0, 0, 0, 1));

        rfxCmdBindPipeline(cmd, blitPipeline);
        rfxCmdPushConstants(cmd, &pcBlit, sizeof(pcBlit));
        rfxCmdDraw(cmd, 3, 1);

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
    rfxDestroyBuffer(ibo);
    rfxDestroyBuffer(scratch);
    rfxDestroyBuffer(instanceBuffer);

    return 0;
}
