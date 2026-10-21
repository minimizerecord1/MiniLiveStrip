#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <complex>

//==============================================================================
MiniLiveStripProcessor::MiniLiveStripProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pSens     = apvts.getRawParameterValue ("sensitivity");
    pMaxDepth = apvts.getRawParameterValue ("maxDepth");
    pQ        = apvts.getRawParameterValue ("q");
    pSoloLevel = apvts.getRawParameterValue ("soloLevel");
    pGrowth   = apvts.getRawParameterValue ("growth");
    pScan     = apvts.getRawParameterValue ("scan");
    pReset    = apvts.getRawParameterValue ("reset");
    pOutGain  = apvts.getRawParameterValue ("outGain");
    pFbOn       = apvts.getRawParameterValue ("fbOn");
    pGrowthTime = apvts.getRawParameterValue ("growthTime");
    pLimOn      = apvts.getRawParameterValue ("limOn");
    pLimThresh  = apvts.getRawParameterValue ("limThresh");
    pLimCeiling = apvts.getRawParameterValue ("limCeiling");
    pLimAttack  = apvts.getRawParameterValue ("limAttack");
    pLimRelease = apvts.getRawParameterValue ("limRelease");
    pGateOn      = apvts.getRawParameterValue ("gateOn");
    pGateThresh  = apvts.getRawParameterValue ("gateThresh");
    pGateDepth   = apvts.getRawParameterValue ("gateDepth");
    pGateKnee    = apvts.getRawParameterValue ("gateKnee");
    pGateAttack  = apvts.getRawParameterValue ("gateAttack");
    pGateHold    = apvts.getRawParameterValue ("gateHold");
    pGateRelease = apvts.getRawParameterValue ("gateRelease");
    pEqVisible   = apvts.getRawParameterValue ("eqVisible");
    pLimMode     = apvts.getRawParameterValue ("limMode");
    pCompThresh  = apvts.getRawParameterValue ("compThresh");
    pCompRatio   = apvts.getRawParameterValue ("compRatio");
    pCompKnee    = apvts.getRawParameterValue ("compKnee");
    pCompAttack  = apvts.getRawParameterValue ("compAttack");
    pCompRelease = apvts.getRawParameterValue ("compRelease");
    pCompMakeup  = apvts.getRawParameterValue ("compMakeup");
    pChainOrder  = apvts.getRawParameterValue ("chainOrder");
    pEqGlobalOn  = apvts.getRawParameterValue ("eqOn");
    pGeqOn       = apvts.getRawParameterValue ("geqOn");
    for (int b = 0; b < kGeqBands; ++b)
        pGeqGain[(size_t) b] = apvts.getRawParameterValue ("geq" + juce::String (b + 1));

    for (int b = 0; b < kNumEqBands; ++b)
    {
        const juce::String id = "eq" + juce::String (b + 1);
        pEqFreq [(size_t) b] = apvts.getRawParameterValue (id + "Freq");
        pEqGain [(size_t) b] = apvts.getRawParameterValue (id + "Gain");
        pEqQ    [(size_t) b] = apvts.getRawParameterValue (id + "Q");
        pEqType [(size_t) b] = apvts.getRawParameterValue (id + "Type");
        pEqShape[(size_t) b] = apvts.getRawParameterValue (id + "Shape");
        pEqOn   [(size_t) b] = apvts.getRawParameterValue (id + "On");
        pEqSolo [(size_t) b] = apvts.getRawParameterValue (id + "Solo");
    }

    for (auto& f : notchFreqDisplay)  f.store (0.0f);
    for (auto& d : notchDepthDisplay) d.store (0.0f);
    for (auto& s : spectrumDisplay)   s.store (-120.0f);
    for (auto& s : spectrumPostDisplay) s.store (-120.0f);

    // Undo/redo of every user edit. "reset" is a momentary switch and is not recorded.
    history.attach (*this, juce::StringArray { "reset" });
}

MiniLiveStripProcessor::~MiniLiveStripProcessor()
{
    history.detach();
}

//==============================================================================
// Undo / redo history (message thread)
void ParameterHistory::attach (juce::AudioProcessor& p, const juce::StringArray& ignoredIds)
{
    detach();
    owner = &p;

    const auto& params = p.getParameters();
    numKnown = params.size();
    known.reset (new std::atomic<float>[(size_t) juce::jmax (1, numKnown)]);
    ignored.assign ((size_t) numKnown, 0);

    for (int i = 0; i < numKnown; ++i)
    {
        auto* prm = params[i];
        known[(size_t) i].store (prm->getValue());

        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm))
            ignored[(size_t) i] = ignoredIds.contains (withId->paramID) ? 1 : 0;

        prm->addListener (this);
    }
}

void ParameterHistory::detach()
{
    if (owner == nullptr)
        return;

    for (auto* prm : owner->getParameters())
        prm->removeListener (this);

    owner = nullptr;
}

void ParameterHistory::clear()
{
    pending.clear();
    undoStack.clear();
    redoStack.clear();
    openGestures.store (0);
}

void ParameterHistory::parameterValueChanged (int index, float newValue)
{
    if (index < 0 || index >= numKnown)
        return;

    const float old = known[(size_t) index].exchange (newValue);   // lock-free, any thread

    if (applying.load() || openGestures.load() <= 0 || ignored[(size_t) index] != 0)
        return;

    // Only edits made on the message thread (the GUI) are recorded, never host automation.
    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;

    for (auto& c : pending)
    {
        if (c.index == index)
        {
            c.after = newValue;
            return;
        }
    }

    pending.push_back ({ index, old, newValue });
}

void ParameterHistory::parameterGestureChanged (int index, bool starting)
{
    if (index < 0 || index >= numKnown || applying.load() || ignored[(size_t) index] != 0)
        return;

    if (! juce::MessageManager::existsAndIsCurrentThread())
        return;

    if (starting)
    {
        ++openGestures;
    }
    else if (openGestures.load() > 0)
    {
        if (--openGestures == 0)
            finishGesture();
    }
}

void ParameterHistory::finishGesture()
{
    Step step;
    for (const auto& c : pending)
        if (std::abs (c.after - c.before) > 1.0e-6f)
            step.push_back (c);

    pending.clear();

    if (step.empty())
        return;

    undoStack.push_back (std::move (step));
    if (undoStack.size() > kMaxSteps)
        undoStack.erase (undoStack.begin());

    redoStack.clear();
}

void ParameterHistory::apply (const Step& step, bool useAfter)
{
    if (owner == nullptr)
        return;

    applying.store (true);
    const auto& params = owner->getParameters();

    for (const auto& c : step)
    {
        if (c.index < 0 || c.index >= params.size())
            continue;

        auto* prm = params[c.index];
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (useAfter ? c.after : c.before);
        prm->endChangeGesture();
    }

    applying.store (false);
}

