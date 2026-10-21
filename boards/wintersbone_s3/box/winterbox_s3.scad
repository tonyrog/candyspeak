// Winter guard enclosure, S3 version
// LilyGO T-SIM7080G-S3 (18650 holder underneath) + 4-relay board.
// Origin = inner floor corner. All dimensions in mm.
// Values marked VERIFY are not from datasheets: measure with calipers.
//
// Layout, seen from above:
//
//   back  (y = in_y)   LTE + GPS SMA bulkheads
//   +------------------------------------------------------+
//   |        [ T-SIM7080G-S3 on tall standoffs, battery under ]|=  USB-C (right)
// T |                                                      |
// e c          [ 4-relay board, terminals to front ]       |
// m c                                                      |
//   |   ====== one clamp bar over all four cables ======   |
//   +------------------------------------------------------+
//   front (y = 0)      4 x PG9 for the 230 V cables
//
// The 230 V side is the front half; the low-voltage board sits behind it.
// Printed parts: part = "box", "lid", "clamps" (cable clamp bars) or "all".

part = "all";

$fn = 48;

// ---------------------------------------------------------------- general
wall    = 2.4;
floor_t = 2.4;
lid_t   = 2.4;
in_x    = 130;                  // inner length
in_y    = 120;                  // inner width
in_h    = 46;                   // inner height (floor to lid)
r_out   = 4;                    // outer corner radius
clr     = 0.3;                  // general fit clearance

// Lid screws, M3 thread-forming into corner bosses. The bosses hang from
// the top of the walls (45 degree support underneath) so they stay clear of
// the boards lower down.
boss_d     = 8;
screw_d    = 2.6;               // pilot hole
screw_dep  = 14;
screw_cl   = 3.4;               // clearance hole in lid
screw_head = 6.2;               // counterbore for head
head_dep   = 1.2;

// Lid lip that locates the lid inside the box
lip_h = 3;
lip_t = 1.6;

// ------------------------------------------------- LilyGO T-SIM7080G-S3
// About 110 x 32; the 18650 holder hangs under the board, so it stands on
// tall standoffs. Hole pattern from a published case for the same board,
// 105 x 28.4, M2.                                                  VERIFY
s3_size    = [110, 32];         // VERIFY
// USB-C end 1 mm from the right wall so a plug reaches it; back edge just in
// front of the corner bosses.
s3_pos     = [in_x - 1 - s3_size[0], in_y - boss_d - s3_size[1]];
s3_holes   = [[2.5, 1.8], [107.5, 1.8], [2.5, 30.2], [107.5, 30.2]];  // VERIFY
s3_so_h    = 24;                // battery holder height + 2 mm air   VERIFY
s3_so_d    = 5;
s3_hole_d  = 1.7;               // M2 thread-forming
s3_pcb_t   = 1.6;
// USB-C at the board's +x end, centred across it.
usb_w      = 13;                // plug overmould + margin
usb_h      = 8;
usb_up     = 1.6;               // receptacle centre above the PCB top  VERIFY
usb_dy     = 16;                // across the board from s3_pos[1]       VERIFY

// ------------------------------------------------------- 4-relay board
// The common 4-channel module, 75 x 55 with 3 mm holes inset 3.1.  VERIFY
// Screw terminals along the FRONT edge (y = rel_pos[1]).
rel_size   = [75, 55];          // VERIFY
rel_pos    = [in_x / 2 - rel_size[0] / 2, 22];
rel_holes  = [[3.1, 3.1], [71.9, 3.1], [3.1, 51.9], [71.9, 51.9]];  // VERIFY
rel_so_h   = 6;
rel_so_d   = 6;
rel_hole_d = 2.6;               // M3 thread-forming

// ------------------------------------------------------------- wall holes
// [wall, offset along wall, diameter, z above inner floor]
// wall: "front" (y=0), "back" (y=in_y), "left" (x=0), "right" (x=in_x)
// PG7 = 12.5 mm, PG9 = 15.2 mm, SMA bulkhead = 6.5 mm
// 25 mm pitch: a PG9 locknut is about 22 mm across the corners.
mains_x = [for (i = [0 : 3]) in_x / 2 + (i - 1.5) * 25];   // one per relay
sens_y  = [36, 62];             // PG7 for the DS18B20s, left wall
holes = concat(
    [for (x = mains_x) ["front", x, 15.2, 16]],
    [
        ["left", sens_y[0], 12.5, 16],  // DS18B20 inside
        ["left", sens_y[1], 12.5, 16],  // DS18B20 outside
        ["back", 30, 6.5, 34],      // SMA, LTE
        ["back", 60, 6.5, 34]       // SMA, GPS
    ]);

