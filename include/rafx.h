/* Rafx is a C graphics abstraction library designed around modern graphics workflows. */

#ifndef RAFX_H
#define RAFX_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef RAFX_API
#    if defined(_WIN32) && !defined(RAFX_STATIC)
#        ifdef RAFX_EXPORTS
#            define RAFX_API __declspec(dllexport)
#        else
#            define RAFX_API __declspec(dllimport)
#        endif
#    elif defined(__GNUC__) && !defined(RAFX_STATIC)
#        define RAFX_API __attribute__((visibility("default")))
#    else
#        define RAFX_API
#    endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RFX_MAX_BINDLESS_TEXTURES
#    define RFX_MAX_BINDLESS_TEXTURES 4096
#endif

//
// Helper macros
//

#define RFX_ENUM(type, name)                                                                                                               \
    type name;                                                                                                                             \
    enum
#define RFX_BIT(iota) (1 << (iota))
#define RFX_COLORF(r, g, b, a) ((RfxColor){ (float)(r), (float)(g), (float)(b), (float)(a) })
#define RFX_COLOR(r, g, b, a) ((RfxColor){ (float)(r) / 255.0f, (float)(g) / 255.0f, (float)(b) / 255.0f, (float)(a) / 255.0f })

//
// Typedefs
//

typedef struct {
    float r, g, b, a;
} RfxColor;

typedef struct RfxBufferImpl* RfxBuffer;
typedef struct RfxTextureImpl* RfxTexture;
typedef struct RfxShaderImpl* RfxShader;
typedef struct RfxPipelineImpl* RfxPipeline;
typedef struct RfxSamplerImpl* RfxSampler;
typedef struct RfxCommandListImpl* RfxCommandList;
typedef struct RfxDenoiserImpl* RfxDenoiser;
typedef struct RfxAccelerationStructureImpl* RfxAccelerationStructure;
typedef struct RfxShaderBindingTableImpl* RfxShaderBindingTable;
typedef struct RfxMicromapImpl* RfxMicromap;
typedef struct RfxUpscalerImpl* RfxUpscaler;
typedef struct RfxFenceImpl* RfxFence;
typedef struct RfxQueryPoolImpl* RfxQueryPool;

typedef enum {
    RFX_FILTER_NEAREST,
    RFX_FILTER_LINEAR,
} RfxFilter;

typedef enum {
    RFX_WRAP_REPEAT,
    RFX_WRAP_CLAMP,
    RFX_WRAP_MIRROR,
} RfxAddressMode;

typedef enum {
    RFX_TOPOLOGY_TRIANGLE_LIST,
    RFX_TOPOLOGY_TRIANGLE_STRIP,
    RFX_TOPOLOGY_POINT_LIST,
    RFX_TOPOLOGY_LINE_LIST,
    RFX_TOPOLOGY_LINE_STRIP,
    RFX_TOPOLOGY_LINE_LIST_WITH_ADJACENCY,
    RFX_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY,
    RFX_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY,
    RFX_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY,
    RFX_TOPOLOGY_PATCH_LIST
} RfxTopology;

typedef RFX_ENUM(uint32_t, RfxBufferUsageFlags){
    RFX_USAGE_SHADER_RESOURCE = RFX_BIT(0),                    // Read-only shader resource (SRV)
    RFX_USAGE_SHADER_RESOURCE_STORAGE = RFX_BIT(1),            // Read/write shader resource (UAV)
    RFX_USAGE_VERTEX_BUFFER = RFX_BIT(2),                      // Vertex buffer
    RFX_USAGE_INDEX_BUFFER = RFX_BIT(3),                       // Index buffer
    RFX_USAGE_CONSTANT_BUFFER = RFX_BIT(4),                    // Constant buffer
    RFX_USAGE_ARGUMENT_BUFFER = RFX_BIT(5),                    // Argument buffer in "Indirect" commands
    RFX_USAGE_SCRATCH_BUFFER = RFX_BIT(6),                     // Scratch buffer
    RFX_USAGE_SHADER_BINDING_TABLE = RFX_BIT(7),               // Shader binding table
    RFX_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT = RFX_BIT(8), // AS Build Input
    RFX_USAGE_MICROMAP_BUILD_INPUT = RFX_BIT(9),               // Micromap Build Input
    RFX_USAGE_TRANSFER_SRC = RFX_BIT(10),                      // Allow buffer to be source of copy
    RFX_USAGE_TRANSFER_DST = RFX_BIT(11),                      // Allow buffer to be destination of copy
};

typedef enum {
    RFX_STATE_UNDEFINED,
    RFX_STATE_PRESENT,
    RFX_STATE_COPY_SRC,
    RFX_STATE_COPY_DST,
    RFX_STATE_VERTEX_BUFFER,
    RFX_STATE_INDEX_BUFFER,
    RFX_STATE_INDIRECT_ARGUMENT,
    RFX_STATE_SHADER_READ,  // SRV (texture or buffer)
    RFX_STATE_SHADER_WRITE, // UAV (storage)
    RFX_STATE_RENDER_TARGET,
    RFX_STATE_DEPTH_READ,
    RFX_STATE_DEPTH_WRITE,
    RFX_STATE_SCRATCH_BUFFER, // AS scratch buffer
    RFX_STATE_RESOLVE_SRC,
    RFX_STATE_RESOLVE_DST,
} RfxResourceState;

typedef enum {
    RFX_MEM_GPU_ONLY,
    RFX_MEM_CPU_TO_GPU, // Upload heap
    RFX_MEM_GPU_TO_CPU, // Readback heap
} RfxMemoryType;

