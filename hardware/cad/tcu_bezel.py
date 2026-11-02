#!/usr/bin/env python3
"""Parametric TCU bezel (base + shroud), rebuilt from hardware/3d-print/*.stl.

Every dimension below was measured from the original STL files (see README.md here).
Change a parameter, run the script, and it writes STEP + STL for both parts:

    pip install cadquery
    python3 hardware/cad/tcu_bezel.py            # writes hardware/cad/out/*.step|stl
    python3 hardware/cad/tcu_bezel.py --compare  # also reports deviation from the originals

Coordinate system (same as the original STLs): Z up, X along the top tube (+X = the end
with the snap barb), the base flange's underside at Z = 0, the shroud sitting on top at Z = 3.
"""
import argparse
import math
from pathlib import Path

import cadquery as cq

# --------------------------------------------------------------------------- parameters
P = dict(
    # Base flange (plate that sits on the top tube)
    flange_t=3.0,             # plate thickness
    flange_chamfer=1.5,       # 45 deg chamfer on the top outer edge
    flange_half_len=30.6,     # X of the rounded-corner centres (+/-)
    flange_half_w=16.5,       # half width (Y)
    flange_corner_r=6.75,     # outer corner radius
    nose_x=-41.35,            # mounting tab centre
    nose_r=8.5,               # mounting tab radius
    mount_hole_d=4.5,         # frame bolt clearance
    cbore_d=9.5,              # counterbore for the bolt head
    cbore_depth=1.0,
    screw_x=25.0,             # shroud screws at X = +/- screw_x
    screw_clear_d=2.3,        # M2 clearance in the base
    window_l=33.0,            # cable window through the flange
    window_w=18.0,
    window_r=2.0,
    # Skirt (goes into the frame opening)
    skirt_x=1.85,             # centre offset in X
    skirt_l=62.4,             # outer stadium length
    skirt_w=21.6,             # outer stadium width (= end diameter)
    skirt_wall=1.6,
    skirt_depth=10.0,         # below the flange
    skirt_chamfer=0.5,        # bottom outer edge
    # Snap barb on the +X end of the skirt
    barb_out=2.5,             # radial projection
    barb_bottom=-6.5,         # Z of the flat (retaining) face
    barb_flat=1.5,            # vertical land above the flat face
    barb_ramp=2.5,            # 45 deg lead-in height
    barb_half_angle=70.0,     # arc covered on each side of +X
    # Shroud (display surround)
    shroud_h=11.7,            # height above the flange
    shroud_bot_l=71.7,
    shroud_bot_w=30.0,
    shroud_bot_r=6.0,
    shroud_top_l=58.0,        # outer size at the top before the edge round
    shroud_top_w=28.5,
    shroud_top_r=8.5,
    shroud_top_fillet=1.0,
    pocket_l=45.0,            # display pocket
    pocket_w=25.04,
    pocket_r=6.0,
    lip_t=1.2,                # retaining lip thickness at the top
    lip_l=42.4,               # visible opening
    lip_w=22.46,
    lip_r=4.7,
    lip_chamfer=0.7,
    pilot_d=1.6,              # M2 self-tapping pilot holes
    pilot_depth=5.5,
)


# --------------------------------------------------------------------------- helpers
def rounded_rect(wp, l, w, r):
    return wp.sketch().rect(l, w).vertices().fillet(r).finalize()


def loft_rr(z, h, bottom, top):
    """Ruled loft between two rounded rectangles (l, w, r) from Z=z to Z=z+h."""
    s1 = cq.Sketch().rect(bottom[0], bottom[1]).vertices().fillet(bottom[2])
    s2 = cq.Sketch().rect(top[0], top[1]).vertices().fillet(top[2]) \
        .moved(cq.Location(cq.Vector(0, 0, h)))
    return cq.Workplane('XY').workplane(offset=z).placeSketch(s1, s2).loft(ruled=True)


