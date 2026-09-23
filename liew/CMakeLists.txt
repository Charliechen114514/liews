# Monolithic DLL façade: GN stays static, but its complete closure is absorbed
# here once so every consumer links only the small import library.
add_library(liew SHARED app.cc)
add_dependencies(liew liew-tree)  # gen/ headers and bridge inputs must exist

# PUBLIC: host exes share the same recipe; the repo root resolves "liew/*.h"
# where the GN tree used a junction.
# The plain-PUBLIC (-I) rank must beat the SYSTEM (-imsvc) rank, or the
# MSVC/SDK STL shadows the tree's libc++ and std::__Cr decorations stop
# matching the tree objs at link time.
target_include_directories(liew PUBLIC
  ${CMAKE_SOURCE_DIR} ${LIEW_CXX_INCLUDES})
target_include_directories(liew SYSTEM PUBLIC ${LIEW_CXX_SYSTEM_INCLUDES})
target_compile_options(liew PUBLIC ${LIEW_CXX_OPTIONS})
target_compile_definitions(liew PRIVATE LIEW_IMPLEMENTATION)

# Quoted-value macros are redefined here with absolute paths (nested quotes
# cannot survive the Windows command line; the bridge strips them).
target_compile_definitions(liew PUBLIC
  SK_USER_CONFIG_HEADER="${CMAKE_SOURCE_DIR}/ui.views/skia/config/SkUserConfig.h"
  SK_GPU_WORKAROUNDS_HEADER="${LIEWS_TREE_OUT}/gen/gpu/config/gpu_driver_bug_workaround_autogen.h")

# The GN closure is private implementation of liew.dll. Consumers receive only
# liew.lib through the imported SDK target and never expand this response file.
target_link_options(liew PRIVATE
  "@${LIEW_LINK_RSP}" ${LIEW_LINK_LIBPATHS} ${LIEW_LINK_DEFAULTLIBS})
target_link_libraries(liew PRIVATE ${LIEW_LINK_SYSTEM_LIBS})
