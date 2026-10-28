# What's different in this custom build

This is an unofficial fork of the [TONE3000 plugin](https://github.com/tone-3000/tone3000-plugin),
based on the official **v0.0.12**. Everything the official plugin does still
works the same; the changes below are additions on top of it.

Downloads (Windows, macOS, Linux) are on the
[Releases page](https://github.com/NovodyInParticular/tone3000-plugin/releases).
The source is on the [`custom`](https://github.com/NovodyInParticular/tone3000-plugin/tree/custom) branch.

## Finding and switching tones

- **Previous / next tone on every block.** Each block remembers the search its
  tone came from; step through the results without reopening the browser.
  The tone browser also stays open while you step.
- **Faster stepping.** Downloaded models are cached on disk, and the next and
  previous tones are fetched ahead of time.
- **Crossfaded swaps.** Changing a model crossfades instead of dipping to
  silence.
- **MIDI next / previous tone per block**, for footswitches.
- **A2-Lite suggestion.** When the CPU load stays high, the plugin offers to
  switch the heavy (Full) models to their Lite versions, and back.

## New sound features

- **Clouds.** The granular texture processor from Mutable Instruments' Clouds
  module, running the module's own open-source DSP (MIT, by Emilie Gillet).
  It takes the faceplate spot the Bass / Middle / Treble knobs had: Density,
  Texture and Blend plus a power switch, and a right-click panel with Freeze,
  Mode (Granular / Stretch / Looping Delay / Spectral), Quality, Position,
  Size, Pitch, Spread, Feedback and Reverb. No latency on the dry signal.
  Off by default.
- **Glitch Engine.** A digital glitch insert, shown as its own card in the rig
  view (click it for the controls, drag it before or after the amps):
  tempo-synced stutter (1/4 to 1/64 and triplets, with a repeat length),
  random glitches (stutters, reverses, dropouts and octave jumps) with a
  chance, an intensity and a seed that replays the same pattern at the same
  song position, freeze with a fade, bit-crush (16 to 1 bit) and sample-rate
  reduction, mix and output. Stutter and Freeze latch or hold, on the card
  and over MIDI ("Stutter (hold)", "Freeze (hold)" targets for momentary
  footswitches). Glitches can never come out much louder than what you
  played. No latency. Off by default. The standalone app has its own tempo
  (in the Glitch controls) for everything tempo-synced.
- **Abyss Pitch.** A layered pitch effect with its own rig-view card: -1 and
  -2 octave and +1 octave layers with their own levels, a shift layer (free
  ±12 semitones or a dissonant interval: minor 2nd, tritone, major 7th,
  minor 9th), detune from a shimmer to a wide chorus, a pitch bend (±12 or
  ±24, with glide; map it to an expression pedal), dry, wet and output.
  Tracking: Tight (11 ms latency, best for chords and feel) or Smooth
  (21 ms). It uses the same low-latency engine as the Pitch knob, and the
  latency is reported to the host only while it is on. Before the amps by
  default (octaves into the amp); drag the card after the cab for octaves
  on the amp's sound. When both inserts share a side, drag them past each
  other to change which runs first. Off by default.
- **Time FX rack** after the cab: chorus / flanger, delay (tempo sync,
  ping-pong, damping) and reverb, in any of six orders. Effects ring out
  when switched off.
- **Dual cab IR.** Blend two cabinet IRs inside one IR block, with blend,
  time alignment, per-IR level and polarity.
- **Guided input calibration.** Play for a few seconds and the plugin sets
  the input level for your interface.
- **Movable tone stack bands.** The tone stack's bass, mid and treble
  frequencies are adjustable (40-400 Hz, 150-2500 Hz, 800-8000 Hz).

## Rig sharing

- **Rig files and rig codes.** Save a whole rig (chain, tones, faceplate and
  effects settings) as a file, or copy a short code someone can paste into
  their own plugin.

## Rig view

A second way to see the chain: as a row of gear instead of tone artwork.

- Live **output spectrum** with Hz / dB labels, and a faint trace of the dry
  input underneath it.
- The **tone stack's EQ curve** drawn over the spectrum: drag a handle up or
  down for gain, sideways for frequency. This is now where the tone stack is
  edited (its faceplate knobs became Clouds).
- The amp shows its **real waveform**, and pulses run along the cables.
- A live **Time FX unit** (click it to open the rack), a cab that **splits in
  two** when two IRs are blended, and the **tuner** in the analyser's place.
- **Drag to reorder** the gear, adjust module knobs directly, and step tones
  from the modules.
- **CPU meter** and FULL / LITE badges; bypassed modules look switched off,
  with a footswitch click.
- Long tone names scroll on hover.

## Look and feel

- **Skins:** Neural, Minimal, Vintage, Classic and Burning Church built in,
  plus your own skins from a folder (see [SKINS.md](SKINS.md)). Skins can
  restyle the TONE3000 wordmark and colour the level meters.
- **Knob values on hover**, and double-click a knob to reset it. Typing in a
  value moved to double-clicking the knob's *name*.
- **Reduce animations** setting for slower machines.
- Popups (like the Time FX rack) stay inside the window.

## Fixes

- **Security:** your TONE3000 login token is only ever sent to TONE3000's
  own model servers, never to another site a preset's model link points at.
- A rig module's name tooltip refreshes when its tone steps.

## Notes

- The installers are built in GitHub Actions and are **not code-signed**:
  Windows SmartScreen and macOS Gatekeeper will warn the first time.
- Presets and sessions from the official plugin load unchanged. New features
  are off by default.
- Not affiliated with or supported by TONE3000, so please don't send
  TONE3000 support requests about this build.

## Ether skin

A psychedelic built-in skin (Settings → Plugin → Skin → Ether): a 60s/70s
liquid light show over deep cosmic indigo. Drifting oil-projector colour and
a starfield behind everything, liquid cards with swirling rainbow borders,
lava-lamp footswitches, a mandala behind the amp's name and its waveform as
a rainbow ribbon, cabs as turning kaleidoscopes (two melting into each other
with a dual IR), the Time FX unit as ripples, a nebula and a rainbow spiral,
the Glitch Engine with an RGB split and pixel melt when glitches fire and a
crystal freeze, Abyss Pitch as a vortex, rainbow knobs, glowing pill buttons,
a rainbow liquid spectrum and flowing rainbow cables. The music makes it
breathe. Titles in Shrikhand, text in Varela Round (both open-licensed, and
bundled).

**Trip Intensity** (Settings, under the skin) sets how much it moves, from
fully still to full trip. It goes static with Reduce animations, with the
system's reduced-motion setting, or when frames get slow; nothing flashes
more than 3 times a second.

