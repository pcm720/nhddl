cmake_minimum_required(VERSION 3.13)

include($ENV{PS2SDK}/ps2dev.cmake)

project(nhddl)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Executable name
set(EE_BIN "nhddl_unc.elf")
set(EE_BIN_PKD "nhddl.elf")
# Enable debug output if requested
option(ENABLE_PRINTF "Enable printf debugging output" ON)

# Source files
set(SOURCES
    src/main.c
    src/common.c
    src/options.c
    src/neutrino.c
    src/target.c
    src/forwarder.c
    src/favorites.c

    # UI
    src/ui/gui.c
    src/ui/graphics.c
    src/ui/args.c
    src/ui/coverart.c
    src/ui/pad.c

    # Devices
    src/devices/init.c
    src/devices/devices.c
    src/devices/mmce.c
    src/devices/udpfs.c
    src/devices/bdm.c
    src/devices/iso.c
    src/devices/hdl.c
    src/devices/title_id.c
)
include(irx.cmake)

# Get git version for build info
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags --exclude nightly
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
  set(GIT_VERSION "unknown")
endif()

# Set compiler flags
add_definitions(-DGIT_VERSION=\"${GIT_VERSION}\")

if(ENABLE_PRINTF)
  add_definitions(-DENABLE_PRINTF)
endif()

# Create the main executable
add_executable(${EE_BIN} ${SOURCES})

# Include directories
target_include_directories(${EE_BIN}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Custom linker script
separate_arguments(LINKER_FLAGS UNIX_COMMAND ${CMAKE_EXE_LINKER_FLAGS})
list(REMOVE_ITEM LINKER_FLAGS "-T$ENV{PS2SDK}/ee/startup/linkfile")
list(JOIN LINKER_FLAGS " " CMAKE_EXE_LINKER_FLAGS)
target_link_options(${EE_BIN}
    PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/linkfile
)

# Link libraries
target_link_libraries(${EE_BIN}
    pad
    patches
    fileXio
    libdebug.a
    gskit_toolkit
    gskit
    dmakit
    png
    z
    tiff
)

# Set link flags for PS2
set_target_properties(${EE_BIN} PROPERTIES
    LINK_FLAGS "-s"
)

# Create packed binary
add_custom_command(
    TARGET ${EE_BIN}
    POST_BUILD
    COMMAND ${PS2DEV}/bin/ps2-packer
            ${EE_BIN}
            ${EE_BIN_PKD}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Packing ${EE_BIN} as ${EE_BIN_PKD}"
)
