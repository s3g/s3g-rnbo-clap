cmake_minimum_required(VERSION 3.20)

project(s3g_rnbo_clap VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

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
set(S3G_DSP_DIR "${CMAKE_CURRENT_LIST_DIR}/../s3g-dsp" CACHE PATH "s3g-dsp checkout providing the shared VSTGUI foundation")
option(S3G_ENABLE_PORTABLE_CLAP_GUI "Use shared VSTGUI editors on macOS and Windows" ON)
set(S3G_VSTGUI_GIT_REPOSITORY "https://github.com/steinbergmedia/vstgui.git" CACHE STRING "VSTGUI repository")
set(S3G_VSTGUI_GIT_TAG "5db272256172557818b6158cf0bb2c4410bddb25" CACHE STRING "VSTGUI revision shared with s3g-dsp")
set(S3G_DR_LIBS_GIT_TAG "dfe8377631000664666519fdb83da193fd8037f4" CACHE STRING "Pinned dr_libs revision")
set(S3G_RNBO_EXPORT_DIR "" CACHE PATH "Optional RNBO C++ export folder containing rnbo_source.cpp or a Max RNBO *.rnbopat.cpp source plus rnbo/RNBO.cpp")
set(S3G_RNBO_INPUT_CHANNELS "8" CACHE STRING "Fixed CLAP input channel count for the test wrapper")
set(S3G_RNBO_OUTPUT_CHANNELS "8" CACHE STRING "Fixed CLAP output channel count for the test wrapper")
set(S3G_RNBO_PLUGIN_SLUG "" CACHE STRING "Short identifier used for the built CLAP bundle filename; defaults to the RNBO export folder name")
set(S3G_RNBO_PLUGIN_NAME "" CACHE STRING "Human-readable CLAP bundle name; defaults to s3g RNBO plus the title-cased RNBO export folder name")
set(S3G_RNBO_PLUGIN_ID "" CACHE STRING "Stable CLAP plugin id; defaults to org.s3g.s3g-rnbo-clap.<slug>")
set(S3G_RNBO_PLUGIN_DESCRIPTION "CLAP wrapper for an RNBO C++ export." CACHE STRING "CLAP plugin description")
set(S3G_RNBO_PLUGIN_KIND "auto" CACHE STRING "CLAP feature kind: auto, effect, or instrument")
set_property(CACHE S3G_RNBO_PLUGIN_KIND PROPERTY STRINGS auto effect instrument)
if(NOT S3G_RNBO_PLUGIN_KIND MATCHES "^(auto|effect|instrument)$")
  message(FATAL_ERROR "S3G_RNBO_PLUGIN_KIND must be auto, effect, or instrument")
endif()

foreach(channels S3G_RNBO_INPUT_CHANNELS S3G_RNBO_OUTPUT_CHANNELS)
  if(NOT "${${channels}}" MATCHES "^[0-9]+$" OR ${channels} GREATER 256)
    message(FATAL_ERROR "${channels} must be an integer from 0 to 256")
  endif()
endforeach()
if(S3G_RNBO_OUTPUT_CHANNELS LESS 1)
  message(FATAL_ERROR "S3G_RNBO_OUTPUT_CHANNELS must be at least 1")
endif()
if(S3G_RNBO_EXPORT_DIR)
  get_filename_component(S3G_RNBO_EXPORT_DIR "${S3G_RNBO_EXPORT_DIR}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
endif()
set(S3G_RNBO_DERIVED_PLUGIN_SLUG "${S3G_RNBO_PLUGIN_SLUG}")
if(NOT S3G_RNBO_DERIVED_PLUGIN_SLUG)
  if(S3G_RNBO_EXPORT_DIR)
    get_filename_component(S3G_RNBO_DERIVED_PLUGIN_SLUG "${S3G_RNBO_EXPORT_DIR}" NAME)
  else()
    set(S3G_RNBO_DERIVED_PLUGIN_SLUG "fallback")
  endif()
endif()

if(NOT S3G_RNBO_DERIVED_PLUGIN_SLUG MATCHES "^[A-Za-z0-9_][A-Za-z0-9_-]*$")
  message(FATAL_ERROR "Plugin slug must contain only ASCII letters, digits, underscores or hyphens")
endif()

function(s3g_rnbo_title_from_slug output slug)
  string(REPLACE "_" ";" S3G_RNBO_NAME_PARTS "${slug}")
  set(S3G_RNBO_TITLE_PARTS "")
  set(S3G_RNBO_CHANNEL_PARTS "")
  foreach(S3G_RNBO_NAME_PART IN LISTS S3G_RNBO_NAME_PARTS)
    if(NOT S3G_RNBO_NAME_PART)
      continue()
    endif()
    string(TOLOWER "${S3G_RNBO_NAME_PART}" S3G_RNBO_NAME_WORD)
    if(S3G_RNBO_NAME_WORD MATCHES "^[0-9]+ch$")
      list(APPEND S3G_RNBO_CHANNEL_PARTS "${S3G_RNBO_NAME_WORD}")
    else()
      string(SUBSTRING "${S3G_RNBO_NAME_WORD}" 0 1 S3G_RNBO_NAME_FIRST)
      string(SUBSTRING "${S3G_RNBO_NAME_WORD}" 1 -1 S3G_RNBO_NAME_REST)
      string(TOUPPER "${S3G_RNBO_NAME_FIRST}" S3G_RNBO_NAME_FIRST)
      list(APPEND S3G_RNBO_TITLE_PARTS "${S3G_RNBO_NAME_FIRST}${S3G_RNBO_NAME_REST}")
    endif()
  endforeach()
  list(APPEND S3G_RNBO_TITLE_PARTS ${S3G_RNBO_CHANNEL_PARTS})
  list(JOIN S3G_RNBO_TITLE_PARTS " " S3G_RNBO_TITLE)
  if(NOT S3G_RNBO_TITLE)
    set(S3G_RNBO_TITLE "Fallback")
  endif()
  set(${output} "${S3G_RNBO_TITLE}" PARENT_SCOPE)
endfunction()

set(S3G_RNBO_DERIVED_PLUGIN_NAME "${S3G_RNBO_PLUGIN_NAME}")
if(NOT S3G_RNBO_DERIVED_PLUGIN_NAME)
  s3g_rnbo_title_from_slug(S3G_RNBO_PLUGIN_NAME_SUFFIX "${S3G_RNBO_DERIVED_PLUGIN_SLUG}")
  set(S3G_RNBO_DERIVED_PLUGIN_NAME "s3g RNBO ${S3G_RNBO_PLUGIN_NAME_SUFFIX}")
endif()

if(S3G_RNBO_PLUGIN_ID)
  set(S3G_RNBO_EFFECTIVE_PLUGIN_ID "${S3G_RNBO_PLUGIN_ID}")
else()
  string(REPLACE "_" "-" S3G_RNBO_PLUGIN_ID_SUFFIX "${S3G_RNBO_DERIVED_PLUGIN_SLUG}")
  set(S3G_RNBO_EFFECTIVE_PLUGIN_ID "org.s3g.s3g-rnbo-clap.${S3G_RNBO_PLUGIN_ID_SUFFIX}")
endif()

add_executable(s3g_rnbo_smoke tests/smoke.cpp)
target_include_directories(s3g_rnbo_smoke PRIVATE src)

add_custom_target(audit_gui_style
  COMMAND "${CMAKE_COMMAND}" "-DS3G_DSP_DIR=${S3G_DSP_DIR}"
          -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditGui.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Auditing s3g RNBO CLAP GUI style and host-name rules"
)

if(BUILD_TESTING)
  add_test(NAME s3g_rnbo_smoke COMMAND s3g_rnbo_smoke)
  add_test(
    NAME s3g_rnbo_gui_style
    COMMAND "${CMAKE_COMMAND}" "-DS3G_DSP_DIR=${S3G_DSP_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditGui.cmake"
  )
endif()

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

  if(S3G_ENABLE_PORTABLE_CLAP_GUI AND (APPLE OR WIN32))
    include(cmake/S3GRnboVSTGUI.cmake)
    s3g_rnbo_prepare_vstgui()
  endif()
  if(WIN32)
    FetchContent_Declare(dr_libs GIT_REPOSITORY https://github.com/mackron/dr_libs.git
      GIT_TAG "${S3G_DR_LIBS_GIT_TAG}" GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(dr_libs)
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
    src/s3g_rnbo_audio_file.cpp
    ${S3G_RNBO_SOURCES}
  )

  set(rnbo_source_hash "fallback")
  if(S3G_HAS_RNBO_EXPORT)
    file(SHA256 "${S3G_RNBO_PATCH_SOURCE}" rnbo_source_hash)
  endif()
  file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/rnbo-build-identity.txt" CONTENT
    "id=${S3G_RNBO_EFFECTIVE_PLUGIN_ID}\nname=${S3G_RNBO_DERIVED_PLUGIN_NAME}\nslug=${S3G_RNBO_DERIVED_PLUGIN_SLUG}\nversion=${PROJECT_VERSION}\nkind=${S3G_RNBO_PLUGIN_KIND}\ninputs=${S3G_RNBO_INPUT_CHANNELS}\noutputs=${S3G_RNBO_OUTPUT_CHANNELS}\nexport_sha256=${rnbo_source_hash}\nvstgui=${S3G_VSTGUI_GIT_TAG}\n")

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
    S3G_RNBO_PLUGIN_NAME="${S3G_RNBO_DERIVED_PLUGIN_NAME}"
    S3G_RNBO_PLUGIN_DESCRIPTION="${S3G_RNBO_PLUGIN_DESCRIPTION}"
    S3G_RNBO_PLUGIN_KIND_${S3G_RNBO_PLUGIN_KIND}=1
  )

  if(APPLE)
    set_source_files_properties(src/s3g_rnbo_test_clap.cpp src/s3g_rnbo_audio_file.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(s3g_rnbo_test_clap PRIVATE "-framework Cocoa" "-framework AVFoundation")
  endif()

  set_target_properties(s3g_rnbo_test_clap PROPERTIES
    PREFIX ""
    OUTPUT_NAME "s3g_${S3G_RNBO_DERIVED_PLUGIN_SLUG}"
  )

  if(APPLE)
    set_target_properties(s3g_rnbo_test_clap PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "clap"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${S3G_RNBO_EFFECTIVE_PLUGIN_ID}"
      MACOSX_BUNDLE_BUNDLE_NAME "${S3G_RNBO_DERIVED_PLUGIN_NAME}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
  endif()

  if(S3G_ENABLE_PORTABLE_CLAP_GUI AND (APPLE OR WIN32))
    s3g_rnbo_enable_vstgui(s3g_rnbo_test_clap)
  endif()
  if(WIN32)
    set_target_properties(s3g_rnbo_test_clap PROPERTIES SUFFIX ".clap")
    target_compile_definitions(s3g_rnbo_test_clap PRIVATE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_include_directories(s3g_rnbo_test_clap SYSTEM PRIVATE "${dr_libs_SOURCE_DIR}")
    target_link_libraries(s3g_rnbo_test_clap PRIVATE winmm)
    configure_file("${dr_libs_SOURCE_DIR}/LICENSE" "${PROJECT_BINARY_DIR}/resources/Licenses/dr_libs-LICENSE.txt" COPYONLY)
    add_custom_command(TARGET s3g_rnbo_test_clap POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:s3g_rnbo_test_clap>/Resources/Licenses"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_BINARY_DIR}/resources/Licenses/dr_libs-LICENSE.txt"
        "$<TARGET_FILE_DIR:s3g_rnbo_test_clap>/Resources/Licenses"
      VERBATIM)
  endif()

  if(BUILD_TESTING AND UNIX)
    add_executable(s3g_rnbo_clap_state_smoke tests/clap_state.cpp)
    target_include_directories(s3g_rnbo_clap_state_smoke PRIVATE
      ${S3G_CLAP_INCLUDE_DIR}
    )
    target_link_libraries(s3g_rnbo_clap_state_smoke PRIVATE ${CMAKE_DL_LIBS})
    add_test(
      NAME s3g_rnbo_clap_state
      COMMAND s3g_rnbo_clap_state_smoke $<TARGET_FILE:s3g_rnbo_test_clap>
    )
  endif()
  if(BUILD_TESTING AND S3G_ENABLE_PORTABLE_CLAP_GUI AND (APPLE OR WIN32))
    add_executable(s3g_rnbo_audio_file_smoke tests/audio_file.cpp src/s3g_rnbo_audio_file.cpp)
    if(APPLE)
      target_link_libraries(s3g_rnbo_audio_file_smoke PRIVATE "-framework Cocoa" "-framework AVFoundation")
    else()
      target_include_directories(s3g_rnbo_audio_file_smoke SYSTEM PRIVATE "${dr_libs_SOURCE_DIR}")
    endif()
    add_test(NAME s3g_rnbo_audio_file COMMAND s3g_rnbo_audio_file_smoke)
    add_executable(s3g_rnbo_gui_smoke tests/gui.cpp src/s3g_rnbo_audio_file.cpp ${S3G_RNBO_SOURCES})
    get_target_property(rnbo_definitions s3g_rnbo_test_clap COMPILE_DEFINITIONS)
    get_target_property(rnbo_includes s3g_rnbo_test_clap INCLUDE_DIRECTORIES)
    target_compile_definitions(s3g_rnbo_gui_smoke PRIVATE ${rnbo_definitions})
    target_include_directories(s3g_rnbo_gui_smoke PRIVATE ${rnbo_includes})
    if(APPLE)
      set_source_files_properties(tests/gui.cpp PROPERTIES LANGUAGE OBJCXX)
      set_target_properties(s3g_rnbo_gui_smoke PROPERTIES MACOSX_BUNDLE TRUE)
      target_link_libraries(s3g_rnbo_gui_smoke PRIVATE "-framework Cocoa" "-framework AVFoundation")
    elseif(WIN32)
      target_link_libraries(s3g_rnbo_gui_smoke PRIVATE winmm)
    endif()
    s3g_rnbo_enable_vstgui(s3g_rnbo_gui_smoke)
    add_test(NAME s3g_rnbo_gui COMMAND s3g_rnbo_gui_smoke)
    set_tests_properties(s3g_rnbo_gui PROPERTIES LABELS "gui" TIMEOUT 180)
  endif()
endif()
