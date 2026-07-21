// Screen 5 — SEISMIC ACTIVITY DETECTED (alert). Ten composition options.
// Soft HUD palette, 320×240 landscape. Single quake, "showing NOW", animated
// concentric activity rings. Header string is fixed: SEISMIC ACTIVITY DETECTED.
//
// Severity is carried by COLOUR. The brief's ramp is non-monotonic in
// brightness (M7 ends up the dimmest pixel on a black screen). Proposed ramp
// below climbs in BOTH luminance and saturation so worse = brighter = hotter,
// which reads correctly on an additive display. Old ramp kept for comparison.

const ALERT = {
  bg: '#03080a', ink: '#b8e6c4', primary: '#7fd69a',
  secondary: '#4a8a68', sub: '#355a48', rule: '#0e1a16', edge: '#234a36',
};

// PROPOSED severity ramp — monotonic luminance + saturation.
function sevColor(m) {
  if (m >= 7.0) return '#ff5d4d';   // hot red — brightest, most urgent
  if (m >= 6.0) return '#ff9538';   // orange
  if (m >= 5.0) return '#ffcf47';   // amber
  if (m >= 4.0) return '#cfe25f';   // chartreuse
  return '#7fd69a';                 // calm phosphor green (routine)
}
// Firmware's current ramp (for the comparison note).
function sevColorOld(m) {
  if (m >= 7.0) return '#a54142';
  if (m >= 6.0) return '#ce615a';
  if (m >= 5.0) return '#add2e6';
  if (m >= 4.0) return '#7bb6d6';
  return '#7fd69a';
}

const HEADER = 'SEISMIC ACTIVITY DETECTED';

// sample states from the brief
const Q_ROUTINE = { mag: 2.4, place: 'Porirua, New Zealand',   depth: 12 };
const Q_NOTABLE = { mag: 5.5, place: '35km E of Cheviot',      depth: 29 };
const Q_SEVERE  = { mag: 7.1, place: 'Off E. Honshu, Japan',   depth: 38 };
const Q_LONG    = { mag: 3.9, place: '20km SW of Tokomaru Bay', depth: 45 };

// inject shared keyframes once
(function () {
  if (document.getElementById('alert-anim')) return;
  const st = document.createElement('style');
  st.id = 'alert-anim';
  st.textContent = `
  @keyframes alRing { 0%{transform:scale(.06);opacity:.85} 100%{transform:scale(1);opacity:0} }
  @keyframes alRingHard { 0%{transform:scale(.06);opacity:1} 70%{opacity:.5} 100%{transform:scale(1);opacity:0} }
  @keyframes alPulse { 0%,100%{opacity:.92;transform:scale(1)} 50%{opacity:1;transform:scale(1.03)} }
  @keyframes alBlink { 0%,45%{opacity:1} 55%,100%{opacity:.25} }
  @keyframes alSweep { 0%{transform:rotate(0)} 100%{transform:rotate(360deg)} }
  @keyframes alScan { 0%{transform:translateX(-30%)} 100%{transform:translateX(320%)} }
  @media (prefers-reduced-motion: reduce){ .al-an{animation:none!important} }
  `;
  document.head.appendChild(st);
})();

// Concentric expanding rings emanating from (ox,oy) px.
function Rings({ ox, oy, color, count = 3, maxR = 170, dur = 2.6, thick = 1.5, hard = false, delayBase = 0 }) {
  return (
    <div style={{ position: 'absolute', left: ox, top: oy, pointerEvents: 'none' }}>
      {Array.from({ length: count }).map((_, i) => (
        <span key={i} className="al-an" style={{
          position: 'absolute', left: -maxR, top: -maxR, width: maxR * 2, height: maxR * 2,
          borderRadius: '50%', border: `${thick}px solid ${color}`,
          animation: `${hard ? 'alRingHard' : 'alRing'} ${dur}s cubic-bezier(0,.45,.5,1) infinite`,
          animationDelay: `${delayBase + i * dur / count}s`,
        }} />
      ))}
      {/* origin core */}
      <span style={{ position: 'absolute', left: -3, top: -3, width: 6, height: 6, borderRadius: '50%', background: color, boxShadow: `0 0 6px ${color}` }} />
    </div>
  );
}

