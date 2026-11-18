#!/usr/bin/env python3
"""
siglent_scope.py — zero-dependency SCPI driver + CLI for the bench oscilloscope.

Target instrument (verified 2026-07-10):
    Siglent Technologies, SDS814X HD   (12-bit "HD" platform, SDS800X HD family)
    LAN raw-socket SCPI on TCP:5025    (default host 192.168.0.112)

Why raw socket and not pyvisa: the SDS8xxX HD speaks plain LF-terminated SCPI on
port 5025, and waveform payloads are standard IEEE-488.2 definite-length blocks
(`#<ndigits><length><bytes>`). That needs no VISA layer and no numpy, so this file
runs anywhere python3 does. VXI-11 (:111) and the web UI (:80) are also open but
unused here.

Waveform conversion (validated against live capture):
    volts = code * (vdiv / code_per_div) - voffset
where `code` is a signed 16-bit sample (WORD width, holds the 12-bit HD value),
and vdiv / code_per_div / voffset come from the WAVEDESC preamble. On this unit a
2 V/div capture reported code_per_div = 7680.

CLI:
    ./siglent_scope.py idn
    ./siglent_scope.py state
    ./siglent_scope.py capture -c C1 --csv cap.csv [--points N] [--stop]
    ./siglent_scope.py run | stop | single
    ./siglent_scope.py raw ':TIMebase:SCALe?'
Env: SCOPE_HOST overrides the default IP.
"""
from __future__ import annotations
import argparse
import os
import socket
import struct
import sys
import time

DEFAULT_HOST = os.environ.get("SCOPE_HOST", "192.168.0.112")
SCPI_PORT = 5025

# WAVEDESC (346-byte preamble) little-endian field offsets, confirmed on SDS814X HD.
_OFF_COMM_TYPE   = 32    # int16: 0=BYTE, 1=WORD
_OFF_COMM_ORDER  = 34    # int16: 0=LOFIRST(little), 1=HIFIRST(big)
_OFF_WAVE_COUNT  = 116   # int32: number of samples in this transfer
_OFF_FIRST_VALID = 124   # int32
_OFF_LAST_VALID  = 128   # int32
_OFF_VDIV        = 156   # float32: volts/div of the captured data
_OFF_VOFFSET     = 160   # float32: vertical offset (volts)
_OFF_CODE_PERDIV = 164   # float32: ADC codes per vertical division
_OFF_INTERVAL    = 176   # float32: seconds between samples (1/sample_rate)
_OFF_TRIG_DELAY  = 180   # float64: time of the first sample (trigger delay, s)


class ScopeError(RuntimeError):
    pass


