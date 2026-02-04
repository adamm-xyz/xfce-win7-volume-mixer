cmake_minimum_required(VERSION 3.10)
project(xfce-volume-mixer-plugin C)

set(CMAKE_C_STANDARD 99)

# Find required packages
find_package(PkgConfig REQUIRED)

pkg_check_modules(GTK REQUIRED gtk+-3.0)
pkg_check_modules(XFCE REQUIRED libxfce4panel-2.0)
pkg_check_modules(PULSE REQUIRED libpulse libpulse-mainloop-glib)

# Include directories
include_directories(
    ${GTK_INCLUDE_DIRS}
    ${XFCE_INCLUDE_DIRS}
    ${PULSE_INCLUDE_DIRS}
)

# Link directories
link_directories(
    ${GTK_LIBRARY_DIRS}
    ${XFCE_LIBRARY_DIRS}
    ${PULSE_LIBRARY_DIRS}
)

# Compiler flags
add_definitions(
    ${GTK_CFLAGS_OTHER}
    ${XFCE_CFLAGS_OTHER}
    ${PULSE_CFLAGS_OTHER}
)

# Add flags to export symbols
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=default")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--export-dynamic")

# Build the plugin as a shared library (not MODULE)
add_library(volume-mixer-plugin SHARED
    volume-mixer-plugin.c
)

target_link_libraries(volume-mixer-plugin
    ${GTK_LIBRARIES}
    ${XFCE_LIBRARIES}
    ${PULSE_LIBRARIES}
)

# Don't prefix the library with 'lib' and set the suffix
set_target_properties(volume-mixer-plugin PROPERTIES 
    PREFIX "lib"
    LINK_FLAGS "-Wl,--export-dynamic -Wl,-E"
)

# Installation
execute_process(
    COMMAND pkg-config --variable=libdir libxfce4panel-2.0
    OUTPUT_VARIABLE XFCE_PANEL_PLUGIN_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT XFCE_PANEL_PLUGIN_DIR)
    set(XFCE_PANEL_PLUGIN_DIR "${CMAKE_INSTALL_PREFIX}/lib/xfce4/panel/plugins")
else()
    set(XFCE_PANEL_PLUGIN_DIR "${XFCE_PANEL_PLUGIN_DIR}/xfce4/panel/plugins")
endif()

install(TARGETS volume-mixer-plugin
    LIBRARY DESTINATION ${XFCE_PANEL_PLUGIN_DIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

# Process and install desktop file
# Check if the .in file exists, if not use the plain .desktop file
if(EXISTS "${CMAKE_SOURCE_DIR}/volume-mixer-plugin.desktop.in")
    configure_file(
        volume-mixer-plugin.desktop.in
        volume-mixer-plugin.desktop
        @ONLY
    )
    set(DESKTOP_FILE "${CMAKE_BINARY_DIR}/volume-mixer-plugin.desktop")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/volume-mixer-plugin.desktop")
    set(DESKTOP_FILE "${CMAKE_SOURCE_DIR}/volume-mixer-plugin.desktop")
else()
    # Generate the desktop file
    file(WRITE "${CMAKE_BINARY_DIR}/volume-mixer-plugin.desktop"
"[Xfce Panel]
Type=X-XFCE-PanelPlugin
Encoding=UTF-8
Name=Volume Mixer
Comment=Per-application volume control
Icon=multimedia-volume-control
X-XFCE-Module=volume-mixer-plugin
X-XFCE-Unique=false
X-XFCE-API=2.0
")
    set(DESKTOP_FILE "${CMAKE_BINARY_DIR}/volume-mixer-plugin.desktop")
endif()

execute_process(
    COMMAND pkg-config --variable=datadir xfce4-panel
    OUTPUT_VARIABLE XFCE_PANEL_DATA_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT XFCE_PANEL_DATA_DIR)
    set(XFCE_PANEL_DATA_DIR "${CMAKE_INSTALL_PREFIX}/share/xfce4/panel/plugins")
else()
    set(XFCE_PANEL_DATA_DIR "${XFCE_PANEL_DATA_DIR}/plugins")
endif()

install(FILES ${DESKTOP_FILE}
    DESTINATION ${XFCE_PANEL_DATA_DIR}
)

# Print installation directories
message(STATUS "Plugin will be installed to: ${XFCE_PANEL_PLUGIN_DIR}")
message(STATUS "Desktop file will be installed to: ${XFCE_PANEL_DATA_DIR}")
