/* page.js: outatime's T1 art, from the owner's design reference F:\OP1firmware\artwork\outatime\v3 (outatime3.js:
   "here is the design reference i made for you previously"), its instruments taken over by meaning (the owner,
   2026-10-26, plan §6 point 20): the time circuits are year's (blue: DEST the destination, the PRES row the first
   arm drifting with speed, LAST the year before), the speedometer, road and wheels speed's (green), the flux
   capacitor power's (white: the rods are the three arms, the readout gigawatts), the fire trail's (orange: each note
   lights it, it burns down as the ignition does). This file draws what never moves; ota_page.c draws the rest with
   the places written next to the art (build.js). op1draw.js, opfont.json and car.json are copies of
   artwork/lib/op1draw.js, opfont.json and artwork/outatime/v3/tools/car.json (the owner's time-machine car) of 2026-10-23.
*/
'use strict';
const D = require('./op1draw.js');
const CAR = require('./car.json');
const { C, line, path, fillPath, dot, ring, group, roundRect, textWidth, text, svgDoc } = D;

// the OP-1's palette (stock colors.svg) and v3's roles; colours the user's picks change are placeholders the page
// sets (opus_art_colour_slots): the fourth encoder's (the car's accents, the 88 tick), the second's dim (PRES's line),
// the first's (the time cables' steady glow: the owner, 2026-10-26, "the blue cables ... don't inherit the theme's
// colors")
const K = {
  dkblue: C.dkblue, offblue: C.offblue, lavender: C.lavender, purplegrey: C.purplegrey, lightpurple: C.lightpurple,
  grey: C.grey, pink: C.pink, blue: C.blue, pu: '#FFD21A',
  e4: '#FF4240', e2dim: '#00475C', cable: '#4358A1',
};
const hex = c => [1, 3, 5].map(i => parseInt(c.slice(i, i + 2), 16));
const mix = (a, b, t) => '#' + hex(a).map((v, i) => Math.round(v + (hex(b)[i] - v) * t).toString(16).padStart(2, '0')).join('').toUpperCase();
const rad = d => (d * Math.PI) / 180;

function arcSegs(cx, cy, r, a0, a1) {          // degrees, 0 = right, 90 = up; cubics of <= 90 degrees (v3's)
  const segs = [], k = Math.max(1, Math.ceil(Math.abs(a1 - a0) / 90)), da = (a1 - a0) / k;
  const P = a => [cx + r * Math.cos(rad(a)), cy - r * Math.sin(rad(a))];
  const T = a => [-Math.sin(rad(a)), -Math.cos(rad(a))];
  const q = (4 / 3) * Math.tan(rad(da) / 4) * r;
  segs.push(['M', ...P(a0)]);
  for (let i = 0, a = a0; i < k; i++, a += da) {
    const p0 = P(a), p3 = P(a + da), t0 = T(a), t3 = T(a + da);
    segs.push(['C', p0[0] + q * t0[0], p0[1] + q * t0[1], p3[0] - q * t3[0], p3[1] - q * t3[1], p3[0], p3[1]]);
  }
  return segs;
}
function samplePath(segs, steps = 8) {        // a path's points, each cubic in steps
  const pts = [];
  let cur = null;
  for (const s of segs) {
    if (s[0] === 'M' || s[0] === 'L') { cur = [s[1], s[2]]; pts.push(cur); }
    else if (s[0] === 'C') {
      const [x0, y0] = cur;
      for (let i = 1; i <= steps; i++) {
        const t = i / steps, u = 1 - t;
        pts.push([u * u * u * x0 + 3 * u * u * t * s[1] + 3 * u * t * t * s[3] + t * t * t * s[5], u * u * u * y0 + 3 * u * u * t * s[2] + 3 * u * t * t * s[4] + t * t * t * s[6]]);
      }
      cur = [s[5], s[6]];
    }
  }
  return pts;
}
// v3's trefoil, each shape filled and stroked a pixel wide in its colour (strokes are widths in the panel's pixels,
// F0197): the OS's fill comes up half a pixel short at the bottom and right, and at about 15 pixels across the
// blades lost their inner corners and lower edges, so the sign looked twisted (the owner, 2026-10-26: "the
// radioactive sign looks twisted for some reason and no like on the reference"); the stroke gives the edges back
// and smooths them
function trefoil(cx, cy, r, col) {
  const out = [], ri = r * 0.38, both = `fill="${col}" stroke="${col}" stroke-width="1" stroke-linejoin="round"`;
  for (const a of [150, 30, 270]) {
    const s = arcSegs(cx, cy, r, a - 30, a + 30);
    const inner = arcSegs(cx, cy, ri, a + 30, a - 30);
    s.push(['L', inner[0][1], inner[0][2]], ...inner.slice(1), ['z']);
    out.push(`<path ${both} d="${D.pathD(s)}"/>`);
  }
  out.push(`<circle ${both} cx="${D.n(cx)}" cy="${D.n(cy)}" r="${D.n(ri * 0.62)}"/>`);
  return out;
}

