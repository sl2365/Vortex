# Vortex — Changelog

## 1.24.13 — Host shutdown investigation and compiler warnings — 20 November 2026

- Avoid the global `juce::ImageCache` singleton and the function-local static background image. Decode the embedded PNG once per editor using `juce::ImageFileFormat::loadFrom`, and share a lightweight JUCE image reference with the sequencer. The image is released with the editor; no additional cache/timer is started just for the background. This targets a plausible Windows host-exit hang but still requires PHI and SAVIHost validation.
- Fix MSVC warning C4458 by renaming the `TwoLineHelpLabel::paint` local `font` to `helpFont` (including its lambda capture).
- Fix MSVC warning C4456 by renaming the inner Chorus `voicePan` lookup to `chorusVoicePan`, avoiding shadowing the outer Doubler lookup.
- No changes to DSP calculations, waveforms, effect/sequence parameters, theme appearance, panel geometry, plugin identity, or `Resources`.

## 1.24.12 — Shutdown diagnostic: display-synchronised GUI updates — 20 November 2026

- Replace the editor's 30 Hz `juce::Timer` with an editor-owned `juce::VBlankAttachment` limited to approximately 30 updates/second, to test a possible JUCE TimerThread-related host shutdown hang.
- Release the VBlank attachment first in the editor destructor, before freeing other editor members.
- No DSP, parameter, sequencer processing, artwork or interface geometry changes. This is a diagnostic candidate until tested with PHI and SAVIHost on Windows.

## 1.24.11 — Modulation DSP CPU reduction — 20 November 2026

- Use 65,536-sample sine and rounded-square lookup tables prepared in `prepareToPlay()` for the four modulation engines, replacing repeated expensive trigonometric calculations in the audio callback. All eight waveform selections, their phase/rate behaviour, crossfades and routing remain the same; only minute floating-point differences from interpolation are expected.
- Move the Phaser sweep guide's two exponential calculations from every audio sample to once per processing block, using the final active smoothed values as before. This is a display-only change and does not alter its filter DSP or visual range.
- Standalone waveform checks: sine worst error 4.2e-7, rounded-square 1.3e-6, with relative waveform RMS differences below -134 dB. An isolated eight-voice Chorus audio comparison found approximately -84.5 dB relative RMS difference at 44.1, 48 and 96 kHz. **Not bit-identical**, but intended to retain audible character.
- Isolated Linux C++ microbenchmarks measured approximately 2.1–2.5x faster sine evaluation and 5.9x faster rounded-square evaluation. Those are hot-kernel figures, **not** measured full-VST3 or PHI CPU reductions.
- No changes to the GUI, JUCE plugin identity, parameters, preset schema, gate, sequencer, effect bypass or Resources. The Windows build, whole-plugin CPU reduction, and DAW sound comparison remain to be verified.

## 1.24.10 — Audio engine CPU optimisation — 20 November 2026

- Phaser: replace 12 per-stereo-sample tangent calculations for the six all-pass pairs with a 16,384-step coefficient lookup precomputed at `prepareToPlay()` for the current sample rate. Preserve the original stage structure, resonance, feedback, and smoothing. A standalone high-feedback Phaser simulation found an RMS difference of about -109 to -118 dB relative to the original signal at 44.1–192 kHz.
- Chorus, Phaser and Flanger: skip evaluating a second LFO shape when the selected waveform blend is exactly zero; keep the existing waveform definitions and smooth transitions.
- Flanger: reuse the tone-filter coefficient while TONE is stationary; recompute on the very next changed sample.
- Doubler: cache stationary pitch-depth/rate calculations and stereo pan weights; avoid unused Timing/Tightness crossfade reads once the transition is finished, and avoid duplicate detune-head reads when both heads coincide. Crossfades, independent voices and delay state remain intact.
- All delay engines: replace integer modulo wrapping in the fractional delay reader and ring write indices with equivalent conditional wrapping. Calculate per-voice gain normalization only once per processing block.
- Preserve all four effect algorithms, wet/dry and gate routing, smoothing, bypass fades, sequencer assignments, GUI, settings and embedded artwork. No new parameters or dependencies.
- Standalone numerical and hot-path tests passed; the complete Windows VST3 was not compiled or DAW/CPU-benchmarked in this environment. Test sound, CPU use and long playback at 44.1/48/96 kHz before release.