const shell = (extra) => ({
  width: SML.W, height: SML.H, background: ALERT.bg, color: ALERT.ink,
  fontFamily: '"JetBrains Mono", ui-monospace, monospace', position: 'relative', overflow: 'hidden',
  ...extra,
});
const mag = (m) => 'M' + m.toFixed(1);

// ── Option 1 — Shockwave, off-centre epicenter (lower-right) ───────────────
function Alert01({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={244} oy={168} color={c} count={4} maxR={230} dur={2.8} thick={1.5} />
      <div style={{ position: 'absolute', top: 14, left: 16, fontSize: 9, letterSpacing: 1.6, color: c, fontWeight: 700 }}>SEISMIC ACTIVITY</div>
      <div style={{ position: 'absolute', top: 26, left: 16, fontSize: 9, letterSpacing: 1.6, color: c, fontWeight: 700 }}>DETECTED</div>
      <div style={{ position: 'absolute', top: 58, left: 14, fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 88, lineHeight: .8, color: c, letterSpacing: -3 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 40, left: 16, fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 15, color: ALERT.ink, maxWidth: 210 }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 20, left: 16, fontSize: 9, letterSpacing: 1, color: ALERT.secondary }}>
        <span style={{ color: c }}>● </span>NOW · {q.depth}KM DEEP
      </div>
    </div>
  );
}

// ── Option 2 — Hero magnitude dead-centre inside rings ─────────────────────
function Alert02({ q = Q_ROUTINE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell({ textAlign: 'center' })}>
      <Rings ox={160} oy={128} color={c} count={3} maxR={150} dur={2.6} thick={1.5} />
      <div style={{ position: 'absolute', top: 16, left: 0, right: 0, fontSize: 9.5, letterSpacing: 2, color: c, fontWeight: 700 }}>{HEADER}</div>
      <div style={{ position: 'absolute', top: 24, left: 0, right: 0, height: 1 }}><div style={{ width: 150, height: 1, background: c, margin: '8px auto 0', opacity: .5 }} /></div>
      <div style={{ position: 'absolute', top: 62, left: 0, right: 0, fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 76, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 34, left: 0, right: 0, fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 18, left: 0, right: 0, fontSize: 8.5, letterSpacing: 1.4, color: ALERT.secondary }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// ── Option 3 — Left data rail, rings sweep from the right edge ──────────────
function Alert03({ q = Q_LONG }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={300} oy={120} color={c} count={4} maxR={250} dur={3} thick={1.4} />
      <div style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: 150, background: 'linear-gradient(90deg,#03080a 70%,rgba(3,8,10,0))', padding: '18px 14px' }}>
        <div style={{ fontSize: 8.5, letterSpacing: 1.5, color: c, fontWeight: 700, lineHeight: 1.4 }}>SEISMIC<br/>ACTIVITY<br/>DETECTED</div>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 62, lineHeight: .9, color: c, letterSpacing: -2, marginTop: 14 }} className="al-an">{mag(q.mag)}</div>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 13, color: ALERT.ink, marginTop: 10, lineHeight: 1.15 }}>{q.place}</div>
        <div style={{ fontSize: 8, letterSpacing: 1, color: ALERT.secondary, marginTop: 6 }}><span style={{ color: c }}>●</span> NOW · {q.depth}KM DEEP</div>
      </div>
    </div>
  );
}

