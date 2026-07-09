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
set(S3G_RNBO_EXPORT_DIR "" CACHE PATH "Optional RNBO C++ export folder containing rnbo_source.cpp and rnbo/RNBO.cpp")
set(S3G_RNBO_INPUT_CHANNELS "8" CACHE STRING "Fixed CLAP input channel count for the test wrapper")
set(S3G_RNBO_OUTPUT_CHANNELS "8" CACHE STRING "Fixed CLAP output channel count for the test wrapper")

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
    if(EXISTS "${S3G_RNBO_EXPORT_DIR}/rnbo_source.cpp" AND EXISTS "${S3G_RNBO_EXPORT_DIR}/rnbo/RNBO.cpp")
      set(S3G_HAS_RNBO_EXPORT 1)
      list(APPEND S3G_RNBO_SOURCES
        "${S3G_RNBO_EXPORT_DIR}/rnbo_source.cpp"
        "${S3G_RNBO_EXPORT_DIR}/rnbo/RNBO.cpp"
      )
    else()
      message(FATAL_ERROR "S3G_RNBO_EXPORT_DIR must contain rnbo_source.cpp and rnbo/RNBO.cpp")
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
  )

  if(APPLE)
    set_source_files_properties(src/s3g_rnbo_test_clap.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(s3g_rnbo_test_clap PRIVATE "-framework Cocoa")
  endif()

  set_target_properties(s3g_rnbo_test_clap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "s3g_rnbo_8ch_passthru"
  )

  if(APPLE)
    set_target_properties(s3g_rnbo_test_clap PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "org.s3g.s3g-rnbo-clap.8ch-passthru"
      MACOSX_BUNDLE_BUNDLE_NAME "s3g RNBO 8ch Passthru"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()
endif()
