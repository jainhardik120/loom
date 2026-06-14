# loom

Monorepo for the Loom tablet-as-secondary-display project.

## Components

- `loomd`: Linux desktop daemon. It owns EVDI lifecycle, receives framebuffer updates, and will later encode and stream frames.
- `loomctl`: planned command-line control client for `loomd`.
- `gnome-shell-extension`: planned GNOME Shell integration for status and display controls.
- `android`: planned Android tablet client.
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
loomd/build/loomd
```

## Run

Load EVDI first if needed:

```bash
sudo modprobe evdi
```

Run the daemon:

```bash
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./loomd/build/loomd
```

Or:

```bash
make run-loomd
```

Useful daemon options:

```bash
./loomd/build/loomd --help
sudo ./loomd/build/loomd --device 0
sudo ./loomd/build/loomd --no-capture
sudo ./loomd/build/loomd --dump-frame frame.raw
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