// ── Option 4 — Diagonal corner burst, big severe magnitude ─────────────────
function Alert04({ q = Q_SEVERE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={20} oy={24} color={c} count={5} maxR={340} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 16, right: 16, textAlign: 'right', fontSize: 9, letterSpacing: 1.5, color: c, fontWeight: 700, lineHeight: 1.35 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', bottom: 44, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 96, lineHeight: .78, color: c, letterSpacing: -4 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 26, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 12, right: 16, textAlign: 'right', fontSize: 8.5, letterSpacing: 1, color: c }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// ── Option 5 — Instrument: rings encode magnitude & depth, annotated ───────
function Alert05({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  const ringCount = Math.max(2, Math.round(q.mag));      // count ~ magnitude
  const originR = 6 + q.depth / 6;                        // origin size ~ depth
  return (
    <div style={shell()}>
      <div style={{ position: 'absolute', left: 160, top: 132 }}>
        {Array.from({ length: ringCount }).map((_, i) => (
          <span key={i} className="al-an" style={{ position: 'absolute', left: -170, top: -170, width: 340, height: 340, borderRadius: '50%', border: `1.3px solid ${c}`, animation: `alRing 2.6s cubic-bezier(0,.45,.5,1) infinite`, animationDelay: `${i * 2.6 / ringCount}s` }} />
        ))}
        <span style={{ position: 'absolute', left: -originR, top: -originR, width: originR * 2, height: originR * 2, borderRadius: '50%', border: `1px solid ${c}`, background: 'rgba(255,255,255,0.04)' }} />
      </div>
      <div style={{ position: 'absolute', top: 14, left: 14, fontSize: 9, letterSpacing: 1.5, color: c, fontWeight: 700 }}>SEISMIC ACTIVITY DETECTED</div>
      <div style={{ position: 'absolute', top: 40, left: 14, fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 54, color: c, letterSpacing: -1.5 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', top: 40, right: 14, textAlign: 'right', fontSize: 8, letterSpacing: 1, color: ALERT.sub, lineHeight: 1.5 }}>
        RINGS = MAG<br/>ORIGIN = DEPTH
      </div>
      <div style={{ position: 'absolute', bottom: 34, left: 14, fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 16, left: 14, fontSize: 8.5, letterSpacing: 1, color: ALERT.secondary }}>NOW · {q.depth}KM DEEP · M{q.mag.toFixed(1)}</div>
    </div>
  );
}

// ── Option 6 — Two-line stacked header top, magnitude baseline bottom ──────
function Alert06({ q = Q_ROUTINE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={160} oy={150} color={c} count={3} maxR={180} dur={2.8} thick={1.4} />
      <div style={{ position: 'absolute', top: 18, left: 0, right: 0, textAlign: 'center' }}>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 22, letterSpacing: 1, color: c, lineHeight: 1 }}>SEISMIC ACTIVITY</div>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 22, letterSpacing: 6, color: c, lineHeight: 1.1 }}>DETECTED</div>
      </div>
      <div style={{ position: 'absolute', bottom: 40, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 60, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 22, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 13, color: ALERT.ink }}>{q.place} <span style={{ color: ALERT.secondary, fontFamily: '"JetBrains Mono",monospace', fontWeight: 400 }}>· {q.depth}KM · NOW</span></div>
    </div>
  );
}

// ── Option 7 — Segmented smooth arcs (radar rings w/ gaps) ─────────────────
function Alert07({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  const arcs = [44, 74, 104, 134];
  return (
    <div style={shell()}>
      <svg width={320} height={240} style={{ position: 'absolute', inset: 0 }}>
        {arcs.map((r, i) => (
          <circle key={i} cx={160} cy={132} r={r} fill="none" stroke={c}
            strokeWidth={1.4} strokeDasharray={`${r*1.3},${r*0.7}`} opacity={0.75 - i * 0.14}
            transform={`rotate(${i * 24} 160 132)`}>
            <animateTransform attributeName="transform" type="rotate"
              from={`${i*24} 160 132`} to={`${i*24 + (i%2?-360:360)} 160 132`}
              dur={`${6 + i*2}s`} repeatCount="indefinite" />
          </circle>
        ))}
        <circle cx={160} cy={132} r={3} fill={c} />
      </svg>
      <div style={{ position: 'absolute', top: 15, left: 0, right: 0, textAlign: 'center', fontSize: 9.5, letterSpacing: 2, color: c, fontWeight: 700 }}>{HEADER}</div>
      <div style={{ position: 'absolute', top: 96, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 64, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 30, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 14, left: 0, right: 0, textAlign: 'center', fontSize: 8.5, letterSpacing: 1.4, color: ALERT.secondary }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// ── Option 8 — Rotating radar sweep + epicenter ────────────────────────────
function Alert08({ q = Q_LONG }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <div style={{ position: 'absolute', left: 220, top: 128, width: 0, height: 0 }}>
        {[50, 90, 130, 170].map((r, i) => (
          <span key={i} style={{ position: 'absolute', left: -r, top: -r, width: r * 2, height: r * 2, borderRadius: '50%', border: `1px solid ${ALERT.edge}` }} />
        ))}
        <div className="al-an" style={{ position: 'absolute', left: -170, top: -170, width: 340, height: 340, borderRadius: '50%', background: `conic-gradient(${c}55, transparent 45%)`, animation: 'alSweep 3.2s linear infinite' }} />
        <span style={{ position: 'absolute', left: -3, top: -3, width: 6, height: 6, borderRadius: '50%', background: c, boxShadow: `0 0 6px ${c}` }} />
      </div>
      <div style={{ position: 'absolute', top: 16, left: 16, fontSize: 9, letterSpacing: 1.5, color: c, fontWeight: 700, lineHeight: 1.4 }}>SEISMIC<br/>ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', top: 118, left: 16, fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 58, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 30, left: 16, fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 13, color: ALERT.ink, maxWidth: 200 }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 14, left: 16, fontSize: 8.5, letterSpacing: 1, color: ALERT.secondary }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// ── Option 9 — Waveform burst integrated with rings ────────────────────────
function Alert09({ q = Q_LONG }) {
  const c = sevColor(q.mag);
  const s = seisTrace(200, 12, 1);
  return (
    <div style={shell()}>
      <Rings ox={160} oy={96} color={c} count={3} maxR={130} dur={2.6} thick={1.3} />
      <div style={{ position: 'absolute', top: 14, left: 0, right: 0, textAlign: 'center', fontSize: 9.5, letterSpacing: 2, color: c, fontWeight: 700 }}>{HEADER}</div>
      <div style={{ position: 'absolute', top: 60, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 800, fontSize: 52, color: c, letterSpacing: -1.5 }} className="al-an">{mag(q.mag)}</div>
      {/* hard waveform burst */}
      <svg width={320} height={44} style={{ position: 'absolute', top: 132, left: 0 }}>
        <line x1={0} x2={320} y1={22} y2={22} stroke={ALERT.rule} />
        <path d={tracePath(s, 320, 44, 0.15)} fill="none" stroke={c} strokeWidth={1.4} />
      </svg>
      <div style={{ position: 'absolute', bottom: 30, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 14, left: 0, right: 0, textAlign: 'center', fontSize: 8.5, letterSpacing: 1.4, color: ALERT.secondary }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// ── Option 10 — Minimal restrained pulse (the routine case, done calm) ─────
function Alert10({ q = Q_ROUTINE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={160} oy={120} color={c} count={2} maxR={110} dur={3.2} thick={1.2} />
      <div style={{ position: 'absolute', top: 20, left: 0, right: 0, textAlign: 'center', fontSize: 8.5, letterSpacing: 2.5, color: ALERT.secondary, fontWeight: 700 }}>{HEADER}</div>
      <div style={{ position: 'absolute', top: 0, bottom: 0, left: 0, right: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center' }}>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 46, color: c, letterSpacing: -1 }} className="al-an">{mag(q.mag)}</div>
      </div>
      <div style={{ position: 'absolute', bottom: 32, left: 0, right: 0, textAlign: 'center', fontFamily: '"Inter",sans-serif', fontWeight: 600, fontSize: 13, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 16, left: 0, right: 0, textAlign: 'center', fontSize: 8, letterSpacing: 1.4, color: ALERT.sub }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

Object.assign(window, {
  ALERT, sevColor, sevColorOld, HEADER,
  Q_ROUTINE, Q_NOTABLE, Q_SEVERE, Q_LONG, Rings,
  Alert01, Alert02, Alert03, Alert04, Alert05,
  Alert06, Alert07, Alert08, Alert09, Alert10,
});

// ===========================================================================
// Diagonal corner burst — iterations (the chosen direction). Rings originate
// from the top-left corner; content anchored bottom-/top-right. Magnitude
// pulled back from 96px so M7.1 doesn't overwhelm the panel.
// ===========================================================================

// 4A — chosen: rings from BOTTOM-left, larger header, magnitude 72px.
function Alert04a({ q = Q_SEVERE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={16} oy={224} color={c} count={5} maxR={360} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 16, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontSize: 15, letterSpacing: 0.5, color: c, fontWeight: 800, lineHeight: 1.15 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', bottom: 54, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 600, fontSize: 60, lineHeight: .8, color: c, letterSpacing: -1.5 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 30, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 22, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 12, right: 16, textAlign: 'right', fontSize: 13, letterSpacing: 1, color: c }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// 4B — magnitude + label share a baseline block bottom-right; hairline rule.
function Alert04b({ q = Q_SEVERE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={18} oy={22} color={c} count={5} maxR={330} dur={2.4} thick={1.8} hard />
      <div style={{ position: 'absolute', top: 16, right: 18, textAlign: 'right', fontSize: 9, letterSpacing: 1.6, color: c, fontWeight: 700, lineHeight: 1.35 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', bottom: 20, right: 18, textAlign: 'right' }}>
        <div style={{ fontSize: 8, letterSpacing: 2, color: ALERT.secondary, marginBottom: 2 }}>MAGNITUDE · NOW</div>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 68, lineHeight: .82, color: c, letterSpacing: -3 }} className="al-an">{mag(q.mag)}</div>
        <div style={{ width: 150, height: 1, background: c, opacity: .5, margin: '6px 0 6px auto' }} />
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
        <div style={{ fontSize: 8, letterSpacing: 1, color: ALERT.secondary, marginTop: 2 }}>{q.depth}KM DEEP</div>
      </div>
    </div>
  );
}

// 4C — rings from BOTTOM-left corner, content top-right (flipped diagonal).
function Alert04c({ q = Q_SEVERE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={16} oy={220} color={c} count={5} maxR={330} dur={2.5} thick={1.8} hard />
      <div style={{ position: 'absolute', top: 18, right: 18, textAlign: 'right', fontSize: 9, letterSpacing: 1.6, color: c, fontWeight: 700, lineHeight: 1.35 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', top: 52, right: 18, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 72, lineHeight: .82, color: c, letterSpacing: -3 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', top: 138, right: 18, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', top: 156, right: 18, textAlign: 'right', fontSize: 8.5, letterSpacing: 1, color: c }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// 4D — epicenter marker at the corner origin; magnitude left, place along foot.
function Alert04d({ q = Q_SEVERE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={26} oy={30} color={c} count={5} maxR={340} dur={2.4} thick={1.8} hard />
      <div style={{ position: 'absolute', top: 20, right: 18, textAlign: 'right', fontSize: 9, letterSpacing: 1.6, color: c, fontWeight: 700, lineHeight: 1.35 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', bottom: 46, left: 20, fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 76, lineHeight: .8, color: c, letterSpacing: -3 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 20, left: 22, right: 18, display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
        <span style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</span>
        <span style={{ fontSize: 8.5, letterSpacing: 1, color: ALERT.secondary }}>NOW · {q.depth}KM</span>
      </div>
    </div>
  );
}

Object.assign(window, { Alert04a, Alert04b, Alert04c, Alert04d });

// Generic 4A composition with a configurable burst origin, for exploring
// where the shockwave should emanate from.
function AlertBurst({ q = Q_SEVERE, ox, oy }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={ox} oy={oy} color={c} count={5} maxR={360} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 16, right: 16, textAlign: 'right', fontSize: 9, letterSpacing: 1.5, color: c, fontWeight: 700, lineHeight: 1.35 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', bottom: 42, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 72, lineHeight: .8, color: c, letterSpacing: -3 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 26, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 14, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 12, right: 16, textAlign: 'right', fontSize: 8.5, letterSpacing: 1, color: c }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

Object.assign(window, { AlertBurst });

// ===========================================================================
// 4A layout variations — bottom-left burst retained, magnitude pulled back
// (~52–58px), content rearranged. Shown at M5.5.
// ===========================================================================

// L1 — right column: header, magnitude, place, depth all right-aligned stack.
function Alert4L1({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={16} oy={224} color={c} count={5} maxR={360} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 0, bottom: 0, right: 16, width: 220, display: 'flex', flexDirection: 'column', alignItems: 'flex-end', justifyContent: 'center', textAlign: 'right', gap: 4 }}>
        <div style={{ fontFamily: '"Inter",sans-serif', fontSize: 14, letterSpacing: 0.5, color: c, fontWeight: 800, lineHeight: 1.15 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 56, lineHeight: .85, color: c, letterSpacing: -2, marginTop: 4 }} className="al-an">{mag(q.mag)}</div>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 18, color: ALERT.ink }}>{q.place}</div>
        <div style={{ fontSize: 11, letterSpacing: 1, color: c }}>NOW · {q.depth}KM DEEP</div>
      </div>
    </div>
  );
}

// L2 — header top-right; magnitude + place share a bottom baseline (mag left).
function Alert4L2({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={16} oy={224} color={c} count={5} maxR={360} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 16, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontSize: 15, letterSpacing: 0.5, color: c, fontWeight: 800, lineHeight: 1.15 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', bottom: 18, left: 20, right: 16, display: 'flex', alignItems: 'flex-end', justifyContent: 'space-between' }}>
        <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 54, lineHeight: .8, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
        <div style={{ textAlign: 'right' }}>
          <div style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 18, color: ALERT.ink }}>{q.place}</div>
          <div style={{ fontSize: 11, letterSpacing: 1, color: c, marginTop: 3 }}>NOW · {q.depth}KM DEEP</div>
        </div>
      </div>
    </div>
  );
}

// L3 — centred stack over the bottom-left burst; compact magnitude.
function Alert4L3({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell({ textAlign: 'center' })}>
      <Rings ox={16} oy={224} color={c} count={5} maxR={360} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 20, left: 0, right: 0, fontFamily: '"Inter",sans-serif', fontSize: 16, letterSpacing: 0.5, color: c, fontWeight: 800 }}>SEISMIC ACTIVITY DETECTED</div>
      <div style={{ position: 'absolute', top: 78, left: 0, right: 0, fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 58, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 34, left: 0, right: 0, fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 18, color: ALERT.ink }}>{q.place}</div>
      <div style={{ position: 'absolute', bottom: 16, left: 0, right: 0, fontSize: 11, letterSpacing: 1.2, color: c }}>NOW · {q.depth}KM DEEP</div>
    </div>
  );
}

// L4 — header top-right, magnitude mid-right, place + depth pinned bottom band.
function Alert4L4({ q = Q_NOTABLE }) {
  const c = sevColor(q.mag);
  return (
    <div style={shell()}>
      <Rings ox={16} oy={224} color={c} count={5} maxR={360} dur={2.4} thick={2} hard />
      <div style={{ position: 'absolute', top: 16, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontSize: 15, letterSpacing: 0.5, color: c, fontWeight: 800, lineHeight: 1.15 }}>SEISMIC ACTIVITY<br/>DETECTED</div>
      <div style={{ position: 'absolute', top: 96, right: 16, textAlign: 'right', fontFamily: '"Inter",sans-serif', fontWeight: 900, fontSize: 56, lineHeight: .8, color: c, letterSpacing: -2 }} className="al-an">{mag(q.mag)}</div>
      <div style={{ position: 'absolute', bottom: 14, left: 20, right: 16, borderTop: `1px solid ${ALERT.rule}`, paddingTop: 8, display: 'flex', justifyContent: 'space-between', alignItems: 'baseline' }}>
        <span style={{ fontFamily: '"Inter",sans-serif', fontWeight: 700, fontSize: 18, color: ALERT.ink }}>{q.place}</span>
        <span style={{ fontSize: 11, letterSpacing: 1, color: c }}>NOW · {q.depth}KM</span>
      </div>
    </div>
  );
}

Object.assign(window, { Alert4L1, Alert4L2, Alert4L3, Alert4L4 });
