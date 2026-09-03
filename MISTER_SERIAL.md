# MiSTer Serial

`mister_serial` is an optional, write-only stream of structured MiSTer state.
It is independent of `debug`, `log_file_entry`, and standard output.

## Configuration

Select the USB serial device by vendor and product ID in `MiSTer.ini`:

```ini
mister_serial=16d0_14f7
```

If identical devices are attached, append the USB serial number:

```ini
mister_serial=16d0_14f7_NOVA1234
```

An absolute `/dev/...` path remains available as an explicit fallback. USB ID
selection scans sysfs without opening unrelated ports, requires exactly one
match, and resolves the current TTY again after a disconnect. The port uses
115200 baud, 8 data bits, no parity, and one stop bit. An empty value disables
the feature without opening a device.

## Record Format

Each record is one LF-terminated, tab-delimited line:

```text
MISTER_STATUS/1<TAB>sequence<TAB>event<TAB>key=value...<TAB>time_us=...<TAB>snapshot=0|1
```

Control characters in values are replaced with spaces. Receivers should ignore
unknown events and keys so the protocol can grow without breaking older clients.

Current events:

- `HELLO`: Main version and supported event capabilities.

- `CORE`: active core name.
- `GAME`: primary game name without its final extension plus its retained
  identifiers. `state=loading` is emitted before transfer with empty `crc32`
  and `serial` fields; `state=loaded` atomically commits the title and any
  available identifiers, while `state=empty` clears all three fields.
- `PREVIEW`: highlighted core/game after a 75 ms dwell, or clear. This remains
  above Main's 50 ms held-direction repeat rate, so continuous scrolling is
  coalesced into one preview after the cursor stops.
- `OSD`: OSD visibility.
- `LOAD`: transient loading state and percentage. The active `CORE` and `GAME`
  records remain authoritative; consumers should compare them to retained state
  before applying a loaded theme or jingle.
- `STATE`: `state=active|idle`; idle follows Main's global `hdmi_off`
  input-idle state.
- `VIDEO`: source and HDMI output dimensions, refresh rates in millihertz,
  interlace state, and direct/scaled mode.
- `CONTROLLER`: present, connected, disconnected, or assigned player.
- `NET`: retained network-interface state. `type=lan` and `type=wlan` records
  independently carry presence, interface name, IPv4 address, and MAC address.
  They are sampled every two seconds and emitted only when their values change.
  MAC addresses provide stable interface identity when DHCP addresses change
  and are useful for reservations and adapter diagnostics. Simple consumers
  may ignore `interface` and `mac`; SSIDs and credentials are never included.

Examples:

```text
MISTER_STATUS/1	1	HELLO	main=260826	caps=core,game,load,state,video,osd,preview,controller,net	time_us=123456	snapshot=1
MISTER_STATUS/1	2	NET	type=lan	present=1	interface=eth0	ip=192.168.1.78	mac=02:00:00:00:00:01
MISTER_STATUS/1	3	NET	type=wlan	present=0	interface=	ip=	mac=
MISTER_STATUS/1	4	CORE	name=SNES
MISTER_STATUS/1	5	GAME	state=loading	name=Super Mario World	crc32=	serial=
MISTER_STATUS/1	6	LOAD	state=progress	percent=50
MISTER_STATUS/1	7	GAME	state=loaded	name=Super Mario World	crc32=42CF9B5B	serial=SNS-MW-USA
MISTER_STATUS/1	8	STATE	state=active
MISTER_STATUS/1	9	VIDEO	source_width=256	source_height=224	source_refresh_millihz=60098	interlaced=0	output_width=1920	output_height=1080	output_refresh_millihz=59940	mode=scaled
```

Every event has a monotonic source timestamp. `snapshot=1` identifies replayed
state after connection, not a fresh user action. `PREVIEW` is reversible state
for displays and lighting; consumers must not treat it as a core/game load.
Initial controllers use `present`, while later lifecycle changes use
`connected` and `disconnected`.

## Reliability

Writes are nonblocking and event-driven. A missing or slow receiver cannot
delay Main. The sender coalesces superseded state, retries a missing endpoint
once per second, detects disconnection, and sends `HELLO` plus a complete
current-state snapshot after reconnection. Consumers should treat a valid
`HELLO` plus the open CDC session as connected; no periodic heartbeat is sent.

