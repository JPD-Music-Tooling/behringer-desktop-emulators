// SPDX-License-Identifier: AGPL-3.0-or-later
// aq64l-emulator: Behringer's AQ64L demo firmware (0715-AAX) running on the desktop, with a panel to
// play it and two virtual MIDI ports where the USB ports would be: "AQ64L DAW Control" (cable 0) and
// "AQ64L User MIDI" (cable 1).
//
//   aq64l-emulator [--control FILE]
//
// --control FILE: the scripting channel (common/ControlFile.h, tools/emu_ctl). Commands:
//   press NAME [MS]   down NAME   up NAME   list   lit   shot OUT.png [SCALE]   state   ports   power
// Names: UP DOWN LEFT RIGHT SESSION USER1 USER2 MIXER, VOL PAN SNDA SNDB STOP TRACK SOLO ARM, and
// PAD X Y for the pads (X 1..8 from the left, Y 1..8 from the bottom: PAD 1 1 sends note 36).
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <iostream>

#include "../../common/ControlFile.h"
#include "../../common/UsbMidi.h"
#include "../shim/aq64l_emu.h"

namespace aq64l {

// The panel, in the coordinates of Behringer's top view (docs/images/views.png in 0715-AAX), the
// body's top-left corner at 0,0.
constexpr float kW = 900, kH = 465;

enum class Kind { Top, Pad, Func };
struct Control {
    std::string name, label;
    Kind kind;
    int key, led, note = -1;
    juce::Rectangle<float> r;
};

static std::vector<Control> makeControls() {
    std::vector<Control> c;
    const float colX[8] = {81, 173, 266, 359, 451, 544, 636, 728};
    const char* top[8] = {"UP", "DOWN", "LEFT", "RIGHT", "SESSION", "USER1", "USER2", "MIXER"};
    const char* topLabel[8] = {"\xe2\x96\xb2", "\xe2\x96\xbc", "\xe2\x97\x80", "\xe2\x96\xb6", "SESSION", "USER 1", "USER 2", "MIXER"};
    for (int i = 0; i < 8; ++i) c.push_back({top[i], topLabel[i], Kind::Top, i, i, -1, {colX[i] - 26, 40, 52, 27}});
    for (int row = 0; row < 8; ++row) {
        const float y = 106 + row * 43.2f;
        for (int col = 0; col < 8; ++col)
            c.push_back({"PAD " + std::to_string(col + 1) + " " + std::to_string(8 - row), "", Kind::Pad, (row + 1) * 9 + col, row * 9 + col + 8,
                         36 + (7 - row) * 8 + col, {colX[col] - 39, y - 14, 78, 28}});
    }
    const char* fn[8] = {"VOL", "PAN", "SNDA", "SNDB", "STOP", "TRACK", "SOLO", "ARM"};
    const char* fnLabel[8] = {"VOL", "PAN", "SND A", "SND B", "STOP", "TRACK", "SOLO", "ARM"};
    for (int n = 0; n < 8; ++n) c.push_back({fn[n], fnLabel[n], Kind::Func, (n + 1) * 9 + 8, (n + 1) * 9 + 7, -1, {792, 106 + n * 43.2f - 14, 52, 28}});
    return c;
}

class FirmwareThread : public juce::Thread {
public:
    FirmwareThread() : Thread("AQ64L firmware") {}
    void run() override { aq64l_run(); }
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

    juce::Colour ledColour(int led) const {
        const uint8_t* p = &rgb_[size_t(led) * 3];
        return juce::Colour(p[0], p[1], p[2]);
    }

    std::string lit() const {
        std::string out;
        for (const auto& c : controls_) {
            const auto col = ledColour(c.led);
            if (col.getARGB() == 0xFF000000u) continue;
            out += c.name + " " + col.toDisplayString(false).toStdString() + (aq64l_key(c.key) ? " (held)" : "") + "\n";
        }
        return out;
    }

private:
    void timerCallback() override {
        uint32_t seq = 0;
        uint8_t now[AQ64L_LEDS * 3];
        aq64l_leds(now, &seq);
        if (seq != seq_) {
            seq_ = seq;
            std::memcpy(rgb_, now, sizeof(rgb_));
            repaint();
        }
    }

