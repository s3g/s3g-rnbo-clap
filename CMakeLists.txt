cmake_minimum_required(VERSION 3.20)

project(s3g_rnbo_clap VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(S3G_BUILD_CLAP "Build the RNBO CLAP wrapper" ON)
option(S3G_FETCH_CLAP "Fetch CLAP headers when S3G_CLAP_INCLUDE_DIR is empty" ON)
set(S3G_CLAP_INCLUDE_DIR "" CACHE PATH "Path containing clap/clap.h")
set(S3G_CLAP_GIT_REPOSITORY "https://github.com/free-audio/clap.git" CACHE STRING "CLAP git repository")
set(S3G_CLAP_GIT_TAG "1.2.6" CACHE STRING "CLAP git tag or commit")
set(S3G_RNBO_EXPORT_DIR "" CACHE PATH "Optional RNBO C++ export folder containing rnbo_source.cpp or a Max RNBO *.rnbopat.cpp source plus rnbo/RNBO.cpp")
set(S3G_RNBO_INPUT_CHANNELS "8" CACHE STRING "Fixed CLAP input channel count for the test wrapper")
set(S3G_RNBO_OUTPUT_CHANNELS "8" CACHE STRING "Fixed CLAP output channel count for the test wrapper")
set(S3G_RNBO_PLUGIN_SLUG "8ch_passthru" CACHE STRING "Short identifier used for the built CLAP bundle filename")
set(S3G_RNBO_PLUGIN_NAME "s3g RNBO 8ch Passthru" CACHE STRING "Human-readable CLAP bundle name")
set(S3G_RNBO_PLUGIN_ID "" CACHE STRING "Stable CLAP plugin id; defaults to org.s3g.s3g-rnbo-clap.<slug>")
set(S3G_RNBO_PLUGIN_DESCRIPTION "CLAP wrapper for an RNBO C++ export." CACHE STRING "CLAP plugin description")

if(S3G_RNBO_PLUGIN_ID)
  set(S3G_RNBO_EFFECTIVE_PLUGIN_ID "${S3G_RNBO_PLUGIN_ID}")
else()
  string(REPLACE "_" "-" S3G_RNBO_PLUGIN_ID_SUFFIX "${S3G_RNBO_PLUGIN_SLUG}")
  set(S3G_RNBO_EFFECTIVE_PLUGIN_ID "org.s3g.s3g-rnbo-clap.${S3G_RNBO_PLUGIN_ID_SUFFIX}")
endif()

add_executable(s3g_rnbo_smoke tests/smoke.cpp)
target_include_directories(s3g_rnbo_smoke PRIVATE src)

if(S3G_BUILD_CLAP)
  if(NOT S3G_CLAP_INCLUDE_DIR)
    if(NOT S3G_FETCH_CLAP)
      message(FATAL_ERROR "Set S3G_CLAP_INCLUDE_DIR or enable S3G_FETCH_CLAP")
    endif()
    FetchContent_Declare(
      clap
      GIT_REPOSITORY ${S3G_CLAP_GIT_REPOSITORY}
      GIT_TAG ${S3G_CLAP_GIT_TAG}
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(clap)
    set(S3G_CLAP_INCLUDE_DIR ${clap_SOURCE_DIR}/include)
  endif()

  if(NOT EXISTS "${S3G_CLAP_INCLUDE_DIR}/clap/clap.h")
    message(FATAL_ERROR "S3G_CLAP_INCLUDE_DIR must contain clap/clap.h: ${S3G_CLAP_INCLUDE_DIR}")
  endif()

  set(S3G_RNBO_SOURCES "")
  set(S3G_HAS_RNBO_EXPORT 0)
  if(S3G_RNBO_EXPORT_DIR)
    set(S3G_RNBO_PATCH_SOURCE "")
    if(EXISTS "${S3G_RNBO_EXPORT_DIR}/rnbo_source.cpp")
      set(S3G_RNBO_PATCH_SOURCE "${S3G_RNBO_EXPORT_DIR}/rnbo_source.cpp")
    else()
      file(GLOB S3G_RNBO_PATCH_SOURCES CONFIGURE_DEPENDS "${S3G_RNBO_EXPORT_DIR}/*.rnbopat.cpp")
      list(LENGTH S3G_RNBO_PATCH_SOURCES S3G_RNBO_PATCH_SOURCE_COUNT)
      if(S3G_RNBO_PATCH_SOURCE_COUNT EQUAL 1)
        list(GET S3G_RNBO_PATCH_SOURCES 0 S3G_RNBO_PATCH_SOURCE)
      elseif(S3G_RNBO_PATCH_SOURCE_COUNT GREATER 1)
        message(FATAL_ERROR "S3G_RNBO_EXPORT_DIR contains multiple *.rnbopat.cpp files; keep one export source or provide rnbo_source.cpp")
      endif()
    endif()

    if(S3G_RNBO_PATCH_SOURCE AND EXISTS "${S3G_RNBO_EXPORT_DIR}/rnbo/RNBO.cpp")
      set(S3G_HAS_RNBO_EXPORT 1)
      list(APPEND S3G_RNBO_SOURCES
        "${S3G_RNBO_PATCH_SOURCE}"
        "${S3G_RNBO_EXPORT_DIR}/rnbo/RNBO.cpp"
      )
    else()
      message(FATAL_ERROR "S3G_RNBO_EXPORT_DIR must contain rnbo_source.cpp or exactly one *.rnbopat.cpp plus rnbo/RNBO.cpp")
    endif()
  endif()

  add_library(s3g_rnbo_test_clap MODULE
    src/s3g_rnbo_test_clap.cpp
    ${S3G_RNBO_SOURCES}
  )

  target_include_directories(s3g_rnbo_test_clap PRIVATE
    ${S3G_CLAP_INCLUDE_DIR}
    src
  )

  if(S3G_HAS_RNBO_EXPORT)
    target_include_directories(s3g_rnbo_test_clap PRIVATE
      "${S3G_RNBO_EXPORT_DIR}/rnbo"
      "${S3G_RNBO_EXPORT_DIR}/rnbo/common"
    )
  endif()

  target_compile_definitions(s3g_rnbo_test_clap PRIVATE
    S3G_HAS_RNBO_EXPORT=${S3G_HAS_RNBO_EXPORT}
    S3G_RNBO_INPUT_CHANNELS=${S3G_RNBO_INPUT_CHANNELS}
    S3G_RNBO_OUTPUT_CHANNELS=${S3G_RNBO_OUTPUT_CHANNELS}
    S3G_RNBO_PLUGIN_ID="${S3G_RNBO_EFFECTIVE_PLUGIN_ID}"
    S3G_RNBO_PLUGIN_NAME="${S3G_RNBO_PLUGIN_NAME}"
    S3G_RNBO_PLUGIN_DESCRIPTION="${S3G_RNBO_PLUGIN_DESCRIPTION}"
  )

  if(APPLE)
    set_source_files_properties(src/s3g_rnbo_test_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(s3g_rnbo_test_clap PRIVATE "-framework Cocoa")
  endif()

  set_target_properties(s3g_rnbo_test_clap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "s3g_rnbo_${S3G_RNBO_PLUGIN_SLUG}"
  )

  if(APPLE)
    set_target_properties(s3g_rnbo_test_clap PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${S3G_RNBO_EFFECTIVE_PLUGIN_ID}"
      MACOSX_BUNDLE_BUNDLE_NAME "${S3G_RNBO_PLUGIN_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()
endif()
