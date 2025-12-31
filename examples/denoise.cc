// An SDF raymarcher with denoised indirect lighting and TAA (rfxCmdDenoise demo)
//
// Note: this is far from a good raytracer, ideally you'd use STBN instead of the hash33 function
//       (the denoiser will probably freak out otherwise)
//       https://github.com/NVIDIA-RTX/STBN

#include "HandmadeMath.h"
#include "rafx.h"
#include <imgui.h>

#include <cstring>
#include <cmath>

const char* kSdfShaderSource = R"(
#include "rafx.slang"

struct Camera {
    float4x4 viewInv;
    float4x4 projInv;
    float4x4 cleanViewProj;
    float4x4 cleanPrevViewProj;
    float4x4 view;
    float3   camPos;
    float    time;
    float2   resolution;
    float2   jitter;
    float    hitDistScale;
    uint     frameIndex;
    float2   _pad;

    uint idxRadiance;
    uint idxNormal;
    uint idxViewZ;
    uint idxMotion;
    uint idxBaseColor;
};

struct PC { uint camDataID; };
RFX_PUSH_CONSTANTS(PC, pc);

float3 LinearToYCoCg(float3 color) {
    float Y = dot(color, float3(0.25, 0.5, 0.25));
    float Co = dot(color, float3(0.5, 0.0, -0.5));
    float Cg = dot(color, float3(-0.25, 0.5, -0.25));
    return float3(Y, Co, Cg);
}

float3 hash33(float3 p3) {
    p3 = frac(p3 * float3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yxz+33.33);
    return frac((p3.xxy + p3.yxx)*p3.zyx);
}

float3 CosineSampleHemisphere(float3 n, float3 seed) {
    float3 u = hash33(seed);
    float r = sqrt(u.x);
    float theta = 2.0 * 3.14159 * u.y;
    float3 b = abs(n.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(n, b));
    float3 bitangent = cross(n, tangent);
    float3 localPos = float3(r * cos(theta), r * sin(theta), sqrt(max(0.0, 1.0 - u.x)));
    return normalize(localPos.x * tangent + localPos.y * bitangent + localPos.z * n);
}

float sdBox(float3 p, float3 b) { float3 q = abs(p) - b; return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }
float sdSphere(float3 p, float s) { return length(p) - s; }
float opSmoothUnion(float d1, float d2, float k) { float h = clamp(0.5 + 0.5*(d2-d1)/k, 0.0, 1.0); return lerp(d2, d1, h) - k*h*(1.0-h); }

struct MapRes { float dist; float matID; };
MapRes opUnion(MapRes m1, MapRes m2) { return (m1.dist < m2.dist) ? m1 : m2; }

MapRes map(float3 p, float time) {
    MapRes res = { p.y + 1.0, 1.0 }; // floor

    float3 q = p;
    q.x = (frac(p.x / 4.0 + 0.5) - 0.5) * 4.0;
    q.z = (frac(p.z / 4.0 + 0.5) - 0.5) * 4.0;

    float dBox = sdBox(q - float3(0, 0.0, 0), float3(0.5, 2.0, 0.5));
    float dSphere = sdSphere(q - float3(0, 1.0 + sin(time + p.x)*0.5, 0), 0.9);
    float dCol = opSmoothUnion(dBox, dSphere, 0.3);
    res = opUnion(res, { dCol, 2.0 }); // columns

    float3 ballPos = p - float3(0, 1.0, 0);
    res = opUnion(res, { sdSphere(ballPos, 1.5), 3.0 }); // ball

    // light sphere
    float3 lightPos = float3(3.5*sin(time*0.8), 2.5 + sin(time*1.3), 3.5*cos(time*0.8));
    res = opUnion(res, { sdSphere(p - lightPos, 0.3), 4.0 });

    return res;
}

float calcSoftShadow(float3 ro, float3 rd, float tmin, float tmax, const float k, float time) {
    float res = 1.0;
    float t = tmin;
    for(int i=0; i<24; i++) {
        float h = map(ro + rd*t, time).dist;
        res = min(res, k*h/t);
        t += clamp(h, 0.02, 0.10);
        if(res<0.005 || t>tmax) break;
    }
    return clamp(res, 0.0, 1.0);
}

