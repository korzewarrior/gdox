#!/bin/sh

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
gdox_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
output_root=${GDOX_OUTPUT_ROOT:-${GDOX_OUTPUT_DIR:-"$gdox_root/../gdox-output"}}
source_root=$output_root/build/android-emulator/source
temporary_root=$output_root/cache/android-tmp
assets_root=$output_root/build/android-assets
build_type=${1:-debug}
version=$(python "$gdox_root/scripts/project_version.py")

# shellcheck source=android/dependencies.lock
. "$gdox_root/android/dependencies.lock"

if [ -z "$version" ]; then
    echo "could not read the GDOX version from CMakeLists.txt" >&2
    exit 1
fi

case "$build_type" in
    debug)
        gradle_task=assembleDebug
        apk_variant=debug
        output_name=gdox-"$version"-android-arm64-debug.apk
        ;;
    release)
        gradle_task=assembleRelease
        apk_variant=release
        output_name=gdox-"$version"-android-arm64.apk
        ;;
    *)
        echo "usage: $0 [debug|release]" >&2
        exit 2
        ;;
esac

require_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "required command is unavailable: $1" >&2
        exit 1
    fi
}

checkout_exact()
{
    repository=$1
    revision=$2
    destination=$3

    if [ ! -d "$destination/.git" ]; then
        git clone --filter=blob:none --no-checkout "$repository" "$destination"
        git -C "$destination" checkout --detach "$revision"
    fi
    actual=$(git -C "$destination" rev-parse HEAD)
    if [ "$actual" != "$revision" ]; then
        echo "$destination is at $actual, expected $revision" >&2
        exit 1
    fi
}

require_command git
require_command sha256sum
require_command python

android_sdk=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}
if [ -z "$android_sdk" ]; then
    echo "set ANDROID_SDK_ROOT to an Android SDK containing API 36" >&2
    exit 1
fi
if [ -z "${JAVA_HOME:-}" ] || [ ! -x "$JAVA_HOME/bin/java" ]; then
    echo "set JAVA_HOME to JDK 21" >&2
    exit 1
fi
if [ ! -d "$android_sdk/platforms/android-36" ]; then
    echo "Android platform 36 is not installed in $android_sdk" >&2
    exit 1
fi
if [ ! -d "$android_sdk/ndk/29.0.14206865" ]; then
    echo "Android NDK 29.0.14206865 is not installed in $android_sdk" >&2
    exit 1
fi

mkdir -p "$source_root" "$temporary_root" "$assets_root" \
    "$output_root/release/android"

python "$gdox_root/scripts/fetch_runtime.py" asset \
    --name hdd \
    --output "$assets_root/xbox_hdd.qcow2"
python "$gdox_root/scripts/fetch_runtime.py" asset \
    --name hdd-license \
    --output "$assets_root/xbox_hdd-LICENSE.txt"

xemu_root=$("$gdox_root/scripts/prepare_android_emulator.sh")
sdl2_root=$("$gdox_root/scripts/prepare_android_sdl2.sh")
libusb_name=$(printf %.12s "$LIBUSB_REVISION")
libusb_root=$source_root/libusb-$libusb_name
checkout_exact "$LIBUSB_REPOSITORY" "$LIBUSB_REVISION" "$libusb_root"

key_properties=${GDOX_ANDROID_KEY_PROPERTIES:-"$xemu_root/android/key.properties"}
environment_signing=0
for value in \
    "${GDOX_ANDROID_KEYSTORE_FILE:-}" \
    "${GDOX_ANDROID_KEYSTORE_PASSWORD:-}" \
    "${GDOX_ANDROID_KEY_ALIAS:-}" \
    "${GDOX_ANDROID_KEY_PASSWORD:-}"
do
    if [ -n "$value" ]; then
        environment_signing=$((environment_signing + 1))
    fi
done

if [ "$build_type" = release ] && [ "$environment_signing" -ne 0 ]; then
    if [ "$environment_signing" -ne 4 ]; then
        echo "Android release signing environment is incomplete." >&2
        exit 1
    fi
    if [ ! -f "$GDOX_ANDROID_KEYSTORE_FILE" ]; then
        echo "Android release keystore does not exist." >&2
        exit 1
    fi
elif [ "$build_type" = release ]; then
    if [ ! -f "$key_properties" ]; then
        echo "Android release signing is not configured." >&2
        echo "Set the four GDOX_ANDROID_KEY* variables or GDOX_ANDROID_KEY_PROPERTIES." >&2
        exit 1
    fi
    for property in storeFile storePassword keyAlias keyPassword; do
        if ! grep -Eq \
            "^[[:space:]]*${property}[[:space:]]*=[[:space:]]*[^[:space:]]" \
            "$key_properties"; then
            echo "Android signing configuration is missing $property." >&2
            exit 1
        fi
    done
fi

java_options=${JAVA_TOOL_OPTIONS:-}
if [ -n "$java_options" ]; then
    java_options="$java_options "
fi
java_options="${java_options}-Djava.io.tmpdir=$temporary_root"

(
    cd "$xemu_root/android"
    TMPDIR=$temporary_root \
    JAVA_TOOL_OPTIONS=$java_options \
    ANDROID_HOME=$android_sdk \
    ANDROID_SDK_ROOT=$android_sdk \
    GDOX_SOURCE_DIR=$gdox_root \
    GDOX_ASSETS_DIR=$assets_root \
    GDOX_LIBUSB_SOURCE_DIR=$libusb_root \
    GDOX_SDL2_SOURCE_DIR=$sdl2_root \
    ./gradlew \
        --no-daemon \
        "$gradle_task" \
        -PgdoxVersion="$version" \
        -PgdoxKeyProperties="$key_properties" \
        -Pkotlin.compiler.execution.strategy=in-process
)

apk=$xemu_root/android/app/build/outputs/apk/$apk_variant/app-"$apk_variant".apk
if [ ! -f "$apk" ]; then
    echo "Gradle completed without producing $apk" >&2
    exit 1
fi
if [ "$build_type" = release ]; then
    apksigner=$android_sdk/build-tools/36.1.0/apksigner
    if [ ! -x "$apksigner" ]; then
        echo "required command is unavailable: $apksigner" >&2
        exit 1
    fi
    "$apksigner" verify --verbose "$apk"
fi

cp "$apk" "$output_root/release/android/$output_name"
(
    cd "$output_root/release/android"
    sha256sum "$output_name" > "$output_name.sha256"
)