typedef enum {
    RFX_FORMAT_UNKNOWN,
    RFX_FORMAT_RGBA8_UNORM,
    RFX_FORMAT_RGBA8_SRGB,
    RFX_FORMAT_BGRA8_UNORM,
    RFX_FORMAT_BGRA8_SRGB,
    // Integers
    RFX_FORMAT_R8_UINT,
    RFX_FORMAT_R8_SINT,
    RFX_FORMAT_RG8_UINT,
    RFX_FORMAT_RG8_SINT,
    RFX_FORMAT_RGBA8_UINT,
    RFX_FORMAT_RGBA8_SINT,
    RFX_FORMAT_R16_UINT,
    RFX_FORMAT_R16_SINT,
    RFX_FORMAT_R16_UNORM,
    RFX_FORMAT_R16_SNORM,
    RFX_FORMAT_RG16_UINT,
    RFX_FORMAT_RG16_SINT,
    RFX_FORMAT_RG16_UNORM,
    RFX_FORMAT_RG16_SNORM,
    RFX_FORMAT_RGBA16_UINT,
    RFX_FORMAT_RGBA16_SINT,
    RFX_FORMAT_RGBA16_UNORM,
    RFX_FORMAT_RGBA16_SNORM,
    RFX_FORMAT_R32_UINT,
    RFX_FORMAT_R32_SINT,
    RFX_FORMAT_RG32_UINT,
    RFX_FORMAT_RG32_SINT,
    RFX_FORMAT_RGB32_UINT,
    RFX_FORMAT_RGB32_SINT,
    RFX_FORMAT_RGBA32_UINT,
    RFX_FORMAT_RGBA32_SINT,
    // Packed
    RFX_FORMAT_R10_G10_B10_A2_UNORM,
    RFX_FORMAT_R10_G10_B10_A2_UINT,
    RFX_FORMAT_R11_G11_B10_UFLOAT,
    RFX_FORMAT_R9_G9_B9_E5_UFLOAT,
    // BCn compressed
    RFX_FORMAT_BC1_RGBA_UNORM,
    RFX_FORMAT_BC1_RGBA_SRGB,
    RFX_FORMAT_BC2_RGBA_UNORM,
    RFX_FORMAT_BC2_RGBA_SRGB,
    RFX_FORMAT_BC3_RGBA_UNORM,
    RFX_FORMAT_BC3_RGBA_SRGB,
    RFX_FORMAT_BC4_R_UNORM,
    RFX_FORMAT_BC4_R_SNORM,
    RFX_FORMAT_BC5_RG_UNORM,
    RFX_FORMAT_BC5_RG_SNORM,
    RFX_FORMAT_BC6H_RGB_UFLOAT,
    RFX_FORMAT_BC6H_RGB_SFLOAT,
    RFX_FORMAT_BC7_RGBA_UNORM,
    RFX_FORMAT_BC7_RGBA_SRGB,
    // Depth/Stencil
    RFX_FORMAT_D16_UNORM,
    RFX_FORMAT_D24_UNORM_S8_UINT,
    RFX_FORMAT_D32_FLOAT,
    RFX_FORMAT_D32_FLOAT_S8_UINT_X24,
    // Floats
    RFX_FORMAT_R32_FLOAT,
    RFX_FORMAT_RG32_FLOAT,
    RFX_FORMAT_RGB32_FLOAT,
    RFX_FORMAT_RGBA32_FLOAT,
    RFX_FORMAT_RGBA16_FLOAT,
} RfxFormat;

typedef enum {
    RFX_BLEND_FACTOR_ZERO,
    RFX_BLEND_FACTOR_ONE,
    RFX_BLEND_FACTOR_SRC_COLOR,
    RFX_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    RFX_BLEND_FACTOR_DST_COLOR,
    RFX_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    RFX_BLEND_FACTOR_SRC_ALPHA,
    RFX_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    RFX_BLEND_FACTOR_DST_ALPHA,
    RFX_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    RFX_BLEND_FACTOR_CONSTANT_COLOR,
    RFX_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    RFX_BLEND_FACTOR_CONSTANT_ALPHA,
    RFX_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
    RFX_BLEND_FACTOR_SRC_ALPHA_SATURATE,
    RFX_BLEND_FACTOR_SRC1_COLOR,
    RFX_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR,
    RFX_BLEND_FACTOR_SRC1_ALPHA,
    RFX_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA
} RfxBlendFactor;

typedef enum {
    RFX_BLEND_OP_ADD,
    RFX_BLEND_OP_SUBTRACT,
    RFX_BLEND_OP_REVERSE_SUBTRACT,
    RFX_BLEND_OP_MIN,
    RFX_BLEND_OP_MAX,
} RfxBlendOp;

typedef RFX_ENUM(uint8_t, RfxColorWriteMask){
    RFX_COLOR_WRITE_RED = RFX_BIT(0),   //
    RFX_COLOR_WRITE_GREEN = RFX_BIT(1), //
    RFX_COLOR_WRITE_BLUE = RFX_BIT(2),  //
    RFX_COLOR_WRITE_ALPHA = RFX_BIT(3), //
    RFX_COLOR_WRITE_ALL = 0xF,          //
};

typedef struct {
    bool blendEnabled;
    RfxBlendFactor srcColor;
    RfxBlendFactor dstColor;
    RfxBlendOp colorOp;
    RfxBlendFactor srcAlpha;
    RfxBlendFactor dstAlpha;
    RfxBlendOp alphaOp;
    RfxColorWriteMask writeMask;
} RfxBlendState;

typedef enum {
    RFX_QUERY_TYPE_TIMESTAMP,
    RFX_QUERY_TYPE_OCCLUSION,
} RfxQueryType;

typedef enum {
    RFX_CULL_NONE,
    RFX_CULL_BACK,
    RFX_CULL_FRONT,
} RfxCullMode;

typedef enum {
    RFX_DENOISER_REBLUR_DIFFUSE,
    RFX_DENOISER_REBLUR_DIFFUSE_SPECULAR,
    RFX_DENOISER_RELAX_DIFFUSE,
    RFX_DENOISER_RELAX_DIFFUSE_SPECULAR,
    RFX_DENOISER_SIGMA_SHADOW,
} RfxDenoiserType;

typedef enum {
    // Inputs
    RFX_DENOISER_IN_VIEWZ,            // R32F (Required)
    RFX_DENOISER_IN_MV,               // RGBA16F or RG16F (Required)
    RFX_DENOISER_IN_NORMAL_ROUGHNESS, // RGBA16F or R10G10B10A2 (Required)
    RFX_DENOISER_IN_DIFF_RADIANCE,    // Noisy Diffuse (Required for Diffuse/Specular modes)
    RFX_DENOISER_IN_SPEC_RADIANCE,    // Noisy Specular (Required for Diffuse/Specular modes)
    RFX_DENOISER_IN_SHADOW_DATA,      // RGBA16F (Penumbra/Translucency) for SIGMA

    // Outputs
    RFX_DENOISER_OUT_DIFF_RADIANCE, // Denoised Diffuse
    RFX_DENOISER_OUT_SPEC_RADIANCE, // Denoised Specular
    RFX_DENOISER_OUT_SHADOW,        // Denoised Shadow
    RFX_DENOISER_OUT_VALIDATION,    // Validation texture

    RFX_DENOISER_RESOURCE_COUNT,
} RfxDenoiserResourceId;

typedef struct {
    float viewToClip[16];
    float viewToClipPrev[16];
    float worldToView[16];
    float worldToViewPrev[16];

    float denoisingRange;        // Distance where denoising stops (e.g., 100.0)
    float viewZScale;            // Scale for ViewZ (usually 1.0)
    float disocclusionThreshold; // Sensitivity to motion (default 0.01)
    bool enableValidation;       // Enable validation texture

    float motionVectorScale[2]; // {1.0f, 1.0f} for UV space
    bool isMotionVectorInWorldSpace;
    float jitter[2];
    float jitterPrev[2];
    uint32_t frameIndex;
    bool resetHistory;
} RfxDenoiserSettings;

typedef struct {
    uint32_t location;        // Shader input location
    RfxFormat format;         // Format of the attribute
    uint32_t offset;          // Offset in the vertex buffer
    const char* semanticName; // HLSL semantic (e.g. "POSITION", "TEXCOORD")
} RfxVertexLayoutElement;

