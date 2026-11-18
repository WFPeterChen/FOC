---
name: scope
description: Talk to the bench oscilloscope (Siglent SDS814X HD at 192.168.0.112) over LAN SCPI to debug FOC hardware. Use whenever the user wants to read a waveform / voltage / signal off the scope, capture channel data to CSV, grab a screenshot of the scope display, check or change its state (run/stop/single, timebase, channel scale), or "look at" what a probe is measuring. Triggers: "示波器", "scope", "oscilloscope", "抓波形/拉数据", "看看 C1/C2/CH1", "截个图", Siglent, SCPI, waveform capture.
---

# Bench oscilloscope (Siglent SDS814X HD)

A zero-dependency python3 driver already exists and is verified working. **Use it —
do not re-derive the SCPI protocol.** The instrument is a Siglent SDS814X HD (12-bit)
on the internal LAN at `192.168.0.112`, raw-socket SCPI on TCP **5025**.

Driver + full protocol notes:
`FalconFoc-main/host/scope/siglent_scope.py` and `.../scope/README.md`.

## ⚠️ This firmware crashes if you abuse it — read before sending SCPI

The SDS814X HD (fw v3.8.12) is fragile. On 2026-07-10 careless probing **hard-
crashed the whole instrument** (frozen screen, dead keys, needed a power-cycle)
and separately **wedged the :5025 server** (refuses connections for minutes, no
self-heal). Both are avoidable:

- **Never flood it.** The driver paces every command (`pace=0.05s`) — keep it.
  Flooding also silently drops commands (a set that reads back wrong).
- **Reuse ONE connection** per session (`with SiglentScope() as sc:`). Do **not**
  run the CLI in a tight loop — each call reconnects, and connection churn wedges
  the single-client server. For multiple ops, use the library, not repeated CLI.
- After a burst of setters call `sc.sync()` (`*OPC?`) before reading state back.
- Don't query `:ACQuire:AVERage?` (hangs on this fw).
- If :5025 refuses and stays dead for minutes → the scope needs a **power-cycle**;
  there's no clean remote recovery. Tell the user.

## Do this first

```bash
cd FalconFoc-main/host/scope
./siglent_scope.py idn      # confirm reachable → "Siglent Technologies,SDS814X HD,..."
./siglent_scope.py state    # trigger/acq/timebase + which channels are on and their V/div
```

If `idn` hangs/fails: `ping 192.168.0.112`; check port 5025 is open. Override IP with
`SCOPE_HOST=<ip>` or `--host <ip>`.

## Common tasks (CLI — fine for one-offs)

```bash
./siglent_scope.py screenshot shot.png            # then Read the PNG to "see" it
./siglent_scope.py capture -c C1 --points 50000 --csv cap.csv --stop
./siglent_scope.py capture -c C1,C2,C3 --csv iabc.csv   # 3-phase, time-aligned
./siglent_scope.py measure -c C1 --types PKPK,MEAN,RMS,FREQ,DUTY
./siglent_scope.py fft -c C1 --points 32768       # current ripple / switching harmonics
./siglent_scope.py set-chan 1 --scale 0.5 --coupling DC --unit A --probe 10
./siglent_scope.py set-trig --source C1 --level 0.5 --slope RISing --mode NORMal
./siglent_scope.py set-acq --type ERES            # low-noise for current traces
./siglent_scope.py run | stop | single | autoset
./siglent_scope.py raw ':ACQuire:SRATe?'
```

## For anything multi-step, use the library (one paced connection)

```python
from siglent_scope import SiglentScope, fft_mag
with SiglentScope() as sc:                        # SCOPE_HOST env or default IP
    sc.set_channel(1, scale=0.5, coupling="DC", unit="A")   # current probe → amps
    sc.set_trigger_edge(source="C1", level=0.5, slope="RISing", mode="NORMal")
    sc.sync()                                     # settle before reading back
    cap = sc.capture_many(["C1", "C2", "C3"], points=50000, stop_first=True)
    # cap['t'] (s), cap['channels']['C1'] (V/A), cap['dt']
    print(sc.measure_many("C1", ["PKPK", "RMS", "FREQ"]))
    freqs, amps, dom = fft_mag(cap["channels"]["C1"], cap["dt"])
```

## FOC recipes
- 3-phase currents aligned: current probes on C1/C2/C3, `--unit A`, `capture -c C1,C2,C3`; check i_a+i_b+i_c≈0.
- One-shot transient/fault: `set-trig --mode NORMal` on a PWM/debug edge → `single` → `capture --stop`.
- PWM freq / dead-time: `measure --types FREQ,DUTY,PWID,NWID` on the gate.
- Switching noise spectrum: `fft -c C1` (peaks at fsw & harmonics); `set-acq --type ERES` first.

## Gotchas (already handled by the driver — mind them if you go raw)

- Waveform data is an **IEEE-488.2 block** `#<ndig><len><bytes>`; loop `recv` to the
  full length. Read **WORD** width for 12-bit HD; `volts = code*(vdiv/code_per_div) - voffset`
  from the WAVEDESC preamble.
- `:WAVeform:POINt 0` returns only the on-screen frame, not deep memory → pass `--points N`.
- Screenshot (`:PRINt? PNG`) is a **raw PNG stream, not a block** — read until idle,
  trim to `\x89PNG`. `screenshot()` does this.
- Scope boots in **ROLL** mode; use `--stop` / `stop_first=True` for a coherent frame.

SCPI dialect is Keysight-style (`:CHANnel/:TIMebase/:ACQuire/:TRIGger/:WAVeform`), not
the old LeCroy `C1:VDIV?` style. See README.md for the full preamble field table.
