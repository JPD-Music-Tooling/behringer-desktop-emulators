// SPDX-License-Identifier: AGPL-3.0-or-later
// bcr32-emulator: Behringer's BCR32 demo firmware (0715-AAW) running on the desktop, with a panel
// to play it, its OLED, and virtual MIDI ports where the unit's would be: USB "BCR-32 CTRL" (cable 0)
// and "BCR-32 ZAQ" (cable 1), DIN "BCR-32 MIDI IN", "BCR-32 MIDI OUT A" and "BCR-32 MIDI OUT B".
//
//   bcr32-emulator [--control FILE]
//
// --control FILE: the scripting channel (common/ControlFile.h, tools/emu_ctl). Commands:
//   press NAME [MS]   down NAME   up NAME   turn NAME DETENTS   list   lit   oled [OUT.png]
//   shot OUT.png [SCALE]   cv   cable on|off   state   ports   power
// Names: the keys by their print (PITCH ... DIR, ZQR/CTRL, T1..T4, EDIT, EXIT, STORE, LD/LRN, TAP DEL,
// TAP WRT, INST, MIX, LOOP, PLAY), BTN 1..64 for any button id, KNOB 1..32 and SELECT.
//
// What the firmware documents: buttons 1..16 are the numbered keys, T1..T4 are 18..21, the SELECT
// push is 24. The ids of the other keys, the order of the ring LEDs in the HT16K33s, and which RAM
// bits light the keys are not published: the panel's guesses are marked "assumed" (`list`), and the
// raw LED driver RAM is drawn along the bottom.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

#include "../../common/ControlFile.h"
#include "../../common/UsbMidi.h"
#include "../shim/bcr32_emu.h"

namespace bcr32 {

constexpr float kW = 1140, kH = 850;

enum class Kind { Key, Knob };
struct Control {
    std::string name, label;
    Kind kind;
    int button;              // button id 1..64, or -1
    int encoder = -1;        // 1..33
    bool assumed = false;    // id or LED order not documented
    juce::Rectangle<float> r;
    juce::Colour print = juce::Colours::white;
};

static std::vector<Control> makeControls() {
    std::vector<Control> c;
    // the 16 numbered keys, two staggered rows, their print colours as on the unit
    const char* keys[16] = {"PITCH", "VEL", "MUTE", "DELAY", "LEN", "CTRL A", "CTRL B", "WAVE",
                            "CHORD", "SKIP", "START", "SEQ LEN", "OFFSET", "DIV", "SHUFFLE", "DIR"};
    const juce::uint32 tint[16] = {0xffff5a5a, 0xffffa040, 0xffffe040, 0xffd8f040, 0xff70f070, 0xff50f0b0, 0xff50e0e0, 0xff50c8ff,
                                   0xffff5a5a, 0xffffa040, 0xffffe040, 0xffd8f040, 0xff70f070, 0xff50f0b0, 0xff50e0e0, 0xff50c8ff};
    for (int i = 0; i < 16; ++i) {
        const bool lower = i >= 8;
        const float x = (lower ? 62.0f : 112.0f) + float(i % 8) * 100.0f, y = lower ? 118.0f : 66.0f;
        juce::String label(keys[i]);
        label = label.replace("CTRL A", juce::String::fromUTF8("CTRL \xce\xb1")).replace("CTRL B", juce::String::fromUTF8("CTRL \xce\xb2"));
        c.push_back({keys[i], label.toStdString(), Kind::Key, i + 1, -1, false, {x, y, 84, 30}, juce::Colour(tint[i])});
    }
    c.push_back({"ZQR/CTRL", "ZQR/CTRL", Kind::Key, 17, -1, true, {912, 66, 84, 30}, juce::Colour(0xff50c8ff)});
    // 32 knobs, 4 rows of 8
    const char* knobs[32] = {"BPM", "CLOCK", "CLOCK OUT", "MIDI THRU", "NOTES", "SCALE", "ROOT", "TRANSPOSE",
                             "WAVE/TRIG", "GAIN/TRIG CC", "RATE/QUANT", "OFFSET/Q-MIX", "MODE T1", "MODE T2", "MODE T3", "MODE T4",
                             "UAB T1", "CH T1", "UAB T2", "CH T2", "UAB T3", "CH T3", "UAB T4", "CH T4",
                             "CTRL A T1", "CTRL B T1", "CTRL A T2", "CTRL B T2", "CTRL A T3", "CTRL B T3", "CTRL A T4", "CTRL B T4"};
    for (int i = 0; i < 32; ++i) {
        const float cx = 92.0f + float(i % 8) * 100.0f, cy = 232.0f + float(i / 8) * 128.0f;
        juce::String label(knobs[i]);
        label = label.replace("CTRL A", juce::String::fromUTF8("CTRL \xce\xb1")).replace("CTRL B", juce::String::fromUTF8("CTRL \xce\xb2"));
        c.push_back({"KNOB " + std::to_string(i + 1), label.toStdString(), Kind::Knob, 33 + i, i + 1, true, {cx - 26, cy - 26, 52, 52}});
    }
    // the right-hand section
    struct K { const char* name; int id; bool assumed; float x, y; juce::uint32 tint; };
    const K right[] = {{"T1", 18, false, 930, 236, 0xffc070ff}, {"T2", 19, false, 1040, 236, 0xffc070ff},
                       {"T3", 20, false, 930, 280, 0xffc070ff}, {"T4", 21, false, 1040, 280, 0xffc070ff},
                       {"EDIT", 22, true, 912, 438, 0xffff5a5a}, {"EXIT", 23, true, 1022, 470, 0xffff5a5a},
                       {"STORE", 25, true, 912, 508, 0xffff5a5a}, {"LD/LRN", 26, true, 1022, 540, 0xffff5a5a},
                       {"TAP DEL", 27, true, 912, 600, 0xffff5a5a}, {"TAP WRT", 28, true, 1022, 632, 0xffff5a5a},
                       {"INST", 29, true, 912, 692, 0xffff5a5a}, {"MIX", 30, true, 1022, 724, 0xffff5a5a},
                       {"LOOP", 31, true, 912, 760, 0xffff5a5a}, {"PLAY", 32, true, 1022, 792, 0xffff5a5a}};
    for (const auto& k : right) c.push_back({k.name, k.name, Kind::Key, k.id, -1, k.assumed, {k.x, k.y, 84, 30}, juce::Colour(k.tint)});
    c.push_back({"SELECT", "SELECT", Kind::Knob, 24, 33, false, {1052, 352, 60, 60}});
    return c;
}

class FirmwareThread : public juce::Thread {
public:
    FirmwareThread() : Thread("BCR32 firmware") {}
    void run() override { bcr32_run(); }
};

class Panel : public juce::Component, private juce::Timer {
public:
    Panel() : controls_(makeControls()) {
        setSize(int(kW), int(kH));
        startTimerHz(60);
    }

