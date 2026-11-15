cmake_minimum_required(VERSION 3.16)

# The firmware version is the short git hash. IoT Support reads it from the image.
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(PROJECT_VER "${GIT_COMMIT_HASH}")

message(STATUS "Firmware version set to: ${PROJECT_VER}")

# esp-libs is checked out beside this repository, in the environment and in Jenkins.
set(
    SDKCONFIG_DEFAULTS
    ../esp-libs/esp-mdm/sdkconfig.defaults
    ../esp-libs/esp-network-support/sdkconfig.defaults
    ../esp-libs/esp-mqtt-support/sdkconfig.defaults
    sdkconfig.defaults
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The project name is the IoT Support model code: the image is build/sonos_display.bin.
project(sonos_display)

# The app image must stay well inside its 3 MiB OTA slot, and inside the 2.5 MiB the PSRAM
# budget gives the firmware's code and constants (docs/firmware-design.md, section 7).
set(SONOS_DISPLAY_MAX_IMAGE_SIZE 2621440)

add_custom_command(
    TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DIMAGE=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
        -DMAX_SIZE=${SONOS_DISPLAY_MAX_IMAGE_SIZE}
        -P ${CMAKE_SOURCE_DIR}/tools/check-image-size.cmake
    VERBATIM
)
