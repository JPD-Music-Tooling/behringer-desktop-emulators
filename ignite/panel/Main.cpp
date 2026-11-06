// SPDX-License-Identifier: AGPL-3.0-or-later
// ignite-panel: an emulated IGNITE front panel. It makes a pty, prints its path, and speaks the panel
// microcontroller's side of the IGNITE link on it; the IGNITE app (patched, see ../patches) opens that
// pty when IGNITE_FP_TTY names it.
//
//   ignite-panel [--control FILE]
//
// --control FILE: a scripting channel. Append "<seq> <command> [args]" lines; the panel answers in
// FILE.reply and logs "PanelControl: ok <seq>" (or "error <seq> <reason>") on stdout. Commands:
//   press NAME [MS]   down NAME   up NAME   turn NAME STEPS   shot OUT.png [SCALE]   list   lit   link
//   screenshot OUT.png (the app's screen as received)   touch X Y [X2 Y2 [MS]] (a touch on it, screen pixels)
// --screen unix:PATH: show the IGNITE app's screen (its IGNITE_SCREEN_MIRROR) in the display cut-out.
// Names: SHIFT, PLAY, PAD 1..16, ENC 1..16, A..H, JOG, ROW, BAR, ... (list prints them all).
#include <juce_gui_basics/juce_gui_basics.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "Panel.h"

namespace ignitem {

class Scripting : private juce::Timer {
public:
    Scripting(Panel& panel, Link& link, std::string path) : panel_(panel), link_(link), path_(std::move(path)) {
        std::ofstream(path_, std::ios::app).close();
        startTimer(30);
    }

private:
    void timerCallback() override {
        std::ifstream in(path_);
        in.seekg(pos_);
        std::string line;
        while (std::getline(in, line)) {
            if (in.eof() && !line.empty() && line.back() != '\n') {  // a half-written line: read it next time
            }
            pos_ = in.tellg() < 0 ? pos_ + std::streamoff(line.size() + 1) : std::streamoff(in.tellg());
            if (!line.empty()) run(line);
        }
    }

    void reply(const std::string& seq, bool ok, const std::string& text) {
        std::ofstream(path_ + ".reply") << text;
        std::cout << "PanelControl: " << (ok ? "ok " : "error ") << seq << (ok ? "" : " " + text) << std::endl;
    }

