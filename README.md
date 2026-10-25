# libfprint OMS/OFilm driver

[中文安装与使用说明](README.zh-CN.md)

Optional: [independent animated login / lock-screen wallpaper plugin](ui/README.md).

Optional: [terminal fingerprint feedback for sudo and other commands](terminal/README.md).

Experimental libfprint driver for the **OMS/OFilm USB fingerprint sensor
`33a7:2388`**. It supports enrolling multiple fingerprints into separate sensor
slots and using them through fprintd and KDE Plasma's fingerprint settings.

This is an independent project, not an official vendor driver or upstream release.
It is authentication software under development; a successful test on one machine
does not establish production security or compatibility with all firmware.

## Supported hardware

| Item | Tested / supported |
| --- | --- |
| Sensor | OMS/OFilm match-on-chip, USB **`33a7:2388`** |
| Laptop | MECHREVO WUJIE 14 Pro / 机械革命无界 14 Pro |
| CPU | Ryzen 7 8845S (tester model label); `lscpu`: AMD Ryzen 7 8845HS |
| OS | Fedora Linux 44 KDE, x86_64 |
| Desktop | KDE Plasma 6.7.5 |
| Base library | libfprint **1.94.100** |

**Check the USB ID, not just the laptop or CPU model.** Different configurations
of the same laptop may use different fingerprint sensors:

```bash
lsusb -d 33a7:2388
```

If nothing is listed, this driver is not established to support your sensor.
Other OMS/OFilm IDs, Goodix, ELAN, FPC, Realtek and PixelAuth PXAT devices are
not supported by this driver. The included build enables only the OMS driver;
it replaces a system library and disables other drivers in that library.

## Tested behavior and limits

- Two fingerprints enrolled into slots 0 and 1 through KDE.
- A registered finger returns `verify-match`; an unregistered finger returns
  an explicit chip no-match; no touch times out without authenticating.
- KDE lock-screen unlocking confirmed on the test machine.
- Verification matches the requested print; identification only accepts slots
  present in the user gallery supplied by fprintd.
- **Not yet validated:** full cold-boot behavior, suspend/resume, deletion on
  real hardware, different firmware, and multiple user accounts.
- The index protocol contains 256 occupancy bits. **Physical template capacity
  is unknown**; this does not promise support for 256 fingerprints.
- Older prototype records are interpreted as slot 0 for compatibility. Those
  records did not cryptographically bind an owner to the chip template.
- Runtime USB autosuspend can stall the sensor; installation adds a rule to
  keep this specific USB device powered.

## Install on Fedora x86_64

The installer requires the Fedora `libfprint` package version **1.94.100**.
Other distributions and library versions require manual adaptation and testing.

1. Install dependencies:

   ```bash
   sudo dnf install git curl patch gcc gcc-c++ meson ninja-build pkgconf-pkg-config \
     glib2-devel libgusb-devel gobject-introspection-devel systemd-devel \
     libfprint fprintd fprintd-pam usbutils
   ```

2. Clone and build as your regular desktop user:

   ```bash
   git clone https://github.com/xiecheesejohn/libfprint-oms-driver.git
   cd libfprint-oms-driver
   bash scripts/build.sh
   ```

   The script downloads upstream libfprint v1.94.100, verifies its SHA-256,
   applies the integration patch, builds the OMS driver and runs protocol
   and libfprint unit tests. Vendor binaries and firmware are not needed.

3. Close fingerprint enrollment/verification windows, then install:

   ```bash
   sudo bash scripts/install-fedora.sh --install
   ```

   The script backs up the current library and existing sensor power rule,
   installs the library and rule, and restarts fprintd. **Save the backup path.**
   It does not change PAM, delete prints, or restart the desktop.

## Enroll and use

Run the following as your **regular desktop user**, not root:

```bash
fprintd-list "$USER"
kcmshell6 kcm_users
```

In **System Settings → Users → your user → Fingerprint**, add a finger and
follow the touch/lift prompts. Add another finger separately. The driver reads
the chip occupancy table and chooses an unused slot instead of overwriting slot 0.

Command-line enrollment is also available:

```bash
fprintd-enroll -f right-index-finger "$USER"
fprintd-enroll -f right-middle-finger "$USER"
fprintd-verify -f any "$USER"
```

