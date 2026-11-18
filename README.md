# Vortex

<div align="center">

[![Release](https://img.shields.io/github/v/release/sl2365/Vortex?style=for-the-badge-square&logo=github&logoColor=white&color=purple)](https://github.com/sl2365/Vortex/releases/latest/download/Vortex.rar)
[![Release Date](https://img.shields.io/github/release-date/sl2365/Vortex?style=for-the-badge-square&logo=github&logoColor=white&color=yellow)](https://github.com/sl2365/Vortex/releases)
[![Latest Asset Downloads](https://img.shields.io/github/downloads/sl2365/Vortex/latest/Vortex.rar?style=for-the-badge-square&logo=github&logoColor=white&label=downloads-latest&displayAssetName=false&color=blue)](https://github.com/sl2365/Vortex/releases/latest)
[![Total Downloads](https://img.shields.io/github/downloads/sl2365/Vortex/total?style=for-the-badge-square&logo=github&logoColor=white&label=downloads-total&color=blue)](https://github.com/sl2365/Vortex/releases)

</div>

![Vortex](Resources/Vortex-Seq.jpg)


**Version:** 0.1.21 (Flanger feedback -100% to +100%, stronger low settings and text rendering; Windows/DAW build unverified)  
**Platform:** Windows x64 VST3  
**Framework:** JUCE 9.0.4  
**Identity:** `sl23` / `vtx1` / `com.sl23.vortex`

## Concept

Vortex is a single modulation effect with four independent engines:

- **Chorus:** phase-spaced, multi-voice modulated fractional-delay chorus. Adjust Rate, Depth, Pre-Delay, Voices, Feedback and Width.
- **Phaser:** enhanced 2–12-pole cascaded all-pass phaser. All six controls (Rate, Depth, Stages, Centre Frequency, Feedback and Stereo LFO Phase/Spread) appear together, with a red inline guide showing the swept frequency range and live left/right positions. Vortex’s global MIX handles dry/wet blending once.

When an effect is switched off with its amber LED, its knobs, values and labels are dimmed but **remain fully adjustable**. The header guide shows an inactive grey track. You can prepare new settings while that effect is silent and bring it back in using its LED; its normal colours and live guide return immediately. Sequencer targets are retained, but bypassed knobs show their actual parameter positions while being edited, not sequencer-animated positions. Audio routing is unchanged.

**Inline modulation guides:** Each panel has a red bar beside its heading, with a swept range and two moving indicators. Chorus shows delay tap movement in milliseconds, Phaser the frequency sweep in Hz, Flanger the short delay sweep in milliseconds, and Doubler the ensemble's active timing spread in milliseconds (two dots represent the first two voices). Doubler uses a logarithmic timing display so short doubling delays remain visible. These displays do not affect processing or add parameters.
- **Flanger:** short modulated fractional delay with feedback. Adjust Rate, Depth, Feedback, stereo LFO Phase (0–180°), base delay Offset (0–12 ms), sweep Shape (sine to triangle to rounded-square) and wet/feedback Tone (deep two-pole low-pass to unfiltered).
- **Doubler:** up to eight independently positioned and detuned delay voices. Adjust Tightness, Detune, Spread, Pitch Depth, Pitch Rate and Timing. This is intended as *naturalistic doubling*, not a formant-preserving professional vocal pitch shifter.

**Routing:** Motion Map has been completely removed. All independently enabled engines feed a normalised parallel wet bus. Their amber LEDs control participation; the master MIX controls the combined dry/wet balance, and OUT adjusts final gain. Four compact top-row LEVEL knobs (Chorus, Phaser, Doubler, Flanger) independently trim each wet effect from 0% to 200% (100% unity); they are available as sequencer targets and remain editable while an effect is off. Its former space is used by Phaser and Flanger.

**Gate modes:** OFF does not gate audio; INPUT gates only the dry path while the effects continue to receive signal; EFFECT gates only the combined wet signal; OUTPUT gates the complete dry/wet output. Smooth gate transitions are retained.

The full 32-step sequencer and gate sequencer from the Dusk v0.1.1 source remain present: host-sync/free-run operation, step envelopes, waveform/direction choices, drag/drop modulation assignment, 16 target slots, independent slot depths, gate options, sequence files and preset navigation. Sequencer targets include Chorus, Doubler and the additional Phaser parameters, plus effect On/Off switches; the 32 steps and all 16 assignable target slots remain.

Click the **Vortex logo** for a **Dusk-style dark GUI Zoom popup**, with 75%, 100%, 125%, 150%, 175%, 200%, 225%, 250%, 275% and 300% options. The popup displays percentages only, with the current preset zoom level checked. Both zoom and pixel dimensions remain available in the logo tooltip and resize feedback; manual edge resizing remains supported.

## Build (Windows)

1. Place the extracted `Vortex` folder at `_Projects\Vortex`, directly beside the shared `_Projects\_Tools` folder.
2. Ensure JUCE is installed at `_Projects\_Tools\JUCE\_9.0.4` and CMake is installed at `_Projects\_Tools\cmake\_4.4.2`.
3. Double-click `_Projects\Vortex\- Build.bat`.
4. Inspect `Results.log` for any build errors. The script builds a Windows x64 VST3 and copies the result into `dist\Vortex.vst3` if compilation succeeds.

No automatic VST3 installation takes place. The build deliberately does not overwrite Afterglow or Dusk.

## Independent identity / data

- Plugin ID: `vtx1`
- Bundle ID: `com.sl23.vortex`
- State root: `VortexParameters`
- Preset format: `.vortexpreset`
- Sequence format: `.vortexseq`
- Portable user-data tree: `Vortex/Settings`, `Vortex/Presets`, `Vortex/Sequences`, beside the VST3 bundle.

## Initial release limitations and required verification

This source revision was prepared without a Windows / Visual Studio / JUCE build environment. Although the plugin has been tried by the user, the revised source still requires a Windows build and DAW validation. In particular, smoother parameter transitions are implemented, but the reported adjustment artifacts must be retested in a DAW. Phaser and Flanger tuning remains preliminary. Also verify UI/zoom, VST3 ID and state loading, presets, parallel wet-bus level stability, mono/stereo behavior and the preserved sequencer. The doubler's pitch offsets are implemented via slowly drifting delay taps, not formant correction; stronger settings may produce chorus-like movement.

The original uploaded Dusk source was left unchanged and serves as the parent template for this first fork.

### Controls

- Each effect section has an amber on/off LED; off returns its contribution smoothly to the unprocessed sound.
- Chorus Rate 0.01–10 Hz, Depth 0–24 ms, Pre-Delay 0–50 ms, Voices 1–8, Feedback 0–100% (stronger curved response, dedicated feedback tap and bounded below-unity regeneration), Width 0–200%.
- Doubler Pitch Depth 0–250%, Pitch Rate 0–100%, Timing 0–800 ms, plus Tightness, Detune, Spread and Voices 1–8.
- Control changes are smoothed on the audio thread. Original 32-step sequencer and 16 target slots are preserved. As a pre-release project, this revision does not guarantee compatibility with earlier prototype preset files or automation.

### Chorus and effect routing

The Chorus now uses eight independently drifting delay-tap clocks (with the selected Voices count controlling the active taps) instead of one shared phase sweep. Feedback defaults to zero and uses a dedicated primary tap blended with a smaller normalised multi-voice contribution, so adding Voices no longer weakens regeneration. All enabled engines play together in a normalised parallel wet mix. The four individual amber FX LEDs determine which engines participate; on/off transitions remain smoothed.

All engine knob labels and numeric readouts use fixed and identical sizes. Doubler Pitch Depth and Rate now use fixed-size percentage readouts to match the AIR comparison workflow; these percentages are not a reverse-engineered AIR scale. This pre-release build is not guaranteed preset-compatible with earlier versions.

### Chorus controls

- **Feedback** is unipolar 0–100% (default 0%). Its stronger response curve reaches a bounded 0.985 maximum coefficient. A dedicated primary delay tap supplies most of the regeneration, with a smaller normalised multi-voice component retaining ensemble colour without letting Voices dilute the effect.
- **Pre-Delay** is a new, sequencer-assignable 0–50 ms control (default 18 ms). It sets the minimum base delay before the moving voice delays. A positive voice-offset delay ensures the tap never reads ahead of the writer.
- Six Chorus controls retain the same label/readout font and geometry as the other effect sections.
- Chorus timing adjusts to add Pre-Delay; no additional Chorus sound-shaping was attempted in this iteration.

### Doubler engine

- Eight independent doubling voices selected using the seventh **VOICES** knob in the widened Doubler panel. Voice count remains sequencer assignable.
- **P DEPTH** 0-250% (Vortex maps 250% to approximately +/-120 cents peak), **P RATE** 0-100% (about 0.10-3.60 Hz internally), **TIMING** 0-800 ms. AIR's percent-to-pitch conversion is not public; these are not AIR-matched DSP units.
- Voice-specific pitch movement, low-speed drift, variable timing offsets, and stereo panning. Timing and Tightness changes crossfade between taps to avoid Doppler glides.
- Extended internal delay memory to accommodate long offsets and deep pitch sweeps. At high Timing settings the result can become echo-like rather than a tight vocal double.
- Test in stereo, with only Doubler enabled and MIX initially at 50%.

### Doubler and panel layout

- Left column: Chorus (six equal-sized knobs), Doubler (seven equal-sized knobs). Right column: Phaser (six knobs in one row, with the red frequency guide in its title line) and Flanger (seven knobs). All four effect panels have equal heights.
- Doubler P RATE now appears before P DEPTH. The Voices dropdown was replaced by a knob linked to the same 1–8 parameter.
- Detune creates sustained independent voice pitch offsets of up to approximately +/-36 cents at 100%, instead of only a small delay wobble. Dual crossfaded delay read heads suppress resets; the result remains a creative delay-based effect, not a formant-preserving pitch shifter.
- To audition Detune: only Doubler ON, MIX 50%, Voices 2 or 8, Pitch Depth 0%, then sweep Detune from 0% to 100%. Test on sustained notes with headphones.
