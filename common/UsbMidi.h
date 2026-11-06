// SPDX-License-Identifier: AGPL-3.0-or-later
// USB-MIDI 1.0 event packets <-> MIDI messages, for the emulated controllers whose firmware speaks
// USB-MIDI (AQ64L, BCR32), and a byte-stream parser for their DIN ports.
#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace emu {

// One MIDI message as USB-MIDI packets on a cable (code index numbers per the USB-MIDI 1.0 spec).
inline std::vector<uint8_t> toPackets(const juce::MidiMessage& m, int cable) {
    std::vector<uint8_t> out;
    const auto* d = m.getRawData();
    const int n = m.getRawDataSize();
    const uint8_t c = uint8_t((cable & 0x0F) << 4);
    auto put = [&](uint8_t cin, uint8_t a, uint8_t b, uint8_t x) { out.insert(out.end(), {uint8_t(c | cin), a, b, x}); };
    if (n <= 0) return out;
    if (d[0] == 0xF0) {  // SysEx: 0x4 for each full triple, 0x5/0x6/0x7 for the tail
        for (int i = 0; i < n; i += 3) {
            const int rem = n - i;
            if (rem > 3) put(0x4, d[i], d[i + 1], d[i + 2]);
            else if (rem == 3) put(0x7, d[i], d[i + 1], d[i + 2]);
            else if (rem == 2) put(0x6, d[i], d[i + 1], 0);
            else put(0x5, d[i], 0, 0);
        }
        return out;
    }
    const uint8_t s = d[0];
    if (s < 0xF0) put(uint8_t(s >> 4), s, n > 1 ? d[1] : 0, n > 2 ? d[2] : 0);
    else if (s >= 0xF8) put(0xF, s, 0, 0);                   // real time
    else if (s == 0xF2) put(0x3, s, d[1], d[2]);             // song position
    else if (s == 0xF1 || s == 0xF3) put(0x2, s, d[1], 0);   // MTC quarter frame, song select
    else put(0x5, s, 0, 0);                                  // tune request etc.
    return out;
}

// Packets -> messages, per cable (SysEx is reassembled across packets).
class PacketParser {
public:
    using Sink = std::function<void(int cable, const juce::MidiMessage&)>;
    explicit PacketParser(Sink sink) : sink_(std::move(sink)) {}

    void feed(const uint8_t* p, size_t len) {
        for (size_t i = 0; i + 4 <= len; i += 4) packet(p + i);
    }

private:
    void packet(const uint8_t* p) {
        const int cable = p[0] >> 4, cin = p[0] & 0x0F;
        auto& sx = sysex_[size_t(cable)];
        auto sysexBytes = [&](int count) {
            for (int k = 0; k < count; ++k) {
                const uint8_t b = p[1 + k];
                if (b == 0xF0) sx.clear();
                sx.push_back(b);
                if (b == 0xF7) {
                    if (sx.size() >= 2 && sx.front() == 0xF0) sink_(cable, juce::MidiMessage(sx.data(), int(sx.size())));
                    sx.clear();
                }
            }
        };
        switch (cin) {
            case 0x4: sysexBytes(3); break;
            case 0x5:
                if (p[1] == 0xF7 || !sx.empty()) sysexBytes(1);
                else sink_(cable, juce::MidiMessage(p[1]));
                break;
            case 0x6: sysexBytes(2); break;
            case 0x7: sysexBytes(3); break;
            case 0x2: case 0xC: case 0xD: sink_(cable, juce::MidiMessage(p[1], p[2])); break;
            case 0x3: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: sink_(cable, juce::MidiMessage(p[1], p[2], p[3])); break;
            case 0xF: sink_(cable, juce::MidiMessage(p[1])); break;
            default: break;  // 0x0/0x1 reserved
        }
    }
    Sink sink_;
    std::vector<uint8_t> sysex_[16];
};

// A raw MIDI byte stream (a DIN port) -> messages, with running status.
class ByteParser {
public:
    using Sink = std::function<void(const juce::MidiMessage&)>;
    explicit ByteParser(Sink sink) : sink_(std::move(sink)) {}

    void feed(const uint8_t* p, size_t len) {
        for (size_t i = 0; i < len; ++i) byte(p[i]);
    }

private:
    static int dataBytes(uint8_t s) {
        if (s < 0xF0) return (s & 0xE0) == 0xC0 ? 1 : 2;
        if (s == 0xF1 || s == 0xF3) return 1;
        if (s == 0xF2) return 2;
        return 0;
    }
    void byte(uint8_t b) {
        if (b >= 0xF8) return sink_(juce::MidiMessage(b));  // real time goes anywhere
        if (inSysex_) {
            if (b == 0xF7) {
                sx_.push_back(b);
                sink_(juce::MidiMessage(sx_.data(), int(sx_.size())));
                inSysex_ = false;
                return;
            }
            if (b < 0x80) return sx_.push_back(b);
            inSysex_ = false;  // a status byte ends a broken SysEx
        }
        if (b == 0xF0) {
            inSysex_ = true;
            sx_.assign(1, b);
            return;
        }
        if (b >= 0x80) {
            status_ = b;
            n_ = 0;
            if (dataBytes(b) == 0) {
                sink_(juce::MidiMessage(b));
                status_ = 0;
            }
            return;
        }
        if (!status_) return;
        data_[n_++] = b;
        if (n_ == dataBytes(status_)) {
            sink_(n_ == 1 ? juce::MidiMessage(status_, data_[0]) : juce::MidiMessage(status_, data_[0], data_[1]));
            n_ = 0;
            if (status_ >= 0xF0) status_ = 0;
        }
    }
    Sink sink_;
    bool inSysex_ = false;
    std::vector<uint8_t> sx_;
    uint8_t status_ = 0, data_[2]{};
    int n_ = 0;
};

// A pair of virtual CoreMIDI endpoints named like the device's port: a source the host reads (the
// device's output) and a destination the host writes to.
class VirtualPort : private juce::MidiInputCallback {
public:
    using Receive = std::function<void(const juce::MidiMessage&)>;
    VirtualPort(const juce::String& name, Receive onReceive) : name_(name), onReceive_(std::move(onReceive)) {
        out_ = juce::MidiOutput::createNewDevice(name);
        in_ = juce::MidiInput::createNewDevice(name, this);
        if (in_) in_->start();
    }
    ~VirtualPort() override {
        if (in_) in_->stop();
    }
    bool ok() const { return out_ != nullptr && in_ != nullptr; }
    const juce::String& name() const { return name_; }
    void send(const juce::MidiMessage& m) {
        if (out_) out_->sendMessageNow(m);
    }

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if (onReceive_) onReceive_(m);
    }
    juce::String name_;
    Receive onReceive_;
    std::unique_ptr<juce::MidiOutput> out_;
    std::unique_ptr<juce::MidiInput> in_;
};

inline juce::String hex(const juce::MidiMessage& m) {
    return juce::String::toHexString(m.getRawData(), m.getRawDataSize()).toUpperCase();
}

}  // namespace emu
