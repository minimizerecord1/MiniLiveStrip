#pragma once

#include "PluginProcessor.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

//==============================================================================
// Minimal flat look: thin-arc rotary knobs, flat combo boxes.
class MiniLiveStripLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MiniLiveStripLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float startAngle, float endAngle,
                           juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
// Small flat toggle/push button with an LED dot and a text label.
class LedButton : public juce::Button
{
public:
    LedButton (const juce::String& name = {});

    void setColourOn (juce::Colour c) { colourOn = c; repaint(); }
    void setLabel (const juce::String& t) { label = t; repaint(); }
    void setLabelOff (const juce::String& t) { labelOff = t; repaint(); }   // text while off (optional)

    void paintButton (juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

private:
    juce::Colour colourOn { 0xffffb020 };
    juce::String label;
    juce::String labelOff;
};

//==============================================================================
// Thin vertical level meter (peak, -60..0 dB) with a small peak-hold tick.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float linearPeak);
    void paint (juce::Graphics&) override;

private:
    float levelDb = -60.0f;
    float holdDb  = -60.0f;
    int   holdFrames = 0;
};

//==============================================================================
// Thin vertical gain-reduction meter: the bar hangs down from the top (0 dB = empty),
// with a small peak-hold tick. Used for the limiter / compressor and the gate.
class ReductionMeter : public juce::Component
{
public:
    static constexpr int kGutter = 20;      // left strip with the dB numbers + marker label

    void setScale (float rangeDb, float tickStepDb) { range = rangeDb; step = tickStepDb; repaint(); }
    void setBarColour (juce::Colour c)  { colour = c; }
    void setFallRate (float dbPerSecond);           // max fall speed = follows the release time
    void setSpan (float db)             { if (spanDb != db) { spanDb = db; repaint(); } }
    void setMarker (float db, const juce::String& text)
    {
        if (markDb != db || markText != text) { markDb = db; markText = text; repaint(); }
    }
    void setValue (float reductionDb);  // positive dB of reduction
    void paint (juce::Graphics&) override;

    // Optional fader: the marker line can be dragged (limiter / compressor threshold).
    bool markerDraggable = false;
    std::function<void (float)> onMarkerDrag;          // depth in dB (0..range) while dragging
    std::function<void (bool)>  onMarkerDragState;     // true = drag started, false = ended
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> barRect() const;
    float yForDepth (float db) const;
    bool draggingMarker = false;

    float range = 30.0f;
    float step = 10.0f;
    float fallPerFrame = 1.5f;
    float spanDb = 0.0f;                // shaded range on the right edge (0 .. spanDb)
    float markDb = -1.0f;               // marker line (depth / ratio), < 0 = none
    juce::String markText;
    float valueDb = 0.0f;
    float holdDb = 0.0f;
    int   holdFrames = 0;
    juce::Colour colour { 0xffffb020 };
};

//==============================================================================
// Floating panel with the FREQ / Q / GAIN knobs of the selected EQ band.
// The editor places it above the band point on the RTA. The sliders live in the editor
// (they are children of this panel), the panel only draws the frame and the captions.
class BandPopup : public juce::Component
{
public:
    static constexpr int kW = 186, kH = 100;

    void setContent (const juce::String& newTitle, juce::Colour c, bool gainUsed)
    {
        if (title != newTitle || colour != c || usesGain != gainUsed)
        {
            title = newTitle;
            colour = c;
            usesGain = gainUsed;
            repaint();
        }
    }

    static juce::Rectangle<int> slot (int i) { return { 6 + i * 58, 29, 58, 66 }; }

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

private:
    juce::String title;
    juce::Colour colour { 0xffffffff };
    bool usesGain = true;
};

//==============================================================================
// Knob panel of the limiter / compressor or the gate. It opens inside the RTA display
// (bottom right): on/off switch, (limiter | compressor) switch and the knobs. No graph.
class DynamicsPanel : public juce::Component
{
public:
    enum Kind { Limiter, Gate };            // Limiter = the limiter / compressor slot

    DynamicsPanel (MiniLiveStripProcessor&, Kind);

    juce::Point<int> getPreferredSize() const;
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