    const std::vector<Control>& controls() const { return controls_; }
    const Control* find(const std::string& name) const {
        for (const auto& c : controls_)
            if (juce::String(c.name).equalsIgnoreCase(juce::String(name))) return &c;
        return nullptr;
    }
    void setHalted(const juce::String& why) {
        halted_ = why;
        repaint();
    }
    const juce::String& halted() const { return halted_; }
    std::function<void()> onPowerCycle;
    const bcr32_view& view() const { return view_; }

    // the ring of a knob: chip row 1..4 = knob row, RAM bytes 2c and 2c+1 = 16 segments (assumed)
    static uint16_t ring(const bcr32_view& v, int knob) {
        const int row = (knob - 1) / 8, col = (knob - 1) % 8;
        return uint16_t(v.ram[1 + row][col * 2] | (v.ram[1 + row][col * 2 + 1] << 8));
    }
    // the activity LEDs, from App/activity_led.c (chip 0xE0)
    static bool activity(const bcr32_view& v, int i) {
        const int ram[4] = {11, 9, 9, 7}, bit[4] = {1, 1, 2, 1};
        return (v.ram[0][ram[i]] & bit[i]) != 0;
    }

    juce::Image oledImage(int scale) const {
        juce::Image img(juce::Image::RGB, BCR32_OLED_W * scale, BCR32_OLED_H * scale, true);
        juce::Graphics g(img);
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colour(0xffdff4ff));
        for (int y = 0; y < BCR32_OLED_H; ++y)
            for (int x = 0; x < BCR32_OLED_W; ++x)
                if (view_.oled_on && view_.oled[y][x]) g.fillRect(x * scale, y * scale, scale, scale);
        return img;
    }

private:
    void timerCallback() override {
        bcr32_view v;
        bcr32_view_get(&v);
        if (v.seq != view_.seq) {
            view_ = v;
            repaint();
        }
    }

