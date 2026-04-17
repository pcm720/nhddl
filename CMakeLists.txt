cmake_minimum_required(VERSION 3.13)

include($ENV{PS2SDK}/ps2dev.cmake)

project(nhddl)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Executable name
set(EE_BIN "nhddl_unc.elf")
set(EE_BIN_PKD "nhddl.elf")
# Enable debug output if requested
option(ENABLE_PRINTF "Enable printf debugging output" OFF)
# Print debug output to EE SIO
option(USE_EESIO "Use EE SIO for debugging" OFF)

# Source files
set(SOURCES
    src/main.c
    src/dprintf.c
    src/forwarder.c

    # Main UI code
    src/ui/ui_main.c
    src/ui/view.c
    src/ui/title_list.c
    src/ui/atlas.c
    src/ui/font.c
    src/ui/scale.c
    src/ui/png.c
    src/ui/draw.c
    src/ui/icons.c

    # UI worker thread
    src/ui/worker/worker.c

    # Scenes (one per file under ui/views)
    src/ui/views/exit_modal.c
    src/ui/views/splash_scene.c
    src/ui/views/osd_overlay.c
    src/ui/views/main_scene.c
    src/ui/views/menu_scene.c
    src/ui/views/file_selector_scene.c
    src/ui/views/title_options_scene.c
    src/ui/neutrino/neutrino_arg_editor.c
    src/ui/neutrino/neutrino_arg_rows.c
    src/ui/neutrino/neutrino_arg_draw.c
    src/ui/neutrino/neutrino_arg_input.c
    src/ui/views/view_options_popup.c

    # Neutrino support
    src/neutrino/neutrino_arg_marshal.c
    src/neutrino/argv.c
    src/neutrino/locate.c
    src/neutrino/launcher.c

    # Config
    src/config/arguments.c
    src/config/common.c
    src/config/config.c
    src/config/nhddl.c
    src/config/neutrino_args.c


    # Devices
    src/devices/devices.c
    src/devices/hdd.c
    src/devices/utils.c
    src/devices/info.c
    src/devices/pad.c

    # Backends
    src/backends/backends.c
    src/backends/init.c
    src/backends/utils.c
    src/backends/cache.c
    src/backends/target.c
    src/backends/iso.c
    src/backends/hdl.c
    src/backends/title_id.c
)
include(embed.cmake)

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
if(USE_EESIO)
    add_definitions(-DUSE_EESIO)
endif()

# Create the main executable
add_executable(${EE_BIN} ${SOURCES})

# Include directories
target_include_directories(${EE_BIN}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    $ENV{PS2SDK}/ports/include/freetype2
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
set(EE_LIBS
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
    freetype
)
target_link_libraries(${EE_BIN} ${EE_LIBS})

# # Set link flags for PS2
# set_target_properties(${EE_BIN} PROPERTIES
#     LINK_FLAGS "-s"
# )

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
