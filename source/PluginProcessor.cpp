#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <cstdint>
#include <utility>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
namespace ParameterIds
{
constexpr auto chorusRate = "chorusRate";
constexpr auto chorusDepth = "chorusDepth";
constexpr auto chorusVoices = "chorusVoices";
constexpr auto chorusLfoShape = "chorusLfoShape";
constexpr auto chorusFeedback = "chorusFeedback";
constexpr auto chorusPreDelay = "chorusPreDelay";
constexpr auto chorusWidth = "chorusWidth";
constexpr auto chorusEnabled = "chorusEnabled";
constexpr auto phaserEnabled = "phaserEnabled";
constexpr auto flangerEnabled = "flangerEnabled";
constexpr auto doublerEnabled = "doublerEnabled";
constexpr std::array<const char*, 4> effectLevelIds {
    "chorusLevel", "phaserLevel", "flangerLevel", "doublerLevel" };
constexpr std::array<const char*, 4> effectLevelNames {
    "Chorus Level", "Phaser Level", "Flanger Level", "Doubler Level" };
constexpr auto doublerVoices = "doublerVoices";
constexpr auto doublerPitchDepth = "doublerPitchDepth";
constexpr auto doublerPitchRate = "doublerPitchRate";
constexpr auto doublerTiming = "doublerTiming";
constexpr auto phaserRate = "phaserRate";
constexpr auto phaserDepth = "phaserDepth";
constexpr auto phaserStages = "phaserStages";
constexpr auto phaserCenter = "phaserCenter";
constexpr auto phaserFeedback = "phaserFeedback";
constexpr auto phaserStereo = "phaserStereo";
constexpr auto phaserLfoShape = "phaserLfoShape";
constexpr auto flangerRate = "flangerRate";
constexpr auto flangerDepth = "flangerDepth";
constexpr auto flangerFeedback = "flangerFeedback";
constexpr auto flangerPhase = "flangerPhase";
constexpr auto flangerOffset = "flangerOffset";
constexpr auto flangerShape = "flangerShape";
constexpr auto flangerTone = "flangerTone";
constexpr auto doublerTightness = "doublerTightness";
constexpr auto doublerDetune = "doublerDetune";
constexpr auto doublerSpread = "doublerSpread";
constexpr auto mix = "mix";
constexpr auto output = "output";
constexpr auto bypass = "bypass";
}

constexpr float smoothingSeconds = 0.060f;

float nextChorusRandom(std::uint32_t& state) noexcept
{
    // Xorshift32: used only at LFO cycle boundaries, never per sample.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return 2.0f * static_cast<float>(state & 0x00ffffffu)
        / static_cast<float>(0x00ffffffu) - 1.0f;
}

// Modulation phases use cycles (0..1); the chorus harmonic may exceed 1.
// Sine and rounded-square functions are precomputed in prepareToPlay.
// The audio callback uses linear interpolation rather than trig functions.
// The extra entry handles the phase-one boundary without an additional wrap.
float modulationSine(const std::array<float, 65537>& table,
                     float cycles) noexcept
{
    if (cycles < 0.0f || cycles >= 1.0f)
        cycles -= std::floor(cycles);
    const float position = cycles * 65536.0f;
    const int index = juce::jmin(65535, static_cast<int>(position));
    const float fraction = position - static_cast<float>(index);
    const float low = table[static_cast<size_t>(index)];
    return low + fraction *
        (table[static_cast<size_t>(index + 1)] - low);
}

float chorusShapeWave(int shape, float phase, int voice,
                      float randomStart, float randomEnd,
                      const std::array<float, 65537>& sineTable,
                      const std::array<float, 65537>& roundedTable) noexcept
{
    switch (shape)
    {
        // Preserve the original two-harmonic chorus waveform shape.
        case 0: return 0.84f * modulationSine(sineTable, phase)
                     + 0.16f * modulationSine(sineTable,
                         2.0f * phase + 0.07f * static_cast<float>(voice));
        case 1: return 1.0f - 4.0f * std::abs(phase - 0.5f); // triangle
        case 2: return 2.0f * phase - 1.0f;                 // saw
        case 3: return 1.0f - 2.0f * phase;                 // ramp
        case 4: return 2.0f * phase * phase * phase - 1.0f;
        case 5: { const float q = 1.0f - phase;
                  return 1.0f - 2.0f * q * q * q; }
        case 6: { // Random: smoothly interpolated independent values.
            const float eased = phase * phase * (3.0f - 2.0f * phase);
            return randomStart + (randomEnd - randomStart) * eased;
        }
        case 7: // Original rounded-square transfer, table-interpolated.
            return modulationSine(roundedTable, phase);
        default: return 0.0f;
    }
}

float chorusFeedbackCoefficient(float percentage) noexcept
{
    const float normalised = juce::jlimit(0.0f, 1.0f, percentage * 0.01f);
    // Keep the loop below unity, but give the useful part of the control a
    // stronger rise than the previous linear 0..0.95 mapping.
    return 0.985f * std::pow(normalised, 0.70f);
}

float flangerFeedbackCoefficient(float percentage) noexcept
{
    // The knob is bipolar (-100..+100%), but the loop stays below unity.
    // A concave taper gives usable regeneration throughout 0..60%.
    const float amount = juce::jlimit(0.0f, 1.0f,
        std::abs(percentage) * 0.01f);
    return std::copysign(0.96f * std::pow(amount, 0.48f), percentage);
}
}

VortexAudioProcessor::VortexAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "VortexParameters", createParameterLayout())
{
    bindParameterPointers();
    for (auto& value : sequencerAnimatedNormalised)
        value.store(-1.0f, std::memory_order_relaxed);
    parameters.state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    setSavedParameterState(parameters.copyState());
    getSettingsDirectory().createDirectory();
    getPresetsDirectory().createDirectory();
    getSequencesDirectory().createDirectory();
}

juce::File VortexAudioProcessor::getDataDirectory()
{
#if JUCE_WINDOWS
    std::array<wchar_t, 32768> modulePath {};
    const auto moduleHandle = static_cast<HMODULE>(
        juce::Process::getCurrentModuleInstanceHandle());
    if (moduleHandle != nullptr)
    {
        const auto pathLength = ::GetModuleFileNameW(
            moduleHandle, modulePath.data(),
            static_cast<DWORD>(modulePath.size()));
        if (pathLength > 0 && pathLength < modulePath.size() - 1)
        {
            const juce::File moduleFile(juce::String(modulePath.data()));
            auto folder = moduleFile.getParentDirectory();
            for (int depth = 0; depth < 6; ++depth)
            {
                if (folder.hasFileExtension(".vst3"))
                    return folder.getParentDirectory().getChildFile("Vortex_Data");
                const auto parent = folder.getParentDirectory();
                if (parent == folder)
                    break;
                folder = parent;
            }
            return moduleFile.getParentDirectory().getChildFile("Vortex_Data");
        }
    }
#endif
    return juce::File::getSpecialLocation(juce::File::currentApplicationFile)
        .getParentDirectory().getChildFile("Vortex_Data");
}

juce::File VortexAudioProcessor::getSettingsDirectory()
{
    return getDataDirectory().getChildFile("Settings");
}

juce::File VortexAudioProcessor::getPresetsDirectory()
{
    return getDataDirectory().getChildFile("Presets");
}

juce::File VortexAudioProcessor::getSequencesDirectory()
{
    return getDataDirectory().getChildFile("Sequences");
}

juce::Result VortexAudioProcessor::savePresetToFile(
    const juce::File& presetFile)
{
    if (presetFile == juce::File())
        return juce::Result::fail("No preset file was selected.");

    const auto parent = presetFile.getParentDirectory();
    const auto root = getPresetsDirectory();
    if (parent != root && ! parent.isAChildOf(root))
        return juce::Result::fail(
            "Presets must be saved inside the Vortex Presets folder.");
    if (! parent.isDirectory() && parent.createDirectory().failed())
        return juce::Result::fail("The preset folder could not be created.");

    auto state = parameters.copyState();
    state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    state.removeProperty("currentPresetPath", nullptr);
    const auto xml = state.createXml();
    if (xml == nullptr)
        return juce::Result::fail("The plugin state could not be encoded.");

    const juce::TemporaryFile temporaryFile(presetFile);
    if (! temporaryFile.getFile().replaceWithText(xml->toString())
        || ! temporaryFile.overwriteTargetFileWithTemporary())
        return juce::Result::fail("The preset could not be written safely.");

    parameters.state.setProperty(
        "currentPresetPath", presetFile.getFullPathName(), nullptr);
    setSavedParameterState(state);
    return juce::Result::ok();
}

juce::Result VortexAudioProcessor::loadPresetFromFile(
    const juce::File& presetFile)
{
    if (! presetFile.existsAsFile())
        return juce::Result::fail("The preset file no longer exists.");
    const auto parent = presetFile.getParentDirectory();
    const auto root = getPresetsDirectory();
    if (parent != root && ! parent.isAChildOf(root))
        return juce::Result::fail(
            "Presets must be loaded from the Vortex Presets folder.");

    const auto xml = juce::XmlDocument::parse(presetFile);
    if (xml == nullptr || ! xml->hasTagName("VortexParameters"))
        return juce::Result::fail("This is not a Vortex preset.");
    auto restored = juce::ValueTree::fromXml(*xml);
    if (static_cast<int>(restored.getProperty("schemaVersion", 0))
        != stateSchemaVersion)
        return juce::Result::fail(
            "This preset does not use the current Vortex schema.");

    restored.removeProperty("currentPresetPath", nullptr);
    setSavedParameterState(restored);
    parameters.replaceState(restored);
    parameters.state.setProperty(
        "currentPresetPath", presetFile.getFullPathName(), nullptr);
    pendingProcessingReset.store(true, std::memory_order_release);
    return juce::Result::ok();
}

juce::File VortexAudioProcessor::getCurrentPresetFile() const
{
    const auto path = parameters.state.getProperty(
        "currentPresetPath", juce::String()).toString();
    return path.isEmpty() ? juce::File() : juce::File(path);
}

void VortexAudioProcessor::setSavedParameterState(const juce::ValueTree& state)
{
    const juce::ScopedLock lock(savedParameterStateLock);
    savedParameterState = state.createCopy();
    savedParameterState.removeProperty("currentPresetPath", nullptr);
}

std::optional<float> VortexAudioProcessor::getSavedParameterValue(
    const juce::String& parameterId) const
{
    const juce::ScopedLock lock(savedParameterStateLock);
    for (int index = 0; index < savedParameterState.getNumChildren(); ++index)
    {
        const auto child = savedParameterState.getChild(index);
        if (child.getProperty("id").toString() != parameterId)
            continue;
        const float value = child.getProperty("value").toString()
            .getFloatValue();
        return std::isfinite(value) ? std::optional<float>(value)
                                    : std::nullopt;
    }
    return std::nullopt;
}

