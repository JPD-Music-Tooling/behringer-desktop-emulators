// SPDX-License-Identifier: AGPL-3.0-or-later
// The scripting channel every emulated panel offers (tools/emu_ctl writes to it): a text file the
// panel polls for "<seq> <command> [args]" lines. Each command is answered in FILE.reply, and a line
// "PanelControl: ok <seq>" (or "error <seq> <reason>") on stdout tells emu_ctl it is done.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <unistd.h>

#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace emu {

class ControlFile : private juce::Timer {
public:
    struct Command {
        std::string seq, cmd;
        std::vector<std::string> args;
        // the words from..from+count joined with spaces: control names may have spaces ("PAD 3")
        std::string words(size_t from, size_t count) const {
            std::string n;
            for (size_t i = from; i < args.size() && i < from + count; ++i) n += (n.empty() ? "" : " ") + args[i];
            return n;
        }
    };
    using Handler = std::function<void(const Command&)>;

    // Starts reading at the end of the file, so a panel restarted in place does not replay old commands.
    ControlFile(std::string path, Handler handler) : path_(std::move(path)), handler_(std::move(handler)) {
        std::ofstream(path_, std::ios::app).close();
        std::ifstream in(path_, std::ios::ate);
        pos_ = in.tellg() < 0 ? 0 : std::streamoff(in.tellg());
        startTimer(30);
    }

    void reply(const std::string& seq, bool ok, const std::string& text = {}) const {
        std::ofstream(path_ + ".reply") << text;
        std::cout << "PanelControl: " << (ok ? "ok " : "error ") << seq << (ok ? "" : " " + text) << std::endl;
    }

private:
    void timerCallback() override {
        std::ifstream in(path_);
        in.seekg(pos_);
        std::string rest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        size_t start = 0;
        for (size_t nl; (nl = rest.find('\n', start)) != std::string::npos; start = nl + 1) {  // whole lines only
            const std::string line = rest.substr(start, nl - start);
            if (line.empty()) continue;
            std::istringstream ss(line);
            Command c;
            ss >> c.seq >> c.cmd;
            for (std::string a; ss >> a;) c.args.push_back(a);
            handler_(c);
        }
        pos_ += std::streamoff(start);
    }

    std::string path_;
    Handler handler_;
    std::streamoff pos_ = 0;
};

inline bool writePng(const juce::Image& img, const std::string& path) {
    juce::File f{juce::String(path)};
    f.deleteFile();
    juce::FileOutputStream out(f);
    return out.openedOk() && juce::PNGImageFormat().writeImageToStream(img, out);
}

// "Power cycle": replace this process with a fresh copy of itself (same pid, same arguments), as a
// real unit comes back from its bootloader only by being switched off and on.
inline void powerCycle() {
    const auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getFullPathName().toStdString();
    std::vector<std::string> words{exe};
    for (const auto& a : juce::JUCEApplicationBase::getCommandLineParameterArray()) words.push_back(a.toStdString());
    std::vector<char*> argv;
    for (auto& w : words) argv.push_back(w.data());
    argv.push_back(nullptr);
    std::cout << "Panel: power cycle" << std::endl;
    ::execv(exe.c_str(), argv.data());
    std::cout << "Panel: power cycle failed" << std::endl;
}

}  // namespace emu