// ------------------------------------------------------------------ the car (the owner's drawing), v3's place
const S = 1.18, CX = x => (x - 19.4) * S + 8, CY = y => (y - 66.3) * S + 66, GROUND = 66.6;
const carParts = CAR.parts.map(p => {
  if (p.circle) return { c: p.c, circle: [CX(p.circle[0]), CY(p.circle[1]), p.circle[2] * S] };
  return { c: p.c, segs: p.segs.map(s => (s[0] === 'z' ? ['z'] : [s[0], ...s.slice(1).map((v, i) => (i % 2 ? CY(v) : CX(v)))])) };
});
const wheels = CAR.wheels.map(w => ({ cx: CX(w.cx), cy: CY(w.cy), spokes: w.spokes.map(([a, r1, r2]) => [a, r1 * S, r2 * S]) }));
// the time cables, the current along each (v3 ran it along the longest alone; the owner, 2026-10-26: "only one of
// the blue cables has the pulsing power surge effect, but there are actually 3 of those cables, those should have it
// too"): the drawing's blue parts in each cable's order, front to back (the sill; over the rear wheel and down the
// back; the tubes on top), a gap between two parts the cable behind the body. Part 55, a short mark, is no cable.
const CABLE_PARTS = [[56], [59, 60, 61, 62, 63], [57, 58]];
const cables = CABLE_PARTS.map(parts => parts.flatMap(i => {
  if (carParts[i].c !== 'blue' || !carParts[i].segs) throw new Error(`car part ${i} is no cable`);
  return samplePath(carParts[i].segs).map((p, j) => [p[0], p[1], j ? 1 : 0]);   // pen 0: the stretch to it hidden
}));
const EMIT_X = wheels[1].cx + 6;