juce::AudioProcessorValueTreeState::ParameterLayout
VortexAudioProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using API = juce::AudioParameterInt;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> layout;

    const auto percentageAttributes = juce::AudioParameterFloatAttributes()
        .withLabel("%");
    const auto targetDepthAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int)
        {
            return juce::String(juce::roundToInt(value * 100.0f)) + "%";
        })
        .withValueFromStringFunction([](const juce::String& text)
        {
            return text.getFloatValue() * 0.01f;
        });
    auto outputRange = juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f);
    outputRange.setSkewForCentre(0.0f);

    // Four independently enabled engines share a normalised parallel wet bus.
    const auto rateRange = juce::NormalisableRange<float>(0.05f, 8.0f, 0.01f, 0.4f);
    const auto percentRange = juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f);
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::chorusRate, 1), "Chorus Rate",
        juce::NormalisableRange<float>(0.01f, 10.0f, 0.01f, 0.35f),
        0.65f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::chorusDepth, 1), "Chorus Depth",
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.01f), 8.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<API>(
        juce::ParameterID(ParameterIds::chorusVoices, 1), "Chorus Voices", 1, 8, 4));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID(ParameterIds::chorusLfoShape, 1), "Chorus LFO Shape",
        juce::StringArray { "Sine", "Tri", "Saw", "Ramp",
                            "Exp", "Inv Exp", "Random", "Rnd Sq" }, 0));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID(ParameterIds::phaserLfoShape, 1), "Phaser Wave",
        juce::StringArray { "Sine", "Tri", "Saw", "Ramp",
                            "Exp", "Inv Exp", "Random", "Rnd Sq" }, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::phaserRate, 1), "Phaser Rate",
        rateRange, 0.45f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::phaserDepth, 1), "Phaser Depth",
        percentRange, 65.0f, percentageAttributes));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID(ParameterIds::phaserStages, 1), "Phaser Stages",
        juce::StringArray { "2", "4", "6", "8", "10", "12" }, 2));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::flangerRate, 1), "Flanger Rate",
        rateRange, 0.28f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::flangerDepth, 1), "Flanger Depth",
        percentRange, 65.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::flangerFeedback, 1), "Flanger Feedback",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f), 25.0f,
        percentageAttributes));
    // Extra Flanger sound-design parameters. Defaults preserve the previous
    // sine sweep, 90-degree stereo offset, base delay and unfiltered sound.
    const auto flangerPhaseText = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float degrees, int)
        {
            return juce::String(juce::roundToInt(degrees)) + "deg";
        })
        .withValueFromStringFunction([](const juce::String& value)
        {
            return value.getFloatValue();
        });
    const auto flangerOffsetText = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float milliseconds, int)
        {
            return juce::String(milliseconds, 1) + "ms";
        })
        .withValueFromStringFunction([](const juce::String& value)
        {
            return value.getFloatValue();
        });
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::flangerPhase, 1), "Flanger Phase",
        juce::NormalisableRange<float>(0.0f, 180.0f, 1.0f), 90.0f,
        flangerPhaseText));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::flangerOffset, 1), "Flanger Offset",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f), 0.0f,
        flangerOffsetText));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID(ParameterIds::flangerShape, 1), "Flanger Wave",
        juce::StringArray { "Sine", "Tri", "Saw", "Ramp",
                            "Exp", "Inv Exp", "Random", "Rnd Sq" }, 0));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::flangerTone, 1), "Flanger Tone",
        percentRange, 100.0f, percentageAttributes));
    // Independent, stereo-spread, humanised 1-to-8-voice doubling.
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::doublerTightness, 1), "Doubler Tightness",
        percentRange, 72.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::doublerDetune, 1), "Doubler Detune",
        percentRange, 45.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::doublerSpread, 1), "Doubler Spread",
        percentRange, 80.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::mix, 1), "Mix",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 35.0f,
        percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::output, 1), "Output", outputRange,
        0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
    // Phaser's additional controls stay available to host automation and sequencer.
    // Compact readouts keep the exact same font as every other effect knob.
    const auto phaserFrequencyText = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float hz, int)
        {
            return hz >= 1000.0f ? juce::String(hz / 1000.0f, 1) + "k"
                                 : juce::String(juce::roundToInt(hz)) + "Hz";
        })
        .withValueFromStringFunction([](const juce::String& text)
        {
            const auto trimmed = text.trim().toLowerCase();
            return trimmed.containsChar('k') ? trimmed.getFloatValue() * 1000.0f
                                             : trimmed.getFloatValue();
        });
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::phaserCenter, 1), "Phaser Centre",
        juce::NormalisableRange<float>(75.0f, 7500.0f, 1.0f, 0.35f),
        680.0f, phaserFrequencyText));
    const auto feedbackText = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int)
        {
            return juce::String(value, 1) + "%";
        })
        .withValueFromStringFunction([](const juce::String& text)
        {
            return text.getFloatValue();
        });
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::phaserFeedback, 1), "Phaser Feedback",
        juce::NormalisableRange<float>(-90.0f, 90.0f, 0.1f), 20.0f,
        feedbackText));
    const auto stereoText = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction([](float value, int)
        {
            return juce::String(juce::roundToInt(value)) + "deg";
        })
        .withValueFromStringFunction([](const juce::String& text)
        {
            return text.getFloatValue();
        });
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::phaserStereo, 1), "Phaser Stereo Phase",
        juce::NormalisableRange<float>(0.0f, 180.0f, 1.0f), 70.0f,
        stereoText));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID(ParameterIds::bypass, 1), "Bypass", false));

    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerEnabled", 1), "Sequencer Enabled", true));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerHostSync", 1), "Sequencer Host Sync", true));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("sequencerRate", 1), "Sequencer Rate",
        juce::StringArray { "1/128", "1/64T", "1/64", "1/32T", "1/32",
            "1/16T", "1/16", "1/8T", "1/8", "1/4T", "1/4", "1/2T",
            "1/2", "1/1" }, 6));
    layout.push_back(std::make_unique<API>(
        juce::ParameterID("sequencerStart", 1), "Sequencer Start", 1,
        vortex::sequencer::stepCount - 1, 1));
    layout.push_back(std::make_unique<API>(
        juce::ParameterID("sequencerEnd", 1), "Sequencer End", 2,
        vortex::sequencer::stepCount, vortex::sequencer::stepCount));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerRangeLink", 1),
        "Sequencer Range Link", false));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("sequencerDirection", 1), "Sequencer Direction",
        juce::StringArray { "Loop", "Bounce", "Reverse", "Played", "Random" },
        0));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerBipolar", 1), "Sequencer Bipolar", false));
    layout.push_back(std::make_unique<APB>(
        juce::ParameterID("sequencerUnipolarNegative", 1),
        "Sequencer Unipolar Negative", false));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerAttack", 1), "Sequencer Attack",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        targetDepthAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerRelease", 1), "Sequencer Release",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        targetDepthAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerPeak", 1), "Sequencer Depth",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerMove", 1), "Sequencer Offset",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.0f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerHue", 1), "Sequencer Hue",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerSaturation", 1), "Sequencer Saturation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerLuminance", 1), "Sequencer Luminance",
        juce::NormalisableRange<float>(0.2f, 1.0f, 0.001f), 0.5f));

    for (int step = 0; step < vortex::sequencer::stepCount; ++step)
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(vortex::sequencer::stepParameterId(step), 1),
            "Sequencer Step " + juce::String(step + 1),
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.0001f), 1.0f));

    juce::StringArray targetNames { "None" };
    for (const auto& target : vortex::sequencer::targets)
        targetNames.add(target.displayName);
    for (int slot = 0; slot < vortex::sequencer::targetSlotCount; ++slot)
    {
        layout.push_back(std::make_unique<APC>(
            juce::ParameterID(vortex::sequencer::targetParameterId(slot), 1),
            "Sequencer Target " + juce::String(slot + 1), targetNames, 0));
        layout.push_back(std::make_unique<APB>(
            juce::ParameterID(
                vortex::sequencer::targetEnabledParameterId(slot), 1),
            "Sequencer Target " + juce::String(slot + 1) + " Enabled", true));
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(
                vortex::sequencer::targetDepthParameterId(slot), 1),
            "Sequencer Target " + juce::String(slot + 1) + " Depth",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f,
            targetDepthAttributes));
    }

    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerGateShortLength", 1),
        "Sequencer Gate Short Length",
        juce::NormalisableRange<float>(0.10f, 0.60f, 0.01f), 0.50f,
        targetDepthAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID("sequencerGateLongLength", 1),
        "Sequencer Gate Long Length",
        juce::NormalisableRange<float>(0.65f, 0.95f, 0.01f), 0.90f,
        targetDepthAttributes));
    for (int step = 0; step < vortex::sequencer::stepCount; ++step)
        layout.push_back(std::make_unique<APC>(
            juce::ParameterID(vortex::sequencer::gateStepParameterId(step), 1),
            "Sequencer Gate Step " + juce::String(step + 1),
            juce::StringArray { "Off", "Short", "Long", "Rest" },
            static_cast<int>(vortex::sequencer::GateStepMode::rest)));
    layout.push_back(std::make_unique<APC>(
        juce::ParameterID("sequencerGateMode", 1), "Sequencer Gate Mode",
        juce::StringArray { "Input", "Effect", "Output", "Off" }, 0));

    // Independent wet-only output levels for the four engines.
    // 100% is unity, 0% is mute, and 200% is +6 dB of wet trim.
    for (size_t i = 0; i < ParameterIds::effectLevelIds.size(); ++i)
        layout.push_back(std::make_unique<APF>(
            juce::ParameterID(ParameterIds::effectLevelIds[i], 1),
            ParameterIds::effectLevelNames[i],
            juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 100.0f,
            percentageAttributes));

    // Pre-release parameters: keep the existing order for straightforward review.
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::chorusFeedback, 1), "Chorus Feedback",
        percentRange, 0.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::chorusWidth, 1), "Chorus Width",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.1f), 135.0f,
        percentageAttributes));
    // Pre-delay is the minimum nominal delay before the Chorus LFO sweep.
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::chorusPreDelay, 1), "Chorus Pre-Delay",
        juce::NormalisableRange<float>(0.0f, 50.0f, 0.1f), 18.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::doublerPitchDepth, 1), "Doubler Pitch Depth",
        juce::NormalisableRange<float>(0.0f, 250.0f, 1.0f), 45.0f,
        percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::doublerPitchRate, 1), "Doubler Pitch Rate",
        percentRange, 40.0f, percentageAttributes));
    layout.push_back(std::make_unique<APF>(
        juce::ParameterID(ParameterIds::doublerTiming, 1), "Doubler Timing",
        juce::NormalisableRange<float>(0.0f, 800.0f, 1.0f, 0.35f), 63.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));
    layout.push_back(std::make_unique<API>(
        juce::ParameterID(ParameterIds::doublerVoices, 1), "Doubler Voices", 1, 8, 4));
    const std::array<std::pair<const char*, const char*>, 4> enginePower {
        {{ ParameterIds::chorusEnabled, "Chorus On" },
         { ParameterIds::phaserEnabled, "Phaser On" },
         { ParameterIds::flangerEnabled, "Flanger On" },
         { ParameterIds::doublerEnabled, "Doubler On" }} };
    for (const auto& entry : enginePower)
        layout.push_back(std::make_unique<APB>(
            juce::ParameterID(entry.first, 1), entry.second, true));

    return { layout.begin(), layout.end() };
}