float3 calcNormal(float3 p, float t) {
    const float h = 0.001;
    const float2 k = float2(1, -1);
    return normalize(k.xyy * map(p + k.xyy * h, t).dist +
                     k.yyx * map(p + k.yyx * h, t).dist +
                     k.yxy * map(p + k.yxy * h, t).dist +
                     k.xxx * map(p + k.xxx * h, t).dist);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    Camera cam = GetBuffer(pc.camDataID).Load<Camera>(0);
    if (id.x >= uint(cam.resolution.x) || id.y >= uint(cam.resolution.y)) return;

    RWTexture2D<float4> outRadiance  = GetRWTexture(cam.idxRadiance);
    RWTexture2D<float4> outNormal    = GetRWTexture(cam.idxNormal);
    RWTexture2D<float4> outViewZ     = GetRWTexture(cam.idxViewZ);
    RWTexture2D<float4> outMotion    = GetRWTexture(cam.idxMotion);
    RWTexture2D<float4> outBaseColor = GetRWTexture(cam.idxBaseColor);

    float2 pixelCenter = float2(id.xy) + 0.5;
    float2 uv = (pixelCenter + cam.jitter) / cam.resolution;
    float2 cleanUV = pixelCenter / cam.resolution;
    float2 ndc = uv * 2.0 - 1.0;

    float4 target = mul(cam.projInv, float4(ndc, 1.0, 1.0));
    float3 rayDir = mul(cam.viewInv, float4(normalize(target.xyz / target.w), 0.0)).xyz;
    float3 rayOrigin = cam.camPos;
    rayDir = normalize(rayDir);

    float t = 0.0;
    float tMax = 100.0;
    MapRes h = { tMax, 0.0 };

    for(int i=0; i<128 && t<tMax; i++) {
        h = map(rayOrigin + rayDir * t, cam.time);
        if(h.dist < 0.001) break;
        t += h.dist;
    }

    float3 lighting = float3(0);
    float3 albedo = float3(0);
    float3 normal = float3(0);
    float roughness = 0.0;
    float viewZ = tMax;
    float2 motion = float2(0);

    if(t < tMax) {
        float3 pos = rayOrigin + rayDir * t;
        normal = calcNormal(pos, cam.time);
        bool isLight = false;

        if(h.matID == 1.0) {
            float check = fmod(floor(pos.x*2.0) + floor(pos.z*2.0), 2.0);
            albedo = (check < 0.5) ? float3(0.05) : float3(0.5);
            roughness = 0.5;
        } else if (h.matID == 2.0) {
            albedo = float3(0.8, 0.4, 0.1);
            roughness = 0.8;
        } else if (h.matID == 3.0) {
            albedo = float3(0.1, 0.8, 0.2);
            roughness = 0.1;
        } else {
            isLight = true;
            albedo = float3(0.0);
            lighting = float3(1.0, 0.8, 0.4) * 50.0;
            roughness = 0.0;
        }

        if(!isLight) {
            // sun
            float3 sunDir = normalize(float3(0.5, 0.6, -0.4));
            float sunNDL = max(dot(normal, sunDir), 0.0);
            float sunShadow = calcSoftShadow(pos, sunDir, 0.05, 20.0, 16.0, cam.time);
            float3 directSun = float3(3.0, 2.9, 2.7) * sunNDL * sunShadow;

            // pointlight
            float3 lightPos = float3(3.5*sin(cam.time*0.8), 2.5 + sin(cam.time*1.3), 3.5*cos(cam.time*0.8));
            float3 toLight = lightPos - pos;
            float distLight = length(toLight);
            float3 dirLight = toLight / distLight;
            float pointAtten = 1.0 / (1.0 + distLight*distLight*0.005);
            float pointNDL = max(dot(normal, dirLight), 0.0);
            float3 directPoint = float3(1.0, 0.8, 0.4) * 100.0 * pointNDL * pointAtten;

            // point shadow
            float pointShadow = 1.0;
            MapRes hShadow = map(pos + dirLight * 0.1, cam.time);
            if(hShadow.dist < distLight - 0.2 && hShadow.matID != 4.0) pointShadow = 0.0;
            directPoint *= pointShadow;

            // indirect
            float3 seed = float3(id.xy, float(cam.frameIndex) * 0.1);

            float tMax2 = 10.0;
            float3 indirect = float3(0);

            const int indirectSamples = 4;
            for(int s=0; s<indirectSamples; s++) {
                float3 bounceDir = CosineSampleHemisphere(normal, seed + float3(float(s)*19.19, float(s)*47.47, float(s)*101.01));

                float t2 = 0.01;
                MapRes h2 = { tMax2, 0.0 };
                float3 indirectSample = float3(0);

                for(int j=0; j<16 && t2<tMax2; j++) {
                    h2 = map(pos + bounceDir * t2, cam.time);
                    if(h2.dist < 0.01) break;
                    t2 += h2.dist;
                }

                if(t2 < tMax2) {
                    if(h2.matID == 4.0) indirectSample = float3(1.0, 0.8, 0.4) * 10.0; // hit light
                    else if(h2.matID == 1.0) indirectSample = float3(0.1);
                    else indirectSample = float3(0.2, 0.1, 0.05);
                } else {
                    indirectSample = float3(0.05, 0.05, 0.1); // sky
                }

                indirect += indirectSample;
            }

            // commented out to make it more pronounced
            //indirect /= float(indirectSamples);

            lighting = directSun + directPoint + indirect;
        }

        float4 prevClip = mul(cam.cleanPrevViewProj, float4(pos, 1.0));
        float2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
        motion = prevUV - cleanUV;
        viewZ = abs(mul(cam.view, float4(pos, 1.0)).z);
    } else {
        albedo = float3(0.05, 0.05, 0.1);
        lighting = float3(1.0);
        viewZ = tMax;
        normal = -rayDir;
    }

    outBaseColor[id.xy] = float4(albedo, 1.0);
    outRadiance[id.xy]  = float4(LinearToYCoCg(lighting), saturate(t / cam.hitDistScale));
    outNormal[id.xy]    = float4(normal, roughness);
    outViewZ[id.xy]     = float4(viewZ, 0, 0, 0);
    outMotion[id.xy]    = float4(motion, 0, 0);
}
)";