void ParameterHistory::undo()
{
    if (undoStack.empty() || openGestures.load() > 0)
        return;

    auto step = std::move (undoStack.back());
    undoStack.pop_back();
    apply (step, false);
    redoStack.push_back (std::move (step));
}

void ParameterHistory::redo()
{
    if (redoStack.empty() || openGestures.load() > 0)
        return;

    auto step = std::move (redoStack.back());
    redoStack.pop_back();
    apply (step, true);
    undoStack.push_back (std::move (step));
}

juce::AudioProcessorValueTreeState::ParameterLayout MiniLiveStripProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "sensitivity", 1 }, "Sensitivity",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "q", 1 }, "Notch Q (higher = narrower)",
        NormalisableRange<float> (4.0f, 60.0f, 0.1f, 0.5f), 60.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "maxDepth", 1 }, "Depth (dB)",
        NormalisableRange<float> (3.0f, 60.0f, 0.1f), 60.0f));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "scan", 1 }, "Scan Feedback", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "growth", 1 }, "Growth Check", false));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "reset", 1 }, "Reset Notches", false));


    // ---- EQ (after the feedback suppressor): bands 1..6 keep their original place in the
    //      parameter list, bands 7..10 are appended at the very end.
    const int kLegacy = 6;
    const float fDef[kNumEqBands] = { 63.0f, 250.0f, 1200.0f, 4000.0f, 8000.0f, 16000.0f,
                                      125.0f, 600.0f, 2500.0f, 10000.0f };
    const StringArray types { "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Bell", "Tilt", "Notch" };
    const StringArray shapes { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct",
                               "30 dB/oct", "36 dB/oct", "42 dB/oct" };

    auto addBand = [&] (int b)
    {
        const String id    = "eq" + String (b + 1);
        const String label = "EQ" + String (b + 1);

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id + "Freq", 1 }, label + " Freq (Hz)",
            NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), fDef[b]));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id + "Q", 1 }, label + " Q",
            NormalisableRange<float> (0.2f, 10.0f, 0.01f, 0.5f), 0.7f));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { id + "Gain", 1 }, label + " Gain (dB)",
            NormalisableRange<float> (-25.0f, 25.0f, 0.1f), 0.0f));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id + "Type", 1 }, label + " Slope", types, (int) Bell));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { id + "Shape", 1 }, label + " Shape", shapes, 0));
    };

    for (int b = 0; b < kLegacy; ++b)
        addBand (b);

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "outGain", 1 }, "Input Gain (dB)",     // ID kept so old sessions / presets still load
        NormalisableRange<float> (-30.0f, 30.0f, 0.1f), 0.0f));

    // ---- Added later (kept at the end so existing parameter order is unchanged)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "fbOn", 1 }, "Feedback On", false));

    // Growth Check time window: how long a peak must persist and grow (default ~ 6 hops).
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "growthTime", 1 }, "Growth Time (ms)",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.4f), 64.0f));

    for (int b = 0; b < kLegacy; ++b)
    {
        const String id = "eq" + String (b + 1);
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { id + "On", 1 }, "EQ" + String (b + 1) + " On", true));
    }

    // Order of the four modules (Feedback, EQ, Limiter / Comp, Gate): index of the permutation.
    // 0 = Feedback -> EQ -> Limiter -> Gate (the original fixed order).
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "chainOrder", 1 }, "Chain Order", 0, kNumOrders - 1, 0));

    // Limiter on/off (default on: it only does something when the output would pass 0 dBFS)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "limOn", 1 }, "Limiter On", false));

    // RTA fall time (display only): how slowly the analyser curves fall back, in ms.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rtaTime", 1 }, "RTA Time (ms)",
        NormalisableRange<float> (20.0f, 2000.0f, 1.0f, 0.4f), 150.0f,
        AudioParameterFloatAttributes().withAutomatable (false)));

    // ---- Limiter: threshold ADDS gain, ceiling, attack, release
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "limThresh", 1 }, "Limiter Threshold (dB)",
        NormalisableRange<float> (-30.0f, 0.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "limCeiling", 1 }, "Limiter Ceiling (dB)",
        NormalisableRange<float> (-10.0f, 0.0f, 0.1f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "limAttack", 1 }, "Limiter Attack (ms)",
        NormalisableRange<float> (0.5f, 500.0f, 0.1f, 0.35f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "limRelease", 1 }, "Limiter Release (ms)",
        NormalisableRange<float> (1.0f, 500.0f, 0.1f, 0.4f), 1.0f));

    // ---- Gate (default off, so existing sessions sound the same)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "gateOn", 1 }, "Gate On", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateThresh", 1 }, "Gate Threshold (dB)",
        NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -50.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateDepth", 1 }, "Gate Depth (dB)",
        NormalisableRange<float> (0.0f, 80.0f, 0.1f), 40.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateKnee", 1 }, "Gate Knee (dB)",
        NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateAttack", 1 }, "Gate Attack (ms)",
        NormalisableRange<float> (0.1f, 100.0f, 0.1f, 0.4f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateHold", 1 }, "Gate Hold (ms)",
        NormalisableRange<float> (0.0f, 500.0f, 1.0f, 0.5f), 50.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateRelease", 1 }, "Gate Release (ms)",
        NormalisableRange<float> (5.0f, 2000.0f, 1.0f, 0.4f), 150.0f));

    // ---- Number of EQ bands that are shown / active (the editor's "add band" cell raises it)
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { "eqVisible", 1 }, "EQ Bands", kMinEqBands, kNumEqBands, kMinEqBands,
        AudioParameterIntAttributes().withAutomatable (false)));

    // ---- EQ bands 7..10
    for (int b = kLegacy; b < kNumEqBands; ++b)
    {
        addBand (b);
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "eq" + String (b + 1) + "On", 1 }, "EQ" + String (b + 1) + " On", true));
    }

    // ---- Compressor (second mode of the limiter slot; default mode = limiter)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "limMode", 1 }, "Limiter / Compressor", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "compThresh", 1 }, "Comp Threshold (dB)",
        NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -18.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "compRatio", 1 }, "Comp Ratio",
        NormalisableRange<float> (1.0f, 20.0f, 0.1f, 0.5f), 4.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "compKnee", 1 }, "Comp Knee (dB)",
        NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "compAttack", 1 }, "Comp Attack (ms)",
        NormalisableRange<float> (0.5f, 200.0f, 0.1f, 0.4f), 10.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "compRelease", 1 }, "Comp Release (ms)",
        NormalisableRange<float> (10.0f, 2000.0f, 1.0f, 0.4f), 100.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "compMakeup", 1 }, "Comp Makeup (dB)",
        NormalisableRange<float> (0.0f, 24.0f, 0.1f), 0.0f));

    // RTA depth (display only): the analyser shows 0 dB down to -depth.
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "rtaDepth", 1 }, "RTA Depth (dB)",
        NormalisableRange<float> (40.0f, 120.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withAutomatable (false)));

    // EQ module on / off (the EQ button in the editor). Appended last, old sessions load it as on.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "eqOn", 1 }, "EQ On", true));

    // Per-band solo (appended last so older sessions keep loading).
    for (int b = 0; b < kNumEqBands; ++b)
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { "eq" + String (b + 1) + "Solo", 1 }, "EQ" + String (b + 1) + " Solo", false));

    // Solo listening level (drag vertically on the SOLO button; not in the normal EQ path).
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "soloLevel", 1 }, "Solo Level", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));

    // ---- GEQ: 31-band graphic EQ (appended last so older sessions keep loading, bands flat)
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "geqOn", 1 }, "GEQ On", true));

    for (int b = 0; b < kGeqBands; ++b)
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { "geq" + String (b + 1), 1 },
            "GEQ " + String (b + 1) + " Gain (dB)",
            NormalisableRange<float> (-30.0f, 30.0f, 0.1f), 0.0f));

    return layout;
}

