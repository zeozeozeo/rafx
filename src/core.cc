#include "rafx.h"
#include "rafx_internal.h"
#include <cassert>
#include <cstring>

#if (RAFX_PLATFORM == RAFX_WINDOWS)
#    include <windows.h>
#endif

CoreData CORE = {};

#if _WIN32

static void* AlignedMalloc(void*, size_t size, size_t alignment) {
    return _aligned_malloc(size, alignment);
}

static void* AlignedRealloc(void*, void* memory, size_t size, size_t alignment) {
    return _aligned_realloc(memory, size, alignment);
}

static void AlignedFree(void*, void* memory) {
    _aligned_free(memory);
}

#else

static uint8_t* AlignMemory(uint8_t* memory, size_t alignment) {
    return (uint8_t*)((size_t(memory) + alignment - 1) & ~(alignment - 1));
}

static void* AlignedMalloc(void*, size_t size, size_t alignment) {
    uint8_t* memory = (uint8_t*)malloc(size + sizeof(uint8_t*) + alignment - 1);

    if (memory == nullptr)
        return nullptr;

    uint8_t* alignedMemory = AlignMemory(memory + sizeof(uint8_t*), alignment);
    uint8_t** memoryHeader = (uint8_t**)alignedMemory - 1;
    *memoryHeader = memory;

    return alignedMemory;
}

static void* AlignedRealloc(void* userArg, void* memory, size_t size, size_t alignment) {
    if (memory == nullptr)
        return AlignedMalloc(userArg, size, alignment);

    uint8_t** memoryHeader = (uint8_t**)memory - 1;
    uint8_t* oldMemory = *memoryHeader;
    uint8_t* newMemory = (uint8_t*)realloc(oldMemory, size + sizeof(uint8_t*) + alignment - 1);

    if (newMemory == nullptr)
        return nullptr;

    if (newMemory == oldMemory)
        return memory;

    uint8_t* alignedMemory = AlignMemory(newMemory + sizeof(uint8_t*), alignment);
    memoryHeader = (uint8_t**)alignedMemory - 1;
    *memoryHeader = newMemory;

    return alignedMemory;
}

static void AlignedFree(void*, void* memory) {
    if (memory == nullptr)
        return;

    uint8_t** memoryHeader = (uint8_t**)memory - 1;
    uint8_t* oldMemory = *memoryHeader;
    free(oldMemory);
}

#endif

// allocator
RfxAllocator g_Allocator = { AlignedMalloc, AlignedRealloc, AlignedFree, nullptr };

void* RfxAlloc(size_t size, size_t align) {
    return g_Allocator.allocate(g_Allocator.userArg, size, align);
}

void* RfxRealloc(void* ptr, size_t size, size_t align) {
    return g_Allocator.reallocate(g_Allocator.userArg, ptr, size, align);
}

void RfxFree(void* ptr) {
    g_Allocator.free(g_Allocator.userArg, ptr);
}

void rfxSetAllocator(const RfxAllocator* allocator) {
    if (allocator && allocator->allocate && allocator->free) {
        g_Allocator = *allocator;
    } else {
        g_Allocator = { AlignedMalloc, AlignedRealloc, AlignedFree, nullptr };
    }
}

void* NRI_CALL InternalNriAlloc(void* userArg, size_t size, size_t alignment) {
    RfxAllocator* alloc = (RfxAllocator*)userArg;
    return alloc->allocate(alloc->userArg, size, alignment);
}

void* NRI_CALL InternalNriRealloc(void* userArg, void* memory, size_t size, size_t alignment) {
    RfxAllocator* alloc = (RfxAllocator*)userArg;
    return alloc->reallocate(alloc->userArg, memory, size, alignment);
}

void NRI_CALL InternalNriFree(void* userArg, void* memory) {
    RfxAllocator* alloc = (RfxAllocator*)userArg;
    alloc->free(alloc->userArg, memory);
}

void rfxDeferDestruction(std::function<void()>&& task) {
    if (!CORE.NRIDevice) {
        task();
        return;
    }
    uint32_t numFrames = GetQueuedFrameNum();

    uint32_t currentFrame = CORE.FrameIndex;
    if (!CORE.FrameStarted && currentFrame > 0) {
        currentFrame--;
    }

    uint32_t safeSlot = currentFrame % numFrames;
    CORE.Graveyard[safeSlot].tasks.push_back(std::move(task));
}

