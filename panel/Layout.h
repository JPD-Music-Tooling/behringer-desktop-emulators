// SPDX-License-Identifier: AGPL-3.0-or-later
// The IGNITE front panel: every control, where it sits, and how the app knows it.
// Positions are in canvas units: the IGNITE photo (docs/images/ignite-front.jpg in 0716-AAF) at
// 2000 x 1196, shifted so the panel's top left corner is near (0, 0).
// Protocol facts are from FrontPannelManager.cpp / MCUInterfaceParams.h / FrontPannelLed.cpp:
//   key   = the bit in the 12-byte key bitmap (E1), LSB first
//   enc   = the encoder index in E4 (88 jog, 89-96 knobs A-H, 97 ROW, 98-113 step encoders, 114 BAR)
//   led   = (com, row) in the 16-byte LED matrix (E2): byte = row, bit = com
#pragma once
#include <string>
#include <vector>

namespace ignitem {

constexpr float kCanvasW = 1660, kCanvasH = 1075;
constexpr float kOX = 170, kOY = 100;  // photo coordinates minus this = canvas coordinates

enum class Kind { Key, Pad, Encoder, Knob, Jog, RowBar, Screen };

struct Led { int com = -1, row = -1; };

struct Control {
    std::string name;      // what the scripting channel and the logs call it
    std::string label;     // printed on the panel
    Kind kind = Kind::Key;
    int key = -1;          // bit in the key bitmap (push, for encoders), -1 none
    int enc = -1;          // encoder index, -1 none
    float x = 0, y = 0, w = 0, h = 0;  // canvas units (centre and radius in w for round ones)
    std::vector<Led> leds; // its lights in the matrix
    int index = 0;         // pad / encoder / knob number (1-based)
};

inline std::vector<Led> run(int com, int row0, int count, int step) {
    std::vector<Led> v;
    for (int i = 0; i < count; ++i) v.push_back({com, row0 + i * step});
    return v;
}

inline std::vector<Control> layout() {
    std::vector<Control> c;
    const auto key = [&](std::string name, std::string label, int bit, float x0, float y0, float x1, float y1, Led led) {
        Control k;
        k.name = name, k.label = label, k.kind = Kind::Key, k.key = bit;
        k.x = x0 - kOX, k.y = y0 - kOY, k.w = x1 - x0, k.h = y1 - y0;
        if (led.com >= 0) k.leds = {led};
        c.push_back(k);
    };
    // left of the screen
    key("USB STOP", "USB\nSTOP", 32, 230, 362, 286, 400, {0, 0});
    key("MENU", "MENU", 33, 230, 470, 286, 507, {0, 4});
    // jog cluster: the jog (turn: enc 88, push: key 37) in a rounded square, a key in each corner
    {
        Control j;
        j.name = "JOG", j.label = "", j.kind = Kind::Jog, j.key = 37, j.enc = 88;
        j.x = 1131 - kOX, j.y = 378 - kOY, j.w = 50;
        c.push_back(j);
    }
    key("RETURN", "RETURN", 35, 1048, 293, 1094, 327, {0, 8});
    key("COPY", "COPY", 36, 1168, 293, 1214, 327, {0, 10});
    key("MINUS", "-", 38, 1048, 429, 1094, 463, {0, 12});
    key("PLUS", "+", 39, 1168, 429, 1214, 463, {0, 14});
    // the first key row
    key("SHIFT", "SHIFT", 3, 372, 627, 456, 665, {3, 4});
    key("LAST", "LAST", 2, 497, 627, 579, 665, {3, 2});
    key("SAVE", "SAVE", 1, 621, 627, 704, 665, {3, 0});
    key("REC", "", 4, 757, 620, 839, 696, {2, 0});     // transport keys: icons, drawn as shapes
    key("BACK", "", 5, 882, 620, 963, 696, {2, 2});
    key("STOP", "", 6, 1006, 620, 1086, 696, {2, 4});
    key("PLAY", "", 7, 1130, 620, 1211, 696, {2, 6});
    key("SCULPT", "SCULPT", 8, 1267, 624, 1349, 662, {3, 12});
    key("DELETE", "DELETE", 9, 1393, 624, 1476, 662, {3, 14});
    key("INSERT", "INSERT", 10, 1521, 624, 1606, 662, {4, 0});
    // the second key row
    key("SONG", "SONG", 17, 372, 728, 456, 767, {3, 6});
    key("SCENE", "SCENE", 18, 497, 728, 579, 767, {3, 8});
    key("TRACK", "TRACK", 19, 621, 728, 704, 767, {3, 10});
    key("NEXT", "NEXT/TIE", 20, 757, 725, 839, 764, {2, 8});
    key("FILL", "FILL/X", 21, 882, 725, 963, 764, {2, 10});
    key("SOLO", "SOLO/SKIP", 22, 1006, 725, 1086, 764, {2, 12});
    key("GANG", "GANG", 23, 1130, 725, 1211, 764, {2, 14});
    key("PATTERN", "PATTERN", 24, 1267, 724, 1349, 763, {4, 2});
    key("TEMPO", "TEMPO", 25, 1393, 724, 1476, 763, {4, 4});
    key("SOUNDS", "INTERNAL\nSOUNDS", 26, 1521, 724, 1606, 763, {4, 6});
    // ROW and BAR: turn and push, with a ring
    {
        Control r;
        r.name = "ROW", r.label = "ROW", r.kind = Kind::RowBar, r.key = 0, r.enc = 97;
        r.x = 258 - kOX, r.y = 697 - kOY, r.w = 46, r.leds = run(1, 0, 5, 2);
        c.push_back(r);
        Control b;
        b.name = "BAR", b.label = "BAR", b.kind = Kind::RowBar, b.key = 11, b.enc = 114;
        b.x = 1716 - kOX, b.y = 692 - kOY, b.w = 46, b.leds = run(4, 8, 4, 2);
        c.push_back(b);
    }
    // knobs A-H with their rings (the ring level is its own message, E4 out)
    static const char* knobNames[8] = {"RELEASE", "CUTOFF", "RESONANCE", "VOLUME", "REVERB", "CHORUS", "EQ HIGH", "EQ LOW"};
    for (int i = 0; i < 8; ++i) {
        Control k;
        k.name = std::string(1, char('A' + i)), k.label = knobNames[i], k.kind = Kind::Knob, k.enc = 89 + i, k.index = i + 1;
        k.x = (i % 4 == 0 ? 1313 : i % 4 == 1 ? 1447 : i % 4 == 2 ? 1580 : 1715) - kOX;
        k.y = (i < 4 ? 280 : 458) - kOY;
        k.w = 30;
        c.push_back(k);
    }
    // step encoders 1-16 (turn, push) with their collars, and the step pads under them
    for (int i = 0; i < 16; ++i) {
        const float x = 230 + i * 101.5f - kOX;
        Control e;
        e.name = "ENC " + std::to_string(i + 1), e.label = "", e.kind = Kind::Encoder, e.enc = 98 + i, e.index = i + 1;
        e.key = i < 8 ? 68 + i : 80 + (i - 8);
        e.x = x, e.y = 882 - kOY, e.w = 32;
        // collar n: Com5-7 even rows for 1-6, Com0-4 odd rows for 7-16 (FrontPannelLed.cpp)
        const int n = i + 1;
        if (n <= 6) e.leds = run(5 + (n - 1) / 2, (n - 1) % 2 ? 8 : 0, 4, 2);
        else e.leds = run((n - 7) / 2, (n - 7) % 2 ? 9 : 1, 4, 2);
        c.push_back(e);
        Control p;
        p.name = "PAD " + std::to_string(i + 1), p.label = std::to_string(i + 1), p.kind = Kind::Pad, p.index = i + 1;
        p.key = i < 12 ? 48 + i : 64 + (i - 12);
        p.x = x - 30, p.y = 975 - kOY, p.w = 60, p.h = 95;
        p.leds = {{i < 8 ? 5 : 6, (i % 8) * 2 + 1}};  // the blue LED under the pad
        c.push_back(p);
    }
    {
        Control s;
        s.name = "SCREEN", s.kind = Kind::Screen;
        s.x = 370 - kOX, s.y = 185 - kOY, s.w = 605, s.h = 340;
        c.push_back(s);
    }
    return c;
}

}  // namespace ignitem
