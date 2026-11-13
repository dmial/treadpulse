// TreadPulse desk stand: a front frame and a wedge for the Freenove FNK0115 5" display board.
// Board numbers are from the drawing of the same board sold as LCDWiki ES3C50P (see README.md).
// build.py renders the STL files, runs the fit checks and refreshes viewer.html.
//
// Millimetres. x to the right, y away from the viewer, z up; the desk is z = 0.
// The screen box is drawn upright in "panel" coordinates (front face at y = 0, z up the face),
// tilted back by `tilt`, then cut flat at the desk.

// What to render: "assembly"; the printed parts "frame" and "wedge"; the parts the viewer
// shows but nobody prints ("board_pcb", "board_lcd", "board_metal", "board_white",
// "board_dark", "speaker", "screws"); a fit check ("check_*"); or "meta" for build.py.
part = "assembly";

$fa = 4;
$fs = 0.4;

/* [Board: FNK0115 = ES3C50P drawing] */
pcb = [137, 84];            // width, height
pcb_t = 1.6;
pcb_r = 3;                  // corner radius
hole_d = 3.2;
hole_pitch = [130.03, 76.95];
hole_left = 3.52;           // left edge to the left holes
hole_top = 3.55;            // top edge to the top holes
lcm = [120.7, 75.9];        // touch glass with the panel behind it
lcm_left = 8.15;
lcm_bottom = 4.05;
lcm_t = 4.2;
glass_to_pcb = 5.70;        // glass face to the board's front face
aa = [108.0, 64.76];        // lit area of the screen
aa_left = 14.5;
aa_bottom = 11.81;
back_h = 6.45;              // tallest part on the back
usb_z = 42.05;              // USB-C centre above the board's bottom edge, on the left edge
usb_h = 1.6;                // USB-C centre above the board's back face
btn_x = 137 - 5.15;         // RESET and BOOT on the back; x from the left edge seen from the front
btn_z = [20.57, 31.20];     // BOOT, RESET above the bottom edge
btn_top = 2.5;              // button top above the board's back face

// Parts on the back of the board, seen from the front: [x, z, width, height, thickness].
// x from the board's left edge, z from its bottom edge, scaled off the drawing (about ±1 mm).
back_metal = [
    [111.1, 45.2, 19.5, 17.65, 3.1],    // ESP32-S3 module shield
    [-0.3, 37.5, 7.6, 9.1, 3.2],        // USB-C socket
    [76.0, 0.6, 15.9, 15.3, 1.85],      // microSD slot
];
back_white = [
    [0.7, 18.4, 4.5, 9.7, 4.3],         // UART socket P1
    for (c = [65.30, 50.44, 35.58, 22.50]) [c - 4, 0.6, 8, 5, 4.3],  // sockets P3 to P6
    [11.74 - 2.75, 0.6, 5.5, 5, 4.3],   // speaker socket P7
];
back_dark = [
    [130.6, 45.2, 6.0, 17.65, 0.8],     // ESP32 module, antenna end
    [32.0, 46.9, 25.0, 4.1, 2.0],       // screen ribbon socket
    [33.0, 33.0, 23.0, 13.9, 0.3],      // screen ribbon
    [39.5, 67.6, 5.0, 5.0, 3.0],        // inductor
    [16.3, 59.4, 9.7, 5.0, 1.6],        // SOIC-14
    [18.4, 47.1, 6.9, 7.0, 1.8],        // regulator
    [18.4, 23.9, 6.9, 7.0, 1.8],        // regulator
    [17.0, 12.8, 6.0, 4.9, 1.6],        // audio amplifier
    [71.1, 47.6, 6.3, 5.5, 1.6],        // USB-serial chip
    [101.4, 23.9, 6.8, 11.0, 2.0],      // touch ribbon socket
    for (z = btn_z) [btn_x - 1.5, z - 2, 3, 4, btn_top],  // BOOT, RESET
];

/* [Speaker: measured with a ruler, about ±0.5 mm] */
spk = [40, 18, 10];         // length, height, thickness; the cable leaves one 18 x 10 end
spk_play = 1.5;             // slot between the back wall and the pad, past the thickness
cable_room = 10;            // free space at the cable end, next to the wedge's side wall
pad_over = 3;               // pad past each end of the speaker

