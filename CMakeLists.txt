# liews top-level CMake: compiles own code (liew/, examples/) and drives the
# ui.views tree (GN+ninja via scripts/build.py). Own code never enters the GN
# graph; compile flags and the link closure are bridged from GN outputs.
#
#   cmake -B build                # configure (generator is your choice)
#   cmake --build build           # tree(GN) -> liew/examples(CMake) -> compdb -> deploy
#   cmake --build build --target liew-smoke
#
# Constraint: own code must compile with clang-cl, same as the tree.
cmake_minimum_required(VERSION 3.23)

find_program(LIEWS_PYTHON NAMES python python3 REQUIRED)

if(NOT DEFINED CMAKE_CXX_COMPILER)
  find_program(LIEWS_CLANG_CL NAMES clang-cl REQUIRED)
  set(CMAKE_CXX_COMPILER "${LIEWS_CLANG_CL}")
endif()

project(liews VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_COMPILER MATCHES "clang-cl(\\.exe)?$")
  message(FATAL_ERROR
    "own code requires clang-cl (got ${CMAKE_CXX_COMPILER}); "
    "pick a Ninja kit or the VS generator with -T ClangCL.")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-config generators are forced to Release: the tree is /O2 + /MT.
if(NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")  # tree is static CRT (/MT)

# lld-link embeds manifests natively; bypass cmake's vs_link_exe wrapper,
# which expands @rsp into one mega command line and dies (ENAMETOOLONG).
set(CMAKE_CXX_LINK_EXECUTABLE
  "<CMAKE_LINKER> /nologo <OBJECTS> <LINK_FLAGS> /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> /version:<TARGET_VERSION_MAJOR>.<TARGET_VERSION_MINOR> <LINK_LIBRARIES>")
# Same direct-link rule for the monolithic DLL. The default ClangCL rule uses
# vs_link_dll, which expands the 5000-input @rsp and exceeds Windows limits.
set(CMAKE_CXX_CREATE_SHARED_LIBRARY
  "<CMAKE_LINKER> /nologo <OBJECTS> <LINK_FLAGS> /dll /out:<TARGET> /implib:<TARGET_IMPLIB> /pdb:<TARGET_PDB> <LINK_LIBRARIES>")

set(LIEWS_TREE_OUT "${CMAKE_SOURCE_DIR}/build/Release")
# Bridge files always live in the repo's build/ dir, written by build.py.
set(LIEWS_FLAGS_CMAKE "${CMAKE_SOURCE_DIR}/build/liew_flags.cmake")
set(LIEWS_LINK_CMAKE "${CMAKE_SOURCE_DIR}/build/liew_link.cmake")

# First configure generates the bridge; later refreshes happen in liew-tree.
if(NOT EXISTS "${LIEWS_FLAGS_CMAKE}" OR NOT EXISTS "${LIEWS_LINK_CMAKE}")
  execute_process(
    COMMAND ${LIEWS_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/build.py bridge
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    RESULT_VARIABLE _bridge_rc)
  if(NOT _bridge_rc EQUAL 0)
    message(FATAL_ERROR "build.py bridge failed (exit=${_bridge_rc})")
  endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${LIEWS_FLAGS_CMAKE}" "${LIEWS_LINK_CMAKE}")
include("${LIEWS_FLAGS_CMAKE}" REQUIRED)
include("${LIEWS_LINK_CMAKE}" REQUIRED)

# --- SDK 导出: configure 期产 build/cmake/liew-config*.cmake 供 find_package(liew) ---
# SDK = 本仓库检出 + 本 build 目录（绝对路径烘焙; 不做 install、不做重定位）。
# 须晚于桥文件 include（要烘焙 LIEW_LINK_RSP 等现值）, 早于 examples/（其以
# find_package 消费本产物）。liew.lib 路径按单配置（Release）烘焙。
include(CMakePackageConfigHelpers)
set(LIEW_SDK_DIR "${CMAKE_BINARY_DIR}/cmake")
set(LIEW_SOURCE_DIR "${CMAKE_SOURCE_DIR}")
set(LIEW_TREE_OUT "${LIEWS_TREE_OUT}")
set(LIEW_LIBRARY "${CMAKE_BINARY_DIR}/liew/liew.lib")
set(LIEW_RUNTIME "${CMAKE_BINARY_DIR}/liew/liew.dll")
set(LIEW_FLAGS_CMAKE "${LIEWS_FLAGS_CMAKE}")
configure_package_config_file(
  "${CMAKE_SOURCE_DIR}/scripts/cmake/liew-config.cmake.in"
  "${LIEW_SDK_DIR}/liew-config.cmake"
  INSTALL_DESTINATION "${LIEW_SDK_DIR}"  # 必填; 构建期包, 包就在这里
  PATH_VARS LIEW_SOURCE_DIR LIEW_TREE_OUT LIEW_LIBRARY LIEW_RUNTIME
            LIEW_FLAGS_CMAKE)
write_basic_package_version_file(
  "${LIEW_SDK_DIR}/liew-config-version.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/scripts/cmake/liew-config.cmake.in")
if(CMAKE_CONFIGURATION_TYPES)
  message(WARNING "liew SDK export assumes single-config; liew.lib path is per-config")
endif()

add_custom_target(liew-tree
  COMMAND ${LIEWS_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/build.py build
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "liews: build.py build (ui.views tree, GN + ninja)"
  USES_TERMINAL VERBATIM)

add_custom_target(liew-compdb
  COMMAND ${LIEWS_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/build.py compdb
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "liews: build.py compdb (merge tree entries into build/compile_commands.json)"
  USES_TERMINAL VERBATIM)

# ALL sits on the pipeline tail: cmake --build runs build -> compdb -> deploy.
add_custom_target(liew-deploy ALL
  COMMAND ${LIEWS_PYTHON} ${CMAKE_SOURCE_DIR}/scripts/build.py deploy
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMENT "liews: build.py deploy (minimal runtime set -> build/deploy)"
  USES_TERMINAL VERBATIM)
add_dependencies(liew-deploy liew-compdb)

add_subdirectory(liew)
add_subdirectory(examples)
add_dependencies(liew-deploy liew-smoke)
