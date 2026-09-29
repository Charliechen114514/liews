# Monolithic DLL façade: GN stays static, but its complete closure is absorbed
# here once so every consumer links only the small import library.
add_library(liew SHARED
  app/app.cc
  app/liew_app_config_builder.cpp
  cxx_support.cc
  chrome_private/app/application_impl.cpp
  chrome_private/app/window.cpp
  chrome_private/app/window_manager.cpp
  chrome_private/render/compositor_runtime.cc
  chrome_private/render/direct_layer_tree_frame_sink.cc
  chrome_private/runtime/discardable_memory.cpp
  chrome_private/runtime/process_runtime.cpp
  chrome_private/runtime/task_runtime.cpp
  chrome_private/ui/accessibility_platform.cpp
  chrome_private/ui/liew_views_delegate.cpp
  chrome_private/ui/ui_toolkit.cpp
  widget/button.cc
  widget/checkbox.cc
  widget/combobox.cc
  widget/label.cc
  widget/progress_bar.cc
  widget/radio_button.cc
  widget/scroll_view.cc
  widget/slider.cc
  widget/text_field.cc
  widget/toggle_button.cc
  widget/tabbed_pane.cc
  widget/widget.cc
  window/dialog.cc
  window/menu.cc
)

add_dependencies(liew liew-tree) # gen/ headers and bridge inputs must exist

# Pin outputs to <build>/liew: the SDK export (repo root CMakeLists) and
# build.py deploy bake this absolute location; the default would drift with
# this file's subdirectory name.
set_target_properties(liew PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/liew"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/liew"
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/liew")

# PUBLIC: include/ resolves "liew/*.h" for consumers. The plain-PUBLIC (-I)
# rank must beat the SYSTEM (-imsvc) rank, or the MSVC/SDK STL shadows the
# tree's libc++ and std::__Cr decorations stop matching the tree objs at
# link time.
target_include_directories(liew PUBLIC
  ${CMAKE_SOURCE_DIR}/include ${LIEW_CXX_INCLUDES})
target_include_directories(liew SYSTEM PUBLIC ${LIEW_CXX_SYSTEM_INCLUDES})
# Private internal headers are included as "src/..." from the repo root.
target_include_directories(liew PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(liew PUBLIC ${LIEW_CXX_OPTIONS})
target_compile_definitions(liew PRIVATE LIEW_IMPLEMENTATION)

# Quoted-value macros are redefined here with absolute paths (nested quotes
# cannot survive the Windows command line; the bridge strips them).
target_compile_definitions(liew PUBLIC
  SK_USER_CONFIG_HEADER="${CMAKE_SOURCE_DIR}/ui.views/skia/config/SkUserConfig.h"
  SK_GPU_WORKAROUNDS_HEADER="${LIEWS_TREE_OUT}/gen/gpu/config/gpu_driver_bug_workaround_autogen.h")

# The GN closure is private implementation of liew.dll. Consumers receive only
# liew.lib through the imported SDK target and never expand this response file.
# Export libc++ support symbols that consumer TUs reference: our definition
# (verbose_abort) plus libc++-provided ones already in the closure
# (bad_function_call), which otherwise stay unexported inside liew.dll.
target_link_options(liew PRIVATE
  "/EXPORT:?__libcpp_verbose_abort@__Cr@std@@YAXPEBDZZ"
  "/EXPORT:??1bad_function_call@__Cr@std@@UEAA@XZ"
  "/EXPORT:?what@bad_function_call@__Cr@std@@UEBAPEBDXZ"
  "/DEBUG"
  "@${LIEW_LINK_RSP}" ${LIEW_LINK_LIBPATHS} ${LIEW_LINK_DEFAULTLIBS})
target_link_libraries(liew PRIVATE ${LIEW_LINK_SYSTEM_LIBS})