    juce::AffineTransform toScreen() const {
        const float s = std::min(getWidth() / kW, getHeight() / kH);
        return juce::AffineTransform::scale(s).translated((getWidth() - kW * s) / 2, (getHeight() - kH * s) / 2);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff161618));
        g.addTransform(toScreen());
        g.setColour(juce::Colour(0xff252527));
        g.fillRoundedRectangle(2, 2, kW - 4, kH - 4, 16);
        g.setColour(juce::Colour(0xff36363a));
        g.drawRoundedRectangle(2, 2, kW - 4, kH - 4, 16, 2);

        g.setColour(juce::Colour(0xffd8d8d8));
        g.setFont(juce::Font(26.0f, juce::Font::bold));
        g.drawText("BCR32", juce::Rectangle<float>(700, 14, 200, 34), juce::Justification::centredRight);
        g.setFont(juce::Font(10.0f));
        g.setColour(juce::Colour(0xff8a8a8a));
        g.drawText("unofficial emulator", juce::Rectangle<float>(700, 44, 200, 12), juce::Justification::centredRight);
        g.drawText("ZAQUENCER/CONTROLLER", juce::Rectangle<float>(900, 130, 220, 14), juce::Justification::centredRight);

        // activity LEDs
        const char* act[4] = {"IN", "A", "B", "USB"};
        for (int i = 0; i < 4; ++i) {
            const float x = 930 + i * 44.0f, y = 168;
            g.setColour(activity(view_, i) ? juce::Colour(0xffff3030) : juce::Colour(0xff4a2222));
            g.fillEllipse(x - 6, y - 6, 12, 12);
            g.setColour(juce::Colour(0xffb0b0b0));
            g.setFont(juce::Font(9.0f, juce::Font::bold));
            g.drawText(act[i], juce::Rectangle<float>(x - 20, y + 8, 40, 12), juce::Justification::centred);
        }

        // OLED
        const juce::Rectangle<float> oled(898, 330, 128 * 1.2f, 32 * 1.2f + 12);
        g.setColour(juce::Colours::black);
        g.fillRoundedRectangle(oled.expanded(8), 6);
        g.drawImage(oledImage(1), oled.withTrimmedTop(6).withHeight(32 * 1.2f), juce::RectanglePlacement::stretchToFit);
        g.setColour(juce::Colour(0xff8a8a8a));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText("SELECT", juce::Rectangle<float>(1052, 414, 60, 12), juce::Justification::centred);
        g.drawText("GLOBAL", juce::Rectangle<float>(912, 470, 84, 12), juce::Justification::centred);
        g.drawText("PAGES/PRESETS", juce::Rectangle<float>(940, 578, 140, 12), juce::Justification::centred);
        g.drawText("NOTE RPT", juce::Rectangle<float>(1022, 664, 84, 12), juce::Justification::centred);

        for (const auto& c : controls_) c.kind == Kind::Key ? drawKey(g, c) : drawKnob(g, c);
        drawOutputs(g);
        drawLedRam(g);

        if (halted_.isNotEmpty()) {
            g.setColour(juce::Colours::black.withAlpha(0.72f));
            g.fillRoundedRectangle(2, 2, kW - 4, kH - 4, 16);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(24.0f, juce::Font::bold));
            g.drawText(halted_, juce::Rectangle<float>(0, kH / 2 - 40, kW, 30), juce::Justification::centred);
            g.setFont(juce::Font(15.0f));
            g.drawText("click to switch the unit off and on again", juce::Rectangle<float>(0, kH / 2, kW, 24), juce::Justification::centred);
        }
    }

    void drawKey(juce::Graphics& g, const Control& c) const {
        const bool held = c.button > 0 && bcr32_button(c.button);
        const auto r = c.r.reduced(held ? 1.5f : 0.0f);
        g.setColour(juce::Colour(0xff111113));
        g.fillRoundedRectangle(r.translated(0, 3), 4);
        g.setColour(held ? juce::Colour(0xff4a4a50) : juce::Colour(0xff38383d));
        g.fillRoundedRectangle(r, 4);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(r, 4, 1);
        g.setColour(c.print);
        g.setFont(juce::Font(11.0f, juce::Font::bold));
        g.drawText(juce::String::fromUTF8(c.label.c_str()), r, juce::Justification::centred);
        if (c.button >= 1 && c.button <= 16) {
            g.setColour(juce::Colour(0xff9a9a9a));
            g.setFont(juce::Font(9.0f));
            g.drawText(juce::String(c.button), juce::Rectangle<float>(c.r.getX(), c.r.getY() - 13, c.r.getWidth(), 11), juce::Justification::centred);
        }
        if (held) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawRoundedRectangle(c.r.expanded(2), 5, 1.5f);
        }
    }

    void drawKnob(juce::Graphics& g, const Control& c) const {
        const auto ctr = c.r.getCentre();
        const float rad = c.r.getWidth() / 2;
        if (c.encoder <= 32) {  // the ring: 16 segments over 270 degrees, from 7 o'clock clockwise
            const uint16_t bits = ring(view_, c.encoder);
            for (int s = 0; s < 16; ++s) {
                const float a = juce::degreesToRadians(-135.0f + 270.0f * float(s) / 15.0f);
                const auto p = ctr.getPointOnCircumference(rad + 11, a);
                g.setColour((bits >> s) & 1 ? juce::Colour(0xffff2a2a) : juce::Colour(0xff3d2526));
                g.fillRoundedRectangle(p.x - 2.5f, p.y - 2.5f, 5, 5, 1.5f);
            }
        }
        const bool held = c.button > 0 && bcr32_button(c.button);
        g.setColour(juce::Colour(0xff0d0d0e));
        g.fillEllipse(c.r.translated(0, 3));
        g.setGradientFill(juce::ColourGradient(juce::Colour(held ? 0xff3a3a3e : 0xff2c2c30), ctr.x, ctr.y - rad, juce::Colour(0xff121214), ctr.x, ctr.y + rad, false));
        g.fillEllipse(c.r);
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawEllipse(c.r, 1);
        // the pointer shows where the knob was turned to (the encoders are endless)
        const float a = juce::degreesToRadians(float(angle(c.encoder)) * 18.0f);
        g.setColour(juce::Colour(0xffd0d0d0));
        g.drawLine(juce::Line<float>(ctr.getPointOnCircumference(rad * 0.35f, a), ctr.getPointOnCircumference(rad * 0.85f, a)), 2.5f);
        if (c.encoder <= 32) {
            g.setColour(juce::Colour(0xffe0e0e0));
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8(c.label.c_str()), juce::Rectangle<float>(ctr.x - 50, ctr.y + rad + 16, 100, 12), juce::Justification::centred);
            g.setColour(juce::Colour(0xff8a8a8a));
            g.setFont(juce::Font(9.0f));
            g.drawText(juce::String(c.encoder), juce::Rectangle<float>(ctr.x + rad + 2, ctr.y - rad - 12, 20, 10), juce::Justification::centredLeft);
        }
        if (held) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawEllipse(c.r.expanded(2), 1.5f);
        }
    }

    // the rear panel's outputs: CV and gate levels, the SYNC loopback cable
    void drawOutputs(juce::Graphics& g) const {
        g.setFont(juce::Font(11.0f));
        juce::String s;
        for (int i = 0; i < 4; ++i) s << "CV" << (i + 1) << " " << juce::String(view_.cv[i], 1) << " V   ";
        for (int i = 0; i < 4; ++i) s << "GATE" << (i + 1) << " " << int(view_.gate[i]) << " V   ";
        g.setColour(juce::Colour(0xffb8b8b8));
        g.drawText(s, juce::Rectangle<float>(40, 724, 780, 14), juce::Justification::centredLeft);
        g.setColour(bcr32_sync_cable_plugged() ? juce::Colour(0xff70e070) : juce::Colour(0xff8a8a8a));
        g.drawText(cableRect_.isEmpty() ? "" : (bcr32_sync_cable_plugged() ? "SYNC OUT -> SYNC IN: cable plugged (click to pull)"
                                                                           : "SYNC OUT -> SYNC IN: no cable (click to plug)"),
                   cableRect_, juce::Justification::centredLeft);
    }

    // the five HT16K33s' display RAM, 16 bytes x 8 bits each
    void drawLedRam(juce::Graphics& g) const {
        const char* names[5] = {"0x70 keys", "0x72 row 1", "0x73 row 2", "0x74 row 3", "0x75 row 4"};
        g.setFont(juce::Font(9.0f));
        for (int c = 0; c < BCR32_LED_CHIPS; ++c) {
            const float x0 = 40 + c * 164.0f, y0 = 784;
            g.setColour(juce::Colour(0xff8a8a8a));
            g.drawText(names[c], juce::Rectangle<float>(x0, y0 - 14, 150, 11), juce::Justification::centredLeft);
            for (int b = 0; b < 16; ++b)
                for (int bit = 0; bit < 8; ++bit) {
                    g.setColour((view_.ram[c][b] >> bit) & 1 ? juce::Colour(0xffff3a3a) : juce::Colour(0xff3a3034));
                    g.fillRect(x0 + b * 9.0f, y0 + bit * 6.0f, 7.0f, 4.0f);
                }
        }
    }

    int& angle(int encoder) const { return angles_[size_t(encoder)]; }

    const Control* hit(juce::Point<float> p) const {
        const auto q = p.transformedBy(toScreen().inverted());
        for (const auto& c : controls_)
            if (c.r.expanded(4).contains(q)) return &c;
        return nullptr;
    }
