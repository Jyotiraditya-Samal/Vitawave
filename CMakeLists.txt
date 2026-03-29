cmake_minimum_required(VERSION 3.0)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your VitaSDK installation")
  endif()
endif()

project(VitaWave C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaWave")
set(VITA_TITLEID  "VWAV00001")
set(VITA_VERSION  "01.00")

set(VITA_MKSFOEX_FLAGS
  "-d ATTRIBUTE=17338376"
  "-d ATTRIBUTE_MINOR=0"
  "-s CATEGORY=gd"
  "-s CONTENT_ID=HB0000-VWAV00001_00-VITAWAVEPLAYER00"
  "-d EBOOT_APP_MEMSIZE=67108864"
  "-d EBOOT_PHY_MEMSIZE=4259840"
  "-d GC_RO_SIZE=0"
  "-d GC_RW_SIZE=0"
  "-d PARENTAL_LEVEL=1"
  "-s PSP2_DISP_VER=03.600"
  "-d PSP2_SYSTEM_VER=56623104"
  "-d REGION_DENY=0"
  "-d SAVEDATA_MAX_SIZE=0"
  "-s VERSION=01.00"
)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -O2 -std=c99")

include_directories(
  include
  sqlite
  ${VITASDK}/arm-vita-eabi/include
  ${VITASDK}/arm-vita-eabi/include/freetype2
)

set(SOURCES
  src/main.c
  src/audio_engine.c
  src/decoder_mp3.c
  src/decoder_flac.c
  src/decoder_ogg.c
  src/file_browser.c
  src/media_db.c
  src/metadata.c
  src/playlist.c
  src/ui.c
  src/visualizer.c
  src/equalizer.c
  src/kiss_fft.c        # bundled kissfft — no external package needed
  src/vita_shims.c      # POSIX stubs not provided by VitaSDK
  src/vita_sqlite.c     # custom SQLite VFS using sceIo*
  src/font_renderer.c   # custom FreeType glyph atlas renderer (point filter)
  src/noto_compat.c     # default Noto font path list
  src/theme.c           # Winamp-skin-style theme system
  src/gifdec.c          # GIF decoder (FILE*-based, Vita-compatible)
  sqlite/sqlite3.c      # bundled SQLite amalgamation
)

# SQLite flags apply ONLY to the bundled sqlite3.c, not to the rest of the app
set_source_files_properties(sqlite/sqlite3.c PROPERTIES
  COMPILE_FLAGS "-DSQLITE_OS_OTHER=1 -DSQLITE_TEMP_STORE=3 -DSQLITE_THREADSAFE=0 -w"
)

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
  vita2d
  SceDisplay_stub
  SceGxm_stub
  SceCtrl_stub
  SceAudio_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  ScePower_stub
  SceCommonDialog_stub
  SceAppMgr_stub
  SceAppUtil_stub
  SceShellSvc_stub
  SceRtc_stub
  ScePgf_stub
  ScePvf_stub
  mpg123
  vorbisfile
  vorbis
  ogg
  png
  freetype
  jpeg
  bz2
  z
  m
  c
)

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/icons/icon0.png    sce_sys/icon0.png
  FILE assets/icons/bg.png       sce_sys/livearea/contents/bg.png
  FILE assets/icons/startup.png  sce_sys/livearea/contents/startup.png
  FILE assets/livearea/template.xml  sce_sys/livearea/contents/template.xml
  FILE assets/fonts/NotoSans-Regular.ttf    fonts/NotoSans-Regular.ttf
  FILE assets/fonts/NotoSansJP-Regular.ttf  fonts/NotoSansJP-Regular.ttf
  FILE assets/fonts/NotoSansKR-Regular.ttf  fonts/NotoSansKR-Regular.ttf
  FILE assets/fonts/NotoSansSC-Regular.ttf  fonts/NotoSansSC-Regular.ttf
)
