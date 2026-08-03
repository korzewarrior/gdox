#include "ui/presentation.hpp"
#include "ui/gamepad_input_policy.h"

#include "app/background.h"
#include "app/background_lifecycle.h"
#if !defined(_WIN32)
#include "app/termination.h"
#endif

#include "raylib.h"
#include "rlImGui.h"
#include "imgui.h"

#include <cstdlib>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

constexpr Color background = {14, 16, 18, 255};
constexpr int desktop_width = 880;
constexpr int desktop_height = 680;
constexpr int desktop_min_width = 680;
constexpr int desktop_min_height = 560;
constexpr unsigned int shutdown_attempts = 4U;
constexpr auto shutdown_retry_delay = std::chrono::milliseconds(100);
constexpr auto shutdown_recovery_delay = std::chrono::seconds(1);
constexpr auto gaming_playback_poll_delay = std::chrono::milliseconds(100);

bool gaming_mode()
{
    const char *value = std::getenv("GDOX_GAMING_MODE");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

gdox_host_profile packaged_host_profile()
{
    const char *value = std::getenv("GDOX_HOST_PROFILE");

    return value != nullptr && std::strcmp(value, "handheld") == 0
        ? GDOX_HOST_PROFILE_HANDHELD
        : GDOX_HOST_PROFILE_DESKTOP;
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
    bool emulator_window_hidden = false;
    gdox_gamepad_input_policy gamepad{};
};

bool initialize_window(bool deck)
{
    unsigned int flags =
        FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT;

    flags |= deck ? FLAG_WINDOW_UNDECORATED : FLAG_WINDOW_HIGHDPI;
    SetConfigFlags(flags);
    InitWindow(deck ? 1280 : desktop_width, deck ? 800 : desktop_height, "GDOX");
    if (!IsWindowReady()) {
        return false;
    }

    SetWindowMinSize(desktop_min_width, desktop_min_height);
    if (deck) {
        SetExitKey(KEY_NULL);
    }
    SetTargetFPS(60);
    return true;
}

void initialize_imgui(bool deck)
{
    rlImGuiSetup(true);
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigNavCursorVisibleAlways = deck;
    io.ConfigNavEscapeClearFocusItem = !deck;
    gdox::ui::initialize_presentation();
    if (!deck) {
        return;
    }

    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(1.18F);
    style.FontScaleMain = 1.20F;
    ImGui::SetNavCursorVisible(true);
}

bool wait_while_emulator_owns_display(
    bool deck,
    bool playback_running,
    window_state &state
)
{
    if (!deck || !playback_running) {
        return false;
    }
    if (!state.emulator_window_hidden) {
        SetWindowState(FLAG_WINDOW_HIDDEN);
        state.emulator_window_hidden = true;
    }
    std::this_thread::sleep_for(gaming_playback_poll_delay);
    return true;
}

void restore_deck_window(bool deck, window_state &state, ImGuiIO &io)
{
    if (!deck || !state.emulator_window_hidden) {
        return;
    }
    ClearWindowState(FLAG_WINDOW_HIDDEN);
    RestoreWindow();
    SetWindowFocused();
    ImGui::SetNavCursorVisible(true);
    io.ClearInputKeys();
    state.emulator_window_hidden = false;
}

void update_gamepad_input(
    bool playback_running,
    window_state &state,
    ImGuiIO &io
)
{
    const bool focused = IsWindowFocused();
    const bool buttons_released =
        !playback_running && gamepad_buttons_released();

    gdox_gamepad_input_update(
        &state.gamepad,
        playback_running,
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

void close_desktop_window()
{
    gdox::ui::shutdown_presentation();
    rlImGuiShutdown();
    CloseWindow();
}

bool shutdown_application_batch(gdox_app &app, gdox_error &error)
{
    for (unsigned int attempt = 0U; attempt < shutdown_attempts; ++attempt) {
        if (gdox_app_shutdown(&app, &error)) {
            return true;
        }
        if (app.runtime == nullptr) {
            return false;
        }
        if (attempt + 1U < shutdown_attempts) {
            std::this_thread::sleep_for(shutdown_retry_delay);
        }
    }
    return false;
}

bool shutdown_application(gdox_app &app)
{
    gdox_error error;

    while (!shutdown_application_batch(app, error)) {
        if (app.runtime == nullptr) {
            std::fprintf(
                stderr,
                "GDOX: shutdown completed with an error: %s\n",
                error.message
            );
            return false;
        }
        std::fprintf(
            stderr,
            "GDOX: shutdown is waiting for safe device cleanup: %s\n",
            error.message
        );
        std::this_thread::sleep_for(shutdown_recovery_delay);
    }
    return true;
}

#if defined(_WIN32)
struct native_shutdown_context {
    gdox_app *app;
    bool initialized;
    bool complete;
    bool success;
};

void complete_native_shutdown(void *opaque)
{
    auto *context = static_cast<native_shutdown_context *>(opaque);

    if (context == nullptr || !context->initialized || context->complete) {
        return;
    }
    context->success = shutdown_application(*context->app);
    context->complete = true;
}
#endif

gdox_background_action poll_background_host(
    gdox_app_background *host,
    bool background_only
)
{
    switch (gdox_app_background_poll(host, background_only)) {
        case GDOX_APP_BACKGROUND_OPEN:
            return GDOX_BACKGROUND_OPEN_REQUESTED;
        case GDOX_APP_BACKGROUND_QUIT:
            return GDOX_BACKGROUND_QUIT_REQUESTED;
        case GDOX_APP_BACKGROUND_AVAILABLE:
            return GDOX_BACKGROUND_FACILITY_AVAILABLE;
        case GDOX_APP_BACKGROUND_UNAVAILABLE:
            return GDOX_BACKGROUND_FACILITY_UNAVAILABLE;
        case GDOX_APP_BACKGROUND_NONE:
            return GDOX_BACKGROUND_NO_ACTION;
    }
    return GDOX_BACKGROUND_NO_ACTION;
}

int run_application(bool start_hidden)
{
    gdox_app app{};
    const bool deck = gaming_mode();
    gdox_app_instance *instance;
    gdox_app_background *background_host;
    gdox_background_lifecycle lifecycle;
    window_state ui_state;
    bool another_instance = false;
    bool window_open = false;
#if defined(_WIN32)
    native_shutdown_context native_shutdown{&app, false, false, false};
#endif
#if defined(__APPLE__)
    bool close_requested_during_window_initialization = false;
#endif
#if !defined(_WIN32)
    bool termination_handler_installed = false;
#endif
    int exit_code = 0;

    instance = gdox_app_instance_acquire(&another_instance);
    if (instance == nullptr) {
        if (another_instance) {
            if (gdox_app_instance_activate_existing()) {
                return 0;
            }
            gdox_app_instance_report_conflict();
        } else {
            gdox_app_instance_report_failure();
        }
        return 1;
    }
    gdox_gamepad_input_initialize(&ui_state.gamepad);
    background_host = deck ? nullptr : gdox_app_background_create();
#if defined(_WIN32)
    gdox_app_background_set_shutdown_handler(
        background_host,
        complete_native_shutdown,
        &native_shutdown
    );
#endif
    gdox_background_lifecycle_initialize(
        &lifecycle,
        start_hidden && !deck,
        background_host != nullptr
    );
    gdox_app_background_set_window_visible(
        background_host, lifecycle.state == GDOX_BACKGROUND_VISIBLE
    );

    if (lifecycle.state == GDOX_BACKGROUND_VISIBLE) {
        if (!initialize_window(deck)) {
            std::fprintf(
                stderr,
                "GDOX: could not initialize the application window\n"
            );
            gdox_app_background_destroy(background_host);
            gdox_app_instance_release(instance);
            return 1;
        }
        initialize_imgui(deck);
        window_open = true;
        gdox_app_background_set_window_visible(background_host, true);
#if defined(__APPLE__)
        close_requested_during_window_initialization = WindowShouldClose();
#endif
    }
#if !defined(_WIN32)
    {
        gdox_error signal_error;

        termination_handler_installed = gdox_app_termination_install(
            &signal_error
        );
        if (!termination_handler_installed) {
            std::fprintf(
                stderr,
                "GDOX: %s\n",
                signal_error.message
            );
            if (window_open) {
                close_desktop_window();
            }
            gdox_app_background_destroy(background_host);
            gdox_app_instance_release(instance);
            return 1;
        }
    }
#endif
    gdox_app_initialize(
        &app,
        packaged_host_profile()
    );
#if defined(_WIN32)
    native_shutdown.initialized = true;
#endif
#if defined(__APPLE__)
    if (close_requested_during_window_initialization) {
        gdox_background_lifecycle_apply(
            &lifecycle, GDOX_BACKGROUND_QUIT_REQUESTED
        );
    }
#endif

    while (lifecycle.state != GDOX_BACKGROUND_STOPPING) {
#if !defined(_WIN32)
        if (gdox_app_termination_requested()) {
            gdox_background_lifecycle_apply(
                &lifecycle, GDOX_BACKGROUND_QUIT_REQUESTED
            );
            continue;
        }
#endif
        if (gdox_app_instance_take_activation(instance)) {
            gdox_background_lifecycle_apply(
                &lifecycle, GDOX_BACKGROUND_OPEN_REQUESTED
            );
        }
        gdox_app_tick(&app);
        const gdox_app_snapshot *snapshot = gdox_app_snapshot_get(&app);
        if (background_host != nullptr) {
            gdox_app_background_set_status(
                background_host,
                snapshot != nullptr ? snapshot->status : "Starting"
            );
            gdox_background_lifecycle_apply(
                &lifecycle,
                poll_background_host(
                    background_host,
                    lifecycle.state == GDOX_BACKGROUND_HIDDEN
                )
            );
        }
        if (lifecycle.state == GDOX_BACKGROUND_STOPPING) {
            break;
        }
        if (lifecycle.state == GDOX_BACKGROUND_HIDDEN) {
            if (window_open) {
                close_desktop_window();
                window_open = false;
                gdox_app_background_set_window_visible(
                    background_host, false
                );
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        if (!window_open) {
            if (!initialize_window(deck)) {
                std::fprintf(
                    stderr,
                    "GDOX: could not reopen the application window\n"
                );
                exit_code = 1;
                gdox_background_lifecycle_apply(
                    &lifecycle, GDOX_BACKGROUND_QUIT_REQUESTED
                );
                continue;
            }
            initialize_imgui(deck);
            ui_state.emulator_window_hidden = false;
            window_open = true;
            gdox_app_background_set_window_visible(background_host, true);
#if defined(__APPLE__)
            if (WindowShouldClose()) {
                gdox_background_lifecycle_apply(
                    &lifecycle, GDOX_BACKGROUND_QUIT_REQUESTED
                );
                continue;
            }
#endif
        }

        if (gdox_background_lifecycle_take_window_activation(&lifecycle)) {
            RestoreWindow();
            SetWindowFocused();
        }

        ImGuiIO &io = ImGui::GetIO();
        const bool playback_running =
            snapshot != nullptr && snapshot->can_close;
        update_gamepad_input(playback_running, ui_state, io);
        if (wait_while_emulator_owns_display(
                deck,
                playback_running,
                ui_state
            )) {
            continue;
        }
        restore_deck_window(deck, ui_state, io);
        if (WindowShouldClose()) {
            gdox_background_lifecycle_apply(
                &lifecycle, GDOX_BACKGROUND_WINDOW_CLOSED
            );
            continue;
        }
        handle_page_navigation(deck, ui_state, app);
        import_dropped_files(app);
        if (draw_frame(app, deck)) {
            gdox_background_lifecycle_apply(
                &lifecycle, GDOX_BACKGROUND_QUIT_REQUESTED
            );
        }
    }

#if defined(_WIN32)
    if (!native_shutdown.complete) {
        if (!shutdown_application(app)) {
            exit_code = 1;
        }
    } else if (!native_shutdown.success) {
        exit_code = 1;
    }
#else
    if (!shutdown_application(app)) {
        exit_code = 1;
    }
#endif
    gdox_app_background_complete_shutdown(background_host);
    if (window_open) {
        close_desktop_window();
    }
#if !defined(_WIN32)
    if (termination_handler_installed) {
        gdox_app_termination_uninstall();
    }
#endif
    gdox_app_background_destroy(background_host);
    gdox_app_instance_release(instance);
    return exit_code;
}

int print_version()
{
    return std::printf("GDOX %s\n", GDOX_VERSION) < 0 ? 1 : 0;
}

#if defined(_WIN32)
bool command_line_requests_version(const char *command_line)
{
    static constexpr char option[] = "--version";
    const char *cursor = command_line;

    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    if (std::strncmp(cursor, option, sizeof(option) - 1U) != 0) {
        return false;
    }
    cursor += sizeof(option) - 1U;
    while (*cursor == ' ' || *cursor == '\t') {
        ++cursor;
    }
    return *cursor == '\0';
}
#endif

}

#if defined(_WIN32)
extern "C" int WinMain(void *, void *, char *command_line, int)
{
    if (command_line_requests_version(command_line)) {
        return print_version();
    }
    return run_application(
        gdox_background_command_line_requests_hidden(command_line)
    );
}
#else
int main(int argument_count, char **arguments)
{
    if (argument_count == 2
        && std::strcmp(arguments[1], "--version") == 0) {
        return print_version();
    }
    return run_application(gdox_background_arguments_request_hidden(
        argument_count, arguments
    ));
}
#endif
