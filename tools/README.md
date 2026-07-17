# tools/

## `genfont.ps1` — generate a GFX font at any size

Rasterises a Windows system font (default Arial) into the **Adafruit GFX font format** that TFT_eSPI's
`setFreeFont()` consumes. This is how `src/SeisSans10.h` and `src/SeisSans8.h` were made.

**Why this exists:** the stock GFX fonts only come in 9pt / 12pt / 18pt / 24pt, so place names used to fall
off a cliff from FreeSans9pt straight to the tiny 5×7 built-in font — the long-running "the text is way too
small" problem. This lets us make whatever intermediate rung we need. It also means **design work is not
constrained to the stock sizes**: if a design calls for a 48px magnitude, generate it.

```powershell
powershell -File tools/genfont.ps1 -FontName Arial -EmPx 14 -VarName SeisSans10 -OutFile src/SeisSans10.h
```

| Param | Meaning |
|---|---|
| `-FontName` | any installed font family, e.g. `Arial`, `Arial Black`, `Tahoma` |
| `-EmPx` | em size in **pixels** (not points). Cap height lands ≈ 0.72 × EmPx |
| `-VarName` | the C symbol, e.g. `SeisSans10` → `const GFXfont SeisSans10` |
| `-OutFile` | header path; then `#include` it and `tft.setFreeFont(&SeisSans10)` |

Sizes used so far: `-EmPx 14` → cap 10 (`SeisSans10`), `-EmPx 11` → cap 8 (`SeisSans8`).

**Notes / limits**
- ASCII **0x20–0x7E only**. Place names are already romanised, so that's fine — but it means no macrons
  (firmware strokes those as a separate 1px bar) and no CJK.
- Costs roughly **8–12KB of flash** per font. We have room, but they're not free.
- Regular weight only as written. For bold, pass a bold family (`-FontName "Arial Black"`) or change
  `$style` to `[System.Drawing.FontStyle]::Bold`.
- Rendered with `SingleBitPerPixelGridFit` — 1-bit, no anti-aliasing, which is what the GFX format is.
  Below ~8px cap the glyphs start to break up.
- Uses `System.Drawing`, so it's **Windows-only**. Run it from the repo root.

**Format reference** (what the script emits, if it ever needs debugging): each glyph is
`{bitmapOffset, width, height, xAdvance, xOffset, yOffset}`; bitmaps are packed MSB-first, row-major, and
**each glyph is padded to a whole byte**, so offsets advance by `ceil(w*h/8)`. `GFXfont` is
`{bitmap, glyph, first, last, yAdvance}`. `tft.textWidth()` sums `xAdvance` — which is how the header
string widths in `design_brief_alert_screen.md` were measured exactly rather than estimated.
