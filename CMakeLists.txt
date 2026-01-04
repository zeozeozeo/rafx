cmake_minimum_required(VERSION 3.11)
project(rafx)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FETCHCONTENT_QUIET FALSE)

set(RAFX_WINDOW_BACKEND "RGFW" CACHE STRING "Select window backend (RGFW, GLFW or SDL)")
set_property(CACHE RAFX_WINDOW_BACKEND PROPERTY STRINGS "RGFW" "GLFW" "SDL")
option(RAFX_OPTIMAL_USAGE "Assert optimal API callsites" ON)
option(RAFX_USE_WAYLAND "Enable Wayland support on Linux" OFF)
option(RAFX_D3D11_SUPPORT "Enable D3D11 support (Experimental)" OFF)
option(RAFX_D3D12_SUPPORT "Enable D3D12 support" OFF)
option(RAFX_VULKAN_SUPPORT "Enable Vulkan support" ON)
option(RAFX_VALIDATION "Enable validation layers and debug info" ON)
option(RAFX_UPSCALER_NIS "Enable support for NIS (NVIDIA Image Sharpening) upscaler" ON)
option(RAFX_UPSCALER_DLSS "Enable support for DLSS upscaler" OFF)
option(RAFX_UPSCALER_FFX "Enable support for FFX (FidelityFX Super Resolution) upscaler" OFF)
option(RAFX_UPSCALER_XESS "Enable support for XeSS upscaler" OFF)
option(RAFX_BUILD_EXAMPLES "Build examples" OFF)
option(RAFX_STATIC_SLANG "Build Slang from source instead of downloading binaries" OFF)
option(RAFX_STATIC_SDL "Link SDL statically instead of building a shared library" OFF)
option(RAFX_STRIP_SDL "Disable useless SDL features" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Arm64?
if((CMAKE_GENERATOR_PLATFORM MATCHES "ARM64") OR (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64"))
    set(IS_ARM64 TRUE)
else()
    set(IS_ARM64 FALSE)
endif()

# Compile options
if(NOT IS_ARM64)
    set(SIMD -mssse3)
endif()

if(WIN32)
    set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
elseif(RAFX_USE_WAYLAND)
    set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} RAFX_USE_WAYLAND=1)
endif()

if(RAFX_OPTIMAL_USAGE)
    set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} RAFX_OPTIMAL_USAGE)
endif()
if(RAFX_NO_HMM)
    set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} RAFX_NO_HMM)
endif()

#set(COMPILE_DEFINITIONS ${COMPILE_DEFINITIONS} RAFX_EXPORTS)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COMPILE_OPTIONS ${SIMD} -Wextra)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COMPILE_OPTIONS ${SIMD} -Wextra)
elseif(MSVC)
    set(COMPILE_OPTIONS
        /W4
        /wd4324 # struct padding warning
        /wd4244 # possible loss of data
        /wd4305 # truncation from 'double' to 'float'
        /wd4576 # parenthesized type followed by initializer list
        /wd4996 # 'sprintf': This function or variable may be unsafe
        /wd4189 # local variable is initialized but not referenced
        /wd4505 # unreferenced function with internal linkage removed
        /wd4201 # nonstandard extension used: nameless struct/union
    )
    string(REPLACE "/WX" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
else()
    message(WARNING "Unknown compiler!")
endif()

# CPM
include("CPM.cmake")

# NRI
set(NRI_ENABLE_NONE_SUPPORT ON)
set(NRI_ENABLE_D3D11_SUPPORT ${RAFX_D3D11_SUPPORT})
set(NRI_ENABLE_D3D12_SUPPORT ${RAFX_D3D12_SUPPORT})
set(NRI_ENABLE_VK_SUPPORT ${RAFX_VULKAN_SUPPORT})
set(NRI_ENABLE_WAYLAND_SUPPORT ${RAFX_USE_WAYLAND})
set(NRI_ENABLE_IMGUI_EXTENSION ON)
set(NRI_STATIC_LIBRARY ON)
set(NRI_ENABLE_VALIDATION_SUPPORT ${RAFX_VALIDATION})
set(NRI_ENABLE_DEBUG_NAMES_AND_ANNOTATIONS ${RAFX_VALIDATION})
set(NRI_ENABLE_NIS_SDK ${RAFX_UPSCALER_NIS})
set(NRI_ENABLE_NGX_SDK ${RAFX_UPSCALER_DLSS}) # DLSS
set(NRI_ENABLE_FFX_SDK ${RAFX_UPSCALER_FFX})
set(NRI_ENABLE_XESS_SDK ${RAFX_UPSCALER_XESS})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/NRI)

