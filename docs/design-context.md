# SeisMonitor — Design Context

A one-file brief to bring a design collaborator up to speed: what the product is, what it stands for, and
the visual language it's built in. For finer firmware/screen detail see
[`design_handoff_landscape_ui/README.md`](design_handoff_landscape_ui/README.md) (the full spec) and the
reference renders in [`design_handoff_landscape_ui/screenshots/`](design_handoff_landscape_ui/screenshots/).

---

## What it is

SeisMonitor is a small **desk earthquake monitor** — a dedicated device (ESP32-S3, 2.8" 320×240 colour
screen, capacitive touch) that shows live seismic activity. It polls the official feeds continuously and
plots recent quakes on a real regional map — or a slowly rotating wireframe globe — the moment they're
reported. A significant event takes over the whole screen. It's an instrument and an object: useful, and
nice to have on a desk.

## Who it's for (and why that shapes the design)

Early buyers are **New Zealanders**. That drives real decisions: the New Zealand map and the **Pacific**
are treated as the home turf (extra island detail there), Te Reo Māori is respected (see below), and the
default framing is Aotearoa-first. The tone is understated and factual, not alarmist — most alerts are
small quakes nobody felt, so the product informs rather than frightens.

## What it means (the values to keep visible)

These aren't just features; they're the product's character, and good marketing/packaging should surface them:

- **Real, live data — not simulated.** Every reading is an actual event from the world's seismic networks.
  Sourced from **GeoNet** (Aotearoa New Zealand) and the **USGS** (worldwide). Credit them.
- **Te Reo Māori, done right.** The data feed ships New Zealand place names with the macrons stripped
  ("Taupo"). SeisMonitor **puts them back** ("Taupō"). Fixing something the source gets wrong is a genuine
  point of pride and a memorable hook.
- **Felt severity, not just magnitude.** Colour reflects how strongly a quake was actually *felt* at the
  surface (which depends on depth), using GeoNet's own intensity data where available — a shallow small
  quake can read hotter than a deep large one. The product shows what matters to a person, not just a number.
- **Made in Aotearoa New Zealand.**

## Voice & tone

Functional, objective, plain. Short declarative sentences. Name things by what they are. No hype, no
metaphor-stacking, minimal warmth — confident and matter-of-fact. (Recent copy was deliberately tightened
in this direction.)

---

## The visual language — "Soft HUD"

Desaturated, lowered-brightness **phosphor green on near-black** — a calm, instrument/terminal aesthetic,
easier on the eyes than pure `#00ff55`. Everything is drawn as glowing lines and type on black; think
oscilloscope or vector terminal, not paper. This is the through-line for *every* surface — screen,
packaging, marketing.

### Palette

| Role | Hex | Notes |
|---|---|---|
| Background | `#03080a` | near-black, faint cool green |
| Ink (primary text) | `#b8e6c4` | place names, readable body |
| Phosphor (primary) | `#7fd69a` | status text, header, seismograph trace |
| Secondary | `#4a8a68` | quiet supporting green — meta labels |
| Sub | `#355a48` | deep muted — ticks, footnotes |
| Panel edge | `#234a36` | thin 1px panel borders (rounded, radius 3) |
| Latest accent | `#9de8b2` | soft light-green highlight |
| 24h-high accent | `#d4e88a` | soft chartreuse highlight |
| Coastline (NZ) | `#00b347` | deliberately **bright**, crisp — stands out from the soft greens |

**Severity ramp** (colours a quake by felt intensity — the one place warm colour appears; climbs in
brightness so the worst event is the hottest, brightest pixel):
`#7fd69a` green → `#cfe25f` chartreuse → `#ffcf47` amber → `#ff9538` orange → `#ff5d4d` hot red.

### Typography

Two voices, deliberately paired:
- **Monospace** — the *instrument voice*: the wordmark, HUD labels, data, magnitudes, setup steps, ticks.
  On device it's a bitmap mono; the design equivalent is **JetBrains Mono**.
- **Humanist sans** — the *human voice*: place names, region names, any prose. Design equivalent: **Inter**
  (600–700 for names). On device, `FreeSans` and custom-rasterised sizes.

Uppercase labels get a little letter-spacing. Place names are the most important readable element.

### Recurring idioms (reuse these to stay on-brand)

- **`◉`** — the identity mark. Prefixes the wordmark: `◉ SEISMONITOR`.
- **`◆`** — a small diamond marking a section/label: `◆ LATEST`, `◆ 24H HIGH`.
- **Thin bordered panels** — content sits in rounded-rect panels with a 1px `#234a36` edge.
- **Concentric rings** — faint distance rings around a map centre; also the "activity" motif on the alert.
- **Seismograph trace** — a live green waveform; a good divider/texture element anywhere.
- **The wireframe globe** — a slowly rotating orthographic sphere with a lat/lon graticule and real
  coastlines; the signature "world" view.
- **Macron bars** — a 1px bar drawn above a vowel to restore a Māori macron (ā ē ī ō ū).

## The screens (what it looks like)

See `screenshots/` for renders. In brief:
- **Main monitor** — header strip (`◉ SEISMONITOR · REGION`, clock, wifi, gear) over three bordered panels:
  a **data stack** (latest + 24h-high, each: `◆` label, magnitude, place name, `age · depth`), a **live
  seismograph**, and a **map or globe**.
- **Settings** — region list, WiFi, and alert sensitivity, opened from the gear.
- **Loading/boot** — pulsing `◉`, wordmark, and a status line (`CONNECTING TO WIFI` / `FETCHING DATA`).
- **Alert** — a full-screen takeover: `◉ SEISMIC ACTIVITY DETECTED`, concentric rings with the magnitude
  centred (the one splash of severity colour), place name, and how long ago.

## Packaging

The box insert ([`packaging/insert.html`](packaging/insert.html)) is built in this exact language — dark
phosphor card, mono HUD labels, a seismograph divider — so the unboxing feels like the screen. Any
further collateral should do the same.

---

*Everything here is drawn at runtime with simple primitives (lines, circles, polygons, type) — there are
no raster UI assets. The design system, not any single asset, is the source of truth.*
