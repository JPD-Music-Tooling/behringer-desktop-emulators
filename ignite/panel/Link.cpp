// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Link.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace ignitem {

namespace {
#ifdef _WIN32
// Synchronous ReadFile/WriteFile from two different threads on the same pipe handle can hang
// indefinitely while the other direction is pending (observed directly: a minimal repro with a
// concurrent reader thread and writer thread on one handle hung the same way readLoop()/tickLoop()
// did here; switching both to overlapped I/O with a per-call OVERLAPPED/event fixed it). Every
// Windows read/write on this link goes through these two helpers instead of the bare WinAPI calls.
// lpNumberOfBytesRead/-Written is unreliable on an overlapped handle even when the call completes
// synchronously (TRUE immediately) -- the actually-transferred count must always come from
// GetOverlappedResult. Assuming it equals the requested size (an earlier version of this code did)
// causes a short completion to report phantom bytes from whatever was left in the caller's buffer,
// which looks exactly like a flood of repeated/garbled frames once the real traffic is short reads.
bool overlappedWrite(HANDLE h, const void* data, DWORD size, DWORD& written) {
    OVERLAPPED ov{};
    ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = ::WriteFile(h, data, size, nullptr, &ov);
    if (ok || ::GetLastError() == ERROR_IO_PENDING) ok = ::GetOverlappedResult(h, &ov, &written, TRUE);
    ::CloseHandle(ov.hEvent);
    return ok != 0;
}
bool overlappedRead(HANDLE h, void* buf, DWORD size, DWORD& read) {
    OVERLAPPED ov{};
    ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    BOOL ok = ::ReadFile(h, buf, size, nullptr, &ov);
    if (ok || ::GetLastError() == ERROR_IO_PENDING) ok = ::GetOverlappedResult(h, &ov, &read, TRUE);
    ::CloseHandle(ov.hEvent);
    return ok != 0;
}
#endif
double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
std::string hex(const std::vector<uint8_t>& b, size_t max = 24) {
    std::string s;
    char t[4];
    for (size_t i = 0; i < b.size() && i < max; ++i) std::snprintf(t, sizeof t, "%02x", b[i]), s += t;
    if (b.size() > max) s += "...";
    return s;
}
#ifndef _WIN32
void makeRaw(int fd) {
    termios t{};
    if (tcgetattr(fd, &t) != 0) return;
    cfmakeraw(&t);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}
#endif
}  // namespace

Link::Link(std::function<void(const std::string&)> log) : log_(std::move(log)) {}

Link::~Link() {
    running_ = false;
    if (reader_.joinable()) reader_.join();
    if (ticker_.joinable()) ticker_.join();
#ifdef _WIN32
    if (master_) ::CloseHandle(static_cast<HANDLE>(master_));
#else
    if (slaveFd_ >= 0) ::close(slaveFd_);
    if (master_ >= 0) ::close(master_);
#endif
}

bool Link::open() {
#ifdef _WIN32
    // Named pipe, server side: the app (Ignite's FrontPannelManager, FP_UART_WIN_PANEL) connects as a
    // client via CreateFile on the same name -- the same "panel is the side that creates the link, the
    // app opens it" shape as the macOS pty, just with Win32 primitives instead of posix_openpt.
    slave_ = R"(\\.\pipe\IgnitePanel)";
    HANDLE h = ::CreateNamedPipeA(slave_.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return log_("IgnitePanel: pipe failed, GetLastError=" + std::to_string(::GetLastError())), false;
    master_ = h;
#else
    master_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_ < 0 || grantpt(master_) != 0 || unlockpt(master_) != 0) return log_("IgnitePanel: pty failed"), false;
    const char* name = ptsname(master_);
    if (!name) return false;
    slave_ = name;
    // Hold the slave open ourselves (as the KickEmulator does): the master never sees a hang-up when the
    // app quits or restarts, and the app's bytes are not echoed back.
    slaveFd_ = ::open(slave_.c_str(), O_RDWR | O_NOCTTY);
    makeRaw(slaveFd_);
    makeRaw(master_);
#endif
    running_ = true;
    reader_ = std::thread([this] { readLoop(); });
    ticker_ = std::thread([this] { tickLoop(); });
    log_("IgnitePanel: listening on " + slave_);
    return true;
}

