# Design Brief — SeisMonitor Screen 5: the Alert ("SEISMIC ACTIVITY DETECTED")

**This is a brief _for_ Claude Design, not a spec _from_ it.** It's the companion to the existing
`design_handoff_landscape_ui/` bundle, which designed Screens 1–4 (NZ monitor, Global globe, Settings,
Loading) and is **the design system of record** — same device, same 320×240 canvas, same Soft HUD palette.
Read that bundle's `README.md` first for the house style; this brief only covers what's new.

**Screen 5 is the one screen that never got designed.** It was built ad-hoc in firmware and it shows —
it's the weak link in an otherwise coherent product.

---

## 1. The device (why the constraints below are real)

ESP32-S3, QDTFT **ES3C28P** board, 2.8" IPS **ILI9341V**, run **landscape 320×240**, rotated 180°
(mounted upside-down in its enclosure). FT6336G capacitive touch. Firmware is C++ / Arduino / **TFT_eSPI**,
in `src/main.cpp`. The 320×240 design space maps **1:1** to the framebuffer.

Everything is **drawn with primitives at runtime**. There is no compositor, no image decoder, no CSS.

---

## 2. What this screen is for

It takes over the whole display when a **new earthquake arrives** that meets the user's sensitivity
threshold. It holds for **25 seconds** (`ALERT_DURATION`), then returns to the main monitor screen.

**The single most important thing to understand:** the alert threshold is user-set and **defaults to M2.0**.
Most alerts are therefore **small quakes nobody felt** — an M2.4 somewhere off the coast. This screen is
*not* an emergency warning. It is a **"something just happened" notification**, and it fires several times
a day.

So it has to work across a huge range without crying wolf:
- **M2.4** — routine. Should feel like a pulse of life from the instrument, not a siren.
- **M5.5** — genuinely notable. Should command attention.
- **M7.1** — rare and serious. Should be unmistakable.

That's why the header reads **"SEISMIC ACTIVITY DETECTED"** and not "EARTHQUAKE" or "ALERT". Keep that
wording exactly — it's deliberate and the owner specified it. **Severity is carried by colour** (see §7),
not by shouting.

---

## 3. What the owner asked for (verbatim)

> "the eq detected screen is really boring! it shuld be more distinct than the main screen, and shuld say
> activity detected, and just have the latest data - but showing now, with the concentric activity rings"

Unpacking that into requirements:

| Requirement | Meaning |
|---|---|
| **"more distinct than the main screen"** | This is the brief's core. It must not read as a quieter variant of the monitor. Different structure, different rhythm. The monitor is a calm three-panel instrument; this is a **single event, full-bleed**. |
| **"should say activity detected"** | Fixed copy: **`SEISMIC ACTIVITY DETECTED`** (confirmed by the owner as the final wording). 25 characters — **see the width warning in §5.2, this string does not fit the screen at large sizes.** |
| **"just have the latest data"** | **One** quake only. No 24H HIGH, no map, no seismograph, no panels. Resist the urge to add. |
| **"but showing NOW"** | Framed as *happening*, not logged. No "12M AGO" timestamp — this is the live moment. Currently rendered as `NOW · 29KM DEEP`; better ideas welcome. |
| **"with the concentric activity rings"** | Rings radiating outward, animated. Non-negotiable — it's the owner's central image for the screen. Make them the hero, not decoration. |

---

## 4. What exists now, and why it's boring

Current firmware (`displayEarthquakeAlert()`, `src/main.cpp` ~line 2861):

- `SEISMIC ACTIVITY DETECTED` in FreeSansBold9pt + 1px tracking, magnitude-coloured, centred at y=30,
  with an underline. (It's at 9pt and not the larger 12pt purely because it *doesn't fit* — see §5.2.)
- `M3.7` in FreeSansBold12pt, centred at (160, 130).
- Place name centred at y=214.
- `NOW · 29KM DEEP` in the tiny built-in font at y=230.
- 3 concentric rings animating outward between **r=36 and r=80** around (160, 124).

