#!/usr/bin/env python3
"""Generates bg.svg for the Burning Church skin: frozen peaks, a blood moon, a
burning wooden church, a dead pine forest and a graveyard of inverted crosses.

The plugin draws this at 50% opacity behind the chain and covers the middle
with opaque cards, so the parts that matter live in the top strip (y < ~80)
and the right-hand gap (x ~860-950). Run: python3 make_bg.py
"""
import math
import os
import random

W, H = 1024, 432
out = []
a = out.append


def path(pts, fill, extra=""):
    d = "M" + " L".join(f"{x:.1f},{y:.1f}" for x, y in pts) + " Z"
    a(f'<path d="{d}" fill="{fill}" {extra}/>')


def ridge(base, amp, step, jag, seed):
    random.seed(seed)
    pts = [(0, H)]
    x, y = 0.0, base
    while x <= W:
        y = max(base - amp, min(base + amp * 0.3, y + random.uniform(-jag, jag)))
        pts.append((x, y))
        x += random.uniform(step * 0.5, step * 1.5)
    pts += [(W, y), (W, H)]
    return pts


def flames(x0, x1, base, height, n, seed, colour, opacity=1.0):
    """A row of flame tongues standing on the segment x0..x1 at y=base."""
    random.seed(seed)
    d = f"M{x0:.1f},{base:.1f}"
    step = (x1 - x0) / n
    for i in range(n):
        lx = x0 + i * step
        tip_x = lx + step * random.uniform(0.3, 0.7) + random.uniform(-8, 8)
        tip_y = base - height * random.uniform(0.45, 1.0)
        rx = lx + step
        d += (f" Q{lx + step * 0.1:.1f},{base - height * 0.35:.1f} {tip_x:.1f},{tip_y:.1f}"
              f" Q{rx - step * 0.1:.1f},{base - height * 0.3:.1f} {rx:.1f},{base:.1f}")
    d += " Z"
    a(f'<path d="{d}" fill="{colour}" opacity="{opacity}"/>')


