# Fingerprint feedback in the terminal

[中文说明](README.zh-CN.md)

Optional inline status feedback for commands such as `sudo -i`. This is a
user-space command wrapper, not a replacement PAM module, sudo binary or
authentication backend.

```text
Place your finger on the fingerprint reader

╭─ Fingerprint reader ────────────────────
│ ○ Touch the fingerprint sensor · ·
│ ✓ Fingerprint matched
╰─────────────────────────────────
```

It listens read-only to fprintd signals while the wrapped command is starting.
Waiting appends progress dots; supported finger-present events show reading;
match and no-match show a result. Native PAM prompts remain visible.
It does not read stdin, proxy terminal input, create a PTY, change terminal
settings, erase lines, or replace password handling. The child command inherits
the original terminal and streams. Output goes only to stderr, and animation
is suppressed when terminal echo is disabled for password entry.

## Install on Fedora with Bash

```bash
sudo dnf install python3-gobject
sudo python3 terminal/install.py --install "$USER"
```

Run as your normal desktop user with sudo, not from a root login using `$USER`.
The installer uses Fedora's existing `~/.bashrc.d` convention. It installs
`~/.local/bin/fingerprint-tui` and `~/.bashrc.d/90-fingerprint-tui.sh`, backing up
any existing files. It does not change PAM, sudoers, the sudo binary or root's
shell configuration.

Open a new Bash terminal, or load the function in your current normal-user shell:

```bash
source ~/.bashrc.d/90-fingerprint-tui.sh
sudo -i
```

Cached sudo credentials may mean no fingerprint prompt; in that case no panel is
shown. The helper does not invalidate sudo credentials to force an animation.

## Demo and other commands

```bash
FPRINT_TUI_LANG=zh fingerprint-tui --demo
fingerprint-tui -- sudo -i
fingerprint-tui -- su -
```

Demo states are explicitly examples and do not authenticate. Other commands only
produce feedback if their existing authentication configuration uses fprintd.
The Bash sudo hook defaults to Chinese; set `FPRINT_TUI_LANG=en` to prefer English.

Pipes/non-TTY calls and `sudo -S`, `-n`, or `-A` bypass the panel. Parameters and
exit codes are preserved. Disable the Bash hook temporarily with `FPRINT_TUI=0`,
or bypass the shell function using `command sudo ...`.

## Limits and rollback

- This decorates commands launched through the wrapper; it cannot replace every
  system PAM prompt globally.
- The display is reader feedback, not proof that the wrapped command was
  authorized. fprintd broadcasts do not identify the requesting client; another
  concurrent authentication could produce reader events. Monitoring stops after
  a fingerprint match or 45 seconds, while the command continues normally.
- Drivers that do not expose finger-present remain in waiting until the result.
- Colors honor `NO_COLOR`. No full-screen terminal control is used.

Use the exact backup directory printed during installation:

```bash
sudo python3 terminal/install.py --restore /var/lib/oms-fingerprint-tui-backups/ACTUAL_DIRECTORY
unset -f sudo
```

Restoring affects only the two installed user files. Open a new terminal to load
any previous configuration.

Tests require a private session bus; no real sudo or authentication is invoked:

```bash
dbus-run-session -- python3 terminal/tests/test-terminal.py
```

Tests cover native stdin/stdout pass-through, child exit and signal status,
missing-command status, and retry/match feedback from a private fake D-Bus name.
The sources use LGPL-2.1-or-later; see the repository LICENSE.
