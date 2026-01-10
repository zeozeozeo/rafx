#ifndef RAFX_INTERNAL_H
#define RAFX_INTERNAL_H

#include "rafx.h"

#include <NRI.h>
#include <Extensions/NRISwapChain.h>
#include <Extensions/NRIDeviceCreation.h>
#include <Extensions/NRIHelper.h>
#include <Extensions/NRIStreamer.h>
#include <Extensions/NRILowLatency.h>
#include <Extensions/NRIImgui.h>
#include <Extensions/NRIMeshShader.h>
#include <Extensions/NRIRayTracing.h>
#include <Extensions/NRIUpscaler.h>

#include <slang.h>
#include <slang-com-ptr.h>

#if defined(__clang__)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wmissing-field-initializers"
#elif defined(__GNUC__) || defined(__GNUG__)
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

#include "watcher.hpp"

#if defined(__clang__)
#    pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#    pragma GCC diagnostic pop
#endif

#include <functional>
#include <vector>
#include <string>
#include <set>
#include <variant>

// platform definitions
#define RAFX_WINDOWS 0
#define RAFX_X11 1
#define RAFX_WAYLAND 2
#define RAFX_COCOA 3

// default to rgfw backend
#if !defined(RAFX_BACKEND_GLFW) && !defined(RAFX_BACKEND_RGFW)
#    define RAFX_BACKEND_RGFW
#endif

#if defined(_WIN32)
#    define RAFX_PLATFORM RAFX_WINDOWS
#    define VK_USE_PLATFORM_WIN32_KHR
#    ifdef RAFX_BACKEND_GLFW
#        define GLFW_EXPOSE_NATIVE_WIN32
#    endif
#elif defined(__APPLE__)
#    define RAFX_PLATFORM RAFX_COCOA
#    define VK_USE_PLATFORM_METAL_EXT
#    ifdef RAFX_BACKEND_GLFW
#        define GLFW_EXPOSE_NATIVE_COCOA
#    endif
#elif (defined(__linux__) && RAFX_USE_WAYLAND)
#    define RAFX_PLATFORM RAFX_WAYLAND
#    define VK_USE_PLATFORM_WAYLAND_KHR
#    ifdef RAFX_BACKEND_GLFW
#        define GLFW_EXPOSE_NATIVE_WAYLAND
#    endif
#elif (defined(__linux__))
#    define RAFX_PLATFORM RAFX_X11
#    define VK_USE_PLATFORM_XLIB_KHR
#    ifdef RAFX_BACKEND_GLFW
#        define GLFW_EXPOSE_NATIVE_X11
#    endif
#else
#    error "Unknown platform"
#endif

#ifdef __FILE_NAME__
#    define RFX_CURRENT_FILE __FILE_NAME__
#else
#    define RFX_CURRENT_FILE __FILE__
#endif

#define RFX_ASSERT(cond)                                                                                                                   \
    ((void)((cond) || (fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, RFX_CURRENT_FILE, __LINE__), abort(), 0)))
