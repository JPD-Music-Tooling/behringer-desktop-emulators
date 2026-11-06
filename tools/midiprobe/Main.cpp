// SPDX-License-Identifier: AGPL-3.0-or-later
// midiprobe: play the host on a MIDI port, for testing the emulators from scripts.
//
//   midiprobe list                               the computer's MIDI sources and destinations
//   midiprobe send PORT HEX...                   send one message to PORT
//   midiprobe listen PORT SECONDS                print what PORT sends, one message per line
//   midiprobe ask PORT SECONDS HEX...            send, then print what comes back for SECONDS
//
// PORT is a name or the start of one ("AQ64L DAW"). HEX bytes may be separated by spaces or not.
#include <juce_audio_devices/juce_audio_devices.h>

#include <iostream>
#include <mutex>

namespace {

juce::MidiDeviceInfo findDevice(const juce::Array<juce::MidiDeviceInfo>& list, const juce::String& name) {
    for (const auto& d : list)
        if (d.name == name) return d;
    for (const auto& d : list)
        if (d.name.startsWithIgnoreCase(name)) return d;
    return {};
}

juce::MemoryBlock parseHex(const juce::StringArray& words) {
    juce::MemoryBlock b;
    b.loadFromHexString(words.joinIntoString(""));
    return b;
}

struct Printer : juce::MidiInputCallback {
    std::mutex lock;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        std::lock_guard<std::mutex> g(lock);
        std::cout << juce::String::toHexString(m.getRawData(), m.getRawDataSize()).toUpperCase() << std::endl;
    }
};

int run(const juce::StringArray& a) {
    if (a.isEmpty() || a[0] == "-h" || a[0] == "--help") {
        std::cout << "midiprobe list | send PORT HEX... | listen PORT SECONDS | ask PORT SECONDS HEX...\n";
        return 2;
    }
    if (a[0] == "list") {
        for (const auto& d : juce::MidiInput::getAvailableDevices()) std::cout << "source      " << d.name << "\n";
        for (const auto& d : juce::MidiOutput::getAvailableDevices()) std::cout << "destination " << d.name << "\n";
        return 0;
    }
    if (a.size() < 2) return 2;
    std::unique_ptr<juce::MidiInput> in;
    Printer printer;
    double seconds = 0;
    juce::StringArray hex;
    if (a[0] == "listen" || a[0] == "ask") {
        const auto d = findDevice(juce::MidiInput::getAvailableDevices(), a[1]);
        if (d.identifier.isEmpty()) return std::cerr << "midiprobe: no source " << a[1] << "\n", 1;
        in = juce::MidiInput::openDevice(d.identifier, &printer);
        if (!in) return std::cerr << "midiprobe: cannot open " << d.name << "\n", 1;
        in->start();
        seconds = a[2].getDoubleValue();
        for (int i = 3; i < a.size(); ++i) hex.add(a[i]);
    } else if (a[0] == "send") {
        for (int i = 2; i < a.size(); ++i) hex.add(a[i]);
    } else {
        return std::cerr << "midiprobe: unknown command " << a[0] << "\n", 2;
    }
    if (!hex.isEmpty()) {
        const auto d = findDevice(juce::MidiOutput::getAvailableDevices(), a[1]);
        if (d.identifier.isEmpty()) return std::cerr << "midiprobe: no destination " << a[1] << "\n", 1;
        auto out = juce::MidiOutput::openDevice(d.identifier);
        if (!out) return std::cerr << "midiprobe: cannot open " << d.name << "\n", 1;
        const auto bytes = parseHex(hex);
        const auto* p = static_cast<const juce::uint8*>(bytes.getData());
        out->sendMessageNow(p[0] == 0xF0 ? juce::MidiMessage::createSysExMessage(p + 1, int(bytes.getSize()) - 2)
                                         : juce::MidiMessage(p, int(bytes.getSize())));
        juce::Thread::sleep(30);  // CoreMIDI delivers asynchronously
    }
    if (seconds > 0) juce::Thread::sleep(int(seconds * 1000));
    if (in) in->stop();
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray a;
    for (int i = 1; i < argc; ++i) a.add(argv[i]);
    return run(a);
}
