// Landscape — Rings layout + soft HUD palette (the favourite), reflowed for
// the 320×240 rotated board. Concentric distance rings + accurate NZ in the
// centre; LATEST flanks left, 24H HIGH flanks right; full-width seismo along
// the bottom; settings cog in the header.

const LANDR = {
  bg: '#03080a',
  ink: '#b8e6c4',
  primary: '#7fd69a',
  secondary: '#4a8a68',
  sub: '#355a48',
  rule: '#0e1a16',
  latest: '#9de8b2',
  highest: '#d4e88a',
  mapLand: '#0c1813',
  mapOutline: '#00b347',
  ring1: '#0e1c17', ring2: '#16291f', ring3: '#20392b', ring4: '#2b4938',
  edge: '#234a36',          // structural panel border — soft green
  edgeDim: '#16301f',       // inner hairline divider
};

function Cog({ size = 13, color }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" style={{ display: 'block' }}>
      <path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"
        fill={color} />
    </svg>
  );
}

function LandRingsFrame() {
  const t = LANDR;
  const W = SML.W, H = SML.H, HH = SML.HEADER_H;

  // ── Panel geometry ─────────────────────────────────────────────
  const pad = 6, gap = 5;
  const contentTop = HH + 4;               // 26
  const contentBot = H - pad;              // 234
  const leftX = pad, leftW = 106;          // left column
  const rightX = leftX + leftW + gap;      // 117
  const rightW = W - pad - rightX;         // 197
  const seismoH = 42;
  const dataTop = contentTop;              // 26
  const dataH = (contentBot - contentTop) - seismoH - gap; // 161
  const seismoTop = contentBot - seismoH;  // 192

  // Map panel inner space
  const mpW = rightW - 2, mpH = (contentBot - contentTop) - 2;
  const cx = mpW / 2, cy = mpH / 2;
  const s = seisTrace(220, 24, 1);

  // Project NZ centred inside the map panel
  const boxW = 150, boxH = 190;
  const nz = NZ_PROJECT(boxW, boxH, 8);
  const gx = cx - boxW / 2, gy = cy - boxH / 2;
  const [latX, latY] = nz.project(LATEST.lat, LATEST.lon);
  const [hiX, hiY]   = nz.project(HIGH24.lat, HIGH24.lon);

  const rings = [
    { r: 30, c: t.ring1, l: '100' },
    { r: 54, c: t.ring2, l: '200' },
    { r: 78, c: t.ring3, l: '300' },
    { r: 98, c: t.ring4, l: '500' },
  ];

  const seismoSvgW = leftW - 2, seismoSvgH = seismoH - 2;

  return (
    <div style={{ width: W, height: H, background: t.bg, color: t.ink, fontFamily: '"JetBrains Mono", ui-monospace, monospace', position: 'relative', overflow: 'hidden' }}>
      {/* HEADER */}
      <div style={{ height: HH, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 10px', fontSize: 10, letterSpacing: 1.3, fontWeight: 700, borderBottom: `1px solid ${t.edge}` }}>
        <span style={{ color: t.primary }}>◉ SEIS · NZ</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <span style={{ color: t.secondary }}>23:42 · WIFI</span>
          <span style={{ display: 'flex', color: t.secondary }}><Cog color={t.secondary} /></span>
        </div>
      </div>

      {/* DATA PANEL — bordered, contents centred */}
      <div style={{
        position: 'absolute', left: leftX, top: dataTop, width: leftW, height: dataH,
        border: `1px solid ${t.edge}`, borderRadius: 3,
        display: 'flex', flexDirection: 'column',
      }}>
        {/* LATEST cell */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', textAlign: 'center', padding: '2px 6px' }}>
          <div style={{ fontSize: 8, color: t.latest, letterSpacing: 1.4, fontWeight: 700 }}>◆ LATEST</div>
          <div style={{ fontSize: 18, fontWeight: 700, lineHeight: 1, color: t.latest, marginTop: 2, letterSpacing: -0.5 }}>M{LATEST.mag.toFixed(1)}</div>
          <div style={{ fontSize: 11.5, color: t.ink, marginTop: 3, lineHeight: 1.15, fontFamily: '"Inter", sans-serif', fontWeight: 700, textWrap: 'balance' }}>{LATEST.place}</div>
          <div style={{ fontSize: 8, color: t.secondary, marginTop: 2, letterSpacing: 0.6 }}>{LATEST.ago.toUpperCase()} AGO · 12KM</div>
        </div>
        {/* divider */}
        <div style={{ height: 1, background: t.edgeDim, margin: '0 8px' }} />
        {/* 24H HIGH cell */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', textAlign: 'center', padding: '2px 6px' }}>
          <div style={{ fontSize: 8, color: t.highest, letterSpacing: 1.4, fontWeight: 700 }}>◆ 24H HIGH</div>
          <div style={{ fontSize: 18, fontWeight: 700, lineHeight: 1, color: t.highest, marginTop: 2, letterSpacing: -0.5 }}>M{HIGH24.mag.toFixed(1)}</div>
          <div style={{ fontSize: 11.5, color: t.ink, marginTop: 3, lineHeight: 1.15, fontFamily: '"Inter", sans-serif', fontWeight: 700, textWrap: 'balance' }}>{HIGH24.place}</div>
          <div style={{ fontSize: 8, color: t.secondary, marginTop: 2, letterSpacing: 0.6 }}>{HIGH24.ago.toUpperCase()} AGO · 38KM</div>
        </div>
      </div>

      {/* SEISMO PANEL — bordered, below the data panel */}
      <div style={{
        position: 'absolute', left: leftX, top: seismoTop, width: leftW, height: seismoH,
        border: `1px solid ${t.edge}`, borderRadius: 3, overflow: 'hidden',
      }}>
        <svg width={seismoSvgW} height={seismoSvgH} style={{ display: 'block' }}>
          {[...Array(3)].map((_, i) => (
            <line key={i} x1={0} x2={seismoSvgW} y1={(i + 1) * seismoSvgH / 4} y2={(i + 1) * seismoSvgH / 4} stroke={t.ring1} strokeWidth={0.3} />
          ))}
          {[...Array(11)].map((_, i) => {
            const x = i * seismoSvgW / 10;
            const major = i % 5 === 0;
            return <line key={'t' + i} x1={x} x2={x} y1={seismoSvgH / 2 - (major ? 3 : 1.5)} y2={seismoSvgH / 2 + (major ? 3 : 1.5)} stroke={t.secondary} strokeWidth={0.4} opacity={major ? 0.7 : 0.4} />;
          })}
          <line x1={0} x2={seismoSvgW} y1={seismoSvgH / 2} y2={seismoSvgH / 2} stroke={t.ring3} strokeWidth={0.5} />
          <path d={tracePath(s, seismoSvgW, seismoSvgH, 0.28)} fill="none" stroke={t.primary} strokeWidth={1.8} opacity={0.3} style={{ filter: 'blur(1.3px)' }} />
          <path d={tracePath(s, seismoSvgW, seismoSvgH, 0.28)} fill="none" stroke={t.primary} strokeWidth={1} />
          <text x={3} y={9} fontSize={6.5} fill={t.sub} fontFamily="monospace" letterSpacing={0.5}>Z</text>
          <text x={seismoSvgW - 18} y={9} fontSize={6.5} fill={t.sub} fontFamily="monospace" letterSpacing={0.5}>60s</text>
        </svg>
      </div>

      {/* MAP PANEL — bordered, rings + NZ centred inside */}
      <div style={{
        position: 'absolute', left: rightX, top: contentTop, width: rightW, height: contentBot - contentTop,
        border: `1px solid ${t.edge}`, borderRadius: 3, overflow: 'hidden',
      }}>
        <svg width={mpW} height={mpH} style={{ display: 'block' }}>
          {/* distance rings — labels along the lower-left (ocean, clear of NZ) */}
          {rings.map((ring, i) => {
            const a = Math.PI * 0.82;          // ~down-left
            const lx = cx + Math.cos(a) * ring.r;
            const ly = cy + Math.sin(a) * ring.r;
            return (
              <g key={i}>
                <circle cx={cx} cy={cy} r={ring.r} fill="none" stroke={ring.c} strokeWidth={0.7} strokeDasharray="2,3" />
                <text x={lx} y={ly} fontSize={6.5} fill={t.sub} textAnchor="middle" fontFamily="monospace" letterSpacing={0.3}>{ring.l}</text>
              </g>
            );
          })}

          {/* crosshair */}
          <line x1={cx - 6} x2={cx + 6} y1={cy} y2={cy} stroke={t.secondary} strokeWidth={0.6} opacity={0.6} />
          <line x1={cx} x2={cx} y1={cy - 6} y2={cy + 6} stroke={t.secondary} strokeWidth={0.6} opacity={0.6} />

          {/* NZ — accurate firmware coastline, centred */}
          <g transform={`translate(${gx.toFixed(2)}, ${gy.toFixed(2)})`}>
            <path d={nz.north}   fill={t.mapLand} stroke={t.mapOutline} strokeWidth={1.1} strokeLinejoin="round" strokeLinecap="round" />
            <path d={nz.south}   fill={t.mapLand} stroke={t.mapOutline} strokeWidth={1.1} strokeLinejoin="round" strokeLinecap="round" />
            <path d={nz.stewart} fill={t.mapLand} stroke={t.mapOutline} strokeWidth={1.1} strokeLinejoin="round" strokeLinecap="round" />

            {/* bearing lines + markers */}
            <line x1={boxW / 2} y1={boxH / 2} x2={latX} y2={latY} stroke={t.latest} strokeWidth={0.4} strokeDasharray="1.5,2" opacity={0.5} />
            <circle cx={latX} cy={latY} r={9} fill={t.latest} opacity={0.12} />
            <circle cx={latX} cy={latY} r={4.5} fill="none" stroke={t.latest} strokeWidth={0.7} />
            <circle cx={latX} cy={latY} r={2} fill={t.latest} />

            <line x1={boxW / 2} y1={boxH / 2} x2={hiX} y2={hiY} stroke={t.highest} strokeWidth={0.4} strokeDasharray="1.5,2" opacity={0.45} />
            <circle cx={hiX} cy={hiY} r={8} fill={t.highest} opacity={0.10} />
            <circle cx={hiX} cy={hiY} r={3.5} fill="none" stroke={t.highest} strokeWidth={0.7} />
            <circle cx={hiX} cy={hiY} r={1.7} fill={t.highest} />
          </g>
        </svg>
      </div>
    </div>
  );
}

Object.assign(window, { LandRingsFrame });