# NRD
set(NRD_STATIC_LIBRARY ON)
set(NRD_NORMAL_ENCODING "4" CACHE STRING "")
set(NRD_ROUGHNESS_ENCODING "1" CACHE STRING "")
set(NRD_EMBEDS_DXIL_SHADERS ${RAFX_D3D12_SUPPORT})
set(NRD_EMBEDS_DXBC_SHADERS ${RAFX_D3D11_SUPPORT})
set(NRD_EMBEDS_SPIRV_SHADERS ${RAFX_VULKAN_SUPPORT})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/NRD)
if(TARGET NRD AND WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # an incredible hack to get rid of -fPIC that's there for some reason
    set_target_properties(NRD PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    get_target_property(_nrd_opts NRD COMPILE_OPTIONS)
    if(_nrd_opts)
        list(REMOVE_ITEM _nrd_opts "-fPIC")
        set_target_properties(NRD PROPERTIES COMPILE_OPTIONS "${_nrd_opts}")
    endif()
    get_target_property(_nrd_interface_opts NRD INTERFACE_COMPILE_OPTIONS)
    if(_nrd_interface_opts)
        list(REMOVE_ITEM _nrd_interface_opts "-fPIC")
        set_target_properties(NRD PROPERTIES INTERFACE_COMPILE_OPTIONS "${_nrd_interface_opts}")
    endif()
endif()

if(TARGET NRD AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(NRD PRIVATE -Wno-unused-parameter)
endif()

if(RAFX_WINDOW_BACKEND STREQUAL "GLFW")
    # MetalUtility to get CAMetalLayer on GLFW+MacOS
    if(APPLE)
        find_library(COCOA_LIBRARY Cocoa REQUIRED)
        find_library(QUARTZ_CORE QuartzCore REQUIRED)
        add_library(MetalUtility SHARED "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/MetalUtility/MetalUtility.m")
        target_link_libraries(MetalUtility
            ${QUARTZ_CORE}
            ${COCOA_LIBRARY}
            glfw
        )
    endif()

    # glfw
    set(GLFW_BUILD_WAYLAND ${RAFX_USE_WAYLAND})
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_BUILD_TESTS OFF)
    set(GLFW_BUILD_DOCS OFF)
    set(GLFW_INSTALL OFF)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glfw)
endif()

# slang
if(CMAKE_HOST_WIN32)
    set(SLANG_BIN_PACKAGE_NAME "windows-x86_64.zip")
elseif(CMAKE_HOST_LINUX)
    set(SLANG_BIN_PACKAGE_NAME "linux-x86_64.zip")
else()
    message(FATAL_ERROR "Unsupported host")
endif()

if(RAFX_STATIC_SLANG)
    # This is hell
    CPMAddPackage(
        NAME slang
        GIT_SHALLOW TRUE
        GITHUB_REPOSITORY shader-slang/slang
        GIT_TAG v2025.24.2
        GIT_PROGRESS TRUE
        GIT_SUBMODULES_RECURSE FALSE
        GIT_SUBMODULES "external/spirv-headers;external/vulkan;external/miniz;external/unordered_dense;external/lz4;external/lua"
        OPTIONS
            "SLANG_ENABLE_EXAMPLES OFF"
            "SLANG_ENABLE_TESTS OFF"
            "SLANG_ENABLE_SLANG_GLSLANG OFF"
            "SLANG_ENABLE_SLANGI OFF"
            "SLANG_ENABLE_SLANGRT OFF"
            "SLANG_ENABLE_SLANGD OFF"
            "SLANG_ENABLE_GFX OFF"
            "SLANG_ENABLE_PREBUILT_BINARIES OFF"
            "BUILD_SHARED_LIBS OFF"
            "SLANG_EXCLUDE_DAWN ON"
            "SLANG_EXCLUDE_TINT ON"
            "SLANG_SLANG_LLVM_FLAVOR DISABLE"
            "SLANG_ENABLE_OPTIX OFF"
            "SLANG_ENABLE_SLANGC OFF"
            "SLANG_ENABLE_SLANG_RHI OFF"
            "SLANG_LIB_TYPE STATIC"
    )

    if(NOT TARGET slang::slang)
        add_library(slang::slang ALIAS slang)
    endif()
else()
    if(CMAKE_HOST_WIN32)
        set(SLANG_BIN_PACKAGE_NAME "windows-x86_64.zip")
    elseif(CMAKE_HOST_LINUX)
        set(SLANG_BIN_PACKAGE_NAME "linux-x86_64.zip")
    else()
        message(FATAL_ERROR "Unsupported host for Slang binaries")
    endif()

    if (NOT slang_SOURCE_DIR)
        CPMAddPackage(
            NAME slang
            URL "https://github.com/shader-slang/slang/releases/download/v2025.24.2/slang-2025.24.2-${SLANG_BIN_PACKAGE_NAME}"
            DOWNLOAD_ONLY true
        )
    endif()

    if(NOT TARGET slang::slang)
        add_library(slang::slang SHARED IMPORTED)
        if(CMAKE_HOST_WIN32)
            set_target_properties(slang::slang PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES   "${slang_SOURCE_DIR}/include/"
                IMPORTED_IMPLIB                 "${slang_SOURCE_DIR}/lib/slang.lib"
                IMPORTED_LOCATION               "${slang_SOURCE_DIR}/bin/slang.dll"
            )
        elseif(CMAKE_HOST_LINUX)
            set_target_properties(slang::slang PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES   "${slang_SOURCE_DIR}/include/"
                IMPORTED_LOCATION               "${slang_SOURCE_DIR}/lib/libslang.so"
            )
            target_link_libraries(slang::slang PUBLIC INTERFACE "${slang_SOURCE_DIR}/lib/libslang.so")
        endif()
    endif()
endif()

# Us
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
file(GLOB_RECURSE HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")

if(RAFX_WINDOW_BACKEND STREQUAL "GLFW")
    set(BACKEND_DEFINES RAFX_BACKEND_GLFW)
elseif(RAFX_WINDOW_BACKEND STREQUAL "RGFW")
    set(BACKEND_DEFINES RAFX_BACKEND_RGFW)
elseif(RAFX_WINDOW_BACKEND STREQUAL "SDL")
    set(BACKEND_DEFINES RAFX_BACKEND_SDL)
else()
    message(FATAL_ERROR "Invalid windowing backend specified: ${RAFX_WINDOW_BACKEND}")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
if(BUILD_SHARED_LIBS)
    add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})
