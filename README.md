# SeisMonitor

A desk earthquake monitor built on an ESP32-S3. It polls live seismic feeds, plots quakes on a regional
map or a spinning wireframe globe, runs a live seismograph trace, and takes over the screen when
something happens.

Firmware is C++ (Arduino framework, TFT_eSPI) built with PlatformIO. The whole UI is drawn at runtime
with graphics primitives — no image assets, no UI framework.

## Hardware

| | |
|---|---|
| MCU | ESP32-S3 |
| Board | QDTFT **ES3C28P** |
| Display | 2.8" IPS **ILI9341V**, 240×320 native — run **landscape 320×240, rotated 180°** (mounted inverted in its enclosure) |
| Touch | **FT6336G** capacitive (I2C 0x38) |
| Audio | ES8311 codec + FM8002E amp (unused so far — see the pin caveat in the board reference) |

Full pinout and specs: [`docs/ES3C28P_BOARD_REFERENCE.md`](docs/ES3C28P_BOARD_REFERENCE.md).

## Features

- **Five regions** — New Zealand, Japan, California, China, Global. Switchable on-device.
- **Live data** — GeoNet for New Zealand, USGS for everywhere else.
- **Regional maps** drawn from real coastline polygons, with epicentre markers sized by magnitude.
- **Spinning wireframe globe** for the Global region — orthographic projection, real Natural Earth
  coastlines, rendered into an off-screen sprite so the rotation stays smooth.
- **Live seismograph** with modelled P/S/surface/coda phases, triggered by real events.
- **Alert screen** — a full-screen takeover when a quake clears your sensitivity threshold.
- **Depth-aware severity** — magnitude is energy at the source, but what you feel at the surface falls
  off with focal depth, so severity colour folds in both (a shallow M4 can outrank a deep M6).
- **Te Reo macron restoration** — GeoNet's feed ships New Zealand place names stripped of macrons
  ("Taupo"); the device puts them back ("Taupō").
- **On-device settings** — region and alert sensitivity, via the gear icon.

## Build & flash

PlatformIO. TFT_eSPI is configured entirely through `build_flags` in `platformio.ini` — there is no
`User_Setup.h` to edit.

```bash
pio run -e seismonitor                              # build
pio run -e seismonitor -t upload --upload-port COM11 # flash (adjust the port)
```

WiFi is configured on first boot: the device raises a captive-portal AP, and you enter your credentials
in a browser. The boot screen shows the firmware version bottom-left — handy for confirming which binary
is actually on the device.

> **`src/main.cpp` is the only build target.** The `.ino` sketches under `archive/` are earlier
> Arduino-IDE iterations and are not compiled.

## Layout

```
src/                    firmware — main.cpp plus generated font/coastline headers
include/
tools/                  code generators (see below)
docs/                   design system, board reference, photos
archive/                every earlier iteration, kept for reference
platformio.ini          build config, incl. all TFT_eSPI settings
```

### Generators (`tools/`)

The stock Adafruit GFX fonts only come in a few sizes, and hand-plotted coastlines look blocky — so both
are generated rather than hand-maintained:

| Tool | What it does |
|---|---|
| `genfont.ps1` | Rasterises any installed system font, at **any** pixel size, into the GFX font format. Supports bold and a restricted character range (the big magnitude font carries only `.`, `0-9`, `M`). |
| `gencoast.py` | Fetches Natural Earth coastline GeoJSON, simplifies it (Douglas–Peucker) to a point budget, and emits the globe's coastline header. |

```powershell
powershell -File tools/genfont.ps1 -FontName Arial -Bold -EmPx 22 -VarName SeisPlace22 -OutFile src/SeisPlace22.h
python tools/gencoast.py ne_110m_coastline.geojson src/SeisCoast.h 0.6 4.0 4
```

## Design

The UI follows a design system in [`docs/design_handoff_landscape_ui/`](docs/design_handoff_landscape_ui/) —
a "soft HUD" palette of desaturated phosphor green on near-black, with per-screen specs and reference
renders. The firmware is built to match it.

## Data sources

- **GeoNet** (New Zealand) — <https://api.geonet.org.nz>
- **USGS** (everywhere else) — earthquake summary feeds

Both are polled on a background task pinned to the second core, so a slow network call can never stall
the UI. Note that both agencies publish a quake some minutes *after* it occurs (they locate and review
first), so an alert firing means "newly published", not "just happened" — the screen shows the true
origin age.

## Licence

Not yet specified.