// ------------------------------------------------------------------ the instruments
// v3's three, the time circuits wider and its text bigger (the owner, 2026-10-26: "text is really small, it might
// needs some layout redesign where you make the text bigger"): the speedometer's and the flux capacitor's panels
// narrower (the capacitor drawn at 0.9 of v3's), the rows' characters 13 tall (v3 11), the names 6 (v3 4.2), the
// tabs' 7 (v3 5).
const PD = [2.5, 77, 70, 145], PS = [74, 77, 251, 145], PF = [255, 77, 317.5, 145];
const GA = { cx: 36.25, cy: 106, r: 17, a0: 210, a1: -30 };
const LABEL = 6;                                 // the row names' and MPH's cap height
const TC = { rows: [85, 104, 123], h: 13, w14: 6.4, w7: 5.8, pitch: 8.6, sw: 1.4, gap: 6, label: PS[0] + 4 };
TC.x0 = TC.label + Math.max(...['DEST', 'PRES', 'LAST'].map(w => textWidth(w, LABEL))) + 5;
const FK = 0.9, FM = (x, y) => [286.25 + (x - 281) * FK, 112.75 + (y - 112.75) * FK];   // v3's capacitor, scaled
const FXB = [...FM(251, 83), ...FM(311, 122)];
const FX = { j: FM(281, 104.5), el: [FM(262.5, 92.6), FM(299.5, 92.6), FM(281, 116)] };
const MPH = { x: 18, pitch: 10.5, y: 119.5, w: 7.6, h: 13 };
const READ_BOX = [259, 125.5, 290, 142.5], FUEL = [293.5, 125.5, 313.5, 142.5];
const READ = { x: 261.5, y: 128.5, w: 5.2, h: 11, pitch: 7.4 };   // gigawatts: three digits and a point
const driveAngle = v => GA.a0 + (GA.a1 - GA.a0) * Math.max(0, Math.min(1, v));
const cols = {};
{
  let x = TC.x0;
  for (const [k, n, w] of [['mon', 3, TC.w14], ['day', 2, TC.w7], ['year', 4, TC.w7], ['ampm', 2, TC.w14], ['hour', 2, TC.w7], ['min', 2, TC.w7]]) {
    cols[k] = x; x += (n - 1) * TC.pitch + w + (k === 'hour' ? 4.6 : TC.gap);
  }
}
// the tabs: a name (and under shift the second setting's) on a box across a frame's top edge, one width for both
const nums = Array.from({ length: 100 }, (_, i) => String(i).padStart(2, '0'));
const TAB_CAP = 7, TAB_H = 4.8, TAB_PAD = 4;
const TABS = [
  { x: 4, y: 8.2, words: [...nums.map(n => 'TRAIL ' + n),                                // top left, no frame;
                          ...['WARM', 'HARD', 'TUBE', 'FOLD'].map(t => 'DRIVE ' + t)] },  // shift: the overdrive (1.2)
  { x: PD[0] + 4, y: PD[1], words: ['SPEED', ...nums.map(n => 'TONE ' + n)] },      // tone green's, flux white's
  { x: PS[0] + 4, y: PS[1], words: ['YEAR', ...nums.map(n => 'SPREAD ' + n)] },     // (the owner, 2026-10-26)
  { x: PF[0] + 4, y: PF[1], words: ['POWER', ...nums.map(n => 'FLUX ' + n)] },
];
for (const t of TABS) t.w = Math.max(...t.words.map(w => textWidth(w, TAB_CAP))) + 2 * TAB_PAD;

