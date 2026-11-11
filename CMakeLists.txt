cmake_minimum_required(VERSION 3.25)

project(StimFlow VERSION 0.1.0 LANGUAGES CXX)

if (NOT MSVC)
  message(FATAL_ERROR "StimFlow 目前只支持 MSVC 工具链（需要 Windows SDK 与 Direct3D 11）。")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 单文件绿色版：静态链接 CRT，不依赖 VC++ 运行库
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(STIMFLOW_WARNINGS_AS_ERRORS "把编译器警告当作错误" OFF)

add_executable(stimflow WIN32
  src/main.cpp
  src/app/Application.cpp
  src/app/FolderPicker.cpp
  src/app/Settings.cpp
  src/app/CacheDialog.rc
  src/cache/ThumbnailCache.cpp
  src/gfx/Hr.cpp
  src/gfx/Renderer.cpp
  src/gfx/Texture.cpp
  src/gfx/TextRenderer.cpp
  src/image/FolderLibrary.cpp
  src/image/WicDecoder.cpp
  src/input/Gamepad.cpp
  src/audio/ClickPlayer.cpp
)

set_target_properties(stimflow PROPERTIES
  OUTPUT_NAME "StimFlow"
)

# ---- 资源：音效 RCDATA + 版本信息 ----

file(GLOB AUDIO_WAVS "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/click_*.wav")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${AUDIO_WAVS})

set(AUDIO_RC "${CMAKE_CURRENT_BINARY_DIR}/stimflow_audio.rc")
set(AUDIO_RC_CONTENT "#include <windows.h>\n")
foreach(i RANGE 1 12)
  if (i LESS 10)
    set(audio_name "0${i}")
  else()
    set(audio_name "${i}")
  endif()
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio/click_${audio_name}.wav" audio_path)
  string(APPEND AUDIO_RC_CONTENT "${i} RCDATA \"${audio_path}\"\n")
endforeach()
file(WRITE "${AUDIO_RC}" "${AUDIO_RC_CONTENT}")

set(VERSION_RC "${CMAKE_CURRENT_BINARY_DIR}/stimflow_version.rc")
file(WRITE "${VERSION_RC}" [=[
#include <windows.h>

VS_VERSION_INFO VERSIONINFO
FILEVERSION 0,1,0,0
PRODUCTVERSION 0,1,0,0
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "StimFlow contributors"
      VALUE "FileDescription", "StimFlow"
      VALUE "FileVersion", "0.1.0"
      VALUE "InternalName", "StimFlow"
      VALUE "LegalCopyright", "Copyright (c) 2026 StimFlow contributors"
      VALUE "OriginalFilename", "StimFlow.exe"
      VALUE "ProductName", "StimFlow"
      VALUE "ProductVersion", "0.1.0"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
]=])

target_sources(stimflow PRIVATE "${AUDIO_RC}" "${VERSION_RC}")

# ---- 着色器：构建时用 fxc 编译，运行时不再需要 d3dcompiler_47.dll ----

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/gfx/Shaders.hlsl")
set(SHADER_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${SHADER_GEN_DIR}")

find_program(FXC_EXECUTABLE fxc
  HINTS
    "$ENV{WindowsSdkDir}/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
    "C:/Program Files (x86)/Windows Kits/10/bin/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/x64"
)

if (NOT FXC_EXECUTABLE)
  file(GLOB FXC_CANDIDATES "C:/Program Files (x86)/Windows Kits/10/bin/10.0.*/x64/fxc.exe")
  if (FXC_CANDIDATES)
    list(SORT FXC_CANDIDATES)
    list(GET FXC_CANDIDATES -1 FXC_EXECUTABLE)
  endif()
endif()

if (NOT FXC_EXECUTABLE)
  message(FATAL_ERROR "找不到 fxc.exe。请安装 Windows SDK（含 DirectX 着色器编译器）。")
endif()

set(SHADER_HEADERS "")
function(add_shader name entry target)
  add_custom_command(
    OUTPUT "${SHADER_GEN_DIR}/${name}.h"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ${target} /E ${entry}
            /Fh "${SHADER_GEN_DIR}/${name}.h" /Vn ${name} "${SHADER_SOURCE}"
    DEPENDS "${SHADER_SOURCE}"
    COMMENT "fxc ${name}"
    VERBATIM
  )
  list(APPEND SHADER_HEADERS "${SHADER_GEN_DIR}/${name}.h")
  set(SHADER_HEADERS "${SHADER_HEADERS}" PARENT_SCOPE)
endfunction()

add_shader(kBackgroundVS BackgroundVS vs_5_0)
add_shader(kBackgroundPS BackgroundPS ps_5_0)
add_shader(kCardVS VSMain vs_5_0)
add_shader(kCardPS CardPS ps_5_0)
add_shader(kReflectionPS ReflectionPS ps_5_0)
add_shader(kShadowPS ShadowPS ps_5_0)
add_shader(kTextPS TextPS ps_5_0)
add_shader(kInspectPS InspectPS ps_5_0)

add_custom_target(stimflow_shaders DEPENDS ${SHADER_HEADERS})
add_dependencies(stimflow stimflow_shaders)

target_compile_definitions(stimflow PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

target_include_directories(stimflow PRIVATE
  "${SHADER_GEN_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_options(stimflow PRIVATE
  /W4
  /permissive-
  /utf-8
  /Zc:__cplusplus
  /EHsc
)

if (STIMFLOW_WARNINGS_AS_ERRORS)
  target_compile_options(stimflow PRIVATE /WX)
endif()

target_link_libraries(stimflow PRIVATE
  d3d11
  dxgi
  winmm
  shell32
  windowscodecs
  ole32
  shlwapi
  uuid
  bcrypt
  crypt32
  gdi32
  xinput
)
