#!/bin/sh
set -eu

source_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
data_home=${XDG_DATA_HOME:-"${HOME}/.local/share"}
opt_home="${HOME}/.local/opt"
bin_home="${HOME}/.local/bin"
install_root="${opt_home}/gdox"
desktop_directory="${data_home}/applications"
icon_directory="${data_home}/icons/hicolor/scalable/apps"
private_data="${data_home}/gdox"

move_legacy_preservations() {
    legacy_install=$1
    destination=$2

    if [ ! -d "${legacy_install}" ]; then
        return
    fi
    mkdir -p "${destination}"
    find "${legacy_install}" -maxdepth 1 -type f \
        \( -name '*.iso' -o -name '*.iso.*' \
        -o -name '*.xiso' -o -name '*.xiso.*' \) \
        -exec sh -c '
            destination=$1
            shift
            for source do
                name=${source##*/}
                target=${destination}/${name}
                suffix=1
                while [ -e "${target}" ]; do
                    target=${destination}/migrated-${suffix}-${name}
                    suffix=$((suffix + 1))
                done
                mv -- "${source}" "${target}"
                echo "Moved preserved image data to ${target}."
            done
        ' sh "${destination}" {} +
}

mkdir -p "${data_home}" "${opt_home}" "${private_data}"
canonical_install_root=$(CDPATH= cd -- "${opt_home}" && pwd -P)/gdox
if [ "${source_root}" != "${canonical_install_root}" ]; then
    temporary="${canonical_install_root}.installing.$$"
    previous="${canonical_install_root}.previous.$$"
    cleanup() {
        rm -rf -- "${temporary}" "${previous}"
    }
    trap cleanup EXIT HUP INT TERM
    rm -rf -- "${temporary}" "${previous}"
    mkdir -p "${temporary}"
    cp -a "${source_root}/." "${temporary}/"
    move_legacy_preservations \
        "${canonical_install_root}" \
        "${private_data}/preservations"
    if [ -e "${canonical_install_root}" ]; then
        mv -- "${canonical_install_root}" "${previous}"
    fi
    mv -- "${temporary}" "${canonical_install_root}"
    rm -rf -- "${previous}"
    trap - EXIT HUP INT TERM
fi

# Early development packages stored application files beside private xemu data.
# Remove only known application payloads; firmware, configuration, saves, and
# preservation output remain untouched.
legacy_root="${data_home}/gdox"
if [ "${legacy_root}" != "${canonical_install_root}" ]; then
    rm -rf -- \
        "${legacy_root}/catalog" \
        "${legacy_root}/docs" \
        "${legacy_root}/lib" \
        "${legacy_root}/libexec" \
        "${legacy_root}/licenses" \
        "${legacy_root}/packaging" \
        "${legacy_root}/runtime"
    rm -f -- \
        "${legacy_root}/CHANGELOG.md" \
        "${legacy_root}/LICENSE" \
        "${legacy_root}/README-FIRST.md" \
        "${legacy_root}/README.md" \
        "${legacy_root}/THIRD_PARTY_NOTICES.md" \
        "${legacy_root}/gdox" \
        "${legacy_root}/imgui.ini" \
        "${legacy_root}/install.sh" \
        "${legacy_root}/setup-device-access.sh"
fi

mkdir -p "${bin_home}" "${desktop_directory}" "${icon_directory}"
ln -sfn "${canonical_install_root}/gdox" "${bin_home}/gdox"
# Early packages exposed a second launcher beside private runtime data.
rm -f -- "${private_data}/gdox"
cp "${canonical_install_root}/packaging/gdox.svg" \
    "${icon_directory}/gdox.svg"

desktop_entry="${desktop_directory}/org.gdox.gdox.desktop"
if printf '%s' "${canonical_install_root}/gdox" | grep '[\\"`$&|]' >/dev/null; then
    echo "GDOX cannot create a launcher for this home-directory path." >&2
    exit 1
fi
sed \
    "s|^Exec=.*$|Exec=\"${canonical_install_root}/gdox\"|" \
    "${canonical_install_root}/packaging/org.gdox.gdox.desktop" \
    >"${desktop_entry}"
chmod 0644 "${desktop_entry}" "${icon_directory}/gdox.svg"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${desktop_directory}" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache \
        --force \
        --ignore-theme-index \
        "${data_home}/icons/hicolor" >/dev/null 2>&1 || true
fi

if [ "${GDOX_SKIP_DEVICE_SETUP:-0}" != "1" ] && command -v udevadm >/dev/null 2>&1; then
    echo "GDOX needs one administrator prompt to access the supported USB drive."
    if ! sudo bash "${canonical_install_root}/setup-device-access.sh"; then
        echo "Device access was not installed. Run sudo ${canonical_install_root}/setup-device-access.sh before using the drive." >&2
    fi
fi

steam_shortcuts="${data_home}/Steam/userdata"
steam_has_gdox=0
if [ -d "${steam_shortcuts}" ] \
    && grep -a -l -r -m 1 "GDOX" "${steam_shortcuts}"/*/config/shortcuts.vdf \
        >/dev/null 2>&1; then
    steam_has_gdox=1
fi
if command -v steamos-add-to-steam >/dev/null 2>&1; then
    if [ "${steam_has_gdox}" = "0" ]; then
        steamos-add-to-steam "${desktop_entry}" >/dev/null 2>&1 || true
        echo "GDOX was added to the Steam library."
    else
        echo "The existing GDOX Steam shortcut was kept."
    fi
    if [ -x "${canonical_install_root}/packaging/steamdeck-artwork.py" ]; then
        "${canonical_install_root}/packaging/steamdeck-artwork.py" \
            "${canonical_install_root}/packaging/steam-artwork" || true
    fi
fi

echo "GDOX is installed. Start it from the app menu or run ${bin_home}/gdox."
