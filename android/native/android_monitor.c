#include "gdox/android_disc.h"

#include <jni.h>
#include <stdint.h>
#include <string.h>

static void throw_io_exception(JNIEnv *environment, const char *message)
{
    jclass exception_class = (*environment)->FindClass(
        environment,
        "java/io/IOException"
    );

    if (exception_class != NULL) {
        (void)(*environment)->ThrowNew(
            environment,
            exception_class,
            message
        );
        (*environment)->DeleteLocalRef(environment, exception_class);
    }
}

static gdox_android_drive_monitor *monitor_from_handle(jlong handle)
{
    return (gdox_android_drive_monitor *)(intptr_t)handle;
}

JNIEXPORT jlong JNICALL
Java_org_korze_gdox_android_GdoxDiscMonitor_openNative(
    JNIEnv *environment,
    jobject instance,
    jint file_descriptor
)
{
    gdox_android_drive_monitor *monitor = NULL;
    gdox_error error;

    (void)instance;
    if (!gdox_android_drive_monitor_open(
            (int)file_descriptor,
            &monitor,
            &error
        )) {
        throw_io_exception(environment, error.message);
        return (jlong)0;
    }
    return (jlong)(intptr_t)monitor;
}

JNIEXPORT jint JNICALL
Java_org_korze_gdox_android_GdoxDiscMonitor_pollNative(
    JNIEnv *environment,
    jobject instance,
    jlong handle
)
{
    gdox_android_media_state state;
    gdox_error error;

    (void)instance;
    if (!gdox_android_drive_monitor_poll(
            monitor_from_handle(handle),
            &state,
            &error
        )) {
        throw_io_exception(environment, error.message);
        return (jint)GDOX_ANDROID_MEDIA_CHANGING;
    }
    return (jint)state;
}

JNIEXPORT jbyteArray JNICALL
Java_org_korze_gdox_android_GdoxDiscMonitor_identifyNative(
    JNIEnv *environment,
    jobject instance,
    jint file_descriptor
)
{
    gdox_android_disc_info info;
    gdox_error error;
    size_t title_length;
    jbyteArray title;

    (void)instance;
    if (!gdox_android_disc_identify(
            (int)file_descriptor,
            &info,
            &error
        )) {
        throw_io_exception(environment, error.message);
        return NULL;
    }
    title_length = strlen(info.title);
    title = (*environment)->NewByteArray(
        environment,
        (jsize)title_length
    );
    if (title != NULL && title_length != 0U) {
        (*environment)->SetByteArrayRegion(
            environment,
            title,
            0,
            (jsize)title_length,
            (const jbyte *)info.title
        );
    }
    if (title == NULL && !(*environment)->ExceptionCheck(environment)) {
        throw_io_exception(
            environment,
            "could not create the Android disc-title data"
        );
    }
    return title;
}

JNIEXPORT void JNICALL
Java_org_korze_gdox_android_GdoxDiscMonitor_ejectNative(
    JNIEnv *environment,
    jobject instance,
    jlong handle
)
{
    gdox_error error;

    (void)instance;
    if (!gdox_android_drive_monitor_eject(
            monitor_from_handle(handle),
            &error
        )) {
        throw_io_exception(environment, error.message);
    }
}

JNIEXPORT void JNICALL
Java_org_korze_gdox_android_GdoxDiscMonitor_closeNative(
    JNIEnv *environment,
    jobject instance,
    jlong handle
)
{
    gdox_error error;

    (void)instance;
    if (!gdox_android_drive_monitor_close(
            monitor_from_handle(handle),
            &error
        )) {
        throw_io_exception(environment, error.message);
    }
}

JNIEXPORT void JNICALL
Java_org_korze_gdox_android_GdoxDiscMonitor_handoffNative(
    JNIEnv *environment,
    jobject instance,
    jlong handle
)
{
    gdox_error error;

    (void)instance;
    if (!gdox_android_drive_monitor_handoff(
            monitor_from_handle(handle),
            &error
        )) {
        throw_io_exception(environment, error.message);
    }
}
