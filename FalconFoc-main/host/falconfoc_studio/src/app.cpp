#include "app.h"
#include "imgui.h"
#include "implot.h"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <cstring>

static double mono() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

App::App() { t0_ = mono(); }
double App::now() const { return mono(); }

// ----------------------------------------------------------------------------
void App::render() {
    poll_bus();
    maybe_stream();
    panel_connection();
    panel_control();
    panel_telemetry();
    panel_canlog();
}

// ---- bus / logic -----------------------------------------------------------
void App::send_frame(uint32_t id, const dm::Frame& f) { bus_.send(id, f.data(), 8); }
void App::send_special(uint8_t cmd) {
    if (!bus_.is_open()) return;
    send_frame(dm::ID_MIT(motor_id_), dm::pack_special(cmd));
}

dm::Frame App::active_command_frame() const {
    switch (mode_) {
        case MODE_MIT:      return dm::pack_mit(sp_pos_, sp_vel_, sp_kp_, sp_kd_, sp_tff_);
        case MODE_POSITION: return dm::pack_posvel(sp_pos_, sp_posvel_limit_);
        case MODE_VELOCITY: return dm::pack_vel(sp_vel_);
        case MODE_TORQUE:   return dm::pack_mit(0, 0, 0, 0, sp_tff_);
    }
    return dm::pack_special(dm::CMD_QUERY);
}

void App::maybe_stream() {
    if (!bus_.is_open()) return;
    double t = now();
    if (streaming_) {
        double period = 1.0 / (stream_hz_ > 1 ? stream_hz_ : 1);
        if (t - last_stream_ >= period) {
            last_stream_ = t;
            uint32_t id;
            switch (mode_) {
                case MODE_POSITION: id = dm::ID_POSVEL(motor_id_); break;
                case MODE_VELOCITY: id = dm::ID_VEL(motor_id_); break;
                default:            id = dm::ID_MIT(motor_id_); break;
            }
            send_frame(id, active_command_frame());
        }
    } else {
        // idle: poll telemetry with zero-side-effect query @20Hz
        if (t - last_poll_q_ >= 0.05) {
            last_poll_q_ = t;
            send_special(dm::CMD_QUERY);
        }
    }
}

void App::poll_bus() {
    std::deque<CanFrameRx> in;
    bus_.drain(in);
    for (auto& f : in) {
        // log
        if ((f.tx && log_show_tx_) || (!f.tx && log_show_rx_)) {
            log_.push_back(f);
            while (log_.size() > log_cap_) log_.pop_front();
        }
        if (f.tx) continue;
        if (f.id == (uint32_t)dm::MASTER_ID) {
            dm::Feedback fb = dm::decode_feedback(f.data.data(), f.dlc);
            if (!fb.valid) continue;
            fb_ = fb; fb_count_++;
            // feedback rate (0.5s window)
            rate_win_n_++;
            if (f.t - rate_win_t_ >= 0.5) {
                fb_rate_ = (float)rate_win_n_ / (float)(f.t - rate_win_t_);
                rate_win_t_ = f.t; rate_win_n_ = 0;
            }
            if (!paused_) {
                float rt = (float)(f.t - t0_);
                ch_pos_.push(rt, fb.pos);
                ch_vel_.push(rt, fb.vel);
                ch_tau_.push(rt, fb.torque);
                float c = (mode_ == MODE_VELOCITY) ? sp_vel_ : sp_pos_;
                ch_cmd_.push(rt, c);
            }
        }
    }
}

// ---- panels ----------------------------------------------------------------
void App::panel_connection() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(560, 90), ImGuiCond_FirstUseEver);
    ImGui::Begin("Connection");

    ImGui::PushItemWidth(90);
    ImGui::InputText("##if", ifname_, sizeof(ifname_));
    ImGui::SameLine();
    ImGui::InputInt("ID", &motor_id_); if (motor_id_ < 0) motor_id_ = 0; if (motor_id_ > 0x7F) motor_id_ = 0x7F;
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (!bus_.is_open()) {
        if (ImGui::Button("Connect")) {
            if (!bus_.open(ifname_, conn_err_)) { /* err shown below */ }
            else conn_err_.clear();
        }
    } else {
        if (ImGui::Button("Disconnect")) bus_.close();
    }
    ImGui::SameLine();
    std::string st = CanBus::iface_state(ifname_);
    bool up = (st == "up" || st == "unknown");
    ImGui::TextColored(bus_.is_open() ? ImVec4(0.3f,0.9f,0.4f,1) : ImVec4(0.7f,0.7f,0.7f,1),
                       "%s", bus_.is_open() ? "● LINK" : "○ idle");
    ImGui::SameLine();
    ImGui::TextColored(up ? ImVec4(0.6f,0.8f,0.6f,1) : ImVec4(0.9f,0.5f,0.3f,1), "[%s %s]", ifname_, st.c_str());

    if (bus_.is_open()) {
        ImGui::SameLine();
        ImGui::Text("| rx %llu  tx %llu  fb %.0fHz",
                    (unsigned long long)bus_.rx_count(), (unsigned long long)bus_.tx_count(), fb_rate_);
    }
    if (!conn_err_.empty())
        ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "%s", conn_err_.c_str());
    else
        ImGui::TextDisabled("Tip: bring up CAN first:  sudo ip link set %s up type can bitrate 1000000", ifname_);
    ImGui::End();
}