CoreData::~CoreData() {
    if (NRIDevice) {
        NRI.DeviceWaitIdle(NRIDevice);

        // process graveyard
        for (auto& queue : Graveyard) {
            for (auto& task : queue.tasks) {
                task();
            }
            queue.tasks.clear();
        }

        // destroy depth
        if (DepthBuffer.handle) {
            RfxTextureImpl* ptr = DepthBuffer.handle;
            if (ptr->descriptor)
                NRI.DestroyDescriptor(ptr->descriptor);
            if (ptr->descriptorAttachment)
                NRI.DestroyDescriptor(ptr->descriptorAttachment);
            NRI.DestroyTexture(ptr->texture);
            NRI.FreeMemory(ptr->memory);
            RfxDelete(ptr);
            DepthBuffer.handle = nullptr;
        }

        if (MSAAColorBuffer.handle) {
            RfxTextureImpl* ptr = MSAAColorBuffer.handle;
            if (ptr->descriptor)
                NRI.DestroyDescriptor(ptr->descriptor);
            if (ptr->descriptorAttachment)
                NRI.DestroyDescriptor(ptr->descriptorAttachment);
            NRI.DestroyTexture(ptr->texture);
            NRI.FreeMemory(ptr->memory);
            RfxDelete(ptr);
            MSAAColorBuffer.handle = nullptr;
        }

        // destroy queued frames
        for (QueuedFrame& qf : QueuedFrames) {
            if (qf.commandBuffer)
                NRI.DestroyCommandBuffer(qf.commandBuffer);
            if (qf.commandAllocator)
                NRI.DestroyCommandAllocator(qf.commandAllocator);
            if (qf.dynamicDescriptorPool)
                NRI.DestroyDescriptorPool(qf.dynamicDescriptorPool);
        }
        QueuedFrames.clear();

        // destroy bindless
        if (Bindless.globalLayout)
            NRI.DestroyPipelineLayout(Bindless.globalLayout);
        if (Bindless.descriptorPool)
            NRI.DestroyDescriptorPool(Bindless.descriptorPool);
        for (int i = 0; i < 4; i++) {
            if (Bindless.staticSamplers[i])
                NRI.DestroyDescriptor(Bindless.staticSamplers[i]);
        }

        // destroy swapchain texturess and semaphores
        for (auto& s : SwapChainTextures) {
            NRI.DestroyFence(s.acquireSemaphore);
            NRI.DestroyFence(s.releaseSemaphore);
            NRI.DestroyDescriptor(s.colorAttachment);
        }
        SwapChainTextures.clear();

        // destroy nri
        if (NRISwapChain)
            NRI.DestroySwapChain(NRISwapChain);
        if (NRIFrameFence)
            NRI.DestroyFence(NRIFrameFence);
        if (NRIStreamer)
            NRI.DestroyStreamer(NRIStreamer);
        if (ImguiRenderer)
            NRI.DestroyImgui(ImguiRenderer);
        if (TimestampPool)
            NRI.DestroyQueryPool(TimestampPool);
        if (TimestampBuffer)
            NRI.DestroyBuffer(TimestampBuffer);
        if (TimestampBufferMemory)
            NRI.FreeMemory(TimestampBufferMemory);

        if (SlangSession)
            SlangSession.setNull();

        nri::nriDestroyDevice(NRIDevice);
    }

    Backend_DestroyWindow();
}

