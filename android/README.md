# Android Client

Jetpack Compose tablet client for Loom.

Current prototype:

- listens on TCP port `27183`
- receives H.264 Annex-B byte stream over ADB USB forwarding
- decodes with Android `MediaCodec`
- renders fullscreen to a `SurfaceView`

Run:

```bash
./gradlew :app:assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb forward tcp:27183 tcp:27183
adb shell am start -n com.jainhardik120.loom/.MainActivity
```

Then enable streaming from the repo root:

```bash
./build/loomctl set stream_enabled true
```
