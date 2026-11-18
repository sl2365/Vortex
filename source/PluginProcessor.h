#pragma once

#include <JuceHeader.h>
#include "VortexSequencer.h"
#include <array>
#include <optional>
#include <vector>

class VortexAudioProcessor final : public juce::AudioProcessor
{
public:
    VortexAudioProcessor();
    ~VortexAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return parameters.getParameter("bypass");
    }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState parameters;
    static juce::AudioProcessorValueTreeState::ParameterLayout
        createParameterLayout();
    static juce::File getDataDirectory();
    static juce::File getSettingsDirectory();
    static juce::File getPresetsDirectory();
    static juce::File getSequencesDirectory();
    juce::Result savePresetToFile(const juce::File&);
    juce::Result loadPresetFromFile(const juce::File&);
    juce::File getCurrentPresetFile() const;
    std::optional<float> getSavedParameterValue(
        const juce::String& parameterId) const;
    int getActiveSequencerStep() const noexcept
    {
        return activeSequencerStep.load(std::memory_order_relaxed);
    }
    std::optional<float> getSequencerAnimatedNormalised(
        const juce::String& parameterId) const noexcept;
    std::array<float, 4> getPhaserSweepDisplay() const noexcept
    {
        return { phaserDisplayHz[0].load(std::memory_order_relaxed),
                 phaserDisplayHz[1].load(std::memory_order_relaxed),
                 phaserDisplayMinimumHz.load(std::memory_order_relaxed),
                 phaserDisplayMaximumHz.load(std::memory_order_relaxed) };
    }
    // Two representative moving taps and the current range, in milliseconds.
    // All readings are published once per audio block for the UI thread.
    std::array<float, 4> getChorusSweepDisplay() const noexcept
    {
        return { chorusDisplayMs[0].load(std::memory_order_relaxed),
                 chorusDisplayMs[1].load(std::memory_order_relaxed),
                 chorusDisplayMinimumMs.load(std::memory_order_relaxed),
                 chorusDisplayMaximumMs.load(std::memory_order_relaxed) };
    }
    std::array<float, 4> getFlangerSweepDisplay() const noexcept
    {
        return { flangerDisplayMs[0].load(std::memory_order_relaxed),
                 flangerDisplayMs[1].load(std::memory_order_relaxed),
                 flangerDisplayMinimumMs.load(std::memory_order_relaxed),
                 flangerDisplayMaximumMs.load(std::memory_order_relaxed) };
    }
    // Doubler markers show the first two voices; the span includes all voices.
    std::array<float, 4> getDoublerSweepDisplay() const noexcept
    {
        return { doublerDisplayMs[0].load(std::memory_order_relaxed),
                 doublerDisplayMs[1].load(std::memory_order_relaxed),
                 doublerDisplayMinimumMs.load(std::memory_order_relaxed),
                 doublerDisplayMaximumMs.load(std::memory_order_relaxed) };
    }

