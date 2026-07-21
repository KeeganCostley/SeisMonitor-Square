// Shared primitives for SeisMonitor 240x240 variants.

const SM = {
  W: 240, H: 240,
  HEADER_H: 18,
  DATA_X: 2, DATA_Y: 18, DATA_W: 76, DATA_H: 158,
  MAP_X: 82, MAP_Y: 18, MAP_W: 156, MAP_H: 158,
  SEISMO_X: 4, SEISMO_Y: 180, SEISMO_W: 232, SEISMO_H: 56,
};

// Landscape layout — board rotated (setRotation 1) → 320×240.
const SML = {
  W: 320, H: 240,
  HEADER_H: 22,
};

function seisTrace(n, seed = 7, intensity = 1) {
  const out = []; let s = seed;
  const rng = () => { s = (s * 9301 + 49297) % 233280; return (s / 233280) - 0.5; };
  for (let i = 0; i < n; i++) {
    const t = i / n;
    let y = rng() * 0.05;
    const p = Math.exp(-Math.pow((t - 0.32) * 20, 2));
    y += p * (Math.sin(i * 0.85) * 0.35 + rng() * 0.2);
    const sw = Math.exp(-Math.pow((t - 0.52) * 7, 2));
    y += sw * (Math.sin(i * 0.5) * 0.85 + Math.sin(i * 0.28) * 0.4 + rng() * 0.25);
    const coda = Math.exp(-Math.pow((t - 0.72) * 3.5, 2));
    y += coda * (Math.sin(i * 0.35) * 0.3 + rng() * 0.2);
    out.push(y * intensity);
  }
  return out;
}
function tracePath(samples, w, h, padY = 0.12) {
  const mid = h / 2;
  const max = Math.max(...samples.map(Math.abs)) || 1;
  const sc = (h / 2) * (1 - padY) / max;
  let d = '';
  for (let i = 0; i < samples.length; i++) {
    const x = (i / (samples.length - 1)) * w;
    const y = mid - samples[i] * sc;
    d += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + y.toFixed(1) + ' ';
  }
  return d;
}

const LATEST = { mag: 2.4, place: 'Porirua, New Zealand', ago: '4m', lat: -41.13, lon: 174.84 };
const HIGH24 = { mag: 4.1, place: '25 km NW Wellington',  ago: '3h', lat: -41.12, lon: 174.52 };

// =============================================================================
// NZ coastline — REAL lat/lon coordinates from the firmware (src/main.cpp).
// These are the exact polygons drawn on the ESP32 screen.
// Use NZ_PROJECT(w, h) to project them into pixel paths fitting any box.
// =============================================================================
const NZ_NORTH_LL = [
  [-34.42,172.68],[-34.45,173.05],[-34.87,173.38],[-35.22,174.05],
  [-35.83,174.55],[-36.30,174.80],[-36.63,174.83],[-36.84,174.80],
  [-37.05,175.30],[-36.50,175.35],[-36.85,175.72],[-37.22,175.92],
  [-37.63,176.18],[-37.97,177.00],[-37.69,178.55],[-38.67,178.02],
  [-39.10,177.85],[-39.48,176.92],[-39.65,177.10],[-40.35,176.62],
  [-40.90,176.20],[-41.45,175.40],[-41.30,174.78],[-41.15,174.85],
  [-40.88,175.00],[-40.30,175.00],[-39.93,175.05],[-39.50,174.25],
  [-39.28,173.75],[-39.05,174.05],[-38.70,174.55],[-38.30,174.65],
  [-37.80,174.75],[-37.40,174.65],[-37.02,174.52],[-36.85,174.42],
  [-36.40,174.13],[-35.93,173.85],[-35.55,173.45],[-35.20,173.10],
  [-34.75,172.78],[-34.42,172.68]
];
const NZ_SOUTH_LL = [
  [-40.50,172.68],[-40.65,172.90],[-40.78,172.62],[-40.95,173.00],
  [-41.10,173.45],[-41.10,174.03],[-41.22,174.15],[-41.35,174.30],
  [-41.72,174.18],[-42.15,173.88],[-42.42,173.68],[-42.85,173.45],
  [-43.30,172.88],[-43.55,172.75],[-43.62,172.95],[-43.83,173.08],
  [-43.82,172.65],[-44.10,172.00],[-44.38,171.50],[-44.72,171.18],
  [-45.10,170.98],[-45.52,170.68],[-45.78,170.72],[-45.88,170.73],
  [-45.92,170.50],[-46.22,170.05],[-46.45,169.45],[-46.67,168.36],
  [-46.60,168.00],[-46.40,167.55],[-46.15,166.60],[-45.75,166.50],
  [-45.30,166.85],[-44.63,167.85],[-44.15,168.08],[-43.98,168.62],
  [-43.88,169.05],[-43.47,170.18],[-42.72,170.97],[-42.45,171.21],
  [-41.75,171.60],[-41.25,172.10],[-40.78,172.15],[-40.50,172.68]
];
const NZ_STEWART_LL = [
  [-46.65,167.80],[-46.72,168.05],[-46.82,168.22],[-46.97,168.15],
  [-47.05,167.80],[-46.95,167.45],[-46.80,167.40],[-46.68,167.55],
  [-46.65,167.80]
];