## 1.24.9 — Three-stage Theme background control — 19 November 2026

- Make THEME 0% display the existing vortex backdrop at full artwork strength, fading it out to the original plain charcoal surround at 50%.
- From THEME 50% to 100%, fade in the original solid aqua surround colour; preserve the 100% default and settings storage.
- Keep one image spanning the two separate background panels, with opaque inner controls and the dark dividing gap unchanged.
- Update the Theme contextual help; no DSP, sequencer, parameter IDs, layout, or Resources changes.

## 1.24.8 — Compact contextual help — 19 November 2026

- Show contextual tooltips as `PARAMETER NAME | Description` on one line whenever the full text fits.
- Wrap longer help at word boundaries onto a second line only; preserve the existing font size and prevent a third line from being drawn.
- Keep the GUI layout, effects, sequencer and all DSP processing unchanged.

## 1.24.7 — Single-image background and opaque controls — 19 November 2026

- Render the single existing `Resources/vortex_backdrop.png` image across the combined main and sequencer backgrounds in one consistent coordinate space; crop each panel to its respective portion rather than repeating the artwork twice.
- Preserve the eight-pixel dark gap between background panels and make all inner effect, mix and sequencer control panels completely opaque.
- No changes to the image asset, Resources folder, DSP, sequencer, controls, or layout.

## 1.24.6 — Artwork location and Windows build fix — 19 November 2026

- Move the new, embedded layered aqua background artwork from `source/vortex_backdrop.png` to `Resources/vortex_backdrop.png`; CMake now embeds it from there.
- Fix Visual Studio C3480/C4573/C2439/C2064 in `PluginEditor.cpp` by capturing `this` instead of trying to capture the `presetBrowserOpen` member directly in the panel-drawing lambda.
- Add an explicit missing-background check to the Windows build script. The source ZIP contains only the newly introduced resource image, not the user's existing logo or screenshots.
- No changes to DSP, sequencer, controls or GUI layout.

## 1.24.5 — Layered aqua vortex artwork — 19 November 2026

- Rebuilt from the untouched v1.24.0 source, not from any v1.24.1–v1.24.4 background experiments.
- Added a high-quality aqua vortex texture embedded directly from `source/vortex_backdrop.png` (never from `Resources`).
- Rendered inside the two existing, separately clipped aqua background panels; preserved their 8px gap, the dark control surfaces and the original layout.
- No changes to effect DSP, routing, sequencer, controls or plugin identifiers.

## 1.24.0 — Version milestone — 18 November 2026

- Bump version metadata and display from 1.23.6 to 1.24.0 to mark the expanded Chorus, Phaser and Flanger waveform controls.
- No DSP, controls, parameter IDs, sequencer, plugin identity, or layout changes from v1.23.6.

## 1.23.6 — Unified WAVE controls — 18 November 2026

- Rename the visible Flanger SHAPE knob, host-facing parameter name and sequencer label to WAVE; preserve the existing `flangerShape` parameter ID and target 43.
- Add a seventh, identical-size WAVE knob to Phaser, with the same eight choices as Chorus and Flanger. Phaser's original Sine sweep is the default, and the new shapes sweep the actual all-pass filter frequencies.
- Give Phaser a private deterministic Random LFO, independent left/right phases, and smoothed modulation for stepped shapes. Preserve Chorus, Flanger, Doubler DSP and all four 82px panels.
- Append Phaser WAVE as sequencer target 46, preserving targets 1-45 and all 16 slots. Bump the pre-release state schema to 5; older prototype presets are not migrated.
- Static source/layout/waveform checks and ZIP integrity verified; Windows VST3 compilation and DAW audition remain outstanding.

## 1.23.5 — Fix eight-wave Flanger build error — 18 November 2026

