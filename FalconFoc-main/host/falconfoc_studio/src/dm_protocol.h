// DM-motor (DaMiao MIT-mode) CAN protocol — encode/decode.
// Mirrors firmware MODULES/can_comm/dm_can.c exactly. Header-only.
#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <string>

namespace dm {

// ---- IDs (single motor on bus) ----
constexpr uint32_t MOTOR_ID  = 0x01;
constexpr uint32_t MASTER_ID = 0x00;            // feedback target
inline uint32_t ID_MIT(uint32_t id)    { return id; }
inline uint32_t ID_POSVEL(uint32_t id) { return 0x100 + id; }
inline uint32_t ID_VEL(uint32_t id)    { return 0x200 + id; }

// ---- physical ranges (must match firmware dm_can.h) ----
constexpr float P_MAX = 50.0f;     // rad   (16 bit)
constexpr float V_MAX = 300.0f;    // rad/s (12 bit)
constexpr float T_MAX = 0.3f;      // N·m   (12 bit)
constexpr float KP_MIN = 0.0f, KP_MAX = 500.0f; // (12 bit)
constexpr float KD_MIN = 0.0f, KD_MAX = 5.0f;   // (12 bit)

// ---- special command bytes (frame = FF FF FF FF FF FF FF <cmd>) ----
constexpr uint8_t CMD_ENABLE      = 0xFC;
constexpr uint8_t CMD_DISABLE     = 0xFD;
constexpr uint8_t CMD_SET_ZERO    = 0xFE;
constexpr uint8_t CMD_CLEAR_ERR   = 0xFB;
constexpr uint8_t CMD_COGGING_CAL = 0xFA;
constexpr uint8_t CMD_SING        = 0xF9;  // ext (non-DM): motor plays a melody (~5.6s, autonomous)
constexpr uint8_t CMD_QUERY       = 0x00;  // undefined cmd: zero side-effect, still replies one feedback frame

// 4010 measured MIT Kd stable ceiling N·m/(rad/s) (input_kd≈0.1 in firmware).
// Firmware now also hard-clamps, but warn the user here too.
constexpr float MIT_KD_SAFE = 0.016f;

inline uint32_t f2u(float x, float lo, float hi, uint8_t bits) {
    if (x < lo) x = lo; if (x > hi) x = hi;
    float span = hi - lo;
    return (uint32_t)((x - lo) * (float)((1u << bits) - 1u) / span);
}
inline float u2f(uint32_t xi, float lo, float hi, uint8_t bits) {
    float span = hi - lo;
    return (float)xi * span / (float)((1u << bits) - 1u) + lo;
}

using Frame = std::array<uint8_t, 8>;

// MIT impedance frame: tau = kp*(p-θ) + kd*(v-ω) + t_ff
inline Frame pack_mit(float p, float v, float kp, float kd, float t) {
    uint32_t pi = f2u(p, -P_MAX, P_MAX, 16);
    uint32_t vi = f2u(v, -V_MAX, V_MAX, 12);
    uint32_t ki = f2u(kp, KP_MIN, KP_MAX, 12);
    uint32_t di = f2u(kd, KD_MIN, KD_MAX, 12);
    uint32_t ti = f2u(t, -T_MAX, T_MAX, 12);
    return Frame{ (uint8_t)(pi >> 8), (uint8_t)(pi & 0xFF), (uint8_t)(vi >> 4),
                  (uint8_t)(((vi & 0xF) << 4) | (ki >> 8)), (uint8_t)(ki & 0xFF),
                  (uint8_t)(di >> 4), (uint8_t)(((di & 0xF) << 4) | (ti >> 8)),
                  (uint8_t)(ti & 0xFF) };
}

inline Frame pack_special(uint8_t cmd) {
    return Frame{ 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, cmd };
}

// posvel / vel frames carry little-endian float32 in payload
inline Frame pack_posvel(float p_rad, float v_rad) {
    Frame f{}; std::memcpy(f.data(), &p_rad, 4); std::memcpy(f.data()+4, &v_rad, 4); return f;
}
inline Frame pack_vel(float v_rad) {
    Frame f{}; std::memcpy(f.data(), &v_rad, 4); return f;
}

struct Feedback {
    int   err = -1;       // status nibble
    float pos = 0;        // rad
    float vel = 0;        // rad/s
    float torque = 0;     // N·m
    int   temp = 0;       // °C
    bool  valid = false;
};

inline Feedback decode_feedback(const uint8_t* d, int len) {
    Feedback fb;
    if (len < 7) return fb;
    fb.err    = d[0] >> 4;
    uint32_t pi = (d[1] << 8) | d[2];
    uint32_t vi = (d[3] << 4) | (d[4] >> 4);
    uint32_t ti = ((d[4] & 0xF) << 8) | d[5];
    fb.pos    = u2f(pi, -P_MAX, P_MAX, 16);
    fb.vel    = u2f(vi, -V_MAX, V_MAX, 12);
    fb.torque = u2f(ti, -T_MAX, T_MAX, 12);
    fb.temp   = d[6];
    fb.valid  = true;
    return fb;
}

inline const char* err_text(int e) {
    switch (e) {
        case 0: return "Idle";      case 1: return "Run";
        case 8: return "OverVolt";  case 9: return "UnderVolt";
        case 0xA: return "OverCurr";case 0xB: return "OverTemp";
        case 0xE: return "OverSpeed";default: return "?";
    }
}

} // namespace dm