typedef struct {
    RfxFormat format;
    RfxBlendState blend;
} RfxAttachmentDesc;

typedef enum {
    RFX_STENCIL_OP_KEEP,
    RFX_STENCIL_OP_ZERO,
    RFX_STENCIL_OP_REPLACE,
    RFX_STENCIL_OP_INCREMENT_AND_CLAMP,
    RFX_STENCIL_OP_DECREMENT_AND_CLAMP,
    RFX_STENCIL_OP_INVERT,
    RFX_STENCIL_OP_INCREMENT_AND_WRAP,
    RFX_STENCIL_OP_DECREMENT_AND_WRAP
} RfxStencilOp;

typedef enum {
    RFX_COMPARE_NEVER,
    RFX_COMPARE_LESS,
    RFX_COMPARE_EQUAL,
    RFX_COMPARE_LESS_EQUAL,
    RFX_COMPARE_GREATER,
    RFX_COMPARE_NOT_EQUAL,
    RFX_COMPARE_GREATER_EQUAL,
    RFX_COMPARE_ALWAYS
} RfxCompareOp;

typedef enum {
    RFX_SHADING_RATE_1X1,
    RFX_SHADING_RATE_1X2,
    RFX_SHADING_RATE_2X1,
    RFX_SHADING_RATE_2X2,
    RFX_SHADING_RATE_2X4,
    RFX_SHADING_RATE_4X2,
    RFX_SHADING_RATE_4X4
} RfxShadingRate;

typedef enum {
    RFX_SHADING_RATE_COMBINER_PASSTHROUGH,
    RFX_SHADING_RATE_COMBINER_OVERRIDE,
    RFX_SHADING_RATE_COMBINER_MIN,
    RFX_SHADING_RATE_COMBINER_MAX,
    RFX_SHADING_RATE_COMBINER_SUM
} RfxShadingRateCombiner;

typedef struct {
    RfxCompareOp compareOp;
    RfxStencilOp failOp;
    RfxStencilOp passOp;
    RfxStencilOp depthFailOp;
} RfxStencilFace;

typedef struct {
    bool enabled;
    uint8_t readMask;
    uint8_t writeMask;
    RfxStencilFace front;
    RfxStencilFace back;
} RfxStencilState;

typedef RFX_ENUM(uint32_t, RfxUpscaleDispatchFlags){
    RFX_UPSCALE_DISPATCH_NONE = 0,
    RFX_UPSCALE_DISPATCH_RESET_HISTORY = RFX_BIT(0),
    RFX_UPSCALE_DISPATCH_USE_SPECULAR_MOTION = RFX_BIT(1),
};

typedef struct {
    RfxShader shader;
    RfxFormat colorFormat;                // Single render target format (used if attachments == NULL)
    RfxBlendState blendState;             // Single render target blend state (used if attachments == NULL)
    const RfxAttachmentDesc* attachments; // If != NULL and attachmentCount > 0, multi-target
    uint32_t attachmentCount;             // If > 0 and attachments != NULL, multi-target
    RfxFormat depthFormat;
    RfxTopology topology;
    uint32_t patchControlPoints; // Required if topology is RFX_TOPOLOGY_PATCH_LIST
    RfxCullMode cullMode;
    int sampleCount;
    bool depthTest;
    bool depthWrite;
    RfxCompareOp depthCompareOp;
    float depthBiasConstant;
    float depthBiasClamp;
    float depthBiasSlope;
    bool depthBoundsTest;
    RfxStencilState stencil;
    bool shadingRate;
    bool wireframe;
    uint32_t viewMask;
    // Vertex input
    const RfxVertexLayoutElement* vertexLayout;
    int vertexLayoutCount;
    int vertexStride;
    const char* vsEntryPoint;
    const char* psEntryPoint;
} RfxPipelineDesc;

typedef struct {
    RfxShader shader;
    const char* entryPoint; // Optional; if NULL, default to first entrypoint
} RfxComputePipelineDesc;

typedef struct {
    const char* name;
    float microseconds;
} RfxGpuTimestamp;

typedef enum {
    RFX_INDEX_UINT16, // 16-bit index buffer
    RFX_INDEX_UINT32, // 32-bit index buffer
} RfxIndexType;

typedef enum {
    RFX_AS_TOP_LEVEL,
    RFX_AS_BOTTOM_LEVEL,
} RfxAccelerationStructureType;

typedef RFX_ENUM(uint32_t, RfxBuildASFlags){
    RFX_BUILD_AS_NONE = 0,
    RFX_BUILD_AS_PREFER_FAST_TRACE = RFX_BIT(0),
    RFX_BUILD_AS_PREFER_FAST_BUILD = RFX_BIT(1),
    RFX_BUILD_AS_ALLOW_UPDATE = RFX_BIT(2),
    RFX_BUILD_AS_ALLOW_COMPACTION = RFX_BIT(3),
};

typedef RFX_ENUM(uint32_t, RfxInstanceFlags){
    RFX_INSTANCE_NONE = 0,
    RFX_INSTANCE_TRIANGLE_CULL_DISABLE = RFX_BIT(0),
    RFX_INSTANCE_TRIANGLE_FLIP_FACING = RFX_BIT(1),
    RFX_INSTANCE_FORCE_OPAQUE = RFX_BIT(2),
    RFX_INSTANCE_FORCE_NON_OPAQUE = RFX_BIT(3),
};

typedef RFX_ENUM(uint32_t, RfxRayTracingPipelineFlags){
    RFX_RT_PIPELINE_NONE = 0,
    RFX_RT_PIPELINE_SKIP_TRIANGLES = RFX_BIT(0), // Skip triangle geometry (e.g. only AABBs)
    RFX_RT_PIPELINE_SKIP_AABBS = RFX_BIT(1),     // Skip AABB geometry
    RFX_RT_PIPELINE_ALLOW_MICROMAPS = RFX_BIT(2) // Required if any AS uses micromaps
};

typedef struct {
    RfxBuffer vertexBuffer;
    uint64_t vertexOffset;
    uint32_t vertexCount;
    uint32_t vertexStride;
    RfxFormat vertexFormat;
    RfxBuffer indexBuffer;
    uint64_t indexOffset;
    uint32_t indexCount;
    RfxIndexType indexType;
    RfxBuffer transformBuffer;
    uint64_t transformOffset;

    // Micromap state
    RfxMicromap micromap;
    RfxBuffer micromapIndexBuffer;
    uint64_t micromapIndexOffset;
    RfxIndexType micromapIndexType;
    uint32_t micromapBaseTriangle;
} RfxGeometryTriangles;

typedef struct {
    RfxBuffer aabbBuffer;
    uint64_t offset;
    uint32_t count;
    uint32_t stride;
} RfxGeometryAABBs;

typedef struct {
    bool isAABB; // false = Triangles, true = AABBs
    bool opaque;
    union {
        RfxGeometryTriangles triangles;
        RfxGeometryAABBs aabbs;
    };
} RfxGeometryDesc;

