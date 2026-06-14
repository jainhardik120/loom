# loomd

Linux desktop daemon for Loom.

`loomd` is currently an EVDI correctness prototype. It connects a fake monitor, listens for EVDI events, captures framebuffer updates into CPU memory, logs dirty rectangles, and can dump one raw frame for inspection.

## Architecture

Current pipeline:

```text
GNOME / Wayland
  -> EVDI virtual monitor
  -> loomd libevdi event loop
  -> CPU framebuffer capture
  -> raw-frame/debug output
```

Modules:

- `EvdiDevice`: finds or creates an EVDI DRM card, opens it through `libevdi`, connects a fake monitor EDID, owns EVDI callbacks, registers capture buffers, requests updates, and disconnects cleanly.
- `Framebuffer`: allocates and frees CPU frame buffers, tracks width, height, stride, format, dirty rectangles, and can dump one raw frame to `frame.raw`.
- `EventLoop`: installs SIGINT/SIGTERM handlers, polls the selectable fd from `evdi_get_event_ready`, and dispatches `evdi_handle_events`.
- `Logging`: timestamped stdout/stderr logging plus libevdi log forwarding.

Later modules can be added behind the same daemon boundary:

```text
EvdiDevice -> Framebuffer -> Encoder(VAAPI/AMD render node) -> Transport(ADB/USB)
```

## Build

From the monorepo root:

```bash
make loomd
```

From this directory:

```bash
make
```

The daemon links against the EVDI user-space library built from:

```text
../third_party/evdi/library/libevdi.so
```

## Run

Load EVDI first if needed:

```bash
sudo modprobe evdi
```

Run from the monorepo root:

```bash
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./loomd/build/loomd
```

Or from this directory:

```bash
./scripts/run-dev.sh
```

Useful options:

```bash
./build/loomd --help
sudo ./build/loomd --device 0
sudo ./build/loomd --no-capture
sudo ./build/loomd --dump-frame frame.raw
sudo ./build/loomd --no-dump-frame
```

`--no-capture` keeps the fake monitor connected and logs display events only. Without it, `loomd` allocates a CPU buffer after the first mode event, registers it with EVDI, requests updates, logs dirty rectangles, and dumps the first captured frame to `frame.raw` by default.

Stop with Ctrl+C. Shutdown unregisters the framebuffer, disconnects EVDI, frees memory, and closes the handle.

## Debug Commands

Check EVDI device nodes:

```bash
ls -l /dev/dri/card* /dev/dri/renderD*
```

Inspect `/dev/dri/card0`:

```bash
drm_info /dev/dri/card0
udevadm info --query=all --name=/dev/dri/card0
```

Watch GNOME/Mutter and kernel hotplug activity:

```bash
journalctl -f /usr/bin/gnome-shell
sudo dmesg -w
```

Check what GNOME sees:

```bash
gdbus call --session \
  --dest org.gnome.Mutter.DisplayConfig \
  --object-path /org/gnome/Mutter/DisplayConfig \
  --method org.gnome.Mutter.DisplayConfig.GetCurrentState
```

Expected early logs include an EVDI card selection, monitor connection, DPMS/CRTC/mode events, and then dirty rectangles once GNOME configures the virtual display.

