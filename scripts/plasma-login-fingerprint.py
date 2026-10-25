#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Opt-in Fedora Plasma Login Manager fingerprint authentication with password fallback."""
import argparse
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

VENDOR = Path("/usr/lib/pam.d/plasmalogin")
OVERRIDE = Path("/etc/pam.d/plasmalogin")
BACKUPS = Path("/var/lib/libfprint-oms-login-backups")
AUTH = re.compile(r"(?m)^(\s*auth\s+substack\s+)password-auth(\s*(?:#.*)?)$")


def write_override(text):
    with tempfile.NamedTemporaryFile(mode="w", dir=OVERRIDE.parent,
                                     prefix=".plasmalogin-", delete=False) as file:
        file.write(text)
        name = file.name
    try:
        os.chmod(name, 0o644)
        os.replace(name, OVERRIDE)
        subprocess.run(["restorecon", str(OVERRIDE)], check=True)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def enable():
    if OVERRIDE.is_symlink():
        raise ValueError("Refusing to replace a symlinked local PAM override.")
    if not VENDOR.is_file():
        raise ValueError("Expected Fedora Plasma Login Manager's vendor PAM service.")
    subprocess.run(["authselect", "check"], check=True)
    password = Path("/etc/pam.d/password-auth").read_text()
    system = Path("/etc/pam.d/system-auth").read_text()
    pattern = r"(?m)^auth\s+sufficient\s+pam_fprintd\.so[^\n]*\n"
    without_fingerprint, count = re.subn(pattern, "", system)
    if count != 1 or without_fingerprint != password:
        raise ValueError("system-auth must equal password-auth plus one sufficient pam_fprintd line; review other profiles manually.")
    if "pam_unix.so" not in system:
        raise ValueError("Password fallback is missing.")
    base = OVERRIDE if OVERRIDE.exists() else VENDOR
    original = base.read_text()
    if re.search(r"(?m)^\s*auth\s+substack\s+system-auth\s*$", original):
        print("Plasma Login Manager already uses the fingerprint-enabled system-auth stack.")
        return
    updated, count = AUTH.subn(r"\1system-auth\2", original)
    if count != 1:
        raise ValueError("Expected exactly one auth substack password-auth line.")
    BACKUPS.mkdir(parents=True, mode=0o700, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime("%Y%m%d-%H%M%S-"), dir=BACKUPS))
    shutil.copy2(base, backup / "plasmalogin.before")
    (backup / "manifest.json").write_text(json.dumps({"had_override": OVERRIDE.exists()}, indent=2) + "\n")
    write_override(updated)
    print("Enabled fingerprint authentication only for Plasma Login Manager.")
    print("Password fallback and all account/password/session rules are preserved.")
    print("SSH/global password-auth and the vendor PAM file were not changed.")
    print("At boot login, select your user, press Enter/click Log In, then touch the sensor.")
    print("No logout, reboot, or display-manager restart was performed.")
    print(f"Rollback: sudo python3 {Path(__file__).resolve()} --restore {backup}")


def restore(backup):
    backup = backup.resolve()
    if backup.parent != BACKUPS or not (backup / "manifest.json").is_file():
        raise ValueError("Use the backup directory printed by --enable.")
    saved = json.loads((backup / "manifest.json").read_text())
    if saved["had_override"]:
        write_override((backup / "plasmalogin.before").read_text())
    elif OVERRIDE.is_file() and not OVERRIDE.is_symlink():
        OVERRIDE.unlink()
    print("Restored the previous Plasma Login Manager PAM configuration.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--enable", action="store_true")
    action.add_argument("--restore", type=Path)
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("Run with sudo.")
    try:
        enable() if args.enable else restore(args.restore)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")
