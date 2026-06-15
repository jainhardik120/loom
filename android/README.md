# Android Client

Jetpack Compose tablet client for Loom.

Current prototype:

- receives H.264 Annex-B byte stream from USB accessory mode when available
- falls back to listening on TCP port `27183`
- decodes with Android `MediaCodec`
- renders fullscreen to a `SurfaceView`
- shows a small debug stats overlay

Run:

```bash
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb forward tcp:27183 tcp:27183
adb shell am start -n com.jainhardik120.loom/.MainActivity
```

Then enable USB accessory streaming from the repo root:

```bash
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./build/loomd --config "$HOME/.config/loom/loomd.conf"
./build/loomctl set stream_transport usb_accessory
./build/loomctl set stream_enabled true
```

TCP fallback still works through ADB:

```bash
adb forward tcp:27183 tcp:27183
./build/loomctl set stream_transport tcp
./build/loomctl set stream_enabled true
```

USB accessory mode:

```bash
LD_LIBRARY_PATH=third_party/evdi/library sudo -E ./build/loomd --config "$HOME/.config/loom/loomd.conf"
./build/loomctl set stream_transport usb_accessory
./build/loomctl set stream_enabled true
```

The desktop build needs `libusb-1.0-0-dev` for the real USB accessory transport. Without it, the app still builds and TCP fallback still works.