- Fix Visual Studio C2665 at `PluginProcessor.cpp:353`: construct the Flanger SHAPE wave-list parameter with `juce::AudioParameterChoice`, not `juce::AudioParameterFloat`.
- Remove the now-unused `channelPhase` local at the Chorus render loop, eliminating warning C4189.
- No waveform algorithms, parameter IDs, defaults, sequencer targets, control geometry, state schema or logo logic changed. This is a compile repair only.
- Source/static and ZIP checks passed; a Windows JUCE VST3 build and DAW validation are still required.

## 1.23.4 — Matching eight-wave Chorus and Flanger LFOs — 18 November 2026

- Add Rounded Square as the eighth choice on the existing Chorus WAVE knob.
- Replace Flanger SHAPE's 0-100% three-wave blend with the same eight selectable LFO shapes: Sine, Triangle, Saw, Ramp, Exponential, Inverse Exponential, Random, and Rounded Square. Preserve Flanger's original sine at the default and its rounded-square character at the last position.
- Both choices have 60 ms smoothed transitions. Flanger uses deterministic, smoothly interpolated Random values and an 8 ms waveform smoothing stage to keep delay read-head motion controlled on stepped shapes.
- Keep Chorus and Flanger control placement, seven full-size knobs per panel, all four 82 px panels, unchanged sequencer targets (43 for Flanger SHAPE and 45 for Chorus WAVE), and existing audio routing. Increase the pre-release state schema to 4 because Flanger SHAPE changes parameter type; no preset compatibility is required.
- Source integrity, range, and waveform checks completed; Windows VST3 build and DAW audio tests remain unverified.

## 1.23.3 — Seven-knob Chorus layout and custom logo support — 18 November 2026

- Replace the Chorus header LFO dropdown with a seventh full-size WAVE knob, matching Doubler's size and equal 58 px spacing. Preserve all four 82 px panels and existing effect knob fonts; restore Chorus's full-width header sweep guide.
- Keep the seven DSP LFO shapes, 0.01–10 Hz RATE, voice-count improvements, host automation and sequencer target 45 from v1.23.2. Abbreviate the knob's choice readouts so values fit without shrinking fonts.
- Prefer an embedded `Resources/logo.png` as the Vortex GUI logo when supplied locally. Fall back to the existing `Resources/logo.svg` if PNG is absent. Keep the logo zoom hotspot and surrounding layout unchanged.
- No changes to active-effect DSP, sequencer routing, gate, other three effect sections or VST3 identity. The user's custom PNG was not part of the uploaded ZIP, so the image must be present locally before building.
- Source/static and ZIP tests completed; Windows VST3 compilation and DAW visuals/audio remain unverified.

## 1.23.2 — Chorus LFO shapes and stronger voice differentiation — 18 November 2026

- Add a compact seven-shape Chorus LFO selector (Sine, Triangle, Sawtooth, Ramp, Exponential, Inverse Exponential and Random) to the existing 82 px panel header. The red Chorus sweep guide is shortened; all six effect knobs and all four panel heights are untouched.
- Retain existing Chorus RATE (0.01–10 Hz) as the LFO speed control, rather than duplicate it.
- Add host-automatable `chorusLfoShape`, with 60 ms shape morphing, a smoothed transition for discontinuous waveforms and independently evolving deterministic Random waveforms per voice.
- Increase Chorus voice delay separation from 4 ms to 12 ms and introduce voice-dependent L/R placement so VOICES 1–8 changes ensemble density and stereo character more noticeably. Maintain fade-smoothed voice-count changes.
- Append `Cho LFO Shape` as sequencer target 45, retaining all 44 existing target assignments and all 16 target slots. Bump preset schema to 3 (pre-release migration not required).
- No changes to Phaser, Flanger, Doubler audio engines, gate routing or GUI panel sizing. Static checks performed; Windows VST3 compilation and actual audio/visual validation outstanding.

## 1.23.1 — First-release version and portable data folder — 18 November 2026

- Set the plugin version to `1.23.1` consistently in CMake/JUCE metadata, the GUI, build script and documentation.
- Rename the portable runtime data root beside the VST3 from `Vortex` to `Vortex_Data`; Settings, Presets and Sequences stay under this new root.
- Do not import or migrate pre-release `Vortex` data. The plugin identity (`sl23` / `vtx1` / `com.sl23.vortex`), DSP, GUI, sequencer and audio routing remain unchanged.
- Source consistency and ZIP integrity checked; Windows compilation and DAW validation pending.