typedef struct {
    RfxAccelerationStructureType type;
    RfxBuildASFlags flags;
    uint32_t count;                    // For BLAS: number of geometries; for TLAS: max instance count
    const RfxGeometryDesc* geometries; // BLAS only
} RfxAccelerationStructureDesc;

// Single TLAS instance
typedef struct {
    float transform[3][4]; // Row-major 3x4 matrix
    uint32_t instanceId : 24;
    uint32_t mask : 8;
    uint32_t instanceContributionToHitGroupIndex : 24;
    RfxInstanceFlags flags : 8;
    RfxAccelerationStructure blas;
} RfxInstance;

typedef enum {
    RFX_SHADER_GROUP_GENERAL,   // RayGen, Miss, Callable
    RFX_SHADER_GROUP_TRIANGLES, // ClosestHit + AnyHit
    RFX_SHADER_GROUP_PROCEDURAL // Intersection + ClosestHit + AnyHit
} RfxShaderGroupType;

typedef struct {
    RfxShaderGroupType type;
    const char* generalShader;      // RayGen, Miss, Callable entrypoint name
    const char* closestHitShader;   // HitGroup entrypoint name
    const char* anyHitShader;       // HitGroup entrypoint name
    const char* intersectionShader; // HitGroup entrypoint name
} RfxShaderGroup;

typedef struct {
    RfxShader shader;
    const RfxShaderGroup* groups;
    uint32_t groupCount;
    uint32_t maxRecursionDepth;
    uint32_t maxPayloadSize;
    uint32_t maxAttributeSize;
    RfxRayTracingPipelineFlags flags;
} RfxRayTracingPipelineDesc;

typedef struct {
    RfxShaderBindingTable sbt;
    uint32_t rayGenIndex;
    uint32_t missIndex;
    uint32_t missCount;
    uint32_t hitIndex;
    uint32_t hitCount;
    uint32_t callableIndex;
    uint32_t callableCount;
} RfxTraceRaysDesc;

typedef RFX_ENUM(uint8_t, RfxTextureUsageFlags){
    RFX_TEXTURE_USAGE_SHADER_RESOURCE = RFX_BIT(0), // Texture sampled in shader
    RFX_TEXTURE_USAGE_RENDER_TARGET = RFX_BIT(1),   // Color attachment
    RFX_TEXTURE_USAGE_DEPTH_STENCIL = RFX_BIT(2),   // Depth buffer
    RFX_TEXTURE_USAGE_STORAGE = RFX_BIT(3),         // UAV / Compute write
};

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;       // 1 for 2D
    uint32_t mipLevels;   // 0 or 1 = default
    uint32_t arrayLayers; // 0 or 1 = default
    RfxFormat format;
    int sampleCount;
    RfxTextureUsageFlags usage;
    const void* initialData; // Initial data for mip 0, layer 0, (slice 0 if 3D)
} RfxTextureDesc;

typedef enum {
    RFX_MICROMAP_FORMAT_OPACITY_2_STATE = 1, // 1 bit per micro-triangle
    RFX_MICROMAP_FORMAT_OPACITY_4_STATE = 2, // 2 bits per micro-triangle
} RfxMicromapFormat;

typedef RFX_ENUM(uint32_t, RfxBuildMicromapFlags){
    RFX_BUILD_MICROMAP_NONE = 0,
    RFX_BUILD_MICROMAP_PREFER_FAST_TRACE = RFX_BIT(0),
    RFX_BUILD_MICROMAP_PREFER_FAST_BUILD = RFX_BIT(1),
    RFX_BUILD_MICROMAP_ALLOW_COMPACTION = RFX_BIT(2),
};

typedef struct {
    uint32_t count; // Number of triangles using this format/level
    uint16_t subdivisionLevel;
    RfxMicromapFormat format;
} RfxMicromapUsage;

typedef struct {
    const RfxMicromapUsage* usages;
    uint32_t usageCount;
    RfxBuildMicromapFlags flags;
} RfxMicromapDesc;

typedef struct {
    RfxMicromap dst;
    RfxBuffer data;
    uint64_t dataOffset;
    RfxBuffer triangleIndices; // RfxMicromapTriangle[]
    uint64_t triangleIndicesOffset;
    RfxBuffer scratch;
    uint64_t scratchOffset;
} RfxBuildMicromapDesc;

typedef enum {
    RFX_UPSCALER_NIS,  // NVIDIA Image Scaling
    RFX_UPSCALER_FSR,  // AMD FSR
    RFX_UPSCALER_XESS, // Intel XeSS
    RFX_UPSCALER_DLSR, // NVIDIA DLSS
    RFX_UPSCALER_DLRR  // NVIDIA Ray Reconstruction
} RfxUpscalerType;

typedef enum {
    RFX_UPSCALER_MODE_NATIVE,            // 1.0x
    RFX_UPSCALER_MODE_ULTRA_QUALITY,     // 1.3x
    RFX_UPSCALER_MODE_QUALITY,           // 1.5x
    RFX_UPSCALER_MODE_BALANCED,          // 1.7x
    RFX_UPSCALER_MODE_PERFORMANCE,       // 2.0x
    RFX_UPSCALER_MODE_ULTRA_PERFORMANCE, // 3.0x
} RfxUpscalerMode;

typedef RFX_ENUM(uint32_t, RfxUpscalerFlags){
    RFX_UPSCALER_NONE = 0,
    RFX_UPSCALER_HDR = RFX_BIT(0),
    RFX_UPSCALER_SRGB = RFX_BIT(1),
    RFX_UPSCALER_DEPTH_INVERTED = RFX_BIT(2), // Near=1, Far=0
    RFX_UPSCALER_DEPTH_INFINITE = RFX_BIT(3),
    RFX_UPSCALER_DEPTH_LINEAR = RFX_BIT(4), // Linear ViewZ
    RFX_UPSCALER_MV_UPSCALED = RFX_BIT(5),  // MVs are at output resolution
    RFX_UPSCALER_MV_JITTERED = RFX_BIT(6),  // MVs include jitter
};

typedef struct {
    RfxUpscalerType type;
    RfxUpscalerMode mode;
    RfxUpscalerFlags flags;
    uint32_t outputWidth;
    uint32_t outputHeight;
    uint8_t preset; // 0 = default
} RfxUpscalerDesc;

typedef struct {
    float scalingFactor;
    float mipBias;
    uint32_t renderWidth;
    uint32_t renderHeight;
    uint32_t outputWidth;
    uint32_t outputHeight;
    uint8_t jitterPhaseCount;
} RfxUpscalerProps;