const char* kTaaShaderSource = R"(
#include "rafx.slang"

struct PC {
    uint texLighting; // NRD output (YCoCg)
    uint texAlbedo;   // Base color
    uint texMotion;   // MV
    uint texHistory;  // Previous frame
    uint texViewZ;    // Depth for dilation
    uint texResult;   // Output
};
RFX_PUSH_CONSTANTS(PC, pc);

float3 YCoCgToLinear(float3 color) {
    float t = color.x - color.z;
    float3 r;
    r.y = color.x + color.z;
    r.x = t + color.y;
    r.z = t - color.y;
    return max(r, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    Texture2D<float4>   inLight   = GetTexture(pc.texLighting);
    Texture2D<float4>   inAlbedo  = GetTexture(pc.texAlbedo);
    Texture2D<float4>   inMotion  = GetTexture(pc.texMotion);
    Texture2D<float4>   inHistory = GetTexture(pc.texHistory);
    Texture2D<float4>   inViewZ   = GetTexture(pc.texViewZ);
    RWTexture2D<float4> outResult = GetRWTexture(pc.texResult);

    uint w, h;
    outResult.GetDimensions(w, h);
    if(id.x >= w || id.y >= h) return;
    int2 dim = int2(w, h);

    float3 cMin = float3(10000);
    float3 cMax = float3(-10000);
    float3 cCurrent = float3(0);

    float bestDepth = 1e9;
    float2 bestMotion = float2(0);

    for(int y = -1; y <= 1; ++y) {
        for(int x = -1; x <= 1; ++x) {
            int2 pos = clamp(int2(id.xy) + int2(x, y), int2(0), dim - 1);

            float3 l = YCoCgToLinear(inLight[pos].rgb);
            float3 a = inAlbedo[pos].rgb;
            float3 c = l * a;

            c = c / (1.0 + c);

            cMin = min(cMin, c);
            cMax = max(cMax, c);

            if(x == 0 && y == 0) {
                cCurrent = c;
                bestMotion = inMotion[pos].xy;
                bestDepth = inViewZ[pos].x;
            }

            float z = inViewZ[pos].x;
            if(z < bestDepth) {
                bestDepth = z;
                bestMotion = inMotion[pos].xy;
            }
        }
    }

    float2 uv = (float2(id.xy) + 0.5) / float2(w, h);
    float2 prevUV = uv + bestMotion;

    float3 cHistory = cCurrent;
    float blend = 0.1;

    if (all(prevUV >= 0.0) && all(prevUV <= 1.0)) {
        float3 hRaw = inHistory.SampleLevel(GetSamplerLinearClamp(), prevUV, 0).rgb;
        float3 hTonemapped = hRaw / (1.0 + hRaw);

        cHistory = clamp(hTonemapped, cMin, cMax);
    } else {
        blend = 1.0;
    }

    float3 cResult = lerp(cHistory, cCurrent, blend);
    cResult = cResult / max(0.0001, 1.0 - cResult);

    outResult[id.xy] = float4(cResult, 1.0);
}
)";