/* [Case] */
tilt = 15;                  // the screen leans back from vertical
wall = 2.0;
face_t = 2.0;               // front plate
glass_gap = 0.3;            // front plate to glass
gap = 1.0;                  // board edge to inner wall
plug_room = 6;              // free space under the board's bottom edge for the speaker plug
back_gap = 1.45;            // tallest back part to the cover
cover_t = 2.4;
corner_r = 6;
base = 65;                  // depth of the wedge on the desk
foot_inset = 16;            // wedge sides, in from the screen box sides
window_margin = 0.4;        // window past the lit area, each side
window_chamfer = 1.0;
edge_chamfer = 0.6;

/* [Hardware] */
post_d = 6.4;
pilot_d = 2.5;              // for 3 mm self-tapping screws in PLA
standoff_d = 8;
screw_hole_d = 3.4;
head_d = 6.2;               // pocket for a pan head up to 6 mm across
head_depth = 2.2;
screw_len = 14;
clip_hole_d = 2.0;          // paperclip holes for RESET and BOOT

// Derived sizes
pcb_front = face_t + glass_gap + glass_to_pcb;   // board's front face
pcb_back = pcb_front + pcb_t;
cover_y = pcb_back + back_h + back_gap;          // front face of the wedge's cover
cover_back = cover_y + cover_t;
W = pcb[0] + 2 * (gap + wall);
pcb_x = wall + gap;
// Height up the face of the frame floor's top at depth y.
function floor_s(y) = (wall + y * sin(tilt)) / cos(tilt);
pcb_z = floor_s(pcb_back + 2.5) + plug_room;     // board's bottom edge
H = pcb_z + pcb[1] + gap + wall;                 // front face height, along the face
holes = [for (i = [0, 1], j = [0, 1])
    [pcb_x + hole_left + i * hole_pitch[0], pcb_z + pcb[1] - hole_top - j * hole_pitch[1]]];

// Side profile, desk coordinates [y, z]
function desk(y, s) = [y * cos(tilt) + s * sin(tilt), -y * sin(tilt) + s * cos(tilt)];
function cross2(a, b) = a[0] * b[1] - a[1] * b[0];
function meet(p1, d1, p2, d2) = p1 + d1 * (cross2(p2 - p1, d2) / cross2(d1, d2));
ridge = desk(cover_back, H);                     // top of the wedge's back wall
heel = [base, 0];                                // bottom of the wedge's back wall
bw_dir = (ridge - heel) / norm(ridge - heel);    // up the back wall
bw_in = [-bw_dir[1], bw_dir[0]];                 // from the back wall into the wedge
face_dir = [sin(tilt), cos(tilt)];               // up the front face
cav_front = [(cover_y - 1) / cos(tilt), 0];      // hollow reaches just past the cover
cav_a = meet(cav_front, face_dir, [0, wall], [1, 0]);
cav_b = meet(heel + bw_in * wall, bw_dir, [0, wall], [1, 0]);
cav_d = meet(heel + bw_in * wall, bw_dir, cav_front, face_dir);

pocket_x = [foot_inset + wall + cable_room, foot_inset + wall + cable_room + spk[0]];  // the speaker
pad_x = [pocket_x[0] - pad_over, pocket_x[1] + pad_over];
slot = spk[2] + spk_play;                        // back wall to the pad
ledge_u = slot * -bw_in[1] / bw_dir[1] + 0.2;    // lifts the speaker clear of the floor

// Helpers
module tilted() rotate([-tilt, 0, 0]) children();
module above_desk(z0 = 0) intersection() {
    children();
    translate([-100, -100, z0]) cube([400, 400, 300]);
}
// A 2D shape drawn in the x-z plane, extruded between depths y0 and y1.
module slab(y0, y1) rotate([90, 0, 0]) translate([0, 0, -y1]) linear_extrude(y1 - y0) children();
// A 2D side profile [y, z], extruded between x0 and x1.
module along_x(x0, x1) translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children();
// A cylinder along y, from depth y0 to y1, centred at panel point p = [x, z].
module tube(p, y0, y1, d) translate([p[0], y0, p[1]]) rotate([-90, 0, 0]) cylinder(d = d, h = y1 - y0);
// Children drawn as [x, v, u]: v from the back wall's inside into the wedge, u up the wall,
// origin where the back wall meets the floor.
module on_back_wall()
    multmatrix([[1, 0, 0, 0],
                [0, bw_in[0], bw_dir[0], cav_b[0]],
                [0, bw_in[1], bw_dir[1], cav_b[1]],
                [0, 0, 0, 1]]) children();

// Screen box outline seen from the front; runs past the desk so the cut makes the bottom.
module outline(inset = 0) hull() {
    for (x = [corner_r, W - corner_r]) translate([x, H - corner_r]) circle(r = corner_r - inset);
    translate([inset, -40]) square([W - 2 * inset, 1]);
}