    std::function<void()> onClose;
    std::function<void()> onSizeChanged;           // the number of knobs changed (mode switch)

private:
    bool compMode() const;
    void setMode (bool comp);
    void syncMode();
    int  activeKnobCount() const;

    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        juce::String caption;
        int mode = 0;                              // 0 = limiter / gate, 1 = compressor
    };

    MiniLiveStripProcessor& proc;
    Kind kind;
    std::atomic<float>* pMode = nullptr;
    bool lastComp = false;

    LedButton onBtn, closeBtn, limModeBtn, compModeBtn;
    std::vector<Knob> knobs;

    // Attachments last, so they are destroyed before the controls they refer to.
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAtt;
    std::unique_ptr<juce::ButtonParameterAttachment> onAtt;
};

//==============================================================================
// Small module button (FEEDBACK / EQ / LIMI-COMP / GATE) with a vector icon.
//   click = open / close the module's panel (EQ: on / off), drag left / right = change the
//   order in which the modules process the audio (the editor handles the reordering).
class ModuleButton : public juce::Component
{
public:
    enum Icon { IconFeedback, IconEq, IconDynamics, IconGate };

    ModuleButton (Icon, const juce::String& text, juce::Colour accent);

    void setLit (bool b)       { if (lit != b)  { lit = b;  repaint(); } }       // module is active
    void setOpen (bool b)      { if (open != b) { open = b; repaint(); } }       // panel is open
    void setPosition (int n)   { if (slotNumber != n) { slotNumber = n; repaint(); } }   // 1..4 in the chain
    bool isBeingDragged() const { return dragging; }

    std::function<void()>    onClick, onDragStart, onDragEnd;
    std::function<void (int)> onDragMove;          // wanted x of the left edge, in parent coordinates

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { over = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { over = false; repaint(); }
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;

private:
    Icon icon;
    juce::String text;
    juce::Colour accent;
    bool lit = false, open = false, over = false, dragging = false;
    int  slotNumber = 1;
    int  grabX = 0;
};

//==============================================================================
// Editor: one big RTA analyser with everything on top of it:
//   top left      Undo / Redo
//   bottom left   the four module buttons (drag = processing order)
//   bottom right  panel of the open module (Feedback, Limi / Comp, Gate)
//   EQ points     click = Freq / Q / Gain knobs above the point, drag = freq / gain,
//                 wheel = Q, double-click = reset, right-click = menu
// Right of the analyser: thin full-height meters (gain reduction, gate, output).
// SOLO button of the band popup. One click toggles solo on/off. Dragging it changes the soloed band:
// horizontal = frequency (Ctrl = Q for gain bands, both for Low/High Cut and Notch), vertical = solo level.
// A press that was dragged does not toggle solo.
class SoloButton : public LedButton
{
public:
    using LedButton::LedButton;

    std::function<void()>                                onDragBegin, onDragEnd;
    std::function<void (float dx, float dy, bool ctrl)>  onDragMove;   // distance from drag start, pixels
    std::function<void (bool on)>                        onSoloToggled;

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragged = false;
        LedButton::mouseDown (e);
        if (onDragBegin) onDragBegin();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 4)
            dragged = true;
        if (onDragMove)
            onDragMove ((float) e.getDistanceFromDragStartX(), (float) e.getDistanceFromDragStartY(),
                        e.mods.isCtrlDown() || e.mods.isCommandDown());
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        LedButton::mouseUp (e);
        if (! dragged && isMouseOver (false))
        {
            setToggleState (! getToggleState(), juce::dontSendNotification);
            if (onSoloToggled) onSoloToggled (getToggleState());
        }
        if (onDragEnd) onDragEnd();
    }

    void paint (juce::Graphics& g) override
    {
        LedButton::paint (g);

        // Headphone icon instead of a text label.
        const auto  b   = getLocalBounds().toFloat().reduced (5.0f, 3.0f);
        const float r   = juce::jmin (b.getWidth(), b.getHeight()) * 0.36f;
        const auto  c   = b.getCentre();
        const auto  ink = getToggleState() ? juce::Colours::white : juce::Colour (0xff8a96a8);

        juce::Path band;
        band.addArc (c.x - r, c.y - r, 2.0f * r, 2.0f * r,
                     1.5f * juce::MathConstants<float>::pi, 2.5f * juce::MathConstants<float>::pi, true);
        g.setColour (ink);
        g.strokePath (band, juce::PathStrokeType (1.6f));

        const float cupW = r * 0.55f, cupH = r * 0.95f;
        g.fillRoundedRectangle (c.x - r - cupW * 0.5f, c.y, cupW, cupH, 2.0f);
        g.fillRoundedRectangle (c.x + r - cupW * 0.5f, c.y, cupW, cupH, 2.0f);
    }