//==============================================================================
// RBJ cookbook biquads
MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makePeaking (double fs, double freq,
                                                                      double gainDb, double q)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    q    = juce::jmax (0.1, q);

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
    const double cw    = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);

    const double a0 = 1.0 + alpha / A;

    Coeffs c;
    c.b0 = (1.0 + alpha * A) / a0;
    c.b1 = (-2.0 * cw) / a0;
    c.b2 = (1.0 - alpha * A) / a0;
    c.a1 = (-2.0 * cw) / a0;
    c.a2 = (1.0 - alpha / A) / a0;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeLowShelf (double fs, double freq,
                                                                       double gainDb, double q)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    q    = juce::jmax (0.1, q);

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
    const double cw    = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double tsa   = 2.0 * std::sqrt (A) * alpha;

    const double a0 = (A + 1.0) + (A - 1.0) * cw + tsa;

    Coeffs c;
    c.b0 =        A * ((A + 1.0) - (A - 1.0) * cw + tsa) / a0;
    c.b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cw)       / a0;
    c.b2 =        A * ((A + 1.0) - (A - 1.0) * cw - tsa) / a0;
    c.a1 = -2.0     * ((A - 1.0) + (A + 1.0) * cw)       / a0;
    c.a2 =            ((A + 1.0) + (A - 1.0) * cw - tsa) / a0;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeHighShelf (double fs, double freq,
                                                                        double gainDb, double q)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    q    = juce::jmax (0.1, q);

    const double A     = std::pow (10.0, gainDb / 40.0);
    const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
    const double cw    = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double tsa   = 2.0 * std::sqrt (A) * alpha;

    const double a0 = (A + 1.0) - (A - 1.0) * cw + tsa;

    Coeffs c;
    c.b0 =        A * ((A + 1.0) + (A - 1.0) * cw + tsa) / a0;
    c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw)       / a0;
    c.b2 =        A * ((A + 1.0) + (A - 1.0) * cw - tsa) / a0;
    c.a1 =  2.0     * ((A - 1.0) - (A + 1.0) * cw)       / a0;
    c.a2 =            ((A + 1.0) - (A - 1.0) * cw - tsa) / a0;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeLowPass2 (double fs, double freq, double q)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    q    = juce::jmax (0.1, q);

    const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
    const double cw    = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0    = 1.0 + alpha;

    Coeffs c;
    c.b0 = (1.0 - cw) * 0.5 / a0;
    c.b1 = (1.0 - cw) / a0;
    c.b2 = c.b0;
    c.a1 = (-2.0 * cw) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeHighPass2 (double fs, double freq, double q)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    q    = juce::jmax (0.1, q);

    const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
    const double cw    = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0    = 1.0 + alpha;

    Coeffs c;
    c.b0 = (1.0 + cw) * 0.5 / a0;
    c.b1 = -(1.0 + cw) / a0;
    c.b2 = c.b0;
    c.a1 = (-2.0 * cw) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeLowPass1 (double fs, double freq)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    const double K = std::tan (juce::MathConstants<double>::pi * freq / fs);
    const double n = 1.0 / (1.0 + K);

    Coeffs c;
    c.b0 = K * n;
    c.b1 = K * n;
    c.a1 = (K - 1.0) * n;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeHighPass1 (double fs, double freq)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    const double K = std::tan (juce::MathConstants<double>::pi * freq / fs);
    const double n = 1.0 / (1.0 + K);

    Coeffs c;
    c.b0 = n;
    c.b1 = -n;
    c.a1 = (K - 1.0) * n;
    return c;
}

MiniLiveStripProcessor::Coeffs MiniLiveStripProcessor::makeNotch2 (double fs, double freq, double q)
{
    freq = juce::jlimit (10.0, 0.49 * fs, freq);
    q    = juce::jmax (0.1, q);

    const double w0    = juce::MathConstants<double>::twoPi * freq / fs;
    const double cw    = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0    = 1.0 + alpha;

    Coeffs c;
    c.b0 = 1.0 / a0;
    c.b1 = (-2.0 * cw) / a0;
    c.b2 = 1.0 / a0;
    c.a1 = (-2.0 * cw) / a0;
    c.a2 = (1.0 - alpha) / a0;
    return c;
}

// Slope/shape -> cascade of biquads.
//  * Low Cut / High Cut: Butterworth high-/low-pass of order shape/6 (6 dB/oct = 1st order).
//    The Q knob sets the resonance of the sharpest 2nd-order stage (0.71 = flat).
//  * Shelves, Bell, Tilt, Notch: shape/6 identical stages in series; the total gain
//    is split evenly over the stages (steeper transitions, deeper notch).
int MiniLiveStripProcessor::makeEqSections (int type, int shapeIdx, double fs, double freq,
                                             double gainDb, double q, EqSections& out)
{
    const int order = juce::jlimit (1, kNumShapes, shapeIdx + 1);
    int n = 0;

    auto add = [&] (const Coeffs& c)
    {
        if (n < kMaxEqSections)
            out.c[(size_t) n++] = c;
    };

    const double gStage = gainDb / (double) order;

    switch (type)
    {
        case LowShelf:
            for (int i = 0; i < order; ++i) add (makeLowShelf (fs, freq, gStage, q));
            break;

        case HighShelf:
            for (int i = 0; i < order; ++i) add (makeHighShelf (fs, freq, gStage, q));
            break;

        case Bell:
            for (int i = 0; i < order; ++i) add (makePeaking (fs, freq, gStage, q));
            break;

        case Tilt:
            // Pivot at 'freq': lows go up by gain/2, highs go down by gain/2 (and vice versa).
            for (int i = 0; i < order; ++i)
            {
                add (makeLowShelf  (fs, freq,  0.5 * gStage, q));
                add (makeHighShelf (fs, freq, -0.5 * gStage, q));
            }
            break;

        case NotchFilter:
            for (int i = 0; i < order; ++i) add (makeNotch2 (fs, freq, q));
            break;

        case LowCut:
        case HighCut:
        {
            const bool hp = (type == LowCut);
            const int pairs = order / 2;

            for (int k = 0; k < pairs; ++k)
            {
                double qk = 1.0 / (2.0 * std::cos (juce::MathConstants<double>::pi
                                                   * (double) (2 * k + 1) / (double) (2 * order)));
                if (k == pairs - 1)
                    qk *= juce::jmax (0.1, q / 0.7071);   // resonance control on the sharpest stage

                add (hp ? makeHighPass2 (fs, freq, qk) : makeLowPass2 (fs, freq, qk));
            }

            if (order % 2 == 1)
                add (hp ? makeHighPass1 (fs, freq) : makeLowPass1 (fs, freq));
            break;
        }

        default:
            break;
    }

    out.count = n;
    return n;
}

