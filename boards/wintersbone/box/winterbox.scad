// Winter guard enclosure
// MKR NB 1500 + EKM028 4x relay shield (UNO format) + LiPo 1500 mAh
// Origin = inner floor corner. All dimensions in mm.
// Values marked VERIFY are not from datasheets, measure with calipers.

part = "both";                  // "box", "lid", "both"

$fn = 48;

// ---------------------------------------------------------------- general
wall    = 2.4;
floor_t = 2.4;
lid_t   = 2.4;
in_x    = 156;                  // inner length
in_y    = 78;                   // inner width
in_h    = 42;                   // inner height (floor to lid)
r_out   = 4;                    // outer corner radius
clr     = 0.3;                  // general fit clearance

// Lid screws, M3 thread-forming into corner bosses
boss_d     = 8;
screw_d    = 2.6;               // pilot hole
screw_dep  = 14;
screw_cl   = 3.4;               // clearance hole in lid
screw_head = 6.2;               // counterbore for head
head_dep   = 1.2;

// Lid lip that locates the lid inside the box
lip_h = 3;
lip_t = 1.6;

// ---------------------------------------------------- relay shield EKM028
// UNO footprint 68.6 x 53.4, standard UNO mounting holes
rel_pos    = [10, 6];
rel_size   = [68.6, 53.4];
rel_holes  = [[14.0, 2.5], [15.3, 50.7], [66.1, 7.6], [66.1, 35.5]];
rel_so_h   = 6;
rel_so_d   = 6;
rel_hole_d = 2.6;               // M3 thread-forming

// ------------------------------------------------------------ MKR NB 1500
mkr_pos    = [82, 10];
mkr_size   = [67.6, 25.0];
mkr_holes  = [[2.5, 2.5], [2.5, 22.5],
              [65.1, 2.5], [65.1, 22.5]];      // VERIFY
mkr_so_h   = 6;
mkr_so_d   = 5;
mkr_hole_d = 2.2;               // M2.5 thread-forming

// ----------------------------------------------------- LiPo 1500 mAh
bat_pos  = [86, 42];
bat_size = [51, 35, 6];         // VERIFY (cell + margin)
bat_rim  = 1.6;
bat_wall = 5;

// ------------------------------------------------------------- wall holes
// [wall, offset along wall, diameter, z above inner floor]
// wall: "front" (y=0), "back" (y=in_y), "left" (x=0), "right" (x=in_x)
// PG7 = 12.5 mm, PG9 = 15.2 mm, SMA bulkhead = 6.5 mm
holes = [
    ["front", 28,  15.2, 22],   // relay load cables
    ["front", 58,  15.2, 22],   // relay load cables
    ["right", 25,  12.5, 22],   // DS18B20 #1
    ["right", 52,  12.5, 22],   // DS18B20 #2
    ["back",  120, 12.5, 22],   // USB / 5 V supply
    ["back",  95,  6.5,  30]    // SMA antenna bulkhead
];

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
        cylinder(d = d_out, h = h);
        translate([0, 0, 1])
            cylinder(d = d_hole, h = h);
    }
}

module corner_boss(p)
{
    cx = p[0] < in_x / 2 ? 0 : in_x - 1;
    cy = p[1] < in_y / 2 ? 0 : in_y - 1;

    hull() {
        translate([p[0], p[1], 0])
            cylinder(d = boss_d, h = in_h);
        translate([cx, cy, 0])
            cube([1, 1, in_h]);
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

module battery_cradle()
{
    translate(bat_pos)
        difference() {
            translate([-bat_rim, -bat_rim, 0])
                cube([bat_size[0] + 2 * bat_rim,
                      bat_size[1] + 2 * bat_rim, bat_wall]);
            translate([0, 0, -1])
                cube([bat_size[0], bat_size[1], bat_wall + 2]);
            // slot for the JST lead, facing the MKR
            translate([bat_size[0] / 2 - 5, -bat_rim - 1, -1])
                cube([10, bat_rim + 2, bat_wall + 2]);
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

            for (h = mkr_holes)
                translate([mkr_pos[0] + h[0], mkr_pos[1] + h[1], 0])
                    standoff(mkr_so_h, mkr_so_d, mkr_hole_d);

            battery_cradle();

            if (mount_tabs)
                tabs();
        }

        for (p = boss_pts())
            translate([p[0], p[1], in_h - screw_dep])
                cylinder(d = screw_d, h = screw_dep + 1);

        for (h = holes)
            wall_hole(h[0], h[1], h[2], h[3]);
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
            // notch the lip around the bosses
            translate([in_x - p[0], p[1], lid_t])
                cylinder(d = boss_d + 3, h = lip_h + 1);
        }
    }
}

if (part == "box") {
    box();
}
else if (part == "lid") {
    lid();
}
else {
    box();
    translate([0, in_y + 2 * wall + 30, -floor_t])
        lid();
}