typedef struct {
    // Resources
    RfxTexture input;  // SRV (required)
    RfxTexture output; // UAV (required)

    // Common guides
    RfxTexture depth;         // SRV
    RfxTexture motionVectors; // SRV
    RfxTexture exposure;      // SRV (optional)
    RfxTexture reactive;      // SRV (optional)

    // DLRR guides (ignored for others)
    RfxTexture normalRoughness;
    RfxTexture diffuseAlbedo;
    RfxTexture specularAlbedo;
    RfxTexture specularMvOrHitT;
    RfxTexture sss; // Subsurface scattering (optional)

    // Common settings
    float sharpness; // [0..1]
    float jitter[2];
    float motionVectorScale[2];
    RfxUpscaleDispatchFlags dispatchFlags;

    // FSR/DLRR settings
    float zNear;
    float zFar;
    float verticalFov;             // Radians
    float viewSpaceToMetersFactor; // FSR only (defaults to 1.0)

    // Row-major 4x4 matrices {Xx, Yx, Zx, 0, ... Tx, Ty, Tz, 1}
    float viewToClip[16];  // Required for FSR, DLRR
    float worldToView[16]; // Required for DLRR
} RfxUpscaleDesc;

typedef RFX_ENUM(uint8_t, RfxBackend){
    RFX_BACKEND_DEFAULT, // Figure it out
    RFX_BACKEND_VULKAN,  //
    RFX_BACKEND_D3D12,   //
    RFX_BACKEND_D3D11,   // Not that it works
    RFX_BACKEND_NONE,    // Dummy backend, supports everything
};

typedef enum {
    RFX_QUEUE_GRAPHICS,
    RFX_QUEUE_COMPUTE,
    RFX_QUEUE_COPY,
} RfxQueueType;

typedef enum {
    RFX_RESOLVE_OP_AVERAGE,
    RFX_RESOLVE_OP_MIN,
    RFX_RESOLVE_OP_MAX,
} RfxResolveOp;

typedef enum {
    RFX_COPY_MODE_CLONE,
    RFX_COPY_MODE_COMPACT,
} RfxCopyMode;

typedef struct {
    int8_t x, y; // [-8; 7]
} RfxSampleLocation;

typedef enum {
    RFX_LATENCY_MARKER_SIMULATION_START,
    RFX_LATENCY_MARKER_SIMULATION_END,
    RFX_LATENCY_MARKER_RENDER_SUBMIT_START,
    RFX_LATENCY_MARKER_RENDER_SUBMIT_END,
    RFX_LATENCY_MARKER_INPUT_SAMPLE,
} RfxLatencyMarker;

typedef struct {
    uint64_t inputSampleTimeUs;
    uint64_t simulationStartTimeUs;
    uint64_t simulationEndTimeUs;
    uint64_t renderSubmitStartTimeUs;
    uint64_t renderSubmitEndTimeUs;
    uint64_t presentStartTimeUs;
    uint64_t presentEndTimeUs;
    uint64_t driverStartTimeUs;
    uint64_t driverEndTimeUs;
    uint64_t osRenderQueueStartTimeUs;
    uint64_t osRenderQueueEndTimeUs;
    uint64_t gpuRenderStartTimeUs;
    uint64_t gpuRenderEndTimeUs;
} RfxLatencyReport;

typedef RFX_ENUM(uint8_t, RfxMouseButton){
    RFX_MOUSE_BUTTON_LEFT = 0,
    RFX_MOUSE_BUTTON_RIGHT = 1,
    RFX_MOUSE_BUTTON_MIDDLE = 2,
};

typedef RFX_ENUM(uint32_t, RfxWindowFlags){
    RFX_WINDOW_VSYNC = RFX_BIT(0),            // Enable Vertical Sync
    RFX_WINDOW_FULLSCREEN = RFX_BIT(1),       // Exclusive Fullscreen mode
    RFX_WINDOW_BORDERLESS = RFX_BIT(2),       // Borderless Windowed mode
    RFX_WINDOW_ALWAYS_ACTIVE = RFX_BIT(3),    // Continue rendering when window is not focused
    RFX_WINDOW_NO_RESIZE = RFX_BIT(4),        // Disable window resizing by user
    RFX_WINDOW_TRANSPARENT = RFX_BIT(5),      // Transparent framebuffer
    RFX_WINDOW_FLOATING = RFX_BIT(6),         // Floating / Always on top
    RFX_WINDOW_MAXIMIZED = RFX_BIT(7),        // Start maximized
    RFX_WINDOW_HIDDEN = RFX_BIT(8),           // Start hidden
    RFX_WINDOW_CENTERED = RFX_BIT(9),         // Center window on screen
    RFX_WINDOW_SCALE_TO_MONITOR = RFX_BIT(10) // Scale content to monitor DPI/Scale
};

typedef RFX_ENUM(uint32_t, RfxFeatureSupportFlags){
    RFX_FEATURE_MESH_SHADER = RFX_BIT(0),
    RFX_FEATURE_RAY_TRACING = RFX_BIT(1),
    RFX_FEATURE_UPSCALE = RFX_BIT(2),
    RFX_FEATURE_LOW_LATENCY = RFX_BIT(3),
};

typedef enum {
    RFX_CURSOR_DEFAULT,
    RFX_CURSOR_ARROW,
    RFX_CURSOR_IBEAM,
    RFX_CURSOR_CROSSHAIR,
    RFX_CURSOR_HAND,
    RFX_CURSOR_RESIZE_EW,
    RFX_CURSOR_RESIZE_H = RFX_CURSOR_RESIZE_EW,
    RFX_CURSOR_RESIZE_NS,
    RFX_CURSOR_RESIZE_V = RFX_CURSOR_RESIZE_NS,
    RFX_CURSOR_RESIZE_NWSE,
    RFX_CURSOR_RESIZE_NESW,
    RFX_CURSOR_RESIZE_ALL,
    RFX_CURSOR_NOT_ALLOWED,
    RFX_CURSOR_RESIZE_NW,
    RFX_CURSOR_RESIZE_N,
    RFX_CURSOR_RESIZE_NE,
    RFX_CURSOR_RESIZE_E,
    RFX_CURSOR_RESIZE_SE,
    RFX_CURSOR_RESIZE_S,
    RFX_CURSOR_RESIZE_SW,
    RFX_CURSOR_RESIZE_W,
    RFX_CURSOR_WAIT,     // *Unsupported on GLFW backend
    RFX_CURSOR_PROGRESS, // *Unsupported on GLFW backend
    RFX_CURSOR_COUNT
} RfxCursorType;

