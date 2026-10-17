# Copies the Xenos shader storage into the build output when the local folder
# has it. Runs at build time (cmake -P), so files added after configuring are
# bundled by the next build.
#   SOURCE: folder with 555307E5.xsh / 555307E5.rtv.d3d12.xpso
#   DEST:   shader_cache folder next to narutorise.exe
if(EXISTS "${SOURCE}")
    file(COPY "${SOURCE}/" DESTINATION "${DEST}")
else()
    message(STATUS "No Xenos shader storage in ${SOURCE}; none is bundled")
endif()
