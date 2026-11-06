// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Panel.h"

namespace ignitem {

namespace {
const juce::Colour kBody(0xff1b1c1f), kKey(0xff26272b), kKeyTop(0xff2f3035), kSilk(0xffd6d6d6), kRed(0xffff3b30);

// pad colour codes (MCUInterfaceParams.h): A0 off, A1 red ... B2 dark orange 1; 0x00 and 0x03 mean off too
juce::Colour padColour(uint8_t code) {
    switch (code) {
        case 0xA1: return juce::Colour(0xffff2a2a);
        case 0xA2: return juce::Colour(0xff2ee05a);
        case 0xA3: return juce::Colour(0xff2f5dff);
        case 0xA4: return juce::Colour(0xffa64dff);
        case 0xA5: return juce::Colour(0xfff4f4f4);
        case 0xA6: return juce::Colour(0xff5cc8ff);
        case 0xA7: return juce::Colour(0xff8a8a8a);
        case 0xA8: return juce::Colour(0xffd9a3ff);
        case 0xA9: return juce::Colour(0xff17702a);
        case 0xAA: return juce::Colour(0xff9c861a);
        case 0xAB: return juce::Colour(0xffad560f);
        case 0xAC: return juce::Colour(0xff861818);
        case 0xAD: return juce::Colour(0xff5b2286);
        case 0xAE: return juce::Colour(0xff172a8f);
        case 0xAF: return juce::Colour(0xff461664);
        case 0xB0: return juce::Colour(0xff121e66);
        case 0xB1: return juce::Colour(0xff9cf09c);
        case 0xB2: return juce::Colour(0xff8c3d0a);
        default: return juce::Colours::transparentBlack;
    }
}
juce::Colour keyLight(const std::string& name) {
    if (name == "BACK") return juce::Colour(0xff3a5bff);
    if (name == "PLAY") return juce::Colour(0xff35e06a);
    if (name == "SHIFT") return juce::Colours::white;
    return kRed;
}
}  // namespace

Panel::Panel(Link& link) : link_(link), controls_(layout()) {
    link_.onChange = [this] { triggerAsyncUpdate(); };
    startTimerHz(20);
}

Panel::~Panel() { screen_.reset(); }

void Panel::setScreenSource(const juce::String& source) {
    screen_ = std::make_unique<sim::ScreenView>(source);
    addAndMakeVisible(*screen_);
    resized();
}

void Panel::resized() {
    if (!screen_) return;
    if (const Control* sc = find("SCREEN"))
        screen_->setBounds(juce::Rectangle<float>(sc->x, sc->y, sc->w, sc->h).transformedBy(toScreen()).toNearestInt());
}

juce::AffineTransform Panel::toScreen() const {
    const float s = std::min(getWidth() / kCanvasW, getHeight() / kCanvasH);
    return juce::AffineTransform::scale(s).translated((getWidth() - kCanvasW * s) / 2, (getHeight() - kCanvasH * s) / 2);
}

juce::Point<float> Panel::toCanvas(juce::Point<float> p) const { return p.transformedBy(toScreen().inverted()); }

const Control* Panel::find(const std::string& name) const {
    const auto up = juce::String(name).toUpperCase().toStdString();
    for (const auto& c : controls_)
        if (c.name == up) return &c;
    return nullptr;
}

const Control* Panel::hit(juce::Point<float> p) const {
    for (const auto& c : controls_) {
        if (c.kind == Kind::Screen) continue;
        const bool round = c.kind == Kind::Encoder || c.kind == Kind::Knob || c.kind == Kind::Jog || c.kind == Kind::RowBar;
        if (round ? p.getDistanceFrom({c.x, c.y}) <= c.w * 1.35f : juce::Rectangle<float>(c.x, c.y, c.w, c.h).contains(p)) return &c;
    }
    return nullptr;
}

// ---- drawing

void Panel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0d0d0f));
    const PanelState s = link_.state();
    g.addTransform(toScreen());
    g.setColour(kBody);
    g.fillRoundedRectangle(10, 15, 1640, 1045, 14);
    // the screen: the IGNITE app's own window shows it
    if (const Control* sc = find("SCREEN")) {
        g.setColour(juce::Colour(0xff050506));
        g.fillRoundedRectangle(sc->x - 45, sc->y - 45, sc->w + 90, sc->h + 90, 8);
        g.setColour(juce::Colour(0xff101114));
        g.fillRect(sc->x, sc->y, sc->w, sc->h);
        if (!screen_) {
            g.setColour(juce::Colour(0xff6b6f78));
            g.setFont(juce::Font(22.0f));
            g.drawFittedText(link_.live() ? "The screen is the IGNITE app's own window" : "Waiting for the IGNITE app...",
                             juce::Rectangle<float>(sc->x, sc->y, sc->w, sc->h).toNearestInt(), juce::Justification::centred, 2);
        } else {
            screen_->setStatusText(link_.live() ? "LIVE " + juce::String(link_.slavePath()) : "waiting");
        }
        g.setColour(kSilk);
        g.setFont(juce::Font(30.0f, juce::Font::bold));
        g.drawText("IGNITE", juce::Rectangle<float>(sc->x, sc->y + sc->h + 8, sc->w, 36), juce::Justification::centred);
        g.setFont(juce::Font(13.0f, juce::Font::plain));
        g.setColour(kSilk.withAlpha(0.6f));
        g.drawText("unofficial emulator", juce::Rectangle<float>(sc->x, sc->y - 30, sc->w, 20), juce::Justification::centred);
    }
    g.setColour(kSilk);
    g.setFont(juce::Font(15.0f, juce::Font::bold));
    g.drawText("64-CHANNEL MULTI-TRACK SEQUENCER", juce::Rectangle<float>(1180, 32, 440, 24), juce::Justification::centredRight);
    // link state, where the unit has its two status LEDs
    g.setColour(link_.live() ? juce::Colour(0xff35e06a) : juce::Colour(0xffffb020));
    g.fillEllipse(938, 58, 12, 12);
    for (const auto& c : controls_) {
        switch (c.kind) {
            case Kind::Key: drawKey(g, c, s); break;
            case Kind::Pad: drawPad(g, c, s); break;
            case Kind::Screen: break;
            case Kind::Encoder:
            case Kind::Knob:
            case Kind::Jog:
            case Kind::RowBar: drawEncoder(g, c, s); break;
        }
    }
}

