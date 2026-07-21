// Landscape — GLOBAL region. In lieu of the NZ map, a slowly-rotating 3D
// wireframe globe: retro early-internet / vector-terminal aesthetic, hard
// phosphor green. Lat/lon graticule mesh + coarse continents, glowing limb,
// and epicenter markers that sit on the surface with a radial spike pointing
// out. Front hemisphere bright, back hemisphere dim for depth.
// Data column (Latest + 24H High) stays on the LEFT, seismo full-width bottom.

const LANDG = {
  bg: '#03080a',
  ink: '#b8e6c4',
  primary: '#7fd69a',
  secondary: '#4a8a68',
  sub: '#355a48',
  rule: '#0e1a16',
  latest: '#9de8b2',
  highest: '#d4e88a',
  // globe — brighter, harder green
  mesh: '0,255,102',        // rgb for rgba()
  limb: '#00ff88',
  globeFill: '#021109',
  edge: '#234a36',          // structural panel border — soft green
  edgeDim: '#16301f',       // inner hairline divider
};

// Global earthquake context (sample feed values)
const GLOBAL_LATEST = { mag: 5.2, place: 'Off E. Honshu, Japan', ago: '11m', lat: 38.3, lon: 142.4 };
const GLOBAL_HIGH   = { mag: 6.4, place: 'S. Sandwich Islands',  ago: '6h',  lat: -57.5, lon: -26.5 };

// Very coarse continent outlines [lat,lon] — enough to read as Earth on a
// ~150px globe. Rough by design (lo-fi vector look).
const CONTINENTS = [
  // North America
  [[70,-150],[68,-130],[60,-140],[55,-130],[48,-124],[40,-124],[32,-117],[23,-110],[18,-95],[25,-97],[30,-88],[28,-82],[25,-80],[35,-76],[45,-67],[50,-58],[60,-65],[68,-80],[73,-100],[72,-125],[70,-150]],
  // South America
  [[10,-75],[5,-78],[-5,-81],[-15,-75],[-25,-70],[-35,-72],[-45,-74],[-53,-70],[-50,-68],[-40,-62],[-30,-56],[-23,-43],[-13,-38],[-5,-35],[2,-50],[8,-60],[11,-72],[10,-75]],
  // Africa + Arabia
  [[35,-6],[32,10],[33,22],[31,32],[15,40],[12,51],[0,42],[-12,40],[-25,35],[-34,26],[-34,18],[-22,14],[-8,13],[4,9],[5,-4],[10,-15],[21,-17],[31,-10],[35,-6]],
  // Eurasia
  [[36,-9],[43,-9],[48,-5],[51,2],[58,5],[60,20],[66,25],[70,30],[68,55],[73,75],[75,100],[72,130],[66,170],[60,162],[55,158],[60,140],[52,140],[45,135],[40,123],[32,120],[22,108],[10,98],[8,80],[20,72],[25,60],[27,50],[30,48],[36,36],[40,28],[40,18],[44,12],[44,0],[40,-5],[36,-9]],
  // Australia
  [[-12,131],[-11,142],[-19,147],[-28,153],[-38,147],[-38,140],[-35,135],[-32,127],[-34,118],[-22,114],[-15,124],[-12,131]],
];

function GlobeCog({ size = 13, color }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" style={{ display: 'block' }}>
      <path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"
        fill={color} />
    </svg>
  );
}

