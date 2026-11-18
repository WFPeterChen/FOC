#!/usr/bin/env python3
"""
FalconFOC / DM 电机 CAN 上位机  (SocketCAN + 纯 Tkinter, 零额外依赖)
================================================================================
对标社区工具(ODrive liveplotter / SimpleFOC Studio / moteus tview)的功能集,
但只用 python-can + 内置 tkinter, 不引入 matplotlib/numpy。单文件, 现代深色主题。

功能:
  • 多信号实时示波器   位置/速度/力矩/指令 滚动绘图, 每通道独立自动量程, CSV 录制导出
  • 波形发生器         const/step/square/sine/ramp/chirp 注入 速度/位置/MIT(P/V/T_ff), 调参/激励
  • 频谱 FFT           纯 Python FFT, 看速度纹波是"离散极限环"还是"宽带噪声"(诊断 MIT 抖动)
  • CAN 日志           收发帧十六进制 + 解码, 像 tview 的 console
  • 命令/MIT 阻抗      使能/失能/清错/置零/齿槽标定 + 5 滑块 MIT + 快捷单发
  • CAN 自动拉起       连接时若 can0 未就绪, 用 pkexec 一键 up @1Mbps(图形授权)
  • 反馈请求-应答式    "持续查询@30Hz"零副作用帧保持反馈流(不驱动电机)

用法:
    python3 dm_motor_gui.py     # 点"连接", 接口没起会自动 pkexec 拉起
开机/插入自动拉起(跑一次): sudo ./can-autoup-setup.sh
"""
import struct
import threading
import time
import math
import cmath
import collections
import csv
import json
import os
import re
import shutil
import subprocess
import tkinter as tk
import tkinter.font as tkfont
from tkinter import ttk, filedialog, messagebox

try:
    import can  # python-can
except ImportError:
    can = None

# ---------- DM 协议常量 (对齐 dm_can.h) ----------
MOTOR_ID  = 0x01
MASTER_ID = 0x00
ID_MIT    = MOTOR_ID
ID_POSVEL = 0x100 + MOTOR_ID
ID_VEL    = 0x200 + MOTOR_ID

P_MAX, V_MAX, T_MAX = 50.0, 300.0, 0.3
KP_MIN, KP_MAX = 0.0, 500.0
KD_MIN, KD_MAX = 0.0, 5.0

CMD_ENABLE, CMD_DISABLE, CMD_SET_ZERO, CMD_CLEAR_ERR, CMD_COGGING_CAL = 0xFC, 0xFD, 0xFE, 0xFB, 0xFA
CMD_QUERY = 0x00  # 未定义命令字: 固件 default 什么都不做, 但仍回一帧反馈 → 零副作用查询
MIT_KD_SAFE = 0.016  # 4010 实测 MIT Kd 稳定上限 N·m/(rad/s) (input_kd≈0.1, 超过 ~200Hz 极限环自激)
ERR_TEXT = {0: "停机", 1: "运行", 8: "过压", 9: "欠压", 0xA: "过流", 0xB: "过温", 0xE: "超速"}

# ---------- 配色 (现代深色) ----------
C = dict(
    bg="#161a20", panel="#1e242c", panel2="#2a313b", edge="#333c47",
    fg="#e6e9ee", muted="#8a93a0",
    accent="#4c8dff", accent_hi="#6ba2ff", accent_fg="#0b1016",
    ok="#34d399", danger="#f0616d", danger_hi="#ff7884", warn="#f5b54a",
    scope_bg="#10141a", grid="#1b2530",
)
CH_COL = {"vel": "#34d399", "torque": "#f0616d", "pos": "#4c8dff", "cmd": "#f5b54a"}


