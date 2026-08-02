#include "test.h"

#include "app/runtime_commands.h"

#include <string.h>

static gdox_runtime_request_entry request(gdox_runtime_request_kind kind)
{
    const gdox_runtime_request_entry value = {.kind = kind};
    return value;
}

static void check_ordered_queue(void)
{
    gdox_runtime_request_queue queue = {0};
    gdox_runtime_request_entry start = request(GDOX_RUNTIME_REQUEST_START);
    gdox_runtime_request_entry close = request(GDOX_RUNTIME_REQUEST_CLOSE);
    gdox_runtime_request_entry open = request(GDOX_RUNTIME_REQUEST_OPEN_IMAGE);
    gdox_runtime_request_entry output;

    memcpy(open.path, "first.iso", sizeof "first.iso");
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &start));
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &start));
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &open));
    memcpy(open.path, "second.iso", sizeof "second.iso");
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &open));
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &close));
    memcpy(open.path, "third.iso", sizeof "third.iso");
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &open));
    GDOX_TEST_CHECK(queue.count == 4U);

    GDOX_TEST_CHECK(gdox_runtime_request_dequeue(&queue, &output));
    GDOX_TEST_CHECK(output.kind == GDOX_RUNTIME_REQUEST_START);
    GDOX_TEST_CHECK(queue.count == 3U);
    GDOX_TEST_CHECK(gdox_runtime_request_dequeue(&queue, &output));
    GDOX_TEST_CHECK(output.kind == GDOX_RUNTIME_REQUEST_OPEN_IMAGE);
    GDOX_TEST_CHECK(strcmp(output.path, "second.iso") == 0);
    GDOX_TEST_CHECK(gdox_runtime_request_dequeue(&queue, &output));
    GDOX_TEST_CHECK(output.kind == GDOX_RUNTIME_REQUEST_CLOSE);
    GDOX_TEST_CHECK(gdox_runtime_request_dequeue(&queue, &output));
    GDOX_TEST_CHECK(output.kind == GDOX_RUNTIME_REQUEST_OPEN_IMAGE);
    GDOX_TEST_CHECK(strcmp(output.path, "third.iso") == 0);
    GDOX_TEST_CHECK(!gdox_runtime_request_dequeue(&queue, &output));
}

static void check_payload_ownership(void)
{
    gdox_runtime_request_queue queue = {0};
    gdox_runtime_request_entry preserve = {
        .kind = GDOX_RUNTIME_REQUEST_PRESERVE,
        .preservation_format = GDOX_PRESERVATION_REDUMP,
        .preservation_verify = true,
    };
    gdox_runtime_request_entry output;

    memcpy(preserve.path, "original.iso", sizeof "original.iso");
    GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &preserve));
    memcpy(preserve.path, "mutated.iso", sizeof "mutated.iso");
    GDOX_TEST_CHECK(gdox_runtime_request_dequeue(&queue, &output));
    GDOX_TEST_CHECK(strcmp(output.path, "original.iso") == 0);
    GDOX_TEST_CHECK(
        output.preservation_format == GDOX_PRESERVATION_REDUMP
    );
    GDOX_TEST_CHECK(output.preservation_verify);
}

static void check_capacity(void)
{
    gdox_runtime_request_queue queue = {0};
    gdox_runtime_request_entry extra;
    size_t index;

    for (index = 0U; index < GDOX_RUNTIME_REQUEST_CAPACITY; ++index) {
        gdox_runtime_request_entry value = request(
            index % 2U == 0U
                ? GDOX_RUNTIME_REQUEST_START
                : GDOX_RUNTIME_REQUEST_CLOSE
        );
        GDOX_TEST_CHECK(gdox_runtime_request_enqueue(&queue, &value));
    }
    extra = request(GDOX_RUNTIME_REQUEST_EJECT);
    GDOX_TEST_CHECK(!gdox_runtime_request_enqueue(&queue, &extra));
    GDOX_TEST_CHECK(queue.count == GDOX_RUNTIME_REQUEST_CAPACITY);
}

static void check_planner(void)
{
    gdox_runtime_request_entry value = request(GDOX_RUNTIME_REQUEST_START);
    gdox_runtime_command_state state = {
        .media_source = GDOX_MEDIA_PHYSICAL_DISC,
    };

    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_DISCOVER_PHYSICAL
    );
    state.media_source = GDOX_MEDIA_DISC_IMAGE;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_NONE
    );
    state.has_saved_image = true;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_REOPEN_IMAGE
    );
    state.has_session = true;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_START_EMULATOR
    );
    state.emulator_running = true;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_NONE
    );

    value.kind = GDOX_RUNTIME_REQUEST_CLOSE;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_STOP_EMULATOR
    );
    value.kind = GDOX_RUNTIME_REQUEST_RESTART;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_RESTART_EMULATOR
    );
    value.kind = GDOX_RUNTIME_REQUEST_APPLY_DISPLAY;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_APPLY_DISPLAY
    );
    value.kind = GDOX_RUNTIME_REQUEST_EJECT;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_NONE
    );
    state.media_source = GDOX_MEDIA_PHYSICAL_DISC;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_EJECT_PHYSICAL
    );
    value.kind = GDOX_RUNTIME_REQUEST_OPEN_IMAGE;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_OPEN_IMAGE
    );
    value.kind = GDOX_RUNTIME_REQUEST_USE_PHYSICAL;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_USE_PHYSICAL
    );
    value.kind = GDOX_RUNTIME_REQUEST_PRESERVE;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_PRESERVE_PHYSICAL
    );
    state.has_session = false;
    GDOX_TEST_CHECK(
        gdox_runtime_plan_request(&value, &state)
            == GDOX_RUNTIME_ACTION_NONE
    );
}

void gdox_test_runtime_commands(void)
{
    check_ordered_queue();
    check_payload_ownership();
    check_capacity();
    check_planner();
}
