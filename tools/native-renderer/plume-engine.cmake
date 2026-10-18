# The D3D12 backend of the native renderer: Plume at a pinned revision. A supplied
# checkout is mandatory (NATIVE_RENDERER_PLUME_SOURCE), clean at the pinned commits.
set(NATIVE_RENDERER_PLUME_SOURCE "" CACHE PATH "Clean Plume checkout at the accepted revision")
if(NOT NATIVE_RENDERER_PLUME_SOURCE)
    message(FATAL_ERROR "Native bootstrap requires NATIVE_RENDERER_PLUME_SOURCE")
endif()
find_package(Git REQUIRED)
foreach(dep IN ITEMS plume allocator)
    if(dep STREQUAL "plume")
        set(dep_dir "${NATIVE_RENDERER_PLUME_SOURCE}")
        set(dep_commit d72379344dacd3dbf9f810f92ddc87e6de1845b1)
    else()
        set(dep_dir "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator")
        set(dep_commit 9ef66bc14edd10dee0de3a545b98578363552f66)
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dep_dir}" rev-parse HEAD
        OUTPUT_VARIABLE dep_head OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dep_dir}" status --porcelain --untracked-files=no
        OUTPUT_VARIABLE dep_changes OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    if(NOT dep_head STREQUAL dep_commit OR NOT dep_changes STREQUAL "")
        message(FATAL_ERROR "${dep} source must be clean at ${dep_commit}")
    endif()
endforeach()
# Compiled from the upstream sources, unchanged.
add_library(native_renderer_plume STATIC
    "${NATIVE_RENDERER_PLUME_SOURCE}/plume_d3d12.cpp"
    "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator/src/D3D12MemAlloc.cpp")
target_include_directories(native_renderer_plume PUBLIC "${NATIVE_RENDERER_PLUME_SOURCE}"
    "${NATIVE_RENDERER_PLUME_SOURCE}/contrib/D3D12MemoryAllocator/include")
target_compile_features(native_renderer_plume PUBLIC cxx_std_17)
target_compile_definitions(native_renderer_plume PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(native_renderer_plume PUBLIC d3d12 dxgi dxguid)