// Frame: front plate with the window, walls around the board, posts the screws go into.
module frame() above_desk() difference() {
    union() {
        difference() {
            tilted() union() {
                hull() {
                    slab(0, 0.01) outline(edge_chamfer);
                    slab(edge_chamfer, edge_chamfer + 0.01) outline();
                }
                slab(edge_chamfer, cover_y) outline();
            }
            above_desk(wall) tilted() slab(face_t, cover_y + 1) outline(wall);
        }
        tilted() for (h = holes) tube(h, face_t - 0.01, pcb_front, post_d);
    }
    tilted() {
        window();
        usb_cutout();
        for (h = holes) tube(h, 1.0, pcb_front + 0.1, pilot_d);
    }
}

module window_rect(grow = 0)
    translate([pcb_x + aa_left - window_margin - grow, pcb_z + aa_bottom - window_margin - grow])
        offset(r = 1) offset(delta = -1)
            square([aa[0] + 2 * (window_margin + grow), aa[1] + 2 * (window_margin + grow)]);

module window() {
    slab(-1, face_t + 1) window_rect();
    hull() {
        slab(-0.01, 0) window_rect(window_chamfer);
        slab(window_chamfer, window_chamfer + 0.01) window_rect();
    }
}

// Opening for the USB-C plug, through the left wall: 14 mm along the edge, 9 mm deep.
module usb_cutout()
    translate([-1, pcb_back + usb_h, pcb_z + usb_z]) rotate([0, 90, 0])
        linear_extrude(wall + gap + 1) offset(r = 2) offset(delta = -2) square([14, 9], center = true);

// Wedge: the cover behind the board, the hollow foot it stands on, the speaker pocket.
module wedge() above_desk() union() {
    difference() {
        union() {
            tilted() slab(cover_y, cover_back) outline();
            along_x(foot_inset, W - foot_inset) foot_profile();
            above_desk(wall + 0.3) tilted() {
                for (h = holes) gusseted_tube(h, pcb_back, standoff_d);
                for (z = btn_z) gusseted_tube([pcb_x + btn_x, pcb_z + z], pcb_back + btn_top + 1, 4.4);
            }
        }
        along_x(foot_inset + wall, W - foot_inset - wall) cavity_profile();
        tilted() {
            for (h = holes) {
                tube(h, pcb_back - 0.5, cover_back + 1, screw_hole_d);
                tube(h, cover_back - head_depth, cover_back + 1, head_d);
            }
            for (z = btn_z) tube([pcb_x + btn_x, pcb_z + z], 0, cover_back + 1, clip_hole_d);
            labels();
        }
        grille();
    }
    speaker_pocket();
}

module foot_profile() polygon([[cover_y / cos(tilt), 0], heel, ridge, desk(cover_y, H)]);

// Inside of the foot: open toward the board, below a band along the top of the cover.
module cavity_profile() intersection() {
    polygon([cav_a, cav_b, cav_d]);
    polygon([[-300, -300], [300, -300],
             [300, (H - wall - 300 * sin(tilt)) / cos(tilt)],
             [-300, (H - wall + 300 * sin(tilt)) / cos(tilt)]]);
}

// A standoff (or paperclip guide) from depth y0 back to the cover, with a web under it so the
// wedge prints standing on its bottom without supports: 45° where there is room, a bit flatter
// (about 50°) at the bottom corners, where the web must stay clear of the frame's floor.
module gusseted_tube(p, y0, d) {
    need = (cover_y - y0) * tan(45 - tilt) + 0.5;
    room = p[1] - d / 2 - (wall + 0.3 + cover_y * sin(tilt)) / cos(tilt);
    drop = min(need, room);
    hull() {
        tube(p, y0, cover_y + 0.01, d);
        translate([p[0] - d / 2, cover_y - 0.01, p[1] - d / 2 - drop]) cube([d, 0.02, d / 2 + drop]);
    }
}

// Letters next to the paperclip holes, read from behind: B = BOOT, R = RESET.
module labels() for (i = [0, 1])
    slab(cover_back - 0.6, cover_back + 1)
        translate([pcb_x + btn_x + 4.6, pcb_z + btn_z[i]]) mirror([1, 0])
            text(i == 0 ? "B" : "R", size = 3.5, font = "Liberation Sans:style=Bold",
                 halign = "center", valign = "center");

