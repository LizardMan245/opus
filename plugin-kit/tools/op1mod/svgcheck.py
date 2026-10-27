"""svgcheck: will the OP-1 draw this SVG, and draw it as meant? For page art (Opus 2's opus_page_art, opus.dx's
t1.svg, t2.svg, t4.svg) before it goes on the device.

    python tools/op1mod/svgcheck.py FILE.svg [FILE.svg ...]

The OS's own SVG parser (atlas F0197) draws a small part of SVG, drops much without a word, and stops the OP-1 (the
crash screen) on a few things. Errors are those (the core checks the same before it loads a file, art.c, and refuses
it): an XML error, a root other than <svg>, a number with a point and no or more than four decimals, an exponent,
the path commands Q and T, an id with "_x" not followed by two characters and "_". Warnings are what it drops or
draws otherwise: elements other than svg, g, line, polyline, polygon, rect, circle, ellipse, path (text, use, defs,
gradients, images, style...), style= and class (put fill and stroke on each shape), fill or stroke on a <g> (not
inherited), a shape with neither fill nor stroke (not made at all), no fill attribute (no fill, not black),
fill-opacity, stroke-opacity, stroke-dasharray, fill-rule, display, visibility, rx and ry on rect (square corners),
rotate() and skew() (the element vanishes), scale() with one value, commas in a transform, arcs (A: dropped), an M in
the middle of a path (its parts join into one shape), #rgb colours (write #rrggbb), stroke-linejoin bevel (drawn
mitred), an id with text between two underscores (cut out). And the page: 320 x 160 units, the OP-1's window shows
y 2 to 146 (below 146 is hidden on the device).

What draws right (TE's own form, Illustrator 14 "SVG 1.1, presentation attributes"): lines, polylines, paths of
absolute M, L, C, z with up to three decimals, colours and widths on each shape. Exit 1 when a file has an error.
"""
import re
import sys
import xml.etree.ElementTree as ET

DRAWN = {'svg', 'g', 'line', 'polyline', 'polygon', 'rect', 'circle', 'ellipse', 'path'}
SHAPES = DRAWN - {'svg', 'g'}
NUMERIC = {'d', 'points', 'transform', 'x', 'y', 'width', 'height', 'x1', 'y1', 'x2', 'y2', 'cx', 'cy', 'r', 'rx',
           'ry', 'stroke-width', 'opacity', 'stroke-miterlimit', 'viewBox'}
DROPPED_ATTR = {'style': 'style= is dropped: put fill, stroke and widths on the shape as attributes',
                'class': 'class is dropped (no CSS): put fill and stroke on the shape',
                'fill-opacity': 'fill-opacity is dropped', 'stroke-opacity': 'stroke-opacity is dropped',
                'stroke-dasharray': 'stroke-dasharray is dropped (a solid line)', 'fill-rule': 'fill-rule is dropped',
                'display': 'display is dropped', 'visibility': 'visibility is dropped'}


def local(tag):
    return tag.split('}', 1)[-1]


def number_errors(value, is_path):
    errs = []
    if is_path and re.search(r'[QqTt]', value):
        errs.append('a path with Q or T (the OS stops)')
    if re.search(r'\d[eE]', value):
        errs.append('a number with an exponent (the OS stops)')
    for m in re.finditer(r'\.(\d*)', value):
        if len(m.group(1)) == 0 or len(m.group(1)) > 4:
            errs.append(f'a number with {len(m.group(1))} decimals ("{value[max(0, m.start() - 6):m.end()]}"; '
                        'the OS stops on none or more than 4)')
            break
    return errs


def check(path):
    errors, warnings = [], []
    raw = open(path, 'rb').read()
    if len(raw) > 65536:
        errors.append(f'{len(raw)} bytes: more than the 64 KB the core loads')
    try:
        root = ET.fromstring(raw)
    except ET.ParseError as e:
        return [f'an XML error: {e} (the OS stops)'], []
    if local(root.tag) != 'svg':
        errors.append(f'the root is <{local(root.tag)}>, not <svg> (the OS stops)')

    def walk(el, depth):
        tag = local(el.tag)
        where = f'<{tag}' + (f' id="{el.get("id")}"' if el.get('id') else '') + '>'
        if tag not in DRAWN:
            warnings.append(f'{where}: not drawn (the parser drops it and everything in it)')
            return
        for a, v in el.attrib.items():
            name = local(a)
            if name in NUMERIC:
                errors.extend(f'{where} {name}: {e}' for e in number_errors(v, name == 'd'))
            if name == 'id' and re.search(r'_x(?!.._)', v):
                errors.append(f'{where}: an id with _x not followed by two characters and _ (the OS stops)')
            if name == 'id' and re.search(r'_[^_]*_', v):
                warnings.append(f'{where}: the id\'s text between two underscores is cut out ("a_b_c" reads "ac")')
            if name in DROPPED_ATTR:
                warnings.append(f'{where}: {DROPPED_ATTR[name]}')
            if name in ('fill', 'stroke') and re.fullmatch(r'#[0-9a-fA-F]{3}', v.strip()):
                warnings.append(f'{where} {name}="{v}": #rgb is not read: write #rrggbb')
            if name == 'transform':
                if re.search(r'rotate|skew', v):
                    warnings.append(f'{where}: rotate() or skew() makes the element and its children vanish')
                if re.search(r'scale\(\s*[-\d.]+\s*\)', v):
                    warnings.append(f'{where}: scale() with one value scales y to 0: give both')
                if ',' in v:
                    warnings.append(f'{where}: commas in a transform are misread: values apart by spaces')
            if name == 'stroke-linejoin' and v == 'bevel':
                warnings.append(f'{where}: linejoin bevel is drawn mitred')
        if tag == 'g' and (el.get('fill') or el.get('stroke')):
            warnings.append(f'{where}: fill and stroke on a group do not reach its shapes: put them on each')
        if tag in SHAPES:
            fill, stroke = el.get('fill'), el.get('stroke')
            if (not fill or fill == 'none') and (not stroke or stroke == 'none'):
                warnings.append(f'{where}: neither fill nor stroke: the OS does not make it at all')
            if tag == 'rect' and (el.get('rx') or el.get('ry')):
                warnings.append(f'{where}: rx and ry are dropped: square corners')
            if tag == 'path':
                d = el.get('d', '')
                if re.search(r'[Aa]', d):
                    warnings.append(f'{where}: arcs (A) are dropped with their end points')
                if len(re.findall(r'[Mm]', d)) > 1:
                    warnings.append(f'{where}: an M in mid-path joins the parts into one shape: one path each')
            ys = [float(el.get(k)) for k in ('y', 'y1', 'y2', 'cy') if el.get(k) and
                  re.fullmatch(r'-?\d+(\.\d+)?', el.get(k))]
            if any(y > 146 for y in ys):
                warnings.append(f'{where}: below y 146: hidden by the OP-1\'s window')
        for c in el:
            walk(c, depth + 1)
    walk(root, 0)
    return errors, warnings


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__.split('\n\n')[1])
    bad = 0
    for path in sys.argv[1:]:
        errors, warnings = check(path)
        print(f'{path}: {len(errors)} errors, {len(warnings)} warnings')
        for e in errors:
            print(f'  error: {e}')
        for w in warnings:
            print(f'  warning: {w}')
        bad |= bool(errors)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