// 滑条(拖) + 文本框(直接输值) 同一行; fmt 控制显示精度
static void slider_in(const char* label, float* v, float lo, float hi, const char* fmt="%.3f") {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(-150);
    ImGui::SliderFloat("##s", v, lo, hi, fmt);
    ImGui::SameLine(); ImGui::SetNextItemWidth(80);
    ImGui::InputFloat("##i", v, 0.0f, 0.0f, fmt); // 回车/失焦即生效
    ImGui::SameLine(); ImGui::TextUnformatted(label);
    ImGui::PopID();
}

void App::panel_control() {
    ImGui::SetNextWindowPos(ImVec2(10, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 470), ImGuiCond_FirstUseEver);
    ImGui::Begin("Control");

    // status row
    ImVec4 ec = (fb_.err == 1) ? ImVec4(0.3f,0.9f,0.4f,1)
              : (fb_.err <= 0) ? ImVec4(0.7f,0.7f,0.7f,1) : ImVec4(0.95f,0.4f,0.3f,1);
    ImGui::Text("State:"); ImGui::SameLine();
    ImGui::TextColored(ec, "%s", fb_.valid ? dm::err_text(fb_.err) : "—");
    ImGui::SameLine(); ImGui::Text("  T=%d°C", fb_.temp);

    ImGui::Separator();
    // power buttons
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f,0.55f,0.28f,1));
    if (ImGui::Button("ENABLE", ImVec2(90,30))) send_special(dm::CMD_ENABLE);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f,0.2f,0.2f,1));
    if (ImGui::Button("DISABLE", ImVec2(90,30))) send_special(dm::CMD_DISABLE);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Clr Err", ImVec2(70,30))) send_special(dm::CMD_CLEAR_ERR);
    if (ImGui::Button("Set Zero", ImVec2(90,24))) send_special(dm::CMD_SET_ZERO);
    ImGui::SameLine();
    if (ImGui::Button("Cogging Cal", ImVec2(110,24))) send_special(dm::CMD_COGGING_CAL);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f,0.30f,0.60f,1));
    if (ImGui::Button("Sing", ImVec2(70,24))) send_special(dm::CMD_SING);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"Motor plays a melody (简单爱, ~17s).\nAutonomous; ends idle in GUARD.");

    ImGui::Separator();
    const char* modes[] = { "MIT impedance", "Position", "Velocity", "Torque" };
    ImGui::Combo("Mode", &mode_, modes, IM_ARRAYSIZE(modes));

    ImGui::Checkbox("Stream", &streaming_);
    ImGui::SameLine(); ImGui::PushItemWidth(90);
    ImGui::SliderFloat("Hz", &stream_hz_, 5, 200, "%.0f");
    ImGui::PopItemWidth();
    ImGui::SameLine(); ImGui::TextDisabled("(>5 feeds deadman)");

    ImGui::Separator();
    switch (mode_) {
        case MODE_MIT:
            slider_in("P (rad)", &sp_pos_, -dm::P_MAX, dm::P_MAX, "%.3f");
            slider_in("V (rad/s)", &sp_vel_, -20, 20, "%.2f");
            slider_in("Kp", &sp_kp_, 0, 50, "%.2f");
            {
                bool over = sp_kd_ > dm::MIT_KD_SAFE;
                if (over) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,0.35f,0.3f,1));
                slider_in("Kd", &sp_kd_, 0, 0.1f, "%.4f");
                if (over) ImGui::PopStyleColor();
                ImGui::TextColored(over ? ImVec4(1,0.35f,0.3f,1) : ImVec4(0.6f,0.6f,0.6f,1),
                    "Kd ceiling ~%.3f (firmware clamps to 0.1 turn-units)", dm::MIT_KD_SAFE);
            }
            slider_in("T_ff (N·m)", &sp_tff_, -dm::T_MAX, dm::T_MAX, "%.3f");
            break;
        case MODE_POSITION:
            slider_in("Pos (rad)", &sp_pos_, -dm::P_MAX, dm::P_MAX, "%.3f");
            slider_in("Slew V (rad/s)", &sp_posvel_limit_, 0, 50, "%.1f");
            break;
        case MODE_VELOCITY:
            slider_in("Vel (rad/s)", &sp_vel_, -50, 50, "%.2f");
            break;
        case MODE_TORQUE:
            slider_in("Torque (N·m)", &sp_tff_, -dm::T_MAX, dm::T_MAX, "%.3f");
            break;
    }

    ImGui::Spacing();
    if (ImGui::Button("Send once", ImVec2(100, 28))) {
        uint32_t id = (mode_==MODE_POSITION)?dm::ID_POSVEL(motor_id_)
                    : (mode_==MODE_VELOCITY)?dm::ID_VEL(motor_id_)
                    : dm::ID_MIT(motor_id_);
        send_frame(id, active_command_frame());
    }
    ImGui::SameLine();
    if (ImGui::Button("Zero setpoints", ImVec2(120, 28))) {
        sp_pos_ = sp_vel_ = sp_tff_ = 0;
    }
    ImGui::End();
}

