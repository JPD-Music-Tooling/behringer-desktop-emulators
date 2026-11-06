// SPDX-License-Identifier: AGPL-3.0-or-later
// The IGNITE panel, drawn from the layout and the lights the app sends.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "Layout.h"
#include "Link.h"
#include "ScreenView.h"

namespace ignitem {

class Panel : public juce::Component, private juce::AsyncUpdater, private juce::Timer {
public:
    explicit Panel(Link& link);
    ~Panel() override;
    void resized() override;
    // the IGNITE app's own screen in the cut-out (served by its IGNITE_SCREEN_MIRROR): "unix:<path>"
    void setScreenSource(const juce::String& source);
    sim::ScreenView* screen() { return screen_.get(); }
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    // for the scripting channel
    const Control* find(const std::string& name) const;
    const std::vector<Control>& controls() const { return controls_; }
    std::string litNames() const;  // every control whose light is on, and the pad colours and rings

private:
    void handleAsyncUpdate() override { repaint(); }
    void timerCallback() override { repaint(); }  // the link's state and blinking are drawn from here
    juce::AffineTransform toScreen() const;
    juce::Point<float> toCanvas(juce::Point<float> p) const;
    const Control* hit(juce::Point<float> canvas) const;
    void drawKey(juce::Graphics& g, const Control& c, const PanelState& s);
    void drawPad(juce::Graphics& g, const Control& c, const PanelState& s);
    void drawEncoder(juce::Graphics& g, const Control& c, const PanelState& s);

    Link& link_;
    std::vector<Control> controls_;
    const Control* down_ = nullptr;  // the control the mouse went down on
    juce::Point<float> downAt_;
    float dragAcc_ = 0;
    bool turned_ = false;
    std::map<int, float> angle_;     // encoder -> drawn pointer angle
    std::unique_ptr<sim::ScreenView> screen_;
};

}  // namespace ignitem