typedef enum {
    RFX_KEY_SPACE = 32,
    RFX_KEY_APOSTROPHE = 39, /* ' */
    RFX_KEY_COMMA = 44,      /* , */
    RFX_KEY_MINUS = 45,      /* - */
    RFX_KEY_PERIOD = 46,     /* . */
    RFX_KEY_SLASH = 47,      /* / */
    RFX_KEY_0 = 48,
    RFX_KEY_1 = 49,
    RFX_KEY_2 = 50,
    RFX_KEY_3 = 51,
    RFX_KEY_4 = 52,
    RFX_KEY_5 = 53,
    RFX_KEY_6 = 54,
    RFX_KEY_7 = 55,
    RFX_KEY_8 = 56,
    RFX_KEY_9 = 57,
    RFX_KEY_SEMICOLON = 59, /* ; */
    RFX_KEY_EQUAL = 61,     /* = */
    RFX_KEY_A = 65,
    RFX_KEY_B = 66,
    RFX_KEY_C = 67,
    RFX_KEY_D = 68,
    RFX_KEY_E = 69,
    RFX_KEY_F = 70,
    RFX_KEY_G = 71,
    RFX_KEY_H = 72,
    RFX_KEY_I = 73,
    RFX_KEY_J = 74,
    RFX_KEY_K = 75,
    RFX_KEY_L = 76,
    RFX_KEY_M = 77,
    RFX_KEY_N = 78,
    RFX_KEY_O = 79,
    RFX_KEY_P = 80,
    RFX_KEY_Q = 81,
    RFX_KEY_R = 82,
    RFX_KEY_S = 83,
    RFX_KEY_T = 84,
    RFX_KEY_U = 85,
    RFX_KEY_V = 86,
    RFX_KEY_W = 87,
    RFX_KEY_X = 88,
    RFX_KEY_Y = 89,
    RFX_KEY_Z = 90,
    RFX_KEY_LEFT_BRACKET = 91,  /* [ */
    RFX_KEY_BACKSLASH = 92,     /* \ */
    RFX_KEY_RIGHT_BRACKET = 93, /* ] */
    RFX_KEY_GRAVE_ACCENT = 96,  /* ` */
    RFX_KEY_ESCAPE = 256,
    RFX_KEY_ENTER = 257,
    RFX_KEY_TAB = 258,
    RFX_KEY_BACKSPACE = 259,
    RFX_KEY_INSERT = 260,
    RFX_KEY_DELETE = 261,
    RFX_KEY_RIGHT = 262,
    RFX_KEY_LEFT = 263,
    RFX_KEY_DOWN = 264,
    RFX_KEY_UP = 265,
    RFX_KEY_PAGE_UP = 266,
    RFX_KEY_PAGE_DOWN = 267,
    RFX_KEY_HOME = 268,
    RFX_KEY_END = 269,
    RFX_KEY_CAPS_LOCK = 280,
    RFX_KEY_SCROLL_LOCK = 281,
    RFX_KEY_NUM_LOCK = 282,
    RFX_KEY_PRINT_SCREEN = 283,
    RFX_KEY_PAUSE = 284,
    RFX_KEY_F1 = 290,
    RFX_KEY_F2 = 291,
    RFX_KEY_F3 = 292,
    RFX_KEY_F4 = 293,
    RFX_KEY_F5 = 294,
    RFX_KEY_F6 = 295,
    RFX_KEY_F7 = 296,
    RFX_KEY_F8 = 297,
    RFX_KEY_F9 = 298,
    RFX_KEY_F10 = 299,
    RFX_KEY_F11 = 300,
    RFX_KEY_F12 = 301,
    RFX_KEY_LEFT_SHIFT = 340,
    RFX_KEY_LEFT_CONTROL = 341,
    RFX_KEY_LEFT_ALT = 342,
    RFX_KEY_LEFT_SUPER = 343,
    RFX_KEY_RIGHT_SHIFT = 344,
    RFX_KEY_RIGHT_CONTROL = 345,
    RFX_KEY_RIGHT_ALT = 346,
    RFX_KEY_RIGHT_SUPER = 347,
    RFX_KEY_MENU = 348
} RfxKey;

//
// Window
//

RAFX_API void rfxRequestBackend(RfxBackend backend, bool enableValidation); // Do this *before* opening the window
RAFX_API bool rfxOpenWindow(const char* title, int width, int height);
RAFX_API bool rfxSupportsFeatures(RfxFeatureSupportFlags features);
RAFX_API RfxFeatureSupportFlags rfxGetSupportedFeatures();
RAFX_API void rfxSetSampleCount(int count);
RAFX_API void rfxSetAnisotropy(int level);
RAFX_API void rfxSetWindowFlags(RfxWindowFlags flags);
RAFX_API void rfxEnableWindowFlags(RfxWindowFlags flags);
RAFX_API void rfxDisableWindowFlags(RfxWindowFlags flags);
RAFX_API void rfxToggleWindowFlags(RfxWindowFlags flags);
RAFX_API bool rfxHasWindowFlags(RfxWindowFlags flags);
RAFX_API bool rfxWindowShouldClose();
RAFX_API void rfxPollInputEvents();
RAFX_API void rfxGetWindowSize(int* width, int* height);
RAFX_API int rfxGetWindowWidth();
RAFX_API int rfxGetWindowHeight();
RAFX_API double rfxGetTime();
RAFX_API float rfxGetDeltaTime();
RAFX_API uint32_t rfxGetFrameIndex();

//
// Input
//

RAFX_API bool rfxIsKeyDown(RfxKey key);
RAFX_API bool rfxIsKeyPressed(RfxKey key);
RAFX_API bool rfxIsKeyReleased(RfxKey key);
RAFX_API bool rfxIsMouseButtonDown(RfxMouseButton button);
RAFX_API bool rfxIsMouseButtonPressed(RfxMouseButton button);
RAFX_API bool rfxIsMouseButtonReleased(RfxMouseButton button);
RAFX_API void rfxGetMousePos(float* x, float* y);
RAFX_API void rfxGetMouseDelta(float* x, float* y);
RAFX_API void rfxSetMouseCursorVisible(bool visible);
RAFX_API void rfxSetMouseCursor(RfxCursorType cursor);

//
// Resources
//

// Buffers
RAFX_API RfxBuffer rfxCreateBuffer(size_t size, size_t stride, RfxBufferUsageFlags usage, RfxMemoryType memType, const void* initialData);
RAFX_API void rfxDestroyBuffer(RfxBuffer buffer);
RAFX_API void* rfxMapBuffer(RfxBuffer buffer);
RAFX_API void rfxUnmapBuffer(RfxBuffer buffer);
RAFX_API uint32_t rfxGetBufferId(RfxBuffer buffer);

// Textures
RAFX_API RfxTexture
rfxCreateTexture(int width, int height, RfxFormat format, int sampleCount, RfxTextureUsageFlags usage, const void* initialData);
RAFX_API RfxTexture rfxCreateTextureEx(const RfxTextureDesc* desc);
// Create a view (alias) of a texture for specific mips/layers.
// The returned texture must be destroyed with rfxDestroyTexture (it won't free the underlying memory).
RAFX_API RfxTexture
rfxCreateTextureView(RfxTexture original, RfxFormat format, uint32_t mip, uint32_t mipCount, uint32_t layer, uint32_t layerCount);
RAFX_API void rfxDestroyTexture(RfxTexture texture);
RAFX_API uint32_t rfxGetTextureId(RfxTexture texture);
RAFX_API void* rfxGetTextureDescriptor(RfxTexture texture);
RAFX_API RfxFormat rfxGetSwapChainFormat();
RAFX_API RfxTexture rfxGetBackbufferTexture();

// Samplers
RAFX_API RfxSampler rfxCreateSampler(RfxFilter filter, RfxAddressMode addressMode);
RAFX_API void rfxDestroySampler(RfxSampler sampler);

