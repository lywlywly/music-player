"""Generate the app icon: note-wave.svg, and from it note-wave.ico and note-wave.icns.

The design and its geometry: docs/ui-styling.md, "App icon".

Usage:
    python make_icon.py

Writes the three icon files next to this script, and a PNG preview to preview/ (not committed).
Rasterizing needs ImageMagick (`magick` on PATH); the .ico and .icns containers are packed here.
"""

import math
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

SIZE = 256  # the SVG's viewBox
TILE_RADIUS = 56
CORAL, ROSE, WHITE = "#FF7A59", "#C2185B", "#FFFFFF"

STROKE = 14  # the stem and wave
WAVE_BASELINE = 74  # where the stem ends and the wave runs
WAVE_RADIUS = 14
WAVE_SEMICIRCLES = 3  # crest, trough, crest
FILLET_RADIUS = 9

ICO_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
# ICNS entry types holding PNG data, and their pixel sizes (ic11 to ic14 are the @2x sizes).
ICNS_TYPES = {b"icp4": 16, b"icp5": 32, b"icp6": 64, b"ic07": 128, b"ic08": 256, b"ic09": 512,
              b"ic10": 1024, b"ic11": 32, b"ic12": 64, b"ic13": 256, b"ic14": 512}


@dataclass(frozen=True)
class Ellipse:
    cx: float
    cy: float
    a: float  # semi-axis along the ellipse's own x
    b: float  # semi-axis along the ellipse's own y
    angle: float  # rotation in degrees, SVG convention (clockwise on screen)

    def point(self, t: float) -> tuple[float, float]:
        al = math.radians(self.angle)
        x = self.cx + self.a * math.cos(t) * math.cos(al) - self.b * math.sin(t) * math.sin(al)
        y = self.cy + self.a * math.cos(t) * math.sin(al) + self.b * math.sin(t) * math.cos(al)
        return x, y

    def rightmost(self) -> tuple[float, float]:
        # dx/dt = 0  ->  tan t = -(b sin al) / (a cos al)
        al = math.radians(self.angle)
        return self.point(math.atan2(-self.b * math.sin(al), self.a * math.cos(al)))

    def half_extents(self) -> tuple[float, float]:
        al = math.radians(self.angle)
        return (math.hypot(self.a * math.cos(al), self.b * math.sin(al)),
                math.hypot(self.a * math.sin(al), self.b * math.cos(al)))

    def contains(self, x: float, y: float) -> bool:
        al = math.radians(self.angle)
        dx, dy = x - self.cx, y - self.cy
        u = dx * math.cos(al) + dy * math.sin(al)
        v = -dx * math.sin(al) + dy * math.cos(al)
        return (u / self.a) ** 2 + (v / self.b) ** 2 <= 1

    def nearest(self, x: float, y: float, samples: int = 7200) -> tuple[float, tuple[float, float]]:
        best = min((self.point(2 * math.pi * i / samples) for i in range(samples)),
                   key=lambda p: math.dist(p, (x, y)))
        return math.dist(best, (x, y)), best


HEAD = Ellipse(cx=104, cy=180, a=32, b=24, angle=-22)


def fmt(v: float) -> str:
    return f"{v:.2f}".rstrip("0").rstrip(".")


def fillet_path(edge_x: float) -> str:
    """Fill the inside corner between the stem's inner edge (the line x = edge_x) and the head.

    The fillet circle sits left of the line, tangent to it (centre x = edge_x - r). Bisection on
    its height finds where its distance to the ellipse equals r, so it is tangent to the head too.
    The filled region runs from the line's tangent point along the circle to the head's tangent
    point, then back up the line from a point just inside the head.
    """
    r, cx = FILLET_RADIUS, edge_x - FILLET_RADIUS
    # Search from a diameter above the head's top, where the circle is clear of the head, down to
    # the stem's end: moving the circle down brings it closer to the head, and a centre inside the
    # head counts as too close.
    lo, hi = HEAD.cy - HEAD.half_extents()[1] - 2 * r, HEAD.rightmost()[1]
    for _ in range(60):
        mid = (lo + hi) / 2
        dist, _ = HEAD.nearest(cx, mid, samples=3600)
        too_far = dist > r and not HEAD.contains(cx, mid)
        lo, hi = (mid, hi) if too_far else (lo, mid)
    cy = (lo + hi) / 2
    _, (tx, ty) = HEAD.nearest(cx, cy)
    # Close the region through a point on the stem's edge r/2 below the head's tangent point,
    # which the assert confirms is inside the head, so the closing edge is hidden under the fill.
    inside_y = ty + r / 2
    assert HEAD.contains(edge_x, inside_y), "the fillet's closing point must lie inside the head"
    return (f"M{fmt(edge_x)} {fmt(cy)} A{fmt(r)} {fmt(r)} 0 0 1 {fmt(tx)} {fmt(ty)} "
            f"L{fmt(edge_x)} {fmt(inside_y)} Z")