static void plot_channel(const char* label, const Channel& c) {
    static std::vector<float> ts, ys;
    latest(c, c.size(), ts, ys);
    if (!ts.empty()) ImPlot::PlotLine(label, ts.data(), ys.data(), (int)ts.size());
}

void App::panel_telemetry() {
    ImGui::SetNextWindowPos(ImVec2(380, 110), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(740, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Telemetry");

    ImGui::Text("pos %+8.3f rad   vel %+8.3f rad/s   tau %+7.4f N·m",
                fb_.pos, fb_.vel, fb_.torque);
    ImGui::SameLine();
    ImGui::Checkbox("Pause", &paused_);
    ImGui::SameLine(); ImGui::PushItemWidth(120);
    ImGui::SliderFloat("Window s", &plot_window_s_, 1, 30, "%.0f");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) { ch_pos_.clear(); ch_vel_.clear(); ch_tau_.clear(); ch_cmd_.clear(); }

    float tnow = (float)(now() - t0_);
    ImPlotAxisFlags xfl = ImPlotAxisFlags_None;

    if (ImPlot::BeginPlot("Position / cmd", ImVec2(-1, 150))) {
        ImPlot::SetupAxes("t (s)", "rad", xfl, ImPlotAxisFlags_AutoFit);
        if (!paused_) ImPlot::SetupAxisLimits(ImAxis_X1, tnow - plot_window_s_, tnow, ImGuiCond_Always);
        plot_channel("pos", ch_pos_);
        if (mode_ != MODE_VELOCITY) plot_channel("cmd", ch_cmd_);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Velocity", ImVec2(-1, 150))) {
        ImPlot::SetupAxes("t (s)", "rad/s", xfl, ImPlotAxisFlags_AutoFit);
        if (!paused_) ImPlot::SetupAxisLimits(ImAxis_X1, tnow - plot_window_s_, tnow, ImGuiCond_Always);
        plot_channel("vel", ch_vel_);
        if (mode_ == MODE_VELOCITY) plot_channel("cmd", ch_cmd_);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Torque", ImVec2(-1, 150))) {
        ImPlot::SetupAxes("t (s)", "N·m", xfl, ImPlotAxisFlags_AutoFit);
        if (!paused_) ImPlot::SetupAxisLimits(ImAxis_X1, tnow - plot_window_s_, tnow, ImGuiCond_Always);
        plot_channel("tau", ch_tau_);
        ImPlot::EndPlot();
    }

    // ---- FFT ----
    ImGui::Separator();
    const char* srcs[] = { "pos", "vel", "tau" };
    ImGui::PushItemWidth(90);
    ImGui::Combo("FFT src", &fft_source_, srcs, IM_ARRAYSIZE(srcs));
    ImGui::SameLine();
    float fs = fb_rate_ > 1 ? fb_rate_ : fft_fs_;
    ImGui::PopItemWidth();
    ImGui::SameLine(); ImGui::Text("fs=%.0fHz", fs);
    const Channel& fc = (fft_source_==0)?ch_pos_:(fft_source_==1)?ch_vel_:ch_tau_;
    static std::vector<float> ts, ys, freq, amp;
    latest(fc, fc.size(), ts, ys);
    amplitude_spectrum(ys, fs, freq, amp);
    if (ImPlot::BeginPlot("Spectrum", ImVec2(-1, 150))) {
        ImPlot::SetupAxes("Hz", "amp", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
        if (!freq.empty()) ImPlot::PlotLine("|X(f)|", freq.data(), amp.data(), (int)freq.size());
        ImPlot::EndPlot();
    }
    ImGui::End();
}

void App::panel_canlog() {
    ImGui::SetNextWindowPos(ImVec2(10, 590), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("CAN log");
    ImGui::Checkbox("TX", &log_show_tx_); ImGui::SameLine();
    ImGui::Checkbox("RX", &log_show_rx_); ImGui::SameLine();
    ImGui::Checkbox("Auto", &log_autoscroll_); ImGui::SameLine();
    if (ImGui::Button("Clear")) log_.clear();

    ImGui::BeginChild("logscroll", ImVec2(0,0), true, ImGuiWindowFlags_HorizontalScrollbar);
    for (auto& f : log_) {
        char line[96];
        int n = std::snprintf(line, sizeof(line), "%s %03X [%d]", f.tx ? "T" : "R", f.id, f.dlc);
        for (int i = 0; i < f.dlc; i++) n += std::snprintf(line+n, sizeof(line)-n, " %02X", f.data[i]);
        ImGui::TextColored(f.tx ? ImVec4(0.55f,0.75f,1.0f,1) : ImVec4(0.8f,0.85f,0.6f,1), "%s", line);
    }
    if (log_autoscroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}