double MiniLiveStripProcessor::responseDb (const Coeffs& c, double fs, double freq)
{
    const double w = juce::MathConstants<double>::twoPi * freq / fs;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;

    const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const std::complex<double> den = 1.0  + c.a1 * z1 + c.a2 * z2;

    return 20.0 * std::log10 (std::abs (num / den) + 1.0e-12);
}

MiniLiveStripProcessor::EqSections MiniLiveStripProcessor::getEqSections (int band) const
{
    const auto i = (size_t) juce::jlimit (0, kNumEqBands - 1, band);

    EqSections s;
    if (pEqOn[i]->load() < 0.5f || (int) i >= (int) std::lround (pEqVisible->load()))   // off / not shown: flat curve
        return s;

    makeEqSections ((int) std::lround (pEqType[i]->load()),
                    (int) std::lround (pEqShape[i]->load()),
                    (double) displaySampleRate.load(),
                    (double) pEqFreq[i]->load(),
                    (double) pEqGain[i]->load(),
                    (double) pEqQ[i]->load(),
                    s);
    return s;
}

//==============================================================================
void MiniLiveStripProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    soloGain.reset (sampleRate, 0.02);
    soloGain.setCurrentAndTargetValue (1.0f);
    displaySampleRate.store ((float) sampleRate);

    inputRing.fill (0.0f);
    outputRing.fill (0.0f);
    outWritePos = 0;
    writePos = 0;
    hopCounter = 0;
    candidateBin = -1;
    candidateCount = 0;
    lastReset = false;

    for (auto& n : notches) n = Notch {};
    for (auto& ch : filters)
        for (auto& f : ch)
            f.setPassthrough();

    for (auto& ch : eqFilters)
        for (auto& band : ch)
            for (auto& f : band)
                f.setPassthrough();

    for (auto& l : eqLast)
        l.fill (-1.0f);                   // forces a coefficient update on the first block
    eqCount.fill (0);

    outGainSmoothed.reset (sampleRate, 0.03);
    outGainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));

    fbMix.reset (sampleRate, 0.02);
    fbMix.setCurrentAndTargetValue (pFbOn->load() > 0.5f ? 1.0f : 0.0f);

    eqGlobalMix.reset (sampleRate, 0.015);
    eqGlobalMix.setCurrentAndTargetValue (pEqGlobalOn->load() > 0.5f ? 1.0f : 0.0f);

    // GEQ
    for (auto& ch : geqFilters)
        for (auto& f : ch)
            f.setPassthrough();
    geqLast.fill (-1000.0f);              // forces a coefficient update on the first block
    geqActive.fill (false);
    geqNum = 0;
    geqMix.reset (sampleRate, 0.015);
    geqMix.setCurrentAndTargetValue (pGeqOn->load() > 0.5f ? 1.0f : 0.0f);

    // Limiter: one-pole gain smoothing; attack / release are set per block from the parameters.
    limGain = 1.0f;
    limAttackCoef  = 1.0f - (float) std::exp (-1.0 / (0.001 * sampleRate));
    limReleaseCoef = 1.0f - (float) std::exp (-1.0 / (0.001 * sampleRate));
    limBoost.reset (sampleRate, 0.02);
    limBoost.setCurrentAndTargetValue (pLimOn->load() < 0.5f ? 1.0f
                                       : pLimMode->load() > 0.5f ? juce::Decibels::decibelsToGain (pCompMakeup->load())
                                                                 : juce::Decibels::decibelsToGain (-pLimThresh->load()));

    // Gate
    gateGain = 1.0f;
    gateEnv = 0.0f;
    gateHoldCount = 0;
    gateEnvDecay = (float) std::exp (-1.0 / (0.015 * sampleRate));      // 15 ms detector decay
    limInDisplay.store (-100.0f);   limGrDisplay.store (0.0f);
    gateInDisplay.store (-100.0f);  gateGrDisplay.store (0.0f);
    meterHold = 0.0f;
    meterDecay = std::exp (-1.0 / (0.3 * sampleRate));       // ~300 ms fall for the meter
    outLevelDisplay.store (0.0f);
    outLevelDisplayR.store (0.0f);
    meterHoldR = 0.0f;
    outIsStereo.store (false);

    for (int b = 0; b < kNumEqBands; ++b)
    {
        const auto i = (size_t) b;
        eqOn[i] = pEqOn[i]->load() > 0.5f && b < (int) std::lround (pEqVisible->load());
        eqMix[i].reset (sampleRate, 0.015);
        eqMix[i].setCurrentAndTargetValue (eqOn[i] ? 1.0f : 0.0f);
    }

    for (auto& s : spectrumPostDisplay) s.store (-120.0f);

    publishDisplay();
}

bool MiniLiveStripProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    return in == out && (in == juce::AudioChannelSet::mono()
                      || in == juce::AudioChannelSet::stereo());
}