private:
    bool dragged = false;
};

class MiniLiveStripEditor : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit MiniLiveStripEditor (MiniLiveStripProcessor&);
    ~MiniLiveStripEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Ctrl/Cmd+Z = undo, Ctrl/Cmd+Shift+Z or Ctrl/Cmd+Y = redo.
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int kBands    = MiniLiveStripProcessor::kNumEqBands;
    static constexpr int kModBtn   = 60;       // size of the square module buttons
    static constexpr int kChevGap  = 10;       // gap between the module buttons (arrow of the signal flow)
    static constexpr int kMeterW   = 16;       // thin meters on the right (output meter)
    static constexpr int kRedW     = ReductionMeter::kGutter + kMeterW;   // GR / gate meters incl. dB scale
    static constexpr int kGainBlkW = 60;       // GAIN IN knob block, left of the module buttons
    static constexpr int kMeterGap = 8;
    static constexpr int kFbPanelW = 510;      // feedback panel size
    static constexpr int kFbPanelH = 152;

    struct BandUi
    {
        juce::Slider freq, q, gain;
        juce::RangedAudioParameter* pOn    = nullptr;
        juce::RangedAudioParameter* pSolo  = nullptr;
        juce::RangedAudioParameter* pFreq  = nullptr;
        juce::RangedAudioParameter* pQ     = nullptr;
        juce::RangedAudioParameter* pGain  = nullptr;
        juce::RangedAudioParameter* pType  = nullptr;
        juce::RangedAudioParameter* pShape = nullptr;
    };

    // Coordinate mapping of the RTA display (shared by drawing and mouse handling).
    struct PlotGeom
    {
        juce::Rectangle<float> area;
        float plotTop = 0.0f, plotBottom = 0.0f, midY = 0.0f, halfH = 0.0f;
        float dbBottom = -100.0f;                       // RTA depth knob: the plot shows 0 dB .. dbBottom

        float xForFreq (float f) const;
        float freqForX (float x) const;
        float yForDb   (float db) const;
        float yForResp (double db) const;
        float respForY (float y) const;
    };

    PlotGeom makeGeom() const;
    juce::Point<float> nodePos (int band, const PlotGeom&) const;
    int  hitTestNode (juce::Point<float> p) const;          // -1 = none
    bool bandUsesGain (int band) const;
    bool overlayHit (juce::Point<int> p) const;             // inside the open feedback panel

    static float plainValue (juce::RangedAudioParameter*);
    static void  setPlain   (juce::RangedAudioParameter*, float plain);
    static void  gesture    (juce::RangedAudioParameter*, bool begin);

    void timerCallback() override;
    void drawDisplay (juce::Graphics&);
    void drawBar (juce::Graphics&);
    void drawSub (juce::Graphics&);
    void endWheelGesture();

    // EQ band editing on the RTA
    int  visibleBands() const;            // EQ bands that are shown (4..10)
    void selectBand (int band);           // -1 = none
    void updatePopupHover();              // popup opens on hover of the selected band, fades out after kPopupFadeMs
    void layoutPopup();                   // knob popup above the selected point
    void showBandMenu (int band, juce::Point<int> screenPos);
    void handleBandMenu (int band, int id);
    void showEmptyMenu (const juce::MouseEvent&);
    void addBand (float freq, float gain);   // freq <= 0: default frequency
    void deleteBand (int band);          // removes a band, the bands behind it move up
    void resetAllBands();                // every band back to its defaults, 4 bands shown

    // Module buttons + panels
    juce::Rectangle<int> slotRect (int slot) const;
    void layoutModuleBar (bool animate);
    void layoutSub();                     // places the panel of the open module
    void toggleModule (int module);       // open / close the panel of a module (EQ: on / off)
    void updateModuleVisibility();
    void syncModuleOrder();               // chain order changed by undo / redo / preset load
    void moduleDragStart (int module);
    void moduleDragMove  (int module, int wantedX);
    void moduleDragEnd   (int module);

    MiniLiveStripProcessor& proc;
    MiniLiveStripLookAndFeel lnf;      // declared first: destroyed last

    juce::Rectangle<int> displayArea, barArea, subArea, meterArea, popupRect;
    juce::RangedAudioParameter* pEqVisible = nullptr;
    juce::RangedAudioParameter* pEqOn = nullptr;       // EQ module on / off
    int selectedBand = -1;

    // The popup owns the knob sliders of the bands as children, so it is declared BEFORE 'bands'
    // (destroyed after them).
    BandPopup popup;

    juce::Slider sensitivity, notchQ, depth, outGain, growthTime;      // outGain = GAIN IN (id "outGain" kept)
    juce::Slider rtaTime;                                  // RTA fall time (bottom right of the analyser)
    juce::Slider rtaDepth;                                 // RTA depth in dB (next to RTA time)
    std::atomic<float>* rtaDepthVal = nullptr;
    LedButton scanBtn, growthBtn, resetBtn;
    SoloButton bandSoloBtn;                                 // SOLO for the selected EQ band (inside the popup)
    LedButton fbBtn, undoBtn, redoBtn;
    LedButton subCloseBtn;                                 // closes the feedback panel

    // The four module buttons, indexed by module id (0 Feedback, 1 EQ, 2 Limi / Comp, 3 Gate).
    std::array<std::unique_ptr<ModuleButton>, 4> modBtn;
    std::array<int, 4> moduleOrder { 0, 1, 2, 3 };         // slot -> module (slot 0 processes first)
    std::array<int, 4> dragOrder   { 0, 1, 2, 3 };         // tentative order while a button is dragged
    int  dragModule = -1;
    int  openModule = -1;                                  // module whose panel is open, -1 = none (EQ never opens one)
    juce::RangedAudioParameter* pChainOrder = nullptr;
    juce::ComponentAnimator animator;
    DynamicsPanel limPanel  { proc, DynamicsPanel::Limiter };
    DynamicsPanel gatePanel { proc, DynamicsPanel::Gate };

    ReductionMeter grMeter;                                // limiter / compressor gain reduction
    ReductionMeter gateMeter;                              // gate attenuation
    LevelMeter outMeter;                                   // output level

    juce::String grCaption2, gateCaption2;                 // threshold read-outs under the meters

    double lastRtaMs = 0.0;                                // for the time-based RTA smoothing
    std::array<BandUi, kBands> bands;

    std::vector<float> smoothed;      // RTA: smoothed dB value per display column
    std::vector<float> smoothedPost;  // output RTA: smoothed dB value per display column

    int   qPreviewFrames = 0;         // shows a Q preview curve for a moment after Q/Depth changes
    float lastQ = -1.0f;
    float lastDepth = -1.0f;

    int  hoverBand = -1;              // EQ point under the mouse
    static constexpr int kPopupFadeMs = 2000;   // time for the band popup to fade out after the mouse left it
    juce::uint32 popupLeaveMs = 0;               // last time the mouse was on the selected band / popup
    float popupAlpha = 0.0f;                     // 0 = hidden, 1 = fully shown
    int   soloDragBand = -1;                     // band being changed by dragging the SOLO button
    float soloDragFreq0 = 0.0f, soloDragQ0 = 0.0f, soloDragLevel0 = 0.0f;
    int  dragBand  = -1;              // EQ point being dragged
    int  shiftSoloBand = -1;          // band soloed by Shift + hold on its point
    bool dragGain  = false;           // dragged band also moves gain
    bool  dragCtrl = false;           // Ctrl + drag on a point: horizontal = Q, vertical = gain (freq stays)
    float ctrlQ0 = 1.0f, ctrlGain0 = 0.0f;   // values when the Ctrl drag started
    float choiceWheelAccum = 0.0f;    // Shift / Ctrl wheel: fractional scroll amount
    juce::RangedAudioParameter* grThrParam = nullptr;   // threshold moved by the GR meter fader
    bool grThrComp = false;
    int  wheelBand = -1;              // band whose Q is being changed with the wheel (one undo step)
    juce::uint32 lastWheelMs = 0;

    // Attachments last, so they are destroyed before the controls they refer to.
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniLiveStripEditor)
};