// Slots in the back wall in front of the speaker's membrane, along most of its length.
grille_n = floor((spk[0] - 6) / 3.2) + 1;
module grille() on_back_wall() for (i = [0 : grille_n - 1])
    translate([(pocket_x[0] + pocket_x[1]) / 2 + (i - (grille_n - 1) / 2) * 3.2, 0.5, 0]) rotate([90, 0, 0])
        linear_extrude(wall + 1.5) hull()
            for (u = [ledge_u + 3, ledge_u + spk[1] - 3]) translate([0, u]) circle(d = 1.6, $fn = 16);

// The speaker stands on a ledge, membrane toward the grille, its adhesive side stuck to a pad
// parallel to the back wall. No walls at the ends: the exact length doesn't matter, and the cable
// leaves the end next to the side wall freely. The slot has play, so it goes in without smearing.
module speaker_pocket() on_back_wall() {
    len = pad_x[1] - pad_x[0];
    translate([pad_x[0], -0.01, -3]) cube([len, slot + 0.02, 3 + ledge_u]);              // ledge
    translate([pad_x[0], slot, -3]) cube([len, 1.6, 3 + ledge_u + spk[1] - 3]);         // pad
}

// Board, speaker and screws, for the viewer and the fit checks.
module board(kind) {
    if (kind == "pcb") difference() {
        slab(pcb_front, pcb_back) translate([pcb_x, pcb_z]) offset(r = pcb_r) offset(delta = -pcb_r) square(pcb);
        for (h = holes) tube(h, pcb_front - 1, pcb_back + 1, hole_d);
    }
    if (kind == "lcd")
        translate([pcb_x + lcm_left, face_t + glass_gap, pcb_z + lcm_bottom]) cube([lcm[0], lcm_t, lcm[1]]);
    if (kind == "metal") for (b = back_metal) back_part(b);
    if (kind == "white") for (b = back_white) back_part(b);
    if (kind == "dark") for (b = back_dark) back_part(b);
}
module back_part(b) translate([pcb_x + b[0], pcb_back - 0.01, pcb_z + b[1]]) cube([b[2], b[4] + 0.01, b[3]]);
module whole_board() tilted() for (k = ["pcb", "lcd", "metal", "white", "dark"]) board(k);

// Stuck to the pad: the play is left between the membrane and the grille.
module speaker() on_back_wall() translate([pocket_x[0], spk_play, ledge_u]) cube([spk[0], spk[2], spk[1]]);

// 3 x 14 mm pan-head self-tapping screws, as fitted.
module screws() tilted() for (h = holes) {
    tube(h, cover_back - head_depth, cover_back - head_depth + 2.1, 5.6);
    tube(h, cover_back - head_depth - screw_len, cover_back - head_depth, 3);
}

// Smallest USB-C plug body that has to pass the opening, plugged in.
module usb_plug()
    translate([-30, pcb_back + usb_h - 3.5, pcb_z + usb_z - 6.25]) cube([30 + pcb_x - 0.8, 7, 12.5]);

module meta() {
    echo(str("META W ", W));
    echo(str("META H ", H));
    echo(str("META tilt ", tilt));
    echo(str("META base ", base));
    echo(str("META height ", desk(0, H)[1]));
    echo(str("META screen ", [pcb_x + aa_left, face_t + glass_gap - 0.05, pcb_z + aa_bottom, aa[0], aa[1]]));
    echo(str("META holes ", holes));
    echo(str("META screw ", [cover_back - head_depth, screw_len]));
    echo(str("META foot ", [foot_inset, W - foot_inset]));
    echo(str("META speaker ", spk));
}

module assembly() {
    color("#d9d6cf") { frame(); wedge(); }
    tilted() {
        color("#e2a21c") board("pcb");
        color("#151515") board("lcd");
        color("silver") board("metal");
        color("white") board("white");
        color("#333") board("dark");
    }
    color("#222") speaker();
    color("silver") screws();
}

if (part == "frame") frame();
else if (part == "wedge") wedge();
else if (part == "board_pcb") tilted() board("pcb");
else if (part == "board_lcd") tilted() board("lcd");
else if (part == "board_metal") tilted() board("metal");
else if (part == "board_white") tilted() board("white");
else if (part == "board_dark") tilted() board("dark");
else if (part == "speaker") speaker();
else if (part == "screws") screws();
else if (part == "check_frame_board") intersection() { frame(); whole_board(); }
else if (part == "check_wedge_board") intersection() { wedge(); whole_board(); }
else if (part == "check_frame_wedge") intersection() { frame(); wedge(); }
else if (part == "check_speaker") intersection() { union() { frame(); wedge(); whole_board(); } speaker(); }
else if (part == "check_usb_plug") intersection() { frame(); tilted() usb_plug(); }
else if (part == "meta") meta();
else assembly();