//==============================================================================
// REALTIME AUDIO THREAD: no allocation, no locks, no file/UI access in here.
void MiniLiveStripProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);
    const int numSamples  = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    const bool fbOn = pFbOn->load() > 0.5f;
    settings.threshold   = getThreshold();
    settings.maxDepth    = pMaxDepth->load();
    settings.q           = pQ->load();
    settings.growthCheck = pGrowth->load() > 0.5f;

    // Scan off or Feedback off = frozen.
    settings.locked      = pScan->load() < 0.5f || ! fbOn;

    // Growth Check: the peak has to persist (and grow) for the "Growth time".
    settings.requiredFrames = settings.growthCheck
        ? juce::jmax (2, (int) std::lround (0.001 * (double) pGrowthTime->load() * fs / (double) kHop))
        : kRequiredFrames;

    fbMix.setTargetValue (fbOn ? 1.0f : 0.0f);
    eqGlobalMix.setTargetValue (pEqGlobalOn->load() > 0.5f ? 1.0f : 0.0f);
    geqMix.setTargetValue (pGeqOn->load() > 0.5f ? 1.0f : 0.0f);
    updateGeq();

    const bool  limOn    = pLimOn->load() > 0.5f;
    const bool  compMode = pLimMode->load() > 0.5f;
    const float limCeil  = juce::Decibels::decibelsToGain (pLimCeiling->load());
    const float cThr     = pCompThresh->load();
    const float cRatio   = pCompRatio->load();
    const float cKnee    = pCompKnee->load();

    // Limiter: the threshold ADDS gain in front of the limiter. Compressor: makeup gain after it.
    limBoost.setTargetValue (! limOn ? 1.0f
                             : compMode ? juce::Decibels::decibelsToGain (pCompMakeup->load())
                                        : juce::Decibels::decibelsToGain (-pLimThresh->load()));

    auto coefFor = [this] (float ms)
    {
        return 1.0f - (float) std::exp (-1.0 / (0.001 * (double) juce::jmax (0.01f, ms) * fs));
    };
    limAttackCoef  = coefFor (compMode ? pCompAttack->load()  : pLimAttack->load());
    limReleaseCoef = coefFor (compMode ? pCompRelease->load() : pLimRelease->load());

    const bool  gateOn = pGateOn->load() > 0.5f;
    const float gThr   = pGateThresh->load();
    const float gDepth = pGateDepth->load();
    const float gKnee  = pGateKnee->load();
    const float gAttackCoef  = coefFor (pGateAttack->load());
    const float gReleaseCoef = coefFor (pGateRelease->load());
    const int   gHoldSamples = (int) std::lround (0.001 * (double) pGateHold->load() * fs);

    const bool resetNow = pReset->load() > 0.5f;
    if (resetNow && ! lastReset)
    {
        clearAllNotches();
        resetDone.store (true);
    }
    lastReset = resetNow;

    updateEq();
    {
        // Solo (Pro-Q style): only while the EQ module is on; the soloed band must be on and shown.
        // Solo listens to the band's own frequency region, not to its gain:
        //   Low Shelf / Low Cut   -> everything below the frequency (low-pass)
        //   High Shelf / High Cut -> everything above the frequency (high-pass)
        //   Bell / Tilt / Notch   -> a band-pass around the frequency, width from the band's Q
        bool any = false;
        for (int b = 0; b < kNumEqBands; ++b)
        {
            const auto i = (size_t) b;
            const bool wasSolo = eqSolo[i];
            eqSolo[i] = eqOn[i] && pEqSolo[i]->load() > 0.5f;
            any |= eqSolo[i];

            if (! eqSolo[i])
                continue;

            if (! wasSolo)
                for (auto& ch : soloZ)
                    ch[i] = {};

            const int    type     = (int) std::lround (pEqType[i]->load());
            const bool   bandPass = (type == 4 || type == 5 || type == 6);
            const double bandQ    = juce::jmax (0.2, (double) pEqQ[i]->load());
            // Solo follows the band's Q: higher Q = narrower band-pass and steeper edges.
            // Solo is tighter than the band itself: effective Q = 2.5 x band Q, slope 2..4 stages (24..48 dB/oct).
            const double soloQ = 2.5 * bandQ;
            soloStagesB[i] = juce::jlimit (2, 4, 2 + (int) (bandQ / 3.5));
            const double f        = juce::jlimit (10.0, 0.45 * fs, (double) pEqFreq[i]->load());
            const double w0      = 2.0 * juce::MathConstants<double>::pi * f / fs;
            const double cw      = std::cos (w0);
            const double sw      = std::sin (w0);
            // Band-pass: width from the solo Q (bandwidth = frequency / Q).
            const double alpha   = sw / (2.0 * (bandPass ? soloQ : 0.7071));

            double b0 = 0.0, b1 = 0.0, b2 = 0.0;
            if (type == 0 || type == 2)                     // low-pass
            {
                b0 = (1.0 - cw) * 0.5;  b1 = 1.0 - cw;  b2 = b0;
            }
            else if (type == 1 || type == 3)                // high-pass
            {
                b0 = (1.0 + cw) * 0.5;  b1 = -(1.0 + cw);  b2 = b0;
            }
            else                                            // band-pass, constant 0 dB peak
            {
                b0 = alpha;  b1 = 0.0;  b2 = -alpha;
            }

            const double a0 = 1.0 + alpha;
            const SoloBq k { b0 / a0, b1 / a0, b2 / a0, -2.0 * cw / a0, (1.0 - alpha) / a0 };
            for (auto& stage : soloC[i])
                stage = k;
        }
        anySolo = any && pEqGlobalOn->load() > 0.5f;
        soloGain.setTargetValue (juce::Decibels::decibelsToGain (pSoloLevel->load()));
    }
    outGainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutGain->load()));

    float* left  = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    float blockPeakL = 0.0f, blockPeakR = 0.0f;
    float limInPeak = 0.0f, limGrMin = 1.0f;
    float gateInPeak = 0.0f, gateGainMin = 1.0f;

    const auto order = orderFromIndex ((int) std::lround (pChainOrder->load()));

    // ---- Module 1: feedback suppressor. The active notches always run so their state stays
    //      valid; the Feedback switch crossfades between dry and notched signal.
    auto stageFeedback = [&] (float& L, float& R)
    {
        const float dryL = L;
        const float dryR = R;
        float wetL = L;
        float wetR = R;
        for (int n = 0; n < kMaxNotches; ++n)
        {
            if (! notches[(size_t) n].active)
                continue;

            wetL = filters[0][(size_t) n].process (wetL);
            if (right != nullptr)
                wetR = filters[1][(size_t) n].process (wetR);
        }

        const float fm = fbMix.getNextValue();
        L = fm >= 1.0f ? wetL : dryL + (wetL - dryL) * fm;
        R = fm >= 1.0f ? wetR : dryR + (wetR - dryR) * fm;
    };

    // ---- Module 2: EQ bands (each with its own On/Off crossfade).
    auto stageEq = [&] (float& L, float& R)
    {
        const float eqDryL = L;
        const float eqDryR = R;

        // Direct-form biquad step for the solo listen filter.
        auto soloRun = [] (const SoloBq& k, std::array<double, 2>& z, float x) -> double
        {
            const double y = k.b0 * (double) x + z[0];
            z[0] = k.b1 * (double) x - k.a1 * y + z[1];
            z[1] = k.b2 * (double) x - k.a2 * y;
            return y;
        };

        // Solo stages in series: more stages = steeper edges (set by Solo Slope).
        auto soloChain = [&] (size_t bi, size_t ch, float x) -> float
        {
            double y = (double) x;
            for (size_t st = 0; st < (size_t) soloStagesB[bi]; ++st)
                y = soloRun (soloC[bi][st], soloZ[ch][bi][st], (float) y);
            return (float) y;
        };

        if (anySolo)
        {
            float sumL = 0.0f, sumR = 0.0f;
            for (int b = 0; b < kNumEqBands; ++b)
            {
                const auto bi = (size_t) b;
                if (! eqSolo[bi])
                    continue;

                sumL += soloChain (bi, 0, L);
                sumR += soloChain (bi, 1, R);
            }
            const float g = soloGain.getNextValue();
            L = sumL * g;
            R = sumR * g;
            return;
        }

        for (int b = 0; b < kNumEqBands; ++b)
        {
            const auto bi = (size_t) b;
            const float m = eqMix[bi].getNextValue();
            if (m <= 0.0f && ! eqOn[bi])
                continue;                                   // fully bypassed

            const int cnt = eqCount[bi];
            float wl = L;
            float wr = R;
            for (int k = 0; k < cnt; ++k)
            {
                wl = eqFilters[0][bi][(size_t) k].process (wl);
                if (right != nullptr)
                    wr = eqFilters[1][bi][(size_t) k].process (wr);
            }

            if (m >= 1.0f) { L = wl; R = wr; }
            else           { L += (wl - L) * m; R += (wr - R) * m; }
        }

        // EQ module switch: crossfade between the dry signal and the EQ bands (the bands keep running).
        const float em = eqGlobalMix.getNextValue();
        if (em < 1.0f)
        {
            L = eqDryL + (L - eqDryL) * em;
            R = eqDryR + (R - eqDryR) * em;
        }
    };

    // ---- Module 3: limiter / compressor (same slot, two modes).
    //      'lb' = limiter: gain from the threshold (before the limiter),
    //      compressor: makeup gain (after the compressor).
    auto stageLimiter = [&] (float& oL, float& oR)
    {
        const float lb = limBoost.getNextValue();
        const float pkIn = juce::jmax (std::abs (oL), right != nullptr ? std::abs (oR) : 0.0f);
        limInPeak = juce::jmax (limInPeak, pkIn);

        if (compMode)
        {
            // Compressor: stereo-linked peak detector, soft knee, gain smoothed with attack / release.
            if (limOn || limGain < 0.99999f)
            {
                float target = 1.0f;
                if (limOn && pkIn > 1.0e-6f)
                {
                    const float inDb = juce::Decibels::gainToDecibels (pkIn, -120.0f);
                    const float grDb = MiniLiveStripProcessor::compressorOutDb (inDb, cThr, cRatio, cKnee) - inDb;   // <= 0
                    target = juce::Decibels::decibelsToGain (grDb);
                }

                limGain += (target - limGain) * (target < limGain ? limAttackCoef : limReleaseCoef);
                oL *= limGain;
                oR *= limGain;
            }
            else
            {
                limGain = 1.0f;
            }

            oL *= lb;
            oR *= lb;

            if (limOn)      // safety: makeup / slow attack must never pass 0 dBFS
            {
                oL = juce::jlimit (-1.0f, 1.0f, oL);
                oR = juce::jlimit (-1.0f, 1.0f, oR);
            }
        }
        else
        {
            // Limiter (peak, stereo-linked, zero latency).
            // Switched off: the gain relaxes back to 1 (no click).
            oL *= lb;
            oR *= lb;

            if (limOn || limGain < 0.99999f)
            {
                const float pk = juce::jmax (std::abs (oL), right != nullptr ? std::abs (oR) : 0.0f);
                const float target = (limOn && pk > limCeil) ? limCeil / pk : 1.0f;

                limGain += (target - limGain) * (target < limGain ? limAttackCoef : limReleaseCoef);
                oL *= limGain;
                oR *= limGain;

                if (limOn)      // safety: a slow attack can overshoot, never let it past the ceiling
                {
                    oL = juce::jlimit (-limCeil, limCeil, oL);
                    oR = juce::jlimit (-limCeil, limCeil, oR);
                }
            }
            else
            {
                limGain = 1.0f;
            }
        }
        limGrMin = juce::jmin (limGrMin, limGain);
    };

    // ---- Module 4: gate. Peak envelope detector (15 ms decay), soft knee, hold.
    //      Switched off: the gain relaxes back to 1.
    auto stageGate = [&] (float& oL, float& oR)
    {
        const float det = juce::jmax (std::abs (oL), right != nullptr ? std::abs (oR) : 0.0f);
        gateEnv = juce::jmax (det, gateEnv * gateEnvDecay);
        gateInPeak = juce::jmax (gateInPeak, gateEnv);

        if (gateOn || gateGain < 0.99999f)
        {
            float targetDb = 0.0f;
            if (gateOn)
            {
                const float inDb = juce::Decibels::gainToDecibels (gateEnv, -120.0f);
                const float open = gKnee > 0.01f
                                       ? juce::jlimit (0.0f, 1.0f, (inDb - (gThr - 0.5f * gKnee)) / gKnee)
                                       : (inDb >= gThr ? 1.0f : 0.0f);
                targetDb = -gDepth * (1.0f - open);
            }

            const float target = targetDb < -0.001f ? juce::Decibels::decibelsToGain (targetDb) : 1.0f;

            if (target >= gateGain - 1.0e-4f)          // opening (or already there): restart hold
            {
                gateHoldCount = gHoldSamples;
                gateGain += (target - gateGain) * gAttackCoef;
            }
            else if (gateHoldCount > 0)                // hold: stay open for a while
            {
                --gateHoldCount;
            }
            else
            {
                gateGain += (target - gateGain) * gReleaseCoef;
            }

            oL *= gateGain;
            oR *= gateGain;
        }
        else
        {
            gateGain = 1.0f;
        }
        gateGainMin = juce::jmin (gateGainMin, gateGain);
    };

    // ---- Module 5: 31-band graphic EQ. Only the bands that are not flat run; the GEQ switch
    //      crossfades between dry and equalised signal.
    auto stageGeq = [&] (float& L, float& R)
    {
        const float gm = geqMix.getNextValue();     // runs every sample so the smoother stays in step
        if (geqNum == 0)
            return;

        float wl = L;
        float wr = R;
        for (int k = 0; k < geqNum; ++k)
        {
            const auto bi = (size_t) geqIdx[(size_t) k];
            wl = geqFilters[0][bi].process (wl);
            if (right != nullptr)
                wr = geqFilters[1][bi].process (wr);
        }

        if (gm >= 1.0f) { L = wl; R = wr; }
        else            { L += (wl - L) * gm; R += (wr - R) * gm; }
    };

    for (int i = 0; i < numSamples; ++i)
    {
        // Input gain (smoothed, no zipper noise): in front of ALL modules and of the analyser.
        const float ig  = outGainSmoothed.getNextValue();
        const float inL = left[i] * ig;
        const float inR = right != nullptr ? right[i] * ig : inL;

        // Feed the analyser with the (mono) INPUT signal.
        inputRing[(size_t) writePos] = right != nullptr ? 0.5f * (inL + inR) : inL;
        writePos = (writePos + 1) & (kFftSize - 1);

        if (++hopCounter >= kHop)
        {
            hopCounter = 0;
            analyse();      // may add/change notches
            analysePost();  // output RTA (display only)
        }

        // Processing chain: the five modules run in the order chosen in the editor.
        // Every module runs exactly once per sample, so all smoothers keep running.
        float oL = inL;
        float oR = inR;

        for (int slot = 0; slot < NumModules; ++slot)
        {
            switch (order[(size_t) slot])
            {
                case ModFeedback: stageFeedback (oL, oR); break;
                case ModEq:       stageEq       (oL, oR); break;
                case ModLimiter:  stageLimiter  (oL, oR); break;
                case ModGeq:      stageGeq      (oL, oR); break;
                default:          stageGate     (oL, oR); break;
            }
        }

        left[i] = oL;
        if (right != nullptr)
            right[i] = oR;


        blockPeakL = juce::jmax (blockPeakL, std::abs (oL));
        blockPeakR = juce::jmax (blockPeakR, right != nullptr ? std::abs (oR) : 0.0f);

        // Feed the output analyser (mono mix of the final output).
        outputRing[(size_t) outWritePos] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
        outWritePos = (outWritePos + 1) & (kFftSize - 1);
    }

    // Limiter / gate meters. The GR values keep the deepest reduction since the editor last read
    // them (the editor calls exchange (0)), so short gate / limiter events are never missed.
    auto keepMax = [] (std::atomic<float>& a, float v)
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    };
    limInDisplay.store   (juce::Decibels::gainToDecibels (limInPeak, -100.0f), std::memory_order_relaxed);
    gateInDisplay.store  (juce::Decibels::gainToDecibels (gateInPeak, -100.0f), std::memory_order_relaxed);
    keepMax (limGrDisplay,  -juce::Decibels::gainToDecibels (limGrMin,     -100.0f));
    keepMax (gateGrDisplay, -juce::Decibels::gainToDecibels (gateGainMin,  -100.0f));

    // Output level for the GUI meter (peak with a slow decay).
    const float meterFall = (float) std::pow (meterDecay, (double) numSamples);
    meterHold  = juce::jmax (blockPeakL, meterHold  * meterFall);
    meterHoldR = juce::jmax (blockPeakR, meterHoldR * meterFall);
    outLevelDisplay.store  (meterHold,  std::memory_order_relaxed);
    outLevelDisplayR.store (meterHoldR, std::memory_order_relaxed);
    outIsStereo.store (right != nullptr, std::memory_order_relaxed);
}

