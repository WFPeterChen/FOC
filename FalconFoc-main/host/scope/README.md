# Bench oscilloscope — SCPI over LAN

Driver + CLI for the lab scope, used for FOC hardware debugging (probe phase
currents / PWM / encoder lines while the firmware runs).

## Instrument (verified 2026-07-10)

- **Siglent SDS814X HD** — 12-bit "HD" platform, 4 analog channels.
  `*IDN?` → `Siglent Technologies,SDS814X HD,SDS08A0Q809209,3.8.12.1.1.3.8`
- **LAN**: `192.168.0.112` on the internal net (DHCP, gateway 192.168.0.1).
  Ports open: **5025 raw-socket SCPI** (what we use), 111 VXI-11, 80 web UI,
  5900 VNC (not needed).
- No VISA / no numpy / no pip anything — pure python3 stdlib.

## ⚠️ Do not crash the scope (learned the hard way, 2026-07-10)

This firmware (**v3.8.12**) is fragile over SCPI. Two failure modes we hit:

1. **Whole-instrument hard crash** — frozen display, dead front-panel keys,
   requires a **power-cycle**. Cause: **flooding it with back-to-back commands**
   (no inter-command delay). The driver now paces every command by `pace=0.05 s`
   and this stopped both the crashes *and* silently-dropped commands (e.g. a
   trigger level that read back as 0). **Never set `pace=0`.**
2. **SCPI server wedge** — port 5025 starts refusing connections (111/80 stay up)
   and does not self-heal for minutes. Cause: **connection churn** — opening and
   closing a new socket for every command. The server accepts **one client** and
   dislikes rapid reconnects.

Rules that keep it alive:
- **Reuse one connection** for a whole session (`with SiglentScope() as sc:` or
  keep the CLI usage occasional, not in a tight loop).
- Let the built-in pacing do its job; add `sc.sync()` (`*OPC?`) after a burst of
  setters before reading state back.
- If 5025 refuses: `open()` retries with backoff. If it stays dead for minutes,
  the scope needs a **power-cycle** — there is no clean remote recovery (VXI-11
  core on :111 wedges too).
- Avoid querying `:ACQuire:AVERage?` — it hangs on this firmware.

## Quick start

```bash
export SCOPE_HOST=192.168.0.112        # optional; this is the default
./siglent_scope.py idn
./siglent_scope.py state               # trigger/acq/timebase + per-channel scale
./siglent_scope.py screenshot shot.png # PNG of the display (great for "seeing" it)
./siglent_scope.py run | stop | single | autoset

# capture — one or several channels, coherent frame, to CSV
./siglent_scope.py capture -c C1 --points 50000 --csv cap.csv --stop
./siglent_scope.py capture -c C1,C2,C3 --points 20000 --csv iabc.csv   # 3-phase, time-aligned

# automatic measurements
./siglent_scope.py measure -c C1 --types PKPK,MEAN,RMS,FREQ,DUTY

# spectrum (current ripple / switching harmonics), pure-python FFT
./siglent_scope.py fft -c C1 --points 32768 --csv spec.csv

# configuration
./siglent_scope.py set-chan 1 --scale 0.5 --coupling DC --unit A --probe 10   # current probe → amps
./siglent_scope.py set-chan 2 --on off
./siglent_scope.py set-time --scale 1e-3
./siglent_scope.py set-trig --source C1 --level 0.5 --slope RISing --mode NORMal
./siglent_scope.py set-acq --type ERES        # low-noise acquisition for current traces
./siglent_scope.py raw ':TIMebase:SCALe?'
```

Library use (preferred — one persistent, paced connection):

```python
from siglent_scope import SiglentScope, fft_mag
with SiglentScope() as sc:                       # paced; retries on connect
    sc.set_channel(1, scale=0.5, coupling="DC", unit="A")   # current probe
    sc.set_trigger_edge(source="C1", level=0.5, slope="RISing", mode="NORMal")
    sc.sync()                                     # *OPC? — settle before reading
    cap = sc.capture_many(["C1", "C2", "C3"], points=50000, stop_first=True)
    # cap['t'] (s), cap['channels']['C1'] (V/A), cap['dt'], cap['n']
    freqs, amps, dom = fft_mag(cap["channels"]["C1"], cap["dt"])
    print(sc.measure_many("C1", ["PKPK", "RMS", "FREQ"]))
```

## FOC debugging recipes

- **Three phase currents, time-aligned**: current probes on C1/C2/C3, `set-chan
  --unit A`, then `capture -c C1,C2,C3`. Check i_a+i_b+i_c ≈ 0.
- **Catch a one-shot transient** (fault, step response): `set-trig --mode NORMal`
  on a debug/PWM edge, then `single`, then `capture --stop`.
- **PWM frequency / dead-time**: `measure --types FREQ,DUTY,PWID,NWID` on the gate.
- **Current ripple / switching noise**: `fft -c C1` — the switching frequency and
  its harmonics show up as peaks; use `set-acq --type ERES` first to cut noise.
- **Encoder / index alignment**: capture the encoder line alongside a phase.

## Protocol facts that bit us (don't relearn these the hard way)

- **SCPI dialect is Keysight-style subsystems**: `:CHANnel<n>:`, `:TIMebase:`,
  `:ACQuire:`, `:TRIGger:`, `:WAVeform:`. NOT the old `C1:VDIV?` LeCroy style.
- **Waveform payload = IEEE-488.2 definite-length block** `#<ndig><len><bytes>`.
  You must loop `recv()` until you've read exactly `len` bytes, then eat the
  trailing `\n`. Short reads WILL happen on a 2 MB transfer.
- **Read 12-bit data as WORD** (`:WAVeform:WIDTh WORD`) → signed 16-bit samples.
  BYTE truncates HD resolution.
- **Voltage conversion** (validated against live capture):
  `volts = code * (vdiv / code_per_div) - voffset`, all three from the WAVEDESC
  preamble. On a 2 V/div capture `code_per_div = 7680`.
- **`:WAVeform:POINt 0` returns only the on-screen frame** (~a few k points),
  NOT the full record. For deep memory pass an explicit `--points N` (up to
  `:WAVeform:MAXPoint?`, which was 5,000,000 here).
- **Screenshot is a RAW PNG stream, not an IEEE block.** `:PRINt? PNG` streams
  `\n` + `\x89PNG...` with no length header. Read until the socket goes idle and
  trim to the `\x89PNG` magic. (`screenshot()` handles this.)
- The scope powers up in **ROLL** mode at slow timebase; `capture(stop_first=True)`
  freezes it for a coherent frame.

## WAVEDESC preamble offsets (little-endian, confirmed on this unit)

| offset | type    | field            | note                          |
|-------:|---------|------------------|-------------------------------|
| 0      | char[16]| descriptor name  | `WAVEDESC`                    |
| 32     | int16   | comm_type        | 0=BYTE, 1=WORD                |
| 34     | int16   | comm_order       | 0=little-endian, 1=big        |
| 116    | int32   | wave_array_count | samples in this transfer      |
| 124    | int32   | first_valid_pnt  |                               |
| 128    | int32   | last_valid_pnt   |                               |
| 156    | float32 | vdiv (V/div)     |                               |
| 160    | float32 | vertical offset  | volts                         |
| 164    | float32 | code_per_div     | ADC codes per div (7680 here) |
| 176    | float32 | sample interval  | seconds (= 1/SRATe)           |
| 180    | float64 | trigger delay    | time of first sample, s       |
