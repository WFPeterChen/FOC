#include "can_bus.h"
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <fstream>
#include <chrono>

static double mono_now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool CanBus::open(const std::string& ifname, std::string& err) {
    close();
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { err = "socket() failed"; return false; }

    struct ifreq ifr{};
    std::strncpy(ifr.ifr_name, ifname.c_str(), IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        err = "interface '" + ifname + "' not found (is it up? `ip link`)";
        ::close(s); return false;
    }
    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        err = "bind() failed on " + ifname; ::close(s); return false;
    }
    // non-blocking-ish: use a short recv timeout so the thread can exit cleanly
    struct timeval tv{}; tv.tv_sec = 0; tv.tv_usec = 100000; // 100ms
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    fd_ = s;
    ifname_ = ifname;
    rx_count_ = tx_count_ = 0;
    running_ = true;
    rx_thread_ = std::thread(&CanBus::rx_loop, this);
    return true;
}

void CanBus::close() {
    running_ = false;
    if (rx_thread_.joinable()) rx_thread_.join();
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    ifname_.clear();
    std::lock_guard<std::mutex> lk(q_mtx_);
    queue_.clear();
}

bool CanBus::send(uint32_t id, const uint8_t* data, uint8_t len) {
    if (fd_ < 0) return false;
    struct can_frame fr{};
    fr.can_id = id;
    fr.can_dlc = len > 8 ? 8 : len;
    std::memcpy(fr.data, data, fr.can_dlc);
    ssize_t n = write(fd_, &fr, sizeof(fr));
    if (n != (ssize_t)sizeof(fr)) return false;
    tx_count_++;
    CanFrameRx lf{}; lf.id = id; lf.dlc = fr.can_dlc;
    std::memcpy(lf.data.data(), data, fr.can_dlc);
    lf.t = mono_now(); lf.tx = true;
    { std::lock_guard<std::mutex> lk(q_mtx_); queue_.push_back(lf);
      if (queue_.size() > 4096) queue_.pop_front(); }
    return true;
}

void CanBus::rx_loop() {
    while (running_) {
        struct can_frame fr{};
        ssize_t n = read(fd_, &fr, sizeof(fr));
        if (n != (ssize_t)sizeof(fr)) continue; // timeout or partial
        CanFrameRx f{};
        f.id = fr.can_id & CAN_EFF_MASK;
        f.dlc = fr.can_dlc;
        std::memcpy(f.data.data(), fr.data, fr.can_dlc);
        f.t = mono_now(); f.tx = false;
        rx_count_++;
        std::lock_guard<std::mutex> lk(q_mtx_);
        queue_.push_back(f);
        if (queue_.size() > 4096) queue_.pop_front();
    }
}

void CanBus::drain(std::deque<CanFrameRx>& out) {
    std::lock_guard<std::mutex> lk(q_mtx_);
    while (!queue_.empty()) { out.push_back(queue_.front()); queue_.pop_front(); }
}

std::string CanBus::iface_state(const std::string& ifname) {
    // read /sys/class/net/<if>/operstate and the can state via `ip` is heavier;
    // operstate is enough to show up/down. CAN error state shown via frame flow.
    std::ifstream f("/sys/class/net/" + ifname + "/operstate");
    if (!f.good()) return "missing";
    std::string s; std::getline(f, s);
    return s.empty() ? "unknown" : s;
}
