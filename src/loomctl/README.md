# loomctl

Command-line client for Loom.

Current commands:

```bash
./build/loomctl status
./build/loomctl display list
./build/loomctl display add ID [NAME]
./build/loomctl display remove ID
./build/loomctl display pause ID
./build/loomctl display resume ID
./build/loomctl display set ID KEY VALUE
./build/loomctl settings
./build/loomctl settings get KEY
./build/loomctl settings set KEY VALUE
./build/loomctl settings path
```

`status` and `display` commands talk to the live `loomd` process over D-Bus.

`display` commands manage live display sessions. `display add` creates a saved
profile and starts a new EVDI-backed virtual display. `display pause` disconnects
the display without removing its profile. `display resume` reconnects it.

`settings` reads and writes `~/.config/loom/loomd.conf`, which `loomd` can consume on startup with `--config`.

Live D-Bus commands expect `loomd` to be running on the same user session bus:

```bash
./build/loomctl status
./build/loomctl display add lenovo-tab "Lenovo Tab"
./build/loomctl display set lenovo-tab mode_refresh 60
./build/loomctl display pause lenovo-tab
./build/loomctl display resume lenovo-tab
```