static void CreateStaticSamplers() {
    // 0 = linear clamp
    nri::SamplerDesc sd = {};
    sd.mipMax = 16.0f;
    sd.anisotropy = (uint8_t)CORE.Anisotropy;
    sd.filters = { .min = nri::Filter::LINEAR, .mag = nri::Filter::LINEAR, .mip = nri::Filter::LINEAR, .op = nri::FilterOp::AVERAGE };
    sd.addressModes = { .u = nri::AddressMode::CLAMP_TO_EDGE, .v = nri::AddressMode::CLAMP_TO_EDGE, .w = nri::AddressMode::CLAMP_TO_EDGE };
    NRI_CHECK(CORE.NRI.CreateSampler(*CORE.NRIDevice, sd, CORE.Bindless.staticSamplers[0]));

    // 1 = linear wrap
    sd.addressModes = { .u = nri::AddressMode::REPEAT, .v = nri::AddressMode::REPEAT, .w = nri::AddressMode::REPEAT };
    NRI_CHECK(CORE.NRI.CreateSampler(*CORE.NRIDevice, sd, CORE.Bindless.staticSamplers[1]));

    // nearest
    sd.anisotropy = 1;
    sd.filters = { .min = nri::Filter::NEAREST, .mag = nri::Filter::NEAREST, .mip = nri::Filter::NEAREST, .op = nri::FilterOp::AVERAGE };

    // 2 = nearest clamp
    sd.addressModes = { .u = nri::AddressMode::CLAMP_TO_EDGE, .v = nri::AddressMode::CLAMP_TO_EDGE, .w = nri::AddressMode::CLAMP_TO_EDGE };
    NRI_CHECK(CORE.NRI.CreateSampler(*CORE.NRIDevice, sd, CORE.Bindless.staticSamplers[2]));

    // 3 = nearest wrap
    sd.addressModes = { .u = nri::AddressMode::REPEAT, .v = nri::AddressMode::REPEAT, .w = nri::AddressMode::REPEAT };
    NRI_CHECK(CORE.NRI.CreateSampler(*CORE.NRIDevice, sd, CORE.Bindless.staticSamplers[3]));
}

static void InitBindless() {
    CreateStaticSamplers();

    bool hasRT = (CORE.FeatureSupportFlags & RFX_FEATURE_RAY_TRACING) != 0;

    nri::DescriptorPoolDesc poolDesc = {};
    poolDesc.descriptorSetMaxNum = 1;
    poolDesc.textureMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.structuredBufferMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.storageStructuredBufferMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.storageTextureMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.samplerMaxNum = 4;
    poolDesc.accelerationStructureMaxNum = hasRT ? 2048 : 0;
    poolDesc.flags = nri::DescriptorPoolBits::ALLOW_UPDATE_AFTER_SET;
    NRI_CHECK(CORE.NRI.CreateDescriptorPool(*CORE.NRIDevice, poolDesc, CORE.Bindless.descriptorPool));

    bool isD3D12 = CORE.NRI.GetDeviceDesc(*CORE.NRIDevice).graphicsAPI == nri::GraphicsAPI::D3D12;

    nri::DescriptorRangeBits bindlessFlags =
        nri::DescriptorRangeBits::PARTIALLY_BOUND | nri::DescriptorRangeBits::ARRAY | nri::DescriptorRangeBits::ALLOW_UPDATE_AFTER_SET;

    nri::DescriptorRangeDesc ranges[6];

    // 0 = textures srv
    ranges[0] = { 0, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::TEXTURE, nri::StageBits::ALL, bindlessFlags };

    // 1 = samplers
    ranges[1] = { isD3D12 ? 0u : 1u, 4, nri::DescriptorType::SAMPLER, nri::StageBits::ALL, bindlessFlags };

    // 2 = buffers srv
    ranges[2] = { isD3D12 ? RFX_MAX_BINDLESS_TEXTURES : 2u, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::STRUCTURED_BUFFER,
                  nri::StageBits::ALL, bindlessFlags };

    // 3 = RW buffers
    ranges[3] = { isD3D12 ? 0u : 3u, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::STORAGE_STRUCTURED_BUFFER, nri::StageBits::ALL,
                  bindlessFlags };

    // 4 = RW textures uav
    ranges[4] = { isD3D12 ? RFX_MAX_BINDLESS_TEXTURES : 4u, RFX_MAX_BINDLESS_TEXTURES, nri::DescriptorType::STORAGE_TEXTURE,
                  nri::StageBits::ALL, bindlessFlags };

    uint32_t rangeCount = 5;
    if (hasRT) {
        // 5 = acceleration structures srv
        ranges[5] = { isD3D12 ? (RFX_MAX_BINDLESS_TEXTURES * 2) : 5u, 2048, nri::DescriptorType::ACCELERATION_STRUCTURE,
                      nri::StageBits::ALL, bindlessFlags };
        rangeCount = 6;
    }

    nri::DescriptorSetDesc setDesc = {};
    setDesc.registerSpace = 1;
    setDesc.ranges = ranges;
    setDesc.rangeNum = rangeCount;
    setDesc.flags = nri::DescriptorSetBits::ALLOW_UPDATE_AFTER_SET;

    nri::PipelineLayoutDesc layoutDesc = {};
    layoutDesc.descriptorSets = &setDesc;
    layoutDesc.descriptorSetNum = 1;
    layoutDesc.shaderStages = nri::StageBits::ALL;
    layoutDesc.flags = nri::PipelineLayoutBits::IGNORE_GLOBAL_SPIRV_OFFSETS;

    NRI_CHECK(CORE.NRI.CreatePipelineLayout(*CORE.NRIDevice, layoutDesc, CORE.Bindless.globalLayout));

    nri::DescriptorSet* sets[1];
    NRI_CHECK(CORE.NRI.AllocateDescriptorSets(*CORE.Bindless.descriptorPool, *CORE.Bindless.globalLayout, 0, sets, 1, 0));
    CORE.Bindless.globalDescriptorSet = sets[0];

    nri::UpdateDescriptorRangeDesc update = {};
    update.descriptorSet = CORE.Bindless.globalDescriptorSet;
    update.rangeIndex = 1;
    update.baseDescriptor = 0;
    update.descriptorNum = 4;
    update.descriptors = CORE.Bindless.staticSamplers;
    CORE.NRI.UpdateDescriptorRanges(&update, 1);
}

