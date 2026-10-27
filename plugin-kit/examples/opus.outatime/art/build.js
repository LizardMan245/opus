/* build.js: outatime's T1 art (node art/build.js, from the plugin's folder or anywhere): the static page as
   art/outatime_t1.svg (to look at; tools/op1mod/svgcheck.py checks it) and ../ota_art.h (the art as a C string with
   the places ota_page.c draws at). */
'use strict';
const fs = require('fs');
const path = require('path');
const P = require('./page.js');

const svg = P.staticArt();
fs.writeFileSync(path.join(__dirname, 'outatime_t1.svg'), svg);
fs.writeFileSync(path.join(__dirname, '..', 'ota_art.h'), P.cHeader(svg));
console.log('outatime_t1.svg', svg.length, 'bytes;', (svg.match(/<(path|line|circle|polyline)/g) || []).length, 'shapes');