// Canvas wireframe globe ----------------------------------------------------
function WireGlobe({ w, h, R, markers, palette }) {
  const ref = React.useRef(null);
  React.useEffect(() => {
    const cnv = ref.current;
    if (!cnv) return;
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    cnv.width = w * dpr; cnv.height = h * dpr;
    const ctx = cnv.getContext('2d');
    ctx.scale(dpr, dpr);
    const cx = w / 2, cy = h / 2;
    const TILT = 0.42;                   // axis tilt for a pleasant view
    const cosT = Math.cos(TILT), sinT = Math.sin(TILT);
    let rot = 0.6;
    let raf;
    const reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

    // project lat/lon (deg) -> {x,y,z(front>0),vis}
    const project = (lat, lon) => {
      const phi = lat * Math.PI / 180;
      const lam = lon * Math.PI / 180 + rot;
      let x = Math.cos(phi) * Math.sin(lam);
      let y = Math.sin(phi);
      let z = Math.cos(phi) * Math.cos(lam);
      const y2 = y * cosT - z * sinT;
      const z2 = y * sinT + z * cosT;
      return { x: cx + R * x, y: cy - R * y2, z: z2 };
    };

    const strokePoly = (pts, close, bright, dim) => {
      // pts: [[lat,lon]...]; split into front/back segments
      let prev = null;
      const arr = close ? pts.concat([pts[0]]) : pts;
      for (let i = 0; i < arr.length; i++) {
        const p = project(arr[i][0], arr[i][1]);
        if (prev) {
          const front = (prev.z > 0 && p.z > 0);
          const back  = (prev.z <= 0 && p.z <= 0);
          if (front || back) {
            ctx.beginPath();
            ctx.moveTo(prev.x, prev.y);
            ctx.lineTo(p.x, p.y);
            ctx.strokeStyle = front ? bright : dim;
            ctx.lineWidth = front ? 0.8 : 0.6;
            ctx.stroke();
          }
        }
        prev = p;
      }
    };

    const draw = () => {
      ctx.clearRect(0, 0, w, h);

      // globe disc fill + limb glow
      ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2);
      ctx.fillStyle = palette.globeFill; ctx.fill();
      ctx.shadowColor = palette.limb; ctx.shadowBlur = 8;
      ctx.lineWidth = 1.1; ctx.strokeStyle = palette.limb; ctx.stroke();
      ctx.shadowBlur = 0;

      const M = palette.mesh;
      const brightP = `rgba(${M},0.55)`, dimP = `rgba(${M},0.12)`;
      const brightM = `rgba(${M},0.42)`, dimM = `rgba(${M},0.10)`;

      // parallels every 30°
      for (let lat = -60; lat <= 60; lat += 30) {
        const pts = [];
        for (let lon = -180; lon <= 180; lon += 6) pts.push([lat, lon]);
        strokePoly(pts, false, lat === 0 ? `rgba(${M},0.7)` : brightP, dimP);
      }
      // meridians every 30°
      for (let lon = -180; lon < 180; lon += 30) {
        const pts = [];
        for (let lat = -90; lat <= 90; lat += 6) pts.push([lat, lon]);
        strokePoly(pts, false, brightM, dimM);
      }

      // continents
      for (const c of CONTINENTS) strokePoly(c, true, `rgba(${M},0.85)`, `rgba(${M},0.16)`);

      // epicenter markers — surface dot + radial spike + pulse
      const tnow = performance.now() / 1000;
      for (const mk of markers) {
        const p = project(mk.lat, mk.lon);
        if (p.z <= 0) continue;                       // back face — hidden
        // radial spike: from surface point outward (away from globe centre)
        const dx = p.x - cx, dy = p.y - cy;
        const len = Math.hypot(dx, dy) || 1;
        const ux = dx / len, uy = dy / len;
        const sLen = 12;
        ctx.beginPath();
        ctx.moveTo(p.x, p.y);
        ctx.lineTo(p.x + ux * sLen, p.y + uy * sLen);
        ctx.strokeStyle = mk.color; ctx.lineWidth = 1; ctx.stroke();
        // pulse ring
        const pulse = (Math.sin(tnow * 2 + mk.phase) + 1) / 2; // 0..1
        ctx.beginPath();
        ctx.arc(p.x, p.y, 2 + pulse * 5, 0, Math.PI * 2);
        ctx.strokeStyle = mk.color; ctx.globalAlpha = 0.5 * (1 - pulse);
        ctx.lineWidth = 0.8; ctx.stroke(); ctx.globalAlpha = 1;
        // core dot + tip
        ctx.beginPath(); ctx.arc(p.x, p.y, 2, 0, Math.PI * 2);
        ctx.fillStyle = mk.color; ctx.fill();
        ctx.beginPath(); ctx.arc(p.x + ux * sLen, p.y + uy * sLen, 1.4, 0, Math.PI * 2);
        ctx.fillStyle = mk.color; ctx.fill();
      }

      if (!reduce) { rot += 0.0035; raf = requestAnimationFrame(draw); }
    };
    draw();
    return () => raf && cancelAnimationFrame(raf);
  }, [w, h, R]);

  return <canvas ref={ref} style={{ width: w, height: h, display: 'block' }} />;
}