def setup_theme(root):
    base = "Noto Sans CJK SC"
    mono = "Noto Sans Mono CJK SC"
    fams = set(tkfont.families(root))
    if base not in fams:
        base = tkfont.nametofont("TkDefaultFont").actual("family")
    if mono not in fams:
        mono = "monospace"
    for nm in ("TkDefaultFont", "TkTextFont", "TkMenuFont", "TkHeadingFont"):
        try: tkfont.nametofont(nm).configure(family=base, size=10)
        except tk.TclError: pass

    st = ttk.Style(root)
    st.theme_use("clam")
    root.configure(bg=C["bg"])
    # 下拉列表(经典 tk listbox)配色
    root.option_add("*TCombobox*Listbox.background", C["panel2"])
    root.option_add("*TCombobox*Listbox.foreground", C["fg"])
    root.option_add("*TCombobox*Listbox.selectBackground", C["accent"])
    root.option_add("*TCombobox*Listbox.selectForeground", C["accent_fg"])

    st.configure(".", background=C["bg"], foreground=C["fg"], font=(base, 10),
                 bordercolor=C["edge"], focuscolor=C["bg"])
    st.configure("TFrame", background=C["bg"])
    st.configure("Card.TFrame", background=C["panel"])
    st.configure("TLabel", background=C["bg"], foreground=C["fg"])
    st.configure("Muted.TLabel", background=C["bg"], foreground=C["muted"])
    st.configure("Cap.TLabel", background=C["panel"], foreground=C["muted"], font=(base, 8))
    st.configure("Value.TLabel", background=C["panel"], foreground=C["fg"], font=(base, 16, "bold"))
    st.configure("TLabelframe", background=C["bg"], bordercolor=C["edge"], relief="solid", borderwidth=1)
    st.configure("TLabelframe.Label", background=C["bg"], foreground=C["accent"], font=(base, 9, "bold"))

    st.configure("TButton", background=C["panel2"], foreground=C["fg"], borderwidth=0, padding=(11, 6))
    st.map("TButton", background=[("pressed", C["edge"]), ("active", C["edge"])])
    st.configure("Accent.TButton", background=C["accent"], foreground=C["accent_fg"], font=(base, 10, "bold"))
    st.map("Accent.TButton", background=[("active", C["accent_hi"]), ("pressed", C["accent_hi"])])
    st.configure("Danger.TButton", background=C["danger"], foreground="#fff", font=(base, 10, "bold"))
    st.map("Danger.TButton", background=[("active", C["danger_hi"]), ("pressed", C["danger_hi"])])

    st.configure("TCheckbutton", background=C["bg"], foreground=C["fg"], focuscolor=C["bg"])
    st.map("TCheckbutton", background=[("active", C["bg"])], indicatorcolor=[("selected", C["accent"])])
    st.configure("TEntry", fieldbackground=C["panel2"], foreground=C["fg"], bordercolor=C["edge"],
                 insertcolor=C["fg"], padding=4)
    st.configure("TSpinbox", fieldbackground=C["panel2"], foreground=C["fg"], arrowcolor=C["fg"],
                 bordercolor=C["edge"], padding=3)
    st.configure("TCombobox", fieldbackground=C["panel2"], background=C["panel2"], foreground=C["fg"],
                 arrowcolor=C["fg"], bordercolor=C["edge"], padding=3)
    st.map("TCombobox", fieldbackground=[("readonly", C["panel2"])], foreground=[("readonly", C["fg"])])
    st.configure("Horizontal.TScale", background=C["bg"], troughcolor=C["panel2"], bordercolor=C["bg"])
    st.configure("TNotebook", background=C["bg"], borderwidth=0, tabmargins=(2, 6, 2, 0))
    st.configure("TNotebook.Tab", background=C["panel"], foreground=C["muted"], padding=(16, 7), borderwidth=0)
    st.map("TNotebook.Tab", background=[("selected", C["bg"])], foreground=[("selected", C["accent"])],
           expand=[("selected", (1, 1, 1, 0))])
    return base, mono


# ---------- 协议打包/解包 ----------
def f2u(x, xmin, xmax, bits):
    span = xmax - xmin
    x = max(xmin, min(xmax, x))
    return int((x - xmin) * ((1 << bits) - 1) / span)


def u2f(xi, xmin, xmax, bits):
    return xi * (xmax - xmin) / ((1 << bits) - 1) + xmin


def pack_mit(p, v, kp, kd, t):
    pi = f2u(p, -P_MAX, P_MAX, 16); vi = f2u(v, -V_MAX, V_MAX, 12)
    ki = f2u(kp, KP_MIN, KP_MAX, 12); di = f2u(kd, KD_MIN, KD_MAX, 12)
    ti = f2u(t, -T_MAX, T_MAX, 12)
    return bytes([(pi >> 8) & 0xFF, pi & 0xFF, (vi >> 4) & 0xFF,
                  ((vi & 0xF) << 4) | (ki >> 8), ki & 0xFF, (di >> 4) & 0xFF,
                  ((di & 0xF) << 4) | (ti >> 8), ti & 0xFF])


def decode_feedback(data):
    err = data[0] >> 4
    pi = (data[1] << 8) | data[2]
    vi = (data[3] << 4) | (data[4] >> 4)
    ti = ((data[4] & 0xF) << 8) | data[5]
    return {"err": err, "pos": u2f(pi, -P_MAX, P_MAX, 16),
            "vel": u2f(vi, -V_MAX, V_MAX, 12), "torque": u2f(ti, -T_MAX, T_MAX, 12),
            "temp": data[6]}