def icon_svg() -> str:
    rx, ry = HEAD.rightmost()
    stem_x = rx - STROKE / 2  # the stem's outer edge is the head's vertical tangent

    line = [f"M{fmt(stem_x)} {fmt(ry)} V{fmt(WAVE_BASELINE)}"]
    x = stem_x
    for i in range(WAVE_SEMICIRCLES):
        x += 2 * WAVE_RADIUS
        sweep = 1 if i % 2 == 0 else 0  # 1 arcs over the top (crest), 0 under (trough)
        line.append(f"A{WAVE_RADIUS} {WAVE_RADIUS} 0 0 {sweep} {fmt(x)} {WAVE_BASELINE}")

    # Centre the glyph's bounding box on the tile.
    w, h = HEAD.half_extents()
    x_min, x_max = HEAD.cx - w, x + STROKE / 2
    y_min, y_max = WAVE_BASELINE - WAVE_RADIUS - STROKE / 2, HEAD.cy + h
    dx, dy = SIZE / 2 - (x_min + x_max) / 2, SIZE / 2 - (y_min + y_max) / 2

    h = HEAD
    return "\n".join([
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SIZE} {SIZE}">',
        '<defs><linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">'
        f'<stop offset="0" stop-color="{CORAL}"/><stop offset="1" stop-color="{ROSE}"/>'
        '</linearGradient></defs>',
        f'<rect width="{SIZE}" height="{SIZE}" rx="{TILE_RADIUS}" fill="url(#bg)"/>',
        f'<g transform="translate({fmt(dx)} {fmt(dy)})" fill="{WHITE}">',
        f'<ellipse cx="{fmt(h.cx)}" cy="{fmt(h.cy)}" rx="{fmt(h.a)}" ry="{fmt(h.b)}" '
        f'transform="rotate({fmt(h.angle)} {fmt(h.cx)} {fmt(h.cy)})"/>',
        f'<path d="{fillet_path(stem_x - STROKE / 2)}"/>',
        f'<path d="{" ".join(line)}" fill="none" stroke="{WHITE}" stroke-width="{STROKE}" '
        'stroke-linecap="round"/>',
        "</g>",
        "</svg>",
        "",
    ])


def rasterize(magick: str, svg: Path, sizes: set[int], work: Path) -> dict[int, bytes]:
    """Render svg once at 1024 px, then scale that master down to each size as RGBA PNG."""
    master = work / "master.png"
    # -density 1152 renders the 256-unit SVG well above 1024 px, so the master is a downscale.
    subprocess.run([magick, "-background", "none", "-density", "1152", str(svg),
                    "-resize", "1024x1024", f"PNG32:{master}"], check=True)
    pngs = {}
    for size in sorted(sizes):
        dest = work / f"{size}.png"
        # Without the timestamp chunks, the same SVG always gives the same bytes, so rerunning
        # the script changes the committed .ico and .icns only when the icon changes.
        subprocess.run([magick, str(master), "-filter", "Lanczos", "-resize", f"{size}x{size}",
                        "-define", "png:exclude-chunks=date,time", f"PNG32:{dest}"], check=True)
        pngs[size] = dest.read_bytes()
    return pngs


def pack_ico(pngs: dict[int, bytes]) -> bytes:
    """An ICO file whose images are PNGs (read by Windows Vista and later)."""
    header = struct.pack("<HHH", 0, 1, len(pngs))  # reserved, type 1 = icon, image count
    offset = len(header) + 16 * len(pngs)
    entries, data = b"", b""
    for size, png in pngs.items():
        # width and height (0 means 256), palette size, reserved, planes, bits per pixel,
        # byte count, offset of the image data
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png),
                               offset + len(data))
        data += png
    return header + entries + data


def pack_icns(pngs: dict[int, bytes]) -> bytes:
    """An ICNS file: big-endian (type, length) blocks, each holding a PNG."""
    body = b"".join(kind + struct.pack(">I", 8 + len(pngs[size])) + pngs[size]
                    for kind, size in ICNS_TYPES.items())
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def main() -> None:
    if sys.argv[1:]:
        sys.exit(__doc__)
    magick = shutil.which("magick")
    if not magick:
        sys.exit("needs ImageMagick's `magick` on PATH")

    out = Path(__file__).resolve().parent
    svg = out / "note-wave.svg"
    svg.write_text(icon_svg(), encoding="utf-8", newline="\n")

    with tempfile.TemporaryDirectory() as work:
        pngs = rasterize(magick, svg, set(ICO_SIZES) | set(ICNS_TYPES.values()), Path(work))
    (out / "note-wave.ico").write_bytes(pack_ico({s: pngs[s] for s in ICO_SIZES}))
    (out / "note-wave.icns").write_bytes(pack_icns(pngs))
    preview = out / "preview"
    preview.mkdir(exist_ok=True)
    (preview / "note-wave.png").write_bytes(pngs[256])
    print("note-wave.svg, note-wave.ico, note-wave.icns, preview/note-wave.png")


if __name__ == "__main__":
    main()
