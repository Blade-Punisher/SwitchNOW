cmake_minimum_required(VERSION 3.18)

if (NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. Install devkitPro and export DEVKITPRO before building.")
endif ()

set(DEVKITPRO $ENV{DEVKITPRO})
set(CMAKE_TOOLCHAIN_FILE ${DEVKITPRO}/cmake/Switch.cmake CACHE FILEPATH "devkitPro Switch toolchain")

project(SwitchNOW LANGUAGES C CXX)

set(PLATFORM_SWITCH ON)
# One Deko3D application supports both NVDEC zero-copy and software-decoded
# YUV uploads. Decoder selection is a runtime setting rather than a build type.
set(USE_GLFW OFF CACHE BOOL "Disable GLFW for unified Deko3D" FORCE)
set(USE_DEKO3D ON CACHE BOOL "Use Deko3D on Switch" FORCE)
set(BOREALIS_USE_DEKO3D ON CACHE BOOL "Use Borealis Deko3D driver" FORCE)
set(USE_GLES2 OFF CACHE BOOL "Disable GLES for unified Deko3D" FORCE)
set(USE_SDL2 OFF)
set(USE_GLES3 OFF CACHE BOOL "Disable OpenGL ES 3 on Switch")
set(BRLS_UNITY_BUILD OFF CACHE BOOL "Disable Borealis unity builds")
set(FMT_OS OFF CACHE BOOL "Disable fmt OS-specific APIs for devkitA64")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep developer workstation paths out of assertion strings/debug metadata.
# Apply to dependencies as well as the application, without changing runtime policies.
add_compile_options("-ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)

add_subdirectory(extern/borealis/library)
add_subdirectory(extern/libpeer)

target_include_directories(borealis BEFORE PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/include/borealis/extern/tinyxml2)
target_include_directories(borealis PUBLIC ${DEVKITPRO}/portlibs/switch/include)

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
    app/src/*.cpp
    app/src/*.c
)

set(PLATFORM_SOURCES)

if (PLATFORM_SWITCH)
    list(APPEND PLATFORM_SOURCES
        extern/borealis/library/lib/platforms/switch/switch_wrapper.c
    )
endif ()

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${PLATFORM_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        app/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/lib/extern/glfw/deps
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/third_party/mbedtls/include
)

# Prefer the in-tree libpeer dependencies over portlibs archives with the same
# names. Mixing mbedTLS versions produces TLS failures or an unusable NRO.
target_link_directories(${PROJECT_NAME}
    BEFORE PRIVATE
        ${CMAKE_BINARY_DIR}/dist/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg/lib
)

option(SWITCHNOW_NETWORK_DEBUG "Always-on credential-free connection diagnostics" OFF)
if (SWITCHNOW_NETWORK_DEBUG)
    # Observe the original SDK initialization result before main without editing
    # vendored Borealis or changing the supplied socket configuration.
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=socketInitialize)
endif ()
target_compile_definitions(${PROJECT_NAME} PRIVATE
    SWITCHNOW_NETWORK_DEBUG=$<BOOL:${SWITCHNOW_NETWORK_DEBUG}>
    PLATFORM_SWITCH __SWITCH__ HAS_SOCKLEN_T=1)
if (PLATFORM_SWITCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_DK_RENDERER BOREALIS_USE_DEKO3D OPENNOW_ENABLE_NVDEC)
    find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin")
    if (NOT NX_UAM_EXE)
        message(FATAL_ERROR "uam is required for the NVDEC/Deko3D video shaders")
    endif ()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources/shaders")
    set(BOREALIS_DEKO_SHADER_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/lib/extern/nanovg/deko3d/shaders")
    execute_process(COMMAND ${NX_UAM_EXE} -s vert
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_vsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_vsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_aa_fsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_aa_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_fsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s vert
        -o "${CMAKE_BINARY_DIR}/resources/shaders/opennow_video_vsh.dksh"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/stream/deko3d/basic_vsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/opennow_video_fsh.dksh"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/stream/deko3d/texture_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
endif ()

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -ffunction-sections
        -fdata-sections
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        borealis
        PkgConfig::CURL
        PkgConfig::JANSSON
        peer
        -lavformat
        -lavcodec
        -lavutil
        -lswresample
        z
        bz2
)

set(PROJECT_AUTHOR "Blade-Punisher")
set(PROJECT_VERSION_STRING "1.3.0" CACHE STRING "Display version embedded in SwitchNOW")
set(PROJECT_TITLEID "05004F4E4F575358")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)

target_compile_definitions(SwitchNOW PRIVATE
    SWITCHNOW_VERSION_STRING="${PROJECT_VERSION_STRING}")

add_executable(SwitchNOWShortcut app/shortcut/main.c)
target_compile_options(SwitchNOWShortcut PRIVATE -Wall -Wextra -Werror)

add_custom_target(SwitchNOWShortcut.nro
    DEPENDS SwitchNOWShortcut
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "SwitchNOW Game Shortcut"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        SwitchNOWShortcut.nacp
        --titleid=05004F4E53484F52
    COMMAND ${NX_ELF2NRO_EXE}
        SwitchNOWShortcut.elf
        SwitchNOWShortcut.nro
        --icon=${PROJECT_ICON}
        --nacp=SwitchNOWShortcut.nacp
)

add_executable(SwitchNOWUpdater app/updater/main.c)
target_include_directories(SwitchNOWUpdater PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/third_party/mbedtls/include
)
target_link_directories(SwitchNOWUpdater BEFORE PRIVATE
    ${CMAKE_BINARY_DIR}/dist/lib
)
target_link_libraries(SwitchNOWUpdater PRIVATE mbedcrypto)
target_compile_options(SwitchNOWUpdater PRIVATE -Wall -Wextra -Werror)

add_custom_target(SwitchNOWUpdater.nro
    DEPENDS SwitchNOWUpdater
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "SwitchNOW Update Helper"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        SwitchNOWUpdater.nacp
        --titleid=05004F4E55504454
    COMMAND ${NX_ELF2NRO_EXE}
        SwitchNOWUpdater.elf
        SwitchNOWUpdater.nro
        --icon=${PROJECT_ICON}
        --nacp=SwitchNOWUpdater.nacp
)

add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME} SwitchNOWShortcut.nro SwitchNOWUpdater.nro
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "SwitchNOW"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        ${PROJECT_NAME}.nacp
        --titleid=${PROJECT_TITLEID}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${PROJECT_ROMFS_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}/img
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}/shortcut
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}/updater
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/font
        ${PROJECT_ROMFS_DIR}/font
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n
        ${PROJECT_ROMFS_DIR}/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon
        ${PROJECT_ROMFS_DIR}/icon
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/sys
        ${PROJECT_ROMFS_DIR}/img/sys
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/material
        ${PROJECT_ROMFS_DIR}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/resources/shaders
        ${PROJECT_ROMFS_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/SwitchNOWShortcut.nro
        ${PROJECT_ROMFS_DIR}/shortcut/SwitchNOWShortcut.nro
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/tools/switchnow-forwarder-installer/switchnow-forwarder-installer.nro
        ${PROJECT_ROMFS_DIR}/shortcut/SwitchNOWForwarderInstaller.nro
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/SwitchNOWUpdater.nro
        ${PROJECT_ROMFS_DIR}/updater/SwitchNOWUpdater.nro
    COMMAND ${NX_ELF2NRO_EXE}
        ${PROJECT_NAME}.elf
        ${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${PROJECT_ROMFS_DIR}
)
