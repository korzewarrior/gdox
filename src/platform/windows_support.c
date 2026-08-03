#define WIN32_LEAN_AND_MEAN

#include "platform/windows_support.h"

#include <aclapi.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void gdox_windows_io_error(
    gdox_error *error,
    const char *operation,
    DWORD code
)
{
    char detail[160] = {0};
    char message[GDOX_ERROR_MESSAGE_CAPACITY];
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        code,
        0U,
        detail,
        (DWORD)sizeof(detail),
        NULL
    );

    while (length > 0U
        && (detail[length - 1U] == '\r'
            || detail[length - 1U] == '\n'
            || detail[length - 1U] == ' ')) {
        detail[--length] = '\0';
    }
    (void)snprintf(
        message,
        sizeof(message),
        "%s: %s (Windows error %lu)",
        operation,
        length != 0U ? detail : "operating-system request failed",
        (unsigned long)code
    );
    gdox_error_set(error, GDOX_ERROR_IO, message);
}

static wchar_t *wide_utf8(
    const char *value,
    const char *kind,
    gdox_error *error
)
{
    char message[96];
    int characters;
    wchar_t *output;

    if (value == NULL || value[0] == '\0') {
        (void)snprintf(message, sizeof(message), "%s is required", kind);
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, message);
        return NULL;
    }
    characters = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value,
        -1,
        NULL,
        0
    );
    if (characters <= 0) {
        (void)snprintf(
            message, sizeof(message), "%s is not valid UTF-8", kind
        );
        gdox_windows_io_error(error, message, GetLastError());
        return NULL;
    }
    if ((size_t)characters > SIZE_MAX / sizeof(*output)) {
        (void)snprintf(message, sizeof(message), "%s is too long", kind);
        gdox_error_set(error, GDOX_ERROR_INVALID_ARGUMENT, message);
        return NULL;
    }
    output = malloc((size_t)characters * sizeof(*output));
    if (output == NULL) {
        (void)snprintf(
            message, sizeof(message), "could not allocate Windows %s", kind
        );
        gdox_error_set(error, GDOX_ERROR_INTERNAL, message);
        return NULL;
    }
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value,
            -1,
            output,
            characters
        ) != characters) {
        const DWORD code = GetLastError();
        free(output);
        (void)snprintf(
            message, sizeof(message), "could not convert Windows %s", kind
        );
        gdox_windows_io_error(error, message, code);
        return NULL;
    }
    return output;
}

wchar_t *gdox_windows_wide_text(
    const char *text,
    gdox_error *error
)
{
    return wide_utf8(text, "text", error);
}

wchar_t *gdox_windows_wide_path(
    const char *path,
    gdox_error *error
)
{
    wchar_t *output = wide_utf8(path, "path", error);

    if (output == NULL) {
        return NULL;
    }
    for (wchar_t *cursor = output; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'/') {
            *cursor = L'\\';
        }
    }
    return output;
}

static PSID current_user_sid(gdox_error *error)
{
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    PSID sid = NULL;
    DWORD bytes = 0U;
    DWORD code;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        gdox_windows_io_error(
            error, "could not inspect private directory owner", GetLastError()
        );
        return NULL;
    }
    (void)GetTokenInformation(token, TokenUser, NULL, 0U, &bytes);
    code = GetLastError();
    if (bytes == 0U || code != ERROR_INSUFFICIENT_BUFFER) {
        (void)CloseHandle(token);
        gdox_windows_io_error(
            error, "could not size private directory owner", code
        );
        return NULL;
    }
    user = malloc(bytes);
    if (user == NULL
        || !GetTokenInformation(token, TokenUser, user, bytes, &bytes)) {
        code = user == NULL ? ERROR_OUTOFMEMORY : GetLastError();
        free(user);
        (void)CloseHandle(token);
        gdox_windows_io_error(
            error, "could not read private directory owner", code
        );
        return NULL;
    }
    bytes = GetLengthSid(user->User.Sid);
    sid = malloc(bytes);
    if (sid == NULL || !CopySid(bytes, sid, user->User.Sid)) {
        code = sid == NULL ? ERROR_OUTOFMEMORY : GetLastError();
        free(sid);
        sid = NULL;
        gdox_windows_io_error(
            error, "could not copy private directory owner", code
        );
    }
    free(user);
    (void)CloseHandle(token);
    return sid;
}

