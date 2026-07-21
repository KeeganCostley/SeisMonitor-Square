# Handoff: SeisMonitor — Landscape UI (320×240)

## Overview
SeisMonitor is an ESP32-S3 earthquake monitor (board: QDTFT **ES3C28P**, 2.8" IPS **ILI9341**, FT6336G capacitive touch). The display runs in **landscape** (`setRotation(1)`) at **320×240**. This handoff covers the full landscape UI: the main monitor screen (two region variants — NZ map and Global wireframe globe), the Settings screen, and the Loading/boot screens.

The on-device firmware is C++ (Arduino / TFT_eSPI) in `KeeganCostley/SeisMonitor-Square` (`src/main.cpp`). These design files are the **visual + interaction spec** the firmware should be brought in line with.

## About the Design Files
The files in this bundle are **design references created in HTML/React** — prototypes showing the intended look and behavior, **not production code to copy directly**. The target runtime is an **ESP32 firmware drawing primitives to a TFT via TFT_eSPI** (filled rects, lines, circles, polygons, bitmap/GFX fonts). The task is to **reproduce these layouts and visual treatments using the firmware's existing drawing helpers and coordinate system** — not to embed HTML/JS.

Where the prototype uses effects the panel can't do cheaply (blur glows, anti-aliased strokes, CSS animations), approximate them with the nearest TFT-friendly technique (e.g. a 1px lighter "halo" line instead of a blur, stepped frames for animation). Notes are called out per-screen.

## Fidelity
**High-fidelity.** Colors, layout coordinates, type sizing intent, and component structure are final. Use the exact hex values and the documented pixel geometry. The 320×240 coordinate space in the prototype maps 1:1 to the device framebuffer.

---

## Coordinate System & Global Layout

Canvas: **320 (W) × 240 (H)**, origin top-left. Landscape.

The **main monitor** screens are organized into **bordered panels** (soft-green 1px outlines, radius 3px) with a 6px outer margin and 5px gaps:

- **Header**: full width, height **22px** (`HEADER_H`). Bottom border in `edge` (`#234a36`).
- **Content area**: y `26…234`.
- **Left column** (x `6…112`, width `106`):
  - **Data panel**: top `26`, height `161` (y `26…187`). Bordered. Holds two equal cells (flex:1) — **LATEST** (top) and **24H HIGH** (bottom) — separated by a 1px inner divider (`edgeDim` `#16301f`). **Contents are centred** both horizontally and vertically in each cell (`text-align:center`, `align-items:center`).
  - **Seismograph panel**: top `192`, height `42` (y `192…234`). Bordered, `overflow:hidden`. Aligned to the data-column width (intentionally NOT full-width).
- **Map / Globe panel** (x `117…314`, width `197`, y `26…234`, height `208`): bordered, `overflow:hidden`. Holds the rings+NZ map (NZ frame) or the wireframe globe (Global frame), centred inside.

> Design history notes: (1) the seismograph used to span the full 320px width at the bottom — it was deliberately **reeled in** to the lower-left column so the map could grow. (2) Green **panel borders** were added to give structure and separate the data / seismo / map sections. (3) The data was **centred** within its panel (it previously hugged the left edge).

**Data-cell sizing (must fit the cell — place names can wrap to 2 lines):** label 8px → magnitude **18px** (was 22) → place name **11.5px** (was 13) → meta 8px, with tight 2–3px margins. Each cell is ~80px tall; keep the stacked content under that so the 24H HIGH meta line never spills into the seismo panel.

---

## Palette (Soft HUD theme — the chosen aesthetic)

Desaturated, lowered-brightness phosphor green ("soft HUD"), easier on the eyes than pure #00ff55. The **NZ coastline outline is the exception** — it is deliberately bright.

| Token | Hex | Use |
|---|---|---|
| `bg` | `#03080a` | screen background (near-black, faint cool green) |
| `ink` | `#b8e6c4` | primary readable text (place names) |
| `primary` | `#7fd69a` | muted phosphor — status text, seismo trace |
| `secondary` | `#4a8a68` | quiet supporting green — meta labels |
| `sub` | `#355a48` | deep muted — ticks, footnotes |
| `rule` | `#0e1a16` | faint hairline (legacy / seismo grid) |
| `edge` | `#234a36` | **structural panel borders (header, data, seismo, map)** |
| `edgeDim` | `#16301f` | inner divider between LATEST and 24H HIGH cells |
| `latest` | `#9de8b2` | LATEST accent (soft light-green) |
| `highest` | `#d4e88a` | 24H HIGH accent (soft chartreuse) |
| `mapLand` | `#0c1813` | NZ land fill |
| **`mapOutline`** | **`#00b347`** | **NZ coastline — BRIGHT, crisp (stands out vs. the soft rings)** |
| `ring1…ring4` | `#0e1c17` / `#16291f` / `#20392b` / `#2b4938` | concentric distance rings (faint→less faint) |

**Globe (Global region) brighter "alien green" set:**
| Token | Value | Use |
|---|---|---|
| globe mesh | `rgb(0,255,102)` (varied alpha) | graticule + continents |
| globe limb | `#00ff88` | sphere edge glow |
| globe fill | `#021109` | sphere disc fill |
| globeFill front mesh | `rgba(0,255,102,0.55)` bright / `0.12` back | front vs back hemisphere |

## Typography

- **Mono**: `JetBrains Mono` → device equivalent: the GFX/bitmap mono already in firmware. Used for labels, magnitudes, header, ticks.
- **Sans**: `Inter` (600–700) → device equivalent: `FreeSans9pt7b` / `FreeSansBold9pt7b`. Used for **place names** and region names.

Sizes (px, in the 320×240 space — match intent on device):
| Element | Size | Weight | Notes |
|---|---|---|---|
| Header text | 10 | 700 | letter-spacing ~1.3 |
| Section label (`◆ LATEST`, `◆ 24H HIGH`, `REGION`) | 8 | 700 | letter-spacing ~1.4–1.6, colored by accent |
| **Magnitude (`M2.4`)** | **18** | 700 | supporting figure — sized to fit the cell |
| **Place name** | **11.5** | 700 | **most important readable element — sans (Inter/FreeSans)**, line-height 1.15, may wrap to 2 lines |
| Meta (`4M AGO · 12KM`) | 8 | 400/500 | `secondary` color, uppercase |
| Ring distance labels | 6.5 | — | `sub`, mono, e.g. `100KM` |
| Seismo Z / 60s ticks | 6.5 | — | `sub` |

> Priority: **place names must be large and readable; magnitude is secondary.** This was an explicit design correction.

---

## Screen 1 — Main Monitor · NZ (Rings + soft HUD)

**File:** `screens/sm-land-rings.jsx`

**Purpose:** Default screen for the NZ region. Shows latest + 24h-high quakes as text (left) and plotted on an accurate NZ map ringed by concentric distance circles (right). Live seismograph lower-left.

**Header:** left `◉ SEIS · NZ` (primary). Right group (gap 10): `23:42 · WIFI` (secondary) + settings cog icon (secondary). 
> Header treatment is design-led (option B): keep `◉ SEIS · <REGION>` left and the cog on the right. Firmware's current centred-title/left-gear layout should be updated to match this.

**Left data block** (centred vertically in y `22…190`):
- LATEST: `◆ LATEST` label (`latest` color) · `M2.4` (21px, `latest`) · `Porirua, New Zealand` (13px Inter 700, `ink`) · `4M AGO · 12KM` (8px, `secondary`).
- 24H HIGH: `◆ 24H HIGH` label (`highest`) · `M4.1` (21px, `highest`) · `25 km NW Wellington` (13px, `ink`) · `3H AGO · 38KM`.

**Map zone** (x `118…320`, y `22…240`), centred at **cx≈219, cy≈131**:
- **Concentric distance rings** at radii **30, 54, 78, 98** px, colors `ring1…ring4`, `stroke-dasharray 2,3`, strokeWidth 0.7. Labels `100 / 200 / 300 / 500` (km) placed along the **lower-left** of each ring (in open ocean, clear of NZ), 6.5px `sub`.
- **Crosshair** at centre (cx,cy): two 12px lines, `secondary`, opacity 0.6.
- **NZ coastline**: real GeoNet polygons (North Is., South Is., Stewart Is.) — see **Geometry** below. Fill `mapLand`, stroke **`mapOutline #00b347`**, strokeWidth ~1.1, round joins/caps. Centred in a 152×192 projection box at (gx=cx−76, gy=cy−96).
- **Markers** (projected from real lat/lon):
  - LATEST: dashed bearing line from centre; rings r=9 (fill `latest` α0.12), r=4.5 (stroke `latest`), r=2 (fill `latest`).
  - 24H HIGH: dashed bearing line; rings r=8 (α0.10), r=3.5 (stroke `highest`), r=1.7 (fill `highest`).

**Seismograph** (left `6`, top `190`, w `110`, h `44`): faint horizontal grid lines (`ring1`), tick marks every `w/10` (major every 5th, `secondary`), centre baseline (`ring3`), trace = `primary` 1px with a soft 1.8px α0.3 "glow" underlay (on device: a single lighter 1px halo line, no blur). `Z` label top-left, `60s` top-right (6.5px `sub`).

---

## Screen 2 — Main Monitor · Global (Wireframe Globe)

**File:** `screens/sm-land-globe.jsx`

**Purpose:** Same layout as NZ, but the right zone shows a slowly-rotating 3D **wireframe globe** instead of a flat map — retro early-internet / vector-terminal aesthetic, harder/brighter green. Net-new vs current firmware (which draws a flat 2D world map).

**Header:** `◉ SEIS · GLOBAL` left.

**Data block:** same structure; sample global values — LATEST `M5.2 · Off E. Honshu, Japan · 11M AGO`; 24H HIGH `M6.4 · S. Sandwich Islands · 6H AGO`.

**Globe** (fills right zone, full height; radius **R≈92**, centred):
- **Orthographic projection** of a sphere with axis tilt **0.42 rad**, rotating slowly (yaw `+0.0035 rad/frame`, ~60fps; stop if reduced-motion).
- Project (lat,lon): `x=cosφ·sin(λ+rot)`, `y=sinφ`, `z=cosφ·cos(λ+rot)`, then tilt around X; screen `= (cx + R·x, cy − R·y_tilted)`; **front-facing when z_tilted > 0**.
- **Disc fill** `#021109`; **limb** circle `#00ff88` (glow on web — on device draw a 1px bright edge, optionally a second dimmer ring).
- **Graticule mesh**: parallels every 30° (lat −60…60; equator brighter), meridians every 30°. Front segments `rgba(0,255,102,~0.5)`; back segments dim `rgba(0,255,102,~0.1)` for depth (or cull on device if cheaper).
- **Continents**: coarse lat/lon outline loops (N. America, S. America, Africa+Arabia, Eurasia, Australia) — see `CONTINENTS` array in the file. Front bright `0.85` alpha, back dim. These are intentionally rough/lo-fi.
- **Epicenter markers** (only when front-facing): surface dot in accent color, a **radial spike** (~12px) pointing outward from the globe centre through the point (this is the "pointing to the epicenter" cue), and a pulsing ring. LATEST `latest`, 24H HIGH `highest`.

**Seismograph:** identical to Screen 1.

> Device feasibility: the firmware can compute the projection in floats and draw `drawLine`/`drawPixel` per segment; sample meridians/parallels every ~6°. Animate by redrawing only the globe rect each tick. If full mesh is too heavy, reduce to parallels/meridians every 45° + continents.

---

## Screen 3 — Settings

**File:** `screens/sm-land-settings.jsx`

**Purpose:** The only two settings: **Region selection** and **WiFi connection details**. Opened by tapping the header cog; closed by tapping it again (auto-closes after 30s per firmware).

**Header:** `‹ SETTINGS` left (primary); cog right, shown **active** (`accent #9de8b2`).

**Layout:** two columns split by a vertical divider at **x=160**.

**Left — REGION** (x 12, top 34, width 136):
- `REGION` label (8px `secondary`, letter-spacing 1.6).
- 5 rows, gap 4, each 26px tall, padding 0 8, radius 4:
  - Options: **New Zealand, Japan, California, China, Global**.
  - Selected row: fill `#0f2118`, inset left accent bar `#9de8b2` (`box-shadow: inset 2px 0 0`), marker `◉` in `accent`, label 13px Inter **700** `ink`.
  - Unselected: transparent, marker `○` in `sub`, label 13px Inter 500 `secondary`.

**Right — WIFI CONNECTION** (x 176, top 34, width ~116):
- `WIFI CONNECTION` label (8px `secondary`).
- **NETWORK** row: small `NETWORK` cap (7.5px `sub`) + SSID `Aratau-5G` (15px Inter 700 `ink`); right-aligned **WiFi bars** (4 bars, filled by signal in `accent`, empty in `rule`).
- hairline divider (`rule`).
- **SIGNAL**: `−54 dBm` (12px `primary`), label left.
- **IP ADDR**: `192.168.1.42` (12px `primary`).
- **STATUS**: `CONNECTED` (11px `ok #9de8b2` 700) with a 6px glowing dot.

**Footer** (full width, 18px, top hairline): `TAP REGION TO SWITCH · TAP GEAR TO CLOSE` (7.5px `sub`, centred).

---

## Screen 4 — Loading / Boot

**File:** `screens/sm-land-loading.jsx`

**Purpose:** Power-up + connecting/syncing states. `stage` prop: `'wifi'` (connecting) or `'sync'` (fetching quakes).

**Centred stack:**
- Pulsing `◉` glyph (40px, `accent`, glow) inside two **expanding ring** animations (70px circles scaling 0.4→1.6, fading out, 2.4s loop, staggered 1.2s).
- Wordmark `SEISMONITOR` (18px, letter-spacing 6, `ink`).
- Sub `EARTHQUAKE MONITOR` (8px, letter-spacing 3, `secondary`).
- **Status line** (9px, `primary`): `CONNECTING TO WIFI` or `FETCHING QUAKE DATA`, followed by animated `·` dots (cycles 0→3 every 450ms, in `accent`).
- **Indeterminate scan bar**: 150×2px track (`rule`), a 40%-wide gradient sweep in `accent` sliding left→right, 1.4s loop.

**Footer:** `v3.0` bottom-left, `ES3C28P` bottom-right (7.5px `sub`).

> Device: replace CSS animations with stepped frames — pulse the glyph brightness, advance the dot count on a timer, slide a short bright segment along the bar. Honor reduced-motion = static.

---

## Geometry — NZ coastline (authoritative)

Use the **real GeoNet lat/lon polygons** from the firmware (`drawNZMap()` in `src/main.cpp`): North Island (clockwise from Cape Reinga), South Island (clockwise from Farewell Spit), Stewart Island. The prototype mirrors these exactly in `screens/sm-common.jsx` (`NZ_NORTH_LL`, `NZ_SOUTH_LL`, `NZ_STEWART_LL`).

**Projection:** equal-aspect with `cos(centerLat)` longitude correction, fit to the target box and centred (see `NZ_PROJECT()` in `sm-common.jsx` and `getMapAspect()` in firmware). NZ region bounds in firmware v3.0: `{-47.3, -34.0, 166.0, 179.0}`.

**Marker lat/lon (sample):** LATEST Porirua `(-41.13, 174.84)`; 24H HIGH `(-41.12, 174.52)`.

---

## Interactions & Behavior
- **Cog tap** → open Settings; tap again (or 30s timeout) → return to monitor.
- **Region row tap** → switch active region, persist, redraw monitor with that region's map/data + API feed.
- **Globe**: continuous slow yaw rotation; reduced-motion → static.
- **Loading**: shown during WiFi connect and initial data fetch; dot + scan animations loop until the next state.
- **Seismograph**: live scrolling trace; firmware already models P/S/surface/coda phases — keep that, just within the reeled-in column-width region.

## State
- `region` (NZ | Japan | California | China | Global) — persisted.
- WiFi: SSID, RSSI→bars, IP, connection status.
- `latestQuake`, `highestRegionalQuake` (mag, place, lat/lon, ago), `recentQuakes[]`.
- UI mode: monitor | settings | loading | alert.

## Design Tokens
See **Palette** and **Typography** tables above. Spacing: outer margin 6px, panel gap 5px, panel borders 1px `edge` radius 3px; header 22px; left column 106px wide; data panel 161px tall (two centred cells); seismo panel 42px; map panel 197×208; ring radii 30/54/78/98 with distance labels (100/200/300/500) placed in the lower-left ocean clear of NZ; globe R≈92, tilt 0.42rad.

## Assets
No raster assets. All graphics are drawn (rings, coastline polygons, globe mesh, gear icon, wifi bars, seismo trace). Gear icon = standard Material settings cog path (in each screen file); on device use the existing `drawGearIcon()`.

## Screenshots
Reference renders (640×480 @2x of the 320×240 screens) in `screenshots/`:
- `01-monitor-nz.png` — Screen 1, NZ rings monitor
- `02-monitor-global.png` — Screen 2, Global wireframe globe
- `03-settings.png` — Screen 3, Settings (region + WiFi)
- `04-loading-connecting.png` — Screen 4, Loading (connecting)
- `05-loading-syncing.png` — Screen 4, Loading (syncing data)

## Files (in this bundle)
- `SeisMonitor UI Directions.html` — design canvas host (open in a browser to view all screens).
- `design-canvas.jsx` — canvas harness (pan/zoom, not part of the product).
- `screens/sm-common.jsx` — **shared: 320×240 constants (`SML`), NZ polygons + `NZ_PROJECT()`, seismo trace, sample data.**
- `screens/sm-land-rings.jsx` — Screen 1 (NZ monitor).
- `screens/sm-land-globe.jsx` — Screen 2 (Global globe).
- `screens/sm-land-settings.jsx` — Screen 3 (Settings).
- `screens/sm-land-loading.jsx` — Screen 4 (Loading).
- Portrait references (`sm-hud`, `sm-rings`, `sm-hudrings-soft`, etc.) are **earlier 240×240 explorations — ignore for the landscape build.**
