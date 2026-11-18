#!/usr/bin/env python3
"""FalconFOC one-command test tool (DM CAN protocol). Terse, precise output.

Usage:
  python3 foc_test.py status                 # current pos/vel/tau/err/temp
  python3 foc_test.py enable|disable|clearerr
  python3 foc_test.py vel  <rad/s>           # velocity step, report tracking
  python3 foc_test.py pos  <rad> [slew]      # position step, report reached/err
  python3 foc_test.py mit  <kp> <kd> [tff]   # MIT hold at pos 0, torque ripple
  python3 foc_test.py sweep                   # velocity sweep 2..40 rad/s
  python3 foc_test.py possteps                # position step sequence, err/overshoot
  python3 foc_test.py deadman                 # verify deadman trips on comms loss
  python3 foc_test.py isr                     # FOC ISR cycle time via SWD (resets board!)
  python3 foc_test.py selftest                # full suite, compact PASS/FAIL summary
Env: IFACE (default can0), MID (default 1).
"""
import socket, struct, time, threading, sys, os, statistics, subprocess

IFACE = os.environ.get("IFACE", "can0")
MID   = int(os.environ.get("MID", "1"))
ID_MIT, ID_POSVEL, ID_VEL, ID_FB = MID, 0x100+MID, 0x200+MID, 0x00
P_MAX, V_MAX, T_MAX = 50.0, 300.0, 0.3
KP_MAX, KD_MAX = 500.0, 5.0
MIT_KD_SAFE = 0.016
ENABLE, DISABLE, CLEARERR, SETZERO = 0xFC, 0xFD, 0xFB, 0xFE
ERR = {0:"idle",1:"run",8:"OV",9:"UV",0xA:"OC",0xB:"OT",0xE:"OVSPD"}

def f2u(x,lo,hi,b):
    x=max(lo,min(hi,x)); return int((x-lo)*((1<<b)-1)/(hi-lo))
def u2f(xi,lo,hi,b): return xi*(hi-lo)/((1<<b)-1)+lo

class Bus:
    def __init__(s):
        s.sk=socket.socket(socket.AF_CAN,socket.SOCK_RAW,socket.CAN_RAW); s.sk.bind((IFACE,))
        s.fb=None; s.n=0; s.stop=False; s.coll=True; s.log=[]
        s.rx=socket.socket(socket.AF_CAN,socket.SOCK_RAW,socket.CAN_RAW); s.rx.bind((IFACE,)); s.rx.settimeout(0.2)
        s.t=threading.Thread(target=s._rx,daemon=True); s.t.start(); time.sleep(0.2)
    def _rx(s):
        while not s.stop:
            try: fr=s.rx.recv(16)
            except socket.timeout: continue
            cid,dlc=struct.unpack("=IB3x",fr[:8]); cid&=0x1FFFFFFF; d=fr[8:8+dlc]
            if cid==ID_FB and dlc>=7:
                err=d[0]>>4; p=u2f((d[1]<<8)|d[2],-P_MAX,P_MAX,16)
                v=u2f((d[3]<<4)|(d[4]>>4),-V_MAX,V_MAX,12); tq=u2f(((d[4]&0xF)<<8)|d[5],-T_MAX,T_MAX,12)
                s.fb=(time.time(),p,v,tq,err,d[6]); s.n+=1
                if s.coll: s.log.append(s.fb)
    def snd(s,cid,data): s.sk.send(struct.pack("=IB3x8s",cid,len(data),bytes(data).ljust(8,b'\0')))
    def special(s,cmd): s.snd(ID_MIT,[0xFF]*7+[cmd])
    def mit(s,p,v,kp,kd,t):
        b=[ (f2u(p,-P_MAX,P_MAX,16)>>8)&0xFF, f2u(p,-P_MAX,P_MAX,16)&0xFF, (f2u(v,-V_MAX,V_MAX,12)>>4)&0xFF,
             ((f2u(v,-V_MAX,V_MAX,12)&0xF)<<4)|(f2u(kp,0,KP_MAX,12)>>8), f2u(kp,0,KP_MAX,12)&0xFF,
             (f2u(kd,0,KD_MAX,12)>>4)&0xFF, ((f2u(kd,0,KD_MAX,12)&0xF)<<4)|(f2u(t,-T_MAX,T_MAX,12)>>8), f2u(t,-T_MAX,T_MAX,12)&0xFF ]
        s.snd(ID_MIT,b)
    def close(s): s.stop=True