void Panel::drawKey(juce::Graphics& g, const Control& c, const PanelState& s) {
    const auto r = juce::Rectangle<float>(c.x, c.y, c.w, c.h);
    const bool lit = !c.leds.empty() && s.lit(c.leds[0].com, c.leds[0].row);
    const bool pressed = link_.keyDown(c.key);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(r.translated(0, 3), 4);
    g.setColour(pressed ? kKeyTop.brighter(0.25f) : kKey);
    g.fillRoundedRectangle(r, 4);
    g.setColour(kKeyTop);
    g.drawRoundedRectangle(r.reduced(1), 4, 1.5f);
    const auto light = keyLight(c.name);
    if (c.name == "SHIFT") {  // SHIFT's light is a bar under the key
        g.setColour(lit ? light : juce::Colour(0xff3a3b40));
        g.fillRoundedRectangle(c.x + 6, c.y + c.h + 9, c.w - 12, 4, 2);
    }
    g.setColour(lit && c.name != "SHIFT" ? light : kSilk.withAlpha(0.85f));
    if (c.label.empty()) {  // transport: REC dot, BACK to start, STOP square, PLAY triangle
        const auto m = r.getCentre();
        juce::Path p;
        if (c.name == "REC") p.addEllipse(m.x - 8, m.y - 8, 16, 16);
        if (c.name == "STOP") p.addRectangle(m.x - 8, m.y - 8, 16, 16);
        if (c.name == "PLAY") p.addTriangle(m.x - 7, m.y - 10, m.x - 7, m.y + 10, m.x + 10, m.y);
        if (c.name == "BACK") {
            p.addRectangle(m.x - 13, m.y - 9, 3, 18);
            p.addTriangle(m.x - 9, m.y, m.x + 1, m.y - 9, m.x + 1, m.y + 9);
            p.addTriangle(m.x + 1, m.y, m.x + 11, m.y - 9, m.x + 11, m.y + 9);
        }
        g.fillPath(p);
    } else {
        g.setFont(juce::Font(c.label.find('\n') != std::string::npos ? 12.5f : c.label.size() <= 1 ? 22.0f : 14.5f, juce::Font::bold));
        g.drawFittedText(juce::String::fromUTF8(c.label.c_str()), r.toNearestInt(), juce::Justification::centred, 2, 0.6f);
    }
    if (lit && c.name != "SHIFT") {
        g.setColour(light.withAlpha(0.18f));
        g.fillRoundedRectangle(r.reduced(2), 4);
    }
}

