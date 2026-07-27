#!/bin/sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
gdox_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
output_root=${GDOX_OUTPUT_ROOT:-${GDOX_OUTPUT_DIR:-"$gdox_root/../gdox-output"}}
patch_root=$gdox_root/android/emulator/sdl2/patches
series_file=$patch_root/series

. "$gdox_root/android/dependencies.lock"

require_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "required command is unavailable: $1" >&2
        exit 1
    fi
}

require_command git
require_command sha256sum

if [ ! -f "$series_file" ]; then
    echo "SDL2 patch series is missing: $series_file" >&2
    exit 1
fi

patch_set=$(
    while IFS= read -r patch; do
        case "$patch" in
            ""|\#*) continue ;;
        esac
        file=$patch_root/$patch
        if [ ! -f "$file" ]; then
            echo "SDL2 patch is missing: $file" >&2
            exit 1
        fi
        printf '%s\0' "$patch"
        sha256sum "$file" | cut -d ' ' -f 1
    done < "$series_file" | sha256sum | cut -d ' ' -f 1
)

source_root=$output_root/build/android-emulator/source
base_name=$(printf %.12s "$SDL2_REVISION")
patch_name=$(printf %.12s "$patch_set")
checkout_root=$source_root/sdl2-$base_name-$patch_name
stamp=$checkout_root/.gdox-patch-set

mkdir -p "$source_root"
if [ ! -d "$checkout_root/.git" ]; then
    git clone --filter=blob:none --no-checkout \
        "$SDL2_REPOSITORY" "$checkout_root"
    git -C "$checkout_root" checkout --detach "$SDL2_REVISION"
fi

actual=$(git -C "$checkout_root" rev-parse HEAD)
if [ "$actual" != "$SDL2_REVISION" ]; then
    echo "$checkout_root is at $actual, expected $SDL2_REVISION" >&2
    exit 1
fi

if [ -f "$stamp" ]; then
    recorded=$(sed -n '1p' "$stamp")
    if [ "$recorded" != "$patch_set" ]; then
        echo "$checkout_root contains a different SDL2 patch set" >&2
        exit 1
    fi
    printf '%s\n' "$checkout_root"
    exit 0
fi

if ! git -C "$checkout_root" diff --quiet \
    || ! git -C "$checkout_root" diff --cached --quiet; then
    echo "$checkout_root contains unrecorded changes" >&2
    exit 1
fi

while IFS= read -r patch; do
    case "$patch" in
        ""|\#*) continue ;;
    esac
    git -C "$checkout_root" apply --check --whitespace=error-all \
        "$patch_root/$patch"
    git -C "$checkout_root" apply "$patch_root/$patch"
done < "$series_file"

git -C "$checkout_root" diff --check
printf '%s\n' "$patch_set" > "$stamp"
printf '%s\n' "$checkout_root"
