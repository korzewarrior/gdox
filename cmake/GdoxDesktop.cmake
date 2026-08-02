set(GDOX_RAYLIB_TARGET "")
if(GDOX_USE_SYSTEM_RAYLIB AND NOT CMAKE_CROSSCOMPILING)
    find_package(raylib 6.0 QUIET)
    if(TARGET raylib)
        set(GDOX_RAYLIB_TARGET raylib)
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(RAYLIB 6.0 QUIET IMPORTED_TARGET raylib)
            if(TARGET PkgConfig::RAYLIB)
                set(GDOX_RAYLIB_TARGET PkgConfig::RAYLIB)
            endif()
        endif()
    endif()
endif()

if(NOT GDOX_RAYLIB_TARGET)
    if(NOT GDOX_FETCH_RAYLIB)
        message(
            FATAL_ERROR
            "raylib 6.0 was not found and dependency fetching is disabled"
        )
    endif()
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG dbc56a87da87d973a9c5baa4e7438a9d20121d28
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(raylib)
    set(GDOX_RAYLIB_TARGET raylib)
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG dac07199cfd761113d966eb8ad739254e10df2fe
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG 3bc5731c4216bb8caa67fbea24aa85ce80d57ccb
    GIT_SHALLOW TRUE
)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_SDL2_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
if(UNIX AND NOT APPLE)
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG fc168e8605bfa51aaec22ab0c4e46b9de665a437
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui rlimgui nfd)

add_library(
    gdox_imgui
    STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(
    gdox_imgui
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${rlimgui_SOURCE_DIR}
)
target_compile_definitions(gdox_imgui PRIVATE NO_FONT_AWESOME)
target_link_libraries(gdox_imgui PUBLIC ${GDOX_RAYLIB_TARGET})
if(MSVC)
    target_compile_options(gdox_imgui PRIVATE /w)
    target_compile_options(nfd PRIVATE /w)
else()
    target_compile_options(gdox_imgui PRIVATE -w)
    target_compile_options(nfd PRIVATE -w)
endif()

add_executable(
    gdox
    src/ui/configuration_pages.cpp
    src/ui/details_page.cpp
    src/ui/gamepad_input_policy.c
    src/ui/main.cpp
    src/ui/play_page.cpp
    src/ui/presentation.cpp
    src/ui/preserve_page.cpp
    src/ui/theme.cpp
)
if(WIN32)
    target_compile_definitions(gdox PRIVATE _CRT_SECURE_NO_WARNINGS)
    configure_file(
        src/platform/windows_resources.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc"
        @ONLY
    )
    target_sources(
        gdox
        PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/windows_resources.rc"
    )
    set_target_properties(gdox PROPERTIES WIN32_EXECUTABLE TRUE)
elseif(APPLE)
    target_sources(gdox PRIVATE packaging/macos/GDOX.icns)
    set_source_files_properties(
        packaging/macos/GDOX.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources
    )
    set_target_properties(
        gdox
        PROPERTIES
            MACOSX_BUNDLE TRUE
            MACOSX_BUNDLE_BUNDLE_NAME GDOX
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_GUI_IDENTIFIER org.gdox.gdox
            MACOSX_BUNDLE_ICON_FILE GDOX.icns
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
endif()
target_include_directories(gdox PRIVATE src)
target_link_libraries(
    gdox
    PRIVATE
        gdox::runtime
        gdox_imgui
        nfd::nfd
        ${GDOX_RAYLIB_TARGET}
)
gdox_enable_c_warnings(gdox)
gdox_enable_cxx_warnings(gdox)
if(MINGW)
    target_link_options(
        gdox
        PRIVATE -static -static-libgcc -static-libstdc++
    )
endif()