    juce::AffineTransform toScreen() const {
        const float s = std::min(getWidth() / kW, getHeight() / kH);
        return juce::AffineTransform::scale(s).translated((getWidth() - kW * s) / 2, (getHeight() - kH * s) / 2);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff1b1b1d));
        g.addTransform(toScreen());
        g.setColour(juce::Colour(0xff2b2b2e));
        g.fillRoundedRectangle(2, 2, kW - 4, kH - 4, 18);
        g.setColour(juce::Colour(0xff3a3a3e));
        g.drawRoundedRectangle(2, 2, kW - 4, kH - 4, 18, 2);

        g.setColour(juce::Colour(0xffd8d8d8));
        g.setFont(juce::Font(17.0f, juce::Font::bold));
        g.drawText("AQ64L", juce::Rectangle<float>(760, 26, 110, 20), juce::Justification::centredRight);
        g.setFont(juce::Font(9.0f));
        g.setColour(juce::Colour(0xff8a8a8a));
        g.drawText("unofficial emulator", juce::Rectangle<float>(740, 46, 130, 12), juce::Justification::centredRight);

        // the "MODE" bracket over the arrows and the line joining the mode keys, as printed
        g.setColour(juce::Colour(0xff9a9a9a));
        g.drawLine(107, 53, 147, 53, 1);
        g.drawLine(477, 53, 518, 53, 1);
        g.drawLine(570, 53, 610, 53, 1);
        g.drawLine(662, 53, 702, 53, 1);
        g.setFont(juce::Font(8.5f, juce::Font::bold));
        g.drawText("MODE", juce::Rectangle<float>(107, 70, 40, 10), juce::Justification::centred);

        for (const auto& c : controls_) drawControl(g, c);
        g.setColour(juce::Colours::white.withAlpha(0.85f));  // the dot at the centre of the grid
        g.fillEllipse(405 - 3.5f, 106 + 3.5f * 43.2f - 3.5f, 7, 7);