## Existing project integration

`mister_serial` is intended first as a common state source for existing MiSTer
accessories that currently infer state from `/tmp`, logs, polling, or helper
scripts. Each project needs a small protocol adapter; this is not drop-in
compatibility with its existing device firmware.

| Existing project | Useful records | Suggested integration |
| --- | --- | --- |
| Reflex Nova | All records | Current reference consumer: Main writes directly to the system RP2040 CDC endpoint |
| [MiSTer_tty2oled](https://github.com/venice1200/MiSTer_tty2oled) | `CORE`, `GAME`, `PREVIEW`, `LOAD`, `STATE` | Teach its Arduino/ESP firmware to parse `MISTER_STATUS/1`, allowing Main to drive the display without the resident core-detection script |
| [MiSTer_tty2tft](https://github.com/ojaksch/MiSTer_tty2tft) | `CORE`, `GAME`, `PREVIEW`, `LOAD`, `STATE` | Resolve images, videos, and sounds from committed core/game names while using preview and loading only as temporary display states |
| [MiSTer_i2c2oled](https://github.com/venice1200/MiSTer_i2c2oled) | `CORE`, `GAME`, `LOAD`, `STATE` | Not a direct serial target; retain a small HPS broker or add a serial-to-I2C adapter that translates status records into its display commands |
| [Zaparoo](https://github.com/ZaparooProject/zaparoo-core) | Read-only `CORE`, `GAME`, `LOAD` | Can observe authoritative active state, including game identifiers, but launching media still requires the separate future command protocol described below |

For tty2oled and tty2tft, configure Main with the display controller's stable
`/dev/serial/by-id/...` path. Their microcontroller firmware would buffer one
line, validate `MISTER_STATUS/1`, and map records to the project's existing
media-name lookup. This removes the always-running MiSTer polling script, but
does not require replacing the project's artwork database or display renderer.

`CORE` and `GAME` are the authoritative active state. `PREVIEW` should be shown
temporarily without overwriting the active title. `LOAD` is presentation state;
a terminal `GAME state=loaded` or `LOAD state=done` returns the display to active
content. A minimal display can ignore the optional `GAME` identifiers, `VIDEO`,
network, and controller records.

## Theoretical accessories

After existing projects have adapters, the same protocol can support new
devices without adding project-specific output code to Main:

| Accessory class | Useful records | Example behavior |
| --- | --- | --- |
| LED or ambient-light controllers | `CORE`, `GAME`, `LOAD`, `STATE` | Select themes and show loading or idle states |
| Stream overlays and capture boxes | `CORE`, `GAME`, `LOAD`, `CONTROLLER` | Update now-playing and player overlays |
| VFD, e-paper, or secondary video displays | Active title records plus optional `VIDEO` and `NET` | Show persistent system and media information |
| Accessibility and telemetry bridges | Any required retained state | Translate changes into tactile/audio feedback or session records |

Consumers should:

- Use a stable `/dev/serial/by-id/...` path where possible.
- Buffer until LF, reject an overlong or malformed line, and resume at the next LF.
- Ignore unknown events and keys.
- Treat sequence values as ordering aids, not persistent identifiers.
- Rebuild retained state from `snapshot=1` records after each `HELLO`.
- Avoid triggering user-visible actions such as jingles from snapshot records.
- Keep `PREVIEW` separate from committed `CORE` and `GAME` state.
- Apply a local timeout only to transient presentation state, never to the CDC
  session itself; a quiet stream is healthy because records are change-driven.

Main opens one configured endpoint. Two processes must not independently open
the same TTY. Systems needing several consumers should use a small broker that
owns the port and republishes parsed records, or an external microcontroller
that fans state out to attached devices.

## Read-only boundary

The current protocol is intentionally Main-to-device only. Projects such as
Zaparoo need inbound launch/control requests, validation, explicit result
records, and failure handling. That belongs in a separate future command
protocol with request identifiers; it should not be added as ad hoc writable
status records. A bidirectional implementation may share the physical serial
device later while keeping status and command framing logically separate.

Save dirty-state reporting is intentionally out of scope. Main writes save
sectors when a core submits them, but generic dirty detection requires an
explicit core-to-Main dirty flag, counter, or snapshot protocol.