public:
    void turn(const Control& c, int detents) {
        bcr32_turn(c.encoder, detents);
        angle(c.encoder) += detents;
        repaint();
    }

private:
    void mouseDown(const juce::MouseEvent& e) override {
        if (halted_.isNotEmpty()) {
            if (onPowerCycle) onPowerCycle();
            return;
        }
        if (cableRect_.contains(e.position.transformedBy(toScreen().inverted()))) {
            bcr32_sync_cable(!bcr32_sync_cable_plugged());
            return repaint();
        }
        active_ = hit(e.position);
        dragged_ = 0;
        if (active_ && active_->kind == Kind::Key) {
            if (e.mods.isAltDown()) bcr32_set_button(active_->button, !bcr32_button(active_->button));  // alt-click latches
            else bcr32_set_button(active_->button, 1), pressed_ = active_->button;
            repaint();
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!active_ || active_->kind != Kind::Knob) return;
        const int target = -e.getDistanceFromDragStartY() / 6;  // a detent per 6 pixels, up is clockwise
        if (target != dragged_) {
            turn(*active_, target - dragged_);
            dragged_ = target;
        }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (active_ && active_->kind == Kind::Knob && dragged_ == 0 && !e.mouseWasDraggedSinceMouseDown() && active_->button > 0) {
            const int id = active_->button;  // a click on a knob pushes it
            bcr32_set_button(id, 1);
            juce::Timer::callAfterDelay(120, [id] { bcr32_set_button(id, 0); });
        }
        if (pressed_ > 0) bcr32_set_button(pressed_, 0);
        pressed_ = -1;
        active_ = nullptr;
        repaint();
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (const auto* c = hit(e.position); c && c->kind == Kind::Knob) {
            wheel_ += w.deltaY * 12;
            const int n = int(wheel_);
            if (n != 0) {
                turn(*c, n);
                wheel_ -= float(n);
            }
        }
    }

    std::vector<Control> controls_;
    bcr32_view view_{};
    juce::String halted_;
    const Control* active_ = nullptr;
    int pressed_ = -1, dragged_ = 0;
    float wheel_ = 0;
    mutable int angles_[BCR32_ENCODERS + 1]{};
    juce::Rectangle<float> cableRect_{40, 744, 420, 14};
};

