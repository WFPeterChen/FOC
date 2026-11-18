// SocketCAN wrapper: open a CAN interface, send frames, receive on a background
// thread into a thread-safe queue. Linux-only (uses linux/can.h).
#pragma once
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <array>

struct CanFrameRx {
    uint32_t id;
    uint8_t  dlc;
    std::array<uint8_t, 8> data;
    double   t;   // monotonic seconds when received
    bool     tx;  // true if this is a frame we transmitted (for the log)
};

class CanBus {
public:
    CanBus() = default;
    ~CanBus() { close(); }

    bool open(const std::string& ifname, std::string& err);
    void close();
    bool is_open() const { return fd_ >= 0; }

    // returns false on write error
    bool send(uint32_t id, const uint8_t* data, uint8_t len);

    // pop all received (and locally-logged tx) frames since last call
    void drain(std::deque<CanFrameRx>& out);

    // stats
    uint64_t rx_count() const { return rx_count_.load(); }
    uint64_t tx_count() const { return tx_count_.load(); }
    std::string ifname() const { return ifname_; }

    // helper: query interface state string ("UP/ERROR-ACTIVE", "DOWN", "missing")
    static std::string iface_state(const std::string& ifname);

private:
    void rx_loop();

    int fd_ = -1;
    std::string ifname_;
    std::thread rx_thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> rx_count_{0}, tx_count_{0};
    std::mutex q_mtx_;
    std::deque<CanFrameRx> queue_;
};
