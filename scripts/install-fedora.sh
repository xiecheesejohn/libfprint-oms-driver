#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if [[ ${1:-} != --install || $EUID != 0 ]]; then
    echo "Usage: sudo bash scripts/install-fedora.sh --install" >&2
    exit 1
fi
source /etc/os-release
if [[ $ID != fedora || $(uname -m) != x86_64 ]]; then
    echo "This installer supports Fedora x86_64 only." >&2
    exit 1
fi
if [[ $(rpm -q --qf '%{VERSION}' libfprint) != 1.94.100 ]]; then
    echo "Expected packaged libfprint 1.94.100; refusing a different version." >&2
    exit 1
fi
library="$repo_dir/_work/build/libfprint/libfprint-2.so.2.0.0"
target=/usr/lib64/libfprint-2.so.2.0.0
rule=/etc/udev/rules.d/85-oms-fp-no-autosuspend.rules
[[ -f "$library" && -f "$target" && -d /dev/bus/usb ]]
"$repo_dir/_work/build/tests/test-oms-protocol"
backup_dir=$(mktemp -d /var/lib/libfprint-oms-backup.XXXXXXXX)
chmod 0700 "$backup_dir"
cp -a -- "$target" "$backup_dir/libfprint-2.so.2.0.0"
if [[ -e "$rule" ]]; then
    cp -a -- "$rule" "$backup_dir/85-oms-fp-no-autosuspend.rules"
else
    touch "$backup_dir/no-previous-rule"
fi
systemctl stop fprintd.service
install -m 0755 -- "$library" "$target.oms-new"
mv -fT -- "$target.oms-new" "$target"
install -m 0644 -- "$repo_dir/udev/85-oms-fp-no-autosuspend.rules" "$rule"
restorecon "$target" "$rule"
ldconfig
udevadm control --reload-rules
udevadm trigger --action=change --subsystem-match=usb --attr-match=idVendor=33a7 --attr-match=idProduct=2388
udevadm settle
systemctl start fprintd.service
echo "Installed. Backup: $backup_dir"
echo "Rollback: sudo bash scripts/restore-fedora.sh '$backup_dir'"
echo "PAM settings and fingerprint records were not changed."