else()
    add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})
endif()
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
    $<$<BOOL:${RAFX_USE_WAYLAND}>:/usr/include/wayland> # XXX: workaround
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        slang::slang
     PUBLIC
        NRI
        NRD
        NRDIntegration
)

if(RAFX_WINDOW_BACKEND STREQUAL "GLFW")
    target_link_libraries(${PROJECT_NAME} PRIVATE glfw)
    if(APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}
            pthread
            MetalUtility
        )
    elseif(UNIX)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}
            pthread
            X11
        )
    endif()
elseif(RAFX_WINDOW_BACKEND STREQUAL "RGFW")
    if(APPLE)
        find_library(COCOA_LIBRARY Cocoa REQUIRED)
        find_library(IOKIT_LIBRARY IOKit REQUIRED)
        find_library(COREVIDEO_LIBRARY CoreVideo REQUIRED)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}
            pthread
            ${COCOA_LIBRARY}
            ${IOKIT_LIBRARY}
            ${COREVIDEO_LIBRARY}
        )
    elseif(UNIX AND NOT RAFX_USE_WAYLAND)
        # RGFW+X11
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}
            pthread
            X11
            Xrandr
        )
    elseif(UNIX AND RAFX_USE_WAYLAND)
        # RGFW+Wayland
        # TODO: test
        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${CMAKE_DL_LIBS}
            pthread
            wayland-client
            wayland-cursor
            xkbcommon
        )
    endif()
