# Observed OMS/OFilm protocol

These are interoperability notes for USB **33a7:2388** on the tested machine.
They are incomplete and firmware-specific. No vendor source, firmware, serial
number, raw USB capture or fingerprint template is included in this repository.

## USB transport

- Interface 0; bulk OUT endpoint `0x01`, bulk IN endpoint `0x81`.
- Transfer header: ASCII `USBC`, direction (`0x00` write, `0x80` read),
  followed by a 24-bit little-endian transfer length.
- Commands are padded to 64 bytes. A transfer acknowledgment is ASCII `USBS`.
- The implementation requests a response, then drains a second IN transfer.
  Valid index/identify frames from that second read must not be discarded.
- Timing and power management matter. Commands are separated by a short delay;
  the sensor is kept out of USB runtime autosuspend.

## Command payloads

Command prefix: `EF 01 FF FF FF FF 01 00`.

| Operation | Body after prefix | Checksum |
| --- | --- | --- |
| Handshake | `03 35 00` | Sum from prefix offset 6, low 8 bits |
| Sleep | `03 33 00` | Same |
| Cancel | `03 30 00` | Same |
| Read occupancy index | `04 1F 00 00` | Same |
| Enroll | `08 31 slot_hi slot_lo 06 00 00` | Sum from offset 6, 16-bit BE |
| Identify | `08 32 05 FF FF 00 00` | Same |
| Delete one slot | `07 0C slot_hi slot_lo 00 01` | Same |

Earlier prototypes incorrectly used `03 1F 00` for cancel. Opcode `1F` with
the index command's length/parameters reads the occupancy table instead.

## Responses

```
EF 01 <four address bytes> TYPE LEN_hi LEN_lo DATA... CHECKSUM_hi CHECKSUM_lo
```

`TYPE` is `07` for the responses used here. `LEN` includes the two checksum
bytes, so the complete frame is `9 + LEN` bytes. The checksum is the 16-bit
sum of TYPE, both LEN bytes, and DATA. Bounds and checksum must be validated
before interpreting a status, slot or success result.

### Occupancy table

`LEN=0x23`: DATA contains a zero success byte followed by 32 occupancy bytes.
Bit 0 of byte 0 represents slot 0; the next bit represents slot 1, and so on.
The allocator picks the first unoccupied address. A full table is different
from a malformed or missing response. The address table does **not** establish
the device's physical template capacity.

### Enrollment

Observed `LEN=5` frames use DATA as `code, status, count`:

- Status 1/2/3: capture progress; status 3 advances the displayed stage.
- Status 6 with code 0: enrollment finished.
- Code `0x22` with status 0: slot conflict. Never adopt an unknown existing
  template as proof of a new enrollment.

### Verification / identification

Observed `LEN=8` frames have `code, status, slot_hi, slot_lo, ...`:

- Status **1** with code 0 is the observed touch event during verification.
  Requiring enrollment's status 3 caused valid results to be ignored.
- Status 5 is a result. Code 0 with the correct slot is accepted only after
  a valid touch event in that scan. Code 9 with slot `0xFFFF` was observed
  for an unregistered finger.
- Status 0 is not proof of a new touch. A result alone is not enough to
  authenticate; old results have been observed in early experiments.

The driver cancels the preceding operation before a new scan, resets its touch
gate, sends identify once, and then polls. Identify success must match a slot
in the gallery supplied by fprintd. It never treats any chip match as sufficient
to authenticate an arbitrary user.

### Delete

A successful short acknowledgment has `LEN=3`, DATA `00`, and a valid checksum.
No reply, malformed replies and nonzero status are not treated as success.
The real deletion path still needs hardware testing.

## Host record format

New `fpi-data` is the GVariant tuple `(qyay)`: a 16-bit slot, finger enum and
user identifier bytes. The old prototype `(yay)` tuple with marker `0xFF`
is recognized as slot 0. This metadata enables fprintd serialization and
per-print slot selection; it is **not a cryptographic chip-owner binding**.

Reviewing replay resistance, stale USB queues, cancellation, multiple users,
power cycles and deletion behavior remains necessary before production use.