// ---------------------------------------------------------- cable clamps
// A bar screwed down over the cables just inside the glands, so a pull on
// the outside lands on the box and not on a screw terminal or a sensor
// joint. One bar over all four 230 V cables, one per sensor cable.
// [wall, boss offsets along the wall, cable offsets along the wall]
clamp_boss  = 7;
clamp_h     = 6;                // boss height (cable lies on the floor)
clamp_in    = 11;               // from the wall's inner face, past the locknut
clamp_w     = 8;                // bar width
clamp_t     = 4;                // bar thickness
clamp_groove = 5;               // half-round groove for the cable
// Teeth on the floor under each cable, between the bosses, across the cable
// so the bar presses it down into them.
teeth_h     = 1.5;
teeth_pitch = 2.5;
teeth_l     = 12;               // along the cable
clamps = [
    ["front", concat([mains_x[0] - 12.5], [for (x = mains_x) x + 12.5]), mains_x],
    ["left",  [sens_y[0] - 10, sens_y[0] + 10], [sens_y[0]]],
    ["left",  [sens_y[1] - 10, sens_y[1] + 10], [sens_y[1]]]
];

// ------------------------------------------------------------- antenna
// A flat pad on the inside of the back wall, for a flexible LTE antenna
// stuck on with its own tape, plus two slots for a cable tie.
ant_pad   = [50, 30];           // on the back wall, left of the SMA holes
ant_x     = 70;                 // left edge along the back wall
ant_z     = 8;
ant_t     = 1.2;

// ------------------------------------------------------------- wall tabs
mount_tabs = true;
tab_w      = 16;
tab_l      = 12;
tab_t      = 4;
tab_hole   = 4.5;

// ======================================================================

r_in = max(r_out - wall, 0.5);

function boss_pts() =
    [[boss_d / 2,        boss_d / 2],
     [in_x - boss_d / 2, boss_d / 2],
     [boss_d / 2,        in_y - boss_d / 2],
     [in_x - boss_d / 2, in_y - boss_d / 2]];

module rounded_box(size, r)
{
    hull()
        for (x = [r, size[0] - r], y = [r, size[1] - r])
            translate([x, y, 0])
                cylinder(r = r, h = size[2]);
}

module standoff(h, d_out, d_hole)
{
    difference() {
        union() {
            cylinder(d = d_out, h = h);
            cylinder(d1 = d_out + 4, d2 = d_out, h = 3);   // fillet foot
        }
        translate([0, 0, 1])
            cylinder(d = d_hole, h = h);
    }
}

module corner_boss(p)
{
    cx = p[0] < in_x / 2 ? 0 : in_x - 1;
    cy = p[1] < in_y / 2 ? 0 : in_y - 1;
    z0 = in_h - screw_dep - 2;

    hull() {
        translate([p[0], p[1], z0])
            cylinder(d = boss_d, h = in_h - z0);
        translate([cx, cy, z0 - boss_d * 1.3])
            cube([1, 1, in_h - z0 + boss_d * 1.3]);
    }
}

module wall_hole(w, o, d, z)
{
    len = wall + 2;

    if (w == "front")
        translate([o, -wall - 1, z]) rotate([-90, 0, 0])
            cylinder(d = d, h = len);
    else if (w == "back")
        translate([o, in_y - 1, z]) rotate([-90, 0, 0])
            cylinder(d = d, h = len);
    else if (w == "left")
        translate([-wall - 1, o, z]) rotate([0, 90, 0])
            cylinder(d = d, h = len);
    else
        translate([in_x - 1, o, z]) rotate([0, 90, 0])
            cylinder(d = d, h = len);
}

// Floor position of the point offset o along wall w, clamp_in from it.
function clamp_pt(w, o) =
    w == "front" ? [o, clamp_in] :
    w == "back"  ? [o, in_y - clamp_in] :
    w == "left"  ? [clamp_in, o] :
                   [in_x - clamp_in, o];

module clamp_bosses()
{
    for (c = clamps, o = c[1])
        translate(clamp_pt(c[0], o))
            cylinder(d = clamp_boss, h = clamp_h);
}

// Angle that turns local x along the wall, y into the box, onto wall w.
function wall_rot(w) =
    w == "front" ? 0 : w == "back" ? 180 : w == "left" ? -90 : 90;

module clamp_teeth()
{
    for (c = clamps, i = [0 : len(c[2]) - 1]) {
        o  = c[2][i];
        lo = max([for (b = c[1]) if (b < o) b]);
        hi = min([for (b = c[1]) if (b > o) b]);
        w  = hi - lo - clamp_boss - 1;

        translate(clamp_pt(c[0], (lo + hi) / 2))
            rotate([0, 0, wall_rot(c[0])])
                for (y = [-teeth_l / 2 : teeth_pitch : teeth_l / 2 - teeth_pitch])
                    translate([-w / 2, y, 0])
                        rotate([90, 0, 90])
                            linear_extrude(w)
                                polygon([[0, 0], [teeth_pitch, 0],
                                         [teeth_pitch / 2, teeth_h]]);
    }
}