def circle_hull_wire(circles, z=0.0):
    """Closed wire around convex circles given counter-clockwise as (x, y, r)."""
    n = len(circles)
    tangents = []
    for i in range(n):
        (x1, y1, r1), (x2, y2, r2) = circles[i], circles[(i + 1) % n]
        dx, dy = x2 - x1, y2 - y1
        d = math.hypot(dx, dy)
        phi = math.atan2(dy, dx)
        a = math.acos((r1 - r2) / d)
        ang = phi - a  # outward normal (outside is on the right going CCW)
        tangents.append(((x1 + r1 * math.cos(ang), y1 + r1 * math.sin(ang)),
                         (x2 + r2 * math.cos(ang), y2 + r2 * math.sin(ang)), ang))
    edges = []
    for i in range(n):
        start, end, ang_out = tangents[i]
        edges.append(cq.Edge.makeLine(cq.Vector(*start, z), cq.Vector(*end, z)))
        _, arc_start, ang_in = tangents[i]
        arc_end, _, ang_next = tangents[(i + 1) % n]
        cx, cy, r = circles[(i + 1) % n]
        a0, a1 = ang_in, ang_next
        while a1 < a0:
            a1 += 2 * math.pi
        if a1 - a0 > 1e-6:
            am = (a0 + a1) / 2
            edges.append(cq.Edge.makeThreePointArc(
                cq.Vector(*arc_start, z), cq.Vector(cx + r * math.cos(am), cy + r * math.sin(am), z),
                cq.Vector(*arc_end, z)))
    return cq.Wire.assembleEdges(edges)


# --------------------------------------------------------------------------- parts
def make_base(p=P):
    hx, hy, rc = p['flange_half_len'], p['flange_half_w'], p['flange_corner_r']
    cy = hy - rc
    circles = [(p['nose_x'], 0.0, p['nose_r']), (-hx, -cy, rc), (hx, -cy, rc), (hx, cy, rc),
               (-hx, cy, rc)]
    outline = circle_hull_wire(circles)
    flange = cq.Workplane('XY').add(cq.Face.makeFromWires(outline)).wires().toPending() \
        .extrude(p['flange_t'])
    flange = flange.faces('>Z').edges().chamfer(p['flange_chamfer'])

    # Skirt: stadium tube below the flange
    so = cq.Workplane('XY').center(p['skirt_x'], 0).slot2D(p['skirt_l'], p['skirt_w']) \
        .extrude(-p['skirt_depth'])
    so = so.faces('<Z').edges().chamfer(p['skirt_chamfer'])
    w = p['skirt_wall']
    si = cq.Workplane('XY').center(p['skirt_x'], 0) \
        .slot2D(p['skirt_l'] - 2 * w, p['skirt_w'] - 2 * w).extrude(-p['skirt_depth'] - 1)

    # Snap barb: revolved profile around the +X end's arc centre
    end_cx = p['skirt_x'] + p['skirt_l'] / 2 - p['skirt_w'] / 2
    r0 = p['skirt_w'] / 2
    zb = p['barb_bottom']
    prof = [(r0 - 0.8, zb), (r0 + p['barb_out'], zb), (r0 + p['barb_out'], zb + p['barb_flat']),
            (r0, zb + p['barb_flat'] + p['barb_ramp']), (r0 - 0.8, zb + p['barb_flat'] + p['barb_ramp'])]
    barb = cq.Workplane('XZ').polyline(prof).close() \
        .revolve(2 * p['barb_half_angle'], (0, 0, 0), (0, 1, 0)) \
        .rotate((0, 0, 0), (0, 0, 1), -p['barb_half_angle'])
    bb = barb.val().BoundingBox()
    if abs(bb.ymax + bb.ymin) > 0.01:  # revolve direction differs between OCC versions
        barb = barb.rotate((0, 0, 0), (0, 0, 1), 2 * p['barb_half_angle'])
    barb = barb.translate((end_cx, 0, 0))

    body = flange.union(so).union(barb).cut(si)

    # Holes
    top = cq.Workplane('XY').workplane(offset=p['flange_t'])
    body = body.cut(rounded_rect(top, p['window_l'], p['window_w'], p['window_r'])
                    .extrude(-p['flange_t'] - 1))
    body = body.cut(top.pushPoints([(-p['screw_x'], 0), (p['screw_x'], 0)])
                    .circle(p['screw_clear_d'] / 2).extrude(-p['flange_t'] - 1))
    body = body.cut(top.center(p['nose_x'], 0).circle(p['mount_hole_d'] / 2)
                    .extrude(-p['flange_t'] - 1))
    body = body.cut(top.center(p['nose_x'], 0).circle(p['cbore_d'] / 2).extrude(-p['cbore_depth']))
    return body