// Shaders
RAFX_API RfxShader
rfxCompileShader(const char* filepath, const char** defines, int numDefines, const char** includeDirs, int numIncludeDirs);
RAFX_API RfxShader
rfxCompileShaderMem(const char* source, const char** defines, int numDefines, const char** includeDirs, int numIncludeDirs);
RAFX_API void rfxDestroyShader(RfxShader shader);

// Pipelines
RAFX_API RfxPipeline rfxCreatePipeline(const RfxPipelineDesc* desc);
RAFX_API void rfxDestroyPipeline(RfxPipeline pipeline);
RAFX_API RfxPipeline rfxCreateComputePipeline(const RfxComputePipelineDesc* desc);

// Debug resource naming
RAFX_API void rfxSetBufferName(RfxBuffer buffer, const char* name);
RAFX_API void rfxSetTextureName(RfxTexture texture, const char* name);
RAFX_API void rfxSetPipelineName(RfxPipeline pipeline, const char* name);

//
// Command list
//

RAFX_API RfxCommandList rfxGetCommandList(); // Main command list for current frame
RAFX_API RfxCommandList rfxCreateCommandList(RfxQueueType queueType);
RAFX_API void rfxDestroyCommandList(RfxCommandList cmd);
RAFX_API void rfxBeginCommandList(RfxCommandList cmd);
RAFX_API void rfxEndCommandList(RfxCommandList cmd);

RAFX_API void rfxBeginFrame();
RAFX_API void rfxEndFrame();

RAFX_API RfxFence rfxCreateFence(uint64_t initialValue);
RAFX_API void rfxDestroyFence(RfxFence fence);
RAFX_API void rfxWaitFence(RfxFence fence, uint64_t value); // CPU wait
RAFX_API uint64_t rfxGetFenceValue(RfxFence fence);

RAFX_API void rfxSubmitCommandListAsync(
    RfxCommandList cmd, RfxFence* waitFences, uint64_t* waitValues, uint32_t waitCount, RfxFence* signalFences, uint64_t* signalValues,
    uint32_t signalCount
);

//
// Render pass
//

RAFX_API void rfxCmdBeginSwapchainRenderPass(RfxCommandList cmd, RfxFormat depthStencilFormat, RfxColor clearColor);
RAFX_API void rfxCmdBeginRenderPass(
    RfxCommandList cmd, RfxTexture* colors, uint32_t colorCount, RfxTexture depth, RfxColor clearColor, uint32_t viewMask
);
RAFX_API void rfxCmdEndRenderPass(RfxCommandList cmd);

// Clear currently bound render targets (must be called inside a render pass)
RAFX_API void rfxCmdClear(RfxCommandList cmd, RfxColor color);

RAFX_API void rfxCmdBindPipeline(RfxCommandList cmd, RfxPipeline pipeline);
RAFX_API void rfxCmdSetViewports(RfxCommandList cmd, float* viewports, uint32_t count);
RAFX_API void rfxCmdSetScissor(RfxCommandList cmd, int x, int y, int width, int height);
RAFX_API void rfxCmdSetBlendConstants(RfxCommandList cmd, RfxColor color);
RAFX_API void rfxCmdSetStencilReference(RfxCommandList cmd, uint8_t frontRef, uint8_t backRef);
RAFX_API void rfxCmdSetDepthBounds(RfxCommandList cmd, float minBound, float maxBound);
RAFX_API void rfxCmdSetDepthBias(RfxCommandList cmd, float constant, float clamp, float slope);
RAFX_API void rfxCmdSetShadingRate(
    RfxCommandList cmd, RfxShadingRate rate, RfxShadingRateCombiner primitiveCombiner, RfxShadingRateCombiner attachmentCombiner
);
RAFX_API void
rfxCmdSetSampleLocations(RfxCommandList cmd, const RfxSampleLocation* locations, uint32_t locationCount, uint32_t sampleCount);

//
// Resource binding
//

RAFX_API void rfxCmdBindVertexBuffer(RfxCommandList cmd, RfxBuffer buffer);
RAFX_API void rfxCmdBindIndexBuffer(RfxCommandList cmd, RfxBuffer buffer, RfxIndexType indexType);
RAFX_API void rfxCmdPushConstants(RfxCommandList cmd, const void* data, size_t size);

//
// Drawing, Dispatch
//

RAFX_API void rfxCmdDraw(RfxCommandList cmd, uint32_t vertexCount, uint32_t instanceCount);
RAFX_API void rfxCmdDrawIndexed(RfxCommandList cmd, uint32_t indexCount, uint32_t instanceCount);
RAFX_API void rfxCmdDispatch(RfxCommandList cmd, uint32_t x, uint32_t y, uint32_t z);

// Indirect
RAFX_API void rfxCmdDrawIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset, uint32_t drawCount, uint32_t stride);
RAFX_API void rfxCmdDrawIndexedIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset, uint32_t drawCount, uint32_t stride);
RAFX_API void rfxCmdDispatchIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset);

// Mesh shaders
RAFX_API void rfxCmdDrawMeshTasks(RfxCommandList cmd, uint32_t x, uint32_t y, uint32_t z);
RAFX_API void rfxCmdDrawMeshTasksIndirect(RfxCommandList cmd, RfxBuffer buffer, size_t offset, uint32_t drawCount, uint32_t stride);

// Multidraw
RAFX_API void rfxCmdDrawIndirectCount(
    RfxCommandList cmd, RfxBuffer buffer, size_t offset, RfxBuffer countBuffer, size_t countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride
);
RAFX_API void rfxCmdDrawIndexedIndirectCount(
    RfxCommandList cmd, RfxBuffer buffer, size_t offset, RfxBuffer countBuffer, size_t countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride
);
RAFX_API void rfxCmdDrawMeshTasksIndirectCount(
    RfxCommandList cmd, RfxBuffer buffer, size_t offset, RfxBuffer countBuffer, size_t countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride
);

//
// Transfer, Copy, Blit
//

RAFX_API void rfxCmdCopyBuffer(RfxCommandList cmd, RfxBuffer src, size_t srcOffset, RfxBuffer dst, size_t dstOffset, size_t size);
RAFX_API void rfxCmdCopyTexture(RfxCommandList cmd, RfxTexture src, RfxTexture dst);
RAFX_API void rfxCmdUploadTexture(RfxCommandList cmd, RfxTexture dst, const void* data, uint32_t mip, uint32_t layer);
// Buffer must be `RFX_USAGE_TRANSFER_DST` and `RFX_MEM_GPU_TO_CPU`
RAFX_API void rfxCmdReadbackTextureToBuffer(RfxCommandList cmd, RfxTexture src, RfxBuffer dst, uint64_t dstOffset);

RAFX_API void rfxCmdZeroBuffer(RfxCommandList cmd, RfxBuffer buffer, size_t offset, size_t size);
RAFX_API void rfxCmdClearStorageBuffer(RfxCommandList cmd, RfxBuffer buffer, uint32_t value);
RAFX_API void rfxCmdClearStorageTexture(RfxCommandList cmd, RfxTexture texture, RfxColor value);
RAFX_API void rfxCmdResolveTexture(RfxCommandList cmd, RfxTexture dst, RfxTexture src, RfxResolveOp op);