static void NRIInitialize(nri::GraphicsAPI graphicsAPI) {
    nri::AdapterDesc adapterDesc[2] = {};
    uint32_t adapterCnt = 2;
    nri::nriEnumerateAdapters(adapterDesc, adapterCnt);

    nri::DeviceCreationDesc dcd = {};
    dcd.graphicsAPI = graphicsAPI;
    dcd.enableGraphicsAPIValidation = CORE.EnableValidation;
    dcd.enableNRIValidation = CORE.EnableValidation;
    dcd.vkBindingOffsets = { 0, 128, 32, 64 };
    dcd.adapterDesc = &adapterDesc[0];
    dcd.allocationCallbacks.Allocate = InternalNriAlloc;
    dcd.allocationCallbacks.Reallocate = InternalNriRealloc;
    dcd.allocationCallbacks.Free = InternalNriFree;
    dcd.allocationCallbacks.userArg = &g_Allocator;
    NRI_CHECK(nri::nriCreateDevice(dcd, CORE.NRIDevice));

    NRI_CHECK(nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::CoreInterface), (nri::CoreInterface*)&CORE.NRI));
    NRI_CHECK(nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::HelperInterface), (nri::HelperInterface*)&CORE.NRI));
    NRI_CHECK(nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::StreamerInterface), (nri::StreamerInterface*)&CORE.NRI));
    NRI_CHECK(nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::SwapChainInterface), (nri::SwapChainInterface*)&CORE.NRI));
    NRI_CHECK(nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::ImguiInterface), (nri::ImguiInterface*)&CORE.NRI));

    // these may not be supported:
    if (nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::MeshShaderInterface), (nri::MeshShaderInterface*)&CORE.NRI) ==
        nri::Result::SUCCESS) {
        CORE.FeatureSupportFlags |= RFX_FEATURE_MESH_SHADER;
    }
    if (nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::RayTracingInterface), (nri::RayTracingInterface*)&CORE.NRI) ==
        nri::Result::SUCCESS) {
        CORE.FeatureSupportFlags |= RFX_FEATURE_RAY_TRACING;
    }
    if (nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::UpscalerInterface), (nri::UpscalerInterface*)&CORE.NRI) ==
        nri::Result::SUCCESS) {
        CORE.FeatureSupportFlags |= RFX_FEATURE_UPSCALE;
    }
    if (nri::nriGetInterface(*CORE.NRIDevice, NRI_INTERFACE(nri::LowLatencyInterface), (nri::LowLatencyInterface*)&CORE.NRI) ==
        nri::Result::SUCCESS) {
        CORE.FeatureSupportFlags |= RFX_FEATURE_LOW_LATENCY;
        const nri::DeviceDesc& desc = CORE.NRI.GetDeviceDesc(*CORE.NRIDevice);
        if (desc.features.lowLatency) {
            CORE.AllowLowLatency = true;
        }
    }

    InitBindless();

    nri::StreamerDesc sd = {};
    sd.dynamicBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    sd.dynamicBufferDesc = {
        0, 0, nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER | nri::BufferUsageBits::CONSTANT_BUFFER
    };
    sd.constantBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
    sd.queuedFrameNum = GetQueuedFrameNum();
    NRI_CHECK(CORE.NRI.CreateStreamer(*CORE.NRIDevice, sd, CORE.NRIStreamer));

    CORE.NRI.GetQueue(*CORE.NRIDevice, nri::QueueType::GRAPHICS, 0, CORE.NRIGraphicsQueue);
    if (CORE.NRI.GetQueue(*CORE.NRIDevice, nri::QueueType::COMPUTE, 0, CORE.NRIComputeQueue) != nri::Result::SUCCESS) {
        CORE.NRIComputeQueue = CORE.NRIGraphicsQueue; // fallback
    }
    if (CORE.NRI.GetQueue(*CORE.NRIDevice, nri::QueueType::COPY, 0, CORE.NRICopyQueue) != nri::Result::SUCCESS) {
        CORE.NRICopyQueue = CORE.NRIGraphicsQueue; // fallback
    }

    NRI_CHECK(CORE.NRI.CreateFence(*CORE.NRIDevice, 0, CORE.NRIFrameFence));

    // Profiler
    {
        nri::QueryPoolDesc qpd = {};
        qpd.queryType = nri::QueryType::TIMESTAMP;
        qpd.capacity = RFX_MAX_TIMESTAMP_QUERIES * GetQueuedFrameNum();
        NRI_CHECK(CORE.NRI.CreateQueryPool(*CORE.NRIDevice, qpd, CORE.TimestampPool));

        nri::BufferDesc bd = {};
        bd.size = qpd.capacity * sizeof(uint64_t);
        bd.usage = nri::BufferUsageBits::NONE;
        NRI_CHECK(CORE.NRI.CreateBuffer(*CORE.NRIDevice, bd, CORE.TimestampBuffer));

        nri::MemoryDesc md = {};
        CORE.NRI.GetBufferMemoryDesc(*CORE.TimestampBuffer, nri::MemoryLocation::HOST_READBACK, md);

        nri::AllocateMemoryDesc amd = {};
        amd.size = md.size;
        amd.type = md.type;
        amd.vma.enable = true;
        NRI_CHECK(CORE.NRI.AllocateMemory(*CORE.NRIDevice, amd, CORE.TimestampBufferMemory));

        nri::BindBufferMemoryDesc bind = { CORE.TimestampBuffer, CORE.TimestampBufferMemory, 0 };
        NRI_CHECK(CORE.NRI.BindBufferMemory(&bind, 1));
    }

    CORE.QueuedFrames.resize(GetQueuedFrameNum());
    CORE.Graveyard.resize(GetQueuedFrameNum());

    for (QueuedFrame& qf : CORE.QueuedFrames) {
        NRI_CHECK(CORE.NRI.CreateCommandAllocator(*CORE.NRIGraphicsQueue, qf.commandAllocator));
        NRI_CHECK(CORE.NRI.CreateCommandBuffer(*qf.commandAllocator, qf.commandBuffer));

        nri::DescriptorPoolDesc poolDesc = {};
        poolDesc.descriptorSetMaxNum = 4096;
        poolDesc.textureMaxNum = 8192;
        poolDesc.samplerMaxNum = 512;
        poolDesc.constantBufferMaxNum = 4096;
        poolDesc.bufferMaxNum = 4096;
        poolDesc.storageTextureMaxNum = 1024;
        poolDesc.storageBufferMaxNum = 1024;
        poolDesc.structuredBufferMaxNum = 1024;
        poolDesc.storageStructuredBufferMaxNum = 1024;

        NRI_CHECK(CORE.NRI.CreateDescriptorPool(*CORE.NRIDevice, poolDesc, qf.dynamicDescriptorPool));
        qf.wrapper.nriCmd = qf.commandBuffer;
    }
}

