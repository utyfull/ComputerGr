cmake_minimum_required(VERSION 3.24)
project(lab1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(UNICODE _UNICODE)

set(DXC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/dxc/dxc.exe" CACHE FILEPATH "Path to dxc.exe")
if(NOT EXISTS "${DXC_PATH}")
  message(FATAL_ERROR "DXC not found. Put dxc.exe in lab1/external/dxc/ or pass -DDXC_PATH=<full path>")
else()
  message(STATUS "Using DXC: ${DXC_PATH}")
endif()

function(compile_hlsl OUTVAR NAME SRC ENTRY PROFILE)
  set(OUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${NAME}.cso")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
  add_custom_command(
    OUTPUT "${OUT}"
    COMMAND "${DXC_PATH}" -T "${PROFILE}" -E "${ENTRY}" -Fo "${OUT}" "${SRC}"
    DEPENDS "${SRC}"
    COMMENT "Compiling ${NAME}"
    VERBATIM
  )
  set_source_files_properties("${OUT}" PROPERTIES GENERATED TRUE)
  set(${OUTVAR} ${${OUTVAR}} "${OUT}" PARENT_SCOPE)
endfunction()

include(FetchContent)
FetchContent_Declare(
  directxheaders
  GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers.git
  GIT_TAG v1.614.0
)
FetchContent_MakeAvailable(directxheaders)

add_library(dwin STATIC
  winui3d/src/dwin.cpp
)
target_include_directories(dwin PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/winui3d/include"
)

set(SHADERS)
compile_hlsl(SHADERS ConesVS       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Cones.hlsl" VSMain       vs_6_0)
compile_hlsl(SHADERS ConesPS       "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Cones.hlsl" PSMain       ps_6_0)
# НОВОЕ: вершинный шейдер depth-pass для shadow map
compile_hlsl(SHADERS ConesShadowVS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Cones.hlsl" VSShadowMain vs_6_0)

add_executable(D3D12Red WIN32
  main.cpp
  d3d12_core.cpp
  cone_scene.cpp
  ${SHADERS}
)

add_custom_command(TARGET D3D12Red POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:D3D12Red>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:D3D12Red>/shaders"
  COMMENT "Copy shaders next to exe"
)

target_include_directories(D3D12Red PRIVATE
  "${directxheaders_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/winui3d/include"
)

target_link_libraries(D3D12Red PRIVATE
  dwin
  d3d12 dxgi dxguid d3dcompiler comctl32
)

set_target_properties(D3D12Red PROPERTIES
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:D3D12Red>"
)