## 0.1.23 — Fix v0.1.22 Windows compilation error — 18 November 2026

- Move the existing eight-voice Doubler `voicePitchSpeed` constant array to a shared scope so both its audio rendering and LFO phase-advance code can reference it.
- Fix Visual Studio error C2065 at PluginProcessor.cpp line 1651 (and its dependent C2064 error).
- Retain all v0.1.22 CPU gating behaviour, DSP values, effect controls, GUI alignment, and sequencer without intentional functional changes.
- Static source checks passed; Windows compilation and DAW tests pending.

## 0.1.22 — Centred version label and bypass-aware CPU processing — 18 November 2026

- Centre the displayed version text horizontally over the existing 30px master bypass/power button; do not move the button or the main layout.
- Skip Chorus voice reads, Phaser all-pass stages, Flanger wet/filter processing, and Doubler pitch/timing voice reads once the respective effect's smoothed enable gain reaches silence.
- When the whole plugin is fully bypassed, suspend the same heavy engine processing while still keeping parameter smoothers and sequencer timing current.
- Keep chorus, flanger and doubler input delay rings updated at low cost while effects are off, so re-enabling does not retrieve stale input. Clear small Phaser and Flanger feedback/filter states once after disabling; retain existing effect-enable fades.
- Leave DSP calculations for active effects, gate mode routing, sequencer assignments, 82px panels and all effect controls unchanged. During off periods, modulation phases pause and resume on enable; disabled effect tails are not calculated once fully faded out.
- Source/static/ZIP checks only; Windows compilation and DAW audio/CPU verification pending.


## 0.1.21 — Make Flanger controls audibly effective — 18 November 2026

- Eliminate the Flanger's hidden 50% internal dry blend. The master MIX alone controls dry/wet, so OFFSET, PHASE, SHAPE and FEEDBK have a stronger effect. With a single Flanger enabled, MIX around 50% is the recommended test point.
- Extend Flanger FEEDBK from -85..+85% to -100..+100%, using a stronger, sign-preserving perceptual response below 60%. Internal gain stays bounded at 0.96, below unity, to avoid uncontrolled regeneration.
- Extend SHAPE from sine -> triangle -> smoothly rounded-square modulation, retaining sine at 0% and avoiding a hard LFO step.
- Replace the subtle, fixed 1.7 kHz tone blend with a continuously adjusted two-pole low-pass, from 160 Hz at 0% to unfiltered at 100%. Tone also colours the feedback path; smooth existing per-sample parameter interpolation is retained.
- Replace non-ASCII symbols in the editor's tooltips/readouts/help messages with portable plain text; enable MSVC /utf-8 to prevent Windows codepage-dependent display corruption.
- Keep the four 82px panels, every knob/LED position, sequencer target IDs and all other engines unchanged. The Flanger feedback range and response and Flanger sound behaviour intentionally change before release.
- Static and isolated mathematical checks only; Windows compilation and DAW audio evaluation outstanding.


## 0.1.20 — Restore Phaser and Flanger controls — 18 November 2026

- Corrected the editor effect dial array from 20 to 24 entries so four added Flanger controls no longer overwrite the adjacent Phaser dial pointers.
- Added compile-time consistency assertions for effect-control IDs, captions, help descriptions and dial capacity to prevent this regression.
- Restore six Phaser controls (RATE, DEPTH, STAGES, FREQ, FEEDBK, SPREAD) in Phaser, and seven Flanger controls (RATE, DEPTH, FEEDBK, PHASE, OFFSET, SHAPE, TONE) in Flanger.
- All four effect panels remain 82px; no DSP, parameter schema, sequencer target, or audio routing changes.
- Static source checks passed; Windows VST3 compilation and DAW visual/audio tests pending.


## 0.1.19 — Two-line contextual tooltips — 18 November 2026

