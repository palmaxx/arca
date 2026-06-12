# Vendored SQLite amalgamation, compiled in (no DLL). Serialized threading.
add_library(arca_sqlite STATIC ${CMAKE_SOURCE_DIR}/third_party/sqlite/sqlite3.c)
target_include_directories(arca_sqlite PUBLIC ${CMAKE_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(arca_sqlite PRIVATE
  SQLITE_THREADSAFE=1 SQLITE_DQS=0 SQLITE_DEFAULT_FOREIGN_KEYS=1)
if(MSVC)
  # Amalgamation triggers benign /W4 noise; it is not our code to fix.
  target_compile_options(arca_sqlite PRIVATE /W0)
endif()

add_library(arca_core SHARED
  src/engine/engine.cpp
  src/engine/render_d3d11.cpp
  src/library/library.cpp
  src/library/online_index.cpp
)

target_include_directories(arca_core PUBLIC include)
target_compile_definitions(arca_core PRIVATE ARCA_BUILD)
# Boundary rule: dependencies link PRIVATE and must not leak through the ABI.
# NOTE (Day-0 hard seam): no networking library is linked anywhere in the
# core; the ONLINE pipeline is local indexing + a pending queue only.
target_link_libraries(arca_core PRIVATE libmpv d3d11 dxgi arca_sqlite)

# Runtime dependencies next to the binaries (libmpv + its vendored MSYS2
# closure; see third_party/mpv/refresh.ps1).
add_custom_command(TARGET arca_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
          ${ARCA_MPV_DIR}/bin
          $<TARGET_FILE_DIR:arca_core>)