//==============================================================================
void MiniLiveStripProcessor::updateEq()
{
    for (int b = 0; b < kNumEqBands; ++b)
    {
        const auto i = (size_t) b;

        // Band On/Off: crossfaded in processBlock. When switching on from fully off,
        // start from clean filter states.
        const bool on = pEqOn[i]->load() > 0.5f && b < (int) std::lround (pEqVisible->load());
        if (on != eqOn[i])
        {
            if (on && eqMix[i].getCurrentValue() <= 0.0f)
                for (auto& ch : eqFilters)
                    for (auto& f : ch[i])
                        f.clearState();

            eqOn[i] = on;
            eqMix[i].setTargetValue (on ? 1.0f : 0.0f);
        }

        const float f = pEqFreq[i]->load();
        const float g = pEqGain[i]->load();
        const float q = pEqQ[i]->load();
        const int type  = (int) std::lround (pEqType[i]->load());
        const int shape = (int) std::lround (pEqShape[i]->load());

        auto& last = eqLast[i];
        if (f == last[0] && g == last[1] && q == last[2]
            && (float) type == last[3] && (float) shape == last[4])
            continue;

        last = { f, g, q, (float) type, (float) shape };

        EqSections s;
        int n = makeEqSections (type, shape, fs, (double) f, (double) g, (double) q, s);

        // Gain-based types with ~0 dB gain are bypassed.
        if (typeUsesGain (type) && std::abs (g) <= 0.05f)
            n = 0;

        eqCount[i] = n;

        for (auto& ch : eqFilters)
        {
            for (int k = 0; k < kMaxEqSections; ++k)
            {
                if (k < n) ch[i][(size_t) k].setCoeffs (s.c[(size_t) k]);
                else       ch[i][(size_t) k].setPassthrough();
            }
        }
    }
}

