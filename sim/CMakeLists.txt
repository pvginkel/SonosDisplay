# The native render build: main/ui/ against LVGL 9.2.2, each scene written to an 800x480 PNG.
# It is the repository's `kc project test` (docs/firmware-design.md, section 5):
#
#   cmake -S sim -B build-sim -G Ninja && cmake --build build-sim && build-sim/render --all --out build-sim/screens

cmake_minimum_required(VERSION 3.16)

project(sonos_display_sim C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../main/ui)

# The rule that makes this build possible: main/ui/ includes LVGL and the C++ standard library,
# and nothing else. A quoted include must be lvgl.h or a file in ui/; an angle include must be a
# standard C++ header, which has no extension. ui/log.h alone names an ESP-IDF header, behind
# ESP_PLATFORM.
file(GLOB_RECURSE UI_HEADERS_AND_SOURCES CONFIGURE_DEPENDS ${UI_DIR}/*.h ${UI_DIR}/*.cpp)

foreach(file ${UI_HEADERS_AND_SOURCES})
    if(file MATCHES "/ui/log\\.h$")
        continue()
    endif()

    get_filename_component(dir ${file} DIRECTORY)
    file(STRINGS ${file} include_lines REGEX "^[ \t]*#[ \t]*include")

    foreach(line ${include_lines})
        if(line MATCHES "#[ \t]*include[ \t]*<([^>]+)>")
            set(included ${CMAKE_MATCH_1})
            if(included MATCHES "\\.")
                message(FATAL_ERROR "${file}: ui/ includes LVGL and the C++ standard library only, not <${included}>")
            endif()
        elseif(line MATCHES "#[ \t]*include[ \t]*\"([^\"]+)\"")
            set(included ${CMAKE_MATCH_1})
            if(NOT included STREQUAL "lvgl.h" AND (included MATCHES "\\.\\." OR NOT EXISTS ${dir}/${included}))
                message(FATAL_ERROR "${file}: ui/ includes LVGL and the C++ standard library only, not \"${included}\"")
            endif()
        endif()
    endforeach()
endforeach()

# LVGL at the tag main/idf_component.yml pins, configured by lv_conf.h.
include(FetchContent)

set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h CACHE STRING "" FORCE)
set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.2.2.tar.gz
    URL_HASH SHA256=129b4e00e06639fa79d7e8a6cab3c1ecce2445b1a246652ccd34f22e7b17ad6f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lvgl)

file(GLOB UI_SOURCES CONFIGURE_DEPENDS ${UI_DIR}/*.cpp ${UI_DIR}/fonts/*.c)

add_executable(render main.cpp SimHost.cpp scenes.cpp ${UI_SOURCES})
target_include_directories(render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${UI_DIR})
target_link_libraries(render PRIVATE lvgl)
target_compile_options(render PRIVATE -Wall -Werror)