module clamp_holes()
{
    for (c = clamps, o = c[1])
        translate(concat(clamp_pt(c[0], o), 1))
            cylinder(d = rel_hole_d, h = clamp_h + 1);
}

module usb_cutout()
{
    z = s3_so_h + s3_pcb_t + usb_up;
    translate([in_x - 1, s3_pos[1] + usb_dy - usb_w / 2, z - usb_h / 2])
        cube([wall + 2, usb_w, usb_h]);
}

module antenna_pad()
{
    translate([ant_x, in_y - ant_t, ant_z])
        difference() {
            cube([ant_pad[0], ant_t, ant_pad[1]]);
            // cable-tie slots
            for (x = [6, ant_pad[0] - 9])
                translate([x, -1, ant_pad[1] / 2 - 4])
                    cube([3, ant_t + 2, 8]);
        }
}

module tabs()
{
    for (x = [-wall - tab_l, in_x + wall])
        translate([x, in_y / 2 - tab_w / 2, -floor_t])
            difference() {
                cube([tab_l, tab_w, tab_t]);
                translate([tab_l / 2 + (x < 0 ? -1 : 1),
                           tab_w / 2, -1])
                    cylinder(d = tab_hole, h = tab_t + 2);
            }
}

module box()
{
    difference() {
        union() {
            difference() {
                translate([-wall, -wall, -floor_t])
                    rounded_box([in_x + 2 * wall, in_y + 2 * wall,
                                 in_h + floor_t], r_out);
                rounded_box([in_x, in_y, in_h + 1], r_in);
            }

            for (p = boss_pts())
                corner_boss(p);

            for (h = rel_holes)
                translate([rel_pos[0] + h[0], rel_pos[1] + h[1], 0])
                    standoff(rel_so_h, rel_so_d, rel_hole_d);

            for (h = s3_holes)
                translate([s3_pos[0] + h[0], s3_pos[1] + h[1], 0])
                    standoff(s3_so_h, s3_so_d, s3_hole_d);

            clamp_bosses();
            clamp_teeth();
            antenna_pad();

            if (mount_tabs)
                tabs();
        }

        for (p = boss_pts())
            translate([p[0], p[1], in_h - screw_dep])
                cylinder(d = screw_d, h = screw_dep + 1);

        for (h = holes)
            wall_hole(h[0], h[1], h[2], h[3]);

        clamp_holes();
        usb_cutout();
    }
}

module lid()
{
    ix = in_x - 2 * clr;
    iy = in_y - 2 * clr;

    difference() {
        union() {
            translate([-wall, -wall, 0])
                rounded_box([in_x + 2 * wall, in_y + 2 * wall, lid_t],
                            r_out);

            // locating lip, printed facing up
            translate([clr, clr, lid_t])
                difference() {
                    rounded_box([ix, iy, lip_h], r_in);
                    translate([lip_t, lip_t, -1])
                        rounded_box([ix - 2 * lip_t, iy - 2 * lip_t,
                                     lip_h + 2], max(r_in - lip_t, 0.5));
                }
        }

        // lid is printed upside down: x mirrored so holes match the box
        for (p = boss_pts()) {
            translate([in_x - p[0], p[1], -1])
                cylinder(d = screw_cl, h = lid_t + lip_h + 2);
            translate([in_x - p[0], p[1], -1])
                cylinder(d = screw_head, h = head_dep + 1);
            translate([in_x - p[0], p[1], lid_t])
                cylinder(d = boss_d + 3, h = lip_h + 1);
        }
    }
}

// One clamp bar, laid along x from its first boss: clearance holes at the
// bosses, a groove across it for each cable. Printed groove up.
module clamp_bar(c)
{
    b0 = c[1][0];
    l  = c[1][len(c[1]) - 1] - b0 + clamp_boss + 1;

    difference() {
        translate([-clamp_boss / 2 - 0.5, -clamp_w / 2, 0])
            cube([l, clamp_w, clamp_t]);
        for (o = c[1])
            translate([o - b0, 0, -1])
                cylinder(d = screw_cl, h = clamp_t + 2);
        for (o = c[2])
            translate([o - b0, 0, clamp_t])
                rotate([90, 0, 0])
                    cylinder(d = clamp_groove, h = clamp_w + 2, center = true);
    }
}

module clamps_set()
{
    for (i = [0 : len(clamps) - 1])
        translate([0, i * (clamp_w + 4), 0])
            clamp_bar(clamps[i]);
}

if (part == "box") {
    box();
}
else if (part == "lid") {
    lid();
}
else if (part == "clamps") {
    clamps_set();
}
else {
    box();
    translate([0, in_y + 2 * wall + 30, -floor_t])
        lid();
    translate([in_x + 40, 0, -floor_t])
        clamps_set();
}
