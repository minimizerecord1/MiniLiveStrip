#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

//==============================================================================
// Mini Live Strip: automatic feedback suppressor for live sound + 6-band EQ.
//
// Audio path: zero latency.
//   input -> bank of narrow notch filters (feedback suppressor) -> 6-band EQ
// Analysis path: the input is analysed with an FFT every kHop samples. It only
// decides WHERE to put notches, it never delays the audio.
//
// Notches are permanent: once placed, a notch never moves, never fades and is
// never stolen. Turn "Scan Feedback" off to freeze the set, "Reset Notches"
// to clear.
//
// EQ bands: each has Freq / Q / Gain, a Slope (= filter type) and a Shape
// (6 dB/oct ... 42 dB/oct, i.e. 1..7 cascaded stages) and an On/Off switch.
//
// Feedback switch: bypasses the notch bank (click-free crossfade) and freezes
// detection. Existing notches are kept.
// Growth time: how long a peak has to persist (and grow) before Growth Check
// accepts it as feedback.
// RTA: both the input (pre) and the final output (post notches + EQ + gain) are analysed.
//
// Limiter (after the output gain): peak limiter, no look-ahead (so still zero latency).
//   Mode switch ("limMode"): the same slot can run a COMPRESSOR instead
//   (Threshold / Ratio / Knee / Attack / Release / Makeup, hard clip at 0 dBFS as a safety).
//   Threshold 0..-30 dB : ADDS gain (makeup = -threshold) in front of the limiter.
//   Ceiling   0..-10 dB : maximum output level.
//   Attack 0.5..500 ms, Release 1..500 ms.
//   A hard clip at the ceiling catches the first part of an overshoot.
//
// Gate (very last in the chain, after the limiter): stereo-linked downward gate with
// Threshold / Depth / Knee / Attack / Hold / Release and an on/off switch.
//
// Processing chain: the five modules (Feedback, EQ + Gain Out, Limiter / Compressor, Gate, GEQ) run in
// an order the user chooses by dragging the big module buttons in the editor. The order is stored
// in the "chainOrder" parameter (0..119 = index of the permutation of the 5 modules; 0..23 are the
// old 4-module orders with the GEQ last).
//
// EQ: up to kNumEqBands (10) bands. The editor shows kMinEqBands (4) at first and has an
// "add band" cell ("eqVisible" parameter). Bands that are not shown are bypassed.
//==============================================================================

//==============================================================================
// Undo / redo of user parameter edits (knobs, buttons, combo boxes, EQ points).
// One undo step = one gesture (a whole drag is one step). Message thread only.
// Parameters listed in 'ignoredIds' (the momentary Reset button) are not recorded.
class ParameterHistory : private juce::AudioProcessorParameter::Listener
{
public:
    ParameterHistory() = default;
    ~ParameterHistory() override { detach(); }

    void attach (juce::AudioProcessor&, const juce::StringArray& ignoredIds);
    void detach();
    void clear();
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

private:
    struct Change { int index; float before; float after; };
    using Step = std::vector<Change>;

    void parameterValueChanged (int index, float newValue) override;
    void parameterGestureChanged (int index, bool starting) override;
    void finishGesture();
    void apply (const Step&, bool useAfter);

    juce::AudioProcessor* owner = nullptr;
    std::unique_ptr<std::atomic<float>[]> known;      // last normalised value of every parameter
    int numKnown = 0;
    std::vector<char> ignored;

    Step pending;
    std::atomic<int>  openGestures { 0 };
    std::atomic<bool> applying { false };
    std::vector<Step> undoStack, redoStack;

    static constexpr size_t kMaxSteps = 200;
};

