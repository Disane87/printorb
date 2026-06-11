// =====================================================================
// PrintOrb Desk Stand  —  "Orb" (puck on a hinged disc)
// Inspired by the smart-speaker / pocket-compact look:
//   * orb   : one-piece rounded puck, flat display face, domed closed
//             back, board drops in from the front
//   * bezel : thin snap-on front ring that frames the display and
//             retains the board
//   * disc  : low round base with a shallow cradle and a friction
//             clamshell hinge at the back — tilt by opening the orb
//
// Waveshare ESP32-S3-Touch-LCD-1.28 (round 240x240). Screwless.
// All dimensions parametric — measure your board and adjust [Device].
// =====================================================================

/* [Device] */
board_dia     = 39.5;   // [30:0.5:60]  round PCB diameter (mm)
board_thick   = 9.0;    // [5:0.5:18]   total device thickness front->back
fit_clear     = 0.4;    // [0:0.05:1.0] radial clearance around the board

/* [Orb body] */
wall          = 2.6;    // [1.8:0.1:5]  shell wall thickness
front_face_z  = 2.0;    // [0:0.5:8]    height of the flat front face above equator
back_wall     = 3.0;    // [2:0.2:5]    closed back-dome wall
ledge_w       = 2.0;    // [1:0.2:4]    internal ledge the board rests on

/* [Bezel ring] */
bezel_overlap = 2.6;    // [1.5:0.1:5]  radial overlap onto the display front
bezel_h       = 2.2;    // [1.5:0.1:4]  front ring thickness
bezel_lip     = 3.0;    // [1.5:0.2:6]  snap-lip depth into the orb

/* [Vents] */
vent_count    = 12;     // [0:1:24]     decorative equator vent slots
vent_w        = 2.2;    // [1:0.2:4]    slot width
vent_len      = 9;      // [4:0.5:16]   slot length (around circumference)

/* [USB-C cutout] */
usb_w         = 11;     // [6:0.5:16]
usb_h         = 6;      // [4:0.5:10]

/* [Hinge (friction)] */
peg_dia        = 6.0;   // [4:0.5:9]
socket_depth   = 3.8;   // [2:0.2:6]
friction_clear = -0.10; // [-0.4:0.05:0.3] (neg = interference)
orb_lug_w      = 9;     // [6:0.5:16]  width of the orb's central hinge lug
disc_lug_th    = 4;     // [3:0.2:7]   thickness of each disc hinge knuckle

/* [Disc base] */
disc_extra    = 3.0;    // [0:0.5:8]   disc radius beyond the orb
disc_h        = 7.0;    // [5:0.5:14]  disc thickness
foot_recess   = true;

/* [Placement (assembly tuning)] */
orb_cy        = 26;     // orb-centre height above table
orb_cyy       = 0;      // orb-centre Y offset (+back / -front)

/* [View] */
part      = "assembly"; // [assembly, orb, bezel, disc, plate]
lean      = 20;         // [0:5:45]   backward lean from vertical (assembly)

/* [Hidden] */
$fn = 110;
eps = 0.03;

// ---- derived -------------------------------------------------------
cav_r        = board_dia/2 + fit_clear;     // board cavity radius
orb_or       = cav_r + wall;                // orb max radius (equator)
orb_zr       = orb_or * 0.92;               // ellipsoid half-depth (front->back)
front_r      = sqrt(orb_or*orb_or * (1 - pow(front_face_z/orb_zr, 2))); // flat-face radius
disc_r       = orb_or + disc_extra;
tilt         = 90 - lean;                   // rotation from lying-flat to standing

// orb pocket geometry (in orb-local coords; front face at z = front_face_z)
pocket_top   = front_face_z;
pocket_bot   = front_face_z - board_thick;  // board back / ledge plane
hollow_bot   = -(orb_zr - back_wall);       // deepest interior point

// hinge axis in world coords (back edge of the disc, near its top)
Hy           = disc_r - 7;
Hz           = disc_h + 1.5;

// inverse-transform the world hinge axis into orb-local coords, so the
// pegs drawn on the orb land exactly in the disc sockets after assembly
dY           = Hy - orb_cyy;
dZ           = Hz - orb_cy;
lug_y        = dY*cos(tilt) + dZ*sin(tilt);
lug_z        = -dY*sin(tilt) + dZ*cos(tilt);
// anchor point on the orb's outer surface along the lug direction, so the
// hinge boss is hull-connected to the body (knuckle protrudes at the back)
lug_k        = sqrt(pow(lug_y/orb_or, 2) + pow(lug_z/orb_zr, 2));
anchor_y     = lug_y / lug_k;
anchor_z     = lug_z / lug_k;

// ====================================================================
// ORB  (orb-local: display faces +Z, equator at z=0, back dome at -Z)
// ====================================================================
module orb() {
    difference() {
        orb_outer();
        // board pocket: straight walls for the PCB, open at the front
        translate([0, 0, pocket_bot])
            cylinder(h = orb_zr + 12, r = cav_r);
        // inner ellipsoid: lightening cavity that follows the shell so the
        // back stays closed with an even wall (board rests on the step at -7)
        resize([2*(orb_or - wall), 2*(orb_or - wall), 2*(orb_zr - back_wall)])
            sphere(r = 1);
        // USB-C notch at the lower-back rim (same side as the hinge)
        translate([-usb_w/2, anchor_y - 4, pocket_bot + 1.2])
            cube([usb_w, wall + 8, usb_h]);
        // equator vent slots (sides + back, none across the front)
        orb_vents();
    }
    // hinge boss added AFTER the cavity cuts so it is always solid
    orb_hinge_lug();
}