void Link::write(const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> lk(writeMu_);
#ifdef _WIN32
    DWORD off = 0;
    while (off < bytes.size()) {
        DWORD n = 0;
        if (!overlappedWrite(static_cast<HANDLE>(master_), bytes.data() + off, DWORD(bytes.size()) - off, n) ||
            n == 0) {
            log_("IgnitePanel: WriteFile failed, GetLastError=" + std::to_string(::GetLastError()));
            return;
        }
        off += n;
    }
#else
    size_t off = 0;
    while (off < bytes.size()) {
        const ssize_t n = ::write(master_, bytes.data() + off, bytes.size() - off);
        if (n <= 0) return;
        off += size_t(n);
    }
#endif
    std::lock_guard<std::mutex> l2(mu_);
    ++counters_.framesOut;
}

// ---- input

void Link::setKey(int bit, bool down) {
    if (bit < 0 || bit >= 96) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (down) keys_[size_t(bit / 8)] |= uint8_t(1u << (bit % 8));
        else keys_[size_t(bit / 8)] &= uint8_t(~(1u << (bit % 8)));
        upFrames_ = 3;  // the app ends a press on the frames after the release: send a few all-up ones
    }
    sendKeys();  // at once; the ticker keeps streaming while held (the app counts frames to see a hold)
}

bool Link::keyDown(int bit) const {
    std::lock_guard<std::mutex> lk(mu_);
    return bit >= 0 && bit < 96 && (keys_[size_t(bit / 8)] >> (bit % 8) & 1);
}

void Link::sendKeys() {
    std::vector<uint8_t> f = {0xE1, 0x01, 0x00, 0x0C};
    {
        std::lock_guard<std::mutex> lk(mu_);
        f.insert(f.end(), keys_.begin(), keys_.end());
        ++counters_.keyFrames;
    }
    if (live_) write(f);
}

void Link::turn(int enc, int detents) {
    if (enc < 0 || !detents) return;
    std::lock_guard<std::mutex> lk(mu_);
    pending_[enc] += detents;
}

PanelState Link::state() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_;
}

Link::Counters Link::counters() const {
    std::lock_guard<std::mutex> lk(mu_);
    return counters_;
}

// ---- the panel's own clock: handshake, key streaming, encoder pacing

void Link::tickLoop() {
    double lastReady = 0, lastKeys = 0;
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const double t = nowMs();
        if (!synced_ && t - lastReady > 500) {  // "MCU ready" until the app answers (it starts reading after 2 s)
            write({0xE0, 0x01, 0xA9, 0x01, 0x00});
            lastReady = t;
        }
        if (!live_) continue;
        bool any = false;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (uint8_t b : keys_) any |= b != 0;
            if (!any && upFrames_ > 0) --upFrames_, any = true;
        }
        if (any && t - lastKeys >= 40) sendKeys(), lastKeys = t;
        // encoders: one message per encoder per 100 ms at most (the app drops faster ones); big turns use the
        // fast codes (x5, x15): 0 left, 1 right, 4/5 fast, 6/7 superfast
        std::vector<std::vector<uint8_t>> out;
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (auto& [enc, n] : pending_) {
                if (!n || t - lastSent_[enc] < 105) continue;
                const int a = std::abs(n), right = n > 0;
                const int step = a >= 15 ? 15 : a >= 5 ? 5 : 1;
                const uint8_t code = uint8_t((step == 15 ? 6 : step == 5 ? 4 : 0) + right);
                out.push_back({0xE4, 0x01, 0x00, 0x02, uint8_t(enc), code});
                n -= right ? step : -step;
                lastSent_[enc] = t;
                ++counters_.encFrames;
            }
        }
        for (auto& f : out) write(f);
    }
}

// ---- what the app sends