function staticArt() {
  const stat = [];
  stat.push(line(2, GROUND, 318, GROUND, K.dkblue, 1.1, 'round'));
  const frame = (x0, y0, x1, y1) => path(roundRect(x0, y0, x1, y1, 4), K.dkblue, 1.2);
  stat.push(frame(...PD), frame(...PS), frame(...PF));
  // the speedometer's face: its arc dark, the ticks, 88 in the fourth encoder's colour (the jump that was)
  stat.push(path(arcSegs(GA.cx, GA.cy, GA.r, GA.a0, GA.a1), K.dkblue, 1.2));
  for (let m = 0; m <= 88; m += 11) {
    const ag = rad(driveAngle(m / 88)), c = Math.cos(ag), s = -Math.sin(ag), big = m % 44 === 0;
    stat.push(line(GA.cx + c * (GA.r + 2.2), GA.cy + s * (GA.r + 2.2), GA.cx + c * (GA.r + (big ? 5.6 : 4)), GA.cy + s * (GA.r + (big ? 5.6 : 4)), m === 88 ? K.e4 : K.dkblue, 1.1, 'round'));
  }
  stat.push(text('MPH', MPH.x + MPH.pitch + MPH.w + 5, MPH.y + MPH.h - LABEL, LABEL, K.grey, 'l', 1.1));
  // the time circuits' row names and lines (the rows themselves are drawn by the page)
  const ROWS = ['DEST', 'PRES', 'LAST'];
  for (let r = 0; r < 3; r++) {
    const y = TC.rows[r];
    stat.push(text(ROWS[r], TC.label, y + (TC.h - LABEL) / 2, LABEL, K.grey, 'l', 1.1));
    stat.push(line(TC.x0, y + TC.h + 1.6, PS[2] - 6, y + TC.h + 1.6, r === 1 ? K.e2dim : K.dkblue, 1, 'round'));
  }
  // the flux capacitor: box, window (gaps at the corners), warning strip, electrodes, wires; the readout's box and
  // the fuel button
  stat.push(path(roundRect(...FXB, 6 * FK), K.purplegrey, 1.2));
  const L = (x0, y0, x1, y1, col, w, cap) => line(...FM(x0, y0), ...FM(x1, y1), col, w, cap);   // v3's places, scaled
  const P = segs => segs.map(sg => [sg[0], ...sg.slice(1).flatMap((v, i, a) => (i % 2 ? [] : FM(v, a[i + 1])))]);
  const w0 = 255.5, w1 = 306.5, v0 = 90, v1 = 117.5;                                 // v3's window
  stat.push(L(w0 + 5, v0, w1 - 5, v0, K.dkblue, 1), L(w0 + 5, v1, w1 - 5, v1, K.dkblue, 1), L(w0, v0 + 5, w0, v1 - 5, K.dkblue, 1), L(w1, v0 + 5, w1, v1 - 5, K.dkblue, 1));
  stat.push(L(257, 86.6, 278, 86.6, K.pink, 1.4, 'round'), L(281, 86.6, 283.5, 86.6, K.pink, 1.4, 'round'));
  for (const [ex, ey] of FX.el) stat.push(ring(ex, ey, 3.8 * FK, K.purplegrey, 1.1), dot(ex, ey, 1.6 * FK, K.pink));
  stat.push(path(P([['M', 260.2, 95.4], ['C', 257.8, 99.5, 257.4, 104.6, 258.6, 108.2], ['C', 259.2, 110, 261.2, 110.1, 261.8, 108.7]]), K.lavender, 1.1));
  stat.push(path(P([['M', 301.8, 95.4], ['C', 304.2, 99.5, 304.6, 104.6, 303.4, 108.2], ['C', 302.8, 110, 300.8, 110.1, 300.2, 108.7]]), K.lavender, 1.1));
  stat.push(path(P([['M', 284.4, 117.4], ['C', 289, 118.8, 294.6, 119.1, 298.2, 118.2], ['C', 300, 117.7, 300.1, 115.9, 298.7, 115.4]]), K.lavender, 1.1));
  stat.push(path(roundRect(...READ_BOX, 2), K.dkblue, 1.1));
  stat.push(path(roundRect(...FUEL, 2), K.purplegrey, 1.1), trefoil((FUEL[0] + FUEL[2]) / 2, (FUEL[1] + FUEL[3]) / 2, 6.6, K.pu));
  // the tabs' boxes (black cuts the frame line under each; the names are the page's)
  for (const t of TABS) {
    const b = roundRect(t.x, t.y - TAB_H, t.x + t.w, t.y + TAB_H, 2.2);
    stat.push(fillPath(b, '#000000'), path(b, K.dkblue, 1.1));
  }
  // the car: body light purple, accents the fourth encoder's (the fire's), the time cables a steady glow
  for (const p of carParts) {
    const col = p.c === 'white' ? K.lightpurple : p.c === 'blue' ? K.cable : K.e4;
    if (p.circle) stat.push(ring(p.circle[0], p.circle[1], p.circle[2], col, 1.1));
    else stat.push(path(p.segs, col, 1.1));
  }
  return svgDoc('outatime_t1', [group('static', stat)]);
}

