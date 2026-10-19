# The backends of the native renderer: Plume at a pinned revision. A supplied
# checkout is mandatory (NATIVE_RENDERER_PLUME_SOURCE), clean at the pinned commits.
# native_renderer_plume is the D3D12 backend (NARUTORISE_NATIVE_D3D12) and
# native_renderer_plume_vulkan the Vulkan one (NARUTORISE_NATIVE_VULKAN). Only the
# submodules of the enabled backends are checked, so a D3D12 build needs no
# Vulkan sources.
set(NATIVE_RENDERER_PLUME_SOURCE "" CACHE PATH "Clean Plume checkout at the accepted revision")
if(NOT NATIVE_RENDERER_PLUME_SOURCE)
    message(FATAL_ERROR "Native bootstrap requires NATIVE_RENDERER_PLUME_SOURCE")
endif()
find_package(Git REQUIRED)
set(dep_names plume)
set(dep_dirs .)
set(dep_commits d72379344dacd3dbf9f810f92ddc87e6de1845b1)
if(NARUTORISE_NATIVE_D3D12)
    list(APPEND dep_names allocator)
    list(APPEND dep_dirs contrib/D3D12MemoryAllocator)
    list(APPEND dep_commits 9ef66bc14edd10dee0de3a545b98578363552f66)
endif()
if(NARUTORISE_NATIVE_VULKAN)
    list(APPEND dep_names volk Vulkan-Headers VulkanMemoryAllocator)
    list(APPEND dep_dirs contrib/volk contrib/Vulkan-Headers contrib/VulkanMemoryAllocator)
    list(APPEND dep_commits be3dbd49bf77052665e96b6c7484af855e7e5f67 2fa203425eb4af9dfc6b03f97ef72b0b5bcb8350
         29b35ea4232688c0f42cdff0c10848290760a417)
endif()
foreach(dep IN ZIP_LISTS dep_names dep_dirs dep_commits)
    set(dep_dir "${NATIVE_RENDERER_PLUME_SOURCE}/${dep_1}")
    # An uninitialized submodule is an empty folder, where rev-parse answers for Plume.
    file(GLOB dep_files "${dep_dir}/*")
    if(NOT dep_files)
        message(FATAL_ERROR "${dep_0} source is missing: git -C \"${NATIVE_RENDERER_PLUME_SOURCE}\" "
                            "submodule update --init ${dep_1}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dep_dir}" rev-parse HEAD
        OUTPUT_VARIABLE dep_head OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dep_dir}" status --porcelain --untracked-files=no
        OUTPUT_VARIABLE dep_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT dep_head STREQUAL dep_2 OR NOT dep_changes STREQUAL "")
        message(FATAL_ERROR "${dep_0} source must be clean at ${dep_2}")
    endif()
endforeach()
# Compiled from the upstream sources, unchanged.
if(NARUTORISE_NATIVE_D3D12)
    add_library(native_renderer_plume STATIC
        "${NATIVE_RENDERER_PLUME_SOURCE}/plume_d3d12.cpp"
        "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp")
    target_include_directories(native_renderer_plume PUBLIC "${NATIVE_RENDERER_PLUME_SOURCE}"
        "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator/include")
    target_compile_features(native_renderer_plume PUBLIC cxx_std_17)
    target_compile_definitions(native_renderer_plume PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(native_renderer_plume PUBLIC d3d12 dxgi dxguid)
endif()
# volk and the Vulkan Memory Allocator are implemented inside plume_vulkan.cpp;
# volk loads the system loader (vulkan-1.dll) at run time, so nothing is linked.
if(NARUTORISE_NATIVE_VULKAN)
    add_library(native_renderer_plume_vulkan STATIC "${NATIVE_RENDERER_PLUME_SOURCE}/plume_vulkan.cpp")
    target_include_directories(native_renderer_plume_vulkan PUBLIC "${NATIVE_RENDERER_PLUME_SOURCE}"
        "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/volk"
        "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/Vulkan-Headers/include"
        "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/VulkanMemoryAllocator/include")
    target_compile_features(native_renderer_plume_vulkan PUBLIC cxx_std_17)
    target_compile_definitions(native_renderer_plume_vulkan PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