class SiglentScope:
    """Minimal robust SCPI client for Siglent SDS8xxX HD over raw socket 5025."""

    def __init__(self, host: str = DEFAULT_HOST, port: int = SCPI_PORT, timeout: float = 8.0,
                 pace: float = 0.05):
        self.host, self.port, self.timeout = host, port, timeout
        # pace = min delay after every command. CRITICAL: this firmware
        # (SDS814X HD v3.8.12) HARD-CRASHES the whole instrument (frozen display,
        # dead keys) when flooded with back-to-back SCPI. Never set pace to 0.
        self.pace = pace
        self.sock: socket.socket | None = None

    # -- connection -------------------------------------------------------
    def open(self, retries: int = 8, backoff: float = 1.5) -> "SiglentScope":
        """Connect, retrying on refusal. The SDS raw-socket SCPI server accepts
        ONE client at a time and briefly refuses new connections after a peer
        closes (or if it has wedged), so a short backoff loop is worth it."""
        last = None
        for attempt in range(retries):
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(self.timeout)
                s.connect((self.host, self.port))
                self.sock = s
                return self
            except OSError as e:
                last = e
                time.sleep(backoff)
        raise ScopeError(f"could not connect to {self.host}:{self.port} after "
                         f"{retries} tries ({last}). Is another SCPI client open, "
                         f"or has the scope's :5025 server wedged? See README recovery note.")

    def close(self) -> None:
        if self.sock:
            self.sock.close()
            self.sock = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    # -- primitives -------------------------------------------------------
    def write(self, cmd: str) -> None:
        assert self.sock, "not connected"
        self.sock.sendall((cmd + "\n").encode())
        if self.pace:
            time.sleep(self.pace)   # throttle: this firmware crashes when flooded

    def sync(self, timeout: float | None = None) -> None:
        """Block until the instrument has processed all prior commands (*OPC?).
        Call after a burst of writes before assuming the new state is live."""
        old = self.sock.gettimeout()
        self.sock.settimeout(timeout or self.timeout)
        try:
            self.query("*OPC?")
        finally:
            self.sock.settimeout(old)

    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise ScopeError(f"connection closed with {len(buf)}/{n} bytes")
            buf += chunk
        return bytes(buf)

    def query(self, cmd: str) -> str:
        """Send a query, read one LF-terminated ASCII response."""
        self.write(cmd)
        buf = bytearray()
        while not buf.endswith(b"\n"):
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            buf += chunk
        return buf.decode(errors="replace").strip()

    def query_block(self, cmd: str) -> bytes:
        """Send a query, read one IEEE-488.2 definite-length block `#NLLLL...`.
        Self-correcting: skips any stray leading whitespace/newline (left over
        from a previous response) before the '#', and drains the trailing
        terminator afterwards so back-to-back block reads stay in sync."""
        self.write(cmd)
        head = self._recv_exact(1)
        while head in (b"\n", b"\r", b" "):     # skip stray leading terminator bytes
            head = self._recv_exact(1)
        if head != b"#":
            raise ScopeError(f"expected block header '#', got {head!r}")
        ndig = int(self._recv_exact(1))
        length = int(self._recv_exact(ndig))
        data = self._recv_exact(length)
        # drain the trailing terminator without blocking on absence
        old = self.sock.gettimeout()
        self.sock.settimeout(0.15)
        try:
            while True:
                b = self.sock.recv(1)
                if not b or b not in (b"\n", b"\r"):
                    break
        except socket.timeout:
            pass
        finally:
            self.sock.settimeout(old)
        return data

    # -- high level -------------------------------------------------------
    def idn(self) -> str:
        return self.query("*IDN?")

    def run(self) -> None:
        self.write(":TRIGger:RUN")

    def stop(self) -> None:
        self.write(":TRIGger:STOP")

    def trigger_status(self) -> str:
        return self.query(":TRIGger:STATus?")

    def single(self, timeout: float = 10.0, poll: float = 0.05) -> None:
        """Arm a single acquisition and block until it stops (or timeout)."""
        self.write(":TRIGger:MODE SINGle")
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.trigger_status().lower() in ("stop", "ready"):
                return
            time.sleep(poll)
        raise ScopeError("single: timed out waiting for acquisition to stop")

    def state(self) -> dict:
        """Snapshot of the useful front-panel/acquisition settings."""
        d = {
            "idn": self.idn(),
            "trigger_status": self.query(":TRIGger:STATus?"),
            "trigger_mode": self.query(":TRIGger:MODE?"),
            "acq_mode": self.query(":ACQuire:MODE?"),
            "sample_rate": self.query(":ACQuire:SRATe?"),
            "mem_depth": self.query(":ACQuire:POINts?"),
            "timebase_s_div": self.query(":TIMebase:SCALe?"),
        }
        for ch in (1, 2, 3, 4):
            on = self.query(f":CHANnel{ch}:SWITch?")
            d[f"C{ch}"] = {"on": on}
            if on.upper() == "ON":
                d[f"C{ch}"]["scale_v_div"] = self.query(f":CHANnel{ch}:SCALe?")
                d[f"C{ch}"]["offset_v"] = self.query(f":CHANnel{ch}:OFFSet?")
        return d

    def screenshot(self, path: str, idle: float = 2.0) -> int:
        """Grab the display as PNG. The scope streams raw PNG bytes (NOT an IEEE
        block) after `:PRINt? PNG`, so we read until the link goes idle and trim
        to the PNG magic. Returns bytes written."""
        assert self.sock, "not connected"
        self.write(":PRINt? PNG")
        old = self.sock.gettimeout()
        self.sock.settimeout(idle)
        buf = bytearray()
        try:
            while True:
                chunk = self.sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
        except socket.timeout:
            pass
        finally:
            self.sock.settimeout(old)
        magic = b"\x89PNG"
        start = buf.find(magic)
        if start < 0:
            raise ScopeError("no PNG magic in screenshot response")
        png = bytes(buf[start:])
        with open(path, "wb") as fp:
            fp.write(png)
        return len(png)

    @staticmethod
    def parse_preamble(pre: bytes) -> dict:
        if len(pre) < 184:
            raise ScopeError(f"preamble too short: {len(pre)} bytes")
        f = lambda o: struct.unpack_from("<f", pre, o)[0]
        i = lambda o: struct.unpack_from("<i", pre, o)[0]
        h = lambda o: struct.unpack_from("<h", pre, o)[0]
        dbl = lambda o: struct.unpack_from("<d", pre, o)[0]
        return {
            "comm_type": h(_OFF_COMM_TYPE),     # 1 = WORD
            "comm_order": h(_OFF_COMM_ORDER),   # 0 = little-endian
            "count": i(_OFF_WAVE_COUNT),
            "first_valid": i(_OFF_FIRST_VALID),
            "last_valid": i(_OFF_LAST_VALID),
            "vdiv": f(_OFF_VDIV),
            "voffset": f(_OFF_VOFFSET),
            "code_per_div": f(_OFF_CODE_PERDIV),
            "interval": f(_OFF_INTERVAL),
            "trig_delay": dbl(_OFF_TRIG_DELAY),
        }

    def capture(self, source: str = "C1", points: int | None = None,
                start: int = 0, stop_first: bool = False) -> dict:
        """Capture one channel. Returns dict with t[], v[], and the parsed preamble.

        points=None grabs the whole record (up to the current memory depth).
        stop_first=True freezes acquisition before reading for a coherent frame.
        """
        if stop_first:
            self.stop()
        self.write(f":WAVeform:SOURce {source}")
        self.write(":WAVeform:WIDTh WORD")          # 16-bit container for 12-bit HD data
        self.write(f":WAVeform:STARt {int(start)}")
        if points is not None:
            self.write(f":WAVeform:POINt {int(points)}")
        else:
            self.write(":WAVeform:POINt 0")          # 0 = all available

        pre = self.parse_preamble(self.query_block(":WAVeform:PREamble?"))
        raw = self.query_block(":WAVeform:DATA?")

        n = len(raw) // 2
        codes = struct.unpack(f"<{n}h", raw[: n * 2])
        gain = pre["vdiv"] / pre["code_per_div"]
        voff = pre["voffset"]
        dt = pre["interval"]
        t0 = pre["trig_delay"]
        volts = [c * gain - voff for c in codes]
        t = [t0 + k * dt for k in range(n)]
        return {"source": source, "n": n, "dt": dt, "t": t, "v": volts, "preamble": pre}

    def capture_many(self, sources, points: int | None = None,
                     stop_first: bool = True) -> dict:
        """Coherently capture several channels from ONE frozen acquisition.
        Essential for 3-phase FOC (i_a/i_b/i_c time-aligned). Returns
        {'t': [...], 'dt': , 'channels': {src: [volts]}, 'n': }.
        Freezes once, then reads each source without re-arming."""
        if stop_first:
            self.stop()
        out = {"channels": {}, "t": None, "dt": None, "n": None}
        for src in sources:
            cap = self.capture(src, points=points, stop_first=False)
            out["channels"][src] = cap["v"]
            if out["t"] is None:
                out["t"], out["dt"], out["n"] = cap["t"], cap["dt"], cap["n"]
        return out

    # -- configuration setters -------------------------------------------
    def set_channel(self, ch: int, on: bool | None = None, scale=None, offset=None,
                    coupling=None, probe=None, bwlimit=None, unit=None,
                    invert=None, label_text=None, label=None) -> None:
        """Configure a vertical channel. coupling: DC|AC|GND; unit: V|A (use A for
        a current probe so captures read in amps); bwlimit: FULL|20M; probe: ratio
        like 1 or 10; label shows a name on screen."""
        p = f":CHANnel{ch}"
        if on is not None:      self.write(f"{p}:SWITch {'ON' if on else 'OFF'}")
        if scale is not None:   self.write(f"{p}:SCALe {scale}")
        if offset is not None:  self.write(f"{p}:OFFSet {offset}")
        if coupling is not None:self.write(f"{p}:COUPling {coupling}")
        if probe is not None:   self.write(f"{p}:PROBe {probe}")
        if bwlimit is not None: self.write(f"{p}:BWLimit {bwlimit}")
        if unit is not None:    self.write(f"{p}:UNIT {unit}")
        if invert is not None:  self.write(f"{p}:INVert {'ON' if invert else 'OFF'}")
        if label_text is not None: self.write(f'{p}:LABel:TEXT "{label_text}"')
        if label is not None:   self.write(f"{p}:LABel {'ON' if label else 'OFF'}")

    def set_timebase(self, scale=None, delay=None) -> None:
        """scale = seconds/div, delay = horizontal position (s)."""
        if scale is not None: self.write(f":TIMebase:SCALe {scale}")
        if delay is not None: self.write(f":TIMebase:DELay {delay}")

    def set_trigger_edge(self, source=None, level=None, slope=None,
                         coupling=None, mode=None) -> None:
        """Edge trigger. slope: RISing|FALLing; mode: AUTO|NORMal|SINGle
        (NORMal is what you want to catch a one-shot FOC transient)."""
        self.write(":TRIGger:TYPE EDGE")
        if source is not None:   self.write(f":TRIGger:EDGE:SOURce {source}")
        if slope is not None:    self.write(f":TRIGger:EDGE:SLOPe {slope}")
        if coupling is not None: self.write(f":TRIGger:EDGE:COUPling {coupling}")
        if level is not None:    self.write(f":TRIGger:EDGE:LEVel {level}")
        if mode is not None:     self.write(f":TRIGger:MODE {mode}")

    def set_acquire(self, atype=None, mdepth=None, interp=None, average=None) -> None:
        """atype: NORMal|PEAK|AVERage|ERES (AVERage/ERES cut noise on current
        traces). mdepth like '1M'. average: number of averages when atype=AVERage."""
        if atype is not None:   self.write(f":ACQuire:TYPE {atype}")
        if average is not None: self.write(f":ACQuire:AVERage {average}")
        if mdepth is not None:  self.write(f":ACQuire:MDEPth {mdepth}")
        if interp is not None:  self.write(f":ACQuire:INTerpolation {interp}")

    def autoset(self) -> None:
        self.write(":AUToset")

    def reset(self) -> None:
        """*RST — restores default setup. Heavy; use deliberately."""
        self.write("*RST")

    # -- measurements -----------------------------------------------------
    def measure(self, mtype: str, source: str, source2: str | None = None,
                slot: int = 1, settle: float = 0.3) -> float | None:
        """Configure advanced-measure slot P<slot> and read its value.
        mtype e.g. PKPK, MAX, MIN, AMPL, MEAN, RMS, FREQ, PERiod, DUTY,
        RISetime, FALLtime, PWIDth, NWIDth; two-source: PHA, SKEW, DELay
        (both channels must be ON). Returns float or None if unavailable ('****')."""
        p = f":MEASure:ADVanced:P{slot}"
        self.write(":MEASure ON")
        self.write(f"{p} ON")
        self.write(f"{p}:SOURce1 {source}")
        if source2 is not None:
            self.write(f"{p}:SOURce2 {source2}")
        self.write(f"{p}:TYPE {mtype}")
        time.sleep(settle)
        raw = self.query(f"{p}:VALue?")
        try:
            return float(raw)
        except ValueError:
            return None

    def measure_many(self, source: str, types, start_slot: int = 1,
                     settle: float = 0.4) -> dict:
        """Configure several single-source measurements on `source` and read all.
        Returns {mtype: float|None}. Handy FOC set: PKPK, MEAN, RMS, FREQ, DUTY."""
        p = ":MEASure:ADVanced"
        self.write(":MEASure ON")
        for i, mt in enumerate(types):
            slot = start_slot + i
            self.write(f"{p}:P{slot} ON")
            self.write(f"{p}:P{slot}:SOURce1 {source}")
            self.write(f"{p}:P{slot}:TYPE {mt}")
        time.sleep(settle)
        res = {}
        for i, mt in enumerate(types):
            raw = self.query(f"{p}:P{start_slot + i}:VALue?")
            try:
                res[mt] = float(raw)
            except ValueError:
                res[mt] = None
        return res