void rfxRequestBackend(RfxBackend backend, bool enableValidation) {
    RFX_ASSERT(!CORE.WindowHandle && "rfxRequestBackend called after window creation");

    CORE.EnableValidation = enableValidation;

    nri::GraphicsAPI api;
    switch (backend) {
    case RFX_BACKEND_DEFAULT: return;
    case RFX_BACKEND_VULKAN: api = nri::GraphicsAPI::VK; break;
    case RFX_BACKEND_D3D12: api = nri::GraphicsAPI::D3D12; break;
    case RFX_BACKEND_D3D11: api = nri::GraphicsAPI::D3D11; break;
    case RFX_BACKEND_NONE: api = nri::GraphicsAPI::NONE; break;
    default: api = nri::GraphicsAPI::VK; break;
    }
    CORE.RequestedBackend = api;
}

bool rfxOpenWindow(const char* title, int width, int height) {
    if (!Backend_CreateWindow(title, width, height))
        return false;

    Backend_GetNativeHandles(CORE.NRIWindow);

    if (SLANG_FAILED(slang::createGlobalSession(CORE.SlangSession.writeRef())))
        return false;

    NRIInitialize(CORE.RequestedBackend);
    return true;
}

bool rfxSupportsFeatures(RfxFeatureSupportFlags features) {
    return (CORE.FeatureSupportFlags & features) == features;
}