Verify a registered finger, an unregistered finger, and no touch before relying
on it. The last two cases must not authenticate. Test the KDE lock screen with
**Meta + L**, keeping your password available.

If Fedora fingerprint authentication is not enabled in your authselect profile:

```bash
authselect current
sudo authselect enable-feature with-fingerprint
authselect check
```

This requires a profile that supports the feature. KDE lock-screen support and
first login after boot are separate PAM integrations.

### Enable fingerprint at boot login (optional)

Fedora's Plasma Login Manager uses `password-auth`, which can exclude fingerprint
verification even when KDE lock-screen unlocking works. On the tested Fedora
configuration, enable a local login-only override:

```bash
sudo python3 scripts/plasma-login-fingerprint.py --enable
```

The script checks that `system-auth` differs from `password-auth` only by the
existing sufficient `pam_fprintd` line, backs up the current service, then changes
only Plasma Login Manager's **auth** substack to `system-auth`. Password fallback
and account/password/session rules remain intact. SSH, global `password-auth`,
PAM vendor files and authentication settings for other services are not changed.
Other authselect profiles that do not pass this comparison require manual review.
No logout, reboot or display-manager restart is performed.

At boot login: **select your user, press Enter/click Log In, then touch a registered
finger**. A fingerprint scan does not start merely because the greeter is visible.
If using password login, the fingerprint attempt can precede password fallback.
Fingerprint login does not supply a password to unlock password-encrypted wallets.

The `plasmalogin` PAM service was tested with a real finger and passed both
`authenticate` and `acct_mgmt` after this change. The complete next-boot greeter
flow still needs confirmation. To revert, use the backup path printed by the script:

```bash
sudo python3 scripts/plasma-login-fingerprint.py --restore /var/lib/libfprint-oms-login-backups/ACTUAL_DIRECTORY
```

The local override persists across reboot. Review it when the distribution changes
its vendor PAM service in future updates.

## Reboot and system updates

The installed library, udev rule and fprintd records persist across reboot.
fprintd is D-Bus activated; a `static` or idle/inactive service is normal and
does not require `systemctl enable fprintd`.

After a reboot, USB detection, persisted prints and a real fingerprint match were
confirmed. Full power-off/boot and the updated greeter flow remain pending. Check the USB ID, list
your prints and run `fprintd-verify -f any "$USER"`.

**A libfprint package update or reinstall can overwrite the custom library.**
This project is not a managed Fedora RPM. Do not reinstall this older library
over a newer package version; adapt/rebuild against that version first.

## Roll back

Use the exact backup directory printed during installation:

```bash
sudo bash scripts/restore-fedora.sh /var/lib/libfprint-oms-backup.XXXXXXXX
```

This restores the previous library and power rule without deleting fingerprints.
It does not reconstruct a template deleted from the chip. The deletion interface
is implemented but has **not been validated on hardware**; avoid deleting records
as a workaround for other errors.

## Troubleshooting

```bash
fprintd-list "$USER"
systemctl status fprintd --no-pager
sudo journalctl -u fprintd -b -n 100 --no-pager
```

- **Not Authorized:** open System Settings from your normal desktop session.
  Changing only the process UID from a root terminal can leave it in the wrong
  login session. Do not broadly disable Polkit checks.
- **Already claimed:** close other enrollment/verification apps. If no operation
  is running, restart the daemon with `sudo systemctl restart fprintd`.
- **No space:** this driver selects a free occupancy bit. A genuinely full table,
  unsupported address or sensor rejection needs investigation; do not silently
  adopt or delete an existing template.
- **No match:** specify `-f any` when testing multiple enrolled fingers. If the
  problem persists, include sanitized logs and the USB ID in an issue.

Do not attach raw fingerprint data, full USB captures, account information or
device serial numbers to public issues. Logs may reveal enrollment metadata.

## Source and licensing

See [protocol notes](docs/protocol.md) and [test status](docs/testing.md).
The driver, tests and scripts use **LGPL-2.1-or-later**; see [LICENSE](LICENSE)
and [NOTICE](NOTICE). Upstream libfprint notices must be preserved when sharing
prepared source or binaries. Binary distributions must include the corresponding
source and build materials as required by the license.