elseif(RAFX_WINDOW_BACKEND STREQUAL "SDL")
    set(SDL_OPTIONS
        "SDL_EXAMPLES OFF"
        "SDL_TESTS OFF"
        "SDL_INSTALL OFF"
    )

    if(RAFX_STRIP_SDL)
        set(SDL_DISABLED_FEATURES
            ALTIVEC ARMNEON ASAN DIALOG GPU ASSEMBLY AVX AVX2 AVX512F BACKGROUNDING_SIGNAL
            CCACHE CLANG_TIDY CLOCK_GETTIME DBUS DEPS_SHARED DIRECTX POWER AUDIO DISKAUDIO DUMMYAUDIO
            DUMMYCAMERA DUMMYVIDEO EXAMPLES EXAMPLES_LINK_SHARED FOREGROUNDING_SIGNAL
            GCC_ATOMICS GPU_DXVK PULSEAUDIO PIPEWIRE RENDER SENSOR
            IBUS INSTALL INSTALL_TESTS JACK JACK_SHARED KMSDRM KMSDRM_SHARED LASX LIBICONV
            LIBUDEV LIBURING LSX METAL MMX OFFSCREEN OPENGL OPENGLES OPENVR OSS RENDER_D3D
            RENDER_D3D11 RENDER_D3D12 RENDER_GPU RENDER_METAL RENDER_VULKAN ROCKCHIP RPATH
            RPI SNDIO SNDIO_SHARED SYSTEM_ICONV TESTS
            TESTS_LINK_SHARED UNINSTALL VIRTUAL_JOYSTICK VIVANTE VULKAN WASAPI
        )
        foreach(FEATURE ${SDL_DISABLED_FEATURES})
            list(APPEND SDL_OPTIONS "SDL_${FEATURE} OFF")
        endforeach()
    endif()

    if(APPLE)
        list(APPEND SDL_OPTIONS "SDL_COCOA ON")
    endif()

    if(RAFX_STATIC_SDL)
        list(APPEND SDL_OPTIONS "SDL_STATIC ON" "SDL_SHARED OFF")
    else()
        list(APPEND SDL_OPTIONS "SDL_STATIC OFF" "SDL_SHARED ON")
    endif()

    CPMAddPackage(
        NAME SDL3
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG release-3.4.0
        GIT_SHALLOW TRUE
        OPTIONS ${SDL_OPTIONS}
    )

    if(RAFX_STATIC_SDL)
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3)
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE SDL3::SDL3-shared)
    endif()
endif()

if(BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE RAFX_EXPORTS)
else()
    target_compile_definitions(${PROJECT_NAME} PUBLIC RAFX_STATIC)
endif()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    ${COMPILE_DEFINITIONS}
    ${BACKEND_DEFINES}
)
target_compile_options(${PROJECT_NAME} PRIVATE ${COMPILE_OPTIONS})
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "${PROJECT_NAME}")

# if(MSVC)
#     include(CheckLinkerFlag)
#     check_linker_flag(CXX /NOEXP HAS_NOEXP)
#
#     set(LINK_OPTIONS /NOIMPLIB)
#     if(HAS_NOEXP)
#         set(LINK_OPTIONS ${LINK_OPTIONS} /NOEXP)
#     endif()
#
#     target_link_options(${PROJECT_NAME} PUBLIC ${LINK_OPTIONS})
# endif()

# imgui (only used in examples)
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${IMGUI_DIR})

function(add_example NAME SOURCE_FILE)
    add_executable(${NAME} ${SOURCE_FILE})
    target_link_libraries(${NAME} PRIVATE ${PROJECT_NAME} imgui)
    target_include_directories(${NAME} PRIVATE thirdparty)
    target_compile_options(${NAME} PRIVATE ${COMPILE_OPTIONS})
    set_target_properties(${NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples"
    )
endfunction()

function(add_cpp_example NAME SOURCE_FILE)
    add_example(${NAME} ${SOURCE_FILE})
    target_compile_features(${NAME} PRIVATE cxx_std_20)
endfunction()

if(RAFX_BUILD_EXAMPLES)
    add_cpp_example(triangle examples/triangle.cc)
    add_example(c_triangle examples/triangle.c)
    add_cpp_example(cube examples/cube.cc)
    add_cpp_example(texcube examples/texcube.cc)
    add_cpp_example(compute_triangle examples/compute_triangle.cc)
    add_cpp_example(compute_boids examples/compute_boids.cc)
    add_cpp_example(compute_voronoi examples/compute_voronoi.cc)
    add_cpp_example(denoise examples/denoise.cc)
    add_cpp_example(mesh_triangle examples/mesh_triangle.cc)
    add_cpp_example(rt_triangle examples/rt_triangle.cc)
    add_cpp_example(rt_boxes examples/rt_boxes.cc)
    add_cpp_example(upscaler examples/upscaler.cc)
    add_cpp_example(async_compute examples/async_compute.cc)
    add_cpp_example(bloom examples/bloom.cc)
    add_cpp_example(low_latency examples/low_latency.cc)
    add_cpp_example(shadow_mapping examples/shadow_mapping.cc)
endif()

install(TARGETS ${PROJECT_NAME} NRI NRD NRDIntegration
        DESTINATION .)
install(FILES $<TARGET_FILE:slang::slang> DESTINATION .)
