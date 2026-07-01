cmake_minimum_required(VERSION 3.25)
project(arca LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Single bin dir so executables sit next to arca_core.dll + libmpv-2.dll.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
  add_compile_options(/W4 /utf-8)
endif()

# Vendored libmpv (fork build; see third_party/mpv/PROVENANCE.md + ADR-002).
set(ARCA_MPV_DIR ${CMAKE_SOURCE_DIR}/third_party/mpv)
if(NOT EXISTS ${ARCA_MPV_DIR}/lib/mpv.lib)
  message(FATAL_ERROR "Vendored libmpv missing - run third_party/mpv/refresh.ps1 first")
endif()
add_library(libmpv SHARED IMPORTED GLOBAL)
set_target_properties(libmpv PROPERTIES
  IMPORTED_LOCATION ${ARCA_MPV_DIR}/bin/libmpv-2.dll
  IMPORTED_IMPLIB ${ARCA_MPV_DIR}/lib/mpv.lib
  INTERFACE_INCLUDE_DIRECTORIES ${ARCA_MPV_DIR}/include)

add_subdirectory(core)
add_subdirectory(tools/hdr-verify)
add_subdirectory(tools/lib-verify)
add_subdirectory(tools/db-seed)
add_subdirectory(tools/media-verify)