const char* kBlitShaderSource = R"(
#include "rafx.slang"
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD; };
struct PC {
    uint displayTexID;
    uint mode;
    uint taaTexID;
};
RFX_PUSH_CONSTANTS(PC, ubo);

float3 YCoCgToLinear(float3 color) {
    float t = color.x - color.z;
    float3 r;
    r.y = color.x + color.z;
    r.x = t + color.y;
    r.z = t - color.y;
    return max(r, 0.0);
}

[shader("vertex")]
VSOut vsMain(uint vI : SV_VertexID) {
    VSOut o;
    o.uv = float2((vI << 1) & 2, vI & 2);
    o.pos = float4(o.uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

[shader("fragment")]
float4 psMain(VSOut i) : SV_Target {
    float2 uv = float2(i.uv.x, 1.0 - i.uv.y);
    uint texID = ubo.displayTexID;
    uint currentMode = ubo.mode;

    if (ubo.mode == 6) {
        float2 pipMin = float2(0.25, 0.25);
        float2 pipMax = float2(1.0, 1.0);
        if (uv.x > pipMin.x && uv.x < pipMax.x && uv.y > pipMin.y && uv.y < pipMax.y) {
            uv = (uv - pipMin) / (pipMax - pipMin);
            texID = ubo.taaTexID;
            currentMode = 7;
        }
    }

    float4 val = GetTexture(texID).Sample(GetSamplerLinearClamp(), uv);

    if (currentMode == 0 || currentMode == 1) {
        float3 col = YCoCgToLinear(val.rgb);
        col = col / (col + 1.0);
        col = pow(col, 1.0/2.2);
        return float4(col, 1.0);
    }
    else if (currentMode == 7) {
        float3 col = val.rgb;
        col = col / (col + 1.0);
        col = pow(col, 1.0/2.2);
        return float4(col, 1.0);
    }
    else if (currentMode == 2) return float4(val.rgb * 0.5 + 0.5, 1.0);
    else if (currentMode == 3) return float4(val.rrr / 20.0, 1.0);
    else if (currentMode == 4) return float4(abs(val.xy) * 10.0, 0.0, 1.0);
    else if (currentMode == 5) return float4(val.aaa, 1.0);
    else return val;
}
)";

struct CameraData {
    HMM_Mat4 viewInv;
    HMM_Mat4 projInv;
    HMM_Mat4 cleanViewProj;
    HMM_Mat4 cleanPrevViewProj;
    HMM_Mat4 view;
    HMM_Vec3 camPos;
    float time;
    HMM_Vec2 resolution;
    HMM_Vec2 jitter;
    float hitDistScale;
    uint32_t frameIndex;
    float _pad[2];

    uint32_t idxRadiance;
    uint32_t idxNormal;
    uint32_t idxViewZ;
    uint32_t idxMotion;
    uint32_t idxBaseColor;
};

float Halton(int index, int base) {
    float f = 1, r = 0;
    while (index > 0) {
        f = f / (float)base;
        r = r + f * (float)(index % base);
        index = index / base;
    }
    return r;
}

struct App {
    int width = 1280;
    int height = 720;

    RfxTexture texRadiance = nullptr;
    RfxTexture texNormal = nullptr;
    RfxTexture texViewZ = nullptr;
    RfxTexture texMotion = nullptr;
    RfxTexture texDenoised = nullptr;
    RfxTexture texValidation = nullptr;
    RfxTexture texBaseColor = nullptr;
    RfxTexture texFinalTAA = nullptr;
    RfxTexture texHistory = nullptr;

    static const int kFrameCount = 3;
    RfxBuffer camBuffers[kFrameCount] = { nullptr };

    RfxPipeline psoSDF = nullptr;
    RfxPipeline psoTAA = nullptr;
    RfxPipeline psoBlit = nullptr;
    RfxShader shSDF = nullptr;
    RfxShader shTAA = nullptr;
    RfxShader shBlit = nullptr;

    RfxDenoiser denoiser = nullptr;

    HMM_Vec3 camTarget = { 0, 0, 0 };
    float camDist = 5.0f;
    float camPitch = 0.5f;
    float camYaw = 0.7f;
    HMM_Mat4 cleanPrevViewProj = HMM_M4D(1.0f);
    HMM_Mat4 cleanPrevProj = HMM_M4D(1.0f);
    HMM_Mat4 prevView = HMM_M4D(1.0f);

    int frameIndex = 0;
    uint32_t nrdFrameIndex = 0;
    bool wasNRDEnabled = true;
    HMM_Vec2 prevJitter = { 0, 0 };

    int viewMode = 7;
    bool enableNRD = true;
    bool freezeJitter = true; /* TODO */
    int currentDenoiserType = (int)RFX_DENOISER_REBLUR_DIFFUSE;
    float disocclusionThreshold = 0.02f;
    float denoisingRange = 2000.0f;
    float hitDistScale = 20.0f;

    void Init() {
        if (!rfxOpenWindow("Rafx Denoise Raymarcher", width, height))
            exit(1);
        rfxSetWindowFlags(RFX_WINDOW_ALWAYS_ACTIVE | RFX_WINDOW_VSYNC);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::StyleColorsDark();
        rfxInitImGui();

        CreateBuffers(width, height);
        CreateShaders();
        CreateCameraBuffers();
    }

    void CreateCameraBuffers() {
        for (int i = 0; i < kFrameCount; i++) {
            camBuffers[i] = rfxCreateBuffer(sizeof(CameraData), 0, RFX_USAGE_SHADER_RESOURCE, RFX_MEM_CPU_TO_GPU, nullptr);
        }
    }

    void CreateBuffers(int w, int h) {
        if (texRadiance) {
            rfxDestroyTexture(texRadiance);
            rfxDestroyTexture(texNormal);
            rfxDestroyTexture(texViewZ);
            rfxDestroyTexture(texMotion);
            rfxDestroyTexture(texDenoised);
            rfxDestroyTexture(texValidation);
            rfxDestroyTexture(texBaseColor);
            rfxDestroyTexture(texFinalTAA);
            rfxDestroyTexture(texHistory);
        }
        if (denoiser)
            rfxDestroyDenoiser(denoiser);

        // NRD textures
        texRadiance =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texNormal =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texViewZ = rfxCreateTexture(w, h, RFX_FORMAT_R32_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texMotion =
            rfxCreateTexture(w, h, RFX_FORMAT_RG32_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texDenoised =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texValidation =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA8_UNORM, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texBaseColor =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA8_UNORM, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);

        // TAA textures
        texFinalTAA =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);
        texHistory =
            rfxCreateTexture(w, h, RFX_FORMAT_RGBA16_FLOAT, 1, RFX_TEXTURE_USAGE_STORAGE | RFX_TEXTURE_USAGE_SHADER_RESOURCE, nullptr);

        denoiser = rfxCreateDenoiser((RfxDenoiserType)currentDenoiserType, w, h);
    }

    void CreateShaders() {
        shSDF = rfxCompileShaderMem(kSdfShaderSource, nullptr, 0);
        RfxComputePipelineDesc csDesc = {};
        csDesc.shader = shSDF;
        csDesc.entryPoint = "main";
        psoSDF = rfxCreateComputePipeline(&csDesc);

        shTAA = rfxCompileShaderMem(kTaaShaderSource, nullptr, 0);
        csDesc.shader = shTAA;
        csDesc.entryPoint = "main";
        psoTAA = rfxCreateComputePipeline(&csDesc);

        shBlit = rfxCompileShaderMem(kBlitShaderSource, nullptr, 0);
        RfxPipelineDesc gfxDesc = {};
        gfxDesc.shader = shBlit;
        gfxDesc.colorFormat = rfxGetSwapChainFormat();
        gfxDesc.topology = RFX_TOPOLOGY_TRIANGLE_LIST;
        gfxDesc.vsEntryPoint = "vsMain";
        gfxDesc.psEntryPoint = "psMain";
        gfxDesc.cullMode = RFX_CULL_NONE;
        psoBlit = rfxCreatePipeline(&gfxDesc);
    }

    void Update() {
        int curW = rfxGetWindowWidth();
        int curH = rfxGetWindowHeight();
        if (curW > 0 && curH > 0 && (curW != width || curH != height)) {
            width = curW;
            height = curH;
            CreateBuffers(width, height);
            frameIndex = 0;
        }

        if (!ImGui::GetIO().WantCaptureMouse && rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT)) {
            float dx, dy;
            rfxGetMouseDelta(&dx, &dy);
            camYaw -= dx * 0.005f;
            camPitch -= dy * 0.005f;
            if (camPitch < 0.1f)
                camPitch = 0.1f;
            if (camPitch > 1.5f)
                camPitch = 1.5f;
        }
        if (!ImGui::GetIO().WantCaptureKeyboard) {
            if (rfxIsKeyDown(RFX_KEY_W))
                camDist -= 0.1f;
            if (rfxIsKeyDown(RFX_KEY_S))
                camDist += 0.1f;
        }
    }

    void Render() {
        rfxBeginFrame();
        RfxCommandList cmd = rfxGetCommandList();

        HMM_Vec3 camPos = { camTarget.X + sinf(camYaw) * cosf(camPitch) * camDist, camTarget.Y + sinf(camPitch) * camDist,
                            camTarget.Z + cosf(camYaw) * cosf(camPitch) * camDist };
        HMM_Mat4 view = HMM_LookAt_RH(camPos, camTarget, { 0, 1, 0 });
        HMM_Mat4 proj = HMM_Perspective_RH_ZO(60.0f * (3.14159f / 180.0f), (float)width / (float)height, 0.1f, 1000.0f);
        proj.Elements[1][1] *= -1.0f;

        int jitterIdx = frameIndex % 8;
        float jx = freezeJitter ? 0.0f : (Halton(jitterIdx + 1, 2) - 0.5f);
        float jy = freezeJitter ? 0.0f : (Halton(jitterIdx + 1, 3) - 0.5f);

        HMM_Mat4 projJittered = proj;
        projJittered.Elements[2][0] += (jx * 2.0f) / width;
        projJittered.Elements[2][1] += (jy * 2.0f) / height;

        HMM_Mat4 cleanViewProj = HMM_MulM4(proj, view);

        if (frameIndex == 0) {
            cleanPrevViewProj = cleanViewProj;
            cleanPrevProj = proj;
            prevView = view;
        }

        RfxBuffer currentCamBuffer = camBuffers[frameIndex % kFrameCount];
        CameraData* gpuCam = (CameraData*)rfxMapBuffer(currentCamBuffer);
        if (gpuCam) {
            gpuCam->viewInv = HMM_InvGeneralM4(view);
            gpuCam->projInv = HMM_InvGeneralM4(projJittered);
            gpuCam->cleanViewProj = cleanViewProj;
            gpuCam->cleanPrevViewProj = cleanPrevViewProj;
            gpuCam->view = view;
            gpuCam->camPos = camPos;
            gpuCam->time = (float)rfxGetTime();
            gpuCam->resolution = { (float)width, (float)height };
            gpuCam->jitter = { jx, jy };
            gpuCam->hitDistScale = hitDistScale;
            gpuCam->frameIndex = frameIndex;
            gpuCam->idxRadiance = rfxGetTextureId(texRadiance);
            gpuCam->idxNormal = rfxGetTextureId(texNormal);
            gpuCam->idxViewZ = rfxGetTextureId(texViewZ);
            gpuCam->idxMotion = rfxGetTextureId(texMotion);
            gpuCam->idxBaseColor = rfxGetTextureId(texBaseColor);
            rfxUnmapBuffer(currentCamBuffer);
        }

        // raymarch SDF
        rfxCmdBeginProfile(cmd, "SDF Raymarch");
        rfxCmdBindPipeline(cmd, psoSDF);
        struct {
            uint32_t camDataID;
        } pc = { rfxGetBufferId(currentCamBuffer) };
        rfxCmdPushConstants(cmd, &pc, sizeof(pc));
        rfxCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        rfxCmdEndProfile(cmd);

        // denoise
        if (enableNRD) {
            if (!wasNRDEnabled) {
                nrdFrameIndex = 0;
            }

            rfxCmdBeginProfile(cmd, "NRD Denoise");
            RfxDenoiserSettings ds = {};
            HMM_Mat4 projT = HMM_TransposeM4(proj);
            HMM_Mat4 viewT = HMM_TransposeM4(view);
            HMM_Mat4 prevProjT = HMM_TransposeM4(cleanPrevProj);
            HMM_Mat4 prevViewT = HMM_TransposeM4(prevView);

            memcpy(ds.viewToClip, &projT, 64);
            memcpy(ds.viewToClipPrev, &prevProjT, 64);
            memcpy(ds.worldToView, &viewT, 64);
            memcpy(ds.worldToViewPrev, &prevViewT, 64);

            ds.denoisingRange = denoisingRange;
            ds.viewZScale = 1.0f;
            ds.disocclusionThreshold = disocclusionThreshold;
            ds.motionVectorScale[0] = 1.0f;
            ds.motionVectorScale[1] = 1.0f;
            ds.isMotionVectorInWorldSpace = false;
            ds.frameIndex = nrdFrameIndex;
            ds.jitter[0] = jx;
            ds.jitter[1] = jy;
            ds.jitterPrev[0] = prevJitter.X;
            ds.jitterPrev[1] = prevJitter.Y;
            ds.resetHistory = (nrdFrameIndex == 0);
            ds.enableValidation = (viewMode == 6);

            RfxTexture resources[RFX_DENOISER_RESOURCE_COUNT] = {};
            resources[RFX_DENOISER_IN_DIFF_RADIANCE] = texRadiance;
            resources[RFX_DENOISER_IN_NORMAL_ROUGHNESS] = texNormal;
            resources[RFX_DENOISER_IN_VIEWZ] = texViewZ;
            resources[RFX_DENOISER_IN_MV] = texMotion;
            resources[RFX_DENOISER_OUT_DIFF_RADIANCE] = texDenoised;
            resources[RFX_DENOISER_OUT_VALIDATION] = texValidation;

            rfxCmdDenoise(cmd, denoiser, &ds, resources, RFX_DENOISER_RESOURCE_COUNT);
            rfxCmdEndProfile(cmd);
            nrdFrameIndex++;
        }
        wasNRDEnabled = enableNRD;

        // TAA resolve
        rfxCmdBeginProfile(cmd, "TAA Resolve");
        rfxCmdBindPipeline(cmd, psoTAA);
        struct {
            uint32_t texL, texA, texM, texH, texZ, texR;
        } taaPC = { rfxGetTextureId(enableNRD ? texDenoised : texRadiance),
                    rfxGetTextureId(texBaseColor),
                    rfxGetTextureId(texMotion),
                    rfxGetTextureId(texHistory),
                    rfxGetTextureId(texViewZ),
                    rfxGetTextureId(texFinalTAA) };
        rfxCmdPushConstants(cmd, &taaPC, sizeof(taaPC));
        rfxCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
        rfxCmdEndProfile(cmd);

        // Update history
        rfxCmdCopyTexture(cmd, texFinalTAA, texHistory);

        // Blit
        rfxCmdBeginSwapchainRenderPass(cmd, RFX_FORMAT_UNKNOWN, RFX_COLOR(0, 0, 0, 1));
        rfxCmdBindPipeline(cmd, psoBlit);
        struct {
            uint32_t displayTexID;
            uint32_t mode;
            uint32_t taaTexID;
        } blitPC;

        RfxTexture displayTex = texFinalTAA;
        if (viewMode == 0)
            displayTex = texRadiance;
        else if (viewMode == 1)
            displayTex = texDenoised;
        else if (viewMode == 2)
            displayTex = texNormal;
        else if (viewMode == 3)
            displayTex = texViewZ;
        else if (viewMode == 4)
            displayTex = texMotion;
        else if (viewMode == 5)
            displayTex = texRadiance; // hitdist
        else if (viewMode == 6)
            displayTex = texValidation;

        blitPC.displayTexID = rfxGetTextureId(displayTex);
        blitPC.mode = viewMode;
        blitPC.taaTexID = rfxGetTextureId(texFinalTAA);

        rfxCmdPushConstants(cmd, &blitPC, sizeof(blitPC));
        rfxCmdDraw(cmd, 3, 1);
        rfxCmdEndRenderPass(cmd);

        // UI
        int winW = rfxGetWindowWidth();
        int winH = rfxGetWindowHeight();
        float mx, my;
        rfxGetMousePos(&mx, &my);
        auto& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)winW, (float)winH);
        io.DeltaTime = rfxGetDeltaTime();
        io.MousePos = ImVec2(mx, my);
        io.MouseDown[0] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_LEFT);
        io.MouseDown[1] = rfxIsMouseButtonDown(RFX_MOUSE_BUTTON_RIGHT);
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(ImVec2(0, 0));
        ImGui::Begin("Denoiser");
        ImGui::Text("FPS: %.1f", 1.0f / rfxGetDeltaTime());
        ImGui::Checkbox("Enable NRD", &enableNRD);
        ImGui::Checkbox("Freeze Jitter", &freezeJitter);
        const char* views[] = { "Input Lighting (YCoCg)", "NRD Output (YCoCg)", "Normals",    "ViewZ",
                                "Motion Vectors",         "Hit Distance",       "Validation", "Final TAA" };
        ImGui::Combo("View", &viewMode, views, 8);
        ImGui::Separator();
        ImGui::SliderFloat("Disocclusion", &disocclusionThreshold, 0.001f, 0.1f);
        ImGui::Text("Denoiser Type:");
        bool denoiserChanged = false;
        denoiserChanged |= ImGui::RadioButton("ReBLUR", &currentDenoiserType, RFX_DENOISER_REBLUR_DIFFUSE);
        ImGui::SetItemTooltip("recurrent blur based denoiser");
        ImGui::SameLine();
        denoiserChanged |= ImGui::RadioButton("ReLAX", &currentDenoiserType, RFX_DENOISER_RELAX_DIFFUSE);
        ImGui::SetItemTooltip("a-trous based denoiser");
        if (ImGui::Button("Reset")) {
            currentDenoiserType = RFX_DENOISER_REBLUR_DIFFUSE;
            denoiserChanged = true;
            disocclusionThreshold = 0.02f;
        }
        if (denoiserChanged) {
            if (denoiser)
                rfxDestroyDenoiser(denoiser);
            denoiser = rfxCreateDenoiser((RfxDenoiserType)currentDenoiserType, width, height);
            nrdFrameIndex = 0;
        }
        ImGui::Separator();
        ImGui::Text("GPU Profiler:");
        static RfxGpuTimestamp timestamps[32];
        uint32_t count = rfxGetGpuTimestamps(timestamps, 32);
        float totalMs = 0.0f;
        for (uint32_t i = 0; i < count; ++i)
            totalMs += timestamps[i].microseconds / 1000.0f;
        if (ImGui::BeginTable("ProfilerTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 85.0f);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("%", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (uint32_t i = 0; i < count; ++i) {
                float ms = timestamps[i].microseconds / 1000.0f;
                float fraction = (totalMs > 0.0f) ? (ms / totalMs) : 0.0f;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", timestamps[i].name);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f ms", ms);
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), "");
                ImGui::PopStyleColor();
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "TOTAL");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "%.2f ms/%.0f fps", totalMs, (totalMs > 0.0f) ? (1000.0f / totalMs) : 0.0f);
            ImGui::EndTable();
        }
        ImGui::End();

        RfxImGuiDrawData imguiDrawData = {};
        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        imguiDrawData.drawLists = (void* const*)drawData->CmdLists.Data;
        imguiDrawData.drawListCount = drawData->CmdLists.Size;
        ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
        imguiDrawData.textures = (void* const*)platformIO.Textures.Data;
        imguiDrawData.textureCount = platformIO.Textures.Size;
        imguiDrawData.displayWidth = drawData->DisplaySize.x;
        imguiDrawData.displayHeight = drawData->DisplaySize.y;
        imguiDrawData.hdrScale = 1.0f;
        rfxCmdDrawImGui(cmd, &imguiDrawData);

        rfxEndFrame();

        cleanPrevViewProj = cleanViewProj;
        cleanPrevProj = proj;
        prevView = view;
        prevJitter = { jx, jy };
        frameIndex++;
    }

    void Cleanup() {
        rfxDestroyPipeline(psoSDF);
        rfxDestroyPipeline(psoTAA);
        rfxDestroyPipeline(psoBlit);
        rfxDestroyShader(shSDF);
        rfxDestroyShader(shTAA);
        rfxDestroyShader(shBlit);
        for (int i = 0; i < kFrameCount; i++)
            rfxDestroyBuffer(camBuffers[i]);
        rfxDestroyTexture(texRadiance);
        rfxDestroyTexture(texNormal);
        rfxDestroyTexture(texViewZ);
        rfxDestroyTexture(texMotion);
        rfxDestroyTexture(texDenoised);
        rfxDestroyTexture(texValidation);
        rfxDestroyTexture(texBaseColor);
        rfxDestroyTexture(texFinalTAA);
        rfxDestroyTexture(texHistory);
        rfxDestroyDenoiser(denoiser);
        rfxShutdownImGui();
        ImGui::DestroyContext();
    }
};

int main() {
    App app;
    app.Init();
    while (!rfxWindowShouldClose()) {
        app.Update();
        app.Render();
    }
    app.Cleanup();
    return 0;
}