//==============================================================================
// GEQ: refresh the coefficients of the bands that changed and rebuild the list of non-flat bands.
void MiniLiveStripProcessor::updateGeq()
{
    int n = 0;

    for (int b = 0; b < kGeqBands; ++b)
    {
        const auto i = (size_t) b;
        const float g = pGeqGain[i]->load();

        if (g != geqLast[i])
        {
            geqLast[i] = g;
            const auto c = makePeaking (fs, (double) kGeqFreqs[i], (double) g, kGeqQ);
            for (auto& ch : geqFilters)
                ch[i].setCoeffs (c);
        }

        const bool act = std::abs (g) > 0.05f;
        if (act && ! geqActive[i])
            for (auto& ch : geqFilters)
                ch[i].clearState();               // coming from flat: start from clean state
        geqActive[i] = act;

        if (act)
            geqIdx[(size_t) n++] = b;
    }

    geqNum = n;
}

//==============================================================================
void MiniLiveStripProcessor::analyse()
{
    // Copy the ring buffer (oldest sample first) and window it.
    for (int i = 0; i < kFftSize; ++i)
        fftData[(size_t) i] = inputRing[(size_t) ((writePos + i) & (kFftSize - 1))];

    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // Magnitude in dBFS (a full-scale sine gives ~0 dB with a Hann window).
    const int   nb = kFftSize / 2;
    const float scale = 4.0f / (float) kFftSize;

    for (int k = 0; k <= nb; ++k)
    {
        const float db = 20.0f * std::log10 (fftData[(size_t) k] * scale + 1.0e-9f);
        magDb[(size_t) k] = db;
        spectrumDisplay[(size_t) k].store (db, std::memory_order_relaxed);   // for the RTA
    }

    // Scan off: the RTA keeps running, but no notch is added or changed.
    if (settings.locked)
    {
        candidateBin = -1;
        candidateCount = 0;
        updateNotches();
        publishDisplay();
        return;
    }

    // Search range: 100 Hz .. 12 kHz (never above 0.45 * fs).
    const float binHz = (float) (fs / (double) kFftSize);
    const int lo = juce::jmax (4, (int) std::ceil (100.0f / binHz));
    const int hi = juce::jmin (nb - 16,
                               (int) std::floor (juce::jmin (12000.0f, 0.45f * (float) fs) / binHz));

    if (hi > lo)
    {
        int peak = lo;
        for (int k = lo; k <= hi; ++k)
            if (magDb[(size_t) k] > magDb[(size_t) peak])
                peak = k;

        // Peak-to-neighbour ratio: how far the peak sticks out of its surroundings.
        float sum = 0.0f;
        int count = 0;
        for (int d = 3; d <= 12; ++d)
        {
            if (peak - d >= 1)  { sum += magDb[(size_t) (peak - d)]; ++count; }
            if (peak + d <= nb) { sum += magDb[(size_t) (peak + d)]; ++count; }
        }

        const float peakDb = magDb[(size_t) peak];
        const float pnr = peakDb - sum / (float) juce::jmax (1, count);

        if (pnr >= settings.threshold && peakDb > kGateDb)
        {
            // Is it the same peak as in the previous frames?
            if (candidateBin >= 0 && std::abs (peak - candidateBin) <= 1)
            {
                ++candidateCount;
            }
            else
            {
                candidateCount = 1;
                candidateStartDb = peakDb;
            }
            candidateBin = peak;

            const bool growing = ! settings.growthCheck || (peakDb - candidateStartDb >= 1.0f);

            if (candidateCount >= settings.requiredFrames && growing)
            {
                // Refine frequency with parabolic interpolation.
                const float a = magDb[(size_t) (peak - 1)];
                const float b = magDb[(size_t) peak];
                const float c = magDb[(size_t) (peak + 1)];
                const float denom = a - 2.0f * b + c;
                const float delta = std::abs (denom) > 1.0e-6f
                                        ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / denom)
                                        : 0.0f;

                registerHit (((float) peak + delta) * binHz);

                // If it keeps ringing we deepen again after a few more frames.
                candidateCount = juce::jmax (1, settings.requiredFrames / 2);
                candidateStartDb = peakDb;
            }
        }
        else
        {
            candidateBin = -1;
            candidateCount = 0;
        }
    }

    updateNotches();
    publishDisplay();
}