- Restrict the bottom contextual-help strip to exactly two text rows, even when a control description is long; prevent extra lines from clipping vertically.
- Shorten the longer FX help descriptions while retaining control ranges and meaning, without reducing label/value font sizes.
- Preserve all other GUI geometry, four 82-pixel effect panels, sequencer and DSP behaviour.
- Static source checks only; Windows VST3 compilation and DAW visual validation outstanding.

## 0.1.18 — Aligned header controls and expanded Flanger — 18 November 2026

- Widened the four top LEVEL labels while retaining font size and 75-px spacing; draw the `::` target marker with four vector dots to avoid clipping.
- Match each top LEVEL label's normal colour to its respective effect header (Chorus cyan, Phaser lilac, Doubler green, Flanger orange). Existing sequencer-assignment highlighting still takes priority.
- Centred the six Chorus knobs horizontally with equal 68-px spacing; all four panels remain exactly 82 px tall.
- Restricted the logo-click zoom area to the visible VORTEX wordmark, approximately 170 px instead of 280 px.
- Added four Flanger controls: PHASE 0–180 degrees, OFFSET 0–12 ms, SHAPE 0–100% sine-to-triangle blend and TONE 0–100% dark-to-bright wet/feedback damping.
- Updated the Flanger modulation guide and delay-line allocation for the extended offset range; defaults reproduce the previous Flanger character.
- Appended sequencer targets 41–44 for the new Flanger controls, preserving the previous 40 assignments and all 16 target slots.
- No intentional changes to Chorus, Phaser, Doubler or global mixing DSP. Source checks only; Windows VST3 build and DAW listening remain to be done.


## 0.1.17 — Correct gate routing and add four effect LEVEL trims — 18 November 2026

- Gate OFF leaves both dry and processed audio alone; INPUT gates the original dry path only (effect feeds and tails remain live), EFFECT gates only the complete wet bus, OUTPUT gates the final dry/wet signal.
- Prevent dry fallback leakage when the last effect is bypassed and the gate is in INPUT mode.
- Add CHORUS, PHASER, DOUBLER and FLANGER top-row LEVEL knobs, matching THEME knob dimensions (30×30) and label styling, between logo and preset selector. Controls remain visible in preset-browser mode.
- All new levels are wet-only, smoothly adjusted 0–200% with 100% default/unity, and appended to sequencer targets without disturbing existing assignments.
- Levels are editable but dimmed when their own engine is bypassed; four 82-pixel main effect panels remain unchanged.
- No change to Chorus, Phaser, Doubler or Flanger processing algorithms.
- Source/static checks only; Windows compile and DAW audio/GUI verification still required.


## 0.1.16 — Editable dimmed controls during effect bypass — 17 November 2026

- Keep knobs and parameter-drag labels fully interactive while their effect's amber LED is off; apply 58% alpha to the existing knob/label/value colours instead of disabling components.
- With an effect bypassed, display its actual adjustable parameter positions rather than sequencer-animation overrides; preserve assignments and restore animation when re-enabled.
- The inactive effect's header guide remains a static grey track; no change to its existing bypass/audio routing.
- Preserve all four 82 px panels, controls, text sizes, LED and guide positions, DSP and sequencer logic.
- Source checks completed; Windows VST3 compilation and DAW performance tests remain outstanding.

## 0.1.15 — Greyed-out bypassed FX controls and inactive guides — 17 November 2026

- Bypassing Chorus, Phaser, Flanger or Doubler via its amber LED greys out and disables that engine's knobs, labels and value readouts; re-enabling immediately restores them.
- An inactive effect's header guide shows only its grey track (no red sweep range or moving markers). Live movement resumes upon re-enabling.
- Appearance follows the four APVTS enable parameters at 30 Hz, including changes from preset loading and host automation.
- Clear sequencer-animation visual overrides for controls that are disabled, without changing sequencer target assignments or audio processing.
- No changes to panel geometry (four 82 px panels), knob/LED positions, parameter schema, DSP, routing, sequencer or plug-in identity.
- Source checks completed; Windows VST3 build and DAW visual/automation tests remain outstanding.

## 0.1.14 — Inline live guides for all four effects — 17 November 2026

