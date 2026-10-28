# Skins

Skins change how TONE3000 looks, never what it does: every skin shares one
layout, the same controls and the same sizes. Switching is instant
(**Settings → Plugin → Skin**), doesn't interrupt audio or reload models, and
is remembered for every instance on the machine.

## Where skins live

Each skin is a folder holding a `skin.json` manifest:

| OS      | Folder                                              |
|---------|-----------------------------------------------------|
| Linux   | `$XDG_CONFIG_HOME/tone3000/skins` (usually `~/.config/tone3000/skins`) |
| macOS   | `~/Library/Application Support/TONE3000/Skins`      |
| Windows | `%APPDATA%\TONE3000\Skins`                          |

**Settings → Plugin → Skin → Open skins folder** creates and opens it. The
folder name is the skin's id. Reopen Settings to see a new skin in the list.
A folder named like a built-in (`neural`, `minimal`, `vintage`, `classic`,
`burningchurch`, `ether`) is ignored.

The built-ins are in `plugin/ui/skins/` and are good starting points: copy
one, rename the folder, edit.

## skin.json

```json
{
  "name": "Night Shift",
  "author": "you",
  "version": "1.0",
  "description": "One line shown nowhere yet, but keep it.",
  "colors": {
    "accent": "#00F0FF",
    "text": "#E6FFFC"
  },
  "style": {
    "knob": "ring",
    "glow": 0.5
  },
  "backgroundImage": "bg.svg"
}
```

Everything is optional except `name`. Anything you leave out, misspell or
give a bad value keeps the **Classic** value, so a half-finished skin still
works. A manifest that isn't valid JSON (or is over 64 KB) isn't listed;
if it was the active skin, the default (Neural) is used instead.

### colors

`"#RRGGBB"` or `"#RRGGBBAA"` (alpha last).

| Token            | Used for |
|------------------|----------|
| `accent`         | Knob value arcs, armed buttons (DLY, SYNC, LITE…), highlights |
| `text`           | Primary text and icons |
| `black`          | Page background; text on accent fills |
| `muted`          | Secondary text |
| `subtle`         | Tertiary text, captions |
| `gray`           | Idle labels and icons |
| `highlight`      | Hover and pressed fills |
| `border`         | Every hairline: cards, panels, buttons |
| `surface`        | Cards and tiles |
| `surfaceRaised`  | The faceplate and raised chrome |
| `panelBg`        | Popovers, menus, the FX rack |
| `segmentedTrack` | Behind segmented switches |
| `link`           | Links in Settings |
| `brandRed`, `brandBlue` | Meter ends and warnings |
| `meterLow`, `meterMid`, `meterHigh` | Level meter dots and the gallery glow, quiet → hot (default blue → yellow → red) |

### style

| Key            | Values | Effect |
|----------------|--------|--------|
| `knob`         | `"classic"` / `"ring"` | Machined knob, or a flat disc with a glowing ring |
| `knobFace`     | colour | Ring-knob body |
| `glow`         | 0–1    | Halo strength on arcs and lit parts (drawn as strokes, no blur) |
| `scanlines`    | 0–1    | Horizontal line texture behind the chain |
| `grid`         | 0–1    | Grid texture behind the chain |
| `background`   | colour | Behind the chain (defaults to `black`) |
| `monoLabels`   | bool   | Knob and section labels in the monospace face |
| `animations`   | bool   | Rig-view pulses (off: everything static) |
| `glitchOnLoad` | bool   | A short flicker on a module when a new tone lands |
| `grille`       | `"photo"` / `"dots"` / `"hex"` | How the rig view draws a cab |
| `psychedelic`  | bool   | The Ether light show: drifting oil-blob backdrop, liquid cards, rainbow knobs and cables, kaleidoscope cabs. Adds a **Trip Intensity** setting |
| `titleFont`    | `"display"` | Headings and slot titles in the bundled retro display face (Shrikhand) |
| `textFont`     | `"round"`   | Body text and labels in the bundled rounded face (Varela Round) |

### The Ether skin and Trip Intensity

The built-in **Ether** skin turns all three on. Its motion follows
**Settings → Plugin → Skin → Trip Intensity** (Still, Low, Moderate, High,
Full trip; Moderate by default). It goes fully static with Trip Intensity
at Still, with **Reduce animations** on, when the operating system asks for
reduced motion (macOS Accessibility "Reduce motion", Windows "Show
animations" off, GNOME / GTK animations off), and on its own if the UI's
frames take too long (then until the skin is re-applied or the editor
reopens). Nothing in it flashes more than 3 times a second; colours drift
slowly (one turn of the palette a minute). The backdrop moves at 10 frames
a second; it pauses while the window is hidden or minimised.

### logo

How the header draws the TONE3000 wordmark. Leave it out to keep the
original artwork.

| Key     | Values | Effect |
|---------|--------|--------|
| `style` | `"brand"` / `"fill"` / `"outline"` | Original artwork, the letters in one colour, or outlined letters |
| `color` | colour | The letters (defaults to `accent`) |
| `echo`  | colour | The offset slices behind the letters (left out: no slices) |
| `glow`  | 0–1    | Halo around an outline |

```json
"logo": { "style": "outline", "color": "#00F0FF", "echo": "#FF2E8859", "glow": 0.8 }
```

### backgroundImage

An optional SVG file inside the skin folder (plain file name, no paths,
256 KB max), stretched behind the chain under the textures.

## Rules for shared skins

- Original artwork only: no manufacturers' logos or lookalike faceplates.
- Keep it light: vectors and colours, no large images.
- Keep text readable: check `muted` and `subtle` against `surface` and
  `panelBg`.
- Effects are cheap by design, but `animations: false` is the lowest-CPU
  choice; the Minimal skin does that.

Rig files and rig codes carry the skin's id. When someone loads your rig and
has a skin with that id, it switches to it.
