// SPDX-License-Identifier: AGPL-3.0-or-later
// The front panel microcontroller's side of the IGNITE link, over a pty (macOS/Linux) or a named pipe
// (Windows).
//
// Wire format (FrontPannelManager.cpp, MCUInterfaceParams.h): every packet is
//   [header E0..E8][direction][id][size][payload]
// panel -> app: direction 0x01, size = payload length
// app -> panel: direction 0x02, size = payload + 4 (the whole packet)
// There is no start marker, checksum or escaping, so the panel never sends anything malformed.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ignitem {

struct PanelState {               // what the app last told the panel to show
    std::array<uint8_t, 16> ledRows{};   // E2: byte = row, bit = com
    std::array<uint8_t, 16> padColour{}; // E3: colour code per pad (A0 off, A1 red, ...)
    std::array<uint8_t, 8> ring{};       // E4: lit LEDs per knob A-H, 0..16
    uint8_t brightness = 0x13;           // A5: 0x10..0x13 (25..100 %), 0x14 off
    bool lit(int com, int row) const { return com >= 0 && row >= 0 && row < 16 && (ledRows[size_t(row)] >> com & 1); }
};

class Link {
public:
    explicit Link(std::function<void(const std::string&)> log);
    ~Link();
    bool open();                       // makes the link; slavePath() is what the app opens/connects to
    const std::string& slavePath() const { return slave_; }
    bool live() const { return live_; }

    // input
    void setKey(int bit, bool down);   // the key bitmap; it streams while any key is down
    void turn(int enc, int detents);   // queued; sent at most every 100 ms per encoder, fast codes for big turns
    bool keyDown(int bit) const;

    PanelState state() const;
    std::function<void()> onChange;    // called from the link thread when the lights changed
    uint8_t version = 7;               // what the panel answers to a firmware version request

    struct Counters { long framesIn = 0, framesOut = 0, keyFrames = 0, encFrames = 0; };
    Counters counters() const;

private:
    void readLoop();
    void tickLoop();
    void handle(const std::vector<uint8_t>& f);
    void drainFrames();                // consumes complete frames out of rx_, dispatching each to handle()
    void write(const std::vector<uint8_t>& bytes);
    void sendKeys();

    std::function<void(const std::string&)> log_;
#ifdef _WIN32
    void* master_ = nullptr;           // HANDLE to the named pipe (kept as void* so windows.h stays out of callers)
#else
    int master_ = -1, slaveFd_ = -1;
#endif
    std::string slave_;
    std::atomic<bool> running_{false}, live_{false}, synced_{false};
    std::thread reader_, ticker_;
    mutable std::mutex mu_;
    PanelState state_;
    std::array<uint8_t, 12> keys_{};
    int upFrames_ = 0;                 // all-up frames still to send after a release
    std::map<int, int> pending_;       // encoder -> detents waiting
    std::map<int, double> lastSent_;   // encoder -> time of its last message (ms)
    std::vector<uint8_t> rx_;
    Counters counters_;
    std::mutex writeMu_;
};

}  // namespace ignitem
