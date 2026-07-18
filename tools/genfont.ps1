param(
  [string]$FontName = "Arial",
  [int]$EmPx       = 14,      # em size in pixels
  [string]$VarName = "SeisSans10",
  [string]$OutFile = "out.h",
  [switch]$Bold,             # generate the bold weight
  [int]$First      = 32,     # first ASCII code to include (0x20 = space)
  [int]$Last       = 126     # last  ASCII code to include (0x7E = ~). Narrow this for big fonts:
)                            #   e.g. -First 46 -Last 77 covers '.' '/' 0-9 and A-M ('M' for magnitude)
Add-Type -AssemblyName System.Drawing

$style  = $Bold ? [System.Drawing.FontStyle]::Bold : [System.Drawing.FontStyle]::Regular
$family = New-Object System.Drawing.FontFamily($FontName)
$font   = New-Object System.Drawing.Font($family, $EmPx, $style, [System.Drawing.GraphicsUnit]::Pixel)

# Baseline (ascent) in pixels for this em size
$ascentPx = [math]::Round($family.GetCellAscent($style) / $family.GetEmHeight($style) * $EmPx)
$descPx   = [math]::Round($family.GetCellDescent($style) / $family.GetEmHeight($style) * $EmPx)
$yAdvance = $ascentPx + $descPx + 1

$PAD = 20                     # canvas padding around the pen origin
$W = $EmPx * 4 + $PAD * 2
$H = $EmPx * 4 + $PAD * 2
$penX = $PAD
$baseY = $PAD + $ascentPx

$fmt = [System.Drawing.StringFormat]::GenericTypographic.Clone()
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

$bytes  = New-Object System.Collections.Generic.List[int]
$glyphs = @()

for ($c = $First; $c -le $Last; $c++) {
  $ch = [char]$c
  $bmp = New-Object System.Drawing.Bitmap($W, $H)
  $g   = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::White)
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
  # draw with the top of the em box at (penX, PAD) -> baseline lands at baseY
  $g.DrawString([string]$ch, $font, [System.Drawing.Brushes]::Black, (New-Object System.Drawing.PointF($penX, $PAD)), $fmt)
  $g.Dispose()

  # advance width
  $mb = New-Object System.Drawing.Bitmap(1,1)
  $mg = [System.Drawing.Graphics]::FromImage($mb)
  $sz = $mg.MeasureString([string]$ch, $font, [System.Drawing.PointF]::Empty, $fmt)
  $xAdvance = [math]::Max(1, [math]::Round($sz.Width))
  if ($c -eq 32) { $xAdvance = [math]::Max(2, [math]::Round($EmPx * 0.28)) }
  $mg.Dispose(); $mb.Dispose()

  # ink bounding box
  $minX = $W; $minY = $H; $maxX = -1; $maxY = -1
  for ($y = 0; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
      if ($bmp.GetPixel($x,$y).R -lt 128) {
        if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
        if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
      }
    }
  }

  $off = $bytes.Count
  if ($maxX -lt 0) {                       # blank glyph (space)
    $glyphs += ,@($off, 0, 0, $xAdvance, 0, 1)
  } else {
    $gw = $maxX - $minX + 1
    $gh = $maxY - $minY + 1
    $xOff = $minX - $penX
    $yOff = $minY - $baseY
    # pack bits MSB-first, row-major, padded to a byte at the end of the glyph
    $acc = 0; $nbits = 0
    for ($y = $minY; $y -le $maxY; $y++) {
      for ($x = $minX; $x -le $maxX; $x++) {
        $bit = if ($bmp.GetPixel($x,$y).R -lt 128) { 1 } else { 0 }
        $acc = (($acc -shl 1) -bor $bit) -band 0xFF
        $nbits++
        if ($nbits -eq 8) { $bytes.Add($acc); $acc = 0; $nbits = 0 }
      }
    }
    if ($nbits -gt 0) { $bytes.Add(($acc -shl (8 - $nbits)) -band 0xFF) }
    $glyphs += ,@($off, $gw, $gh, $xAdvance, $xOff, $yOff)
  }
  $bmp.Dispose()
}
$font.Dispose()

# ---- emit header ----
$sb = New-Object System.Text.StringBuilder
$weightStr = $Bold ? "Bold" : "Regular"
[void]$sb.AppendLine(("// Generated: $FontName $weightStr @ ${EmPx}px em  (ascent=$ascentPx yAdvance=$yAdvance, chars 0x{0:X2}-0x{1:X2})" -f $First, $Last))
[void]$sb.AppendLine("const uint8_t ${VarName}Bitmaps[] PROGMEM = {")
$line = "  "
for ($i = 0; $i -lt $bytes.Count; $i++) {
  $line += ("0x{0:X2}, " -f $bytes[$i])
  if (($i % 12) -eq 11) { [void]$sb.AppendLine($line.TrimEnd()); $line = "  " }
}
if ($line.Trim().Length) { [void]$sb.AppendLine($line.TrimEnd()) }
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("const GFXglyph ${VarName}Glyphs[] PROGMEM = {")
for ($i = 0; $i -lt $glyphs.Count; $i++) {
  $gl = $glyphs[$i]
  $cc = $i + $First
  $disp = if ($cc -eq 32) { "' '" } else { "'" + [char]$cc + "'" }
  [void]$sb.AppendLine(("  {{ {0,5}, {1,3}, {2,3}, {3,3}, {4,4}, {5,4} }},   // 0x{6:X2} {7}" -f $gl[0],$gl[1],$gl[2],$gl[3],$gl[4],$gl[5],$cc,$disp))
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("const GFXfont ${VarName} PROGMEM = {")
[void]$sb.AppendLine("  (uint8_t  *)${VarName}Bitmaps,")
[void]$sb.AppendLine("  (GFXglyph *)${VarName}Glyphs,")
[void]$sb.AppendLine(("  0x{0:X2}, 0x{1:X2}, {2} }};" -f $First, $Last, $yAdvance))
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// Approx. $($bytes.Count + $glyphs.Count * 7 + 7) bytes")

Set-Content -Path $OutFile -Value $sb.ToString() -Encoding ASCII
Write-Host "$VarName -> $OutFile : ascent=$ascentPx yAdvance=$yAdvance bitmapBytes=$($bytes.Count)"
