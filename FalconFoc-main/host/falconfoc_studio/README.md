# FalconFOC Studio

A native instrument-style host tool for the FalconFOC board (STM32G431 + 4010),
speaking the DaMiao (DM) MIT-mode CAN protocol. C++ / Dear ImGui / ImPlot —
compiles to a single executable, real-time scope + FFT, no Python runtime.

![panels: Connection / Control / Telemetry (scope+FFT) / CAN log]

## Features
- **Connection**: bind a SocketCAN interface, live rx/tx counters + feedback rate.
- **Control**: MIT impedance / Position / Velocity / Torque modes; ENABLE / DISABLE /
  Clear-Err / Set-Zero / Cogging-Cal; continuous streaming (feeds the firmware's
  200 ms comms-deadman) or single-shot.
- **MIT Kd guard**: slider turns red past the 4010 stable ceiling (≈0.016); the
  firmware also hard-clamps now, this is the visual warning.
- **Telemetry**: rolling pos / vel / torque scopes with command overlay, plus a
  windowed-FFT spectrum (pick pos/vel/torque) for cogging / ripple analysis.
- **CAN log**: raw TX/RX frame trace with filters.
- Window layout is remembered (`falconfoc_studio.ini`).

## Build (Linux)
Dependencies (Ubuntu): `g++ cmake pkg-config libglfw3-dev libgl1-mesa-dev`
```sh
./setup_deps.sh           # clones Dear ImGui + ImPlot into external/ (once)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/falconfoc_studio
```

## Usage
1. Bring the CAN bus up (1 Mbps):
   `sudo ip link set can0 up type can bitrate 1000000`
2. Run `./build/falconfoc_studio`, type the interface (`can0`) + motor ID, **Connect**.
3. Pick a mode, set the setpoint, tick **Stream** (≥5 Hz so the firmware deadman
   stays fed), then **ENABLE**. The motor stops if streaming pauses >200 ms (by design).

## Headless hardware smoke test
`g++ -std=c++17 -O2 -Isrc src/test_cli.cpp src/can_bus.cpp -o build/test_cli -lpthread`
then `./build/test_cli can0` — enables, streams 3 rad/s, prints decoded feedback.

## Windows .exe (not built yet)
The GUI / protocol / plotting are portable; only `can_bus.cpp` is Linux-specific
(`linux/can.h`). A Windows build needs a CAN backend swap — for the gs_usb/candle
adapter use the **candle_api** (Windows) or an **slcan** serial backend behind the
same `CanBus` interface, then build with MSVC/MinGW + GLFW. Ask and I'll add it.

## Layout
```
src/dm_protocol.h   DM CAN encode/decode (mirrors firmware dm_can.c)
src/can_bus.{h,cpp} SocketCAN: open / send / background RX thread / drain
src/scope.h         rolling ring buffers + radix-2 FFT
src/app.{h,cpp}     application state + ImGui/ImPlot panels
src/main.cpp        GLFW/OpenGL/ImGui/ImPlot bootstrap + dark theme
external/           Dear ImGui + ImPlot (via setup_deps.sh)
```
