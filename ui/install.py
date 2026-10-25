#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Install or restore the independent Aurora Fingerprint wallpaper plugin."""
import argparse
import datetime
import json
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import sys
import tempfile

PLUGIN_ID = "io.github.xiecheesejohn.aurorafingerprint"
DEST = Path("/usr/local/share/plasma/wallpapers") / PLUGIN_ID
LOGIN_CONFIG = Path("/etc/plasmalogin.conf")
BACKUPS = Path("/var/lib/oms-aurora-ui-backups")
ROOT = Path(__file__).resolve().parent

def run(args, user=None, capture=False):
    if user:
        args = ["runuser", "-u", user, "--", *args]
    return subprocess.run(args, check=True, text=True, capture_output=capture)

def read_plugin(config):
    return run(["kreadconfig6", "--file", str(config), "--group", "Greeter",
                "--key", "WallpaperPlugin", "--default", "__unset__"], capture=True).stdout.strip()

def set_plugin(config, value, user=None):
    args = ["kwriteconfig6", "--file", str(config), "--group", "Greeter", "--key", "WallpaperPlugin"]
    args += ["--delete"] if value == "__unset__" else [value]
    run(args, user=user)

parser = argparse.ArgumentParser(description=__doc__)
choice = parser.add_mutually_exclusive_group(required=True)
choice.add_argument("--install", metavar="DESKTOP_USER")
choice.add_argument("--restore", type=Path, metavar="BACKUP_DIRECTORY")
args = parser.parse_args()
if os.geteuid() != 0:
    parser.error("Run with sudo; specify the normal desktop user for --install.")

if args.install:
    account = pwd.getpwnam(args.install)
    if account.pw_uid == 0:
        parser.error("Choose the normal desktop user, not root.")
    if not shutil.which("plasma-login-wallpaper"):
        parser.error("This integration requires Plasma Login Manager, not SDDM.")
    source = ROOT / "aurora-fingerprint"
    run(["kpackagetool6", "--type", "Plasma/Wallpaper", "--show", str(source)])
    lock_config = Path(account.pw_dir) / ".config/kscreenlockerrc"
    old = {"user": account.pw_name, "lock_config": str(lock_config),
           "login_plugin": read_plugin(LOGIN_CONFIG), "lock_plugin": read_plugin(lock_config),
           "had_plugin": DEST.exists()}
    BACKUPS.mkdir(mode=0o700, parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime("%Y%m%d-%H%M%S-"), dir=BACKUPS))
    os.chmod(backup, 0o700)
    for config, name in [(LOGIN_CONFIG, "plasmalogin.conf"), (lock_config, "kscreenlockerrc")]:
        if config.exists():
            shutil.copy2(config, backup / name)
    if DEST.exists():
        shutil.copytree(DEST, backup / "plugin")
    (backup / "manifest.json").write_text(json.dumps(old, indent=2) + "\n")
    DEST.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, DEST, dirs_exist_ok=True)
    # Do not replace any KDE package file, binary, PAM stack, or service unit.
    set_plugin(LOGIN_CONFIG, PLUGIN_ID)
    set_plugin(lock_config, PLUGIN_ID, user=account.pw_name)
    run(["restorecon", "-R", str(DEST), str(LOGIN_CONFIG), str(lock_config)])
    assert read_plugin(LOGIN_CONFIG) == PLUGIN_ID
    assert read_plugin(lock_config) == PLUGIN_ID
    print(f"Installed independent wallpaper: {PLUGIN_ID}")
    print("Applied to the next lock screen and the next Plasma Login Manager startup.")
    print("No logout, reboot or display-manager restart was performed.")
    print(f"Rollback: sudo python3 {ROOT / 'install.py'} --restore {backup}")
else:
    backup = args.restore.resolve()
    if backup.parent != BACKUPS or not (backup / "manifest.json").is_file():
        parser.error("Use a backup directory produced by this installer.")
    old = json.loads((backup / "manifest.json").read_text())
    account = pwd.getpwnam(old["user"])
    lock_config = Path(account.pw_dir) / ".config/kscreenlockerrc"
    # Restore only our setting; preserve unrelated configuration changes.
    set_plugin(LOGIN_CONFIG, old["login_plugin"])
    set_plugin(lock_config, old["lock_plugin"], user=account.pw_name)
    if old["had_plugin"]:
        shutil.copytree(backup / "plugin", DEST, dirs_exist_ok=True)
    elif DEST.is_dir():
        shutil.rmtree(DEST)
    print("Restored previous wallpaper selections. Authentication settings were not changed.")