//==============================================================================
class MiniLiveStripProcessor : public juce::AudioProcessor
{
public:
    static constexpr int   kMaxNotches = 35;
    static constexpr int   kNumEqBands = 10;               // maximum number of EQ bands
    static constexpr int   kMinEqBands = 4;                // bands shown at first
    static constexpr int   kNumShapes  = 7;                // 6, 12, ... 42 dB/oct
    static constexpr int   kMaxEqSections = 14;            // Tilt at 42 dB/oct = 7 x 2 biquads
    static constexpr int   kFftOrder   = 11;               // 2048-point FFT
    static constexpr int   kFftSize    = 1 << kFftOrder;
    static constexpr int   kHop        = 512;              // analysis every 512 samples
    static constexpr float kGateDb     = -65.0f;           // ignore peaks quieter than this

    // EQ slope = filter type (order matches the choice parameter / combo box)
    // (the last real type is called NotchFilter so it doesn't hide struct Notch below)
    // Modules of the processing chain (ids used by the editor's big buttons).
    enum Module { ModFeedback = 0, ModEq, ModLimiter, ModGate, ModGeq, NumModules };
    using ModuleOrder = std::array<int, NumModules>;
    static constexpr int kNumOrders = 120;                 // 5! permutations of the modules

    // 31-band graphic EQ (ISO 1/3 octave centres, 20 Hz .. 20 kHz), +/-30 dB per band.
    static constexpr int    kGeqBands = 31;
    static constexpr double kGeqQ     = 4.32;              // ~ 1/3 octave bandwidth
    static constexpr std::array<float, kGeqBands> kGeqFreqs {
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f, 200.0f,
        250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f,
        2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f };

    // "chainOrder" parameter <-> order of the modules (slot 0 runs first).
    static ModuleOrder orderFromIndex (int index)
    {
        ModuleOrder a {};
        for (int i = 0; i < NumModules; ++i)
            a[(size_t) i] = i;
        for (int k = 0; k < juce::jlimit (0, kNumOrders - 1, index); ++k)
            std::next_permutation (a.begin(), a.end());
        return a;
    }

    static int indexFromOrder (const ModuleOrder& order)
    {
        ModuleOrder a {};
        for (int i = 0; i < NumModules; ++i)
            a[(size_t) i] = i;
        for (int k = 0; k < kNumOrders; ++k)
        {
            if (a == order)
                return k;
            std::next_permutation (a.begin(), a.end());
        }
        return 0;
    }

    enum EqType { LowShelf = 0, HighShelf, LowCut, HighCut, Bell, Tilt, NotchFilter, NumEqTypes };

    MiniLiveStripProcessor();
    ~MiniLiveStripProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mini Live Strip"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Presets: XML files in <AppData>/MiniLiveStrip/Presets. "Default.xml" is loaded by every new instance.
    static juce::File presetFolder();
    static juce::Array<juce::File> listPresets();        // user presets (Default.xml not included)
    juce::Result savePreset (const juce::String& name);
    juce::Result saveDefaultPreset();
    juce::Result loadPresetFile (const juce::File& file);
    void resetToFactory();                               // every parameter back to its default
    bool applyStateXml (const juce::XmlElement& xml);    // used by setStateInformation and presets
    juce::Result writeStateTo (const juce::File& file) const;

    juce::AudioProcessorValueTreeState apvts;
    ParameterHistory history;                 // undo / redo (declared after apvts)

    //==========================================================================
    // Biquad coefficients (RBJ cookbook), also used by the editor to draw curves.
    // First-order filters use b2 = a2 = 0.
    struct Coeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    // The cascade of biquads that makes up one EQ band.
    struct EqSections
    {
        std::array<Coeffs, kMaxEqSections> c {};
        int count = 0;
    };

    static Coeffs makePeaking   (double fs, double freq, double gainDb, double q);
    static Coeffs makeLowShelf  (double fs, double freq, double gainDb, double q);
    static Coeffs makeHighShelf (double fs, double freq, double gainDb, double q);
    static Coeffs makeLowPass2  (double fs, double freq, double q);
    static Coeffs makeHighPass2 (double fs, double freq, double q);
    static Coeffs makeLowPass1  (double fs, double freq);
    static Coeffs makeHighPass1 (double fs, double freq);
    static Coeffs makeNotch2    (double fs, double freq, double q);

