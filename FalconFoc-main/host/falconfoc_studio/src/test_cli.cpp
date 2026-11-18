// Headless hardware test of the C++ CAN stack (can_bus + dm_protocol).
// Enables motor, streams velocity, prints decoded feedback, disables.
#include "can_bus.h"
#include "dm_protocol.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <deque>

int main(int argc, char** argv) {
    const char* ifn = argc > 1 ? argv[1] : "can0";
    CanBus bus; std::string err;
    if (!bus.open(ifn, err)) { std::printf("open %s failed: %s\n", ifn, err.c_str()); return 1; }
    std::printf("opened %s\n", ifn);

    auto send = [&](uint32_t id, const dm::Frame& f){ bus.send(id, f.data(), 8); };
    auto sleep_ms = [](int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };

    send(dm::ID_MIT(1), dm::pack_special(dm::CMD_ENABLE));
    sleep_ms(50);

    std::deque<CanFrameRx> in;
    int fb = 0;
    for (int i = 0; i < 30; i++) {                       // ~1.2s @ 25Hz
        send(dm::ID_VEL(1), dm::pack_vel(3.0f));         // 3 rad/s
        sleep_ms(40);
        in.clear(); bus.drain(in);
        for (auto& f : in) if (!f.tx && f.id == 0) {
            auto d = dm::decode_feedback(f.data.data(), f.dlc);
            if (d.valid && (i % 6 == 0)) {
                std::printf("  fb[%2d] %s pos=%+.3f vel=%+.2f tau=%+.4f T=%d\n",
                            i, dm::err_text(d.err), d.pos, d.vel, d.torque, d.temp);
                fb++;
            }
        }
    }
    // stop + disable
    send(dm::ID_VEL(1), dm::pack_vel(0.0f)); sleep_ms(100);
    send(dm::ID_MIT(1), dm::pack_special(dm::CMD_DISABLE)); sleep_ms(50);
    std::printf("done. rx=%llu tx=%llu (printed %d fb samples)\n",
                (unsigned long long)bus.rx_count(), (unsigned long long)bus.tx_count(), fb);
    bus.close();
    return 0;
}