a(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">')
a("<defs>")
a('<linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#000000"/><stop offset="0.55" stop-color="#05070A"/>'
  '<stop offset="1" stop-color="#11161C"/></linearGradient>')
a('<radialGradient id="halo" cx="0.5" cy="0.5" r="0.5">'
  '<stop offset="0" stop-color="#FF3A22" stop-opacity="0.40"/>'
  '<stop offset="0.35" stop-color="#8B0000" stop-opacity="0.16"/>'
  '<stop offset="1" stop-color="#000000" stop-opacity="0"/></radialGradient>')
a('<radialGradient id="moon" cx="0.42" cy="0.4" r="0.6">'
  '<stop offset="0" stop-color="#F0A890"/><stop offset="0.55" stop-color="#B3200A"/>'
  '<stop offset="1" stop-color="#4A0606"/></radialGradient>')
a('<linearGradient id="bloodmist" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#7A0A0A" stop-opacity="0"/>'
  '<stop offset="1" stop-color="#7A0A0A" stop-opacity="0.45"/></linearGradient>')
a('<linearGradient id="fog" x1="0" y1="0" x2="0" y2="1">'
  '<stop offset="0" stop-color="#AEB9C4" stop-opacity="0"/>'
  '<stop offset="1" stop-color="#AEB9C4" stop-opacity="0.10"/></linearGradient>')
a('<radialGradient id="fireglow" cx="0.5" cy="0.5" r="0.5">'
  '<stop offset="0" stop-color="#FF7A1A" stop-opacity="0.55"/>'
  '<stop offset="0.4" stop-color="#C2300A" stop-opacity="0.22"/>'
  '<stop offset="1" stop-color="#000000" stop-opacity="0"/></radialGradient>')
a('<linearGradient id="firelit" x1="0" y1="0" x2="1" y2="0">'
  '<stop offset="0" stop-color="#6A2A0C"/><stop offset="0.45" stop-color="#1A1410"/>'
  '<stop offset="1" stop-color="#07080A"/></linearGradient>')
a('<linearGradient id="smoke" x1="0" y1="1" x2="1" y2="0">'
  '<stop offset="0" stop-color="#4A3A33" stop-opacity="0.75"/>'
  '<stop offset="1" stop-color="#2A2A2E" stop-opacity="0"/></linearGradient>')
a('<linearGradient id="fadeL" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#000"/>'
  '<stop offset="1" stop-color="#000" stop-opacity="0"/></linearGradient>')
a('<linearGradient id="fadeT" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#000"/>'
  '<stop offset="1" stop-color="#000" stop-opacity="0"/></linearGradient>')
a('<linearGradient id="fadeB" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#080A0D"/>'
  '<stop offset="1" stop-color="#080A0D" stop-opacity="0"/></linearGradient>')
a("</defs>")
a(f'<rect width="{W}" height="{H}" fill="url(#sky)"/>')

# Stars
random.seed(666)
for _ in range(140):
    x, y = random.uniform(0, W), random.uniform(0, H * 0.55)
    a(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{random.choice([0.5, 0.6, 0.8, 1.1])}" '
      f'fill="#E8ECF0" opacity="{random.uniform(0.25, 0.8):.2f}"/>')

# Moon, top centre-right, clear of the cards
mx, my, mr = 700, 40, 26
a(f'<circle cx="{mx}" cy="{my}" r="80" fill="url(#halo)"/>')
a(f'<circle cx="{mx}" cy="{my}" r="{mr}" fill="url(#moon)"/>')
for dx, dy, cr in [(-8, -6, 4.5), (7, 3, 3.4), (-2, 10, 2.8), (10, -9, 2.2), (-12, 6, 1.7)]:
    a(f'<circle cx="{mx + dx}" cy="{my + dy}" r="{cr}" fill="#3A0505" opacity="0.45"/>')
# Inverted pentagram in a ring around the blood moon
pr = 36
a(f'<circle cx="{mx}" cy="{my}" r="{pr + 3}" fill="none" stroke="#B3200A" stroke-width="1.2" stroke-opacity="0.6"/>')
star = [(mx + pr * math.cos(math.radians(90 + 72 * k)), my + pr * math.sin(math.radians(90 + 72 * k)))
        for k in range(5)]
order = [star[(2 * k) % 5] for k in range(5)]
a('<path d="M' + ' L'.join(f'{x:.1f},{y:.1f}' for x, y in order) + ' Z" fill="none" '
  'stroke="#B3200A" stroke-width="1.2" stroke-opacity="0.6"/>')

# Far peaks with snow caps, fog, near ridge
far = ridge(250, 120, 38, 55, 13)
path(far, "#1A2028")
for (x0, y0), (x1, y1), (x2, y2) in zip(far[1:-3], far[2:-2], far[3:-1]):
    if y1 < y0 and y1 < y2 and y1 < 230:
        a(f'<path d="M{x1 - 14:.1f},{y1 + 22:.1f} L{x1:.1f},{y1:.1f} L{x1 + 14:.1f},{y1 + 22:.1f} '
          f'L{x1 + 5:.1f},{y1 + 15:.1f} L{x1 - 3:.1f},{y1 + 24:.1f} Z" fill="#C9D2DA" opacity="0.55"/>')
a(f'<rect y="{H * 0.45:.0f}" width="{W}" height="{H * 0.55:.0f}" fill="url(#fog)"/>')
path(ridge(318, 60, 26, 30, 7), "#0B0E12")

# Burning church (left): smoke, glow, building, flames, embers
cx = 300
smoke = (f"M{cx - 40},90 C{cx - 10},20 {cx + 120},40 {cx + 200},0 L{cx + 330},0 "
         f"C{cx + 220},40 {cx + 120},70 {cx + 30},110 Z")
a(f'<path d="{smoke}" fill="url(#smoke)"/>')
a(f'<path d="M{cx - 70},120 C{cx - 30},50 {cx + 60},60 {cx + 120},10 L{cx + 210},0 '
  f'C{cx + 110},60 {cx + 40},90 {cx - 10},130 Z" fill="url(#smoke)" opacity="0.7"/>')
a(f'<ellipse cx="{cx}" cy="150" rx="230" ry="170" fill="url(#fireglow)"/>')
tower_l, tower_r = cx - 95, cx - 45
building = [
    (cx - 110, 330), (cx - 110, 230), (tower_l, 230), (tower_l, 130),   # tower base
    (tower_l - 6, 130), (cx - 70, 70), (cx - 70, 40), (cx - 70, 70),     # spire
    (tower_r + 6, 130), (tower_r, 130), (tower_r, 205),
    (cx + 30, 150), (cx + 130, 205), (cx + 130, 330),                    # nave roof
]
path(building, "#0A0807")
# Inverted cross on the spire
a(f'<path d="M{cx - 72},14 L{cx - 68},14 L{cx - 68},30 L{cx - 60},30 L{cx - 60},34 L{cx - 68},34 '
  f'L{cx - 68},42 L{cx - 72},42 L{cx - 72},34 L{cx - 80},34 L{cx - 80},30 L{cx - 72},30 Z" fill="#0A0807"/>')
# Glowing windows
for wx, wy, ww, wh in [(cx - 80, 160, 10, 22), (cx + 0, 230, 12, 30), (cx + 45, 230, 12, 30),
                       (cx + 90, 230, 12, 30), (cx - 80, 250, 10, 26)]:
    a(f'<rect x="{wx}" y="{wy}" width="{ww}" height="{wh}" rx="5" fill="#FF9A2E"/>')
# Flames: roof, tower and spire, three layers each (red, orange, yellow)
for x0, x1, base, hgt, n, seed in [
    (cx - 40, cx + 130, 205, 120, 7, 1), (tower_l - 4, tower_r + 4, 132, 95, 3, 2),
    (cx - 80, cx - 60, 72, 60, 1, 3), (cx + 20, cx + 110, 175, 90, 4, 4),
]:
    flames(x0, x1, base, hgt, n, seed, "#B3200A", 0.95)
    flames(x0 + 6, x1 - 6, base, hgt * 0.72, n, seed + 10, "#FF6A13")
    flames(x0 + 14, x1 - 14, base, hgt * 0.45, max(1, n - 1), seed + 20, "#FFD24A")
# Embers rising into the top strip
random.seed(4)
for _ in range(70):
    ex = cx + random.uniform(-110, 220)
    ey = random.uniform(0, 150)
    a(f'<circle cx="{ex:.1f}" cy="{ey:.1f}" r="{random.uniform(0.8, 2.0):.1f}" '
      f'fill="{random.choice(["#FFB347", "#FF7A1A", "#FFD24A"])}" opacity="{random.uniform(0.4, 0.95):.2f}"/>')

# Dead pine forest
random.seed(31)
x = -10
while x < W + 10:
    h = random.uniform(55, 150)
    base = H + 4
    top = base - h
    w = h * random.uniform(0.20, 0.30)
    tiers = random.randint(5, 9)
    pts = [(x, top)]
    for i in range(1, tiers + 1):
        ty = top + h * 0.9 * i / tiers
        tw = w * (i / tiers) * random.uniform(0.7, 1.15)
        pts += [(x + tw, ty), (x + tw * 0.35, ty - h * 0.03)]
    pts += [(x + 2, base), (x - 2, base)]
    for i in range(tiers, 0, -1):
        ty = top + h * 0.9 * i / tiers
        tw = w * (i / tiers) * random.uniform(0.7, 1.15)
        pts += [(x - tw * 0.35, ty - h * 0.03), (x - tw, ty)]
    path(pts, "#000000")
    x += random.uniform(14, 42)

# Inverted-cross graves, raised clear of the faceplate in gallery view.
def inverted_cross(x, base, h, arm):
    t = base - h
    y = t + h * 0.62
    a(f'<path d="M{x - 2.5},{t} L{x + 2.5},{t} L{x + 2.5},{y} L{x + arm},{y} L{x + arm},{y + 5} '
      f'L{x + 2.5},{y + 5} L{x + 2.5},{base} L{x - 2.5},{base} L{x - 2.5},{y + 5} L{x - arm},{y + 5} '
      f'L{x - arm},{y} L{x - 2.5},{y} Z" fill="#1C1D20" stroke="#B3200A" stroke-width="1.1" stroke-opacity="0.8"/>')


for x, h in [(150, 50), (232, 40), (470, 54), (560, 44), (648, 50), (790, 40)]:
    inverted_cross(x, 398, h, 10)
a(f'<rect y="330" width="{W}" height="{H - 330}" fill="url(#bloodmist)"/>')

# Bare dead tree, far left
a('<g stroke="#000000" stroke-linecap="round" fill="none">'
  '<path d="M60,432 L66,330 L52,262 L58,214" stroke-width="7"/>'
  '<path d="M64,300 L100,268 L120,262" stroke-width="4"/>'
  '<path d="M58,262 L30,226 L22,196" stroke-width="4"/>'
  '<path d="M56,236 L76,198 L72,170" stroke-width="3"/>'
  "</g>")

# Falling snow and drifting ash
random.seed(9)
for _ in range(90):
    a(f'<circle cx="{random.uniform(0, W):.1f}" cy="{random.uniform(0, H):.1f}" '
      f'r="{random.uniform(0.7, 1.8):.1f}" fill="#F2F4F6" opacity="{random.uniform(0.15, 0.55):.2f}"/>')
a("</svg>")

here = os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(here, "bg.svg"), "w") as f:
    f.write("\n".join(out))