// Output RTA: same analysis as the input, but of the final output signal.
// Display only, it has no influence on the detection.
void MiniLiveStripProcessor::analysePost()
{
    for (int i = 0; i < kFftSize; ++i)
        fftData[(size_t) i] = outputRing[(size_t) ((outWritePos + i) & (kFftSize - 1))];

    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const int   nb = kFftSize / 2;
    const float scale = 4.0f / (float) kFftSize;

    for (int k = 0; k <= nb; ++k)
        spectrumPostDisplay[(size_t) k].store (20.0f * std::log10 (fftData[(size_t) k] * scale + 1.0e-9f),
                                               std::memory_order_relaxed);
}

void MiniLiveStripProcessor::registerHit (float freq)
{
    constexpr float deepenStep = 3.0f;
    const float initialDepth = juce::jmin (6.0f, settings.maxDepth);
    const float toleranceOctaves = 1.0f / settings.q;

    // Already a notch here? Only make it deeper. Its frequency never moves.
    for (auto& n : notches)
    {
        if (n.active && std::abs (std::log2 (freq / n.freq)) < toleranceOctaves)
        {
            n.targetDepthDb = juce::jmin (n.targetDepthDb + deepenStep, settings.maxDepth);
            return;
        }
    }

    // New frequency: take a free slot. If all slots are used, ignore it
    // (existing notches are never replaced; use Reset Notches to start over).
    for (int i = 0; i < kMaxNotches; ++i)
    {
        auto& n = notches[(size_t) i];
        if (n.active)
            continue;

        n.active = true;
        n.freq = freq;
        n.targetDepthDb = initialDepth;
        n.currentDepthDb = 0.0f;

        for (auto& ch : filters)
            ch[(size_t) i].setPassthrough();
        return;
    }
}

void MiniLiveStripProcessor::updateNotches()
{
    for (int i = 0; i < kMaxNotches; ++i)
    {
        auto& n = notches[(size_t) i];
        if (! n.active)
            continue;

        n.targetDepthDb = juce::jmin (n.targetDepthDb, settings.maxDepth);

        // Smooth the depth so coefficient changes don't click.
        n.currentDepthDb += (n.targetDepthDb - n.currentDepthDb) * 0.5f;

        if (n.targetDepthDb <= 0.0f && n.currentDepthDb < 0.05f)
        {
            n = Notch {};
            for (auto& ch : filters)
                ch[(size_t) i].setPassthrough();
            continue;
        }

        updateCoeffs (i);
    }
}

void MiniLiveStripProcessor::updateCoeffs (int index)
{
    const auto& n = notches[(size_t) index];

    // Peaking EQ with negative gain = notch of adjustable depth.
    const auto c = makePeaking (fs, (double) n.freq, -(double) n.currentDepthDb, (double) settings.q);

    for (auto& ch : filters)
        ch[(size_t) index].setCoeffs (c);
}

void MiniLiveStripProcessor::clearAllNotches()
{
    for (auto& n : notches)
        n = Notch {};

    for (auto& ch : filters)
        for (auto& f : ch)
            f.setPassthrough();

    candidateBin = -1;
    candidateCount = 0;
    publishDisplay();
}

void MiniLiveStripProcessor::publishDisplay()
{
    for (int i = 0; i < kMaxNotches; ++i)
    {
        const auto& n = notches[(size_t) i];
        notchFreqDisplay[(size_t) i].store (n.active ? n.freq : 0.0f);
        notchDepthDisplay[(size_t) i].store (n.active ? n.currentDepthDb : 0.0f);
    }
}

//==============================================================================
juce::AudioProcessorEditor* MiniLiveStripProcessor::createEditor()
{
    return new MiniLiveStripEditor (*this);
}

void MiniLiveStripProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MiniLiveStripProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // Sessions saved before "EQ Bands" existed had 6 bands: keep all of them shown.
            bool hasVisible = false;
            for (const auto& child : apvts.state)
                if (child.getProperty ("id").toString() == "eqVisible")
                    hasVisible = true;

            if (! hasVisible)
                if (auto* p = apvts.getParameter ("eqVisible"))
                    p->setValueNotifyingHost (p->convertTo0to1 (6.0f));

            if (juce::MessageManager::existsAndIsCurrentThread())
                history.clear();          // a loaded preset starts a fresh undo history
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MiniLiveStripProcessor();
}