void Link::drainFrames() {
    while (rx_.size() >= 4) {
        if (rx_[0] < 0xE0 || rx_[0] > 0xE8 || rx_[1] != 0x02) {  // not a packet start: resync
            rx_.erase(rx_.begin());
            continue;
        }
        const size_t size = rx_[3];
        if (size < 4) {
            rx_.erase(rx_.begin());
            continue;
        }
        if (rx_.size() < size) break;
        std::vector<uint8_t> f(rx_.begin(), rx_.begin() + long(size));
        rx_.erase(rx_.begin(), rx_.begin() + long(size));
        handle(f);
    }
}

#ifdef _WIN32
void Link::readLoop() {
    uint8_t buf[1024];
    HANDLE h = static_cast<HANDLE>(master_);
    while (running_) {
        OVERLAPPED connOv{};
        connOv.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL ok = ::ConnectNamedPipe(h, &connOv);
        const DWORD err = ::GetLastError();
        if (!ok && err == ERROR_IO_PENDING) {
            // Block until an actual client connects (or shutdown cancels it) -- never abandon this
            // pending I/O on a timeout: that desyncs the pipe's real connection state from what we
            // think it is (seen directly: spurious ERROR_PIPE_LISTENING on later writes).
            DWORD ignored = 0;
            ok = ::GetOverlappedResult(h, &connOv, &ignored, TRUE);
        } else if (!ok && err == ERROR_PIPE_CONNECTED) {
            ok = TRUE;
        }
        ::CloseHandle(connOv.hEvent);
        if (!ok || !running_) {
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        log_("IgnitePanel: client connected");
        while (running_) {
            DWORD n = 0;
            if (!overlappedRead(h, buf, sizeof buf, n) || n == 0) {
                log_("IgnitePanel: ReadFile ended, GetLastError=" + std::to_string(::GetLastError()));
                break;  // app closed: wait for a reconnect
            }
            rx_.insert(rx_.end(), buf, buf + n);
            drainFrames();
        }
        ::DisconnectNamedPipe(h);
    }
}
#else
void Link::readLoop() {
    uint8_t buf[1024];
    while (running_) {
        pollfd p{master_, POLLIN, 0};
        if (poll(&p, 1, 100) <= 0) continue;
        const ssize_t n = ::read(master_, buf, sizeof buf);
        if (n <= 0) continue;
        rx_.insert(rx_.end(), buf, buf + n);
        drainFrames();
    }
}
#endif

void Link::handle(const std::vector<uint8_t>& f) {
    const uint8_t h = f[0], id = f[2];
    bool changed = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ++counters_.framesIn;
        if (h == 0xE2 && f.size() >= 4 + 16) {  // the LED matrix
            std::copy(f.begin() + 4, f.begin() + 20, state_.ledRows.begin());
            changed = true;
        } else if (h == 0xE3 && f.size() >= 4 + 16) {  // pad colours
            std::copy(f.begin() + 4, f.begin() + 20, state_.padColour.begin());
            changed = true;
        } else if (h == 0xE4 && f.size() >= 4 + 8) {  // knob rings A-H
            std::copy(f.begin() + 4, f.begin() + 12, state_.ring.begin());
            changed = true;
        } else if (h == 0xE0 && id == 0xA5 && f.size() >= 5) {  // LED brightness
            state_.brightness = f[4];
            changed = true;
        }
    }
    if (h == 0xE0) {
        if (id == 0xA3 && !synced_) {  // the app saw "ready": it now asks for the version and starts its LED thread
            synced_ = true;
            log_("IgnitePanel: app answered ready (sync)");
        } else if (id == 0xA8) {  // firmware version request
            write({0xE0, 0x01, 0xA8, 0x01, version});
            if (!live_) log_("IgnitePanel: LIVE (version " + std::to_string(version) + " sent)");
            live_ = true;
        } else if (id == 0xA2) {
            // heartbeat from the app: nothing to answer
        } else if (id != 0xA5) {
            log_("IgnitePanel: system message " + hex(f));
        }
    } else if (h == 0xE5) {
        log_("IgnitePanel: MIDI out " + hex(f));
    } else if (h == 0xE7) {
        log_("IgnitePanel: sync " + hex(f));
    }
    if (changed && onChange) onChange();
}

}  // namespace ignitem