void Panel::drawPad(juce::Graphics& g, const Control& c, const PanelState& s) {
    const auto r = juce::Rectangle<float>(c.x, c.y, c.w, c.h);
    const auto col = padColour(s.padColour[size_t(c.index - 1)]);
    const bool pressed = link_.keyDown(c.key);
    // the number, boxed on 1, 5, 9 and 13; the blue LED under the pad lights it
    const bool blue = !c.leds.empty() && s.lit(c.leds[0].com, c.leds[0].row);
    const auto num = juce::Rectangle<float>(c.x, c.y - 38, c.w, 24);
    if (blue) {
        g.setColour(juce::Colour(0xff2f6bff).withAlpha(0.85f));
        g.fillRoundedRectangle(num.reduced(4, 1), 3);
    }
    g.setColour(kSilk);
    if ((c.index - 1) % 4 == 0) g.drawRect(num.reduced(2, 0), 1.5f);
    g.setFont(juce::Font(17.0f, juce::Font::bold));
    g.drawText(juce::String(c.index), num, juce::Justification::centred);
    g.setColour(juce::Colour(0xff0a0a0b));
    g.fillRoundedRectangle(r.expanded(4), 6);
    if (!col.isTransparent()) {
        g.setColour(col.withAlpha(0.35f));
        g.fillRoundedRectangle(r.expanded(9), 10);
    }
    g.setColour(!col.isTransparent() ? col.interpolatedWith(juce::Colours::white, 0.25f) : juce::Colour(0xff3a3b40));
    g.fillRoundedRectangle(r, 5);
    if (pressed) {
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillRoundedRectangle(r, 5);
    }
}

void Panel::drawEncoder(juce::Graphics& g, const Control& c, const PanelState& s) {
    const juce::Point<float> ctr(c.x, c.y);
    const float r = c.w;
    // its lights: knob rings A-H (a level of 16), step encoder collars (4), ROW (5) and BAR (4) rings
    if (c.kind == Kind::Knob) {
        const int level = s.ring[size_t(c.index - 1)];
        for (int i = 0; i < 16; ++i) {
            const float a = juce::degreesToRadians(-135.0f + i * 270.0f / 15);
            const auto p = ctr.getPointOnCircumference(r + 16, a);
            g.setColour(i < level ? kRed : juce::Colour(0xff3a3b40));
            g.fillEllipse(p.x - 3.2f, p.y - 3.2f, 6.4f, 6.4f);
        }
        g.setColour(kSilk);
        g.setFont(juce::Font(15.5f, juce::Font::bold));
        g.drawText(juce::String(c.label), juce::Rectangle<float>(c.x - 80, c.y - r - 52, 160, 22), juce::Justification::centred);
        g.drawText(juce::String(c.name), juce::Rectangle<float>(c.x - 20, c.y + r + 18, 40, 20), juce::Justification::centred);
    } else if (!c.leds.empty()) {
        const int n = int(c.leds.size());
        const float from = c.kind == Kind::Encoder ? -150.0f : -135.0f, span = c.kind == Kind::Encoder ? 120.0f : 270.0f;
        for (int i = 0; i < n; ++i) {
            const bool on = s.lit(c.leds[size_t(i)].com, c.leds[size_t(i)].row);
            const float a0 = juce::degreesToRadians(from + span * i / n), a1 = juce::degreesToRadians(from + span * (i + 1) / n - 4);
            juce::Path arc;
            arc.addCentredArc(ctr.x, ctr.y, r + 10, r + 10, 0, a0, a1, true);
            g.setColour(on ? kRed : juce::Colour(0xff3a3b40));
            g.strokePath(arc, juce::PathStrokeType(c.kind == Kind::Encoder ? 7.0f : 5.0f));
        }
        if (c.kind == Kind::RowBar) {
            g.setColour(kSilk);
            g.setFont(juce::Font(17.0f, juce::Font::bold));
            g.drawText(juce::String(c.label), juce::Rectangle<float>(c.x - 40, c.y - r - 50, 80, 24), juce::Justification::centred);
        }
    }
    if (c.kind == Kind::Jog) {  // the rounded square under the jog and its four keys
        g.setColour(juce::Colour(0xff121214));
        g.fillRoundedRectangle(ctr.x - 88, ctr.y - 90, 176, 180, 26);
        g.setColour(juce::Colour(0xff2a2b30));
        g.drawRoundedRectangle(ctr.x - 88, ctr.y - 90, 176, 180, 26, 2.0f);
    }
    // the knob: a shaded cap with a pointer that follows the turns
    const bool pressed = link_.keyDown(c.key);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(ctr.x - r + 2, ctr.y - r + 4, r * 2, r * 2);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4b50), ctr.x - r, ctr.y - r, juce::Colour(0xff111113), ctr.x + r, ctr.y + r, false));
    g.fillEllipse(ctr.x - r, ctr.y - r, r * 2, r * 2);
    if (pressed) {
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(ctr.x - r, ctr.y - r, r * 2, r * 2);
    }
    const float a = angle_.count(c.enc) ? angle_.at(c.enc) : 0.0f;
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawLine(juce::Line<float>(ctr.getPointOnCircumference(r * 0.25f, a), ctr.getPointOnCircumference(r * 0.92f, a)), 3.0f);
}