    // Builds the biquad cascade for one EQ band. shapeIdx 0..6 = 6..42 dB/oct.
    // Returns the number of sections written to 'out'.
    static int makeEqSections (int type, int shapeIdx, double fs, double freq,
                               double gainDb, double q, EqSections& out);

    static bool typeUsesGain (int type)
    {
        return type == LowShelf || type == HighShelf || type == Bell || type == Tilt;
    }

    // Magnitude response of one biquad in dB at the given frequency.
    static double responseDb (const Coeffs& c, double fs, double freq);

    // Current EQ band sections from the parameters (for the GUI).
    EqSections getEqSections (int band) const;

    // Sensitivity knob 0..10 -> detection threshold in dB (10 = most sensitive).
    static float sensitivityToThresholdDb (float sensitivity)
    {
        return 30.0f - 2.2f * juce::jlimit (0.0f, 10.0f, sensitivity);
    }

    // Static compressor curve: output level (dB, before makeup) for an input level (dB).
    // Used by the audio thread and by the editor's graph, so both always agree.
    static float compressorOutDb (float inDb, float thr, float ratio, float knee)
    {
        const float over  = inDb - thr;
        const float slope = 1.0f / juce::jmax (1.0f, ratio);

        if (knee < 0.01f)
            return over <= 0.0f ? inDb : thr + over * slope;
        if (2.0f * over < -knee)
            return inDb;
        if (2.0f * over > knee)
            return thr + over * slope;

        const float k = over + 0.5f * knee;                       // soft knee
        return inDb + (slope - 1.0f) * k * k / (2.0f * knee);
    }

    float getThreshold() const { return sensitivityToThresholdDb (pSens->load()); }
    float getMaxDepth()  const { return pMaxDepth->load(); }
    float getQ()         const { return pQ->load(); }
    bool  getFeedbackOn() const { return pFbOn->load() > 0.5f; }
    float getGrowthTimeMs() const { return pGrowthTime->load(); }

    //==========================================================================
    // Written by the audio thread, read by the GUI thread (for the display).
    std::array<std::atomic<float>, kMaxNotches> notchFreqDisplay;   // Hz, 0 = unused
    std::array<std::atomic<float>, kMaxNotches> notchDepthDisplay;  // dB of cut

    // RTA: latest spectrum (dBFS per FFT bin, input signal before the notches).
    std::array<std::atomic<float>, kFftSize / 2 + 1> spectrumDisplay;
    // RTA "post": spectrum of the final output (after notches, EQ and output gain).
    std::array<std::atomic<float>, kFftSize / 2 + 1> spectrumPostDisplay;
    std::atomic<float> displaySampleRate { 48000.0f };

    // Set by the audio thread after "Reset Notches" was handled; the editor
    // uses it to flip the reset switch back off (so it behaves like a button).
    std::atomic<bool> resetDone { false };

    // Output peak level (linear, after the limiter, slow decay) for the GUI meter.
    std::atomic<float> outLevelDisplay { 0.0f };
    std::atomic<float> outLevelDisplayR { 0.0f };   // right channel (stereo only)
    std::atomic<bool>  outIsStereo { false };       // host runs the plugin in stereo


    // Limiter / gate meters for the sub-displays (dB, written once per block).
    std::atomic<float> limInDisplay   { -100.0f };   // peak level in front of the limiter (before the threshold gain)
    std::atomic<float> limGrDisplay   { 0.0f };      // limiter gain reduction, positive dB
    std::atomic<float> gateInDisplay  { -100.0f };   // gate detector level
    std::atomic<float> gateGrDisplay  { 0.0f };      // gate attenuation, positive dB

private:
    //==========================================================================
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;

        // Transposed direct form II, double precision (narrow low-frequency
        // notches are numerically touchy in float).
        float process (float in) noexcept
        {
            const double x = (double) in;
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return (float) y;
        }