        if (halted_.isNotEmpty()) {
            g.setColour(juce::Colours::black.withAlpha(0.72f));
            g.fillRoundedRectangle(2, 2, kW - 4, kH - 4, 18);
            g.setColour(juce::Colours::white);
            g.setFont(juce::Font(22.0f, juce::Font::bold));
            g.drawText(halted_, juce::Rectangle<float>(0, kH / 2 - 40, kW, 30), juce::Justification::centred);
            g.setFont(juce::Font(14.0f));
            g.drawText("click to switch the unit off and on again", juce::Rectangle<float>(0, kH / 2, kW, 24), juce::Justification::centred);
        }
    }

    // a small filled triangle pointing up, turned by angle (clockwise)
    static juce::Path triangle(float x, float y, float angle, float size = 4.0f) {
        juce::Path p;
        p.addTriangle(0, -size, size, size * 0.8f, -size, size * 0.8f);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(x, y));
        return p;
    }

    void drawControl(juce::Graphics& g, const Control& c) const {
        const auto col = ledColour(c.led);
        const bool on = col.getARGB() != 0xFF000000u;
        const bool held = aq64l_key(c.key) != 0;
        auto r = c.r.reduced(held ? 1.5f : 0.0f);
        if (c.kind == Kind::Pad) {
            // an unlit pad is milky grey; a lit one glows in its LED colour (shown at full scale: the
            // unit itself dims every LED to 128/255)
            const auto base = juce::Colour(0xff55575b);
            const auto face = on ? col.withMultipliedBrightness(1.0f).interpolatedWith(juce::Colours::white, 0.12f) : base;
            if (on) {
                g.setColour(col.withAlpha(0.35f));
                g.fillRoundedRectangle(r.expanded(4), 7);
            }
            g.setColour(face);
            g.fillRoundedRectangle(r, 4);
            g.setColour(juce::Colours::white.withAlpha(on ? 0.25f : 0.08f));
            g.drawRoundedRectangle(r, 4, 1);
        } else {
            // white keys; a lit key takes on its LED colour
            const auto face = on ? juce::Colours::white.interpolatedWith(col, 0.7f) : juce::Colour(0xffe9e9e9);
            if (on) {
                g.setColour(col.withAlpha(0.4f));
                g.fillRoundedRectangle(r.expanded(4), 6);
            }
            g.setColour(face);
            g.fillRoundedRectangle(r, 4);
            g.setColour(juce::Colours::black.withAlpha(held ? 0.35f : 0.15f));
            g.drawRoundedRectangle(r, 4, 1);
            g.setColour(juce::Colour(0xffcfcfcf));
            g.setFont(juce::Font(8.5f, juce::Font::bold));
            if (c.kind == Kind::Top && c.key < 4) {  // the arrows: up, down, left, right
                const float turn[4] = {0.0f, 2.0f, 3.0f, 1.0f};  // quarter turns from "up"
                g.fillPath(triangle(c.r.getCentreX(), 75, turn[c.key] * juce::MathConstants<float>::halfPi));
            } else if (c.kind == Kind::Top) {
                g.drawText(c.label, juce::Rectangle<float>(c.r.getCentreX() - 40, 70, 80, 10), juce::Justification::centred);
            } else {
                g.fillPath(triangle(858, c.r.getY() + 3, juce::MathConstants<float>::halfPi, 3.0f));
                g.drawText(c.label, juce::Rectangle<float>(855, c.r.getY() + 9, 40, 10), juce::Justification::centredLeft);
            }
        }
        if (held) {
            g.setColour(juce::Colours::white.withAlpha(0.5f));
            g.drawRoundedRectangle(c.r.expanded(2), 5, 1.5f);
        }
    }

    const Control* hit(juce::Point<float> p) const {
        const auto q = p.transformedBy(toScreen().inverted());
        for (const auto& c : controls_)
            if (c.r.expanded(3).contains(q)) return &c;
        return nullptr;
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (halted_.isNotEmpty()) {
            if (onPowerCycle) onPowerCycle();
            return;
        }
        if (const auto* c = hit(e.position)) {
            if (e.mods.isAltDown()) {  // alt-click latches a key, to hold two at once
                aq64l_set_key(c->key, !aq64l_key(c->key));
                latched_ = true;
            } else {
                aq64l_set_key(c->key, 1);
                pressed_ = c->key;
            }
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (pressed_ >= 0) aq64l_set_key(pressed_, 0);
        pressed_ = -1;
        repaint();
    }

    std::vector<Control> controls_;
    uint8_t rgb_[AQ64L_LEDS * 3]{};
    uint32_t seq_ = ~0u;
    int pressed_ = -1;
    bool latched_ = false;
    juce::String halted_;
};

class Window : public juce::DocumentWindow {
public:
    explicit Window(Panel* panel) : DocumentWindow("AQ64L (emulated)", juce::Colours::black, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(panel, false);
        centreWithSize(int(kW * 1.1f), int(kH * 1.1f));
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class App : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "AQ64L emulator"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& cmd) override {
        const auto args = juce::StringArray::fromTokens(cmd, true);
        // device -> host: USB-MIDI packets from the firmware, out on the port of their cable
        parser_ = std::make_unique<emu::PacketParser>([this](int cable, const juce::MidiMessage& m) {
            if (cable < 2 && ports_[cable]) ports_[cable]->send(m);
            ++sent_;
        });
        const char* names[2] = {"AQ64L DAW Control", "AQ64L User MIDI"};
        for (int i = 0; i < 2; ++i) {
            ports_[i] = std::make_unique<emu::VirtualPort>(names[i], [i](const juce::MidiMessage& m) {
                const auto p = emu::toPackets(m, i);
                if (!p.empty()) aq64l_host_packets(p.data(), uint16_t(p.size()));
            });
            std::cout << "AQ64L: port \"" << names[i] << "\" " << (ports_[i]->ok() ? "open" : "FAILED") << std::endl;
        }
        aq64l_set_callbacks(
            [](void* ctx, const uint8_t* p, uint16_t n) { static_cast<App*>(ctx)->parser_->feed(p, n); },
            [](void* ctx, const char* what) {
                const std::string w(what);
                std::cout << "AQ64L: " << w << std::endl;
                juce::MessageManager::callAsync([app = static_cast<App*>(ctx), w] {
                    app->panel_->setHalted(w == "bootloader" ? "Bootloader requested (SysEx 0x77)" : "Reset");
                });
            },
            this);

        panel_ = new Panel();
        panel_->onPowerCycle = [] { emu::powerCycle(); };
        window_ = std::make_unique<Window>(panel_);
        const int i = args.indexOf("--control");
        if (i >= 0 && i + 1 < args.size())
            control_ = std::make_unique<emu::ControlFile>(args[i + 1].unquoted().toStdString(), [this](const auto& c) { command(c); });
        firmware_.startThread();
        std::cout << "AQ64L: firmware running" << std::endl;
    }