// ------------------------------------------------------------------ the C header
const U = v => Math.round(v * 256);            // units x 256, the page service's places
const Q = v => Math.round(v * 4096);           // Q12
function cHeader(svg) {
  const out = [];
  out.push('/* outatime\'s T1 art and its places: made by art/build.js from art/page.js (the owner\'s v3 design, its');
  out.push(' * instruments by meaning). Do not edit by hand. Places in the drawing\'s units (320 x 160) x 256. */');
  out.push('#ifndef OTA_ART_H', '#define OTA_ART_H', '');
  out.push('static const char ota_art[] =');
  for (const ln of svg.split('\n')) if (ln) out.push('    "' + ln.replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '\\n"');
  out.push('    ;', '');
  const def = (n, v, c) => out.push(`#define ${n.padEnd(15)} ${String(v).padEnd(12)}${c ? ' /* ' + c + ' */' : ''}`);
  def('ART_E4_RGB', '0x' + K.e4.slice(1) + 'u', 'placeholder: the fourth encoder\'s colour');
  def('ART_E2DIM_RGB', '0x' + K.e2dim.slice(1) + 'u', 'placeholder: the second\'s, dim');
  def('ART_CABLE_RGB', '0x' + K.cable.slice(1) + 'u', 'placeholder: the first\'s, the cables\' glow');
  def('GROUND', U(GROUND));
  def('EMIT_X', U(EMIT_X), 'the fire starts behind the rear wheel');
  def('GA_CX', U(GA.cx), 'the speedometer');
  def('GA_CY', U(GA.cy));
  def('GA_R', U(GA.r));
  def('GA_A0', Math.round(GA.a0 / 360 * 4096), 'its arc, 1/4096 of a turn, counter-clockwise');
  def('GA_A1', Math.round(GA.a1 / 360 * 4096));
  def('MPH_X', U(MPH.x));
  def('MPH_PITCH', U(MPH.pitch));
  def('MPH_Y', U(MPH.y));
  def('MPH_W', U(MPH.w));
  def('MPH_H', U(MPH.h));
  def('TC_H', U(TC.h), 'the time circuits\' characters');
  def('TC_W14', U(TC.w14));
  def('TC_W7', U(TC.w7));
  def('TC_PITCH', U(TC.pitch));
  def('TC_SW', U(TC.sw));
  for (let r = 0; r < 3; r++) def('TC_ROW' + r, U(TC.rows[r]));
  for (const k of ['mon', 'day', 'year', 'ampm', 'hour', 'min']) def('TC_' + k.toUpperCase(), U(cols[k]));
  def('TC_X1', U(PS[2]), 'the rows\' right end (the frame)');
  def('TC_COLON1', U(TC.h * 0.3), 'the colon\'s dots, down from a row\'s top');
  def('TC_COLON2', U(TC.h * 0.7));
  def('FX_JX', U(FX.j[0]), 'the flux capacitor\'s junction');
  def('FX_JY', U(FX.j[1]));
  def('ROD_E', U(4.6 * FK), 'a rod from its electrode, to the junction');
  def('ROD_J', U(1.6 * FK));
  def('READ_X', U(READ.x), 'the readout');
  def('READ_Y', U(READ.y));
  def('READ_W', U(READ.w));
  def('READ_H', U(READ.h));
  def('READ_PITCH', U(READ.pitch));
  def('READ_X0', U(READ_BOX[0] + 1), 'the readout\'s box, inside');
  def('READ_Y0', U(READ_BOX[1] + 1));
  def('READ_X1', U(READ_BOX[2] - 1));
  def('READ_Y1', U(READ_BOX[3] - 1));
  def('TAB_CAP', U(TAB_CAP), 'the tabs\' names');
  def('TAB_H', U(TAB_H));
  def('TAB_PAD', U(TAB_PAD));
  out.push('');
  out.push('static const int FX_EL[3][2] = { ' + FX.el.map(e => `{ ${U(e[0])}, ${U(e[1])} }`).join(', ') + ' };');
  out.push('static const int TAB[4][3] = {          /* each tab: its box\'s left, middle (y), width */');
  out.push('    ' + TABS.map(t => `{ ${U(t.x)}, ${U(t.y)}, ${U(t.w)} }`).join(', ') + ' };');
  out.push('static const int WHEEL[2][2] = { ' + wheels.map(w => `{ ${U(w.cx)}, ${U(w.cy)} }`).join(', ') + ' };');
  const ns = wheels[0].spokes.length;
  out.push(`#define SPOKES          ${ns}`);
  out.push('static const short SPOKE[2][SPOKES][3] = {   /* angle (1/4096 of a turn, clockwise on the screen), r1, r2 (x 256) */');
  for (const w of wheels) out.push('    { ' + w.spokes.map(([a, r1, r2]) => `{ ${Math.round(a / (2 * Math.PI) * 4096)}, ${U(r1)}, ${U(r2)} }`).join(', ') + ' },');
  out.push('};');
  const all = cables.flat();
  out.push(`#define CABLES          ${cables.length}`);
  out.push(`#define CABLE_N         ${all.length}`);
  out.push('static const short CABLE_AT[CABLES + 1] = { ' + cables.reduce((a, c) => [...a, a[a.length - 1] + c.length], [0]).join(', ') + ' };   /* each cable\'s first point */');
  out.push('static const int CABLE[CABLE_N][3] = {   /* the time cables, sampled: x, y, 0 when the stretch to it is hidden */');
  for (let i = 0; i < all.length; i += 6) out.push('    ' + all.slice(i, i + 6).map(p => `{ ${U(p[0])}, ${U(p[1])}, ${p[2]} }`).join(', ') + ',');
  out.push('};');
  // the letters the tabs need, each stroke a polyline (the font's curves sampled), Q12 of the cap height
  const chars = [...new Set(TABS.flatMap(t => t.words).join('').split(''))].filter(c => c !== ' ').sort();
  const data = [], index = {};
  for (const ch of chars) {
    const g = D.glyph(ch);
    index[ch] = data.length;
    data.push(Q(g.w));
    for (const st of g.p) {
      const segs = st.map(([cmd, pts]) => [cmd, ...pts]);
      const pts = samplePath(segs, 4);
      data.push(pts.length);
      for (const p of pts) data.push(Q(p[0]), Q(p[1]));
    }
    data.push(0);
  }
  out.push(`#define GLYPH_GAP       ${Q(D.GAP)}           /* between two letters, Q12 of the cap */`);
  out.push(`#define GLYPH_SPACE     ${Q(0.22)}`);
  out.push('static const char GLYPH_CHARS[] = "' + chars.join('') + '";');
  out.push('static const unsigned short GLYPH_AT[] = { ' + chars.map(c => index[c]).join(', ') + ' };');
  out.push('static const short GLYPH[] = {           /* per letter: width, then per stroke its points\' count and points; 0 ends */');
  for (let i = 0; i < data.length; i += 16) out.push('    ' + data.slice(i, i + 16).join(', ') + ',');
  out.push('};');
  // the segment displays (op1draw.js's): each segment's ends in halves of the cell (u, v), and which are lit
  const SEG14 = Object.entries(D.SEG14);
  const SEG7 = [['a', [0, 0, 1, 0]], ['b', [1, 0, 1, 0.5]], ['c', [1, 0.5, 1, 1]], ['d', [0, 1, 1, 1]], ['e', [0, 0.5, 0, 1]], ['f', [0, 0, 0, 0.5]], ['g', [0, 0.5, 1, 0.5]]];
  const MAP7 = { 0: 'abcdef', 1: 'bc', 2: 'abdeg', 3: 'abcdg', 4: 'bcfg', 5: 'acdfg', 6: 'acdefg', 7: 'abc', 8: 'abcdefg', 9: 'abcdfg' };
  const half = s => s.map(v => v * 2).join(', ');
  out.push('static const signed char SEG14[14][4] = { ' + SEG14.map(([, s]) => `{ ${half(s)} }`).join(', ') + ' };');
  out.push('static const signed char SEG7[7][4] = { ' + SEG7.map(([, s]) => `{ ${half(s)} }`).join(', ') + ' };');
  const mask14 = ch => (D.MAP14[ch] || '').split(' ').filter(Boolean).reduce((m, k) => m | (1 << SEG14.findIndex(([n]) => n === k)), 0);
  out.push('static const unsigned short MAP14_DIGIT[10] = { ' + Array.from({ length: 10 }, (_, i) => mask14(String(i))).join(', ') + ' };');
  out.push('static const unsigned short MAP14_ALPHA[26] = { ' + Array.from({ length: 26 }, (_, i) => mask14(String.fromCharCode(65 + i))).join(', ') + ' };');
  out.push('static const unsigned char MAP7[10] = { ' + Array.from({ length: 10 }, (_, i) => MAP7[i].split('').reduce((m, k) => m | (1 << 'abcdefg'.indexOf(k)), 0)).join(', ') + ' };');
  out.push('', '#endif', '');
  return out.join('\n');
}

module.exports = { staticArt, cHeader };
