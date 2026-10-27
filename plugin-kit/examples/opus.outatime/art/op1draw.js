/* op1draw.js: the drawing kit shared by the engine pages (dx7, outatime): the OP-1's own plain SVG.

   Every page is 320 x 160 with its content inside y 2-146 (the OP-1's window), and only what the OS's
   2011 parser draws right: lines, polylines, circles and paths of absolute M, L, C, z with three decimals
   at most, a colour and a width on every shape (fill="none" written out), no transforms, no dashes (each
   dash is a line), no text elements (the letters are the stock opfont.svg strokes), simple ids.
   check_plain.py checks a page against those rules.
*/
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./opfont.json'));
  else root.OP1DRAW = factory(root.OPFONT);
})(typeof self !== 'undefined' ? self : this, function (FONT) {
  'use strict';

  // ------------------------------------------------------------------ palette (stock colors.svg)
  const C = {
    blue: '#698EFF', green: '#00ED95', white: '#FFFFFF', red: '#FF3A5D',
    dkblue: '#383572', offblue: '#1D2040', dkgreen: '#00475B', offgreen: '#002826',
    grey: '#7C7990', purplegrey: '#AEB1DC', lavender: '#ADADFF', lightpurple: '#E6E7FF',
    offgrey: '#353238', offred: '#4E2832', dimred: '#512132', purple: '#5F4270', dimwhite: '#656579',
    lightblue: '#BFD8FF', lightgreen: '#80FFAF', pink: '#FF90A7',
  };
  const ENC = [C.blue, C.green, C.white, C.red];
  const ENC_DIM = [C.dkblue, C.dkgreen, C.dimwhite, C.dimred];
  const ENC_OFF = [C.offblue, C.offgreen, C.offgrey, C.offred];

  // ------------------------------------------------------------------ plain SVG writers
  function n(v) {
    if (!isFinite(v)) v = 0;
    let t = (Math.round(v * 1000) / 1000).toFixed(3).replace(/0+$/, '').replace(/\.$/, '');
    return t === '-0' || t === '' ? '0' : t;
  }
  const pair = (x, y) => n(x) + ',' + n(y);
  function stroke(col, w, cap, join) {
    return `fill="none" stroke="${col}" stroke-width="${n(w)}"` +
      (cap ? ` stroke-linecap="${cap}"` : '') + (join ? ` stroke-linejoin="${join}"` : '');
  }
  function line(x1, y1, x2, y2, col, w = 1.5, cap = 'round') {
    return `<line ${stroke(col, w, cap)} x1="${n(x1)}" y1="${n(y1)}" x2="${n(x2)}" y2="${n(y2)}"/>`;
  }
  function polyline(pts, col, w = 1.5, cap = 'round', join = 'round') {
    if (pts.length < 2) return '';
    return `<polyline ${stroke(col, w, cap, join)} points="${pts.map(p => pair(p[0], p[1])).join(' ')}"/>`;
  }
  // segments: ['M', x, y] ['L', x, y] ['C', x1, y1, x2, y2, x, y] ['z']: one letter each, as op1svg writes them
  function pathD(segs) {
    return segs.map(s => {
      if (s[0] === 'z') return 'z';
      const out = [];
      for (let i = 1; i < s.length; i += 2) out.push(pair(s[i], s[i + 1]));
      return s[0] + out.join(' ');
    }).join('');
  }
  function path(segs, col, w = 1.5, cap = 'round', join = 'round') {
    return `<path ${stroke(col, w, cap, join)} d="${pathD(segs)}"/>`;
  }
  function fillPath(segs, col) { return `<path fill="${col}" d="${pathD(segs)}"/>`; }
  function dot(cx, cy, r, col) { return `<circle fill="${col}" cx="${n(cx)}" cy="${n(cy)}" r="${n(r)}"/>`; }
  function ring(cx, cy, r, col, w = 1.5) { return `<circle ${stroke(col, w)} cx="${n(cx)}" cy="${n(cy)}" r="${n(r)}"/>`; }
  function group(id, items) {
    const body = items.flat(Infinity).filter(Boolean).join('');
    return id ? `<g id="${id}">${body}</g>` : `<g>${body}</g>`;
  }
  // a rectangle with rounded corners (cubic corners; the parser ignores rx)
  function roundRect(x0, y0, x1, y1, r) {
    const k = r * 0.5523;
    return [['M', x0 + r, y0], ['L', x1 - r, y0], ['C', x1 - r + k, y0, x1, y0 + r - k, x1, y0 + r],
      ['L', x1, y1 - r], ['C', x1, y1 - r + k, x1 - r + k, y1, x1 - r, y1], ['L', x0 + r, y1],
      ['C', x0 + r - k, y1, x0, y1 - r + k, x0, y1 - r], ['L', x0, y0 + r],
      ['C', x0, y0 + r - k, x0 + r - k, y0, x0 + r, y0], ['z']];
  }
  // dashes as lines of their own (the parser draws stroke-dasharray solid)
  function dashed(x1, y1, x2, y2, col, on = 2, off = 2, w = 1) {
    const L = Math.hypot(x2 - x1, y2 - y1), out = [];
    if (L < 0.01) return out;
    const ux = (x2 - x1) / L, uy = (y2 - y1) / L;
    for (let s = 0; s < L; s += on + off) {
      const e = Math.min(L, s + on);
      out.push(line(x1 + ux * s, y1 + uy * s, x1 + ux * e, y1 + uy * e, col, w, 'butt'));
    }
    return out;
  }
  const lerp = (a, b, t) => a + (b - a) * t;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const frac = v => v - Math.floor(v);

  // ------------------------------------------------------------------ the OP-1's letters
  const GAP = 0.267;                // between two letters' ink, in cap heights (measured on singlelfo.svg)
  const SPACE = { w: 0.22, p: [] };
  function glyph(ch) { return ch === ' ' ? SPACE : FONT[ch] || FONT[String(ch).toUpperCase()] || null; }
  function textWidth(s, cap) {
    let w = 0, prev = false;
    for (const ch of String(s)) {
      const g = glyph(ch);
      if (!g) continue;
      if (prev) w += GAP * cap;
      w += g.w * cap;
      prev = true;
    }
    return w;
  }
  // x, y: the cap top at the left, centre or right (align 'l' 'c' 'r'); one path per stroke
  function text(s, x, y, cap, col, align = 'l', w) {
    const sw = w || (cap >= 8 ? 1.5 : cap >= 6.5 ? 1.3 : 1.15);
    const tw = textWidth(s, cap);
    let cx = align === 'c' ? x - tw / 2 : align === 'r' ? x - tw : x;
    const out = [];
    let prev = false;
    for (const ch of String(s)) {
      const g = glyph(ch);
      if (!g) continue;
      if (prev) cx += GAP * cap;
      for (const st of g.p) {
        const segs = st.map(([cmd, pts]) => [cmd, ...pts.map((v, i) => (i % 2 ? y + v * cap : cx + v * cap))]);
        out.push(path(segs, col, sw, 'square', 'round'));
      }
      cx += g.w * cap;
      prev = true;
    }
    return out;
  }

  // ------------------------------------------------------------------ the DX7's red LED (7 segments)
  const SEG = { a: [0, 0, 1, 0], b: [1, 0, 1, 0.5], c: [1, 0.5, 1, 1], d: [0, 1, 1, 1], e: [0, 0.5, 0, 1], f: [0, 0, 0, 0.5], g: [0, 0.5, 1, 0.5] };
  const SEGMAP = { 0: 'abcdef', 1: 'bc', 2: 'abdeg', 3: 'abcdg', 4: 'bcfg', 5: 'acdfg', 6: 'acdefg', 7: 'abc', 8: 'abcdefg', 9: 'abcdfg', '-': 'g', ' ': '' };
  function led(ch, x, y, w, h, on, off, sw, which) {
    const lit = SEGMAP[ch] || '';
    const P = (u, v) => [x + u * w + (1 - v) * h * 0.12, y + v * h];
    const out = [];
    for (const pass of which === 'lit' ? [1] : which === 'off' ? [0] : [0, 1]) {
      for (const k of 'abcdefg') {
        if ((lit.indexOf(k) >= 0) !== !!pass) continue;
        const [u1, v1, u2, v2] = SEG[k];
        const [x1, y1] = P(u1, v1), [x2, y2] = P(u2, v2);
        const L = Math.hypot(x2 - x1, y2 - y1), g = sw * 1.05;
        const ux = (x2 - x1) / L, uy = (y2 - y1) / L;
        out.push(line(x1 + ux * g, y1 + uy * g, x2 - ux * g, y2 - uy * g, pass ? on : off, sw, 'round'));
      }
    }
    return out;
  }

  // ------------------------------------------------------------------ segment displays: 7 for digits, 14 for letters
  // Returns { ghost, glow, lit } so a page can keep the unlit segments in its static art. Glow: a wide dim
  // stroke under the lit one (the parser has no filters).
  const SEG14 = {
    a: [0, 0, 1, 0], b: [1, 0, 1, 0.5], c: [1, 0.5, 1, 1], d: [0, 1, 1, 1], e: [0, 0.5, 0, 1], f: [0, 0, 0, 0.5],
    g1: [0, 0.5, 0.5, 0.5], g2: [0.5, 0.5, 1, 0.5], h: [0, 0, 0.5, 0.5], j: [0.5, 0, 0.5, 0.5], k: [1, 0, 0.5, 0.5],
    l: [0, 1, 0.5, 0.5], m: [0.5, 0.5, 0.5, 1], n: [0.5, 0.5, 1, 1],
  };
  const MAP14 = {
    A: 'a b c e f g1 g2', B: 'a b c d j m g2', C: 'a d e f', D: 'a b c d j m', E: 'a d e f g1', F: 'a e f g1',
    G: 'a c d e f g2', H: 'b c e f g1 g2', I: 'a d j m', J: 'b c d e', K: 'e f g1 k n', L: 'd e f', M: 'b c e f h k',
    N: 'b c e f h n', O: 'a b c d e f', P: 'a b e f g1 g2', R: 'a b e f g1 g2 n', S: 'a c d f g1 g2', T: 'a j m',
    U: 'b c d e f', V: 'e f k l', W: 'b c e f l n', X: 'h k l n', Y: 'h k m', Z: 'a d k l', '-': 'g1 g2', ' ': '',
    0: 'a b c d e f', 1: 'b c', 2: 'a b d e g1 g2', 3: 'a b c d g2', 4: 'b c f g1 g2', 5: 'a c d f g1 g2',
    6: 'a c d e f g1 g2', 7: 'a b c', 8: 'a b c d e f g1 g2', 9: 'a b c d f g1 g2',
  };
  const SEG7 = { a: SEG.a, b: SEG.b, c: SEG.c, d: SEG.d, e: SEG.e, f: SEG.f, g: SEG.g };
  const MAP7 = {};
  for (const [k, v] of Object.entries(SEGMAP)) MAP7[k] = v.split('');
  function segChar(ch, x, y, w, h, o) {
    const fourteen = o.kind === 14, table = fourteen ? SEG14 : SEG7;
    const lit = new Set(fourteen ? (MAP14[String(ch).toUpperCase()] || '').split(' ').filter(Boolean) : (MAP7[ch] || []));
    const sw = o.sw || 1.5, sl = o.slant === undefined ? 0.06 : o.slant, gk = o.gap === undefined ? 0.95 : o.gap;
    const P = (u, v) => [x + u * w + (1 - v) * h * sl, y + v * h];
    const res = { ghost: [], glow: [], lit: [] };
    for (const [k, s] of Object.entries(table)) {
      const [x1, y1] = P(s[0], s[1]), [x2, y2] = P(s[2], s[3]);
      const L = Math.hypot(x2 - x1, y2 - y1), g = Math.min(L * 0.3, sw * gk);
      const ux = (x2 - x1) / L, uy = (y2 - y1) / L;
      const a = [x1 + ux * g, y1 + uy * g], b = [x2 - ux * g, y2 - uy * g];
      if (lit.has(k)) {
        if (o.glow) res.glow.push(line(a[0], a[1], b[0], b[1], o.glow, sw * (o.glowK || 2.6), 'round'));
        res.lit.push(line(a[0], a[1], b[0], b[1], o.lit, sw, 'round'));
      } else if (o.ghost) res.ghost.push(line(a[0], a[1], b[0], b[1], o.ghost, sw, 'round'));
    }
    return res;
  }
  // a string of segment characters at a pitch; ':' and '.' are dots between cells
  function segText(s, x, y, w, h, pitch, o) {
    const res = { ghost: [], glow: [], lit: [] };
    let cx = x;
    for (const ch of String(s)) {
      if (ch === '.' || ch === ':') {
        const r = (o.sw || 1.5) * 0.62, dx = cx - pitch * 0.18 + (ch === ':' ? 0 : 0);
        const pts = ch === ':' ? [y + h * 0.3, y + h * 0.72] : [y + h];
        for (const py of pts) {
          if (o.dotsOn === false) { if (o.ghost) res.ghost.push(dot(dx, py, r, o.ghost)); }
          else { if (o.glow) res.glow.push(dot(dx, py, r * 2, o.glow)); res.lit.push(dot(dx, py, r, o.lit)); }
        }
        cx += pitch * 0.36;
        continue;
      }
      const c = segChar(ch, cx, y, w, h, o);
      res.ghost.push(...c.ghost); res.glow.push(...c.glow); res.lit.push(...c.lit);
      cx += pitch;
    }
    return res;
  }

  // ------------------------------------------------------------------ the document
  function svgDoc(id, items) {
    return '<?xml version="1.0" encoding="utf-8"?>\n' +
      `<svg version="1.1" id="${id}" xmlns="http://www.w3.org/2000/svg" x="0px" y="0px" width="320px" height="160px" viewBox="0 0 320 160">\n` +
      items.map(s => s).join('\n') + '\n</svg>\n';
  }

  return {
    C, ENC, ENC_DIM, ENC_OFF, n, pair, stroke, line, polyline, pathD, path, fillPath, dot, ring, group, roundRect, dashed,
    lerp, clamp, frac, GAP, glyph, textWidth, text, led, SEG14, MAP14, segChar, segText, svgDoc,
  };
});