private:
    static constexpr int stateSchemaVersion = 2;
    static constexpr int channelCount = 2;
    static constexpr int maxPhaserStages = 12;
    using LinearSmoother = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

    struct ParameterPointers
    {
        std::atomic<float>* chorusRate = nullptr;
        std::atomic<float>* chorusDepth = nullptr;
        std::atomic<float>* chorusVoices = nullptr;
        std::atomic<float>* chorusFeedback = nullptr;
        std::atomic<float>* chorusPreDelay = nullptr;
        std::atomic<float>* chorusWidth = nullptr;
        std::atomic<float>* chorusEnabled = nullptr;
        std::atomic<float>* phaserEnabled = nullptr;
        std::atomic<float>* flangerEnabled = nullptr;
        std::atomic<float>* doublerEnabled = nullptr;
        std::array<std::atomic<float>*, 4> effectLevels {};
        std::atomic<float>* doublerVoices = nullptr;
        std::atomic<float>* doublerPitchDepth = nullptr;
        std::atomic<float>* doublerPitchRate = nullptr;
        std::atomic<float>* doublerTiming = nullptr;
        std::atomic<float>* phaserRate = nullptr;
        std::atomic<float>* phaserDepth = nullptr;
        std::atomic<float>* phaserStages = nullptr;
        std::atomic<float>* phaserCenter = nullptr;
        std::atomic<float>* phaserFeedback = nullptr;
        std::atomic<float>* phaserStereo = nullptr;
        std::atomic<float>* flangerRate = nullptr;
        std::atomic<float>* flangerDepth = nullptr;
        std::atomic<float>* flangerFeedback = nullptr;
        std::atomic<float>* flangerPhase = nullptr;
        std::atomic<float>* flangerOffset = nullptr;
        std::atomic<float>* flangerShape = nullptr;
        std::atomic<float>* flangerTone = nullptr;
        std::atomic<float>* doublerTightness = nullptr;
        std::atomic<float>* doublerDetune = nullptr;
        std::atomic<float>* doublerSpread = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* sequencerEnabled = nullptr;
        std::atomic<float>* sequencerHostSync = nullptr;
        std::atomic<float>* sequencerRate = nullptr;
        std::atomic<float>* sequencerStart = nullptr;
        std::atomic<float>* sequencerEnd = nullptr;
        std::atomic<float>* sequencerDirection = nullptr;
        std::atomic<float>* sequencerBipolar = nullptr;
        std::atomic<float>* sequencerUnipolarNegative = nullptr;
        std::atomic<float>* sequencerAttack = nullptr;
        std::atomic<float>* sequencerRelease = nullptr;
        std::atomic<float>* sequencerPeak = nullptr;
        std::atomic<float>* sequencerMove = nullptr;
        std::atomic<float>* sequencerGateShortLength = nullptr;
        std::atomic<float>* sequencerGateLongLength = nullptr;
        std::atomic<float>* sequencerGateMode = nullptr;
        std::array<std::atomic<float>*, vortex::sequencer::stepCount>
            sequencerSteps {};
        std::array<std::atomic<float>*, vortex::sequencer::stepCount>
            sequencerGateSteps {};
        std::array<std::atomic<float>*, vortex::sequencer::targetSlotCount>
            sequencerTargets {};
        std::array<std::atomic<float>*, vortex::sequencer::targetSlotCount>
            sequencerTargetEnabled {};
        std::array<std::atomic<float>*, vortex::sequencer::targetSlotCount>
            sequencerTargetDepth {};
    } raw;

    void bindParameterPointers();
    void setSavedParameterState(const juce::ValueTree&);
    static float load(const std::atomic<float>*) noexcept;
    bool isSequencerTargetAssigned(int targetChoice) const noexcept;
    float sequencerTargetDepth(int targetChoice) const noexcept;
    float sequencerModulatedValue(int targetChoice, float baseValue,
        float canonicalSequenceValue, bool bipolar, bool unipolarNegative,
        juce::RangedAudioParameter* valueParameter = nullptr) const noexcept;
    static float sanitiseSample(float, float) noexcept;
    void resetEffectState() noexcept;
    float readDelay(const std::vector<float>& line, int writeIndex, float delaySamples) const noexcept;

    std::atomic<int> activeSequencerStep { -1 };
    double sequencerFreePhase = 0.0;
    int sequencerHeldNotes = 0;
    float previousSequencerCanonical = 1.0f;
    float sequencerGateGain = 1.0f;
    std::array<juce::RangedAudioParameter*, vortex::sequencer::targets.size()>
        sequencerTargetParameters {};
    mutable std::array<std::atomic<float>, vortex::sequencer::targets.size()>
        sequencerAnimatedNormalised {};

    double currentSampleRate = 44100.0;
    LinearSmoother mixValue, outputGainValue, bypassValue;
    std::array<LinearSmoother, 4> effectLevelValues {};
    LinearSmoother chorusRateValue, chorusDepthValue, chorusFeedbackValue,
        chorusPreDelayValue, chorusWidthValue, phaserRateValue, phaserDepthValue, phaserStagesValue,
        phaserCenterValue, phaserFeedbackValue, phaserStereoValue,
        flangerRateValue, flangerDepthValue, flangerFeedbackValue,
        flangerPhaseValue, flangerOffsetValue, flangerShapeValue,
        flangerToneValue,
        doublerTightnessValue, doublerDetuneValue, doublerSpreadValue,
        doublerPitchDepthValue, doublerPitchRateValue, doublerTimingValue;
    std::array<LinearSmoother, 8> chorusVoiceGain {};
    std::array<LinearSmoother, 8> doublerVoiceGain {};
    std::array<LinearSmoother, 4> effectEnabledValues {};
    std::array<std::vector<float>, channelCount> chorusDelay, flangerDelay, doublerDelay;
    std::array<std::array<float, maxPhaserStages>, channelCount> phaserState {};
    // Inactive filter states are cleared once after their wet fade reaches zero.
    bool phaserProcessingLastSample = false, flangerProcessingLastSample = false;
    std::array<std::atomic<float>, channelCount> phaserDisplayHz {};
    std::atomic<float> phaserDisplayMinimumHz { 0.0f };
    std::atomic<float> phaserDisplayMaximumHz { 0.0f };
    std::array<std::atomic<float>, channelCount> chorusDisplayMs {}, flangerDisplayMs {};
    std::atomic<float> chorusDisplayMinimumMs { 0.0f }, chorusDisplayMaximumMs { 0.0f };
    std::atomic<float> flangerDisplayMinimumMs { 0.0f }, flangerDisplayMaximumMs { 0.0f };
    std::array<std::atomic<float>, 2> doublerDisplayMs {};
    std::atomic<float> doublerDisplayMinimumMs { 0.0f }, doublerDisplayMaximumMs { 0.0f };
    std::array<float, channelCount> flangerToneState {}, flangerToneSecondState {};
    std::array<float, channelCount> phaserFeedbackSample {}, flangerFeedbackSample {},
        chorusFeedbackSample {};
    int chorusWriteIndex = 0, flangerWriteIndex = 0, doublerWriteIndex = 0;
    std::array<double, 8> chorusVoicePhases {};
    double phaserPhase = 0.0, flangerPhase = 0.0;
    // Eight persistent, independent modulation clocks; no voice-count retuning.
    std::array<double, 8> doublerVoicePitchPhases {}, doublerVoiceDriftPhases {},
        doublerVoiceDetunePhases {};
    // Two static delay heads crossfade when Timing or Tightness changes.
    // This avoids the dramatic Doppler sweep of directly moving an 800 ms tap.
    float doublerBaseFromMs = 0.0f, doublerBaseToMs = 0.0f;
    float doublerBaseFade = 1.0f;
    mutable juce::CriticalSection savedParameterStateLock;
    juce::ValueTree savedParameterState;
    std::atomic<bool> pendingProcessingReset { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VortexAudioProcessor)
};
