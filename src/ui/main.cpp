#include "ui/presentation.hpp"

#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

constexpr Color background = {14, 16, 18, 255};
constexpr int desktop_width = 880;
constexpr int desktop_height = 680;
constexpr int desktop_min_width = 680;
constexpr int desktop_min_height = 560;

float desktop_interface_scale()
{
    const Vector2 dpi = GetWindowScaleDPI();
    float scale = std::max(dpi.x, dpi.y);
    const int monitor = GetCurrentMonitor();
    const int monitor_width = GetMonitorWidth(monitor);
    const int monitor_height = GetMonitorHeight(monitor);
    if (monitor_width >= 3200 && monitor_height >= 1800) {
        scale = std::max(scale, 1.35F);
    }
    const float fit = std::min(
        static_cast<float>(monitor_width) * 0.90F
            / static_cast<float>(desktop_width),
        static_cast<float>(monitor_height) * 0.85F
            / static_cast<float>(desktop_height)
    );
    scale = std::min(scale, std::max(1.0F, fit));
    if (scale < 1.20F) {
        return 1.0F;
    }
    return std::clamp(scale, 1.0F, 1.35F);
}

int scaled_size(int size, float scale)
{
    return static_cast<int>(std::lround(static_cast<float>(size) * scale));
}

bool gaming_mode()
{
    const char *value = std::getenv("GDOX_GAMING_MODE");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

void select_adjacent_page(gdox_app &app, int direction)
{
    const gdox_app_snapshot *snapshot = gdox_app_snapshot_get(&app);
    if (snapshot == nullptr) {
        return;
    }
    constexpr int page_count = GDOX_APP_PAGE_SOURCES + 1;
    const int current = static_cast<int>(snapshot->page);
    const int selected = (current + direction + page_count) % page_count;
    gdox_app_select_page(&app, static_cast<gdox_app_page>(selected));
}

void set_gamepad_navigation(ImGuiIO &io, bool enabled)
{
    if (enabled) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        io.ClearInputKeys();
    }
}

bool gamepad_buttons_released()
{
    if (!IsGamepadAvailable(0)) {
        return true;
    }
    for (int button = GAMEPAD_BUTTON_LEFT_FACE_UP;
         button <= GAMEPAD_BUTTON_RIGHT_THUMB;
         ++button) {
        if (IsGamepadButtonDown(0, button)) {
            return false;
        }
    }
    return true;
}

}

#if defined(_WIN32)
extern "C" int WinMain(void *, void *, char *, int)
#else
int main()
#endif
{
    gdox_app app{};
    const bool deck = gaming_mode();
    bool deck_window_hidden = false;
    bool deck_input_armed = true;
    bool quit_requested = false;
    gdox_app_initialize(&app);

    unsigned int flags =
        FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT;
    if (deck) {
        flags |= FLAG_WINDOW_UNDECORATED;
    } else {
        flags |= FLAG_WINDOW_HIGHDPI;
    }
    SetConfigFlags(flags);
    InitWindow(deck ? 1280 : desktop_width, deck ? 800 : desktop_height, "GDOX");
    const float interface_scale = deck ? 1.0F : desktop_interface_scale();
    if (!deck && interface_scale > 1.0F) {
        SetWindowSize(
            scaled_size(desktop_width, interface_scale),
            scaled_size(desktop_height, interface_scale)
        );
    }
    SetWindowMinSize(
        scaled_size(desktop_min_width, interface_scale),
        scaled_size(desktop_min_height, interface_scale)
    );
    if (deck) {
        SetExitKey(KEY_NULL);
    }
    SetTargetFPS(60);
    rlImGuiSetup(true);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!deck) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    io.ConfigNavCursorVisibleAlways = deck;
    io.ConfigNavEscapeClearFocusItem = !deck;
    gdox::ui::initialize_presentation();
    if (deck || interface_scale > 1.0F) {
        ImGuiStyle &style = ImGui::GetStyle();
        style.ScaleAllSizes(deck ? 1.18F : interface_scale);
        style.FontScaleMain = deck ? 1.20F : interface_scale;
        if (deck) {
            ImGui::SetNavCursorVisible(true);
        }
    }

    while (!quit_requested) {
        gdox_app_tick(&app);
        const gdox_app_snapshot *snapshot = gdox_app_snapshot_get(&app);
        const bool xemu_running = snapshot != nullptr && snapshot->can_close;

        if (deck && xemu_running) {
            if (!deck_window_hidden) {
                set_gamepad_navigation(io, false);
                deck_input_armed = false;
                SetWindowState(FLAG_WINDOW_HIDDEN);
                deck_window_hidden = true;
            }
            WaitTime(0.025);
            continue;
        }

        if (deck && deck_window_hidden) {
            ClearWindowState(FLAG_WINDOW_HIDDEN);
            ImGui::SetNavCursorVisible(true);
            io.ClearInputKeys();
            deck_window_hidden = false;
        }

        if (WindowShouldClose()) {
            break;
        }

        if (deck) {
            const bool focused = IsWindowFocused();
            if (!focused) {
                deck_input_armed = false;
            } else if (!deck_input_armed && gamepad_buttons_released()) {
                deck_input_armed = true;
            }
            set_gamepad_navigation(io, focused && deck_input_armed);
        }

        if (deck
            && deck_input_armed
            && IsGamepadButtonPressed(
                0,
                GAMEPAD_BUTTON_LEFT_TRIGGER_1
            )) {
            select_adjacent_page(app, -1);
        }
        if (deck
            && deck_input_armed
            && IsGamepadButtonPressed(
                0,
                GAMEPAD_BUTTON_RIGHT_TRIGGER_1
            )) {
            select_adjacent_page(app, 1);
        }
        if (IsFileDropped()) {
            const FilePathList dropped = LoadDroppedFiles();
            for (unsigned int index = 0U; index < dropped.count; ++index) {
                (void)gdox_app_import_firmware(&app, dropped.paths[index]);
            }
            UnloadDroppedFiles(dropped);
        }
        BeginDrawing();
        ClearBackground(background);
        rlImGuiBegin();
        quit_requested = gdox::ui::draw_application(app, deck);
        rlImGuiEnd();
        EndDrawing();
    }

    gdox_app_shutdown(&app);
    gdox::ui::shutdown_presentation();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
