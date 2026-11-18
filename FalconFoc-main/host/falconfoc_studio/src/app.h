// FalconFOC Studio — application state + ImGui/ImPlot panels.
#pragma once
#include "can_bus.h"
#include "dm_protocol.h"
#include "scope.h"
#include <deque>
#include <string>
#include <vector>

class App {
public:
    App();
    void render();   // called once per frame between ImGui NewFrame / Render

private:
    // ---- panels ----
    void panel_connection();
    void panel_control();
    void panel_telemetry();
    void panel_canlog();

    // ---- logic ----
    void poll_bus();                 // drain CAN, decode feedback, fill scope/log
    void maybe_stream();             // send active command at configured rate
    void send_frame(uint32_t id, const dm::Frame& f);
    void send_special(uint8_t cmd);
    dm::Frame active_command_frame() const;

    double now() const;
    double t0_;                      // app start time (for plot x-axis)

    CanBus bus_;
    char   ifname_[32] = "can0";
    int    motor_id_ = (int)dm::MOTOR_ID;
    std::string conn_err_;

    // ---- control state ----
    enum Mode { MODE_MIT = 0, MODE_POSITION, MODE_VELOCITY, MODE_TORQUE };
    int   mode_ = MODE_MIT;
    bool  streaming_ = false;        // continuously send setpoint (feeds firmware deadman)
    float stream_hz_ = 50.0f;        // >5Hz to satisfy 200ms deadman
    double last_stream_ = 0;
    double last_poll_q_ = 0;         // idle CMD_QUERY throttle

    // setpoints
    float sp_pos_ = 0.0f;            // rad
    float sp_vel_ = 0.0f;            // rad/s
    float sp_kp_  = 2.0f;
    float sp_kd_  = 0.01f;
    float sp_tff_ = 0.0f;            // N·m
    float sp_posvel_limit_ = 10.0f;  // rad/s slew for position mode

    // ---- telemetry ----
    dm::Feedback fb_;
    uint64_t fb_count_ = 0;
    double   fb_last_t_ = 0;
    float    fb_rate_ = 0;           // measured feedback Hz
    double   rate_win_t_ = 0; uint64_t rate_win_n_ = 0;

    Channel ch_pos_{8192}, ch_vel_{8192}, ch_tau_{8192}, ch_cmd_{8192};
    float   plot_window_s_ = 10.0f;
    bool    paused_ = false;

    // FFT
    int   fft_source_ = 1;           // 0=pos 1=vel 2=tau
    float fft_fs_ = 50.0f;           // assumed feedback rate for FFT x-axis

    // ---- CAN log ----
    std::deque<CanFrameRx> log_;
    bool   log_autoscroll_ = true;
    bool   log_show_tx_ = true, log_show_rx_ = true;
    size_t log_cap_ = 2000;
};
