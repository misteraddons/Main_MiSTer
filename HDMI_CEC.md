# HDMI CEC Behavior

This document describes current HDMI CEC behavior implemented in `hdmi_cec.cpp`.

## Config

- Enable with `hdmi_cec=1` in `MiSTer.ini`.

## Device Identity

- Logical device type: Playback.
- OSD name: `MiSTer`.
- Vendor ID payload: `0x000000`.
- Physical address: read from EDID CEA extension (with loose fallback parser).

## MiSTer -> TV/Broadcast (TX)

Sent during CEC init:

| Opcode | Name | Destination | Purpose |
| --- | --- | --- | --- |
| `0x84` | REPORT_PHYSICAL_ADDRESS | Broadcast (`0xF`) | Advertise physical address and playback type. |
| `0x87` | DEVICE_VENDOR_ID | Broadcast (`0xF`) | Advertise vendor id payload. |
| `0x47` | SET_OSD_NAME | TV (`0x0`) | Publish OSD name (`MiSTer`). |
| `0x04` | IMAGE_VIEW_ON | TV (`0x0`) | Wake/select display path. |
| `0x0D` | TEXT_VIEW_ON | TV (`0x0`) | Wake/select display path. |
| `0x82` | ACTIVE_SOURCE | Broadcast (`0xF`) | Announce current active source path. |

Periodic TX:

| Opcode | Name | Interval | Notes |
| --- | --- | --- | --- |
| `0x84` | REPORT_PHYSICAL_ADDRESS | 60s | Keeps source presence visible on the CEC bus. |

Boot follow-up:

- One delayed retry sends `IMAGE_VIEW_ON`, `TEXT_VIEW_ON`, and `ACTIVE_SOURCE`.

## TV/Bus -> MiSTer (RX handling)

| Incoming opcode | MiSTer behavior |
| --- | --- |
| `0x83` GIVE_PHYSICAL_ADDRESS | Replies with `REPORT_PHYSICAL_ADDRESS`. |
| `0x46` GIVE_OSD_NAME | Replies with `SET_OSD_NAME`. |
| `0x8C` GIVE_DEVICE_VENDOR_ID | Replies with `DEVICE_VENDOR_ID`. |
| `0x9F` GET_CEC_VERSION | Replies with `CEC_VERSION` (`1.4`). |
| `0x8F` GIVE_DEVICE_POWER_STATUS | Replies `REPORT_POWER_STATUS` = ON. |
| `0x85` REQUEST_ACTIVE_SOURCE | Replies with `ACTIVE_SOURCE`. |
| `0x86` SET_STREAM_PATH | If path matches MiSTer physical address, replies with `ACTIVE_SOURCE`. |
| `0x8D` MENU_REQUEST | Replies `MENU_STATUS` active. |
| `0x44` USER_CONTROL_PRESSED | Translates supported remote buttons into MiSTer keys. |
| `0x45` USER_CONTROL_RELEASED | Releases pressed MiSTer key. |

## Remote Button Mapping

Mapped:

- Directional keys -> `KEY_UP`, `KEY_DOWN`, `KEY_LEFT`, `KEY_RIGHT`
- Select/OK -> `KEY_ENTER`
- Exit/Back -> `KEY_ESC`
- Play/Pause/Stop/Rewind/FastForward -> `KEY_SPACE`, `KEY_S`, `KEY_R`, `KEY_F`
- Numeric `0-9` -> `KEY_0..KEY_9`
- **Red key only (`0x72`) -> `KEY_F12` (MiSTer OSD trigger)**

Not mapped:

- Blue/Green/Yellow CEC color keys.
- CEC volume/mute keys (TV/AVR keeps volume control).

## TX Result Logging Semantics

- `OK`: transmit completed.
- `NACK`: explicit nack/arbitration failure.
- `UNCERTAIN`: timeout without explicit nack/arbitration; transmission may still have been seen by TV.

Only repeated explicit `NACK` events trigger temporary TX suppression.

## Standby

- `cec_send_standby()` exists but is not wired to a runtime shutdown path.
- If used, it sends CEC `STANDBY` (`0x36`) broadcast, which is intended for TV/CEC devices (not MiSTer itself).