void VortexAudioProcessor::bindParameterPointers()
{
    const auto bind = [this](const juce::String& id)
    {
        auto* value = parameters.getRawParameterValue(id);
        jassert(value != nullptr);
        return value;
    };
    raw.chorusRate = bind(ParameterIds::chorusRate);
    raw.chorusDepth = bind(ParameterIds::chorusDepth);
    raw.chorusVoices = bind(ParameterIds::chorusVoices);
    raw.chorusLfoShape = bind(ParameterIds::chorusLfoShape);
    raw.chorusFeedback = bind(ParameterIds::chorusFeedback);
    raw.chorusPreDelay = bind(ParameterIds::chorusPreDelay);
    raw.chorusWidth = bind(ParameterIds::chorusWidth);
    raw.chorusEnabled = bind(ParameterIds::chorusEnabled);
    raw.phaserEnabled = bind(ParameterIds::phaserEnabled);
    raw.flangerEnabled = bind(ParameterIds::flangerEnabled);
    raw.doublerEnabled = bind(ParameterIds::doublerEnabled);
    for (size_t i = 0; i < raw.effectLevels.size(); ++i)
        raw.effectLevels[i] = bind(ParameterIds::effectLevelIds[i]);
    raw.doublerVoices = bind(ParameterIds::doublerVoices);
    raw.doublerPitchDepth = bind(ParameterIds::doublerPitchDepth);
    raw.doublerPitchRate = bind(ParameterIds::doublerPitchRate);
    raw.doublerTiming = bind(ParameterIds::doublerTiming);
    raw.phaserRate = bind(ParameterIds::phaserRate);
    raw.phaserDepth = bind(ParameterIds::phaserDepth);
    raw.phaserStages = bind(ParameterIds::phaserStages);
    raw.phaserCenter = bind(ParameterIds::phaserCenter);
    raw.phaserFeedback = bind(ParameterIds::phaserFeedback);
    raw.phaserStereo = bind(ParameterIds::phaserStereo);
    raw.phaserLfoShape = bind(ParameterIds::phaserLfoShape);
    raw.flangerRate = bind(ParameterIds::flangerRate);
    raw.flangerDepth = bind(ParameterIds::flangerDepth);
    raw.flangerFeedback = bind(ParameterIds::flangerFeedback);
    raw.flangerPhase = bind(ParameterIds::flangerPhase);
    raw.flangerOffset = bind(ParameterIds::flangerOffset);
    raw.flangerShape = bind(ParameterIds::flangerShape);
    raw.flangerTone = bind(ParameterIds::flangerTone);
    raw.doublerTightness = bind(ParameterIds::doublerTightness);
    raw.doublerDetune = bind(ParameterIds::doublerDetune);
    raw.doublerSpread = bind(ParameterIds::doublerSpread);
    raw.mix = bind(ParameterIds::mix);
    raw.output = bind(ParameterIds::output);
    raw.bypass = bind(ParameterIds::bypass);
    raw.sequencerEnabled = bind("sequencerEnabled");
    raw.sequencerHostSync = bind("sequencerHostSync");
    raw.sequencerRate = bind("sequencerRate");
    raw.sequencerStart = bind("sequencerStart");
    raw.sequencerEnd = bind("sequencerEnd");
    raw.sequencerDirection = bind("sequencerDirection");
    raw.sequencerBipolar = bind("sequencerBipolar");
    raw.sequencerUnipolarNegative = bind("sequencerUnipolarNegative");
    raw.sequencerAttack = bind("sequencerAttack");
    raw.sequencerRelease = bind("sequencerRelease");
    raw.sequencerPeak = bind("sequencerPeak");
    raw.sequencerMove = bind("sequencerMove");
    raw.sequencerGateShortLength = bind("sequencerGateShortLength");
    raw.sequencerGateLongLength = bind("sequencerGateLongLength");
    raw.sequencerGateMode = bind("sequencerGateMode");
    for (int step = 0; step < vortex::sequencer::stepCount; ++step)
    {
        const auto index = static_cast<size_t>(step);
        raw.sequencerSteps[index] = bind(
            vortex::sequencer::stepParameterId(step));
        raw.sequencerGateSteps[index] = bind(
            vortex::sequencer::gateStepParameterId(step));
    }
    for (int slot = 0; slot < vortex::sequencer::targetSlotCount; ++slot)
    {
        const auto index = static_cast<size_t>(slot);
        raw.sequencerTargets[index] = bind(
            vortex::sequencer::targetParameterId(slot));
        raw.sequencerTargetEnabled[index] = bind(
            vortex::sequencer::targetEnabledParameterId(slot));
        raw.sequencerTargetDepth[index] = bind(
            vortex::sequencer::targetDepthParameterId(slot));
    }
    for (size_t target = 0; target < vortex::sequencer::targets.size(); ++target)
        sequencerTargetParameters[target] = parameters.getParameter(
            vortex::sequencer::targets[target].parameterId);
}

float VortexAudioProcessor::load(const std::atomic<float>* value) noexcept
{
    const float loaded = value != nullptr
        ? value->load(std::memory_order_relaxed) : 0.0f;
    return std::isfinite(loaded) ? loaded : 0.0f;
}

bool VortexAudioProcessor::isSequencerTargetAssigned(int targetChoice) const noexcept
{
    if (targetChoice <= 0)
        return false;
    for (size_t slot = 0; slot < raw.sequencerTargets.size(); ++slot)
        if (load(raw.sequencerTargetEnabled[slot]) >= 0.5f
            && static_cast<int>(std::lround(load(raw.sequencerTargets[slot])))
                   == targetChoice)
            return true;
    return false;
}

float VortexAudioProcessor::sequencerTargetDepth(int targetChoice) const noexcept
{
    if (targetChoice <= 0)
        return 1.0f;
    for (size_t slot = 0; slot < raw.sequencerTargets.size(); ++slot)
        if (load(raw.sequencerTargetEnabled[slot]) >= 0.5f
            && static_cast<int>(std::lround(load(raw.sequencerTargets[slot])))
                   == targetChoice)
            return juce::jlimit(
                0.0f, 1.0f, load(raw.sequencerTargetDepth[slot]));
    return 1.0f;
}

float VortexAudioProcessor::sequencerModulatedValue(
    int targetChoice, float baseValue, float canonicalSequenceValue,
    bool bipolar, bool unipolarNegative,
    juce::RangedAudioParameter* valueParameter) const noexcept
{
    if (! isSequencerTargetAssigned(targetChoice)
        || targetChoice > static_cast<int>(sequencerTargetParameters.size()))
        return baseValue;
    auto* parameter = valueParameter != nullptr
        ? valueParameter
        : sequencerTargetParameters[static_cast<size_t>(targetChoice - 1)];
    if (parameter == nullptr)
        return baseValue;

    canonicalSequenceValue = vortex::sequencer::applyTargetDepth(
        canonicalSequenceValue, bipolar, sequencerTargetDepth(targetChoice));
    const float baseNormalised = juce::jlimit(
        0.0f, 1.0f, parameter->convertTo0to1(baseValue));
    float modulatedNormalised = baseNormalised;
    if (bipolar)
    {
        const float deviation = 2.0f * canonicalSequenceValue - 1.0f;
        const float radius = juce::jmin(
            baseNormalised, 1.0f - baseNormalised);
        modulatedNormalised = baseNormalised + deviation * radius;
    }
    else
    {
        const float amount = juce::jlimit(
            0.0f, 1.0f, 2.0f * canonicalSequenceValue - 1.0f);
        modulatedNormalised = unipolarNegative
            ? baseNormalised - amount * baseNormalised
            : baseNormalised + amount * (1.0f - baseNormalised);
    }
    modulatedNormalised = juce::jlimit(
        0.0f, 1.0f, modulatedNormalised);
    sequencerAnimatedNormalised[static_cast<size_t>(targetChoice - 1)].store(
        modulatedNormalised, std::memory_order_relaxed);
    return parameter->convertFrom0to1(modulatedNormalised);
}

std::optional<float> VortexAudioProcessor::getSequencerAnimatedNormalised(
    const juce::String& parameterId) const noexcept
{
    const int choice = vortex::sequencer::targetChoiceForParameter(parameterId);
    if (choice <= 0
        || choice > static_cast<int>(sequencerAnimatedNormalised.size())
        || load(raw.sequencerEnabled) < 0.5f
        || load(raw.bypass) >= 0.5f
        || ! isSequencerTargetAssigned(choice))
        return std::nullopt;
    const float value = sequencerAnimatedNormalised[
        static_cast<size_t>(choice - 1)].load(std::memory_order_relaxed);
    if (! std::isfinite(value) || value < 0.0f)
        return std::nullopt;
    return juce::jlimit(0.0f, 1.0f, value);
}

bool VortexAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return input == output
        && (output == juce::AudioChannelSet::mono()
            || output == juce::AudioChannelSet::stereo());
}

void VortexAudioProcessor::resetEffectState() noexcept
{
    for (auto& line : chorusDelay)
        std::fill(line.begin(), line.end(), 0.0f);
    for (auto& line : flangerDelay)
        std::fill(line.begin(), line.end(), 0.0f);
    for (auto& line : doublerDelay)
        std::fill(line.begin(), line.end(), 0.0f);
    for (auto& state : phaserState) state.fill(0.0f);
    phaserProcessingLastSample = false;
    flangerProcessingLastSample = false;
    phaserFeedbackSample.fill(0.0f);
    phaserDisplayHz[0].store(0.0f, std::memory_order_relaxed);
    phaserDisplayHz[1].store(0.0f, std::memory_order_relaxed);
    phaserDisplayMinimumHz.store(0.0f, std::memory_order_relaxed);
    phaserDisplayMaximumHz.store(0.0f, std::memory_order_relaxed);
    for (auto& value : chorusDisplayMs) value.store(0.0f, std::memory_order_relaxed);
    for (auto& value : flangerDisplayMs) value.store(0.0f, std::memory_order_relaxed);
    for (auto& value : doublerDisplayMs) value.store(0.0f, std::memory_order_relaxed);
    chorusDisplayMinimumMs.store(0.0f, std::memory_order_relaxed);
    chorusDisplayMaximumMs.store(0.0f, std::memory_order_relaxed);
    flangerDisplayMinimumMs.store(0.0f, std::memory_order_relaxed);
    flangerDisplayMaximumMs.store(0.0f, std::memory_order_relaxed);
    doublerDisplayMinimumMs.store(0.0f, std::memory_order_relaxed);
    doublerDisplayMaximumMs.store(0.0f, std::memory_order_relaxed);
    flangerFeedbackSample.fill(0.0f);
    flangerToneState.fill(0.0f);
    flangerToneSecondState.fill(0.0f);
    chorusFeedbackSample.fill(0.0f);
    chorusWriteIndex = flangerWriteIndex = doublerWriteIndex = 0;
    chorusVoicePhases = { 0.00, 0.25, 0.50, 0.75,
                          0.125, 0.375, 0.625, 0.875 };
    for (size_t voice = 0; voice < chorusVoicePhases.size(); ++voice)
    {
        auto& seed = chorusRandomSeeds[voice];
        seed = 0x9e3779b9u ^ (0x85ebca6bu * (static_cast<std::uint32_t>(voice) + 1u));
        chorusRandomCurrent[voice] = nextChorusRandom(seed);
        chorusRandomNext[voice] = nextChorusRandom(seed);
        chorusRandomFuture[voice] = nextChorusRandom(seed);
    }
    for (auto& channel : chorusWaveSmoothed)
        channel.fill(0.0f);
    flangerRandomSeed = 0xa341316cu;
    flangerRandomCurrent = nextChorusRandom(flangerRandomSeed);
    flangerRandomNext = nextChorusRandom(flangerRandomSeed);
    flangerRandomFuture = nextChorusRandom(flangerRandomSeed);
    flangerWaveSmoothed.fill(0.0f);
    phaserRandomSeed = 0xc2b2ae35u;
    phaserRandomCurrent = nextChorusRandom(phaserRandomSeed);
    phaserRandomNext = nextChorusRandom(phaserRandomSeed);
    phaserRandomFuture = nextChorusRandom(phaserRandomSeed);
    phaserWaveSmoothed.fill(0.0f);
    phaserPhase = flangerPhase = 0.0;
    doublerVoicePitchPhases = { 0.08, 0.61, 0.35, 0.87,
                                0.23, 0.74, 0.47, 0.95 };
    doublerVoiceDriftPhases = { 0.19, 0.78, 0.44, 0.04,
                                0.90, 0.30, 0.57, 0.65 };
    doublerVoiceDetunePhases = { 0.04, 0.57, 0.30, 0.81,
                                 0.16, 0.70, 0.43, 0.93 };
    doublerBaseFromMs = doublerBaseToMs = 10.0f
        + 23.0f * (1.0f - load(raw.doublerTightness) * 0.01f)
        + load(raw.doublerTiming);
    doublerBaseFade = 1.0f;
}

float VortexAudioProcessor::readDelay(const std::vector<float>& line,
                                   int writeIndex, float delaySamples) const noexcept
{
    if (line.empty()) return 0.0f;
    const float size = static_cast<float>(line.size());
    float index = static_cast<float>(writeIndex) - delaySamples;
    if (index < 0.0f) index += size;
    const int before = juce::jlimit(0, static_cast<int>(line.size()) - 1,
                                    static_cast<int>(index));
    const int after = before + 1 == static_cast<int>(line.size())
        ? 0 : before + 1;
    const float fraction = index - static_cast<float>(before);
    return line[static_cast<size_t>(before)]
        + fraction * (line[static_cast<size_t>(after)]
                    - line[static_cast<size_t>(before)]);
}

void VortexAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);
    currentSampleRate = juce::jmax(1.0, sampleRate);
    chorusWaveSmoothingAlpha = 1.0f - std::exp(
        -1.0f / static_cast<float>(0.008 * currentSampleRate));
    // The phaser has six frequency-spread all-pass pairs per channel. Cache
    // their frequency-to-coefficient curve at prepare time, not per sample.
    const float maxPhaserFrequency = juce::jmax(25.0f,
        juce::jmin(12000.0f, static_cast<float>(currentSampleRate * 0.40)));
    phaserCoefficientTableScale = maxPhaserFrequency > 25.0f
        ? static_cast<float>(phaserCoefficientTableSize)
            / (maxPhaserFrequency - 25.0f) : 0.0f;
    for (int i = 0; i <= phaserCoefficientTableSize; ++i)
    {
        const float frequency = 25.0f + (maxPhaserFrequency - 25.0f)
            * static_cast<float>(i) / static_cast<float>(phaserCoefficientTableSize);
        const float tangent = std::tan(juce::MathConstants<float>::pi
            * frequency / static_cast<float>(currentSampleRate));
        phaserCoefficientTable[static_cast<size_t>(i)] =
            (tangent - 1.0f) / (tangent + 1.0f);
    }
    const float roundedSquareNormaliser = 1.0f / std::tanh(3.0f);
    for (int i = 0; i < modulationSineTableSize; ++i)
    {
        const auto wave = static_cast<float>(std::sin(
            juce::MathConstants<double>::twoPi * static_cast<double>(i)
                / static_cast<double>(modulationSineTableSize)));
        modulationSineTable[static_cast<size_t>(i)] = wave;
        modulationRoundedSquareTable[static_cast<size_t>(i)] =
            std::tanh(3.0f * wave) * roundedSquareNormaliser;
    }
    modulationSineTable[modulationSineTableSize] = 0.0f;
    modulationRoundedSquareTable[modulationSineTableSize] = 0.0f;
    for (int pair = 0; pair < maxPhaserStages / 2; ++pair)
        phaserPoleRatios[static_cast<size_t>(pair)] = std::exp2(
            0.21f * (static_cast<float>(pair) - 2.5f));
    for (auto& line : chorusDelay)
        line.assign(static_cast<size_t>(juce::jmax(4096,
            juce::roundToInt(currentSampleRate * 0.120))), 0.0f);
    for (auto& line : flangerDelay)
        line.assign(static_cast<size_t>(juce::jmax(512,
            juce::roundToInt(currentSampleRate * 0.025))), 0.0f);
    // 800 ms Timing plus up to ~240 ms pitch-sweep span, static offsets,
    // and safety headroom; allocated only during prepareToPlay.
    for (auto& line : doublerDelay)
        line.assign(static_cast<size_t>(juce::jmax(4096,
            juce::roundToInt(currentSampleRate * 1.350))), 0.0f);
    resetEffectState();
    const auto resetSmoother = [this](LinearSmoother& smoother, float value)
    {
        smoother.reset(currentSampleRate, smoothingSeconds);
        smoother.setCurrentAndTargetValue(value);
    };
    resetSmoother(mixValue, juce::jlimit(0.0f, 1.0f, load(raw.mix) * 0.01f));
    resetSmoother(outputGainValue,
                  juce::Decibels::decibelsToGain(load(raw.output)));
    resetSmoother(bypassValue, load(raw.bypass) >= 0.5f ? 1.0f : 0.0f);
    for (size_t i = 0; i < effectLevelValues.size(); ++i)
        resetSmoother(effectLevelValues[i],
            juce::jlimit(0.0f, 2.0f, load(raw.effectLevels[i]) * 0.01f));
    resetSmoother(chorusRateValue, load(raw.chorusRate));
    resetSmoother(chorusLfoShapeValue, load(raw.chorusLfoShape));
    resetSmoother(chorusDepthValue, load(raw.chorusDepth));
    // UI is 0..100%; the DSP coefficient remains below unity at 100%.
    resetSmoother(chorusFeedbackValue,
                  chorusFeedbackCoefficient(load(raw.chorusFeedback)));
    resetSmoother(chorusPreDelayValue, load(raw.chorusPreDelay));
    resetSmoother(chorusWidthValue, load(raw.chorusWidth) * 0.01f);
    resetSmoother(phaserRateValue, load(raw.phaserRate));
    resetSmoother(phaserDepthValue, load(raw.phaserDepth) * 0.01f);
    resetSmoother(phaserStagesValue, load(raw.phaserStages));
    resetSmoother(phaserCenterValue, load(raw.phaserCenter));
    resetSmoother(phaserFeedbackValue, load(raw.phaserFeedback) * 0.01f);
    resetSmoother(phaserStereoValue, load(raw.phaserStereo));
    resetSmoother(phaserLfoShapeValue, load(raw.phaserLfoShape));
    resetSmoother(flangerRateValue, load(raw.flangerRate));
    resetSmoother(flangerDepthValue, load(raw.flangerDepth) * 0.01f);
    resetSmoother(flangerFeedbackValue,
                  flangerFeedbackCoefficient(load(raw.flangerFeedback)));
    resetSmoother(flangerPhaseValue, load(raw.flangerPhase));
    resetSmoother(flangerOffsetValue, load(raw.flangerOffset));
    resetSmoother(flangerShapeValue, load(raw.flangerShape));
    resetSmoother(flangerToneValue, load(raw.flangerTone) * 0.01f);
    resetSmoother(doublerTightnessValue, load(raw.doublerTightness) * 0.01f);
    resetSmoother(doublerDetuneValue, load(raw.doublerDetune) * 0.01f);
    resetSmoother(doublerSpreadValue, load(raw.doublerSpread) * 0.01f);
    resetSmoother(doublerPitchDepthValue, load(raw.doublerPitchDepth));
    resetSmoother(doublerPitchRateValue, load(raw.doublerPitchRate));
    resetSmoother(doublerTimingValue, load(raw.doublerTiming));
    const int initialDoublerVoices = juce::jlimit(
        1, 8, juce::roundToInt(load(raw.doublerVoices)));
    for (int v = 0; v < 8; ++v)
        resetSmoother(doublerVoiceGain[static_cast<size_t>(v)],
            v < initialDoublerVoices ? 1.0f / std::pow(
                static_cast<float>(initialDoublerVoices), 0.70f) : 0.0f);
    const int initialVoices = juce::jlimit(1, 8, juce::roundToInt(load(raw.chorusVoices)));
    for (int v = 0; v < 8; ++v)
        resetSmoother(chorusVoiceGain[static_cast<size_t>(v)],
            v < initialVoices ? 1.0f / std::pow(static_cast<float>(initialVoices), 0.65f)
                              : 0.0f);
    const std::array<const std::atomic<float>*, 4> enabledValues {
        raw.chorusEnabled, raw.phaserEnabled, raw.flangerEnabled, raw.doublerEnabled };
    for (size_t e = 0; e < effectEnabledValues.size(); ++e)
        resetSmoother(effectEnabledValues[e],
            load(enabledValues[e]) >= 0.5f ? 1.0f : 0.0f);
    sequencerFreePhase = 0.0;
    sequencerHeldNotes = 0;
    previousSequencerCanonical = 1.0f;
    sequencerGateGain = 1.0f;
}

void VortexAudioProcessor::releaseResources()
{
    activeSequencerStep.store(-1, std::memory_order_relaxed);
}

float VortexAudioProcessor::sanitiseSample(float sample, float limit) noexcept
{
    return std::isfinite(sample) ? juce::jlimit(-limit, limit, sample) : 0.0f;
}

void VortexAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int sampleCount = buffer.getNumSamples();
    const int hostChannels = buffer.getNumChannels();
    const int activeChannels = juce::jmin(channelCount, hostChannels);
    for (int channel = activeChannels; channel < hostChannels; ++channel)
        buffer.clear(channel, 0, sampleCount);
    if (sampleCount <= 0 || activeChannels <= 0)
        return;
    if (pendingProcessingReset.exchange(false, std::memory_order_acq_rel))
        resetEffectState();

    const bool sequenceEnabled = load(raw.sequencerEnabled) >= 0.5f;
    const bool hostSync = load(raw.sequencerHostSync) >= 0.5f;
    const auto direction = static_cast<vortex::sequencer::Direction>(
        juce::jlimit(0, 4, static_cast<int>(std::lround(
            load(raw.sequencerDirection)))));

    juce::Optional<juce::AudioPlayHead::PositionInfo> hostPosition;
    if (sequenceEnabled && hostSync
        && direction != vortex::sequencer::Direction::played)
        if (auto* playhead = getPlayHead())
            hostPosition = playhead->getPosition();
    double bpm = 120.0;
    if (hostPosition.hasValue())
        if (const auto hostBpm = hostPosition->getBpm())
            if (std::isfinite(*hostBpm) && *hostBpm > 0.0)
                bpm = juce::jlimit(10.0, 1000.0, *hostBpm);

    bool phraseTriggered = false;
    if (sequenceEnabled && direction == vortex::sequencer::Direction::played)
    {
        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();
            if (message.isNoteOn())
            {
                if (sequencerHeldNotes == 0)
                    phraseTriggered = true;
                ++sequencerHeldNotes;
            }
            else if (message.isNoteOff())
                sequencerHeldNotes = juce::jmax(0, sequencerHeldNotes - 1);
            else if (message.isAllNotesOff() || message.isAllSoundOff())
                sequencerHeldNotes = 0;
        }
    }
    else
        sequencerHeldNotes = 0;

    std::array<float, vortex::sequencer::stepCount> steps {};
    float sequenceMinimum = 1.0f;
    float sequenceMaximum = 0.0f;
    for (int step = 0; step < vortex::sequencer::stepCount; ++step)
    {
        const auto index = static_cast<size_t>(step);
        steps[index] = juce::jlimit(
            0.0f, 1.0f, load(raw.sequencerSteps[index]));
        sequenceMinimum = juce::jmin(sequenceMinimum, steps[index]);
        sequenceMaximum = juce::jmax(sequenceMaximum, steps[index]);
    }
    const bool bipolar = load(raw.sequencerBipolar) >= 0.5f;
    const bool unipolarNegative = ! bipolar
        && load(raw.sequencerUnipolarNegative) >= 0.5f;
    const auto modulatedPrevious = [this, sequenceEnabled, bipolar,
                                    unipolarNegative](
        int target, const std::atomic<float>* parameter)
    {
        const float base = load(parameter);
        return sequenceEnabled
            ? sequencerModulatedValue(target, base,
                previousSequencerCanonical, bipolar, unipolarNegative)
            : base;
    };
    const float peak = juce::jlimit(-1.0f, 1.0f, modulatedPrevious(
        vortex::sequencer::sequencerDepthTargetChoice, raw.sequencerPeak));
    const float move = juce::jlimit(-1.0f, 1.0f, modulatedPrevious(
        vortex::sequencer::sequencerOffsetTargetChoice, raw.sequencerMove));
    if (sequenceEnabled
        && (std::abs(peak) > 0.000001f || std::abs(move) > 0.000001f))
        for (auto& value : steps)
            value = vortex::sequencer::applyDepthOffset(
                value, sequenceMinimum, sequenceMaximum,
                peak, move, bipolar);

    const int rate = juce::jlimit(
        0, vortex::sequencer::rateChoiceCount - 1,
        static_cast<int>(std::lround(load(raw.sequencerRate))));
    const double beatsPerStep = vortex::sequencer::beatsForRate(rate);
    const double phaseIncrement = bpm / (
        60.0 * beatsPerStep * currentSampleRate);
    if (phraseTriggered)
        sequencerFreePhase = 0.0;
    double gatePhaseAtBlockStart = sequencerFreePhase;
    double sequencePhase = sequencerFreePhase
        + 0.5 * static_cast<double>(sampleCount) * phaseIncrement;
    if (sequenceEnabled && hostSync
        && direction != vortex::sequencer::Direction::played
        && hostPosition.hasValue())
        if (const auto ppq = hostPosition->getPpqPosition())
            if (std::isfinite(*ppq))
            {
                sequencePhase = *ppq / beatsPerStep;
                gatePhaseAtBlockStart = sequencePhase;
            }
    if (sequenceEnabled)
        sequencerFreePhase += static_cast<double>(sampleCount) * phaseIncrement;
    if (sequencerFreePhase > 1048576.0)
        sequencerFreePhase = std::fmod(
            sequencerFreePhase,
            static_cast<double>(vortex::sequencer::stepCount * 8));

    const int firstStep = juce::jlimit(
        1, vortex::sequencer::stepCount - 1,
        static_cast<int>(std::lround(modulatedPrevious(
            vortex::sequencer::sequencerStartTargetChoice,
            raw.sequencerStart)))) - 1;
    const int lastStep = juce::jlimit(
        firstStep + 2, vortex::sequencer::stepCount,
        static_cast<int>(std::lround(modulatedPrevious(
            vortex::sequencer::sequencerEndTargetChoice,
            raw.sequencerEnd)))) - 1;
    const float attack = juce::jlimit(0.0f, 1.0f, modulatedPrevious(
        vortex::sequencer::sequencerAttackTargetChoice, raw.sequencerAttack));
    const float release = juce::jlimit(0.0f, 1.0f, modulatedPrevious(
        vortex::sequencer::sequencerReleaseTargetChoice,
        raw.sequencerRelease));
    int activeStep = -1;
    float canonical = 1.0f;
    if (sequenceEnabled)
        canonical = vortex::sequencer::evaluate(
            steps, sequencePhase, firstStep, lastStep, direction,
            attack, release, &activeStep);
    previousSequencerCanonical = canonical;
    activeSequencerStep.store(activeStep, std::memory_order_relaxed);

    const auto sequenced = [this, sequenceEnabled, canonical, bipolar,
                            unipolarNegative](
        int target, const std::atomic<float>* parameter)
    {
        const float base = load(parameter);
        return sequenceEnabled
            ? sequencerModulatedValue(target, base, canonical,
                bipolar, unipolarNegative)
            : base;
    };
    chorusRateValue.setTargetValue(juce::jlimit(0.01f, 10.0f,
        sequenced(vortex::sequencer::chorusRateTargetChoice, raw.chorusRate)));
    chorusLfoShapeValue.setTargetValue(juce::jlimit(0.0f, 7.0f,
        sequenced(vortex::sequencer::chorusLfoShapeTargetChoice, raw.chorusLfoShape)));
    chorusDepthValue.setTargetValue(juce::jlimit(0.0f, 24.0f,
        sequenced(vortex::sequencer::chorusDepthTargetChoice, raw.chorusDepth)));
    const int chorusVoices = juce::jlimit(1, 8, juce::roundToInt(
        sequenced(vortex::sequencer::chorusVoicesTargetChoice, raw.chorusVoices)));
    chorusFeedbackValue.setTargetValue(chorusFeedbackCoefficient(
        sequenced(vortex::sequencer::chorusFeedbackTargetChoice,
                  raw.chorusFeedback)));
    chorusPreDelayValue.setTargetValue(juce::jlimit(0.0f, 50.0f,
        sequenced(vortex::sequencer::chorusPreDelayTargetChoice,
            raw.chorusPreDelay)));
    chorusWidthValue.setTargetValue(juce::jlimit(0.0f, 2.0f,
        sequenced(vortex::sequencer::chorusWidthTargetChoice,
            raw.chorusWidth) * 0.01f));
    const float chorusVoiceTargetGain = 1.0f /
        std::pow(static_cast<float>(chorusVoices), 0.65f);
    for (int v = 0; v < 8; ++v)
        chorusVoiceGain[static_cast<size_t>(v)].setTargetValue(
            v < chorusVoices ? chorusVoiceTargetGain : 0.0f);
    phaserRateValue.setTargetValue(sequenced(
        vortex::sequencer::phaserRateTargetChoice, raw.phaserRate));
    phaserDepthValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::phaserDepthTargetChoice,
            raw.phaserDepth) * 0.01f));
    phaserStagesValue.setTargetValue(juce::jlimit(0.0f, 5.0f,
        sequenced(vortex::sequencer::phaserStagesTargetChoice, raw.phaserStages)));
    phaserCenterValue.setTargetValue(juce::jlimit(75.0f, 7500.0f,
        sequenced(vortex::sequencer::phaserCenterTargetChoice, raw.phaserCenter)));
    phaserFeedbackValue.setTargetValue(juce::jlimit(-0.90f, 0.90f,
        sequenced(vortex::sequencer::phaserFeedbackTargetChoice,
            raw.phaserFeedback) * 0.01f));
    phaserStereoValue.setTargetValue(juce::jlimit(0.0f, 180.0f,
        sequenced(vortex::sequencer::phaserStereoTargetChoice, raw.phaserStereo)));
    phaserLfoShapeValue.setTargetValue(juce::jlimit(0.0f, 7.0f,
        sequenced(vortex::sequencer::phaserLfoShapeTargetChoice,
            raw.phaserLfoShape)));
    flangerRateValue.setTargetValue(sequenced(
        vortex::sequencer::flangerRateTargetChoice, raw.flangerRate));
    flangerDepthValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::flangerDepthTargetChoice,
            raw.flangerDepth) * 0.01f));
    flangerFeedbackValue.setTargetValue(flangerFeedbackCoefficient(
        sequenced(vortex::sequencer::flangerFeedbackTargetChoice,
            raw.flangerFeedback)));
    flangerPhaseValue.setTargetValue(juce::jlimit(0.0f, 180.0f,
        sequenced(vortex::sequencer::flangerPhaseTargetChoice, raw.flangerPhase)));
    flangerOffsetValue.setTargetValue(juce::jlimit(0.0f, 12.0f,
        sequenced(vortex::sequencer::flangerOffsetTargetChoice, raw.flangerOffset)));
    flangerShapeValue.setTargetValue(juce::jlimit(0.0f, 7.0f,
        sequenced(vortex::sequencer::flangerShapeTargetChoice,
            raw.flangerShape)));
    flangerToneValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::flangerToneTargetChoice,
            raw.flangerTone) * 0.01f));
    doublerTightnessValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::doublerTightnessTargetChoice,
            raw.doublerTightness) * 0.01f));
    doublerDetuneValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::doublerDetuneTargetChoice,
            raw.doublerDetune) * 0.01f));
    doublerSpreadValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::doublerSpreadTargetChoice,
            raw.doublerSpread) * 0.01f));
    doublerPitchDepthValue.setTargetValue(juce::jlimit(0.0f, 250.0f,
        sequenced(vortex::sequencer::doublerPitchDepthTargetChoice,
            raw.doublerPitchDepth)));
    doublerPitchRateValue.setTargetValue(juce::jlimit(0.0f, 100.0f,
        sequenced(vortex::sequencer::doublerPitchRateTargetChoice,
            raw.doublerPitchRate)));
    doublerTimingValue.setTargetValue(juce::jlimit(0.0f, 800.0f,
        sequenced(vortex::sequencer::doublerTimingTargetChoice,
            raw.doublerTiming)));
    const int doublerVoices = juce::jlimit(1, 8, juce::roundToInt(
        sequenced(vortex::sequencer::doublerVoicesTargetChoice,
            raw.doublerVoices)));
    const float doublerVoiceTargetGain = 1.0f /
        std::pow(static_cast<float>(doublerVoices), 0.70f);
    for (int v = 0; v < 8; ++v)
        doublerVoiceGain[static_cast<size_t>(v)].setTargetValue(
            v < doublerVoices ? doublerVoiceTargetGain : 0.0f);
    const std::array<const std::atomic<float>*, 4> enabledValues {
        raw.chorusEnabled, raw.phaserEnabled, raw.flangerEnabled, raw.doublerEnabled };
    const std::array<int, 4> enableTargets {
        vortex::sequencer::chorusEnabledTargetChoice,
        vortex::sequencer::phaserEnabledTargetChoice,
        vortex::sequencer::flangerEnabledTargetChoice,
        vortex::sequencer::doublerEnabledTargetChoice };
    for (size_t e = 0; e < effectEnabledValues.size(); ++e)
        effectEnabledValues[e].setTargetValue(
            sequenced(enableTargets[e], enabledValues[e]) >= 0.5f ? 1.0f : 0.0f);
    constexpr std::array<int, 4> levelTargets {
        vortex::sequencer::chorusLevelTargetChoice,
        vortex::sequencer::phaserLevelTargetChoice,
        vortex::sequencer::flangerLevelTargetChoice,
        vortex::sequencer::doublerLevelTargetChoice };
    for (size_t i = 0; i < effectLevelValues.size(); ++i)
        effectLevelValues[i].setTargetValue(juce::jlimit(0.0f, 2.0f,
            sequenced(levelTargets[i], raw.effectLevels[i]) * 0.01f));
    mixValue.setTargetValue(juce::jlimit(0.0f, 1.0f,
        sequenced(vortex::sequencer::mixTargetChoice, raw.mix) * 0.01f));
    outputGainValue.setTargetValue(juce::Decibels::decibelsToGain(
        sequenced(vortex::sequencer::outputTargetChoice, raw.output)));
    bypassValue.setTargetValue(load(raw.bypass) >= 0.5f ? 1.0f : 0.0f);

    std::array<vortex::sequencer::GateStepMode,
               vortex::sequencer::stepCount> gateSteps {};
    for (int step = 0; step < vortex::sequencer::stepCount; ++step)
        gateSteps[static_cast<size_t>(step)] =
            vortex::sequencer::gateStepModeFromChoice(
                load(raw.sequencerGateSteps[static_cast<size_t>(step)]));
    const float shortLength = juce::jlimit(0.10f, 0.60f, sequenced(
        vortex::sequencer::sequencerGateShortTargetChoice,
        raw.sequencerGateShortLength));
    const float longLength = juce::jlimit(0.65f, 0.95f, sequenced(
        vortex::sequencer::sequencerGateLongTargetChoice,
        raw.sequencerGateLongLength));
    const int gateMode = juce::jlimit(
        0, 3, juce::roundToInt(load(raw.sequencerGateMode)));
    const float gateSmoothing = 1.0f - std::exp(
        -1.0f / static_cast<float>(0.002 * currentSampleRate));
    // Moderate Doubler-only wet boost. Dry input and other engines are unchanged.
    constexpr float doublerWetGain = 1.3335214f; // +2.5 dB

    std::array<float, channelCount> finalPhaserHz { 0.0f, 0.0f };
    float finalPhaserMinimum = 0.0f, finalPhaserMaximum = 0.0f;
    // Phaser guide ranges are read by the GUI only after this block. Retain
    // the last active smoothed values; compute their exponentials just once.
    float lastPhaserCenterForGuide = 0.0f;
    float lastPhaserDepthForGuide = 0.0f;
    bool phaserGuideWasActive = false;
    std::array<float, channelCount> finalChorusMs {}, finalFlangerMs {};
    std::array<float, 2> finalDoublerMs {};
    float finalChorusMinimumMs = 0.0f, finalChorusMaximumMs = 0.0f;
    float finalFlangerMinimumMs = 0.0f, finalFlangerMaximumMs = 0.0f;
    float finalDoublerMinimumMs = 0.0f, finalDoublerMaximumMs = 0.0f;
    auto* left = buffer.getWritePointer(0);
    auto* right = activeChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    // A stable read-head transition is triggered at most once per 60 ms,
    // even during continuous mouse movement or sequencer automation.
    const float targetDoublerBaseMs = 10.0f
        + 23.0f * (1.0f - doublerTightnessValue.getTargetValue())
        + doublerTimingValue.getTargetValue();
    const float baseFadeStep = 1.0f / static_cast<float>(
        juce::jmax(1.0, currentSampleRate * 0.060));
    // Broad, logarithmic tone range. Calculated once per sample for smooth
    // knob/sequence modulation; its two filter stages are continuous.
    constexpr float flangerDarkCutoffHz = 160.0f;
    constexpr float flangerBrightCutoffHz = 16000.0f;
    // Shared by Doubler voice rendering and LFO phase advancement.
    constexpr std::array<float, 8> voicePitchSpeed {
        0.86f, 1.09f, 0.93f, 1.17f,
        0.79f, 1.26f, 1.03f, 1.36f };
    // Reuse calculations for steady parameters. While automating, a changed
    // sample value still gets the original precise calculation immediately.
    float lastTone = -1.0f, cachedToneAlpha = 0.0f;
    float cachedToneBlend = 0.0f;
    float lastPitchRate = -1.0f, lastPitchDepth = -1.0f;
    float cachedPitchHz = 0.0f, cachedPitchSweepMs = 0.0f;
    float lastDoublerSpread = -1.0f;
    std::array<float, 8> doublerLeftPan {}, doublerRightPan {};
    constexpr std::array<float, 8> voicePan {
        -0.94f, 0.94f, -0.44f, 0.44f,
        -0.73f, 0.73f, -0.23f, 0.23f };
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const float dryL = left[sample];
        const float dryR = right != nullptr ? right[sample] : dryL;
        float gateTarget = 1.0f;
        if (sequenceEnabled && gateMode != 3)
        {
            const auto position = vortex::sequencer::positionForPhase(
                gatePhaseAtBlockStart
                    + static_cast<double>(sample) * phaseIncrement,
                lastStep - firstStep + 1, direction);
            gateTarget = vortex::sequencer::gateStepTarget(
                gateSteps[static_cast<size_t>(firstStep + position.current)],
                position.fraction, shortLength, longLength);
        }
        sequencerGateGain += gateSmoothing
            * (gateTarget - sequencerGateGain);
        sequencerGateGain = juce::jlimit(
            0.0f, 1.0f, sequencerGateGain);

        // INPUT mode gates only the original dry path. Keep the input
        // feeding all four effects independent so their tails continue.
        const float dryGate = gateMode == 0 ? sequencerGateGain : 1.0f;
        const float effectInputL = dryL;
        const float effectInputR = dryR;
        // Every continuously variable control moves at sample rate, not once per block.
        const float chorusRate = chorusRateValue.getNextValue();
        const float chorusLfoShape = chorusLfoShapeValue.getNextValue();
        const float chorusDepth = chorusDepthValue.getNextValue();
        const float chorusFeedback = chorusFeedbackValue.getNextValue();
        const float chorusPreDelay = chorusPreDelayValue.getNextValue();
        const float chorusWidth = chorusWidthValue.getNextValue();
        const float phaserRate = phaserRateValue.getNextValue();
        const float phaserDepth = phaserDepthValue.getNextValue();
        const float phaserStageChoice = phaserStagesValue.getNextValue();
        const float phaserCenter = phaserCenterValue.getNextValue();
        const float phaserFeedback = phaserFeedbackValue.getNextValue();
        const float phaserStereo = phaserStereoValue.getNextValue();
        const float phaserLfoShape = phaserLfoShapeValue.getNextValue();
        // Keep the existing enable fades sample-accurate. The expensive DSP
        // stops only once a voice has become fully inaudible; it remains live
        // throughout the fade so switching effects does not click.
        std::array<float, 4> effectGains {};
        for (size_t e = 0; e < effectGains.size(); ++e)
            effectGains[e] = effectEnabledValues[e].getNextValue();
        // An entirely bypassed plugin needs no effect DSP either. Keep each
        // delay ring current so re-engaging the main power switch is seamless.
        const float bypass = bypassValue.getNextValue();
        const bool anyEffectAudible = bypass < 1.0f;
        const bool runChorus  = anyEffectAudible && effectGains[0] > 0.0000001f;
        const bool runPhaser  = anyEffectAudible && effectGains[1] > 0.0000001f;
        const bool runFlanger = anyEffectAudible && effectGains[2] > 0.0000001f;
        const bool runDoubler = anyEffectAudible && effectGains[3] > 0.0000001f;
        // There are no long phaser tails to retain when it is switched off.
        // Clearing the small state only once prevents an old sweep reappearing
        // after re-enabling the effect minutes later.
        if (! runPhaser && phaserProcessingLastSample)
        {
            for (auto& state : phaserState) state.fill(0.0f);
            phaserFeedbackSample.fill(0.0f);
        }
        phaserProcessingLastSample = runPhaser;
        if (! runFlanger && flangerProcessingLastSample)
        {
            flangerFeedbackSample.fill(0.0f);
            flangerToneState.fill(0.0f);
            flangerToneSecondState.fill(0.0f);
        }
        flangerProcessingLastSample = runFlanger;
        if (runPhaser)
        {
            lastPhaserCenterForGuide = phaserCenter;
            lastPhaserDepthForGuide = phaserDepth;
            phaserGuideWasActive = true;
        }
        const float flangerRate = flangerRateValue.getNextValue();
        const float flangerDepth = flangerDepthValue.getNextValue();
        const float flangerFeedback = flangerFeedbackValue.getNextValue();
        const float flangerStereoPhase = flangerPhaseValue.getNextValue();
        const float flangerOffset = flangerOffsetValue.getNextValue();
        const float flangerShape = flangerShapeValue.getNextValue();
        const float flangerTone = flangerToneValue.getNextValue();
        // Calculate once per sample, shared by both channels.
        float flangerToneAlpha = 0.0f;
        float flangerUnfilteredBlend = 0.0f;
        if (runFlanger)
        {
            if (flangerTone != lastTone)
            {
                const float flangerToneCutoff = flangerDarkCutoffHz * std::pow(
                    flangerBrightCutoffHz / flangerDarkCutoffHz, flangerTone);
                cachedToneAlpha = 1.0f - std::exp(
                    -juce::MathConstants<float>::twoPi * flangerToneCutoff
                        / static_cast<float>(currentSampleRate));
                cachedToneBlend = juce::jlimit(0.0f, 1.0f,
                    (flangerTone - 0.88f) / 0.12f);
                lastTone = flangerTone;
            }
            flangerToneAlpha = cachedToneAlpha;
            flangerUnfilteredBlend = cachedToneBlend;
        }
        // Tightness/Timing use a tap crossfade below, not a moving read head.
        const float doublerDetune = doublerDetuneValue.getNextValue();
        const float doublerSpread = doublerSpreadValue.getNextValue();
        const float doublerPitchDepth = doublerPitchDepthValue.getNextValue();
        const float doublerPitchRate = doublerPitchRateValue.getNextValue();
        std::array<float, 8> voiceGains {};
        for (int v = 0; v < 8; ++v)
            voiceGains[static_cast<size_t>(v)] =
                chorusVoiceGain[static_cast<size_t>(v)].getNextValue();
        // Retain a normalised multi-voice contribution for feedback colour,
        // while a dedicated primary tap prevents the regeneration from being
        // averaged away as more Chorus voices are enabled.
        float chorusFeedbackNormaliser = 1.0f;
        if (runChorus)
        {
            float chorusVoiceGainSum = 0.0f;
            float farthestChorusVoiceOffsetMs = 0.15f;
            for (size_t voice = 0; voice < voiceGains.size(); ++voice)
            {
                chorusVoiceGainSum += voiceGains[voice];
                if (voiceGains[voice] > 0.000001f)
                    farthestChorusVoiceOffsetMs = 0.15f
                        + 12.0f * static_cast<float>(voice) / 7.0f;
            }
            finalChorusMinimumMs = chorusPreDelay + 0.15f;
            finalChorusMaximumMs = chorusPreDelay
                + farthestChorusVoiceOffsetMs + 2.0f * chorusDepth;
            chorusFeedbackNormaliser = 1.0f / juce::jmax(1.0f, chorusVoiceGainSum);
        }
        if (runFlanger)
        {
            finalFlangerMinimumMs = 0.4f + flangerOffset;
            finalFlangerMaximumMs = finalFlangerMinimumMs + 5.0f * flangerDepth;
        }

        std::array<float, channelCount> chorusSamples {};
        std::array<float, channelCount> phaserSamples {};
        std::array<float, channelCount> flangerSamples {};
        std::array<float, channelCount> doublerSamples {};
        std::array<float, 8> doubleVoiceGains {};
        for (size_t v = 0; v < doubleVoiceGains.size(); ++v)
            doubleVoiceGains[v] = doublerVoiceGain[v].getNextValue();
        // The AIR-style percentage is a *Vortex* scaling, not a claim about
        // AIR's undisclosed mapping: 250% here means ~+/-120 cents peak.
        float pitchHz = 0.0f;
        float pitchSweepMs = 0.0f;
        if (runDoubler)
        {
            if (doublerPitchRate != lastPitchRate
                || doublerPitchDepth != lastPitchDepth)
            {
                const float pitchCents = 120.0f * doublerPitchDepth / 250.0f;
                cachedPitchHz = 0.10f + 3.50f * std::pow(
                    doublerPitchRate * 0.01f, 1.70f);
                const float pitchRatio = std::exp2(pitchCents / 1200.0f) - 1.0f;
                cachedPitchSweepMs = pitchRatio * 1000.0f /
                    (juce::MathConstants<float>::twoPi * cachedPitchHz);
                lastPitchRate = doublerPitchRate;
                lastPitchDepth = doublerPitchDepth;
            }
            pitchHz = cachedPitchHz;
            pitchSweepMs = cachedPitchSweepMs;
            if (doublerSpread != lastDoublerSpread)
            {
                for (size_t v = 0; v < voicePan.size(); ++v)
                {
                    const float pan = doublerSpread * voicePan[v];
                    doublerLeftPan[v] = std::sqrt(1.0f - pan);
                    doublerRightPan[v] = std::sqrt(1.0f + pan);
                }
                lastDoublerSpread = doublerSpread;
            }
            if (doublerBaseFade >= 1.0f
                && std::abs(targetDoublerBaseMs - doublerBaseToMs) > 0.5f)
            {
                doublerBaseFromMs = doublerBaseToMs;
                doublerBaseToMs = targetDoublerBaseMs;
                doublerBaseFade = 0.0f;
            }
        }
        else
        {
            // Prepare the edited Timing/Tightness while off; don't trigger
            // an old delay-head crossfade when the engine comes back on.
            doublerBaseFromMs = doublerBaseToMs = targetDoublerBaseMs;
            doublerBaseFade = 1.0f;
        }
        const float delayFade = juce::jlimit(0.0f, 1.0f, doublerBaseFade);
        if (runDoubler)
            doublerBaseFade = juce::jmin(1.0f, doublerBaseFade + baseFadeStep);
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const auto c = static_cast<size_t>(channel);
            const float input = channel == 0 ? effectInputL : effectInputR;
            // Stable phase positions: changing voice count crossfades voices,
            // rather than instantly retuning every previously active voice.
            auto& chorusLine = chorusDelay[c];
            if (runChorus)
            {
                chorusLine[static_cast<size_t>(chorusWriteIndex)] =
                    sanitiseSample(input + chorusFeedback * chorusFeedbackSample[c], 16.0f);
                float chorusWet = 0.0f;
                float chorusPrimaryTap = 0.0f;
                // Independent, gently detuned LFO clocks prevent the voices from
                // perpetually sweeping together. Each tap also gets its own static
                // delay offset and gently asymmetrical modulation shape.
                for (int voice = 0; voice < 8; ++voice)
                {
                    const float gain = voiceGains[static_cast<size_t>(voice)];
                    if (gain <= 0.0000001f) continue;
                    const float stereoPhase = channel == 0 ? 0.0f
                        : 0.26f + 0.018f * static_cast<float>(voice);
                    const float phaseUnwrapped = static_cast<float>(
                        chorusVoicePhases[static_cast<size_t>(voice)]) + stereoPhase;
                    const bool wrapsIntoNextCycle = phaseUnwrapped >= 1.0f;
                    const float phase = phaseUnwrapped
                        - (wrapsIntoNextCycle ? 1.0f : 0.0f);
                    const size_t v = static_cast<size_t>(voice);
                    const float randomStart = wrapsIntoNextCycle
                        ? chorusRandomNext[v] : chorusRandomCurrent[v];
                    const float randomEnd = wrapsIntoNextCycle
                        ? chorusRandomFuture[v] : chorusRandomNext[v];
                    const int firstShape = juce::jlimit(0, 7,
                        static_cast<int>(chorusLfoShape));
                    const int nextShape = juce::jmin(7, firstShape + 1);
                    const float shapeBlend = chorusLfoShape
                        - static_cast<float>(firstShape);
                    const float firstWave = chorusShapeWave(firstShape, phase,
                        voice, randomStart, randomEnd, modulationSineTable,
                        modulationRoundedSquareTable);
                    const float requestedWave = shapeBlend == 0.0f
                        ? firstWave : firstWave + shapeBlend * (
                            chorusShapeWave(nextShape, phase,
                                voice, randomStart, randomEnd, modulationSineTable,
                                modulationRoundedSquareTable) - firstWave);
                    // Curved and discontinuous LFO choices must not jump the
                    // fractional delay reader abruptly at the waveform wrap.
                    // The default sine remains unfiltered as in v1.23.1.
                    auto& waveState = chorusWaveSmoothed[c][v];
                    const float smoothingAlpha = chorusLfoShape < 0.0001f
                        ? 1.0f : chorusWaveSmoothingAlpha;
                    waveState += smoothingAlpha * (requestedWave - waveState);
                    const float tapOffsetMs = 0.15f
                        + 12.0f * static_cast<float>(voice) / 7.0f;
                    const float delayMs = tapOffsetMs + chorusPreDelay
                        + chorusDepth * (1.0f + waveState);
                    if (voice == 0) finalChorusMs[c] = delayMs;
                    const float tap = readDelay(
                        chorusLine, chorusWriteIndex,
                        delayMs * static_cast<float>(currentSampleRate * 0.001));
                    // New voices occupy progressively different delays and
                    // stereo positions; the VOICES knob now changes width and
                    // ensemble density, not only the tap count.
                    constexpr std::array<float, 8> voicePan {
                        0.0f, -0.9f, 0.9f, -0.5f,
                        0.5f, -1.0f, 1.0f, 0.0f };
                    const float stereoWeight = activeChannels == 2
                        ? 1.0f + (channel == 0 ? -0.40f : 0.40f) * voicePan[v]
                        : 1.0f;
                    chorusWet += gain * stereoWeight * tap;
                    if (voice == 0)
                        chorusPrimaryTap = tap;
                }
                const float chorusAveragedTap =
                    chorusWet * chorusFeedbackNormaliser;
                const float chorusFeedbackTap =
                    0.75f * chorusPrimaryTap + 0.25f * chorusAveragedTap;
                // A bounded feedback sample keeps extreme settings controlled,
                // while the coefficient itself always remains below unity.
                chorusFeedbackSample[c] = std::tanh(chorusFeedbackTap);
                chorusSamples[c] = chorusWet;
            }
            else
            {
                // Lightweight ring-buffer input history, no feedback or
                // multi-voice taps. Fresh audio is ready when switched on.
                chorusLine[static_cast<size_t>(chorusWriteIndex)] = input;
                chorusFeedbackSample[c] = 0.0f;
            }

            if (runPhaser)
            {
                // Wide logarithmic sweep around an independently adjustable centre.
                // Spread offsets the R-channel LFO from 0 to 180 degrees.
                const float stereoOffset = channel == 0 ? 0.0f
                    : phaserStereo / 360.0f;
                const float phaseUnwrapped = static_cast<float>(phaserPhase)
                    + stereoOffset;
                const bool wrapsIntoNextCycle = phaseUnwrapped >= 1.0f;
                const float phase = phaseUnwrapped
                    - (wrapsIntoNextCycle ? 1.0f : 0.0f);
                const float randomStart = wrapsIntoNextCycle
                    ? phaserRandomNext : phaserRandomCurrent;
                const float randomEnd = wrapsIntoNextCycle
                    ? phaserRandomFuture : phaserRandomNext;
                const int firstShape = juce::jlimit(0, 7,
                    static_cast<int>(phaserLfoShape));
                const int nextShape = juce::jmin(7, firstShape + 1);
                const float blend = phaserLfoShape - static_cast<float>(firstShape);
                const auto waveAt = [&](int shape)
                {
                    // Preserve the original Phaser sine shape at WAVE=0.
                    if (shape == 0)
                        return modulationSine(modulationSineTable, phase);
                    return chorusShapeWave(shape, phase, 0, randomStart,
                        randomEnd, modulationSineTable, modulationRoundedSquareTable);
                };
                const float firstWave = waveAt(firstShape);
                const float requestedWave = blend == 0.0f
                    ? firstWave : juce::jmap(blend, firstWave,
                        waveAt(nextShape));
                // Smooth steep/switching waveforms before moving the all-pass
                // frequencies; the default sine remains unfiltered.
                auto& waveState = phaserWaveSmoothed[c];
                const float alpha = phaserLfoShape < 0.0001f
                    ? 1.0f : chorusWaveSmoothingAlpha;
                waveState += alpha * (requestedWave - waveState);
                const float phaserWave = waveState;
                const float maxFrequency = juce::jmin(12000.0f,
                    static_cast<float>(currentSampleRate * 0.40));
                const float phaserFrequency = juce::jlimit(25.0f,
                    maxFrequency, phaserCenter * std::exp2(
                        phaserDepth * 3.0f * phaserWave));
                finalPhaserHz[c] = phaserFrequency;
                // Use separate pole frequencies to spread the notches instead of
                // stacking coincident all-pass stages. Per-stage state stays live
                // so stage-count changes can crossfade without discontinuities.
                float phased = input + phaserFeedback * phaserFeedbackSample[c];
                std::array<float, 6> stageOutputs {};
                for (int pair = 0; pair < maxPhaserStages / 2; ++pair)
                {
                    const float poleFrequency = juce::jlimit(25.0f,
                        maxFrequency, phaserFrequency
                            * phaserPoleRatios[static_cast<size_t>(pair)]);
                    const float tablePosition = juce::jlimit(0.0f,
                        static_cast<float>(phaserCoefficientTableSize),
                        (poleFrequency - 25.0f) * phaserCoefficientTableScale);
                    const int lowerIndex = juce::jmin(
                        phaserCoefficientTableSize - 1,
                        static_cast<int>(tablePosition));
                    const float fraction = tablePosition
                        - static_cast<float>(lowerIndex);
                    const float lowerCoefficient = phaserCoefficientTable[
                        static_cast<size_t>(lowerIndex)];
                    const float coefficient = lowerCoefficient + fraction * (
                        phaserCoefficientTable[
                            static_cast<size_t>(lowerIndex + 1)]
                        - lowerCoefficient);
                    // Each pair shares a coefficient but has independent state.
                    for (int pole = 0; pole < 2; ++pole)
                    {
                        auto& state = phaserState[c][
                            static_cast<size_t>(pair * 2 + pole)];
                        const float next = coefficient * phased + state;
                        state = phased - coefficient * next;
                        phased = next;
                    }
                    stageOutputs[static_cast<size_t>(pair)] = phased;
                }
                const int lowerStage = juce::jlimit(0, 5,
                    static_cast<int>(phaserStageChoice));
                const int upperStage = juce::jmin(5, lowerStage + 1);
                const float selectedPhased = juce::jmap(
                    phaserStageChoice - static_cast<float>(lowerStage),
                    stageOutputs[static_cast<size_t>(lowerStage)],
                    stageOutputs[static_cast<size_t>(upperStage)]);
                // Bounded feedback and pure all-pass wet output: dry/wet mixing
                // occurs once in Vortex's common MIX stage, restoring deep notches.
                phaserFeedbackSample[c] = std::tanh(selectedPhased);
                phaserSamples[c] = sanitiseSample(selectedPhased, 8.0f);
            }

            auto& flangerLine = flangerDelay[c];
            if (runFlanger)
            {
                flangerLine[static_cast<size_t>(flangerWriteIndex)] = sanitiseSample(
                    input + flangerFeedback * flangerFeedbackSample[c], 16.0f);
                // Left oscillator stays anchored. PHASE rotates only the right
                // oscillator; 90 degrees reproduces the original stereo sweep.
                const float stereoPhase = channel == 0 ? 0.0f
                    : flangerStereoPhase / 360.0f;
                const float phaseUnwrapped = static_cast<float>(flangerPhase)
                    + stereoPhase;
                const bool wrapsIntoNextCycle = phaseUnwrapped >= 1.0f;
                const float phase = phaseUnwrapped
                    - (wrapsIntoNextCycle ? 1.0f : 0.0f);
                const float randomStart = wrapsIntoNextCycle
                    ? flangerRandomNext : flangerRandomCurrent;
                const float randomEnd = wrapsIntoNextCycle
                    ? flangerRandomFuture : flangerRandomNext;
                const int firstShape = juce::jlimit(0, 7,
                    static_cast<int>(flangerShape));
                const int nextShape = juce::jmin(7, firstShape + 1);
                const float shapeBlend = flangerShape
                    - static_cast<float>(firstShape);
                const auto getFlangerWave = [&](int shape)
                {
                    // The original Flanger sine shape is retained at WAVE=0.
                    if (shape == 0)
                        return modulationSine(modulationSineTable, phase);
                    return chorusShapeWave(shape, phase, 0, randomStart,
                        randomEnd, modulationSineTable, modulationRoundedSquareTable);
                };
                const float firstWave = getFlangerWave(firstShape);
                const float requestedWave = shapeBlend == 0.0f
                    ? firstWave : firstWave + shapeBlend
                        * (getFlangerWave(nextShape) - firstWave);
                // Smooth discontinuous waveforms and parameter transitions
                // before converting the LFO into a fractional delay position.
                const float smoothingAlpha = flangerShape < 0.0001f
                    ? 1.0f : chorusWaveSmoothingAlpha;
                auto& waveState = flangerWaveSmoothed[c];
                waveState += smoothingAlpha * (requestedWave - waveState);
                const float flangerWave = 0.5f + 0.5f * waveState;
                const float flangeMs = 0.4f + flangerOffset
                    + 5.0f * flangerDepth * flangerWave;
                finalFlangerMs[c] = flangeMs;
                const float flanged = readDelay(flangerLine, flangerWriteIndex,
                    flangeMs * static_cast<float>(currentSampleRate * 0.001));
                // Two-pole tone filter: 0% is deeply dark, 100% is unfiltered.
                // The high end crossfades back to the exact unfiltered tap.
                flangerToneState[c] += flangerToneAlpha
                    * (flanged - flangerToneState[c]);
                flangerToneSecondState[c] += flangerToneAlpha
                    * (flangerToneState[c] - flangerToneSecondState[c]);
                const float colouredWet = juce::jmap(flangerUnfilteredBlend,
                    flangerToneSecondState[c], flanged);
                flangerFeedbackSample[c] = sanitiseSample(colouredWet, 16.0f);
                // Master MIX already supplies the dry path: a second dry blend
                // here masked OFFSET, PHASE, SHAPE and low-feedback changes.
                flangerSamples[c] = sanitiseSample(colouredWet, 16.0f);
            }
            else
            {
                flangerLine[static_cast<size_t>(flangerWriteIndex)] = input;
            }

            // Record the input independently for each host channel; voices
            // are rendered below using their own delays and stereo panning.
            doublerDelay[c][static_cast<size_t>(doublerWriteIndex)] = input;
        }
        if (runDoubler)
        {
        // Eight independent pitch/timing voices.     Constant delay heads are
            // crossfaded for timing changes, while slow modulation is intentional.
            // Stereo pan weights are cached per block and recalculated only
            // when the smoothed SPREAD value changes.
            // Detune is a separate, persistent pitch offset for each voice,
            // not merely more slow delay-time wobble. Opposite-sign pairs
            // give a recognisable stereo ensemble even with P DEPTH at zero.
            constexpr std::array<float, 8> voiceDetuneCents {
                -36.0f, 36.0f, -22.0f, 22.0f,
                -13.0f, 13.0f, -29.0f, 29.0f };
            const float maxDelaySamples = static_cast<float>(
                doublerDelay[0].size() - 3);
            finalDoublerMinimumMs = 1350.0f;
            finalDoublerMaximumMs = 0.0f;
            finalDoublerMs[0] = finalDoublerMs[1] = 0.0f;
            for (size_t v = 0; v < 8; ++v)
            {
                const float gain = doubleVoiceGains[v];
                if (gain <= 0.0000001f) continue;
                const float pitchWave = modulationSine(modulationSineTable,
                    static_cast<float>(doublerVoicePitchPhases[v]));
                const float driftWave = modulationSine(modulationSineTable,
                    static_cast<float>(doublerVoiceDriftPhases[v]));
                // Two 220-ms overlap-add read heads, a half cycle apart, each
                // run at a slightly different reading speed. The inactive head
                // wraps only at zero gain, avoiding clicks from head resets.
                // The first-order cents-to-ratio approximation has <2% error
                // at the largest +/-36-cent offset and avoids expensive pow()
                // calls in the per-sample voice loop.
                const float detunePhase = static_cast<float>(
                    doublerVoiceDetunePhases[v]);
                const float detunePhaseB = detunePhase < 0.5f
                    ? detunePhase + 0.5f : detunePhase - 0.5f;
                const float detuneSin = modulationSine(modulationSineTable,
                    0.5f * detunePhase);
                const float headAGain = detuneSin * detuneSin;
                const float headBGain = 1.0f - headAGain;
                const float detuneSpanSamples = -0.00057762265f
                    * doublerDetune * voiceDetuneCents[v]
                    * static_cast<float>(currentSampleRate * 0.220);
                const float headAOffset = detuneSpanSamples * (detunePhase - 0.5f);
                const float headBOffset = detuneSpanSamples * (detunePhaseB - 0.5f);
                // Base moves automatically away from the write head when deep
                // pitch sweeps are selected; there is no clipped/cusped LFO.
                const float sweptMs = pitchSweepMs / voicePitchSpeed[v]
                    * (1.0f + pitchWave);
                const float voiceOffsetMs = 2.0f + 3.5f * static_cast<float>(v)
                    + 1.3f * static_cast<float>(v % 3);
                const float wanderingMs = doublerDetune * 1.5f * (1.0f + driftWave);
                const float fromMs = doublerBaseFromMs + voiceOffsetMs
                    + wanderingMs + sweptMs;
                const float toMs = doublerBaseToMs + voiceOffsetMs
                    + wanderingMs + sweptMs;
                // Guide includes both Timing crossfade heads and the moving
                // pitch/detune read positions; this does not change audio.
                const float samplesToMs = 1000.0f /
                    static_cast<float>(currentSampleRate);
                const float readingMs = juce::jmap(delayFade, fromMs, toMs)
                    + (headAGain * headAOffset + headBGain * headBOffset) * samplesToMs;
                if (v == 0 || v == 1)
                    finalDoublerMs[v] = juce::jmax(0.0f, readingMs);
                const float headMinimumMs = juce::jmin(headAOffset, headBOffset)
                    * samplesToMs;
                const float headMaximumMs = juce::jmax(headAOffset, headBOffset)
                    * samplesToMs;
                finalDoublerMinimumMs = juce::jmin(finalDoublerMinimumMs,
                    juce::jmax(0.0f, juce::jmin(fromMs, toMs) + headMinimumMs));
                finalDoublerMaximumMs = juce::jmax(finalDoublerMaximumMs,
                    juce::jmin(1350.0f, juce::jmax(fromMs, toMs) + headMaximumMs));
                const float fromSamples = juce::jlimit(1.0f, maxDelaySamples,
                    fromMs * static_cast<float>(currentSampleRate * 0.001));
                const float toSamples = juce::jlimit(1.0f, maxDelaySamples,
                    toMs * static_cast<float>(currentSampleRate * 0.001));
                // Apply the same detune to both sides of the Timing/Tightness
                // crossfade. Thus either control can move while detuning without
                // resetting pitch phases or jumping the active delay position.
                const auto readDetuned = [&](size_t channel, float delaySamples)
                {
                    const auto& line = doublerDelay[channel];
                    const float tapA = readDelay(line, doublerWriteIndex,
                        juce::jlimit(1.0f, maxDelaySamples,
                            delaySamples + headAOffset));
                    if (headAOffset == headBOffset)
                        return tapA; // Both read heads coincide (Detune at zero).
                    const float tapB = readDelay(line, doublerWriteIndex,
                        juce::jlimit(1.0f, maxDelaySamples,
                            delaySamples + headBOffset));
                    return headAGain * tapA + headBGain * tapB;
                };
                const float leftTap = delayFade >= 1.0f
                    ? readDetuned(0, toSamples)
                    : (delayFade <= 0.0f
                        ? readDetuned(0, fromSamples)
                        : juce::jmap(delayFade,
                            readDetuned(0, fromSamples),
                            readDetuned(0, toSamples)));
                float doubleVoice = leftTap;
                if (activeChannels == 2)
                {
                    const float rightTap = delayFade >= 1.0f
                        ? readDetuned(1, toSamples)
                        : (delayFade <= 0.0f
                            ? readDetuned(1, fromSamples)
                            : juce::jmap(delayFade,
                                readDetuned(1, fromSamples),
                                readDetuned(1, toSamples)));
                    doubleVoice = 0.5f * (leftTap + rightTap);
                    // At Spread 0 the two channels match. At 100% the pairs
                    // occupy distinct, nearly hard-panned stereo positions.
                    doublerSamples[0] += gain * doublerLeftPan[v] * doubleVoice;
                    doublerSamples[1] += gain * doublerRightPan[v] * doubleVoice;
                }
                else
                    doublerSamples[0] += gain * doubleVoice;
            }
            if (doubleVoiceGains[1] <= 0.0000001f)
                finalDoublerMs[1] = finalDoublerMs[0];
            // Boost the completed Doubler wet signal equally for both routing modes.
            for (auto& channelWet : doublerSamples)
                channelWet *= doublerWetGain;
        }
        // Stereo width alters the wet sides, without changing dry input level.
        if (runChorus && activeChannels == 2)
        {
            const float mid = 0.5f * (chorusSamples[0] + chorusSamples[1]);
            chorusSamples[0] = mid + chorusWidth * (chorusSamples[0] - mid);
            chorusSamples[1] = mid + chorusWidth * (chorusSamples[1] - mid);
        }
        // All four trims are wet-only and per-sample smoothed.
        const std::array<float, 4> engineLevels {
            effectLevelValues[0].getNextValue(),
            effectLevelValues[1].getNextValue(),
            effectLevelValues[2].getNextValue(),
            effectLevelValues[3].getNextValue() };
        float wetL = 0.0f, wetR = 0.0f;
        for (int channel = 0; channel < activeChannels; ++channel)
        {
            const size_t c = static_cast<size_t>(channel);
            const float input = channel == 0 ? effectInputL : effectInputR;
            const float enabledSum = effectGains[0] + effectGains[1]
                + effectGains[2] + effectGains[3];
            // Equal-weight parallel blend of only the enabled engines.
            // The dry term makes the transition continuous when the last LED
            // fades out, including the zero-engines-enabled case.
            const float manualWet = (
                effectGains[0] * engineLevels[0] * chorusSamples[c]
                + effectGains[1] * engineLevels[1] * phaserSamples[c]
                + effectGains[2] * engineLevels[2] * flangerSamples[c]
                + effectGains[3] * engineLevels[3] * doublerSamples[c]
                // The no-engine fallback is dry, so INPUT gating covers it.
                + juce::jmax(0.0f, 1.0f - enabledSum) * input * dryGate)
                    / juce::jmax(1.0f, enabledSum);
            const float wet = manualWet;
            if (channel == 0) wetL = wet;
            else wetR = wet;
        }
        if (++chorusWriteIndex == static_cast<int>(chorusDelay[0].size()))
            chorusWriteIndex = 0;
        if (++flangerWriteIndex == static_cast<int>(flangerDelay[0].size()))
            flangerWriteIndex = 0;
        if (++doublerWriteIndex == static_cast<int>(doublerDelay[0].size()))
            doublerWriteIndex = 0;
        const auto advancePhase = [this](double& phase, float frequency)
        {
            phase += static_cast<double>(frequency) / currentSampleRate;
            if (phase >= 1.0) phase -= std::floor(phase);
        };
        // Deliberately non-harmonic clock ratios create an evolving ensemble.
        constexpr std::array<float, 8> chorusSpeedRatios {
            0.86f, 1.09f, 0.93f, 1.19f, 0.78f, 1.27f, 1.03f, 1.39f };
        if (runChorus)
            for (size_t v = 0; v < chorusVoicePhases.size(); ++v)
            {
                const double previous = chorusVoicePhases[v];
                advancePhase(chorusVoicePhases[v], chorusRate * chorusSpeedRatios[v]);
                if (chorusVoicePhases[v] < previous)
                {
                    chorusRandomCurrent[v] = chorusRandomNext[v];
                    chorusRandomNext[v] = chorusRandomFuture[v];
                    chorusRandomFuture[v] = nextChorusRandom(chorusRandomSeeds[v]);
                }
            }
        if (runPhaser)
        {
            const double previous = phaserPhase;
            advancePhase(phaserPhase, phaserRate);
            if (phaserPhase < previous)
            {
                phaserRandomCurrent = phaserRandomNext;
                phaserRandomNext = phaserRandomFuture;
                phaserRandomFuture = nextChorusRandom(phaserRandomSeed);
            }
        }
        if (runFlanger)
        {
            const double previous = flangerPhase;
            advancePhase(flangerPhase, flangerRate);
            if (flangerPhase < previous)
            {
                flangerRandomCurrent = flangerRandomNext;
                flangerRandomNext = flangerRandomFuture;
                flangerRandomFuture = nextChorusRandom(flangerRandomSeed);
            }
        }
        if (runDoubler)
            for (size_t v = 0; v < doublerVoicePitchPhases.size(); ++v)
            {
                advancePhase(doublerVoicePitchPhases[v], pitchHz * voicePitchSpeed[v]);
                advancePhase(doublerVoiceDriftPhases[v],
                    0.11f + 0.037f * static_cast<float>(v));
                advancePhase(doublerVoiceDetunePhases[v], 1.0f / 0.220f);
            }
        if (gateMode == 1)
        {
            wetL *= sequencerGateGain;
            wetR *= sequencerGateGain;
        }
        const float mix = mixValue.getNextValue();
        const float gain = outputGainValue.getNextValue();
        float processedL = gain * (dryL * dryGate * (1.0f - mix) + wetL * mix);
        float processedR = gain * (dryR * dryGate * (1.0f - mix) + wetR * mix);
        if (gateMode == 2)
        {
            processedL *= sequencerGateGain;
            processedR *= sequencerGateGain;
        }
        left[sample] = sanitiseSample(
            processedL + (dryL - processedL) * bypass, 64.0f);
        if (right != nullptr)
            right[sample] = sanitiseSample(
                processedR + (dryR - processedR) * bypass, 64.0f);
    }
    // The guide used only the last processed Phaser values in the original
    // code. Preserve the exact formula, but avoid two exp2() calls per sample.
    if (phaserGuideWasActive)
    {
        finalPhaserMinimum = juce::jmax(25.0f,
            lastPhaserCenterForGuide * std::exp2(-lastPhaserDepthForGuide * 3.0f));
        finalPhaserMaximum = juce::jmin(
            juce::jmin(12000.0f, static_cast<float>(currentSampleRate * 0.40)),
            lastPhaserCenterForGuide * std::exp2(lastPhaserDepthForGuide * 3.0f));
    }
    // One atomic update per block; the GUI never reads DSP filter state.
    phaserDisplayHz[0].store(finalPhaserHz[0], std::memory_order_relaxed);
    phaserDisplayHz[1].store(
        activeChannels > 1 ? finalPhaserHz[1] : finalPhaserHz[0],
        std::memory_order_relaxed);
    phaserDisplayMinimumHz.store(finalPhaserMinimum, std::memory_order_relaxed);
    phaserDisplayMaximumHz.store(finalPhaserMaximum, std::memory_order_relaxed);
    for (size_t channel = 0; channel < channelCount; ++channel)
    {
        chorusDisplayMs[channel].store(
            finalChorusMs[activeChannels > static_cast<int>(channel) ? channel : 0],
            std::memory_order_relaxed);
        flangerDisplayMs[channel].store(
            finalFlangerMs[activeChannels > static_cast<int>(channel) ? channel : 0],
            std::memory_order_relaxed);
    }
    chorusDisplayMinimumMs.store(finalChorusMinimumMs, std::memory_order_relaxed);
    chorusDisplayMaximumMs.store(finalChorusMaximumMs, std::memory_order_relaxed);
    flangerDisplayMinimumMs.store(finalFlangerMinimumMs, std::memory_order_relaxed);
    flangerDisplayMaximumMs.store(finalFlangerMaximumMs, std::memory_order_relaxed);
    for (size_t voice = 0; voice < finalDoublerMs.size(); ++voice)
        doublerDisplayMs[voice].store(finalDoublerMs[voice], std::memory_order_relaxed);
    doublerDisplayMinimumMs.store(finalDoublerMinimumMs, std::memory_order_relaxed);
    doublerDisplayMaximumMs.store(finalDoublerMaximumMs, std::memory_order_relaxed);
}

void VortexAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    state.setProperty("schemaVersion", stateSchemaVersion, nullptr);
    if (const auto xml = state.createXml())
        copyXmlToBinary(*xml, destination);
}

void VortexAudioProcessor::setStateInformation(const void* data, int size)
{
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || ! xml->hasTagName("VortexParameters"))
        return;
    auto state = juce::ValueTree::fromXml(*xml);
    if (static_cast<int>(state.getProperty("schemaVersion", 0))
        != stateSchemaVersion)
        return;
    setSavedParameterState(state);
    parameters.replaceState(state);
    pendingProcessingReset.store(true, std::memory_order_release);
}

juce::AudioProcessorEditor* VortexAudioProcessor::createEditor()
{
    return new VortexAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VortexAudioProcessor();
}