        void setCoeffs (const Coeffs& c) noexcept
        {
            b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2;
        }

        void setPassthrough() noexcept
        {
            b0 = 1.0; b1 = b2 = a1 = a2 = 0.0;
            z1 = z2 = 0.0;
        }

        void clearState() noexcept { z1 = z2 = 0.0; }
    };

    struct Notch
    {
        bool  active = false;
        float freq = 0.0f;
        float targetDepthDb = 0.0f;
        float currentDepthDb = 0.0f;
    };

    struct Settings
    {
        float threshold = 18.0f;       // peak-to-neighbour ratio in dB
        float maxDepth = 15.0f;        // max cut per notch in dB
        float q = 30.0f;
        bool  growthCheck = false;
        bool  locked = false;          // true = detection frozen (Scan off or Feedback off)
        int   requiredFrames = 6;      // hops a peak must persist (from Growth time when Growth Check is on)
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void analyse();                       // runs once per hop (input / pre)
    void analysePost();                   // runs once per hop (output / post, display only)
    void registerHit (float freq);        // feedback detected at freq
    void updateNotches();                 // depth smoothing + coefficients
    void updateCoeffs (int index);
    void updateEq();                      // once per block
    void updateGeq();                     // once per block
    void clearAllNotches();
    void publishDisplay();

    //==========================================================================
    static constexpr int kRequiredFrames = 6;        // ~ 6 hops of a persistent peak

    double fs = 48000.0;
    Settings settings;

    std::atomic<float>* pSens     = nullptr;
    std::atomic<float>* pMaxDepth = nullptr;
    std::atomic<float>* pQ        = nullptr;
    std::atomic<float>* pGrowth   = nullptr;
    std::atomic<float>* pScan     = nullptr;
    std::atomic<float>* pReset    = nullptr;
    std::atomic<float>* pOutGain  = nullptr;   // output gain in dB
    std::atomic<float>* pFbOn       = nullptr; // feedback suppressor on/off
    std::atomic<float>* pGrowthTime = nullptr; // growth check time window in ms
    std::atomic<float>* pLimOn      = nullptr; // limiter on/off
    std::atomic<float>* pLimThresh  = nullptr; // limiter threshold (dB, adds gain)
    std::atomic<float>* pLimCeiling = nullptr; // limiter ceiling (dB)
    std::atomic<float>* pLimAttack  = nullptr; // ms
    std::atomic<float>* pLimRelease = nullptr; // ms
    std::atomic<float>* pGateOn      = nullptr;
    std::atomic<float>* pGateThresh  = nullptr; // dB
    std::atomic<float>* pGateDepth   = nullptr; // dB of attenuation when closed
    std::atomic<float>* pGateKnee    = nullptr; // dB
    std::atomic<float>* pGateAttack  = nullptr; // ms
    std::atomic<float>* pGateHold    = nullptr; // ms
    std::atomic<float>* pGateRelease = nullptr; // ms
    std::atomic<float>* pEqVisible   = nullptr; // number of EQ bands shown / active (4..10)
    std::atomic<float>* pLimMode     = nullptr; // 0 = limiter, 1 = compressor
    std::atomic<float>* pCompThresh  = nullptr; // dB
    std::atomic<float>* pCompRatio   = nullptr; // x:1
    std::atomic<float>* pCompKnee    = nullptr; // dB
    std::atomic<float>* pCompAttack  = nullptr; // ms
    std::atomic<float>* pCompRelease = nullptr; // ms
    std::atomic<float>* pCompMakeup  = nullptr; // dB
    std::atomic<float>* pChainOrder  = nullptr; // order of the 4 modules (0..23)
    std::atomic<float>* pEqGlobalOn  = nullptr; // EQ module on / off (bands only, Gain Out stays active)
    std::atomic<float>* pGeqOn       = nullptr; // GEQ module on / off
    std::array<std::atomic<float>*, kGeqBands> pGeqGain {};   // GEQ band gains (dB)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> geqMix;     // GEQ module on / off crossfade

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> eqGlobalMix;   // EQ module on / off
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> fbMix;      // 0 = bypass, 1 = notches

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outGainSmoothed;

    // Limiter: threshold (adds gain), ceiling, attack and release come from parameters.
    float  limGain = 1.0f;
    float  limAttackCoef = 0.0f, limReleaseCoef = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> limBoost;   // makeup gain from the threshold

    // Gate (last in the chain, after the limiter)
    float  gateGain = 1.0f;
    float  gateEnv = 0.0f;                    // peak envelope used as detector
    float  gateEnvDecay = 0.0f;
    int    gateHoldCount = 0;
    float  meterHold = 0.0f;                  // output peak with decay (linear)
    float  meterHoldR = 0.0f;                 // right channel output peak with decay
    double meterDecay = 0.0;                  // per-sample decay factor of meterHold

    std::array<std::atomic<float>*, kNumEqBands> pEqFreq  {};
    std::array<std::atomic<float>*, kNumEqBands> pEqGain  {};
    std::array<std::atomic<float>*, kNumEqBands> pEqQ     {};
    std::array<std::atomic<float>*, kNumEqBands> pEqType  {};
    std::array<std::atomic<float>*, kNumEqBands> pEqShape {};
    std::array<std::atomic<float>*, kNumEqBands> pEqOn    {};

    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kNumEqBands> eqMix;
    std::array<std::atomic<float>*, kNumEqBands> pEqSolo {};
    std::array<bool, kNumEqBands> eqSolo {};
    bool anySolo = false;
    struct SoloBq { double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };   // biquad for the solo listen filter
    std::array<std::array<SoloBq, 4>, kNumEqBands> soloC {};   // up to 4 cascaded stages (12..48 dB/oct)
    std::array<std::array<std::array<std::array<double, 2>, 4>, kNumEqBands>, 2> soloZ {};   // [channel][band][stage] = {z1, z2}
    std::atomic<float>* pSoloLevel = nullptr;     // solo listening level in dB (drag vertically on the SOLO button)
    juce::SmoothedValue<float> soloGain;          // linear gain of pSoloLevel, smoothed
    // Solo width and slope follow the band's Q (see processBlock): stages per band, 1..4 = 12..48 dB/oct
    std::array<int, kNumEqBands> soloStagesB {};
    std::array<bool, kNumEqBands> eqOn {};

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize,
                                                 juce::dsp::WindowingFunction<float>::hann,
                                                 false };

    std::array<float, kFftSize> inputRing {};
    std::array<float, kFftSize> outputRing {};
    int outWritePos = 0;
    std::array<float, 2 * kFftSize> fftData {};
    std::array<float, kFftSize / 2 + 1> magDb {};
    int writePos = 0;
    int hopCounter = 0;

    int   candidateBin = -1;
    int   candidateCount = 0;
    float candidateStartDb = 0.0f;
    bool  lastReset = false;

    std::array<Notch, kMaxNotches> notches;
    std::array<std::array<Biquad, kMaxNotches>, 2> filters;   // [channel][notch]

    // [channel][band][section]
    std::array<std::array<std::array<Biquad, kMaxEqSections>, kNumEqBands>, 2> eqFilters;
    std::array<int, kNumEqBands> eqCount {};                  // active sections per band
    std::array<std::array<float, 5>, kNumEqBands> eqLast {};  // last freq/gain/q/type/shape seen

    // GEQ: one peaking filter per band, only the bands that are not flat run.
    std::array<std::array<Biquad, kGeqBands>, 2> geqFilters;  // [channel][band]
    std::array<float, kGeqBands> geqLast {};                  // last gain seen per band
    std::array<bool, kGeqBands> geqActive {};
    std::array<int, kGeqBands> geqIdx {};                     // indices of the active bands
    int geqNum = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MiniLiveStripProcessor)
};
