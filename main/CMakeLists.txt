# New source files are picked up without a reconfigure. ESP-IDF also reads this file in script
# mode to collect requirements, where CONFIGURE_DEPENDS is refused.
if(CMAKE_SCRIPT_MODE_FILE)
    set(GLOB_OPTIONS "")
else()
    set(GLOB_OPTIONS CONFIGURE_DEPENDS)
endif()

file(GLOB_RECURSE SOURCES ${GLOB_OPTIONS} ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-missing-field-initializers
    -Wno-switch
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>
)

# LVGL calls lv_malloc_core and its siblings, which board/lv_mem_psram.c defines. liblvgl comes
# after this component on the link line, so the object is pulled in by name.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
