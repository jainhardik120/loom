# loom-tray

GTK/Ayatana AppIndicator control surface for `loomd`.

This replaces the removed GNOME Shell extension with a normal desktop status
indicator. It talks to `loomd` over the existing session D-Bus API and exposes
common runtime settings from the top-bar menu.

Build from the repo root:

```bash
make loom-tray
```

Run:

```bash
./build/loom-tray
```

Build dependencies:

```bash
sudo apt install libgtk-3-dev libayatana-appindicator3-dev
```