def make_shroud(p=P):
    z0 = p['flange_t']
    z1 = z0 + p['shroud_h']
    outer = loft_rr(z0, p['shroud_h'], (p['shroud_bot_l'], p['shroud_bot_w'], p['shroud_bot_r']),
                    (p['shroud_top_l'], p['shroud_top_w'], p['shroud_top_r']))
    try:
        outer = outer.faces('>Z').edges().fillet(p['shroud_top_fillet'])
    except Exception:  # fall back to a chamfer if the kernel rejects the fillet
        outer = outer.faces('>Z').edges().chamfer(p['shroud_top_fillet'])
    zl = z1 - p['lip_t']
    pocket = rounded_rect(cq.Workplane('XY').workplane(offset=z0 - 1), p['pocket_l'],
                          p['pocket_w'], p['pocket_r']).extrude(zl - z0 + 1)
    opening = rounded_rect(cq.Workplane('XY').workplane(offset=zl - 0.5), p['lip_l'], p['lip_w'],
                           p['lip_r']).extrude(p['lip_t'] + 1)
    body = outer.cut(pocket).cut(opening)
    # 45 deg chamfer on the inner top edge of the opening
    c = p['lip_chamfer']
    cham = loft_rr(z1 - c, c + 0.01, (p['lip_l'], p['lip_w'], p['lip_r']),
                   (p['lip_l'] + 2 * c, p['lip_w'] + 2 * c, p['lip_r'] + c))
    body = body.cut(cham)
    pins = cq.Workplane('XY').workplane(offset=z0 - 0.01) \
        .pushPoints([(-p['screw_x'], 0), (p['screw_x'], 0)]).circle(p['pilot_d'] / 2) \
        .extrude(p['pilot_depth'] + 0.01)
    return body.cut(pins)


# --------------------------------------------------------------------------- export
def compare(name, stl_path, out):
    import trimesh
    ref = trimesh.load(stl_path)
    new = trimesh.load(out / f'{name}.stl')
    diff = ref.difference(new).volume + new.difference(ref).volume
    print(f'{name}: original {ref.volume:8.1f} mm3, parametric {new.volume:8.1f} mm3, '
          f'symmetric difference {diff:6.1f} mm3 ({100 * diff / ref.volume:.1f} %)')
    print(f'   bounds original {ref.bounds.round(2).tolist()}')
    print(f'   bounds new      {new.bounds.round(2).tolist()}')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--compare', action='store_true')
    ap.add_argument('--out', default=str(Path(__file__).parent / 'out'))
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    base, shroud = make_base(), make_shroud()
    for name, part in (('base', base), ('shroud', shroud)):
        cq.exporters.export(part, str(out / f'{name}.step'))
        cq.exporters.export(part, str(out / f'{name}.stl'), tolerance=0.02, angularTolerance=0.1)
    asm = cq.Assembly().add(base, name='base', color=cq.Color(0.15, 0.15, 0.15)) \
        .add(shroud, name='shroud', color=cq.Color(0.8, 0.3, 0.1))
    asm.save(str(out / 'tcu_bezel_assembly.step'))
    print('wrote', ', '.join(sorted(f.name for f in out.iterdir())))
    if a.compare:
        ref_dir = Path(__file__).resolve().parents[1] / '3d-print'
        compare('base', ref_dir / 'base.stl', out)
        compare('shroud', ref_dir / 'shroud.stl', out)


if __name__ == '__main__':
    main()
