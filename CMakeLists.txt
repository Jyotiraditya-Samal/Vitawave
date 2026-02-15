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
  src/file_browser.c
  src/ui.c
  src/audio_engine.c
  src/decoder_mp3.c
  src/decoder_flac.c
  src/decoder_ogg.c
  src/metadata.c
  src/playlist.c
  src/equalizer.c
  src/kiss_fft.c
  src/visualizer.c
  src/theme.c
  src/font_renderer.c
  src/noto.c
  src/noto_compat.c
  src/media_db.c
  src/vita_sqlite.c
  sqlite/sqlite3.c
)

set_source_files_properties(sqlite/sqlite3.c PROPERTIES COMPILE_FLAGS
  "-DSQLITE_OS_OTHER=1 -DSQLITE_THREADSAFE=0 -DSQLITE_TEMP_STORE=3")

target_include_directories(VitaWave PRIVATE include sqlite)

target_link_libraries(VitaWave
  vita2d
  SceDisplay_stub
  SceCtrl_stub
  SceAudio_stub
  SceAudioOut_stub
  SceAppMgr_stub
  SceShellSvc_stub
  SceAppUtil_stub
  SceSysmodule_stub
  SceGxm_stub
  SceCommonDialog_stub
  mpg123
  vorbisfile
  vorbis
  ogg
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