# ---------- 纯 Python FFT (仅诊断用) ----------
def _fft(a):
    n = len(a)
    if n <= 1:
        return a
    a = list(a); j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit; bit >>= 1
        j ^= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    while length <= n:
        wl = cmath.exp(-2j * math.pi / length)
        for i in range(0, n, length):
            w = 1 + 0j
            for k in range(i, i + length // 2):
                u = a[k]; v = a[k + length // 2] * w
                a[k] = u + v; a[k + length // 2] = u - v
                w *= wl
        length <<= 1
    return a


def spectrum(samples, dt):
    n = len(samples)
    if n < 8 or dt <= 0:
        return [], []
    p = 1
    while p * 2 <= n:
        p *= 2
    s = samples[-p:]
    mean = sum(s) / p
    win = [(s[i] - mean) * (0.5 - 0.5 * math.cos(2 * math.pi * i / (p - 1))) for i in range(p)]
    X = _fft([complex(x) for x in win])
    half = p // 2; fs = 1.0 / dt
    return [i * fs / p for i in range(half)], [abs(X[i]) * 2.0 / p for i in range(half)]


def resample_uniform(pts, n=512):
    if len(pts) < 8:
        return None, 0.0
    t0, t1 = pts[0][0], pts[-1][0]
    if t1 - t0 < 1e-3:
        return None, 0.0
    dt = (t1 - t0) / (n - 1)
    out = []; j = 0
    for i in range(n):
        t = t0 + i * dt
        while j < len(pts) - 2 and pts[j + 1][0] < t:
            j += 1
        ta, va = pts[j]; tb, vb = pts[j + 1]
        out.append(va if tb == ta else va + (vb - va) * (t - ta) / (tb - ta))
    return out, dt


# ============================================================
# Canvas 实时示波器
# ============================================================
class Scope(ttk.Frame):
    CHANS = [("vel", "速度 rad/s"), ("torque", "力矩 N·m"), ("pos", "位置 rad"), ("cmd", "指令")]
    ML, MR, MT, MB = 8, 8, 8, 16

    def __init__(self, master, maxlen=20000):
        super().__init__(master)
        self.window_s = tk.DoubleVar(value=6.0)
        self.paused = tk.BooleanVar(value=False)
        self.hist = {k: collections.deque(maxlen=maxlen) for k, _ in self.CHANS}
        self.show = {k: tk.BooleanVar(value=(k != "cmd")) for k, _ in self.CHANS}
        self._build()

    def _build(self):
        ctl = ttk.Frame(self); ctl.pack(fill="x", pady=(0, 4))
        for k, name in self.CHANS:
            tk.Checkbutton(ctl, text="● " + name, variable=self.show[k], fg=CH_COL[k],
                           bg=C["bg"], activebackground=C["bg"], activeforeground=CH_COL[k],
                           selectcolor=C["panel2"], borderwidth=0, highlightthickness=0,
                           font=("Noto Sans CJK SC", 9)).pack(side="left", padx=3)
        ttk.Label(ctl, text="窗(s)", style="Muted.TLabel").pack(side="left", padx=(10, 2))
        ttk.Spinbox(ctl, from_=1, to=60, increment=1, width=4, textvariable=self.window_s).pack(side="left")
        ttk.Checkbutton(ctl, text="暂停", variable=self.paused).pack(side="left", padx=8)
        ttk.Button(ctl, text="清空", command=self.clear).pack(side="left")
        self.canvas = tk.Canvas(self, height=260, bg=C["scope_bg"], highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)

    def push(self, key, t, val):
        if not self.paused.get():
            self.hist[key].append((t, val))

    def clear(self):
        for d in self.hist.values():
            d.clear()

    @staticmethod
    def _range(pts):
        lo = min(v for _, v in pts); hi = max(v for _, v in pts)
        if hi - lo < 1e-6:
            lo, hi = lo - 0.5, hi + 0.5
        pad = (hi - lo) * 0.1
        return lo - pad, hi + pad

    def redraw(self):
        c = self.canvas; c.delete("all")
        W = c.winfo_width(); H = c.winfo_height()
        if W < 20 or H < 20:
            return
        x0, x1, y0, y1 = self.ML, W - self.MR, self.MT, H - self.MB
        for i in range(5):
            y = y0 + (y1 - y0) * i / 4
            c.create_line(x0, y, x1, y, fill=C["grid"])
        win = max(0.5, self.window_s.get()); t_lo = time.time() - win
        for i in range(7):
            x = x0 + (x1 - x0) * i / 6
            c.create_line(x, y0, x, y1, fill=C["grid"])
            c.create_text(x, y1 + 7, text=f"-{win*(1-i/6):.0f}s", fill=C["muted"], font=("Noto Sans CJK SC", 7))
        ly = y0 + 9
        for k, name in self.CHANS:
            if not self.show[k].get():
                continue
            pts = [(t, v) for t, v in list(self.hist[k]) if t >= t_lo]
            if len(pts) < 2:
                continue
            col = CH_COL[k]
            lo, hi = self._range(pts); span = hi - lo
            if lo < 0 < hi:
                yz = y1 - (0 - lo) / span * (y1 - y0)
                c.create_line(x0, yz, x1, yz, fill=col, stipple="gray25")
            coords = []
            for t, v in pts:
                coords += [x0 + (t - t_lo) / win * (x1 - x0), y1 - (v - lo) / span * (y1 - y0)]
            c.create_line(*coords, fill=col, width=2)
            c.create_text(x0 + 8, ly, anchor="w", fill=col, font=("Noto Sans CJK SC", 9, "bold"),
                          text=f"{name}: {pts[-1][1]:+.3f}  [{lo:+.2f},{hi:+.2f}]")
            ly += 15


class WaveGen:
    SHAPES = ["const", "step", "square", "sine", "ramp", "chirp"]

    def __init__(self):
        self.shape = "sine"; self.amp = 2.0; self.off = 0.0
        self.freq = 0.5; self.f1 = 5.0; self.dur = 10.0; self.t0 = None

    def start(self):
        self.t0 = time.time()

    def value(self):
        if self.t0 is None:
            return self.off
        t = time.time() - self.t0; s = self.shape
        if s == "const":
            return self.off
        if s == "step":
            return self.off + (self.amp if t >= 0.2 else 0.0)
        if s == "square":
            return self.off + (self.amp if (t * self.freq) % 1.0 < 0.5 else -self.amp)
        if s == "sine":
            return self.off + self.amp * math.sin(2 * math.pi * self.freq * t)
        if s == "ramp":
            return self.off + self.amp * ((t * self.freq) % 1.0)
        if s == "chirp":
            k = (self.f1 - self.freq) / max(self.dur, 1e-3)
            return self.off + self.amp * math.sin(2 * math.pi * (self.freq * t + 0.5 * k * t * t))
        return self.off


class App:
    def __init__(self, root):
        self.root = root
        self.base, self.mono = setup_theme(root)
        self.bus = None; self.rx_thread = None; self.running = False
        self.fb = {"err": 0, "pos": 0, "vel": 0, "torque": 0, "temp": 0, "t": 0}
        self._rate_t = time.time(); self._rate_n = 0; self.rate_hz = 0.0
        self.cont_mit = tk.BooleanVar(value=False)
        self.poll_fb = tk.BooleanVar(value=True)
        self.log = collections.deque(maxlen=500)
        self.rec = None; self.rec_rows = collections.deque()
        self.gen = WaveGen(); self.gen_on = False
        self._last = {"mit": 0.0, "poll": 0.0, "gen": 0.0}
        root.title("FalconFOC · DM 电机 CAN 上位机")
        root.geometry("940x820")
        self._build()
        self._send_tick(); self._ui_tick()

    # ---------------- UI ----------------
    def _build(self):
        pad = dict(padx=8, pady=5)
        # 顶部连接 + 命令栏
        top = ttk.Frame(self.root); top.pack(fill="x", **pad)
        ttk.Label(top, text="CAN", style="Muted.TLabel").pack(side="left", padx=(2, 4))
        self.ch = ttk.Entry(top, width=7); self.ch.insert(0, "can0"); self.ch.pack(side="left")
        self.btn_conn = ttk.Button(top, text="连接", style="Accent.TButton", command=self.toggle_conn)
        self.btn_conn.pack(side="left", padx=6)
        ttk.Button(top, text="拉起CAN", command=self.bring_up_clicked).pack(side="left")
        self.lbl_conn = ttk.Label(top, text="● 未连接", foreground=C["danger"], style="Muted.TLabel")
        self.lbl_conn.pack(side="left", padx=8)
        ttk.Checkbutton(top, text="持续查询@30Hz", variable=self.poll_fb).pack(side="left", padx=8)
        ttk.Frame(top, width=14).pack(side="left")
        ttk.Button(top, text="使能", style="Accent.TButton", command=lambda: self.send_special(CMD_ENABLE)).pack(side="left", padx=2)
        ttk.Button(top, text="失能", style="Danger.TButton", command=lambda: self.send_special(CMD_DISABLE)).pack(side="left", padx=2)
        for txt, cmd in [("清错", CMD_CLEAR_ERR), ("置零", CMD_SET_ZERO), ("齿槽标定", CMD_COGGING_CAL)]:
            ttk.Button(top, text=txt, command=lambda c=cmd: self.send_special(c)).pack(side="left", padx=2)

        # 反馈卡片
        fb = ttk.Frame(self.root); fb.pack(fill="x", **pad)
        self.fb_lbl = {}
        for k, t, unit in [("state", "状态", ""), ("pos", "位置", "rad"), ("vel", "速度", "rad/s"),
                           ("torque", "力矩", "N·m"), ("temp", "温度", "℃"), ("rate", "帧率", "Hz")]:
            card = ttk.Frame(fb, style="Card.TFrame")
            card.pack(side="left", expand=True, fill="x", padx=4, ipadx=6, ipady=6)
            ttk.Label(card, text=t + ("  " + unit if unit else ""), style="Cap.TLabel").pack(anchor="w", padx=8)
            v = ttk.Label(card, text="—", style="Value.TLabel"); v.pack(anchor="w", padx=8)
            self.fb_lbl[k] = v

        nb = ttk.Notebook(self.root); nb.pack(fill="both", expand=True, **pad)
        self._tab_control(nb); self._tab_gen(nb); self._tab_fft(nb); self._tab_log(nb)

    def _tab_control(self, nb):
        tab = ttk.Frame(nb); nb.add(tab, text="  控制 + 示波器  ")
        mit = ttk.LabelFrame(tab, text="MIT 阻抗   tau = Kp·(P-p) + Kd·(V-v) + T_ff")
        mit.pack(fill="x", padx=8, pady=6)
        self.sld = {}

        def _fmt(name, lbl, var):
            val = var.get()
            if name == "Kd":  # 本 4010 实测稳定上限 ~0.016 N·m/(rad/s), 超过会 ~200Hz 极限环振
                lbl.config(text=f"{val:.3f}", foreground=(C["danger"] if val > MIT_KD_SAFE else C["accent"]))
            else:
                lbl.config(text=f"{val:.2f}", foreground=C["accent"])
        # Kd 滑块限到 0~0.1(留点越界空间能看到振), 默认 0.01 安全区; 完整 DM 量程(0~5)请走自己的 host 并按 Kt 缩放
        for name, lo, hi, init in [("P (rad)", -P_MAX, P_MAX, 0), ("V (rad/s)", -20, 20, 0),
                                   ("Kp", 0, 50, 0), ("Kd", 0, 0.1, 0.01), ("T_ff (N·m)", -T_MAX, T_MAX, 0)]:
            row = ttk.Frame(mit); row.pack(fill="x", pady=1)
            ttk.Label(row, text=name, width=11).pack(side="left", padx=(6, 0))
            var = tk.DoubleVar(value=init)
            ttk.Scale(row, from_=lo, to=hi, variable=var, length=420).pack(side="left", padx=6)
            lbl = ttk.Label(row, width=8, text=f"{init:.2f}", foreground=C["accent"]); lbl.pack(side="left")
            var.trace_add("write", lambda *_, n=name, v=var, l=lbl: _fmt(n, l, v))
            self.sld[name] = var
        ttk.Label(mit, text=f"⚠ 本电机 Kd 稳定上限≈{MIT_KD_SAFE} N·m/(rad/s)(红字即超), 超过会 ~200Hz 极限环振; "
                            "阻抗手感主要靠 Kp/T_ff。", style="Muted.TLabel").pack(anchor="w", padx=6)
        brow = ttk.Frame(mit); brow.pack(fill="x", pady=6)
        ttk.Button(brow, text="发送一次", style="Accent.TButton", command=self.send_mit).pack(side="left", padx=6)
        ttk.Checkbutton(brow, text="连续发送 @50Hz", variable=self.cont_mit).pack(side="left", padx=4)
        ttk.Button(brow, text="归零滑块", command=self.zero_sliders).pack(side="left", padx=4)
        ttk.Button(brow, text="存预设", command=self.save_preset).pack(side="left", padx=4)
        ttk.Button(brow, text="读预设", command=self.load_preset).pack(side="left", padx=4)

        q = ttk.LabelFrame(tab, text="快捷单发"); q.pack(fill="x", padx=8, pady=6)
        inner = ttk.Frame(q); inner.pack(fill="x", pady=4)
        ttk.Label(inner, text="速度 rad/s").pack(side="left", padx=(6, 4))
        self.qv = ttk.Entry(inner, width=7); self.qv.insert(0, "2.0"); self.qv.pack(side="left")
        ttk.Button(inner, text="发速度", command=self.send_vel).pack(side="left", padx=6)
        ttk.Label(inner, text="位置 rad").pack(side="left", padx=(12, 4))
        self.qp = ttk.Entry(inner, width=7); self.qp.insert(0, "0.0"); self.qp.pack(side="left")
        ttk.Label(inner, text="限速").pack(side="left", padx=(8, 2))
        self.qpv = ttk.Entry(inner, width=6); self.qpv.insert(0, "5.0"); self.qpv.pack(side="left")
        ttk.Button(inner, text="发位置-速度", command=self.send_posvel).pack(side="left", padx=6)

        sc = ttk.LabelFrame(tab, text="实时示波器"); sc.pack(fill="both", expand=True, padx=8, pady=6)
        self.scope = Scope(sc); self.scope.pack(fill="both", expand=True, padx=6, pady=4)
        rec = ttk.Frame(sc); rec.pack(fill="x", padx=6, pady=(0, 4))
        self.btn_rec = ttk.Button(rec, text="● 录制 CSV", command=self.toggle_record); self.btn_rec.pack(side="left")
        self.lbl_rec = ttk.Label(rec, text="", style="Muted.TLabel"); self.lbl_rec.pack(side="left", padx=8)

    def _tab_gen(self, nb):
        tab = ttk.Frame(nb); nb.add(tab, text="  波形发生器  ")
        info = ("注入测试波形做阶跃/扫频/激励。MIT 可设 Kp/Kd 看不同刚度的响应 —— 诊断「MIT 抖动」用:\n"
                "软阻抗(Kp=0,小Kd)易在齿槽 detent 极限环抖动; 加大 Kp 或给 T_ff 顶过 detent 通常变稳。\n"
                "配合「频谱」标签看抖动是离散音(极限环)还是宽带噪声(Kd 灌噪声)。")
        ttk.Label(tab, text=info, style="Muted.TLabel", justify="left").pack(anchor="w", padx=10, pady=8)
        f = ttk.LabelFrame(tab, text="参数"); f.pack(fill="x", padx=8, pady=6)
        self.gv = {}
        r1 = ttk.Frame(f); r1.pack(fill="x", pady=4)
        ttk.Label(r1, text="目标", width=6).pack(side="left", padx=(6, 0))
        self.gv["target"] = tk.StringVar(value="MIT V (rad/s)")
        ttk.Combobox(r1, textvariable=self.gv["target"], width=16, state="readonly",
                     values=["速度帧 (rad/s)", "位置帧 P (rad)", "MIT P (rad)",
                             "MIT V (rad/s)", "MIT T_ff (N·m)"]).pack(side="left", padx=4)
        ttk.Label(r1, text="波形").pack(side="left", padx=(14, 2))
        self.gv["shape"] = tk.StringVar(value="sine")
        ttk.Combobox(r1, textvariable=self.gv["shape"], width=9, state="readonly",
                     values=WaveGen.SHAPES).pack(side="left", padx=4)
        r2 = ttk.Frame(f); r2.pack(fill="x", pady=4)
        for name, key, init in [("幅值", "amp", "2.0"), ("偏置", "off", "0.0"), ("频率Hz", "freq", "0.5"),
                                ("终频Hz", "f1", "5.0"), ("时长s", "dur", "10")]:
            ttk.Label(r2, text=name).pack(side="left", padx=(10, 2))
            e = ttk.Entry(r2, width=6); e.insert(0, init); e.pack(side="left"); self.gv[key] = e
        r3 = ttk.Frame(f); r3.pack(fill="x", pady=4)
        for name, key, init in [("MIT Kp", "kp", "0"), ("MIT Kd", "kd", "0.01"), ("发送率Hz", "rate", "100")]:
            ttk.Label(r3, text=name).pack(side="left", padx=(10, 2))
            e = ttk.Entry(r3, width=6); e.insert(0, init); e.pack(side="left"); self.gv[key] = e
        br = ttk.Frame(tab); br.pack(fill="x", padx=8, pady=8)
        self.btn_gen = ttk.Button(br, text="▶ 启动", style="Accent.TButton", command=self.toggle_gen)
        self.btn_gen.pack(side="left", padx=4)
        self.lbl_gen = ttk.Label(br, text="● 停止", style="Muted.TLabel"); self.lbl_gen.pack(side="left", padx=8)

    def _tab_fft(self, nb):
        tab = ttk.Frame(nb); nb.add(tab, text="  频谱 FFT  ")
        ctl = ttk.Frame(tab); ctl.pack(fill="x", padx=8, pady=6)
        ttk.Label(ctl, text="信号").pack(side="left", padx=(2, 4))
        self.fft_sig = tk.StringVar(value="vel")
        ttk.Combobox(ctl, textvariable=self.fft_sig, width=8, state="readonly",
                     values=["vel", "torque", "pos"]).pack(side="left", padx=4)
        self.fft_live = tk.BooleanVar(value=True)
        ttk.Checkbutton(ctl, text="实时", variable=self.fft_live).pack(side="left", padx=10)
        ttk.Button(ctl, text="单次刷新", command=self.draw_fft).pack(side="left")
        self.fft_info = ttk.Label(ctl, text="", style="Muted.TLabel"); self.fft_info.pack(side="left", padx=12)
        self.fft_canvas = tk.Canvas(tab, bg=C["scope_bg"], highlightthickness=0)
        self.fft_canvas.pack(fill="both", expand=True, padx=8, pady=6)

    def _tab_log(self, nb):
        tab = ttk.Frame(nb); nb.add(tab, text="  CAN 日志  ")
        bar = ttk.Frame(tab); bar.pack(fill="x", padx=8, pady=6)
        self.log_on = tk.BooleanVar(value=True)
        ttk.Checkbutton(bar, text="记录收发帧", variable=self.log_on).pack(side="left")
        ttk.Button(bar, text="清空", command=lambda: (self.log.clear(), self.txt.delete("1.0", "end"))).pack(side="left", padx=8)
        self.txt = tk.Text(tab, height=20, bg=C["scope_bg"], fg="#aeb6c2", insertbackground=C["fg"],
                           font=(self.mono, 9), borderwidth=0, highlightthickness=0)
        self.txt.pack(fill="both", expand=True, padx=8, pady=6)

    # ---------------- CAN 拉起 ----------------
    def _iface_up(self, ch):
        try:
            r = subprocess.run(["ip", "-br", "link", "show", ch], capture_output=True, text=True, timeout=3)
            toks = r.stdout.split()
            return len(toks) >= 2 and toks[1].upper().startswith("UP")
        except Exception:
            return False

    def _bring_up_can(self, ch, bitrate=1000000):
        if not re.fullmatch(r"[a-zA-Z0-9]+", ch):
            return False, "接口名非法"
        runner = "pkexec" if shutil.which("pkexec") else "sudo"
        script = f"ip link set {ch} down 2>/dev/null; ip link set {ch} up type can bitrate {bitrate}"
        try:
            r = subprocess.run([runner, "sh", "-c", script], capture_output=True, text=True, timeout=60)
            if r.returncode == 0:
                return True, ""
            return False, (r.stderr.strip() or f"退出码 {r.returncode}")
        except Exception as e:
            return False, str(e)

    def bring_up_clicked(self):
        ch = self.ch.get().strip()
        self.lbl_conn.config(text="● 正在拉起…", foreground=C["warn"]); self.root.update()
        ok, err = self._bring_up_can(ch)
        if ok:
            self.lbl_conn.config(text=f"● {ch} 已就绪", foreground=C["ok"])
        else:
            self.lbl_conn.config(text=f"● 拉起失败: {err[:36]}", foreground=C["danger"])

    # ---------------- 连接 ----------------
    def toggle_conn(self):
        if self.bus is None:
            if can is None:
                self.lbl_conn.config(text="● 缺 python-can", foreground=C["danger"]); return
            ch = self.ch.get().strip()
            if not self._iface_up(ch):
                self.lbl_conn.config(text=f"● {ch} 未就绪, 拉起中…", foreground=C["warn"]); self.root.update()
                ok, err = self._bring_up_can(ch)
                if not ok:
                    self.lbl_conn.config(text=f"● 拉起失败: {err[:30]}", foreground=C["danger"]); return
            try:
                self.bus = can.interface.Bus(channel=ch, interface="socketcan")
            except Exception as e:
                self.lbl_conn.config(text=f"● 打开失败: {e}", foreground=C["danger"]); return
            self.running = True
            self.rx_thread = threading.Thread(target=self._rx_loop, daemon=True); self.rx_thread.start()
            self.lbl_conn.config(text="● 已连接", foreground=C["ok"]); self.btn_conn.config(text="断开")
        else:
            self.running = False
            try: self.bus.shutdown()
            except Exception: pass
            self.bus = None
            self.lbl_conn.config(text="● 未连接", foreground=C["danger"]); self.btn_conn.config(text="连接")

    def _logframe(self, tag, arb, data):
        if self.log_on.get():
            self.log.append(f"{time.strftime('%H:%M:%S')} {tag} {arb:03X}  {data.hex()}")

    def _send(self, arb_id, data):
        if self.bus is None:
            return
        try:
            self.bus.send(can.Message(arbitration_id=arb_id, data=data, is_extended_id=False))
            self._logframe("TX→", arb_id, bytes(data))
        except Exception as e:
            self.lbl_conn.config(text=f"● 发送失败: {e}", foreground=C["danger"])

    def _rx_loop(self):
        while self.running and self.bus is not None:
            try:
                msg = self.bus.recv(timeout=0.2)
            except Exception:
                break
            if msg is None:
                continue
            self._logframe("RX←", msg.arbitration_id, bytes(msg.data))
            if msg.arbitration_id == MASTER_ID and len(msg.data) >= 7:
                now = time.time()
                d = decode_feedback(msg.data); d["t"] = now
                self.fb = d; self._rate_n += 1
                self.scope.push("pos", now, d["pos"])
                self.scope.push("vel", now, d["vel"])
                self.scope.push("torque", now, d["torque"])
                if self.rec is not None:
                    self.rec_rows.append((now, d["pos"], d["vel"], d["torque"], d["temp"], d["err"]))

    # ---------------- 命令 ----------------
    def send_special(self, cmd):
        self._send(ID_MIT, bytes([0xFF] * 7 + [cmd]))

    def send_query(self):
        self._send(ID_MIT, bytes([0xFF] * 7 + [CMD_QUERY]))

    def send_mit(self):
        s = self.sld
        self._send(ID_MIT, pack_mit(s["P (rad)"].get(), s["V (rad/s)"].get(),
                                    s["Kp"].get(), s["Kd"].get(), s["T_ff (N·m)"].get()))
        self.scope.push("cmd", time.time(), s["V (rad/s)"].get())

    def send_vel(self):
        try: v = float(self.qv.get())
        except ValueError: return
        self._send(ID_VEL, struct.pack("<f", v)); self.scope.push("cmd", time.time(), v)

    def send_posvel(self):
        try: p, v = float(self.qp.get()), float(self.qpv.get())
        except ValueError: return
        self._send(ID_POSVEL, struct.pack("<ff", p, v)); self.scope.push("cmd", time.time(), p)

    def zero_sliders(self):
        for n, var in self.sld.items():
            var.set(0.01 if n == "Kd" else 0.0)

    def save_preset(self):
        fn = filedialog.asksaveasfilename(defaultextension=".json", title="存 MIT 预设")
        if fn:
            json.dump({n: v.get() for n, v in self.sld.items()}, open(fn, "w"))

    def load_preset(self):
        fn = filedialog.askopenfilename(filetypes=[("JSON", "*.json")], title="读 MIT 预设")
        if fn:
            try:
                for n, v in json.load(open(fn)).items():
                    if n in self.sld:
                        self.sld[n].set(v)
            except Exception as e:
                messagebox.showerror("读预设失败", str(e))

    # ---------------- 录制 ----------------
    def toggle_record(self):
        if self.rec is None:
            fn = filedialog.asksaveasfilename(defaultextension=".csv", title="录制反馈",
                                              initialfile=time.strftime("foc_%Y%m%d_%H%M%S.csv"))
            if not fn:
                return
            f = open(fn, "w", newline=""); w = csv.writer(f)
            w.writerow(["t", "pos_rad", "vel_rad_s", "torque_Nm", "temp_C", "err"])
            self.rec = (w, f); self.rec_rows.clear()
            self.btn_rec.config(text="■ 停止录制"); self.lbl_rec.config(text=os.path.basename(fn))
        else:
            w, f = self.rec
            while self.rec_rows:
                w.writerow(self.rec_rows.popleft())
            f.close(); self.rec = None
            self.btn_rec.config(text="● 录制 CSV"); self.lbl_rec.config(text="已保存")

    # ---------------- 波形发生器 ----------------
    def toggle_gen(self):
        if not self.gen_on:
            try:
                self.gen.shape = self.gv["shape"].get()
                self.gen.amp = float(self.gv["amp"].get()); self.gen.off = float(self.gv["off"].get())
                self.gen.freq = float(self.gv["freq"].get()); self.gen.f1 = float(self.gv["f1"].get())
                self.gen.dur = float(self.gv["dur"].get())
            except ValueError:
                messagebox.showerror("参数错误", "幅值/偏置/频率需为数字"); return
            self.gen.start(); self.gen_on = True
            self.btn_gen.config(text="■ 停止"); self.lbl_gen.config(text="● 运行中", foreground=C["ok"])
        else:
            self.gen_on = False
            self.btn_gen.config(text="▶ 启动"); self.lbl_gen.config(text="● 停止", foreground=C["muted"])

    def _gen_send(self):
        val = self.gen.value(); tgt = self.gv["target"].get()
        if tgt.startswith("速度帧"):
            self._send(ID_VEL, struct.pack("<f", val))
        elif tgt.startswith("位置帧"):
            self._send(ID_POSVEL, struct.pack("<ff", val, 10.0))
        else:
            try: kp, kd = float(self.gv["kp"].get()), float(self.gv["kd"].get())
            except ValueError: kp, kd = 0.0, 0.3
            p = v = t = 0.0
            if "P" in tgt: p = val
            elif "V" in tgt: v = val
            else: t = val
            self._send(ID_MIT, pack_mit(p, v, kp, kd, t))
        self.scope.push("cmd", time.time(), val)

    # ---------------- 调度 ----------------
    def _send_tick(self):
        now = time.time()
        if self.bus is not None:
            if self.gen_on:
                try: rate = max(1.0, float(self.gv["rate"].get()))
                except ValueError: rate = 100.0
                if now - self._last["gen"] >= 1.0 / rate:
                    self._gen_send(); self._last["gen"] = now
            elif self.cont_mit.get():
                if now - self._last["mit"] >= 0.02:
                    self.send_mit(); self._last["mit"] = now
            elif self.poll_fb.get():
                if now - self._last["poll"] >= 0.033:
                    self.send_query(); self._last["poll"] = now
        self.root.after(5, self._send_tick)

    def _ui_tick(self):
        now = time.time()
        if now - self._rate_t >= 1.0:
            self.rate_hz = self._rate_n / (now - self._rate_t)
            self._rate_n = 0; self._rate_t = now
        d = self.fb; fresh = (now - d.get("t", 0)) < 0.5
        st = ERR_TEXT.get(d["err"], f"?{d['err']}") if fresh else "—"
        self.fb_lbl["state"].config(text=st, foreground=(C["ok"] if (fresh and d["err"] in (0, 1)) else
                                                         C["danger"] if fresh else C["muted"]))
        self.fb_lbl["pos"].config(text=f"{d['pos']:+.3f}" if fresh else "—")
        self.fb_lbl["vel"].config(text=f"{d['vel']:+.2f}" if fresh else "—")
        self.fb_lbl["torque"].config(text=f"{d['torque']:+.3f}" if fresh else "—")
        self.fb_lbl["temp"].config(text=f"{d['temp']}" if fresh else "—")
        self.fb_lbl["rate"].config(text=f"{self.rate_hz:.0f}" if fresh else "无",
                                   foreground=(C["ok"] if fresh else C["muted"]))
        self.scope.redraw()
        if self.rec is not None and self.rec_rows:
            w, _ = self.rec
            while self.rec_rows:
                w.writerow(self.rec_rows.popleft())
        if self.log:
            self.txt.delete("1.0", "end"); self.txt.insert("end", "\n".join(self.log)); self.txt.see("end")
        if self.fft_live.get():
            self.draw_fft()
        self.root.after(50, self._ui_tick)

    def draw_fft(self):
        c = self.fft_canvas; c.delete("all")
        W = c.winfo_width(); H = c.winfo_height()
        if W < 20 or H < 20:
            return
        vals, dt = resample_uniform(list(self.scope.hist[self.fft_sig.get()]), 512)
        if vals is None:
            c.create_text(W / 2, H / 2, text="数据不足(需先有反馈流)", fill=C["muted"]); return
        freqs, mags = spectrum(vals, dt)
        if not freqs:
            c.create_text(W / 2, H / 2, text="数据不足", fill=C["muted"]); return
        x0, x1, y0, y1 = 44, W - 12, 12, H - 22
        fmax = freqs[-1]; mmax = max(mags) or 1e-9
        for i in range(5):
            y = y0 + (y1 - y0) * i / 4
            c.create_line(x0, y, x1, y, fill=C["grid"])
        coords = []
        for f, m in zip(freqs, mags):
            coords += [x0 + f / fmax * (x1 - x0), y1 - m / mmax * (y1 - y0)]
        if len(coords) >= 4:
            c.create_line(*coords, fill=C["ok"], width=2)
        for i in range(6):
            x = x0 + (x1 - x0) * i / 5
            c.create_text(x, y1 + 10, text=f"{fmax*i/5:.0f}Hz", fill=C["muted"], font=(self.base, 7))
        pk = max(range(len(mags)), key=lambda i: mags[i])
        xpk = x0 + freqs[pk] / fmax * (x1 - x0)
        c.create_line(xpk, y0, xpk, y1, fill=C["warn"], dash=(3, 3))
        self.fft_info.config(text=f"主峰 {freqs[pk]:.1f} Hz   采样≈{1/dt:.0f} Hz   Nyq {fmax:.0f} Hz")
        c.create_text(min(xpk + 6, x1 - 50), y0 + 10, text=f"{freqs[pk]:.1f}Hz", fill=C["warn"], anchor="w")


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