**Why it fails:**
1. **It's centred stack + centred stack + centred stack.** No structure, no tension, no hierarchy —
   just four things stacked down the middle of the screen.
2. **The magnitude is only 17px tall.** It's the hero by default rather than by design, and it isn't
   remotely big enough to carry the screen. (Reminder from §5.2: we can generate a font at *any* size.)
3. **The rings are trapped in a 44px-wide band** (r=36→80) because that's the only region that doesn't
   collide with text — see the erase problem in §5.4. They read as a spinning progress ring, not a shockwave.
4. Everything is the same magnitude-tinted colour, so the colour carries no information.
5. It uses the same fonts and centring as the main screen → it looks like the main screen with the panels
   deleted. Exactly what the owner is reacting to.

> **Note:** any photo you may have seen of this screen also showed the main UI painted over the top of it —
> that was a genuine firmware bug (the data/map panels repainted in the same loop iteration that raised the
> alert). **It's fixed.** Don't design around it.

---

## 5. Hard constraints — please design *within* these

This is the part that matters most. The previous handoff worked because it respected the panel. Effects
that are free in HTML are expensive or impossible here.

### 5.1 Colour
- **RGB565** — 16-bit, 65k colours. 5 bits red, 6 green, 5 blue. Fine gradients **band visibly**, especially
  in dark greens where we live. Expect it.
- **No alpha channel.** There's no framebuffer transparency. `alphaBlend(alpha, fg, bg)` can pre-compute a
  blended colour, but only against a **known flat background**. Over a flat backdrop (which this screen is)
  that works great — over the map it doesn't. **You may use opacity in the prototype**, as long as what's
  behind it is a single flat colour we can blend against.
- The background is near-black `#03080a`. Everything is additive light on black — think phosphor, not paper.

### 5.2 Type — **more flexible than you'd think, please exploit this**
Currently compiled in:

| Font | Cap height | Notes |
|---|---|---|
| `FreeSansBold12pt7b` | ~17px | the biggest thing we currently have — this is *small* for a hero |
| `FreeSans9pt7b` | 12px | place names, labels |
| `SeisSans10` | 10px | custom-generated |
| `SeisSans8` | 8px | custom-generated |
| built-in Font 1 | 5×7px | tiny, blocky |

**Critical: we have a working font generator.** `SeisSans10`/`SeisSans8` were rasterised from Arial into the
Adafruit GFX format by a script, and **we can generate any size or weight on demand** (ASCII 32–126, ~8–12KB
of flash each, and we have room). So:

> **Do not design to the sizes above. Ask for the size the design needs.** If the magnitude wants to be 48px,
> say 48px and we'll make it. This is a rare degree of freedom for this device — please use it. A genuinely
> large magnitude is probably the single easiest way to make this screen distinct from the main one.