def stream(bus, mode, secs, val=0.0, val2=5.0, kp=2.0, kd=0.016, tff=0.0, rate=100, collect=True):
    """mode: 'vel'|'pos'|'mit'. Streams with periodic re-ENABLE. Returns running samples."""
    bus.log=[]; bus.coll=collect; bus.special(ENABLE); time.sleep(0.1)
    t0=time.time(); le=t0
    while time.time()-t0<secs:
        if   mode=='vel': bus.snd(ID_VEL, struct.pack("<f", val))
        elif mode=='pos': bus.snd(ID_POSVEL, struct.pack("<ff", val, val2))
        elif mode=='mit': bus.mit(val, 0.0, kp, kd, tff)
        if time.time()-le>0.3: bus.special(ENABLE); le=time.time()
        time.sleep(1.0/rate)
    bus.coll=False
    return [x for x in bus.log if x[4]==1]

# ---- tests: return (name, pass_bool, detail) ----
def t_status(bus):
    bus.special(0x00); time.sleep(0.15)
    if not bus.fb: return ("status",False,"no feedback")
    _,p,v,tq,e,T=bus.fb
    return ("status",True,f"err={ERR.get(e,e)} pos={p:+.3f}rad vel={v:+.2f} tau={tq:+.4f}Nm T={T}C")

def t_vel(bus,target):
    w=stream(bus,'vel',3.0,val=target)
    if not w: return ("vel",False,"no run samples")
    vs=[x[2] for x in w[-40:]]; m=statistics.mean(vs)
    ok=abs(m-target)<max(0.5,0.15*abs(target)+0.3)
    return ("vel",ok,f"cmd={target} got={m:+.2f} std={statistics.pstdev(vs):.3f} n={len(w)}")

def t_pos(bus,target,slew=10.0):
    w=stream(bus,'pos',3.0,val=target,val2=slew)
    if not w: return ("pos",False,"no run samples")
    m=statistics.mean([x[1] for x in w[-30:]])
    ok=abs(m-target)<0.15
    return ("pos",ok,f"cmd={target:+.2f} got={m:+.3f} err={m-target:+.3f}")

def t_mit(bus,kp,kd,tff=0.0):
    if kd>MIT_KD_SAFE: print(f"  [warn] kd={kd}>{MIT_KD_SAFE} safe ceiling (fw clamps)")
    bus.special(0x00); time.sleep(0.12)      # hold at CURRENT position (avoid huge error->overcurrent)
    p0=bus.fb[1] if bus.fb else 0.0
    w=stream(bus,'mit',3.0,val=p0,kp=kp,kd=kd,tff=tff)
    if not w: return ("mit",False,"no run samples")
    ts=[x[3] for x in w[-40:]]; std=statistics.pstdev(ts)
    ok=std<0.02
    return ("mit",ok,f"kp={kp} kd={kd} tau_mean={statistics.mean(ts):+.4f} std={std:.4f}")

def t_sweep(bus):
    out=[]; allok=True
    for v in [2,5,10,20,30,40]:
        w=stream(bus,'vel',2.0,val=float(v))
        if w:
            vs=[x[2] for x in w]; m=statistics.mean(vs); ok=abs(m-v)<0.2*v+1
            out.append(f"{v}:{m:.1f}({statistics.pstdev(vs):.2f})"); allok&=ok
        else: out.append(f"{v}:X"); allok=False
    return ("sweep",allok,"cmd:got(std) "+" ".join(out))

def t_possteps(bus):
    res=[]; allok=True
    for tgt in [0.0,3.14,6.28,0.0,-3.14,0.0]:
        w=stream(bus,'pos',2.5,val=tgt,val2=15.0)
        if w:
            m=statistics.mean([x[1] for x in w[-25:]]); e=m-tgt; ok=abs(e)<0.1
            res.append(f"{tgt:+.1f}->{m:+.2f}(e{e:+.3f})"); allok&=ok
        else: res.append(f"{tgt:+.1f}:X"); allok=False
    return ("possteps",allok," ".join(res))

