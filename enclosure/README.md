# Desk stand

> **Status:** work in progress. The design is settled on sketches; the 3D model comes after exact measurements of the board and the speaker.

A 3D-printed desk stand for the Freenove FNK0115 board.

## Design

- **Two parts:** a front frame around the screen and a wedge-shaped back. The wedge stands on the desk by itself with the screen tilted back 15°, like a desk alarm clock. No hinges or fold-out legs.
- **Fastening:** four 3 mm self-tapping screws for plastic clamp the board between the frame and the wedge. They go in from the back, through the board's corner holes, into posts on the frame. Length follows from the model, roughly 10–16 mm.
- **Speaker** (about 20 × 30 × 8 mm, membrane on one wide side, adhesive on the other) sits in a pocket at the bottom of the back wall: the membrane faces a grille in the wall, the adhesive side is glued to a divider printed with the case. The pocket is next to the speaker connector on the board's bottom edge.
- **Openings,** seen from the front: on the left a cutout for the USB-C cable, on the right small holes to press RESET and BOOT with a paperclip. Plastic over the ESP32 antenna at the right edge doesn't bother Wi-Fi or Bluetooth.
- **No microSD cutout:** the slot is not used.
- **Size,** rough, from photos of the board (Freenove publishes no dimensioned drawing): board about 136 × 80 mm, stand front about 144 × 88 mm, wedge about 65 mm deep at the base.

Plain PLA is enough for indoor use: about 100–150 g.

## Next steps

1. **Measure.** Board: width × height; distance between mounting hole centres and their diameter; thickness from the screen glass to the tallest part on the back; how far the glass sits from each board edge. Speaker: length × width × thickness.
2. **Model** in OpenSCAD, parametric, in this folder; two STL files from it: frame and wedge.
3. **Test print** and fit.