// Project lat/lon polygons to fit box (w,h) with optional inner margin.
// Equirectangular w/ cos(centerLat) correction — same as firmware.
// Returns { paths: [pathStr…], project: (lat,lon)=>[x,y] }
function NZ_PROJECT(w, h, margin = 4) {
  const all = NZ_NORTH_LL.concat(NZ_SOUTH_LL, NZ_STEWART_LL);
  let latMin = Infinity, latMax = -Infinity, lonMin = Infinity, lonMax = -Infinity;
  for (const [lat, lon] of all) {
    if (lat < latMin) latMin = lat; if (lat > latMax) latMax = lat;
    if (lon < lonMin) lonMin = lon; if (lon > lonMax) lonMax = lon;
  }
  const latSpan = latMax - latMin;
  const lonSpan = lonMax - lonMin;
  const cLat = (latMin + latMax) / 2;
  const cosLat = Math.cos(cLat * Math.PI / 180);
  const effLon = lonSpan * cosLat;
  const innerW = w - margin * 2, innerH = h - margin * 2;
  const scale = Math.min(innerW / effLon, innerH / latSpan);
  const offX = margin + (innerW - scale * effLon) / 2;
  const offY = margin + (innerH - scale * latSpan) / 2;
  const project = (lat, lon) =>
    [offX + (lon - lonMin) * scale * cosLat,
     offY + (latMax - lat) * scale];
  const toPath = (poly) => {
    let d = '';
    for (let i = 0; i < poly.length; i++) {
      const [x, y] = project(poly[i][0], poly[i][1]);
      d += (i === 0 ? 'M' : 'L') + x.toFixed(2) + ',' + y.toFixed(2) + ' ';
    }
    return d + 'Z';
  };
  return {
    north: toPath(NZ_NORTH_LL),
    south: toPath(NZ_SOUTH_LL),
    stewart: toPath(NZ_STEWART_LL),
    project,
  };
}

// Legacy hand-drawn paths (used by older variants — point them at a default
// projection so they still look right).
const _NZ_DEFAULT = NZ_PROJECT(156, 158, 4);
const NZ_NORTH_PATH = _NZ_DEFAULT.north;
const NZ_SOUTH_PATH = _NZ_DEFAULT.south;
const NZ_STEWART_PATH = _NZ_DEFAULT.stewart;

Object.assign(window, { SM, SML, seisTrace, tracePath, LATEST, HIGH24,
  NZ_NORTH_LL, NZ_SOUTH_LL, NZ_STEWART_LL, NZ_PROJECT,
  NZ_NORTH_PATH, NZ_SOUTH_PATH, NZ_STEWART_PATH });
