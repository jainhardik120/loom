# loomctl

Command-line client for Loom.

Current commands:

```bash
./build/loomctl status
./build/loomctl get KEY
./build/loomctl set KEY VALUE
./build/loomctl settings
./build/loomctl settings get KEY
./build/loomctl settings set KEY VALUE
./build/loomctl settings path
```

`status`, `get`, and `set` talk to the live `loomd` process over D-Bus.

`settings` reads and writes `~/.config/loom/loomd.conf`, which `loomd` can consume on startup with `--config`.

Live D-Bus commands expect `loomd` to be running on the same user session bus:

```bash
./build/loomctl status
./build/loomctl get capture_enabled
./build/loomctl set dump_frame false
./build/loomctl set stream_transport tcp
./build/loomctl set stream_transport usb_accessory
./build/loomctl set stream_enabled true
```
