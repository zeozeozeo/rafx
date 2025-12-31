#include "rafx.h"
#include "rafx_internal.h"
#include <assert.h>

#if (RAFX_PLATFORM == RAFX_WINDOWS)
#    include <windows.h>
#else
#    include <signal.h>
#endif

CoreData CORE = {};

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
            delete ptr;
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
            delete ptr;
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

    nri::DescriptorPoolDesc poolDesc = {};
    poolDesc.descriptorSetMaxNum = 1;
    poolDesc.textureMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.structuredBufferMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.storageStructuredBufferMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.storageTextureMaxNum = RFX_MAX_BINDLESS_TEXTURES;
    poolDesc.samplerMaxNum = 4;
    poolDesc.accelerationStructureMaxNum = 2048;
    poolDesc.flags = nri::DescriptorPoolBits::ALLOW_UPDATE_AFTER_SET;
    NRI_CHECK(CORE.NRI.CreateDescriptorPool(*CORE.NRIDevice, poolDesc, CORE.Bindless.descriptorPool));

    bool isD3D12 = CORE.NRI.GetDeviceDesc(*CORE.NRIDevice).graphicsAPI == nri::GraphicsAPI::D3D12;

    nri::DescriptorRangeDesc ranges[6] = {};
    nri::DescriptorRangeBits bindlessFlags =
        nri::DescriptorRangeBits::PARTIALLY_BOUND | nri::DescriptorRangeBits::ARRAY | nri::DescriptorRangeBits::ALLOW_UPDATE_AFTER_SET;

    // 0 = textures srv
    ranges[0].baseRegisterIndex = 0;
    ranges[0].descriptorNum = RFX_MAX_BINDLESS_TEXTURES;
    ranges[0].descriptorType = nri::DescriptorType::TEXTURE;
    ranges[0].shaderStages = nri::StageBits::ALL;
    ranges[0].flags = bindlessFlags;

    // 1 = samplers
    ranges[1].baseRegisterIndex = isD3D12 ? 0 : 1;
    ranges[1].descriptorNum = 4;
    ranges[1].descriptorType = nri::DescriptorType::SAMPLER;
    ranges[1].shaderStages = nri::StageBits::ALL;
    ranges[1].flags = bindlessFlags;

    // 2 = buffers srv
    ranges[2].baseRegisterIndex = isD3D12 ? RFX_MAX_BINDLESS_TEXTURES : 2;
    ranges[2].descriptorNum = RFX_MAX_BINDLESS_TEXTURES;
    ranges[2].descriptorType = nri::DescriptorType::STRUCTURED_BUFFER;
    ranges[2].shaderStages = nri::StageBits::ALL;
    ranges[2].flags = bindlessFlags;

    // 3 = RW buffers
    ranges[3].baseRegisterIndex = isD3D12 ? 0 : 3;
    ranges[3].descriptorNum = RFX_MAX_BINDLESS_TEXTURES;
    ranges[3].descriptorType = nri::DescriptorType::STORAGE_STRUCTURED_BUFFER;
    ranges[3].shaderStages = nri::StageBits::ALL;
    ranges[3].flags = bindlessFlags;

    // 4 = RW textures uav
    ranges[4].baseRegisterIndex = isD3D12 ? RFX_MAX_BINDLESS_TEXTURES : 4;
    ranges[4].descriptorNum = RFX_MAX_BINDLESS_TEXTURES;
    ranges[4].descriptorType = nri::DescriptorType::STORAGE_TEXTURE;
    ranges[4].shaderStages = nri::StageBits::ALL;
    ranges[4].flags = bindlessFlags;

    // 5 = acceleration structures srv
    ranges[5].baseRegisterIndex = isD3D12 ? (RFX_MAX_BINDLESS_TEXTURES * 2) : 5;
    ranges[5].descriptorNum = 2048;
    ranges[5].descriptorType = nri::DescriptorType::ACCELERATION_STRUCTURE;
    ranges[5].shaderStages = nri::StageBits::ALL;
    ranges[5].flags = bindlessFlags;

    nri::DescriptorSetDesc setDesc = {};
    setDesc.registerSpace = 1;
    setDesc.ranges = ranges;
    setDesc.rangeNum = 6;
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
    dcd.enableGraphicsAPIValidation = true;
    dcd.enableNRIValidation = true;
    dcd.vkBindingOffsets = { 0, 128, 32, 64 };
    dcd.adapterDesc = &adapterDesc[0];
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

void rfxRequestBackend(RfxBackend backend) {
    RFX_ASSERT(!CORE.WindowHandle && "rfxRequestBackend called after window creation");
    nri::GraphicsAPI api;
    switch (backend) {
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
    return (button >= 0 && button < 8) ? CORE.Input.mouseButtonsCurrent[button] : false;
}

bool rfxIsMouseButtonPressed(RfxMouseButton button) {
    return (button >= 0 && button < 8) ? (CORE.Input.mouseButtonsCurrent[button] && !CORE.Input.mouseButtonsPrev[button]) : false;
}

bool rfxIsMouseButtonReleased(RfxMouseButton button) {
    return (button >= 0 && button < 8) ? (!CORE.Input.mouseButtonsCurrent[button] && CORE.Input.mouseButtonsPrev[button]) : false;
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