RfxFeatureSupportFlags rfxGetSupportedFeatures() {
    return CORE.FeatureSupportFlags;
}

//
// Window
//

void rfxSetWindowFlags(RfxWindowFlags flags) {
    Backend_SetWindowFlags(flags); // sets CORE.WindowFlags
}

void rfxEnableWindowFlags(RfxWindowFlags flags) {
    rfxSetWindowFlags(CORE.WindowFlags | flags);
}

void rfxDisableWindowFlags(RfxWindowFlags flags) {
    rfxSetWindowFlags(CORE.WindowFlags & ~flags);
}

void rfxToggleWindowFlags(RfxWindowFlags flags) {
    rfxSetWindowFlags(CORE.WindowFlags ^ flags);
}

bool rfxHasWindowFlags(RfxWindowFlags flags) {
    return (CORE.WindowFlags & flags) == flags;
}

bool rfxWindowShouldClose() {
    return Backend_WindowShouldClose();
}

void rfxPollInputEvents() {
    memcpy(CORE.Input.keysPrev, CORE.Input.keysCurrent, sizeof(CORE.Input.keysCurrent));
    memcpy(CORE.Input.mouseButtonsPrev, CORE.Input.mouseButtonsCurrent, sizeof(CORE.Input.mouseButtonsCurrent));

    double prevX = CORE.Input.mouseX;
    double prevY = CORE.Input.mouseY;

    Backend_PollEvents();

    if (CORE.Input.firstMouseFrame) {
        CORE.Input.mouseDeltaX = 0;
        CORE.Input.mouseDeltaY = 0;
        CORE.Input.firstMouseFrame = false;
    } else {
        CORE.Input.mouseDeltaX = CORE.Input.mouseX - prevX;
        CORE.Input.mouseDeltaY = CORE.Input.mouseY - prevY;
    }
}

void rfxGetWindowSize(int* width, int* height) {
    Backend_GetWindowSize(width, height);
}

int rfxGetWindowWidth() {
    return Backend_GetWindowWidth();
}

int rfxGetWindowHeight() {
    return Backend_GetWindowHeight();
}

double rfxGetTime() {
    return Backend_GetTime();
}
float rfxGetDeltaTime() {
    return CORE.DeltaTime;
}
uint32_t rfxGetFrameIndex() {
    return CORE.FrameIndex;
}

//
// Input
//

void Input_PushKeyPressed(int key) {
    if (CORE.Input.keyPressedQueueCount < RFX_MAX_KEY_QUEUE) {
        CORE.Input.keyPressedQueue[CORE.Input.keyPressedQueueCount] = key;
        CORE.Input.keyPressedQueueCount++;
    }
}

void Input_PushCharPressed(uint32_t codepoint) {
    if (CORE.Input.charPressedQueueCount < RFX_MAX_CHAR_QUEUE) {
        CORE.Input.charPressedQueue[CORE.Input.charPressedQueueCount] = codepoint;
        CORE.Input.charPressedQueueCount++;
    }
}

