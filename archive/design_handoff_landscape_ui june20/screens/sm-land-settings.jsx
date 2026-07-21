// Landscape — SETTINGS screen (soft HUD theme). Opened from the header cog.
// Two things only: REGION selection (left) and WiFi connection details (right).

const LANDS = {
  bg: '#03080a',
  ink: '#b8e6c4',
  primary: '#7fd69a',
  secondary: '#4a8a68',
  sub: '#355a48',
  rule: '#0e1a16',
  accent: '#9de8b2',
  sel: '#0f2118',          // selected row fill
  selEdge: '#9de8b2',
  ok: '#9de8b2',
};

function SetCog({ size = 13, color }) {
  return (
    <svg width={size} height={size} viewBox="0 0 24 24" fill="none" style={{ display: 'block' }}>
      <path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"
        fill={color} />
    </svg>
  );
}

// WiFi signal bars (4)
function WifiBars({ bars = 3, color, dim }) {
  return (
    <svg width={20} height={14} viewBox="0 0 20 14" style={{ display: 'block' }}>
      {[0, 1, 2, 3].map(i => (
        <rect key={i} x={i * 5} y={12 - (4 + i * 3)} width={3.4} height={4 + i * 3}
          fill={i < bars ? color : dim} rx={0.5} />
      ))}
    </svg>
  );
}

function LandSettingsFrame() {
  const t = LANDS;
  const W = SML.W, H = SML.H, HH = SML.HEADER_H;
  const regions = ['New Zealand', 'Japan', 'California', 'China', 'Global'];
  const selected = 'New Zealand';
  const colX = 160;                          // divider x

  const wifi = { ssid: 'Aratau-5G', rssi: -54, ip: '192.168.1.42', bars: 3 };

  return (
    <div style={{ width: W, height: H, background: t.bg, color: t.ink, fontFamily: '"JetBrains Mono", ui-monospace, monospace', position: 'relative', overflow: 'hidden' }}>
      {/* HEADER */}
      <div style={{ height: HH, display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '0 10px', fontSize: 10, letterSpacing: 1.3, fontWeight: 700, borderBottom: `1px solid ${t.rule}` }}>
        <span style={{ color: t.primary }}>‹ SETTINGS</span>
        <span style={{ display: 'flex', color: t.accent }}><SetCog color={t.accent} /></span>
      </div>

      {/* divider */}
      <div style={{ position: 'absolute', left: colX, top: HH, width: 1, height: H - HH, background: t.rule }} />

      {/* ── REGION (left) ── */}
      <div style={{ position: 'absolute', left: 12, top: HH + 12, width: colX - 24 }}>
        <div style={{ fontSize: 8, color: t.secondary, letterSpacing: 1.6, fontWeight: 700, marginBottom: 8 }}>REGION</div>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4 }}>
          {regions.map((r) => {
            const on = r === selected;
            return (
              <div key={r} style={{
                display: 'flex', alignItems: 'center', gap: 8,
                height: 26, padding: '0 8px', borderRadius: 4,
                background: on ? t.sel : 'transparent',
                boxShadow: on ? `inset 2px 0 0 ${t.selEdge}` : 'none',
              }}>
                <span style={{ fontSize: 10, color: on ? t.accent : t.sub }}>{on ? '◉' : '○'}</span>
                <span style={{ fontSize: 13, fontFamily: '"Inter", sans-serif', fontWeight: on ? 700 : 500, color: on ? t.ink : t.secondary }}>{r}</span>
              </div>
            );
          })}
        </div>
      </div>

      {/* ── WIFI (right) ── */}
      <div style={{ position: 'absolute', left: colX + 16, top: HH + 12, width: W - colX - 28 }}>
        <div style={{ fontSize: 8, color: t.secondary, letterSpacing: 1.6, fontWeight: 700, marginBottom: 10 }}>WIFI CONNECTION</div>

        {/* Network */}
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between' }}>
          <div>
            <div style={{ fontSize: 7.5, color: t.sub, letterSpacing: 1, marginBottom: 2 }}>NETWORK</div>
            <div style={{ fontSize: 15, fontFamily: '"Inter", sans-serif', fontWeight: 700, color: t.ink }}>{wifi.ssid}</div>
          </div>
          <WifiBars bars={wifi.bars} color={t.accent} dim={t.rule} />
        </div>

        <div style={{ height: 1, background: t.rule, margin: '12px 0' }} />

        {/* Signal */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 10 }}>
          <span style={{ fontSize: 7.5, color: t.sub, letterSpacing: 1 }}>SIGNAL</span>
          <span style={{ fontSize: 12, color: t.primary }}>{wifi.rssi} dBm</span>
        </div>

        {/* IP */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'baseline', marginBottom: 10 }}>
          <span style={{ fontSize: 7.5, color: t.sub, letterSpacing: 1 }}>IP ADDR</span>
          <span style={{ fontSize: 12, color: t.primary }}>{wifi.ip}</span>
        </div>

        {/* Status */}
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <span style={{ fontSize: 7.5, color: t.sub, letterSpacing: 1 }}>STATUS</span>
          <span style={{ display: 'flex', alignItems: 'center', gap: 5, fontSize: 11, color: t.ok, fontWeight: 700, letterSpacing: 0.5 }}>
            <span style={{ width: 6, height: 6, borderRadius: '50%', background: t.ok, boxShadow: `0 0 5px ${t.ok}` }} />
            CONNECTED
          </span>
        </div>
      </div>

      {/* footer hint */}
      <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: 18, borderTop: `1px solid ${t.rule}`, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 7.5, color: t.sub, letterSpacing: 1 }}>
        TAP REGION TO SWITCH · TAP GEAR TO CLOSE
      </div>
    </div>
  );
}

Object.assign(window, { LandSettingsFrame });