- Added compact red swept-range guides alongside the CHORUS, DOUBLER and FLANGER headings, using the same style as the Phaser guide.
- Chorus shows the current stereo voice-1 delay taps and possible active-voice delay range (ms); Flanger shows live L/R tap delays and its modulation range (ms).
- Doubler shows the first two voice delays and total active read-head timing spread (ms), with a low-delay-friendly logarithmic scale.
- Displays read atomic snapshots of smoothed audio-DSP values once per block. No GUI-only oscillator, new plug-in parameters or audio algorithm changes.
- Kept all four effect panels exactly 82 px high, all knob/LED/label/value dimensions and positions unchanged, as well as sequencer, routing and plugin identity.
- Repaint only the two title-row strips at 30 Hz; no expanded panel or new monitor row.
- Windows build and DAW visual tests remain outstanding.

## 0.1.13 — Equal-height effect panels and inline Phaser frequency guide — 17 November 2026

- Restored identical 82 px heights for Chorus, Phaser, Doubler and Flanger panels, with both rows aligned. No additional top-panel height or other interface expansion.
- Moved the red Phaser frequency-range guide into the same title row as PHASER and shortened the guide to fit. L/R frequency markers and live DSP-derived range calculations are unchanged.
- Aligned all six Phaser knobs with the Chorus knob row, keeping the established identical knob sizes, label sizes and value sizes. Aligned the Doubler and Flanger rows.
- Shifted the four amber effect on/off LEDs left by 6 px, setting a matching 3 px top/left inset in each panel without resizing the LEDs.
- Preserved all DSP, parameters, 32-step sequencer, routing, presets, plugin identity and existing 0.1.12 C4458 warning fix.
- Source layout and archive checks passed; Windows build and DAW visual validation remain outstanding.

## 0.1.12 — Simplified Phaser layout and permanent parallel routing — 17 November 2026

- Removed Motion Map completely, including its XY and enable parameters, LED, GUI component, automated routing, and sequencer targets.
- Retained the existing equal-weight, enabled-engine parallel wet bus as the permanent routing mode; master MIX and OUT remain unchanged.
- Restored separate Chorus/Doubler and Phaser/Flanger rows. Expanded the right-hand panels into the former Map area so all six Phaser knobs fit on one row at unchanged font and knob size.
- Moved the live red Phaser frequency-range monitor above its six knobs; removed the MORE/BACK controls and the Flanger-overlapping expansion mode.
- Fixed MSVC C4458 in PluginEditor.cpp by renaming the local `constrainer` variable to `editorConstrainer`.
- Preserved the 32-step sequencer and 16 assignable target slots. The target list is now 36 entries (two retired Map choices removed); pre-release preset/host-state compatibility is intentionally not guaranteed. Schema version advanced to 2.
- Windows/DAW compilation and visual/audio validation outstanding.

## 0.1.11 — Phaser character and live LFO-frequency guide — 17 November 2026

- Expanded Phaser controls: Centre Frequency (75 Hz–7.5 kHz), signed Feedback (−90–90%) and Stereo LFO Phase (0–180°), with sample-smoothed changes.
- Reworked the phaser's all-pass pole spacing and selectable 2/4/6/8/10/12 stages to produce a greater range of subtle and resonant notches.
- Removed the internal 50% dry blend from the phaser engine; Vortex's global MIX now performs dry/wet mixing once, producing deeper notches at around 50% MIX.
- Phaser shows four equal-sized knobs (Rate, Depth, Stages, Centre Frequency) plus a red frequency-range bar with moving stereo markers. MORE/BACK reveals Feedback and Stereo Phase without shrinking any knobs; it temporarily covers Flanger controls, while Flanger audio stays active.
- Added a 30 Hz red range guide showing the DSP-calculated current frequency limits and moving L/R sweep positions, including sequencer-modulated controls.
- Added the new Phaser controls to the existing sequencer target menu without changing the 32-step/16-slot system.
- Adjusted version/build labels; pre-release parameter compatibility is not required.
- Static source and DSP mathematical checks performed; not compiled or auditioned as Windows VST3 in this environment.

