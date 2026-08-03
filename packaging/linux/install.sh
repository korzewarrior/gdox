#!/bin/sh
set -eu

source_root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
data_home=${XDG_DATA_HOME:-"${HOME}/.local/share"}
opt_home="${HOME}/.local/opt"
bin_home="${HOME}/.local/bin"
desktop_directory="${data_home}/applications"
icon_directory="${data_home}/icons/hicolor/512x512/apps"
legacy_icon="${data_home}/icons/hicolor/scalable/apps/gdox.svg"
private_data="${data_home}/gdox"

move_legacy_preservations() {
    legacy_install=$1
    destination=$2
    migration_status="${legacy_install}/.gdox-migration.$$"
    find_succeeded=0
    migration_result=failed

    if [ ! -d "${legacy_install}" ]; then
        return
    fi
    if [ -e "${migration_status}" ]; then
        echo \
            "GDOX left the previous installation at ${legacy_install} because its migration state is not empty." \
            >&2
        return 1
    fi
    mkdir -p "${destination}"
    printf '%s\n' ready >"${migration_status}"
    if find "${legacy_install}" -maxdepth 1 -type f \
            \( -name '*.iso' -o -name '*.iso.*' \
            -o -name '*.xiso' -o -name '*.xiso.*' \) \
            -exec sh -c '
                migration_status=$1
                destination=$2
                shift 2
                for source do
                    name=${source##*/}
                    target=${destination}/${name}
                    suffix=1
                    while [ -e "${target}" ]; do
                        target=${destination}/migrated-${suffix}-${name}
                        suffix=$((suffix + 1))
                    done
                    if ! mv -- "${source}" "${target}"; then
                        printf "%s\n" failed >"${migration_status}" || true
                        echo "Could not move preserved image data from ${source}." >&2
                        exit 1
                    fi
                    echo "Moved preserved image data to ${target}."
                done
            ' sh "${migration_status}" "${destination}" {} +;
    then
        find_succeeded=1
    fi
    if IFS= read -r migration_result <"${migration_status}"; then
        :
    else
        migration_result=failed
    fi
    if ! rm -f -- "${migration_status}"; then
        migration_result=failed
    fi
    if remaining_image=$(find "${legacy_install}" -maxdepth 1 -type f \
            \( -name '*.iso' -o -name '*.iso.*' \
            -o -name '*.xiso' -o -name '*.xiso.*' \) \
            -print -quit);
    then
        if [ -n "${remaining_image}" ]; then
            migration_result=failed
        fi
    else
        migration_result=failed
    fi
    if [ "${find_succeeded}" != "1" ] \
        || [ "${migration_result}" != "ready" ]; then
        echo \
            "GDOX retained the previous installation at ${legacy_install} so its preserved images can be recovered." \
            >&2
        return 1
    fi
}

mkdir -p "${data_home}" "${opt_home}"
if [ -L "${private_data}" ]; then
    echo \
        "GDOX refused a symbolic link at its private data directory: ${private_data}" \
        >&2
    exit 1
fi
mkdir -p "${private_data}"
if [ -L "${private_data}" ] || [ ! -d "${private_data}" ]; then
    echo \
        "GDOX could not establish its private data directory: ${private_data}" \
        >&2
    exit 1
fi
canonical_install_root=$(CDPATH='' cd -- "${opt_home}" && pwd -P)/gdox
if [ "${source_root}" != "${canonical_install_root}" ]; then
    temporary="${canonical_install_root}.installing.$$"
    previous="${canonical_install_root}.previous.$$"
    if [ -e "${temporary}" ] || [ -e "${previous}" ]; then
        echo "GDOX found an existing installer transaction and left it untouched." >&2
        exit 1
    fi
    old_moved=0
    new_activated=0
    transaction_committed=0
    cleanup_install_transaction() {
        status=$?
        rollback_failed=0

        trap - EXIT HUP INT TERM
        set +e
        if [ "${transaction_committed}" != "1" ]; then
            if [ "${new_activated}" = "1" ]; then
                rm -rf -- "${canonical_install_root}"
            fi
            if [ "${old_moved}" = "1" ] && [ -e "${previous}" ]; then
                if [ -e "${canonical_install_root}" ] \
                    || ! mv -- "${previous}" "${canonical_install_root}"; then
                    rollback_failed=1
                    echo \
                        "GDOX could not restore the previous installation. It remains at ${previous}." \
                        >&2
                fi
            fi
        fi
        rm -rf -- "${temporary}"
        if [ "${rollback_failed}" = "1" ] && [ "${status}" = "0" ]; then
            status=1
        fi
        exit "${status}"
    }
    trap cleanup_install_transaction EXIT
    trap 'exit 129' HUP
    trap 'exit 130' INT
    trap 'exit 143' TERM
    mkdir -p "${temporary}"
    cp -a "${source_root}/." "${temporary}/"
    if [ -e "${canonical_install_root}" ]; then
        if mv -- "${canonical_install_root}" "${previous}"; then
            old_moved=1
        else
            if [ ! -e "${canonical_install_root}" ] \
                && [ -e "${previous}" ]; then
                old_moved=1
            fi
            exit 1
        fi
    fi
    if mv -- "${temporary}" "${canonical_install_root}"; then
        new_activated=1
    else
        if [ ! -e "${temporary}" ] \
            && [ -e "${canonical_install_root}" ]; then
            new_activated=1
        fi
        exit 1
    fi
    transaction_committed=1
    trap - EXIT HUP INT TERM
    if [ "${old_moved}" = "1" ]; then
        if ! move_legacy_preservations \
                "${previous}" \
                "${private_data}/preservations";
        then
            exit 1
        fi
        rm -rf -- "${previous}"
    fi
fi

# Early development packages stored application files beside private xemu data.
# Remove only known application payloads; firmware, configuration, saves, and
# preservation output remain untouched.
legacy_root="${data_home}/gdox"
if [ "${legacy_root}" != "${canonical_install_root}" ]; then
    rm -rf -- \
        "${legacy_root:?}/catalog" \
        "${legacy_root:?}/docs" \
        "${legacy_root:?}/lib" \
        "${legacy_root:?}/libexec" \
        "${legacy_root:?}/licenses" \
        "${legacy_root:?}/packaging" \
        "${legacy_root:?}/runtime"
    rm -f -- \
        "${legacy_root:?}/CHANGELOG.md" \
        "${legacy_root:?}/LICENSE" \
        "${legacy_root:?}/README-FIRST.md" \
        "${legacy_root:?}/README.md" \
        "${legacy_root:?}/THIRD_PARTY_NOTICES.md" \
        "${legacy_root:?}/gdox" \
        "${legacy_root:?}/imgui.ini" \
        "${legacy_root:?}/install.sh" \
        "${legacy_root:?}/setup-device-access.sh"
fi

mkdir -p "${bin_home}" "${desktop_directory}" "${icon_directory}"
ln -sfn "${canonical_install_root}/gdox" "${bin_home}/gdox"
# Early packages exposed a second launcher beside private runtime data.
rm -f -- "${private_data}/gdox"
cp "${canonical_install_root}/packaging/gdox.png" \
    "${icon_directory}/gdox.png"
rm -f -- "${legacy_icon}"

desktop_entry="${desktop_directory}/org.gdox.gdox.desktop"
if printf '%s' "${canonical_install_root}/gdox" | grep '[\\"`$&|]' >/dev/null; then
    echo "GDOX cannot create a launcher for this home-directory path." >&2
    exit 1
fi
sed \
    "s|^Exec=.*$|Exec=\"${canonical_install_root}/gdox\"|" \
    "${canonical_install_root}/packaging/org.gdox.gdox.desktop" \
    >"${desktop_entry}"
chmod 0644 "${desktop_entry}" "${icon_directory}/gdox.png"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${desktop_directory}" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache \
        --force \
        --ignore-theme-index \
        "${data_home}/icons/hicolor" >/dev/null 2>&1 || true
fi

missing_xbox360_helpers=
for helper in nbdfuse fusermount3; do
    bundled_helper_ready=0
    if [ "${helper}" = "nbdfuse" ] \
        && [ -x "${canonical_install_root}/libexec/nbdfuse" ] \
        && [ -x "${canonical_install_root}/libexec/nbdfuse.bin" ]; then
        bundled_helper_ready=1
    fi
    if ! command -v "${helper}" >/dev/null 2>&1 \
        && [ "${bundled_helper_ready}" != "1" ];
    then
        missing_xbox360_helpers="${missing_xbox360_helpers} ${helper}"
    fi
done
if [ -n "${missing_xbox360_helpers}" ]; then
    echo \
        "Xbox 360 playback, including owned images, is unavailable until these host tools are installed:${missing_xbox360_helpers}." \
        >&2
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
