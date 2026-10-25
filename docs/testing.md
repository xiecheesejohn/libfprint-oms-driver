# Test status

Recorded on 2026-10-25 using the WUJIE 14 Pro test machine described in README.

| Check | Result |
| --- | --- |
| USB discovery for 33a7:2388 | Passed |
| Read occupancy table | Passed; initially only slot 0 occupied |
| KDE enrollment of a second finger | Passed; allocated slot 1 |
| Correct-finger identification | Passed; explicit chip success in user gallery |
| Unregistered-finger identification | Passed; explicit code 9 / slot 0xFFFF |
| No-touch verify and identify | Passed; no authentication |
| KDE lock-screen unlock | Confirmed by tester; matching daemon log observed |
| D-Bus activation after stopping fprintd | Passed; both records readable |
| USB power rule ordering | Checked with udevadm test; overrides autosuspend |
| Reboot persistence: USB, driver, records, real fingerprint match | Passed after tester reboot |
| Plasma Login Manager PAM authenticate + acct_mgmt | Passed with real finger after login-only override |
| Full cold power-off / updated boot-greeter flow | Pending |
| Suspend / resume | Pending |
| Real sensor deletion | Pending; protocol tests only |
| Multi-user isolation on real hardware | Pending |
| Other firmware / machines / distributions | Pending |

The tests in `tests/oms-protocol.c` cover frame bounds/checksums, no-touch result
rejection, the observed status-1 touch sequence, slot-specific verification,
bitmap allocation, legacy record parsing, and deletion packet/ack handling.
They do not emulate a complete USB device or prove authentication security.

The build runs `oms-protocol`, `fpi-device`, and `fpi-ssm`. Upstream hardware
emulation tests require additional fixtures/drivers and are not claimed here.
The optional AppStream metadata network validation is not part of these checks.

To report a result, include the USB ID, laptop model, OS, library version and
whether the test was cold boot, warm restart or suspend/resume. Sanitize logs:
remove user identifiers, serial numbers and other personal information. Never
publish fingerprint templates or raw biometric USB traffic.
