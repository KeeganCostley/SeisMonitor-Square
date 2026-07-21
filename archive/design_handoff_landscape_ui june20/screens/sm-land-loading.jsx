// Landscape — LOADING / BOOT screens (soft HUD theme). Shown on power-up and
// while connecting / syncing. Centred wordmark with a pulsing ring nod to the
// rings layout, animated status line, and an indeterminate scan bar.
// `stage` prop: 'wifi' (connecting) | 'sync' (fetching quakes).

const LANDLD = {
  bg: '#03080a',
  ink: '#b8e6c4',
  primary: '#7fd69a',
  secondary: '#4a8a68',
  sub: '#355a48',
  rule: '#0e1a16',
  accent: '#9de8b2',
  ring: '#1b3327',
};

function LandLoadingFrame({ stage = 'wifi' }) {
  const t = LANDLD;
  const W = SML.W, H = SML.H;
  const [dots, setDots] = React.useState('');
  React.useEffect(() => {
    const id = setInterval(() => setDots(d => (d.length >= 3 ? '' : d + '·')), 450);
    return () => clearInterval(id);
  }, []);

  const status = stage === 'sync' ? 'FETCHING QUAKE DATA' : 'CONNECTING TO WIFI';

  return (
    <div style={{ width: W, height: H, background: t.bg, color: t.ink, fontFamily: '"JetBrains Mono", ui-monospace, monospace', position: 'relative', overflow: 'hidden' }}>
      <style dangerouslySetInnerHTML={{ __html: `
        @keyframes smld-pulse { 0%,100% { opacity:.55; transform:scale(0.96);} 50% { opacity:1; transform:scale(1.04);} }
        @keyframes smld-ring  { 0% { transform:scale(0.4); opacity:.55;} 100% { transform:scale(1.6); opacity:0;} }
        @keyframes smld-scan  { 0% { left:-40%; } 100% { left:100%; } }
        @media (prefers-reduced-motion: reduce){
          .smld-an, .smld-ring1, .smld-ring2, .smld-scanbar { animation: none !important; }
        }
      `}} />

      {/* expanding rings + wordmark, centred */}
      <div style={{ position: 'absolute', inset: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center' }}>
        <div style={{ position: 'relative', width: 92, height: 92, display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: 6 }}>
          <span className="smld-ring1" style={{ position: 'absolute', width: 70, height: 70, borderRadius: '50%', border: `1px solid ${t.accent}`, animation: 'smld-ring 2.4s ease-out infinite' }} />
          <span className="smld-ring2" style={{ position: 'absolute', width: 70, height: 70, borderRadius: '50%', border: `1px solid ${t.accent}`, animation: 'smld-ring 2.4s ease-out infinite', animationDelay: '1.2s' }} />
          <span className="smld-an" style={{ fontSize: 40, color: t.accent, animation: 'smld-pulse 1.8s ease-in-out infinite', textShadow: `0 0 14px ${t.accent}` }}>◉</span>
        </div>

        <div style={{ fontSize: 18, letterSpacing: 6, fontWeight: 700, color: t.ink, paddingLeft: 6 }}>SEISMONITOR</div>
        <div style={{ fontSize: 8, letterSpacing: 3, color: t.secondary, marginTop: 4 }}>EARTHQUAKE MONITOR</div>

        {/* status */}
        <div style={{ marginTop: 22, fontSize: 9, letterSpacing: 1.6, color: t.primary, height: 12 }}>
          {status} <span style={{ color: t.accent }}>{dots}</span>
        </div>

        {/* indeterminate scan bar */}
        <div style={{ position: 'relative', width: 150, height: 2, marginTop: 10, background: t.rule, overflow: 'hidden', borderRadius: 2 }}>
          <span className="smld-scanbar" style={{ position: 'absolute', top: 0, width: '40%', height: '100%', background: `linear-gradient(90deg, transparent, ${t.accent}, transparent)`, animation: 'smld-scan 1.4s ease-in-out infinite' }} />
        </div>
      </div>

      {/* footer chrome */}
      <div style={{ position: 'absolute', left: 10, bottom: 8, fontSize: 7.5, letterSpacing: 1, color: t.sub }}>v3.0</div>
      <div style={{ position: 'absolute', right: 10, bottom: 8, fontSize: 7.5, letterSpacing: 1, color: t.sub }}>ES3C28P</div>
    </div>
  );
}

Object.assign(window, { LandLoadingFrame });