class Window : public juce::DocumentWindow {
public:
    explicit Window(Panel* panel) : DocumentWindow("BCR32 (emulated)", juce::Colours::black, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(panel, false);
        centreWithSize(int(kW * 0.85f), int(kH * 0.85f));
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class App : public juce::JUCEApplication, private juce::MidiInputCallback {
public:
    const juce::String getApplicationName() override { return "BCR32 emulator"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& cmd) override {
        const auto args = juce::StringArray::fromTokens(cmd, true);
        usbParser_ = std::make_unique<emu::PacketParser>([this](int cable, const juce::MidiMessage& m) {
            if (cable < 2 && usb_[cable]) usb_[cable]->send(m);
        });
        for (int p = 0; p < 2; ++p)
            dinParser_[p] = std::make_unique<emu::ByteParser>([this, p](const juce::MidiMessage& m) {
                if (dinOut_[p]) dinOut_[p]->sendMessageNow(m);
            });
        const char* names[2] = {"BCR-32 CTRL", "BCR-32 ZAQ"};
        for (int i = 0; i < 2; ++i) {
            usb_[i] = std::make_unique<emu::VirtualPort>(names[i], [i](const juce::MidiMessage& m) {
                const auto p = emu::toPackets(m, i);
                if (!p.empty()) bcr32_usb_in(p.data(), uint32_t(p.size()));
            });
            std::cout << "BCR32: port \"" << names[i] << "\" " << (usb_[i]->ok() ? "open" : "FAILED") << std::endl;
        }
        dinIn_ = juce::MidiInput::createNewDevice("BCR-32 MIDI IN", this);
        if (dinIn_) dinIn_->start();
        dinOut_[0] = juce::MidiOutput::createNewDevice("BCR-32 MIDI OUT A");
        dinOut_[1] = juce::MidiOutput::createNewDevice("BCR-32 MIDI OUT B");
        std::cout << "BCR32: DIN ports " << (dinIn_ && dinOut_[0] && dinOut_[1] ? "open" : "FAILED") << std::endl;

        bcr32_set_callbacks([](void* ctx, const uint8_t* p, uint32_t n) { static_cast<App*>(ctx)->usbParser_->feed(p, n); },
                            [](void* ctx, int port, const uint8_t* b, uint32_t n) {
                                if (port >= 0 && port < 2) static_cast<App*>(ctx)->dinParser_[port]->feed(b, n);
                            },
                            [](void* ctx, const char* what) {
                                const std::string w(what);
                                std::cout << "BCR32: " << w << std::endl;
                                juce::MessageManager::callAsync([app = static_cast<App*>(ctx)] { app->panel_->setHalted("Bootloader requested"); });
                            },
                            this);

        panel_ = new Panel();
        panel_->onPowerCycle = [] { emu::powerCycle(); };
        window_ = std::make_unique<Window>(panel_);
        const int i = args.indexOf("--control");
        if (i >= 0 && i + 1 < args.size())
            control_ = std::make_unique<emu::ControlFile>(args[i + 1].unquoted().toStdString(), [this](const auto& c) { command(c); });
        firmware_.startThread();
        std::cout << "BCR32: firmware running" << std::endl;
    }

    void shutdown() override {
        bcr32_stop();
        firmware_.stopThread(3000);
        control_.reset();
        window_.reset();
        if (dinIn_) dinIn_->stop();
    }

private:
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        bcr32_din_in(m.getRawData(), uint32_t(m.getRawDataSize()));
    }

    void command(const emu::ControlFile::Command& c) {
        auto reply = [&](bool ok, const std::string& text = {}) { control_->reply(c.seq, ok, text); };
        const auto& v = panel_->view();
        if (c.cmd == "list") {
            std::string out;
            for (const auto& k : panel_->controls())
                out += k.name + (k.button > 0 ? "  button " + std::to_string(k.button) : "") + (k.encoder > 0 ? "  encoder " + std::to_string(k.encoder) : "") +
                       (k.assumed ? "  (assumed)" : "") + "\n";
            return reply(true, out);
        }
        if (c.cmd == "lit") {
            std::string out;
            const char* act[4] = {"IN", "A", "B", "USB"};
            for (int i = 0; i < 4; ++i)
                if (Panel::activity(v, i)) out += std::string("activity ") + act[i] + "\n";
            for (int k = 1; k <= 32; ++k)
                if (const auto r = Panel::ring(v, k)) out += "KNOB " + std::to_string(k) + " ring " + juce::String::toHexString(int(r)).paddedLeft('0', 4).toStdString() + "\n";
            for (int ch = 0; ch < BCR32_LED_CHIPS; ++ch)
                for (int b = 0; b < 16; ++b)
                    if (v.ram[ch][b]) out += "chip " + std::to_string(ch) + " ram " + std::to_string(b) + " " + juce::String::toHexString(int(v.ram[ch][b])).toStdString() + "\n";
            return reply(true, out);
        }
        if (c.cmd == "oled") {
            if (!c.args.empty()) {
                if (!emu::writePng(panel_->oledImage(4), c.args[0])) return reply(false, "cannot write " + c.args[0]);
                return reply(true, c.args[0] + "\n");
            }
            std::string out;  // the screen as text, two rows per line
            for (int y = 0; y < BCR32_OLED_H; y += 2) {
                for (int x = 0; x < BCR32_OLED_W; ++x) {
                    const int top = v.oled_on && v.oled[y][x], bottom = v.oled_on && v.oled[y + 1][x];
                    out += top && bottom ? "\xe2\x96\x88" : top ? "\xe2\x96\x80" : bottom ? "\xe2\x96\x84" : " ";
                }
                out += "\n";
            }
            return reply(true, out);
        }
        if (c.cmd == "cv") {
            std::string out;
            for (int i = 0; i < 4; ++i) out += "CV" + std::to_string(i + 1) + " " + juce::String(v.cv[i], 1).toStdString() + " V\n";
            for (int i = 0; i < 4; ++i) out += "GATE" + std::to_string(i + 1) + " " + std::to_string(v.gate[i]) + " V\n";
            return reply(true, out);
        }
        if (c.cmd == "cable") {
            bcr32_sync_cable(!c.args.empty() && c.args[0] == "on");
            panel_->repaint();
            return reply(true);
        }
        if (c.cmd == "state") return reply(true, (panel_->halted().isEmpty() ? std::string("running") : panel_->halted().toStdString()) + "\n");
        if (c.cmd == "ports") {
            std::string out;
            for (auto& p : usb_) out += p->name().toStdString() + "\n";
            out += "BCR-32 MIDI IN\nBCR-32 MIDI OUT A\nBCR-32 MIDI OUT B\n";
            return reply(true, out);
        }
        if (c.cmd == "power") {
            reply(true);
            return emu::powerCycle();
        }
        if (c.cmd == "shot") {
            if (c.args.empty()) return reply(false, "shot needs a file");
            const float scale = c.args.size() > 1 ? std::stof(c.args[1]) : 1.0f;
            if (!emu::writePng(panel_->createComponentSnapshot(panel_->getLocalBounds(), true, scale), c.args[0])) return reply(false, "cannot write " + c.args[0]);
            return reply(true, c.args[0] + "\n");
        }
        // a control: by name, or BTN n for any button id
        size_t used = 0;
        const Control* k = nullptr;
        int button = -1;
        if (!c.args.empty() && juce::String(c.args[0]).equalsIgnoreCase("BTN") && c.args.size() > 1) {
            button = std::stoi(c.args[1]);
            used = 2;
        } else {
            for (size_t n = std::min<size_t>(3, c.args.size()); n >= 1 && !k; --n)
                if ((k = panel_->find(c.words(0, n)))) used = n;
            if (!k) return reply(false, "no control named " + c.words(0, 3));
            button = k->button;
        }
        if (c.cmd == "turn") {
            if (!k || k->encoder < 0 || used >= c.args.size()) return reply(false, "turn KNOB n|SELECT DETENTS");
            panel_->turn(*k, std::stoi(c.args[used]));
            return reply(true);
        }
        if (button < 1 || button > BCR32_BUTTONS) return reply(false, "no button");
        if (c.cmd == "down" || c.cmd == "up") {
            bcr32_set_button(button, c.cmd == "down");
            panel_->repaint();
            return reply(true);
        }
        if (c.cmd == "press") {
            const int ms = used < c.args.size() ? std::stoi(c.args[used]) : 120;
            bcr32_set_button(button, 1);
            panel_->repaint();
            const std::string seq = c.seq;
            juce::Timer::callAfterDelay(ms, [this, button, seq] {
                bcr32_set_button(button, 0);
                panel_->repaint();
                control_->reply(seq, true);
            });
            return;
        }
        reply(false, "unknown command " + c.cmd);
    }

    FirmwareThread firmware_;
    std::unique_ptr<emu::VirtualPort> usb_[2];
    std::unique_ptr<juce::MidiInput> dinIn_;
    std::unique_ptr<juce::MidiOutput> dinOut_[2];
    std::unique_ptr<emu::PacketParser> usbParser_;
    std::unique_ptr<emu::ByteParser> dinParser_[2];
    Panel* panel_ = nullptr;
    std::unique_ptr<Window> window_;
    std::unique_ptr<emu::ControlFile> control_;
};

}  // namespace bcr32

START_JUCE_APPLICATION(bcr32::App)
