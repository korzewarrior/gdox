#include "gdox/error.h"
#include "platform/windows_command.h"

#include <stdio.h>
#include <wchar.h>

int main(void)
{
    static const char uri[] =
        "nbd://127.0.0.1:49152/0123456789abcdef0123456789abcdef";
    static const wchar_t expected[] =
        L"\"nbd://127.0.0.1:49152/0123456789abcdef0123456789abcdef\"";
    gdox_windows_command command = {0};
    gdox_error error;

    gdox_error_clear(&error);
    if (!gdox_windows_command_add_utf8(&command, uri, &error)) {
        (void)fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    if (wcscmp(command.text, expected) != 0) {
        (void)fwprintf(
            stderr,
            L"Windows command argument changed the NBD URI: %ls\n",
            command.text
        );
        gdox_windows_command_destroy(&command);
        return 1;
    }
    gdox_windows_command_destroy(&command);
    return 0;
}