# ---------------------------------------------------------------------------
# Analysis helpers (no numpy)
# ---------------------------------------------------------------------------
def fft_mag(v, dt, window: str = "hann", maxn: int = 65536):
    """One-sided amplitude spectrum of a captured signal, pure python (radix-2).
    Truncates to the largest power of two <= min(len(v), maxn). Returns
    (freqs_hz, amps, dominant_hz). Use for current ripple / switching harmonics."""
    import cmath, math
    n = min(len(v), maxn)
    p2 = 1 << (n.bit_length() - 1)          # largest power of two <= n
    x = list(v[:p2])
    mean = sum(x) / p2
    x = [xi - mean for xi in x]             # drop DC
    if window == "hann":
        w = [0.5 - 0.5 * math.cos(2 * math.pi * i / (p2 - 1)) for i in range(p2)]
        coh = sum(w) / p2                   # coherent gain, for amplitude correction
        x = [xi * wi for xi, wi in zip(x, w)]
    else:
        coh = 1.0
    # iterative radix-2 FFT
    a = [complex(xi) for xi in x]
    j = 0
    for i in range(1, p2):
        bit = p2 >> 1
        while j & bit:
            j ^= bit; bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    while length <= p2:
        ang = -2j * cmath.pi / length
        wl = cmath.exp(ang)
        for i in range(0, p2, length):
            w = 1 + 0j
            for k in range(length // 2):
                u = a[i + k]; t = a[i + k + length // 2] * w
                a[i + k] = u + t; a[i + k + length // 2] = u - t
                w *= wl
        length <<= 1
    half = p2 // 2
    df = 1.0 / (p2 * dt)
    amps = [2.0 * abs(a[k]) / (p2 * coh) for k in range(half)]
    freqs = [k * df for k in range(half)]
    dom = freqs[max(range(1, half), key=lambda k: amps[k])] if half > 1 else 0.0
    return freqs, amps, dom


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def _fmt_state(d: dict) -> str:
    lines = [f"IDN            : {d['idn']}",
             f"trigger        : {d['trigger_status']}  (mode {d['trigger_mode']})",
             f"acquire        : {d['acq_mode']}  srate={d['sample_rate']}  depth={d['mem_depth']}",
             f"timebase       : {d['timebase_s_div']} s/div"]
    for ch in (1, 2, 3, 4):
        c = d[f"C{ch}"]
        if c["on"].upper() == "ON":
            lines.append(f"  C{ch} ON        : {c.get('scale_v_div')} V/div  off {c.get('offset_v')} V")
        else:
            lines.append(f"  C{ch} off")
    return "\n".join(lines)


def _save_csv(path: str, cap: dict) -> None:
    with open(path, "w") as fp:
        fp.write(f"# source={cap['source']} n={cap['n']} dt={cap['dt']:.6g}s "
                 f"vdiv={cap['preamble']['vdiv']} code_per_div={cap['preamble']['code_per_div']}\n")
        fp.write("t_s,v\n")
        for t, v in zip(cap["t"], cap["v"]):
            fp.write(f"{t:.9g},{v:.6g}\n")


def _save_csv_many(path: str, cap: dict) -> None:
    srcs = list(cap["channels"].keys())
    with open(path, "w") as fp:
        fp.write(f"# sources={','.join(srcs)} n={cap['n']} dt={cap['dt']:.6g}s\n")
        fp.write("t_s," + ",".join(srcs) + "\n")
        cols = [cap["channels"][s] for s in srcs]
        for i, t in enumerate(cap["t"]):
            fp.write(f"{t:.9g}," + ",".join(f"{c[i]:.6g}" for c in cols) + "\n")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Siglent SDS8xxX HD SCPI driver / CLI")
    ap.add_argument("--host", default=DEFAULT_HOST, help=f"scope IP (default {DEFAULT_HOST})")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("idn")
    sub.add_parser("state")
    sub.add_parser("run")
    sub.add_parser("stop")
    sub.add_parser("single")
    sub.add_parser("autoset")
    sub.add_parser("reset")
    p_shot = sub.add_parser("screenshot", help="save the display as PNG")
    p_shot.add_argument("path")
    p_raw = sub.add_parser("raw", help="send one raw SCPI line; reads a reply if it ends with '?'")
    p_raw.add_argument("scpi")
    p_cap = sub.add_parser("capture", help="capture one or more channels (-c C1,C2,C3)")
    p_cap.add_argument("-c", "--source", default="C1", help="channel or comma list, e.g. C1 or C1,C2,C3")
    p_cap.add_argument("--points", type=int, default=None)
    p_cap.add_argument("--csv", default=None)
    p_cap.add_argument("--stop", action="store_true", help="freeze acquisition before reading")
    p_meas = sub.add_parser("measure", help="auto-measure a channel")
    p_meas.add_argument("-c", "--source", default="C1")
    p_meas.add_argument("--types", default="PKPK,MEAN,RMS,FREQ,DUTY", help="comma list of measure types")
    p_fft = sub.add_parser("fft", help="capture a channel and print its spectrum peaks")
    p_fft.add_argument("-c", "--source", default="C1")
    p_fft.add_argument("--points", type=int, default=32768)
    p_fft.add_argument("--csv", default=None, help="write full freq,amp spectrum here")
    p_sc = sub.add_parser("set-chan", help="configure a vertical channel")
    p_sc.add_argument("ch", type=int)
    p_sc.add_argument("--on", choices=["on", "off"]); p_sc.add_argument("--scale", type=float)
    p_sc.add_argument("--offset", type=float); p_sc.add_argument("--coupling", choices=["DC", "AC", "GND"])
    p_sc.add_argument("--probe", type=float); p_sc.add_argument("--bwlimit")
    p_sc.add_argument("--unit", choices=["V", "A"]); p_sc.add_argument("--label")
    p_st = sub.add_parser("set-time", help="set timebase")
    p_st.add_argument("--scale", type=float, help="s/div"); p_st.add_argument("--delay", type=float)
    p_tr = sub.add_parser("set-trig", help="set edge trigger")
    p_tr.add_argument("--source"); p_tr.add_argument("--level", type=float)
    p_tr.add_argument("--slope", choices=["RISing", "FALLing"]); p_tr.add_argument("--mode", choices=["AUTO", "NORMal", "SINGle"])
    p_aq = sub.add_parser("set-acq", help="set acquisition")
    p_aq.add_argument("--type", dest="atype", choices=["NORMal", "PEAK", "AVERage", "ERES"])
    p_aq.add_argument("--average", type=int); p_aq.add_argument("--mdepth"); p_aq.add_argument("--interp", choices=["ON", "OFF"])
    args = ap.parse_args(argv)

    with SiglentScope(host=args.host) as sc:
        if args.cmd == "idn":
            print(sc.idn())
        elif args.cmd == "state":
            print(_fmt_state(sc.state()))
        elif args.cmd == "run":
            sc.run(); print("running")
        elif args.cmd == "stop":
            sc.stop(); print("stopped")
        elif args.cmd == "single":
            sc.single(); print("single done:", sc.trigger_status())
        elif args.cmd == "autoset":
            sc.autoset(); print("autoset sent")
        elif args.cmd == "reset":
            sc.reset(); print("*RST sent")
        elif args.cmd == "screenshot":
            n = sc.screenshot(args.path); print(f"wrote {args.path} ({n} bytes)")
        elif args.cmd == "raw":
            if args.scpi.strip().endswith("?"):
                print(sc.query(args.scpi))
            else:
                sc.write(args.scpi); print("(sent)")
        elif args.cmd == "capture":
            srcs = [s.strip() for s in args.source.split(",") if s.strip()]
            if len(srcs) == 1:
                cap = sc.capture(srcs[0], points=args.points, stop_first=args.stop)
                v = cap["v"]; mn, mx = min(v), max(v); avg = sum(v) / len(v)
                print(f"{cap['source']}: {cap['n']} pts  dt={cap['dt']:.4g}s  "
                      f"span={(cap['n']-1)*cap['dt']*1e3:.4f} ms")
                print(f"  Vmin={mn:.4f} Vmax={mx:.4f} Vpp={mx-mn:.4f} Vmean={avg:.4f}")
                if args.csv:
                    _save_csv(args.csv, cap); print(f"  wrote {args.csv}")
            else:
                cap = sc.capture_many(srcs, points=args.points, stop_first=args.stop or True)
                print(f"{','.join(srcs)}: {cap['n']} pts  dt={cap['dt']:.4g}s")
                for s in srcs:
                    v = cap["channels"][s]; mn, mx = min(v), max(v)
                    print(f"  {s}: Vpp={mx-mn:.4f} Vmean={sum(v)/len(v):.4f}")
                if args.csv:
                    _save_csv_many(args.csv, cap); print(f"  wrote {args.csv}")
        elif args.cmd == "measure":
            types = [t.strip() for t in args.types.split(",") if t.strip()]
            res = sc.measure_many(args.source, types)
            for mt in types:
                val = res[mt]
                print(f"  {args.source} {mt:8} = {'n/a' if val is None else f'{val:.6g}'}")
        elif args.cmd == "fft":
            cap = sc.capture(args.source, points=args.points, stop_first=True)
            freqs, amps, dom = fft_mag(cap["v"], cap["dt"])
            print(f"{args.source}: {len(cap['v'])} pts  fs={1/cap['dt']:.4g}Hz  "
                  f"bins={len(freqs)}  dominant={dom:.6g}Hz")
            order = sorted(range(1, len(amps)), key=lambda k: amps[k], reverse=True)[:8]
            for k in sorted(order):
                print(f"  {freqs[k]:12.3f} Hz   amp={amps[k]:.5g}")
            if args.csv:
                with open(args.csv, "w") as fp:
                    fp.write("freq_hz,amp\n")
                    for f_, a_ in zip(freqs, amps):
                        fp.write(f"{f_:.6g},{a_:.6g}\n")
                print(f"  wrote {args.csv}")
        elif args.cmd == "set-chan":
            sc.set_channel(args.ch,
                on=None if args.on is None else args.on == "on",
                scale=args.scale, offset=args.offset, coupling=args.coupling,
                probe=args.probe, bwlimit=args.bwlimit, unit=args.unit,
                label_text=args.label, label=None if args.label is None else True)
            print(f"C{args.ch} configured")
        elif args.cmd == "set-time":
            sc.set_timebase(scale=args.scale, delay=args.delay); print("timebase set")
        elif args.cmd == "set-trig":
            sc.set_trigger_edge(source=args.source, level=args.level,
                                slope=args.slope, mode=args.mode); print("trigger set")
        elif args.cmd == "set-acq":
            sc.set_acquire(atype=args.atype, mdepth=args.mdepth,
                           interp=args.interp, average=args.average); print("acquire set")
    return 0


if __name__ == "__main__":
    sys.exit(main())
