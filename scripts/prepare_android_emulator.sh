#!/bin/sh

set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
gdox_root=$(CDPATH='' cd -- "$script_dir/.." && pwd)
output_root=${GDOX_OUTPUT_ROOT:-${GDOX_OUTPUT_DIR:-"$gdox_root/../gdox-output"}}
patch_root=$gdox_root/android/emulator/patches
series_file=$patch_root/series

# shellcheck source=android/dependencies.lock
. "$gdox_root/android/dependencies.lock"

require_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "required command is unavailable: $1" >&2
        exit 1
    fi
}

require_command git
require_command python
require_command sha256sum

if [ ! -f "$series_file" ]; then
    echo "Android emulator patch series is missing: $series_file" >&2
    exit 1
fi

patch_set=$(
    while IFS= read -r patch; do
        case "$patch" in
            ""|\#*) continue ;;
        esac
        file=$patch_root/$patch
        if [ ! -f "$file" ]; then
            echo "Android emulator patch is missing: $file" >&2
            exit 1
        fi
        printf '%s\0' "$patch"
        sha256sum "$file" | cut -d ' ' -f 1
    done < "$series_file" | sha256sum | cut -d ' ' -f 1
)

source_root=$output_root/build/android-emulator/source
base_name=$(printf %.12s "$XEMU_ANDROID_BASE_REVISION")
patch_name=$(printf %.12s "$patch_set")
checkout_root=$source_root/xemu-$base_name-$patch_name
stamp=$checkout_root/.gdox-patch-set

mkdir -p "$source_root"
if [ ! -d "$checkout_root/.git" ]; then
    git clone --filter=blob:none --no-checkout \
        "$XEMU_REPOSITORY" "$checkout_root"
    git -C "$checkout_root" checkout --detach "$XEMU_ANDROID_BASE_REVISION"
fi

actual=$(git -C "$checkout_root" rev-parse HEAD)
if [ "$actual" != "$XEMU_ANDROID_BASE_REVISION" ]; then
    echo "$checkout_root is at $actual, expected $XEMU_ANDROID_BASE_REVISION" >&2
    exit 1
fi

if [ -f "$stamp" ]; then
    recorded=$(sed -n '1p' "$stamp")
    if [ "$recorded" != "$patch_set" ]; then
        echo "$checkout_root contains a different Android patch set" >&2
        exit 1
    fi
fi

python "$gdox_root/scripts/android_patchset.py" \
    "$checkout_root" "$patch_root"

wrapper_properties=$checkout_root/android/gradle/wrapper/gradle-wrapper.properties
wrapper_jar=$checkout_root/android/gradle/wrapper/gradle-wrapper.jar
if ! grep -Fqx \
    "distributionSha256Sum=$GRADLE_DISTRIBUTION_SHA256" \
    "$wrapper_properties"; then
    echo "$wrapper_properties does not pin the expected Gradle distribution" >&2
    exit 1
fi
wrapper_jar_sha256=$(sha256sum "$wrapper_jar" | cut -d ' ' -f 1)
if [ "$wrapper_jar_sha256" != "$GRADLE_WRAPPER_JAR_SHA256" ]; then
    echo "$wrapper_jar does not match the pinned Gradle wrapper" >&2
    exit 1
fi

printf '%s\n' "$patch_set" > "$stamp"
printf '%s\n' "$checkout_root"
