#include "ui/presentation.hpp"
#include "ui/gamepad_input_policy.h"

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
    const bool currently_enabled =
        (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;

    if (currently_enabled == enabled) {
        return;
    }
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

struct window_state {
    bool window_hidden = false;
    gdox_gamepad_input_policy gamepad{};
};

float initialize_window(bool deck)
{
    unsigned int flags =
        FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT;

    flags |= deck ? FLAG_WINDOW_UNDECORATED : FLAG_WINDOW_HIGHDPI;
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
    return interface_scale;
}

void initialize_imgui(bool deck, float interface_scale)
{
    rlImGuiSetup(true);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigNavCursorVisibleAlways = deck;
    io.ConfigNavEscapeClearFocusItem = !deck;
    gdox::ui::initialize_presentation();
    if (!deck && interface_scale <= 1.0F) {
        return;
    }

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(deck ? 1.18F : interface_scale);
    style.FontScaleMain = deck ? 1.20F : interface_scale;
    if (deck) {
        ImGui::SetNavCursorVisible(true);
    }
}

bool wait_while_emulator_owns_display(
    bool deck,
    bool emulator_running,
    window_state &state
)
{
    if (!deck || !emulator_running) {
        return false;
    }
    if (!state.window_hidden) {
        SetWindowState(FLAG_WINDOW_HIDDEN);
        state.window_hidden = true;
    }
    WaitTime(0.025);
    return true;
}

void restore_deck_window(bool deck, window_state &state, ImGuiIO &io)
{
    if (!deck || !state.window_hidden) {
        return;
    }
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    ImGui::SetNavCursorVisible(true);
    io.ClearInputKeys();
    state.window_hidden = false;
}

void update_gamepad_input(
    bool emulator_running,
    window_state &state,
    ImGuiIO &io
)
{
    const bool focused = IsWindowFocused();
    const bool buttons_released =
        !emulator_running && gamepad_buttons_released();

    gdox_gamepad_input_update(
        &state.gamepad,
        emulator_running,
        focused,
        buttons_released
    );
    set_gamepad_navigation(io, state.gamepad.navigation_enabled);
}

void handle_page_navigation(
    bool deck,
    const window_state &state,
    gdox_app &app
)
{
    if (!deck || !state.gamepad.navigation_enabled) {
        return;
    }
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) {
        select_adjacent_page(app, -1);
    }
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) {
        select_adjacent_page(app, 1);
    }
}

void import_dropped_files(gdox_app &app)
{
    if (!IsFileDropped()) {
        return;
    }
    const FilePathList dropped = LoadDroppedFiles();
    for (unsigned int index = 0U; index < dropped.count; ++index) {
        (void)gdox_app_import_firmware(&app, dropped.paths[index]);
    }
    UnloadDroppedFiles(dropped);
}

bool draw_frame(gdox_app &app, bool deck)
{
    BeginDrawing();
    ClearBackground(background);
    rlImGuiBegin();
    const bool quit_requested = gdox::ui::draw_application(app, deck);
    rlImGuiEnd();
    EndDrawing();
    return quit_requested;
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
    window_state ui_state;
    bool quit_requested = false;
    gdox_app_initialize(&app);
    gdox_gamepad_input_initialize(&ui_state.gamepad);

    const float interface_scale = initialize_window(deck);
    initialize_imgui(deck, interface_scale);
    ImGuiIO &io = ImGui::GetIO();

    while (!quit_requested) {
        gdox_app_tick(&app);
        const gdox_app_snapshot *snapshot = gdox_app_snapshot_get(&app);
        const bool xemu_running = snapshot != nullptr && snapshot->can_close;

        update_gamepad_input(xemu_running, ui_state, io);
        if (wait_while_emulator_owns_display(
                deck,
                xemu_running,
                ui_state
            )) {
            continue;
        }
        restore_deck_window(deck, ui_state, io);
        if (WindowShouldClose()) {
            break;
        }
        handle_page_navigation(deck, ui_state, app);
        import_dropped_files(app);
        quit_requested = draw_frame(app, deck);
    }

    gdox_app_shutdown(&app);
    gdox::ui::shutdown_presentation();
    rlImGuiShutdown();
    CloseWindow();
    return 0;
}