function LandGlobeFrame() {
  const t = LANDG;
  const W = SML.W, H = SML.H, HH = SML.HEADER_H;

  // ── Panel geometry (shared with rings frame) ───────────────────
  const pad = 6, gap = 5;
  const contentTop = HH + 4;
  const contentBot = H - pad;
  const leftX = pad, leftW = 106;
  const rightX = leftX + leftW + gap;
  const rightW = W - pad - rightX;
  const seismoH = 42;
  const dataTop = contentTop;
  const dataH = (contentBot - contentTop) - seismoH - gap;
  const seismoTop = contentBot - seismoH;

  const s = seisTrace(220, 31, 1);
  const L = GLOBAL_LATEST, Hi = GLOBAL_HIGH;
  // Globe fills the map panel
  const mpW = rightW - 2, mpH = (contentBot - contentTop) - 2;
  const R = 92;
  const seismoSvgW = leftW - 2, seismoSvgH = seismoH - 2;
  const markers = [
    { lat: L.lat,  lon: L.lon,  color: t.latest,  phase: 0 },
    { lat: Hi.lat, lon: Hi.lon, color: t.highest, phase: 1.6 },
  ];

  return (
    <div style={{ width: W, height: H, background: t.bg, color: t.ink, fontFamily: '"JetBrains Mono", ui-monospace, monospace', position: 'relative', overflow: 'hidden' }}>
      {/* HEADER */}
      <div style={{ height: HH, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 10px', fontSize: 10, letterSpacing: 1.3, fontWeight: 700, borderBottom: `1px solid ${t.edge}` }}>
        <span style={{ color: t.primary }}>◉ SEIS · GLOBAL</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          <span style={{ color: t.secondary }}>23:42 · WIFI</span>
          <span style={{ display: 'flex', color: t.secondary }}><GlobeCog color={t.secondary} /></span>
        </div>
      </div>

      {/* DATA PANEL — bordered, contents centred */}
      <div style={{
        position: 'absolute', left: leftX, top: dataTop, width: leftW, height: dataH,
        border: `1px solid ${t.edge}`, borderRadius: 3,
        display: 'flex', flexDirection: 'column',
      }}>
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', textAlign: 'center', padding: '2px 6px' }}>
          <div style={{ fontSize: 8, color: t.latest, letterSpacing: 1.4, fontWeight: 700 }}>◆ LATEST</div>
          <div style={{ fontSize: 18, fontWeight: 700, lineHeight: 1, color: t.latest, marginTop: 2, letterSpacing: -0.5 }}>M{L.mag.toFixed(1)}</div>
          <div style={{ fontSize: 11.5, color: t.ink, marginTop: 3, lineHeight: 1.15, fontFamily: '"Inter", sans-serif', fontWeight: 700, textWrap: 'balance' }}>{L.place}</div>
          <div style={{ fontSize: 8, color: t.secondary, marginTop: 2, letterSpacing: 0.6 }}>{L.ago.toUpperCase()} AGO</div>
        </div>
        <div style={{ height: 1, background: t.edgeDim, margin: '0 8px' }} />
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', textAlign: 'center', padding: '2px 6px' }}>
          <div style={{ fontSize: 8, color: t.highest, letterSpacing: 1.4, fontWeight: 700 }}>◆ 24H HIGH</div>
          <div style={{ fontSize: 18, fontWeight: 700, lineHeight: 1, color: t.highest, marginTop: 2, letterSpacing: -0.5 }}>M{Hi.mag.toFixed(1)}</div>
          <div style={{ fontSize: 11.5, color: t.ink, marginTop: 3, lineHeight: 1.15, fontFamily: '"Inter", sans-serif', fontWeight: 700, textWrap: 'balance' }}>{Hi.place}</div>
          <div style={{ fontSize: 8, color: t.secondary, marginTop: 2, letterSpacing: 0.6 }}>{Hi.ago.toUpperCase()} AGO</div>
        </div>
      </div>

      {/* SEISMO PANEL — bordered, below the data panel */}
      <div style={{
        position: 'absolute', left: leftX, top: seismoTop, width: leftW, height: seismoH,
        border: `1px solid ${t.edge}`, borderRadius: 3, overflow: 'hidden',
      }}>
        <svg width={seismoSvgW} height={seismoSvgH} style={{ display: 'block' }}>
          {[...Array(3)].map((_, i) => (
            <line key={i} x1={0} x2={seismoSvgW} y1={(i + 1) * seismoSvgH / 4} y2={(i + 1) * seismoSvgH / 4} stroke="#0e1c17" strokeWidth={0.3} />
          ))}
          {[...Array(11)].map((_, i) => {
            const x = i * seismoSvgW / 10;
            const major = i % 5 === 0;
            return <line key={'t' + i} x1={x} x2={x} y1={seismoSvgH / 2 - (major ? 3 : 1.5)} y2={seismoSvgH / 2 + (major ? 3 : 1.5)} stroke={t.secondary} strokeWidth={0.4} opacity={major ? 0.7 : 0.4} />;
          })}
          <line x1={0} x2={seismoSvgW} y1={seismoSvgH / 2} y2={seismoSvgH / 2} stroke="#20392b" strokeWidth={0.5} />
          <path d={tracePath(s, seismoSvgW, seismoSvgH, 0.28)} fill="none" stroke={t.primary} strokeWidth={1.8} opacity={0.3} style={{ filter: 'blur(1.3px)' }} />
          <path d={tracePath(s, seismoSvgW, seismoSvgH, 0.28)} fill="none" stroke={t.primary} strokeWidth={1} />
          <text x={3} y={9} fontSize={6.5} fill={t.sub} fontFamily="monospace" letterSpacing={0.5}>Z</text>
          <text x={seismoSvgW - 18} y={9} fontSize={6.5} fill={t.sub} fontFamily="monospace" letterSpacing={0.5}>60s</text>
        </svg>
      </div>

      {/* GLOBE PANEL — bordered */}
      <div style={{
        position: 'absolute', left: rightX, top: contentTop, width: rightW, height: contentBot - contentTop,
        border: `1px solid ${t.edge}`, borderRadius: 3, overflow: 'hidden',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}>
        <WireGlobe w={mpW} h={mpH} R={R} markers={markers} palette={t} />
      </div>
    </div>
  );
}

Object.assign(window, { LandGlobeFrame });