    void run(const std::string& line) {
        std::istringstream ss(line);
        std::string seq, cmd;
        ss >> seq >> cmd;
        std::vector<std::string> args;
        for (std::string a; ss >> a;) args.push_back(a);
        const auto name = [&](size_t from, size_t count) {  // names may have a space ("PAD 3")
            std::string n;
            for (size_t i = from; i < args.size() && i < from + count; ++i) n += (n.empty() ? "" : " ") + args[i];
            return n;
        };
        const auto findArg = [&](size_t& used) -> const ignitem::Control* {
            for (size_t k = std::min<size_t>(3, args.size()); k >= 1; --k)
                if (const auto* c = panel_.find(name(0, k))) return used = k, c;
            return nullptr;
        };
        size_t used = 0;
        if (cmd == "list") {
            std::string out;
            for (const auto& c : panel_.controls())
                if (c.kind != Kind::Screen) out += c.name + "\n";
            return reply(seq, true, out);
        }
        if (cmd == "lit") return reply(seq, true, panel_.litNames());
        if (cmd == "link") {
            const auto k = link_.counters();
            return reply(seq, true, std::string(link_.live() ? "LIVE" : "waiting") + " " + link_.slavePath() + " in " + std::to_string(k.framesIn) +
                                        " out " + std::to_string(k.framesOut) + " keys " + std::to_string(k.keyFrames) + " enc " + std::to_string(k.encFrames) + "\n");
        }
        if (cmd == "shot") {
            if (args.empty()) return reply(seq, false, "shot needs a file");
            const float scale = args.size() > 1 ? std::stof(args[1]) : 1.0f;
            const auto img = panel_.createComponentSnapshot(panel_.getLocalBounds(), true, scale);
            juce::File f{juce::String(args[0])};
            f.deleteFile();
            juce::FileOutputStream out(f);
            if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(img, out)) return reply(seq, false, "cannot write " + args[0]);
            return reply(seq, true, args[0] + " " + std::to_string(img.getWidth()) + "x" + std::to_string(img.getHeight()) + "\n");
        }
        if (cmd == "screenshot" || cmd == "touch") {
            auto* sv = panel_.screen();
            if (!sv) return reply(seq, false, "no screen (start the panel with --screen)");
            if (cmd == "screenshot") {
                if (args.empty()) return reply(seq, false, "screenshot needs a file");
                const auto img = sv->frameCopy();
                if (!img.isValid()) return reply(seq, false, "no frame yet");
                juce::File f{juce::String(args[0])};
                f.deleteFile();
                juce::FileOutputStream out(f);
                if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(img, out)) return reply(seq, false, "cannot write " + args[0]);
                return reply(seq, true, args[0] + " " + std::to_string(img.getWidth()) + "x" + std::to_string(img.getHeight()) + "\n");
            }
            if (args.size() < 2) return reply(seq, false, "touch X Y [X2 Y2 [MS]]");
            const juce::Point<int> from(std::stoi(args[0]), std::stoi(args[1]));
            const juce::Point<int> to = args.size() >= 4 ? juce::Point<int>(std::stoi(args[2]), std::stoi(args[3])) : from;
            const int ms = args.size() >= 5 ? std::stoi(args[4]) : 0;
            sv->scriptedTouch(from, to, ms, from == to ? 1 : 12, [this, seq] { reply(seq, true, ""); });
            return;
        }
        const auto* c = findArg(used);
        if (!c) return reply(seq, false, "no control named " + name(0, 3));
        if (cmd == "down" || cmd == "up") {
            if (c->key < 0) return reply(seq, false, c->name + " has no key");
            link_.setKey(c->key, cmd == "down");
            panel_.repaint();
            return reply(seq, true, "");
        }
        if (cmd == "press") {
            if (c->key < 0) return reply(seq, false, c->name + " has no key");
            const int ms = used < args.size() ? std::stoi(args[used]) : 120;
            const int bit = c->key;
            link_.setKey(bit, true);
            panel_.repaint();
            juce::Timer::callAfterDelay(ms, [this, bit, seq] {
                link_.setKey(bit, false);
                panel_.repaint();
                reply(seq, true, "");
            });
            return;
        }
        if (cmd == "turn") {
            if (c->enc < 0 || used >= args.size()) return reply(seq, false, "turn NAME STEPS");
            link_.turn(c->enc, std::stoi(args[used]));
            return reply(seq, true, "");
        }
        reply(seq, false, "unknown command " + cmd);
    }

    Panel& panel_;
    Link& link_;
    std::string path_;
    std::streamoff pos_ = 0;
};

class Window : public juce::DocumentWindow {
public:
    explicit Window(Panel* panel) : DocumentWindow("IGNITE panel", juce::Colours::black, DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(panel, false);
        centreWithSize(int(kCanvasW * 0.72f), int(kCanvasH * 0.72f));
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class App : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "IGNITE panel"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& cmd) override {
        const auto args = juce::StringArray::fromTokens(cmd, true);
        link_ = std::make_unique<Link>([](const std::string& s) { std::cout << s << std::endl; });
        if (!link_->open()) return quit();
        auto* panel = new Panel(*link_);
        window_ = std::make_unique<Window>(panel);
        const int i = args.indexOf("--control");
        if (i >= 0 && i + 1 < args.size()) control_ = std::make_unique<Scripting>(*panel, *link_, args[i + 1].unquoted().toStdString());
        const int sc = args.indexOf("--screen");
        if (sc >= 0 && sc + 1 < args.size()) panel->setScreenSource(args[sc + 1].unquoted());
    }
    void shutdown() override {
        control_.reset();
        window_.reset();
        link_.reset();
    }

private:
    std::unique_ptr<Link> link_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Scripting> control_;
};

}  // namespace ignitem

START_JUCE_APPLICATION(ignitem::App)
