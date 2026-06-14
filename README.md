# loom

Monorepo for the Loom tablet-as-secondary-display project.

## Components

- `loomd`: Linux desktop daemon. It owns EVDI lifecycle, receives framebuffer updates, and will later encode and stream frames.
- `loomctl`: planned command-line control client for `loomd`.
- `gnome-shell-extension`: planned GNOME Shell integration for status and display controls.
- `android`: planned Android tablet client.
- `src/common`: shared C code for logging, settings, and control protocol constants.
- `src/loomd`: daemon-specific C code.
- `src/loomctl`: command-line client C code.
- `third_party/evdi`: EVDI source embedded as a Git submodule.

Current prototype pipeline:

```text
GNOME / Wayland
  -> EVDI virtual display
  -> loomd framebuffer updates
  -> raw frame/debug output
```

Future pipeline:

```text
GNOME / Wayland
  -> EVDI virtual display
  -> loomd
  -> AMD VAAPI encoder
  -> USB transport
  -> Android MediaCodec fullscreen client
```

## Clone

```bash
git clone --recursive git@github.com:jainhardik120/loom.git
cd loom
```

If the repo was cloned without submodules:

```bash
make submodules
```

## Build

```bash
make
```

This builds the EVDI user-space library from `third_party/evdi/library` and then builds:

```text
build/loomd
build/loomctl
```

`loomd` and `loomctl` intentionally share code from `src/common`.

## Run

Load EVDI first if needed:

```bash
sudo modprobe evdi
```

Run the daemon:

```bash
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./build/loomd
```

Or:

```bash
make run-loomd
```

Useful daemon options:

```bash
./build/loomd --help
sudo ./build/loomd --device 0
sudo ./build/loomd --no-capture
sudo ./build/loomd --dump-frame frame.raw
```

## Settings and Control

`loomctl` currently manages the local settings file:

```bash
./build/loomctl status
./build/loomctl get capture_enabled
./build/loomctl set capture_enabled false
./build/loomctl settings
./build/loomctl settings get capture_enabled
./build/loomctl settings set capture_enabled false
./build/loomctl settings path
```

By default, settings are stored at:

```text
~/.config/loom/loomd.conf
```

`loomd` reads that file on startup. Use `--config PATH` if the daemon should read another file:

```bash
sudo -E ./build/loomd --config "$HOME/.config/loom/loomd.conf"
```

The intended runtime control plane is D-Bus:

```text
service:   org.loom.Display
object:    /org/loom/Display
interface: org.loom.Display1
```

`loomd` owns this service on the user session bus when it can connect to that bus. If you run `loomd` with `sudo`, preserve the session bus environment:

```bash
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./build/loomd
```

## Repository Setup

The outer repo should use:

```bash
git remote add origin git@github.com:jainhardik120/loom.git
```

The EVDI submodule should use:

```bash
git -C third_party/evdi remote set-url origin git@github.com:jainhardik120/evdi.git
```

## Debug Commands

Check EVDI device nodes:

```bash
ls -l /dev/dri/card* /dev/dri/renderD*
```

Inspect the EVDI card:

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
