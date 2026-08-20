cmake_minimum_required(VERSION 3.15)
set(PROJECT_NAME "audioplayers_windows")
set(WIL_VERSION "1.0.210803.1")
project(${PROJECT_NAME} LANGUAGES CXX)
include(FetchContent)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "${PROJECT_NAME}_plugin")

set(GSTREAMER_PATH $ENV{GSTREAMER_ROOT_X86})
message("gst path: ${GSTREAMER_PATH}")

FetchContent_Declare(nuget
  URL "https://dist.nuget.org/win-x86-commandline/v6.5.0/nuget.exe"
  URL_HASH SHA256=d5fce5185de92b7356ea9264b997a620e35c6f6c3c061e471e0dc3a84b3d74fd
  DOWNLOAD_NO_EXTRACT true
)

find_program(NUGET nuget)
if (NOT NUGET)
    message(STATUS "Nuget.exe not found, trying to download or use cached version.")
    FetchContent_MakeAvailable(nuget)
    set(NUGET ${nuget_SOURCE_DIR}/nuget.exe)
endif()

execute_process(COMMAND
    ${NUGET} install Microsoft.Windows.ImplementationLibrary -Version ${WIL_VERSION} -ExcludeVersion -OutputDirectory ${CMAKE_BINARY_DIR}/packages
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    RESULT_VARIABLE ret)
if (NOT ret EQUAL 0)
    message(FATAL_ERROR "Failed to install nuget package Microsoft.Windows.ImplementationLibrary.${WIL_VERSION}")
endif()

if(MSVC)
	message("msvc=true")
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_library(${PLUGIN_NAME} SHARED
  "audioplayers_windows_plugin.cpp"
  "audio_player.h"
  "audio_player.cc"
  "event_stream_handler.h"
)
apply_standard_settings(${PLUGIN_NAME})
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)

# gstreamer
# include_directories(
#   "${GSTREAMER_PATH}/include/gstreamer-1.0"
#   "${GSTREAMER_PATH}/include/glib-2.0"
#   "${GSTREAMER_PATH}/lib/glib-2.0/include"
#   ${CMAKE_CURRENT_SOURCE_DIR}
# )
if(DEFINED ENV{DEV})
	message("DEV environment variable found ('./build/windows/x64/runner/Debug'): $ENV{DEV}")
	add_definitions(-DD_GST_PLUGIN_PATH="./build/windows/x64/runner/Debug")
else()
	message("DEV environment variable not found ('./')")
	add_definitions(-DD_GST_PLUGIN_PATH="./")
endif()

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${GSTREAMER_PATH}/include/gstreamer-1.0"
  "${GSTREAMER_PATH}/include/glib-2.0"
  "${GSTREAMER_PATH}/lib/glib-2.0/include"
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_directories(${PLUGIN_NAME} PRIVATE
  "${GSTREAMER_PATH}/lib"
)
target_link_libraries(${PLUGIN_NAME} PRIVATE gstreamer-1.0 glib-2.0 gobject-2.0 gstpbutils-1.0)
# gstreamer end

target_link_libraries(${PLUGIN_NAME} PRIVATE ${CMAKE_BINARY_DIR}/packages/Microsoft.Windows.ImplementationLibrary/build/native/Microsoft.Windows.ImplementationLibrary.targets)
target_link_libraries(${PLUGIN_NAME} PRIVATE Mfplat windowsapp)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)
target_link_libraries(${PLUGIN_NAME} PRIVATE shlwapi)

# List of absolute paths to libraries that should be bundled with the plugin
set(audioplayers_windows_bundled_libraries
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/avcodec-61.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/avfilter-10.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/avformat-61.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/avutil-59.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/bz2.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/ffi-7.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gio-2.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/glib-2.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gmodule-2.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gobject-2.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstapp-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstaudio-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstbase-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstpbutils-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstreamer-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstriff-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstrtp-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gsttag-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/gstvideo-1.0-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/intl-8.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/libmpg123-1.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/orc-0.4-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/pcre2-8-0.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/swresample-5.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/swscale-8.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/z-1.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstapp.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstaudioconvert.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstaudioparsers.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstaudioresample.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstautodetect.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstcdg.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstcoreelements.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstequalizer.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstid3demux.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstisomp4.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstlibav.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstmpg123.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstplayback.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstsubparse.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gsttypefindfunctions.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstvolume.dll"
  "${CMAKE_CURRENT_SOURCE_DIR}/dlls/lib/gstreamer-1.0/gstwasapi2.dll"

  PARENT_SCOPE
)