## 0.1.10 — Doubler wet-output balance — 17 November 2026

- Increased only the Doubler's processed (wet) output by +2.5 dB (1.3335x), equally in mono and stereo.
- The adjustment is after voice summing and before both Motion Map and manual parallel routing, so it applies consistently in either mode.
- No changes to dry volume, MIX, OUT, other DSP engines, knob controls, parameter IDs, sequencer, GUI layout, or the v0.1.9 stronger Chorus feedback.
- Source-level checks and ZIP integrity passed; Windows/DAW compilation and audible level/clipping checks remain outstanding.

## 0.1.9 — Stronger Chorus Feedback — 17 November 2026

- Strengthened the Chorus Feedback response across its existing 0–100% control while retaining a below-unity maximum coefficient.
- Replaced the fully averaged multi-voice feedback source with a stable blend of 75% dedicated primary delay tap and 25% normalised multi-voice output. Adding Voices no longer progressively weakens regeneration.
- Soft-bounded the signal stored in the feedback path, allowing more obvious resonant and watery chorus effects without uncontrolled level growth.
- Kept the existing parameter ID, range, default, sequencer target, layout and state schema unchanged.
- Corrected the visible GUI version text from the stale v0.1.7 label to v0.1.9.

## 0.1.8 — Fix frozen target knob graphics during bypass — 17 November 2026

- While Vortex is bypassed, sequencer-assigned knob graphics display their actual host parameter values rather than cached animated positions.
- On re-enabling Vortex, sequencer animation resumes without changing target assignments, modulation, DSP, presets, or control layouts.
- Applies to main controls and sequencer-panel targets through the shared animated-value accessor.
- Source inspection and archive checks passed; Windows/DAW validation still required.

## 0.1.7 — Yellow knob indicators — 17 November 2026

- Changed the shared knob-indicator colour to the same muted yellow (`#D2B84F`) used by Afterglow and the supplied Dusk template.
- HUE, SAT and LUM sequencer knobs retain their dynamically selected HSL accent colours.
- No DSP, parameter, layout, sequencer, preset, logo or plugin-identity changes.
- Windows/DAW build not verified in this environment.

## 0.1.6 — Doubler detune and effect-panel layout — 16 November 2026

- Replaced Doubler's very subtle detune-only delay wobble with independent, paired positive/negative pitch offsets up to approximately +/-36 cents, using two 220 ms overlap-add read heads per voice. Detune works independently of Pitch Depth/Rate and is smoothed with the existing Detune parameter.
- Reordered Doubler's adjacent pitch controls to P RATE before P DEPTH; no change to their parameter IDs or sequencer target assignments.
- Swapped the entire Doubler and Flanger panel locations. The left column is wider, with Chorus above Doubler; the right column is narrower, with Phaser above Flanger.
- Replaced the Doubler Voices dropdown with a full-size seventh knob. All effect labels, knob sizes and numeric readout sizes remain identical; the Voices control remains bound to the same host/sequencer parameter.
- No intentional changes to Chorus, Phaser, Flanger DSP, Motion Map, gate sequencer, 32-step sequencer or preset browser.
- Pre-release: existing preset compatibility is not required. Windows/DAW compilation and audible artifact checks are still outstanding.

# Vortex — Changelog


## 0.1.5 — Doubler engine overhaul — 16 November 2026

- Replaced the former two-tap Doubler with eight independently modulated voices and actual stereo panning.
- Added a 1-to-8 Voices selector in the Doubler header, keeping all six knob labels and value sizes uniform.
- Extended Pitch Depth to 0-250%, Pitch Rate to 0-100% and Timing to 0-800 ms. Pitch Depth maps to approximately +/-120 cents at maximum in Vortex, not the undisclosed AIR algorithm.
- Removed the 8 ms pitch-sweep limiter that weakened the old algorithm, enlarged delay buffers and added crossfaded Timing/Tightness delay-head changes.
- Smoothed changes to the number of active voices; added voice-count sequencing.
- Four engines, Motion Map, zoom and 32-step sequencer otherwise unchanged.
- Windows/DAW compilation, audibility and click-freedom have not been verified.