module orb_outer() {
    difference() {
        // flattened ellipsoid (lens/pebble)
        resize([2*orb_or, 2*orb_or, 2*orb_zr]) sphere(r = 1);
        // flatten the front to a clean display face
        translate([0, 0, front_face_z + orb_zr])
            cube([4*orb_or, 4*orb_or, 2*orb_zr], center = true);
    }
}

module orb_hinge_lug() {
    // solid neck: hull from a sphere straddling the orb's outer surface out
    // to the peg-bearing boss at the hinge axis
    hull() {
        translate([0, anchor_y, anchor_z]) sphere(r = 5.5);
        translate([0, lug_y, lug_z]) rotate([0, 90, 0])
            cylinder(h = orb_lug_w, r = peg_dia/2 + 1.6, center = true);
    }
    for (s = [-1, 1])
        translate([s * orb_lug_w/2, lug_y, lug_z]) rotate([0, s*90, 0])
            cylinder(h = arm_gap_total(), r1 = peg_dia/2, r2 = peg_dia/2 - 0.8);
}

function arm_gap_total() = 0.6 + socket_depth;

module orb_vents() {
    for (i = [0 : vent_count - 1]) {
        a = 360/vent_count * i;
        // skip a front-facing window (+/- 55 deg around -Y is "front" after tilt;
        // here front of the orb is +Z, so vents on the equator are all on the rim —
        // keep them off the very bottom/top by placing around the equator band)
        rotate([0, 0, a])
            translate([0, orb_or, -1.5])
                rotate([90, 0, 0])
                    hull() {
                        translate([ (vent_len/2 - vent_w/2), 0, 0]) cylinder(h = wall*3, d = vent_w, center=true);
                        translate([-(vent_len/2 - vent_w/2), 0, 0]) cylinder(h = wall*3, d = vent_w, center=true);
                    }
    }
}

// ====================================================================
// BEZEL RING  (snaps onto the orb front; frames the display)
// ====================================================================
module bezel() {
    window_r = board_dia/2 - bezel_overlap;
    difference() {
        union() {
            // face ring
            cylinder(h = bezel_h, r = front_r);
            // snap lip into the board pocket
            translate([0, 0, -bezel_lip])
                cylinder(h = bezel_lip + eps, r = cav_r - 0.2);
        }
        // display window
        translate([0, 0, -bezel_lip - 1])
            cylinder(h = bezel_h + bezel_lip + 2, r = window_r);
        // soft inner chamfer on the front
        translate([0, 0, bezel_h - 0.9])
            cylinder(h = 1.0 + eps, r1 = window_r, r2 = window_r + 1.0);
    }
}

// ====================================================================
// DISC BASE  (low round puck, spherical cradle, back hinge knuckles)
// ====================================================================
module disc() {
    difference() {
        union() {
            disc_body();
            disc_hinge();
        }
        // cable relief notch at the back edge
        translate([0, disc_r - 1, disc_h/2])
            hull() {
                translate([0, 0, -disc_h]) cylinder(d = 11, h = eps);
                translate([0, 0,  disc_h]) cylinder(d = 11, h = eps);
            }
        // sockets for the orb pegs
        for (s = [-1, 1])
            translate([s * (orb_lug_w/2 + 0.6), Hy, Hz])
                rotate([0, s*90, 0])
                    cylinder(h = socket_depth + eps, r = peg_dia/2 + friction_clear);
        // rubber-foot recesses
        if (foot_recess)
            for (a = [45, 135, 225, 315])
                rotate([0, 0, a]) translate([disc_r - 7, 0, -eps])
                    cylinder(d = 8, h = 1);
    }
}

module disc_body() {
    r_edge = 2.0;
    minkowski() {
        cylinder(h = disc_h - r_edge, r = disc_r - r_edge);
        sphere(r = r_edge, $fn = 28);
    }
}

module disc_hinge() {
    // two ears straddling the orb lug, rising from the disc back to the axis
    ear_x = orb_lug_w/2 + 0.6 + disc_lug_th/2;
    for (s = [-1, 1])
        hull() {
            translate([s * ear_x, Hy, disc_h - 1])
                cube([disc_lug_th, 11, 2], center = true);
            translate([s * ear_x, Hy, Hz])
                rotate([0, 90, 0]) cylinder(h = disc_lug_th, r = 5.5, center = true);
        }
}

// ====================================================================
// ASSEMBLY / EXPORT
// ====================================================================
module orb_with_bezel() {
    orb();
    translate([0, 0, front_face_z - eps]) bezel();
}

module assembly() {
    color([0.86, 0.86, 0.88]) disc();
    // orb is modelled centred at the origin; rotate about its centre then
    // drop it onto the disc — the computed lug lands in the disc sockets
    translate([0, orb_cyy, orb_cy])
        rotate([tilt, 0, 0]) {
            color([0.86, 0.86, 0.88]) orb_with_bezel();
            demo_board();
        }
}

// non-printed: PCB + display, to sanity-check fit & read the look
module demo_board() {
    translate([0, 0, pocket_bot + 0.2])
        color([0.15, 0.5, 0.2]) cylinder(h = board_thick - 0.4, r = cav_r - 0.4);
    translate([0, 0, front_face_z - 0.6])
        color([0.05, 0.05, 0.07]) cylinder(h = 0.8, r = board_dia/2 - 1.5);
}

module plate() {
    // print layout: orb back-down, bezel face-down, disc bottom-down
    translate([-(orb_or + 6), 0, orb_zr]) rotate([180, 0, 0]) orb();
    translate([0, orb_or + bezel_overlap + 8, bezel_lip]) rotate([180, 0, 0]) bezel();
    translate([disc_r + 22, 0, 0]) disc();
}

if      (part == "assembly") assembly();
else if (part == "orb")      orb_with_bezel();
else if (part == "bezel")    bezel();
else if (part == "disc")     disc();
else                         plate();
