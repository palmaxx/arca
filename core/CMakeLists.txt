add_library(arca_core SHARED
  src/engine/engine.cpp
  src/engine/render_d3d11.cpp
)

target_include_directories(arca_core PUBLIC include)
target_compile_definitions(arca_core PRIVATE ARCA_BUILD)
# Boundary rule: dependencies link PRIVATE and must not leak through the ABI.
target_link_libraries(arca_core PRIVATE libmpv d3d11 dxgi)

# Runtime dependencies next to the binaries (libmpv + its vendored MSYS2
# closure; see third_party/mpv/refresh.ps1).
add_custom_command(TARGET arca_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${ARCA_MPV_DIR}/bin
          $<TARGET_FILE_DIR:arca_core>)