    void shutdown() override {
        aq64l_stop();
        firmware_.stopThread(3000);
        control_.reset();
        window_.reset();
        for (auto& p : ports_) p.reset();
    }

private:
    void command(const emu::ControlFile::Command& c) {
        auto reply = [&](bool ok, const std::string& text = {}) { control_->reply(c.seq, ok, text); };
        if (c.cmd == "list") {
            std::string out;
            for (const auto& k : panel_->controls()) out += k.name + (k.note >= 0 ? "  note " + std::to_string(k.note) : "") + "\n";
            return reply(true, out);
        }
        if (c.cmd == "lit") return reply(true, panel_->lit());
        if (c.cmd == "state") return reply(true, (panel_->halted().isEmpty() ? std::string("running") : panel_->halted().toStdString()) + "\n");
        if (c.cmd == "ports") {
            std::string out;
            for (auto& p : ports_) out += p->name().toStdString() + (p->ok() ? "" : " (failed)") + "\n";
            return reply(true, out);
        }
        if (c.cmd == "power") {
            reply(true);
            return emu::powerCycle();
        }
        if (c.cmd == "shot") {
            if (c.args.empty()) return reply(false, "shot needs a file");
            const float scale = c.args.size() > 1 ? std::stof(c.args[1]) : 1.0f;
            const auto img = panel_->createComponentSnapshot(panel_->getLocalBounds(), true, scale);
            if (!emu::writePng(img, c.args[0])) return reply(false, "cannot write " + c.args[0]);
            return reply(true, c.args[0] + "\n");
        }
        size_t used = 0;
        const Control* k = nullptr;
        for (size_t n = std::min<size_t>(3, c.args.size()); n >= 1 && !k; --n)
            if ((k = panel_->find(c.words(0, n)))) used = n;
        if (!k) return reply(false, "no control named " + c.words(0, 3));
        if (c.cmd == "down" || c.cmd == "up") {
            aq64l_set_key(k->key, c.cmd == "down");
            panel_->repaint();
            return reply(true);
        }
        if (c.cmd == "press") {
            const int ms = used < c.args.size() ? std::stoi(c.args[used]) : 120;
            aq64l_set_key(k->key, 1);
            panel_->repaint();
            const int key = k->key;
            const std::string seq = c.seq;
            juce::Timer::callAfterDelay(ms, [this, key, seq] {
                aq64l_set_key(key, 0);
                panel_->repaint();
                control_->reply(seq, true);
            });
            return;
        }
        reply(false, "unknown command " + c.cmd);
    }

    FirmwareThread firmware_;
    std::unique_ptr<emu::VirtualPort> ports_[2];
    std::unique_ptr<emu::PacketParser> parser_;
    std::atomic<int> sent_{0};
    Panel* panel_ = nullptr;
    std::unique_ptr<Window> window_;
    std::unique_ptr<emu::ControlFile> control_;
};

}  // namespace aq64l

START_JUCE_APPLICATION(aq64l::App)
