#!/usr/bin/python3
# SPDX-License-Identifier: LGPL-2.1-or-later
import argparse
import datetime
import json
import os
from pathlib import Path
import pwd
import shlex
import shutil
import tempfile

ROOT = Path(__file__).resolve().parent
BACKUPS = Path("/var/lib/oms-fingerprint-tui-backups")
parser = argparse.ArgumentParser(description="Install the optional Bash sudo fingerprint status wrapper")
action = parser.add_mutually_exclusive_group(required=True)
action.add_argument("--install", metavar="DESKTOP_USER")
action.add_argument("--restore", type=Path)
args = parser.parse_args()
if os.geteuid() != 0:
    parser.error("Run with sudo, selecting the normal desktop user.")

if args.install:
    user = pwd.getpwnam(args.install)
    if user.pw_uid == 0:
        parser.error("Select the desktop user, not root.")
    home = Path(user.pw_dir)
    if ".bashrc.d" not in (home / ".bashrc").read_text():
        parser.error("This installer expects Fedora's ~/.bashrc.d loading convention.")
    helper = home / ".local/bin/fingerprint-tui"
    hook = home / ".bashrc.d/90-fingerprint-tui.sh"
    BACKUPS.mkdir(parents=True, mode=0o700, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime("%Y%m%d-%H%M%S-"), dir=BACKUPS))
    previous = {"user": user.pw_name, "files": {}}
    for name, path in [("helper", helper), ("hook", hook)]:
        previous["files"][name] = str(path)
        if path.exists():
            shutil.copy2(path, backup / name)
    (backup / "manifest.json").write_text(json.dumps(previous, indent=2) + "\n")
    for path in [helper.parent, hook.parent]:
        path.mkdir(parents=True, exist_ok=True)
        os.chown(path, user.pw_uid, user.pw_gid)
    shutil.copyfile(ROOT / "fingerprint-tui.py", helper)
    helper.chmod(0o755)
    hook.write_text('''# Optional read-only fingerprint status for interactive Bash sudo.
sudo() {
    if [[ $- == *i* && -t 0 && -t 2 && ${TERM:-dumb} != dumb && ${FPRINT_TUI:-1} != 0 ]]; then
        local _fp_arg
        for _fp_arg in "$@"; do
            case "$_fp_arg" in
                --stdin|--non-interactive|--askpass|-S|-n|-A|-[^-]*S*|-[^-]*n*|-[^-]*A*)
                    command sudo "$@"; return $? ;;
            esac
        done
        FPRINT_TUI_LANG="${FPRINT_TUI_LANG:-zh}" ''' + shlex.quote(str(helper)) + ''' -- "$(type -P sudo)" "$@"
    else
        command sudo "$@"
    fi
}
''')
    hook.chmod(0o644)
    for path in [helper, hook]:
        os.chown(path, user.pw_uid, user.pw_gid)
    print(f"Installed: {helper}")
    print(f"Open a new Bash terminal, or run: source {hook}")
    print(f"Rollback: sudo python3 {ROOT / 'install.py'} --restore {backup}")
else:
    backup = args.restore.resolve()
    if backup.parent != BACKUPS or not (backup / "manifest.json").is_file():
        parser.error("Use the backup directory printed by this installer.")
    saved = json.loads((backup / "manifest.json").read_text())
    user = pwd.getpwnam(saved["user"])
    home = Path(user.pw_dir)
    expected = {"helper": home / ".local/bin/fingerprint-tui", "hook": home / ".bashrc.d/90-fingerprint-tui.sh"}
    for name, path in expected.items():
        if str(path) != saved["files"][name]:
            parser.error("Backup paths do not match the desktop user's home.")
        if (backup / name).is_file():
            shutil.copy2(backup / name, path)
            os.chown(path, user.pw_uid, user.pw_gid)
        elif path.exists():
            path.unlink()
    print("Restored previous files. Open a new terminal or run: unset -f sudo")