Caveats: **ASCII only** (place names are already romanised); Māori macrons are drawn as a separate 1px bar
above the vowel, so a macron needs ~3px of clear space above the cap height. No italics. Letter-spacing is
whole-pixel only (we insert N px between glyphs — fine, we already do it, but it can't be fractional).

**⚠️ The header string is the tight one.** `SEISMIC ACTIVITY DETECTED` is 25 characters. Measured exactly
against the real glyph tables, on a **320px-wide** screen:

| Font | Width of the header string | |
|---|---|---|
| FreeSansBold12pt (cap 17) | **352px** | **does not fit — over by 32px** |
| FreeSansBold9pt (cap 12) | 272px | fits, 48px spare ← *what firmware uses today* |
| FreeSans9pt (cap 12) | 258px | fits |
| SeisSans10 (cap 10) | 203px | fits easily |

So **a large single-line header is not physically possible.** Your options, all fine by us:
- keep it small and quiet (current approach — lets the magnitude be the hero);
- **break it across two lines** — `SEISMIC ACTIVITY` / `DETECTED` measures 216px / 129px at the big 12pt,
  so a large stacked header *does* fit (but note it would then collide with the ring band — see §5.4);
- something else entirely. Just **be explicit about the intended size and line breaks**, and we'll generate
  the font to match.

### 5.3 Drawing primitives available
Cheap and available:
`drawPixel` · `drawLine` · `drawFastHLine/VLine` · `drawRect` / `fillRect` · `drawRoundRect` / `fillRoundRect` ·
`drawCircle` / `fillCircle` · `drawTriangle` / `fillTriangle` · `drawEllipse` / `fillEllipse`

Also available and **anti-aliased** (TFT_eSPI smooth primitives — they blend against a background colour you
pass in, so they're only smooth over a flat backdrop, which this screen has):
`drawSmoothCircle` · `drawSmoothArc` · `drawWideLine` · `drawWedgeLine`

> **`drawSmoothArc` is probably your best friend here.** It gives clean, anti-aliased, variable-thickness arcs
> and partial rings over a flat background. Arcs, sweeps, thick tapered rings, and segmented ring gaps are all
> on the table — they don't have to be 1px jagged circles like the current version.

Not available: blur, drop shadow, glow, gradient fills (only hand-stepped), bitmap images, blend modes, clipping
paths (there's a rectangular `setViewport` and nothing else), free rotation of text.

Fake a glow the way the rest of this product does: a second, dimmer 1px stroke offset outward. That's the
established house technique — see the seismograph trace in the existing bundle.

### 5.4 Animation — **the erase problem** (this shaped the current ugly design)
There's no compositing. To animate, we **redraw the moved thing in the background colour to erase it, then
redraw it at its new position.** Consequences:

- **Anything that moves across something static will erase that static thing.** That's exactly why the current
  rings are stuck in a 44px band — it's the only area with no text in it.
- **The escape hatches** (all already used elsewhere in this firmware, so all proven):
  1. **Re-stamp:** after erasing, redraw whatever static content that region contains. Used by the map ping.
  2. **`setViewport(x,y,w,h)`:** clip drawing to a rect so it can't touch the rest. Used by the map ping.
  3. **Draw order:** let the rings pass *under* text by redrawing the text on top each frame. Costs a redraw
     per frame but text is cheap.
  4. **Sprites (off-screen buffers):** render a region cleanly then push it in one go. **RAM-limited** — the
     globe already uses an 88KB sprite. A full-screen sprite would be 153KB and won't fit. A sprite up to
     roughly **120×120** is realistic here.
- **Budget:** SPI runs at 40MHz. A **full-screen redraw is ~30ms**, so ~15–20fps *if* we repaint everything —
  and it will tear. Partial redraws are much better. Target **~55ms/frame (18fps)** for the ring animation,
  which is what it runs at now and it's smooth enough.

> **So: rings crossing the whole screen are absolutely possible** — via re-stamp or draw-order — but say so
> explicitly in the spec so it gets built rather than value-engineered back into a safe little band.

### 5.5 Interaction
- The alert auto-dismisses after **25s**. Touch is currently ignored while it's up.
- If the design wants "tap to dismiss" or "tap to see it on the map", say so — both are easy.
- There's a seismograph-tap gesture in firmware that previews this screen on demand (for testing).

---

## 6. The data you have

From `EarthquakeData` — this is **everything** available. Please don't design fields we can't fill.

| Field | Type | Real examples | Notes |
|---|---|---|---|
| `magnitude` | float | `2.4`, `3.7`, `5.5`, `7.1` | always shown to 1dp, prefixed `M` |
| `location` | char[128] | `35km E of Cheviot`, `20km SW of Tokomaru Bay`, `Off E. Honshu, Japan` | **already abbreviated** by firmware. Typically 12–30 chars; can reach ~40. **This is the historic pain point** — long names must not shrink into illegibility. |
| `depth` | float | `29`, `12`, `38` | km. Integer-rendered. Range ~0–600. |
| `latitude` / `longitude` | float | `-42.8, 173.3` | available, unused on this screen (no map here) |
| `timestamp` | epoch secs | — | **deliberately not shown** — this screen says NOW |
| region | string | `NZ`, `Japan`, `California`, `China`, `Global` | data source is GeoNet (NZ) or USGS (others) |

Attribution (`POWERED BY GEONET` / `POWERED BY USGS`) appears on the main screen. **Say whether you want it
here** — the owner cares about attribution being present, but this screen is meant to be sparse.

---

## 7. Palette — Soft HUD (unchanged from the existing bundle)

Desaturated phosphor green on near-black. Full table in `design_handoff_landscape_ui/README.md`; the ones
that matter here:

| Token | Hex | Use |
|---|---|---|
| `bg` | `#03080a` | background |
| `ink` | `#b8e6c4` | primary readable text (place name) |
| `primary` | `#7fd69a` | muted phosphor |
| `secondary` | `#4a8a68` | quiet supporting green (meta) |
| `sub` | `#355a48` | deep muted (ticks, footnotes) |
| `rule` | `#0e1a16` | hairlines |

**Magnitude severity ramp** (`getMagnitudeColor()` — the alert currently tints header, magnitude and rings
with this):

| Magnitude | Hex | Firmware name |
|---|---|---|
| < 4.0 | `#7fd69a` | soft phosphor green |
| ≥ 4.0 | `#7bb6d6` | dusty blue |
| ≥ 5.0 | `#add2e6` | pale aqua |
| ≥ 6.0 | `#ce615a` | dusty coral |
| ≥ 7.0 | `#a54142` | deep burnt sienna |

> **Please critique this ramp — we think it's wrong.** It isn't monotonic in urgency: M5 (`#add2e6`, pale and
> bright) reads as *less* severe than M6 but *more* prominent than M4, and M7 (`#a54142`, dark) is actually
> **dimmer** than M6 on a black background — so the worst possible quake is the *quietest* pixel on screen.
> On an additive black display, brightness is urgency. A ramp that climbs in both saturation **and** luminance
> would serve this screen far better. We're happy to change these values — they're only used here and for the
> map markers.

---

## 8. Please design these states

The same screen must carry all of these. Renders for each, please:

1. **`M2.4` · `Porirua, New Zealand` · 12km** — the routine case. **Design this one first** — it's 90% of what
   the owner actually sees. If it feels like a false alarm, the screen has failed.
2. **`M5.5` · `35km E of Cheviot` · 29km** — notable.
3. **`M7.1` · `Off E. Honshu, Japan` · 38km** — the rare bad one. Show how far the visual language escalates.
4. **`M3.9` · `20km SW of Tokomaru Bay` · 45km** — the **long place name** stress test. This is the recurring
   failure mode of this product; if the layout only works with short names it will break in the field.

Optionally: what does this look like at the *moment* it arrives vs. 20s in? A one-off entrance beat (rings
firing once, hard) that settles into a calmer loop would be very welcome, and is cheap to build.

---

## 9. What we'd like back

Matching the existing bundle's conventions:

- **`screens/sm-land-alert.jsx`** — the prototype, 320×240, importing shared tokens from `sm-common.jsx`.
- **Screenshots** at 2× (640×480), one per state in §8.
- **A README section — "Screen 5 — Alert"** in the same voice as Screens 1–4: exact hex values, exact pixel
  geometry, ring radii/timing, font sizes **as intended sizes** (we'll generate the fonts to match), and per-element
  animation notes. The existing README's precision is exactly why it was implementable — please match it.
- **Call out anything that needs a technique from §5.4**, so it survives implementation instead of getting
  quietly simplified.

## 10. Where we most want your opinion

1. **How do you make it distinct without making it a different product?** It must still feel like the same
   instrument as Screens 1–4 — same palette, same phosphor language — while being structurally unmistakable.
   That tension is the whole job.
2. **Composition.** Everything on this device so far is centred or panelled. Off-centre? Anchored? Diagonal?
   Rings originating somewhere other than dead centre?
3. **What do the rings actually mean?** Right now they're decorative. Could they encode something real —
   magnitude in their count/speed/reach, depth in their origin size? The instrument would feel much smarter.
4. **The severity ramp** (§7) — we think it's backwards. Propose a better one.
5. **Is "NOW · 29KM DEEP" the right framing** for "showing now"? It's literal. There's probably something better.
