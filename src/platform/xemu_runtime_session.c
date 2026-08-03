#include "platform/xemu_runtime_session.h"

bool gdox_xemu_runtime_session_open(
    gdox_session_storage *storage,
    gdox_error *error
)
{
#if defined(__linux__)
    return gdox_session_storage_recover_memory(error)
        && gdox_session_storage_create_memory(storage, error);
#else
    return gdox_session_storage_recover(error)
        && gdox_session_storage_create(storage, error);
#endif
}