# Vortex — Changelog


## 0.1.4 — Chorus Feedback and Pre-Delay — 16 November 2026

- Changed Chorus Feedback to unipolar 0–100% (default 0%), including parameter range, modulation and randomisation.
- Normalised the Chorus feedback signal by the sum of active voice gains and limited its internal coefficient to 0.95 at the 100% knob position.
- Added Chorus Pre-Delay from 0 to 50 ms (default 18 ms), smoothed per sample and assignable to the existing sequencer slots.
- Added a sixth Chorus knob without changing the font size, readout size, panel dimensions or the other effects.
- Ensured Chorus tap times remain positive, including when Pre-Delay is 0 ms and Depth is high.
- No further tuning of the Chorus character or other DSP engines in this revision.
- Pre-release parameters may change; preset compatibility is not guaranteed.

## 0.1.3 — Chorus character and consistent typography — 16 November 2026

- Fixed identical font size, knob geometry, and value textbox sizes across all 17 engine knobs. Added compact `ct` readout for Doubler Pitch Depth rather than shrinking its font.
- Added amber Motion Map On/Off LED; ON uses XY blending; OFF gives equal-weight parallel blending of enabled effects. Mode switch is sample-smoothed. The Map becomes non-interactive while disabled.
- Rebuilt Chorus modulation with independently drifting voice clocks, differing delay centres, soft waveform asymmetry, stereo phase offsets, and energy-aware wet gain. Removed the fixed coherent voice sweep that produced robotic ensemble movement.
- Chorus Feedback now defaults to 0%, suitable for comparing with the provided AIR Chorus reference.
- Kept all four FX LED switches, sequencer and logo zoom menu intact.
- Pre-release: preset / host automation compatibility is not a release requirement; no preset compatibility is claimed.

# Vortex Changelog

## 0.1.2 — Dusk-style zoom, effect LEDs and audio adjustment smoothing — 16 November 2026

- Matched Dusk's dark, headed GUI Zoom popup (75%-300% in 25% increments).
- Added four amber effect on/off LEDs with smooth dry-signal crossfading.
- Added Chorus Feedback, Width and Voices up to 8; Chorus Rate now 0.01-10 Hz and Depth 0-24 ms.
- Added Doubler Pitch Depth (cents), Pitch Rate (Hz) and Timing (ms).
- Smoothed all effect control changes per audio sample to reduce zipper/click artifacts, notably Doubler Tightness and Detune.
- Smoothed the Chorus voice activation and Phaser stage selection instead of discontinuously reconfiguring active voices/stages.
- Preserved original host parameter order and all original 24 sequencer target indices; new parameters and targets were appended.
- **Compatibility note:** Chorus Depth retains its parameter ID but now uses milliseconds instead of percent; legacy settings must be checked.
- **Validation:** Source consistency reviewed; Windows build and live DAW listening not yet completed.

## 0.1.1 — Zoom popup cleanup — 16 November 2026

- Simplified the logo-triggered native Windows menu to percentage-only entries (75–300%), with the selected level checked.
- Kept the existing zoom and pixel-size feedback outside the popup, including tooltip and manual resize information.
- No changes to DSP, sequencer, parameter identities, plugin IDs or saved data formats.

## 0.1.0 — Initial source preview — 16 November 2026

- Forked the uploaded Dusk v0.1.1 source, preserving the 32-step sequencer/gate sequencer and 16 target slots.
- Isolated product identifiers, settings/preset/sequence storage, data formats and plugin bundle identity under Vortex / `sl23`.
- Removed the Dusk distortion bank and placeholder dual-filter model, replacing them with Chorus, Phaser, Flanger and Doubler engines.
- Added a four-corner, draggable Motion Map with bilinear blending between the four effect engines.
- Arranged all twelve effect knobs on the left and Motion Map directly before MIX and OUT on the right.
- Added a native Windows logo-triggered zoom menu from 75% to 300%, displaying percentage and current window dimensions.
- Retained the existing expandable sequencer interface, data routines and drag-and-drop target assignment.
- This source package is **not yet compile-tested or host-tested**.
