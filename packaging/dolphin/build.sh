#!/bin/sh
# Run inside the compatibility Containerfile with a pristine extracted source.
set -eu
if [ "$#" -ne 3 ]; then
    echo "usage: build.sh SOURCE BUILD OUTPUT" >&2
    exit 2
fi
source_root=$(CDPATH='' cd -- "$1" && pwd -P)
recipe_root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
build_root=$2
output_root=$3
export SOURCE_DATE_EPOCH=315532800
for patch_file in "$recipe_root"/000*.patch; do
    patch --directory="$source_root" --strip=1 --forward --batch < "$patch_file"
done
cmake -S "$source_root" -B "$build_root" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DDISTRIBUTOR=GDOX -DENABLE_TESTS=OFF -DENABLE_AUTOUPDATE=OFF \
    -DENABLE_ANALYTICS=OFF -DUSE_DISCORD_PRESENCE=OFF -DUSE_UPNP=OFF \
    -DENABLE_NOGUI=OFF \
    "-DCMAKE_C_FLAGS=-ffile-prefix-map=$source_root=/usr/src/dolphin" \
    "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$source_root=/usr/src/dolphin"
cmake --build "$build_root" --parallel "${GDOX_BUILD_JOBS:-6}"
python3 "$recipe_root/stage.py" --source "$source_root" \
    --build "$build_root" --output "$output_root"