// AS compaction
RAFX_API void rfxCmdCopyMicromap(RfxCommandList cmd, RfxMicromap dst, RfxMicromap src, RfxCopyMode mode);
RAFX_API void
rfxCmdCopyAccelerationStructure(RfxCommandList cmd, RfxAccelerationStructure dst, RfxAccelerationStructure src, RfxCopyMode mode);

//
// Explicit barriers
//

RAFX_API void rfxCmdTransitionBuffer(RfxCommandList cmd, RfxBuffer buffer, RfxResourceState state);
RAFX_API void rfxCmdTransitionTexture(RfxCommandList cmd, RfxTexture texture, RfxResourceState state);

//
// Profiling, Occlusion Queries
//

// GPU Profiler, CPU markers
RAFX_API void rfxBeginMarker(const char* name);
RAFX_API void rfxEndMarker();
RAFX_API void rfxMarker(const char* name);
RAFX_API void rfxCmdBeginEvent(RfxCommandList cmd, const char* name);
RAFX_API void rfxCmdEndEvent(RfxCommandList cmd);
RAFX_API void rfxCmdMarker(RfxCommandList cmd, const char* name);

// Timestamps
RAFX_API void rfxCmdBeginProfile(RfxCommandList cmd, const char* name);
RAFX_API void rfxCmdEndProfile(RfxCommandList cmd);
RAFX_API uint32_t rfxGetGpuTimestamps(RfxGpuTimestamp* outTimestamps, uint32_t maxCount);

// Occlusion queries
RAFX_API RfxQueryPool rfxCreateQueryPool(RfxQueryType type, uint32_t capacity);
RAFX_API void rfxDestroyQueryPool(RfxQueryPool pool);
RAFX_API void rfxCmdResetQueries(RfxCommandList cmd, RfxQueryPool pool, uint32_t offset, uint32_t count);
RAFX_API void rfxCmdBeginQuery(RfxCommandList cmd, RfxQueryPool pool, uint32_t queryIndex);
RAFX_API void rfxCmdEndQuery(RfxCommandList cmd, RfxQueryPool pool, uint32_t queryIndex);
RAFX_API void
rfxCmdCopyQueries(RfxCommandList cmd, RfxQueryPool pool, uint32_t offset, uint32_t count, RfxBuffer dstBuffer, uint64_t dstOffset);

// Low latency (aka Reflex)
RAFX_API void rfxSetLowLatencyMode(bool enabled, bool boost);
RAFX_API void rfxLatencySleep();
RAFX_API void rfxSetLatencyMarker(RfxLatencyMarker marker);
RAFX_API bool rfxGetLatencyReport(RfxLatencyReport* outReport);

//
// ImGui Extension
//

typedef struct {
    void* const* drawLists;
    uint32_t drawListCount;
    void* const* textures;
    uint32_t textureCount;
    float displayWidth;
    float displayHeight;
    float hdrScale;
    bool linearColor;
} RfxImGuiDrawData;

RAFX_API bool rfxInitImGui();
RAFX_API void rfxShutdownImGui();
RAFX_API void rfxCmdDrawImGui(RfxCommandList cmd, const RfxImGuiDrawData* data);

//
// Denoiser (NRD) Extension
//

RAFX_API RfxDenoiser rfxCreateDenoiser(RfxDenoiserType type, int width, int height);
RAFX_API void rfxDestroyDenoiser(RfxDenoiser denoiser);
// `resources` must be an array of size `RFX_DENOISER_RESOURCE_COUNT`, sparsely populated with NULL where unused.
// `resourceCount` should be `RFX_DENOISER_RESOURCE_COUNT`.
RAFX_API void
rfxCmdDenoise(RfxCommandList cmd, RfxDenoiser denoiser, const RfxDenoiserSettings* settings, RfxTexture* resources, uint32_t resourceCount);

//
// Raytracing extension
//

RAFX_API RfxAccelerationStructure rfxCreateAccelerationStructure(const RfxAccelerationStructureDesc* desc);
RAFX_API void rfxDestroyAccelerationStructure(RfxAccelerationStructure as);
RAFX_API uint32_t rfxGetAccelerationStructureId(RfxAccelerationStructure as);
RAFX_API uint64_t rfxGetAccelerationStructureScratchSize(RfxAccelerationStructure as);
RAFX_API void rfxCmdWriteAccelerationStructureSize(
    RfxCommandList cmd, RfxAccelerationStructure* asArray, uint32_t count, RfxQueryPool pool, uint32_t queryOffset
);

RAFX_API RfxPipeline rfxCreateRayTracingPipeline(const RfxRayTracingPipelineDesc* desc);
RAFX_API RfxShaderBindingTable rfxCreateShaderBindingTable(RfxPipeline pipeline);
RAFX_API void rfxDestroyShaderBindingTable(RfxShaderBindingTable sbt);

// Builds BLAS or TLAS. For TLAS, `instanceBuffer` must be populated via `rfxCmdUploadInstances`
RAFX_API void
rfxCmdBuildAccelerationStructure(RfxCommandList cmd, RfxAccelerationStructure dst, RfxBuffer scratch, RfxBuffer instanceBuffer);
RAFX_API void rfxCmdUploadInstances(RfxCommandList cmd, RfxBuffer dstBuffer, const RfxInstance* instances, uint32_t instanceCount);
RAFX_API void rfxCmdTraceRays(RfxCommandList cmd, const RfxTraceRaysDesc* desc, uint32_t width, uint32_t height, uint32_t depth);
RAFX_API void rfxCmdDispatchRaysIndirect(RfxCommandList cmd, RfxBuffer argsBuffer, uint64_t argsOffset);

RAFX_API RfxMicromap rfxCreateMicromap(const RfxMicromapDesc* desc);
RAFX_API void rfxDestroyMicromap(RfxMicromap micromap);
RAFX_API uint64_t rfxGetMicromapScratchSize(RfxMicromap micromap);
RAFX_API void rfxCmdBuildMicromaps(RfxCommandList cmd, const RfxBuildMicromapDesc* desc);

//
// Upscaling extension
//

RAFX_API bool rfxIsUpscalerSupported(RfxUpscalerType type);
RAFX_API RfxUpscaler rfxCreateUpscaler(const RfxUpscalerDesc* desc);
RAFX_API void rfxDestroyUpscaler(RfxUpscaler upscaler);
RAFX_API void rfxGetUpscalerProps(RfxUpscaler upscaler, RfxUpscalerProps* outProps);
RAFX_API void rfxCmdUpscale(RfxCommandList cmd, RfxUpscaler upscaler, const RfxUpscaleDesc* desc);

#ifdef __cplusplus
} // extern "C"
#endif

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <https://unlicense.org/>
*/

#endif // RAFX_H