def t_deadman(bus):
    stream(bus,'vel',1.0,val=2.0)            # run 1s
    bus.special(ENABLE); bus.snd(ID_VEL,struct.pack("<f",2.0)); time.sleep(0.05)
    time.sleep(1.0)                           # silence >> 200ms
    n0=bus.n; bus.special(0x00)               # query; wait for a FRESH feedback frame
    t0=time.time()
    while bus.n==n0 and time.time()-t0<0.3: time.sleep(0.005)
    e=bus.fb[4] if (bus.fb and bus.n>n0) else -1
    ok=(e!=1)
    return ("deadman",ok,f"after 1.0s silence err={ERR.get(e,e)} (expect not 'run')")

def t_isr(bus):
    """Read FOC ISR cycle time via SWD. NOTE: openocd connect resets the board."""
    gcc=os.path.join(os.path.dirname(__file__),"..","2.Firmware","FalconFoc","gcc")
    elf=os.path.join(gcc,"build","FalconFOC.elf")
    if not os.path.exists(elf): return ("isr",False,"elf not found")
    g=("set pagination off\nset confirm off\nset architecture arm\ntarget extended-remote :3333\n"
       "monitor halt\nprintf \"foc_cyc=%u max=%u over=%u (8400=50us budget)\\n\", g_foc_cyc, g_foc_cyc_max, g_foc_over_cnt\n"
       "monitor resume\ndetach\nquit\n")
    open("/tmp/_isr.gdb","w").write(g)
    ocd=subprocess.Popen(["openocd","-f","interface/jlink.cfg","-c","transport select swd","-f","target/stm32g4x.cfg",
        "-c","adapter speed 2000"],cwd=gcc,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    time.sleep(1.2)
    try:
        r=subprocess.run(["gdb-multiarch","-q","-nx","-batch","-x","/tmp/_isr.gdb",elf],
            capture_output=True,text=True,timeout=15)
        line=[l for l in r.stdout.splitlines() if "foc_cyc=" in l]
        det=line[0] if line else "read failed"
    finally:
        ocd.terminate()
    return ("isr", "foc_cyc=" in det, det)

def t_selftest(bus):
    bus.special(CLEARERR); time.sleep(1.2)   # clear any latched fault + recalibrate from flash (resets wound-up pos)
    st=t_status(bus)
    # position tests FIRST (before velocity winds up the position), targets relative to current pos
    bus.special(0x00); time.sleep(0.15)
    p0=round(bus.fb[1],2) if bus.fb else 0.0
    tests=[st, t_pos(bus,p0+3.14), t_pos(bus,p0),
           t_vel(bus,5.0), t_mit(bus,2.0,0.016), t_mit(bus,2.0,0.3), t_deadman(bus)]
    bus.special(DISABLE)
    npass=sum(1 for _,ok,_ in tests if ok)
    print("── FalconFOC self-test ──")
    for name,ok,det in tests: print(f"  [{'PASS' if ok else 'FAIL'}] {name:9s} {det}")
    print(f"══ {npass}/{len(tests)} PASS ══")
    return npass==len(tests)

def main():
    if len(sys.argv)<2: print(__doc__); return
    cmd=sys.argv[1]; bus=Bus()
    try:
        if   cmd=="status":   print("%s: %s"%t_status(bus)[::2])
        elif cmd=="enable":   bus.special(ENABLE); print("enabled")
        elif cmd=="disable":  bus.special(DISABLE); print("disabled")
        elif cmd=="clearerr": bus.special(CLEARERR); print("cleared")
        elif cmd=="vel":      n,ok,d=t_vel(bus,float(sys.argv[2])); bus.special(DISABLE); print(f"[{'PASS' if ok else 'FAIL'}] {d}")
        elif cmd=="pos":      n,ok,d=t_pos(bus,float(sys.argv[2]),float(sys.argv[3]) if len(sys.argv)>3 else 10.0); bus.special(DISABLE); print(f"[{'PASS' if ok else 'FAIL'}] {d}")
        elif cmd=="mit":      n,ok,d=t_mit(bus,float(sys.argv[2]),float(sys.argv[3]),float(sys.argv[4]) if len(sys.argv)>4 else 0.0); bus.special(DISABLE); print(f"[{'PASS' if ok else 'FAIL'}] {d}")
        elif cmd=="sweep":    n,ok,d=t_sweep(bus); bus.special(DISABLE); print(f"[{'PASS' if ok else 'FAIL'}] {d}")
        elif cmd=="possteps": n,ok,d=t_possteps(bus); bus.special(DISABLE); print(f"[{'PASS' if ok else 'FAIL'}] {d}")
        elif cmd=="deadman":  n,ok,d=t_deadman(bus); bus.special(DISABLE); print(f"[{'PASS' if ok else 'FAIL'}] {d}")
        elif cmd=="isr":      n,ok,d=t_isr(bus); print(f"[{'OK' if ok else 'FAIL'}] {d}")
        elif cmd=="selftest": sys.exit(0 if t_selftest(bus) else 1)
        elif cmd=="zero":     bus.special(SETZERO); time.sleep(0.2); bus.special(0x00); time.sleep(0.2); print(f"zeroed, pos={bus.fb[1]:+.4f} rad" if bus.fb else "zeroed")
        elif cmd=="run":      # 持续速度模式: foc_test.py run <rad/s>  (Ctrl+C 停)
            v=float(sys.argv[2]); bus.special(ENABLE); time.sleep(0.1)
            print(f"velocity {v} rad/s — Ctrl+C 停"); le=lp=time.time()
            try:
                while True:
                    bus.snd(ID_VEL, struct.pack("<f", v))
                    if time.time()-le>0.3: bus.special(ENABLE); le=time.time()
                    if time.time()-lp>0.5 and bus.fb:
                        _,p,vv,tq,e,T=bus.fb; print(f"\r  vel={vv:+7.2f} tau={tq:+.4f} pos={p:+8.2f} err={ERR.get(e,e)} T={T}C ", end="", flush=True); lp=time.time()
                    time.sleep(0.01)
            except KeyboardInterrupt: pass
            bus.snd(ID_VEL, struct.pack("<f", 0.0)); time.sleep(0.3); bus.special(DISABLE); print("\nstopped")
        elif cmd=="hold":     # MIT 阻抗保持当前位置: foc_test.py hold [kp] [kd]  (Ctrl+C 停; 手感=虚拟弹簧)
            kp=float(sys.argv[2]) if len(sys.argv)>2 else 2.0
            kd=float(sys.argv[3]) if len(sys.argv)>3 else 0.016
            kd=min(kd, MIT_KD_SAFE)
            # 等转子静止(上次OC/急停后可能还在滑行, 抓运动中位置当目标会再冲流)
            t0=time.time()
            while time.time()-t0<5.0:
                bus.special(0x00); time.sleep(0.15)
                if bus.fb and abs(bus.fb[2])<0.5: break
            p0=bus.fb[1] if bus.fb else 0.0
            if abs(p0) >= 49.0:  # 反馈已贴量程钳位(真实位置未知) → 先归零, 否则 kp×假误差会冲电流OC
                bus.special(SETZERO); time.sleep(0.2); p0=0.0; print("(pos超±50量程, 已自动SET_ZERO)")
            bus.special(ENABLE); time.sleep(0.1)
            print(f"MIT hold @ {p0:+.3f} rad, kp={kp} kd={kd} — Ctrl+C 停(可以用手掰感受刚度)"); le=lp=time.time(); ten=time.time()
            try:
                while True:
                    kpr=kp*min(1.0,(time.time()-ten)/1.5)  # kp 1.5s 缓升, 防咬合瞬间冲流
                    bus.mit(p0, 0.0, kpr, kd, 0.0)
                    if time.time()-le>0.3: bus.special(ENABLE); le=time.time()
                    if time.time()-lp>0.5 and bus.fb:
                        _,p,vv,tq,e,T=bus.fb; print(f"\r  pos={p:+7.3f} tau={tq:+.4f} err={ERR.get(e,e)} T={T}C ", end="", flush=True); lp=time.time()
                    time.sleep(0.01)
            except KeyboardInterrupt: pass
            bus.special(DISABLE); print("\nstopped")
        else: print(__doc__)
    finally:
        bus.close()

if __name__=="__main__": main()
