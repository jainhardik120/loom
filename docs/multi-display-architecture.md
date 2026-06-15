# Loom Multi-Display Architecture

This note describes the target desktop architecture for multiple tablet displays.

## EVDI Device Model

The current EVDI module exposes one DRM connector per EVDI DRM device. In EVDI
1.14, `evdi_connector_init()` stores a single connector in `evdi->conn`, and
`evdi_add_device()` creates another `/dev/dri/cardN`.

That means Loom cannot expose many virtual monitors under one Linux DRM card
without modifying the EVDI kernel module. The practical user-facing model is:

```text
Loom virtual adapter
  -> EVDI card N for display A
  -> EVDI card M for display B
  -> EVDI card K for display C
```

The UI can present this as one Loom adapter with multiple displays, while the
daemon owns one EVDI card per active virtual display internally.

## Goals

- `loomd` starts with zero active virtual displays by default.
- Users configure display profiles independently.
- Each display remembers:
  - stable id/name
  - mode and refresh rate
  - stream transport
  - bitrate/FPS
  - paired device identity
  - whether it should auto-connect
- A physical tablet disconnect immediately disconnects the matching virtual
  display.
- A paired tablet reconnect automatically recreates/resumes the virtual display.
- Users can pause/resume/remove displays without deleting their saved profile.
- `loomctl`, `loom-tray`, and future Android pairing all use the same D-Bus
  control model.

## Concepts

### Virtual Adapter

Logical Loom container for all displays. It is not necessarily one Linux DRM
card. It owns configured display profiles and active display sessions.

### Display Profile

Persistent config for one tablet/display.

Example future config shape:

```text
display.0.id=lenovo-tab
display.0.name=Lenovo Tab
display.0.enabled=true
display.0.auto_connect=true
display.0.mode_width=1920
display.0.mode_height=1200
display.0.mode_refresh=30
display.0.transport=usb_accessory
display.0.usb_serial=HA24LBMH
display.0.stream_bitrate_kbps=12000
display.0.stream_fps=30
```

### Display Session

Runtime state for one active display profile.

```text
configured
  -> waiting_for_device
  -> connecting_evdi
  -> connected
  -> streaming
  -> paused
  -> disconnected
  -> removed
```

Each session owns:

- one `EvdiDevice`
- one frame capture buffer set
- one encoder
- one transport instance
- current physical-device presence state

### Device Monitor

Tracks physical transport availability:

- USB accessory devices by serial/model/manufacturer
- later TCP/ADB devices by pairing id
- later wireless devices by pairing id

When a paired device disappears, the owning display session disconnects EVDI and
stops streaming. When it reappears, the session reconnects automatically if its
profile is enabled and not paused.

## D-Bus Direction

Current D-Bus API:

```text
Status() -> s
GetSetting(s key) -> s
SetSetting(s key, s value) -> b
```

Target D-Bus API:

```text
ListDisplays() -> a{sv} or JSON string initially
AddDisplay(s id, a{sv} profile) -> b
RemoveDisplay(s id) -> b
PauseDisplay(s id) -> b
ResumeDisplay(s id) -> b
GetDisplay(s id) -> a{sv}
SetDisplaySetting(s id, s key, s value) -> b
InstallVirtualCard() -> b
RemoveVirtualCard() -> b
```

For the near term, using JSON strings is acceptable to keep the C D-Bus code
simple. Once the model stabilizes, switch to typed D-Bus structs.

## Migration Plan

1. Add `DisplayManager` as the runtime owner of display sessions.
2. Move the existing single `EvdiDevice` into display slot `default`.
3. Change settings to support `display.N.*` profiles while preserving legacy
   keys as an auto-migrated `default` profile.
4. Change default settings to zero displays only after `loomctl display add`
   exists.
5. Add runtime D-Bus commands:
   - `display list`
   - `display add`
   - `display remove`
   - `display pause`
   - `display resume`
6. Add USB device monitor and pair display profiles by USB serial/accessory id.
7. Update `loom-tray` to manage the display list.

## Important Implementation Notes

- Do not create an EVDI card merely because `loomd` started.
- Only create/open/connect EVDI for enabled profiles whose physical device is
  present, or for profiles explicitly forced on by the user.
- Disconnect EVDI immediately when a required physical device disappears.
- Keep profile data after disconnect; only session state is torn down.
- For current EVDI, one active display equals one active EVDI card.
- The old single-display config remains valid until the multi-display commands
  are feature-complete.