bool gdox_windows_verify_private_directory(
    const wchar_t *path,
    gdox_error *error
)
{
    PSECURITY_DESCRIPTOR descriptor = NULL;
    PSID owner = NULL;
    PACL dacl = NULL;
    PSID caller = NULL;
    ACL_SIZE_INFORMATION acl_info;
    SECURITY_DESCRIPTOR_CONTROL control = 0U;
    DWORD revision = 0U;
    DWORD attributes;
    DWORD code = ERROR_SUCCESS;
    bool valid = false;

    gdox_error_clear(error);
    if (path == NULL || path[0] == L'\0') {
        gdox_error_set(
            error, GDOX_ERROR_INVALID_ARGUMENT, "private directory is required"
        );
        return false;
    }
    attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES
        || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U
        || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_SOURCE,
            "private storage path is not an ordinary directory"
        );
        return false;
    }
    caller = current_user_sid(error);
    if (caller == NULL) {
        return false;
    }
    code = GetNamedSecurityInfoW(
        (LPWSTR)path,
        SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        &owner,
        NULL,
        &dacl,
        NULL,
        &descriptor
    );
    if (code == ERROR_SUCCESS && descriptor != NULL && owner != NULL
        && dacl != NULL && EqualSid(owner, caller)
        && GetSecurityDescriptorControl(descriptor, &control, &revision)
        && (control & SE_DACL_PROTECTED) != 0U
        && GetAclInformation(
            dacl,
            &acl_info,
            sizeof(acl_info),
            AclSizeInformation
        ) && acl_info.AceCount == 1U) {
        ACCESS_ALLOWED_ACE *ace = NULL;

        if (GetAce(dacl, 0U, (void **)&ace) && ace != NULL
            && ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE
            && (ace->Mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS
            && EqualSid((PSID)&ace->SidStart, caller)) {
            valid = true;
        }
    }
    if (descriptor != NULL) {
        (void)LocalFree(descriptor);
    }
    free(caller);
    if (!valid) {
        if (code != ERROR_SUCCESS) {
            gdox_windows_io_error(
                error, "could not inspect private directory", code
            );
        } else {
            gdox_error_set(
                error,
                GDOX_ERROR_IO,
                "storage directory is not private and caller-owned"
            );
        }
    }
    return valid;
}

bool gdox_windows_ensure_private_directory(
    const wchar_t *path,
    bool *created,
    gdox_error *error
)
{
    SECURITY_DESCRIPTOR descriptor;
    SECURITY_ATTRIBUTES attributes;
    PSID sid;
    ACL *acl;
    DWORD acl_bytes;
    bool success;

    gdox_error_clear(error);
    if (created == NULL) {
        gdox_error_set(
            error,
            GDOX_ERROR_INVALID_ARGUMENT,
            "private directory creation result is required"
        );
        return false;
    }
    *created = false;
    sid = current_user_sid(error);
    if (sid == NULL) {
        return false;
    }
    acl_bytes = (DWORD)sizeof(ACL)
        + (DWORD)sizeof(ACCESS_ALLOWED_ACE) - (DWORD)sizeof(DWORD)
        + GetLengthSid(sid);
    acl = malloc(acl_bytes);
    if (acl == NULL
        || !InitializeAcl(acl, acl_bytes, ACL_REVISION)
        || !AddAccessAllowedAceEx(
            acl,
            ACL_REVISION,
            OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
            FILE_ALL_ACCESS,
            sid
        )
        || !InitializeSecurityDescriptor(
            &descriptor, SECURITY_DESCRIPTOR_REVISION
        )
        || !SetSecurityDescriptorOwner(&descriptor, sid, FALSE)
        || !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE)
        || !SetSecurityDescriptorControl(
            &descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED
        )) {
        const DWORD code = acl == NULL ? ERROR_OUTOFMEMORY : GetLastError();

        free(acl);
        free(sid);
        gdox_windows_io_error(
            error, "could not secure private directory", code
        );
        return false;
    }
    attributes = (SECURITY_ATTRIBUTES){
        .nLength = sizeof(attributes),
        .lpSecurityDescriptor = &descriptor,
        .bInheritHandle = FALSE,
    };
    success = CreateDirectoryW(path, &attributes) != 0;
    if (success) {
        *created = true;
    } else if (GetLastError() == ERROR_ALREADY_EXISTS) {
        success = true;
    } else {
        gdox_windows_io_error(
            error, "could not create private directory", GetLastError()
        );
    }
    free(acl);
    free(sid);
    if (!success) {
        return false;
    }
    if (gdox_windows_verify_private_directory(path, error)) {
        return true;
    }
    if (*created) {
        (void)RemoveDirectoryW(path);
        *created = false;
    }
    return false;
}
