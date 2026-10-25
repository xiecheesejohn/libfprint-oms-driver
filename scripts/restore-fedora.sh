#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
set -euo pipefail
if [[ $EUID != 0 || $# != 1 ]]; then
    echo "Usage: sudo bash scripts/restore-fedora.sh /var/lib/libfprint-oms-backup.XXXXXXXX" >&2
    exit 1
fi
backup_dir=$(realpath -- "$1")
[[ $backup_dir == /var/lib/libfprint-oms-backup.* ]]
library="$backup_dir/libfprint-2.so.2.0.0"
target=/usr/lib64/libfprint-2.so.2.0.0
rule=/etc/udev/rules.d/85-oms-fp-no-autosuspend.rules
[[ -f "$library" ]]
[[ -f "$backup_dir/85-oms-fp-no-autosuspend.rules" || -f "$backup_dir/no-previous-rule" ]]
systemctl stop fprintd.service
install -m 0755 -- "$library" "$target.oms-restore"
mv -fT -- "$target.oms-restore" "$target"
if [[ -f "$backup_dir/85-oms-fp-no-autosuspend.rules" ]]; then
    install -m 0644 -- "$backup_dir/85-oms-fp-no-autosuspend.rules" "$rule"
    restorecon "$rule"
else
    rm -f -- "$rule"
fi
restorecon "$target"
ldconfig
udevadm control --reload-rules
systemctl start fprintd.service
echo "Restored the pre-install library and udev rule. Fingerprints were not deleted."