#define RFX_ASSERTF(cond, fmt, ...)                                                                                                        \
    ((void)((cond) || (fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, RFX_CURRENT_FILE, __LINE__),                              \
                       fprintf(stderr, fmt, ##__VA_ARGS__), fprintf(stderr, "\n"), abort(), 0)))
#define NRI_CHECK(result) RFX_ASSERT((result) == nri::Result::SUCCESS);

//
// [Internal] Backend
//

bool Backend_CreateWindow(const char* title, int width, int height);
void Backend_DestroyWindow();
void Backend_SetWindowFlags(RfxWindowFlags flags);
bool Backend_WindowShouldClose();
void Backend_PollEvents();
void Backend_GetWindowSize(int* width, int* height);
int Backend_GetWindowWidth();
int Backend_GetWindowHeight();
float Backend_GetWindowScale();
double Backend_GetTime();
void Backend_SetMouseCursorVisible(bool visible);
void Backend_SetMouseCursor(RfxCursorType cursor);
void Backend_GetNativeHandles(nri::Window& window);
void Backend_EventSleep();

void Input_PushKeyPressed(int key);
void Input_PushCharPressed(uint32_t codepoint);

//
// Allocator
//

extern RfxAllocator g_Allocator;
void* RfxAlloc(size_t size, size_t align = 16);
void* RfxRealloc(void* ptr, size_t size, size_t align = 16);
void RfxFree(void* ptr);

template <typename T, typename... Args>
T* RfxNew(Args&&... args) {
    void* ptr = RfxAlloc(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
}

template <typename T>
void RfxDelete(T* ptr) {
    if (ptr) {
        ptr->~T();
        RfxFree(ptr);
    }
}

template <typename T>
struct RfxStlAllocator {
    using value_type = T;

    RfxStlAllocator() noexcept = default;
    template <typename U>
    RfxStlAllocator(const RfxStlAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        return static_cast<T*>(RfxAlloc(n * sizeof(T), alignof(T)));
    }

    void deallocate(T* p, size_t) {
        RfxFree(p);
    }

    bool operator==(const RfxStlAllocator&) const {
        return true;
    }
    bool operator!=(const RfxStlAllocator&) const {
        return false;
    }
};

template <typename T>
using RfxVector = std::vector<T, RfxStlAllocator<T>>;
template <typename T>
using RfxSet = std::set<T, std::less<T>, RfxStlAllocator<T>>;

void* NRI_CALL InternalNriAlloc(void* userArg, size_t size, size_t alignment);
void* NRI_CALL InternalNriRealloc(void* userArg, void* memory, size_t size, size_t alignment);
void NRI_CALL InternalNriFree(void* userArg, void* memory);

//
// Resource impls
//

struct RfxTextureSharedState {
    RfxVector<RfxResourceState> subresourceStates; // size() = mipLevels * arrayLayers
    uint32_t totalMips;
    uint32_t totalLayers;
    int refCount = 1;

    void AddRef() {
        refCount++;
    }
    void Release() {
        refCount--;
        if (refCount == 0)
            RfxDelete(this);
    }

    // Get gets the state of specific subresource
    RfxResourceState Get(uint32_t mip, uint32_t layer) {
        return subresourceStates[layer * totalMips + mip];
    }

    // Set sets the state of specific subresource
    void Set(uint32_t mip, uint32_t layer, RfxResourceState state) {
        subresourceStates[layer * totalMips + mip] = state;
    }
};

struct RfxTextureImpl {
    nri::Texture* texture;
    nri::Memory* memory;

    nri::Descriptor* descriptor;
    nri::Descriptor* descriptorAttachment;
    nri::Descriptor* descriptorUAV;

    nri::Format format;
    uint32_t width;
    uint32_t height;
    uint32_t sampleCount;

    uint32_t mipOffset;
    uint32_t mipNum;
    uint32_t layerOffset;
    uint32_t layerNum;

    uint32_t bindlessIndex;
    bool isView = false;

    RfxTextureSharedState* state = nullptr;
};

struct RfxSamplerImpl {
    nri::Descriptor* descriptor;
};

struct RfxBufferImpl {
    nri::Buffer* buffer;
    nri::Memory* memory;
    nri::Descriptor* descriptorSRV; // tX
    nri::Descriptor* descriptorUAV; // uX
    uint64_t size;
    uint32_t stride;
    uint32_t bindlessIndex;

    RfxResourceState currentState = RFX_STATE_UNDEFINED;
    nri::AccessBits currentAccess = nri::AccessBits::NONE;
    nri::StageBits currentStage = nri::StageBits::NONE;
};

struct RfxShaderImpl {
    struct Stage {
        RfxVector<uint8_t> bytecode;
        nri::StageBits stageBits;
        std::string entryPoint;       // "main" for SPIR-V
        std::string sourceEntryPoint; // name in source code
    };
    RfxVector<Stage> stages;
    nri::PipelineLayout* pipelineLayout;
    uint32_t descriptorSetCount;
    nri::StageBits stageMask;
    uint32_t bindlessSetIndex;

    bool fromCache = false;
    struct BindingRange {
        uint32_t setIndex;
        uint32_t rangeIndex;
        uint32_t baseRegister;
        uint32_t count;
        nri::DescriptorType type;
    };
    RfxVector<BindingRange> bindings;
    RfxVector<nri::RootConstantDesc> rootConstants;
    RfxVector<nri::RootSamplerDesc> rootSamplers;

    std::string filepath;
    RfxVector<std::string> defines; // k,v,k,v,...
    RfxVector<std::string> includeDirs;
    std::unique_ptr<wtr::watch> watcher;
    RfxSet<struct RfxPipelineImpl*> dependentPipelines;
};

struct CachedGraphics {
    RfxPipelineDesc desc;
    RfxVector<RfxAttachmentDesc> attachmentStorage;
    RfxVector<RfxVertexLayoutElement> layoutStorage;
    std::string vsEntryStorage;
    std::string psEntryStorage;
};

struct CachedCompute {
    RfxComputePipelineDesc desc;
    std::string entryStorage;
};

struct CachedRT {
    RfxRayTracingPipelineDesc desc;
    RfxVector<RfxShaderGroup> groupStorage;
    RfxVector<std::string> nameStorage;
};

struct RfxPipelineImpl {
    nri::Pipeline* pipeline;
    RfxShaderImpl* shader;
    uint32_t vertexStride;
    nri::BindPoint bindPoint;
    uint32_t shaderGroupCount;
    enum Type { GRAPHICS, COMPUTE, RAY_TRACING } type;
    std::variant<CachedGraphics, CachedCompute, CachedRT> cache;
};

struct RfxQueryPoolImpl {
    nri::QueryPool* pool;
    RfxQueryType type;
};

struct RfxAccelerationStructureImpl {
    nri::AccelerationStructure* as;
    nri::Memory* memory;
    nri::Descriptor* descriptor;
    uint32_t bindlessIndex;

    nri::AccelerationStructureDesc nriDesc;
    RfxVector<nri::BottomLevelGeometryDesc> geometries;
    RfxVector<nri::BottomLevelMicromapDesc> micromapDescs;

    nri::AccessBits currentAccess = nri::AccessBits::NONE;
    nri::StageBits currentStage = nri::StageBits::NONE;
};

struct RfxShaderBindingTableImpl {
    nri::Buffer* buffer;
    nri::Memory* memory;
    uint64_t stride; // aligned shaderGroupIdentifierSize
    uint64_t size;
};

struct RfxMicromapImpl {
    nri::Micromap* micromap;
    nri::Memory* memory;
    nri::Buffer* barrierBuffer; // buffer for barrier transitions

    nri::AccessBits currentAccess = nri::AccessBits::NONE;
    nri::StageBits currentStage = nri::StageBits::NONE;
};

struct RfxUpscalerImpl {
    nri::Upscaler* upscaler;
    RfxUpscalerType type;
};

struct RfxFenceImpl {
    nri::Fence* fence;
    uint64_t value; // expected next value
};

//
// Command list and barrier batching
//

struct BarrierBatcher {
    RfxVector<nri::BufferBarrierDesc> bufferBarriers;
    RfxVector<nri::TextureBarrierDesc> textureBarriers;
    RfxVector<nri::GlobalBarrierDesc> globalBarriers;

    void RequireState(RfxBuffer buffer, RfxResourceState state);
    void RequireState(RfxTexture texture, RfxResourceState state);

    void Flush(nri::CommandBuffer& cmd);
    bool HasPending() const {
        return !bufferBarriers.empty() || !textureBarriers.empty() || !globalBarriers.empty();
    }
};

struct RfxCommandListImpl {
    nri::CommandBuffer* nriCmd;

    // ring buffer
    RfxVector<nri::CommandAllocator*> allocators;
    RfxVector<nri::CommandBuffer*> buffers;

    RfxQueueType queueType;
    bool isSecondary;

    BarrierBatcher barriers;
    RfxPipelineImpl* currentPipeline = nullptr;

    // cached states
    RfxBuffer lastBoundVertexBuffer = nullptr;
    RfxBuffer lastBoundIndexBuffer = nullptr;
    RfxBuffer currentVertexBuffer = nullptr;
    RfxBuffer currentIndexBuffer = nullptr;

    nri::IndexType currentIndexType = nri::IndexType::UINT32;
    bool isRendering = false;
    nri::Rect currentScissor = { 0, 0, 0, 0 };
    bool scissorSet = false;

    // active renderpass state
    RfxVector<nri::AttachmentDesc> activeColorAttachments;
    nri::RenderingDesc currentRenderingDesc = {};
    nri::Viewport currentViewport = {};
    RfxVector<RfxTexture> activeColorTextures;
    RfxTexture activeDepthTexture = nullptr;
    RfxVector<nri::Descriptor*> tempDescriptors;

    void ResetCache() {
        lastBoundVertexBuffer = nullptr;
        lastBoundIndexBuffer = nullptr;
        currentVertexBuffer = nullptr;
        currentIndexBuffer = nullptr;
        currentPipeline = nullptr;
        isRendering = false;
    }

    void PrepareForDraw();
    void BindDrawBuffers();
    void FlushBarriers();
};

struct BindlessData {
    nri::DescriptorPool* descriptorPool = nullptr;
    nri::PipelineLayout* globalLayout = nullptr;
    nri::DescriptorSet* globalDescriptorSet = nullptr;
    nri::Descriptor* staticSamplers[4];

    // stacks
    RfxVector<uint32_t> freeTextureSlots;
    uint32_t textureHighWaterMark = 0;

    RfxVector<uint32_t> freeBufferSlots;
    uint32_t bufferHighWaterMark = 0;

    RfxVector<uint32_t> freeASSlots;
    uint32_t asHighWaterMark = 0;
};

//
// NRI helpers
//

struct NRIInterface : public nri::CoreInterface,
                      public nri::HelperInterface,
                      public nri::StreamerInterface,
                      public nri::SwapChainInterface,
                      public nri::MeshShaderInterface,
                      public nri::RayTracingInterface,
                      public nri::UpscalerInterface,
                      public nri::LowLatencyInterface,
                      public nri::ImguiInterface {};

struct SwapChainTexture {
    nri::Fence* acquireSemaphore;
    nri::Fence* releaseSemaphore;
    nri::Texture* texture;
    nri::Descriptor* colorAttachment;
    nri::Format attachmentFormat;
    bool initialized = false;
};

#define RFX_MAX_TIMESTAMP_QUERIES 512

struct ProfileRegion {
    const char* name;
    uint32_t startIndex;
    uint32_t endIndex;
    int parentIndex;
};

struct QueuedFrame {
    nri::CommandAllocator* commandAllocator;
    nri::CommandBuffer* commandBuffer;
    nri::DescriptorPool* dynamicDescriptorPool;
    RfxCommandListImpl wrapper;

    // Profiler state
    RfxVector<ProfileRegion> profileRegions;
    RfxVector<int> profileStack;
    uint32_t queryCount;
};

struct RfxDenoiserImpl;

#define RFX_MAX_KEYS 350
#define RFX_MAX_MOUSE_BUTTONS 8
#define RFX_MAX_KEY_QUEUE 16
#define RFX_MAX_CHAR_QUEUE 16

struct InputState {
    bool keysCurrent[RFX_MAX_KEYS];
    bool keysPrev[RFX_MAX_KEYS];
    bool mouseButtonsCurrent[RFX_MAX_MOUSE_BUTTONS];
    bool mouseButtonsPrev[RFX_MAX_MOUSE_BUTTONS];
    double mouseX, mouseY, mouseDeltaX, mouseDeltaY;
    bool firstMouseFrame;

    int keyPressedQueue[RFX_MAX_KEY_QUEUE];
    int keyPressedQueueCount;
    uint32_t charPressedQueue[RFX_MAX_CHAR_QUEUE];
    int charPressedQueueCount;
};

struct CoreData {
    ~CoreData();

    bool EnableValidation = true;
    nri::GraphicsAPI RequestedBackend = nri::GraphicsAPI::VK;
    RfxFeatureSupportFlags FeatureSupportFlags = 0;
    void* WindowHandle = nullptr;
    nri::Window NRIWindow;
    InputState Input;
    RfxWindowFlags WindowFlags = 0;
    bool VsyncEnable = false;
    int FramebufferWidth = 0;
    int FramebufferHeight = 0;
    int SampleCount = 1;
    int Anisotropy = 1;

    bool AllowLowLatency = false;   // low latency supported by device?
    bool LowLatencyEnabled = false; // low latency enabled by user?
    bool LowLatencyBoost = false;

    bool IsFocused = true;
    bool IsMinimized = false;
    int SavedWindowPos[2] = { 100, 100 };
    int SavedWindowSize[2] = { 1280, 720 };

    // NRI
    nri::Device* NRIDevice = nullptr;
    NRIInterface NRI;
    nri::Queue* NRIGraphicsQueue = nullptr;
    nri::Queue* NRIComputeQueue = nullptr;
    nri::Queue* NRICopyQueue = nullptr;
    nri::Fence* NRIFrameFence = nullptr;
    nri::SwapChain* NRISwapChain = nullptr;
    nri::Streamer* NRIStreamer = nullptr;
    nri::Imgui* ImguiRenderer = nullptr;
    BindlessData Bindless;

    // Frames
    RfxVector<QueuedFrame> QueuedFrames;
    RfxVector<SwapChainTexture> SwapChainTextures;
    uint32_t FrameIndex = 0;
    uint32_t CurrentSwapChainTextureIndex = 0;
    uint32_t SwapChainWidth = 0;
    uint32_t SwapChainHeight = 0;
    bool FrameStarted = false;
    double LastTime = 0.0;
    float DeltaTime = 0.0f;

    // Profiler
    nri::QueryPool* TimestampPool = nullptr;
    nri::Buffer* TimestampBuffer = nullptr;
    nri::Memory* TimestampBufferMemory = nullptr;
    RfxVector<RfxGpuTimestamp> LastFrameTimestamps;

    // Implicit resources
    struct {
        RfxTexture handle = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    } DepthBuffer;

    struct {
        RfxTexture handle = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    } MSAAColorBuffer;

    RfxTextureImpl SwapChainWrapper = {};

    // Slang
    Slang::ComPtr<slang::IGlobalSession> SlangSession;

    struct DeletionQueue {
        RfxVector<std::function<void()>> tasks;
    };
    RfxVector<DeletionQueue> Graveyard; // indexed by FrameIndex % QueuedFrameNum
    RfxVector<std::function<void(nri::CommandBuffer&)>> PendingPreBarriers;
    RfxVector<std::function<void(nri::CommandBuffer&)>> PendingPostBarriers;

    std::mutex HotReloadMutex;
    RfxSet<RfxShader> ShadersToReload;

    // vfs, shader cache
    bool ShaderCacheEnabled = false;
    std::string ShaderCachePath;
    RfxShaderCacheLoadCallback CacheLoadCb = nullptr;
    RfxShaderCacheSaveCallback CacheSaveCb = nullptr;
    void* CacheUserPtr = nullptr;

    std::mutex ShaderCacheMutex;
    std::mutex ShaderCompileMutex;
    std::mutex VirtualFSMutex;
};

extern CoreData CORE;

inline uint8_t GetQueuedFrameNum() {
    return 3;
}
void rfxDeferDestruction(std::function<void()>&& task);
void rfxEventSleep();

#endif
