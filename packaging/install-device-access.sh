#!/usr/bin/env bash
set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
    echo "Run this installer with sudo." >&2
    exit 1
fi

script_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
if [[ -f "${script_root}/packaging/60-gdox.rules" ]]; then
    project_root=${script_root}
else
    project_root=$(cd -- "${script_root}/.." && pwd)
fi
install -Dm644 \
    "${project_root}/packaging/60-gdox.rules" \
    /etc/udev/rules.d/60-gdox.rules
udevadm control --reload-rules
udevadm trigger --subsystem-match=bsg --subsystem-match=scsi_generic --subsystem-match=usb

# uaccess ACLs are normally applied by logind on device add. Also grant the
# invoking desktop user access to an already-connected supported USB drive so
# no unplug/replug cycle is required during setup.
desktop_user=${SUDO_USER:-}
if [[ -n ${desktop_user} && ${desktop_user} != root ]]; then
    for device in /sys/bus/usb/devices/*; do
        [[ -r "${device}/idVendor" && -r "${device}/idProduct" ]] || continue
        vendor=$(<"${device}/idVendor")
        product=$(<"${device}/idProduct")
        [[ ${vendor}:${product} == 0e8d:1887 || ${vendor}:${product} == 152e:2507 ]] || continue
        bus=$(<"${device}/busnum")
        number=$(<"${device}/devnum")
        node=$(printf '/dev/bus/usb/%03d/%03d' "${bus}" "${number}")
        if [[ -e ${node} ]]; then
            setfacl -m "u:${desktop_user}:rw" "${node}"
            echo "Granted ${desktop_user} access to ${node}"
        fi
    done
fi

echo "Installed GDOX optical-drive access rules."