// ---- input: click a key or pad (held while the mouse is down); drag or scroll a knob to turn it,
// click it without moving to push it

void Panel::mouseDown(const juce::MouseEvent& e) {
    down_ = hit(toCanvas(e.position));
    downAt_ = e.position;
    dragAcc_ = 0;
    turned_ = false;
    if (!down_) return;
    if (down_->kind == Kind::Key || down_->kind == Kind::Pad) link_.setKey(down_->key, true);
    repaint();
}

void Panel::mouseDrag(const juce::MouseEvent& e) {
    if (!down_ || down_->enc < 0) return;
    const auto d = e.position - downAt_;
    if (!turned_ && d.getDistanceFromOrigin() < 4) return;
    turned_ = true;
    const float px = (d.x - d.y) - dragAcc_;  // up or right turns clockwise
    const float per = down_->kind == Kind::Jog ? 5.0f : 7.0f;
    const int steps = int(px / per);
    if (steps) {
        dragAcc_ += steps * per;
        link_.turn(down_->enc, steps);
        angle_[down_->enc] += steps * 0.12f;
        repaint();
    }
}

void Panel::mouseUp(const juce::MouseEvent&) {
    if (!down_) return;
    if (down_->kind == Kind::Key || down_->kind == Kind::Pad) link_.setKey(down_->key, false);
    else if (!turned_ && down_->key >= 0) {  // a knob clicked without turning: a push
        const int bit = down_->key;
        link_.setKey(bit, true);
        juce::Timer::callAfterDelay(120, [this, bit] { link_.setKey(bit, false); repaint(); });
    }
    down_ = nullptr;
    repaint();
}

void Panel::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const Control* c = hit(toCanvas(e.position));
    if (!c || c->enc < 0) return;
    const int steps = w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0;
    link_.turn(c->enc, steps);
    angle_[c->enc] += steps * 0.12f;
    repaint();
}

std::string Panel::litNames() const {
    const PanelState s = link_.state();
    std::string out;
    for (const auto& c : controls_) {
        bool any = false;
        std::string l;
        for (size_t i = 0; i < c.leds.size(); ++i) {
            const bool on = s.lit(c.leds[i].com, c.leds[i].row);
            l += on ? '1' : '0';
            any |= on;
        }
        if (any) out += c.name + (c.leds.size() > 1 ? " [" + l + "]" : "") + "\n";
        if (c.kind == Kind::Pad && s.padColour[size_t(c.index - 1)] >= 0xA1 && s.padColour[size_t(c.index - 1)] <= 0xB2) {
            char t[8];
            std::snprintf(t, sizeof t, "%02X", s.padColour[size_t(c.index - 1)]);
            out += c.name + " colour " + t + "\n";
        }
        if (c.kind == Kind::Knob && s.ring[size_t(c.index - 1)]) out += c.name + " ring " + std::to_string(s.ring[size_t(c.index - 1)]) + "\n";
    }
    return out;
}

}  // namespace ignitem
