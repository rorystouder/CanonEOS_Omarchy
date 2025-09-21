cmake_minimum_required(VERSION 3.16)
project(obs-canon-eos VERSION 1.0.0 LANGUAGES C CXX)

# Set C/C++ standards as per CLAUDE.md
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable all warnings as per best practices
add_compile_options(-Wall -Wextra -Wpedantic)

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

# Debug build settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g3 -O0)
    add_definitions(-DDEBUG_MODE)
endif()

# Find required packages
find_package(PkgConfig REQUIRED)

# Find OBS Studio via pkg-config
pkg_check_modules(OBS REQUIRED libobs)
if(NOT OBS_FOUND)
    message(FATAL_ERROR "libobs not found via pkg-config. Install obs-studio development package.")
endif()

# Find gPhoto2
pkg_check_modules(GPHOTO2 REQUIRED libgphoto2>=2.5.27)

# Find libusb
pkg_check_modules(USB REQUIRED libusb-1.0>=1.0.24)

# Find threads
find_package(Threads REQUIRED)

# Plugin sources
set(CANON_EOS_SOURCES
    src/plugin-main.c
    src/canon-camera.c
    src/video-source.c
    src/camera-detector.c
    src/utils/error-handling.c
    src/utils/logging.c
)

# Plugin headers
set(CANON_EOS_HEADERS
    src/canon-camera.h
    src/video-source.h
    src/camera-detector.h
    src/canon-errors.h
    src/utils/error-handling.h
    src/utils/logging.h
)

# Create the plugin library
add_library(obs-canon-eos MODULE
    ${CANON_EOS_SOURCES}
    ${CANON_EOS_HEADERS}
)

# Include directories
target_include_directories(obs-canon-eos PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    /usr/include/obs
    ${OBS_INCLUDE_DIRS}
    ${GPHOTO2_INCLUDE_DIRS}
    ${USB_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(obs-canon-eos
    ${OBS_LIBRARIES}
    ${GPHOTO2_LIBRARIES}
    ${USB_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    m
)

# Compile flags
target_compile_options(obs-canon-eos PRIVATE
    ${OBS_CFLAGS_OTHER}
    ${GPHOTO2_CFLAGS_OTHER}
    ${USB_CFLAGS_OTHER}
)

# Set plugin install directory
if(NOT OBS_PLUGIN_DESTINATION)
    set(OBS_PLUGIN_DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/obs-plugins")
endif()

# Installation rules
install(TARGETS obs-canon-eos
    LIBRARY DESTINATION ${OBS_PLUGIN_DESTINATION}
)

# Install plugin data (if any)
install(DIRECTORY resources/camera-profiles
    DESTINATION "${CMAKE_INSTALL_PREFIX}/share/obs/obs-plugins/obs-canon-eos"
    OPTIONAL
)

# Testing configuration
enable_testing()

# Print configuration summary
message(STATUS "")
message(STATUS "Canon EOS OBS Plugin Configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Plugin Dir:     ${OBS_PLUGIN_DESTINATION}")
message(STATUS "")
message(STATUS "Dependencies:")
message(STATUS "  OBS:     Found")
message(STATUS "  gPhoto2: ${GPHOTO2_VERSION}")
message(STATUS "  libusb:  ${USB_VERSION}")
message(STATUS "")