int rfxGetKeyPressed(void) {
    int value = 0;
    if (CORE.Input.keyPressedQueueCount > 0) {
        value = CORE.Input.keyPressedQueue[0];
        for (int i = 0; i < (CORE.Input.keyPressedQueueCount - 1); i++)
            CORE.Input.keyPressedQueue[i] = CORE.Input.keyPressedQueue[i + 1];
        CORE.Input.keyPressedQueue[CORE.Input.keyPressedQueueCount - 1] = 0;
        CORE.Input.keyPressedQueueCount--;
    }
    return value;
}

uint32_t rfxGetCharPressed(void) {
    uint32_t value = 0;
    if (CORE.Input.charPressedQueueCount > 0) {
        value = CORE.Input.charPressedQueue[0];
        for (int i = 0; i < (CORE.Input.charPressedQueueCount - 1); i++)
            CORE.Input.charPressedQueue[i] = CORE.Input.charPressedQueue[i + 1];
        CORE.Input.charPressedQueue[CORE.Input.charPressedQueueCount - 1] = 0;
        CORE.Input.charPressedQueueCount--;
    }
    return value;
}

bool rfxIsKeyDown(RfxKey key) {
    return (key >= 0 && key < 350) ? CORE.Input.keysCurrent[key] : false;
}

bool rfxIsKeyPressed(RfxKey key) {
    return (key >= 0 && key < 350) ? (CORE.Input.keysCurrent[key] && !CORE.Input.keysPrev[key]) : false;
}

bool rfxIsKeyReleased(RfxKey key) {
    return (key >= 0 && key < 350) ? (!CORE.Input.keysCurrent[key] && CORE.Input.keysPrev[key]) : false;
}

bool rfxIsMouseButtonDown(RfxMouseButton button) {
    return (button < 8) ? CORE.Input.mouseButtonsCurrent[button] : false;
}

bool rfxIsMouseButtonPressed(RfxMouseButton button) {
    return (button < 8) ? (CORE.Input.mouseButtonsCurrent[button] && !CORE.Input.mouseButtonsPrev[button]) : false;
}

bool rfxIsMouseButtonReleased(RfxMouseButton button) {
    return (button < 8) ? (!CORE.Input.mouseButtonsCurrent[button] && CORE.Input.mouseButtonsPrev[button]) : false;
}

void rfxGetMousePos(float* x, float* y) {
    if (x)
        *x = (float)CORE.Input.mouseX;
    if (y)
        *y = (float)CORE.Input.mouseY;
}

void rfxGetMouseDelta(float* x, float* y) {
    if (x)
        *x = (float)CORE.Input.mouseDeltaX;
    if (y)
        *y = (float)CORE.Input.mouseDeltaY;
}

void rfxSetMouseCursorVisible(bool visible) {
    Backend_SetMouseCursorVisible(visible);
}

void rfxSetMouseCursor(RfxCursorType cursor) {
    Backend_SetMouseCursor(cursor);
}

void rfxEventSleep() {
    Backend_EventSleep();
}

void rfxSetSampleCount(int count) {
    if (count < 1)
        count = 1;
    CORE.SampleCount = count;
    // trigger recreation
    CORE.DepthBuffer.width = 0;
    CORE.MSAAColorBuffer.width = 0;
}

void rfxSetAnisotropy(int level) {
    if (level < 1)
        level = 1;
    if (level > 16)
        level = 16;

    if (CORE.Anisotropy == level)
        return;
    CORE.Anisotropy = level;

    if (CORE.NRIDevice) {
        for (int i = 0; i < 4; i++) {
            if (CORE.Bindless.staticSamplers[i]) {
                nri::Descriptor* oldSampler = CORE.Bindless.staticSamplers[i];
                rfxDeferDestruction([=]() { CORE.NRI.DestroyDescriptor(oldSampler); });
                CORE.Bindless.staticSamplers[i] = nullptr;
            }
        }

        CreateStaticSamplers();

        nri::UpdateDescriptorRangeDesc update = {};
        update.descriptorSet = CORE.Bindless.globalDescriptorSet;
        update.rangeIndex = 1;
        update.baseDescriptor = 0;
        update.descriptorNum = 4;
        update.descriptors = CORE.Bindless.staticSamplers;
        CORE.NRI.UpdateDescriptorRanges(&update, 1);
    }
}
