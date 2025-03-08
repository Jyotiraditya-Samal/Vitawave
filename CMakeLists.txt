cmake_minimum_required(VERSION 3.0)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(VitaWave C CXX)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaWave")
set(VITA_TITLEID  "VWAVE0001")
set(VITA_VERSION  "00.90")

add_executable(VitaWave
  src/main.c
)

target_include_directories(VitaWave PRIVATE include)

target_link_libraries(VitaWave
  vita2d
  SceDisplay_stub
  SceCtrl_stub
  SceAudio_stub
  SceSysmodule_stub
  SceGxm_stub
  SceCommonDialog_stub
  freetype
  png
  jpeg
  z
  m
  c
)

vita_create_self(VitaWave.self VitaWave)
vita_create_vpk(VitaWave.vpk ${VITA_TITLEID} VitaWave.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
)
