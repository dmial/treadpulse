# Desk stand

> **Status:** the model is ready to print once two things are checked: the speaker's size and the board's size (see [Before printing](#before-printing)).

A 3D-printed desk stand for the Freenove FNK0115 board: a frame around the screen and a wedge behind it that holds the screen tilted back 15°, like a desk alarm clock.

**See it in 3D:** [open the 3D view](https://dmial.github.io/treadpulse/enclosure/viewer.html). You can turn the stand, take it apart, cut it open and show its sizes. It is [viewer.html](viewer.html) from this folder, published with GitHub Pages; a downloaded copy opens in a browser too. Either way it loads its 3D library from the internet. GitHub also shows [frame.stl](frame.stl) and [wedge.stl](wedge.stl) in 3D.

## Files

| File | What it is |
| --- | --- |
| `frame.stl`, `wedge.stl` | the two parts to print |
| `viewer.html` | the 3D view, with the board, speaker and screws in place |
| `case.scad` | the model the STL files are made from; all sizes are numbers at the top |
| `build.py` | remakes the STL files and the 3D view after a change, and checks the fit |

## Design

- **Two parts.** The frame covers the edges of the screen and wraps around the board. The wedge closes the back and stands on the desk by itself. No hinges or fold-out legs.
- **Four screws** hold it all together: from the back, through the wedge's corners and the board's corner holes, into posts on the frame. The wedge is 16 mm narrower than the frame on each side, so the corner screws can be reached from behind.
- **Speaker** stands in a pocket at the bottom of the wedge's back wall, on the same side as the board's speaker socket. The membrane faces the slots in the back wall; the adhesive side sticks to a divider in front of it.
- **Openings:** USB-C on the left side, 14 × 9 mm, wide enough for common cable plugs. Two paperclip holes on the back of the right side, marked R (RESET) and B (BOOT). No microSD opening: the slot is not used. Plastic over the ESP32 antenna doesn't bother Wi-Fi or Bluetooth.
- **Size:** 143 mm wide and 95 mm tall from the front, 65 mm deep; about 95 g of PLA. The border under the screen is wider than above it: the speaker plug sits below the board.

## Board dimensions

Freenove publishes no drawing, but the board is the same as the LCDWiki ES3C50P made by QDtech: the schematics match, and so does the layout of the back side. Numbers below are from its [drawing (PDF)](https://www.lcdwiki.com/res/ES3C50P/5inch_ESP32S3_Size.pdf), in mm, ±0.2. Seen from the front: USB-C on the left, connectors along the bottom edge.

- **Board:** 137.00 × 84.00, 1.60 thick, corners rounded R3.
- **Mounting holes:** four, Ø3.20; centres 130.03 × 76.95 apart, about 3.5 from each edge.
- **Screen glass** (touch glass on top of the panel): 120.70 × 75.90, centred: 8.15 from the sides, 4.05 from the top and bottom edges.
- **Visible window:** 109.20 × 66.05, shifted up: 6.81 from the board's top edge, 11.14 from the bottom, about 13.9 from the sides. The lit area inside it is 108.00 × 64.76.
- **Thickness:** 13.75 in all: from the glass face to the board 5.70, the board 1.60, the tallest part on the back 6.45.
- **USB-C:** left edge, centre 42.00 from the bottom edge (mid-height). The 4-pin UART socket is on the same edge, centre 23.48 from the bottom.
- **RESET and BOOT:** on the back face, centres 5.15 from the right edge; BOOT 20.57 and RESET 31.20 from the bottom edge.
- **Speaker connector:** bottom edge, centre 11.75 from the left edge.

## Before printing

1. **Measure the speaker:** length × width × thickness. The model assumes 30 × 20 × 8 mm. If yours differs, change `spk` at the top of `case.scad` and run `build.py`.
2. **Check the board with a ruler:** it should be 137 × 84 mm. That confirms it matches the drawing the model is built on.

## Print

| | Frame | Wedge |
| --- | --- | --- |
| On the bed | face down | standing on its bottom, as on the desk |
| Supports | none | none |
| Plastic | about 35 g | about 60 g |

For both parts: PLA (matte hides layer lines best), 0.4 mm nozzle, 0.2 mm layers, 4 wall loops so the 2 mm walls print solid, 15% infill, no brim. A textured plate gives the frame a textured front.

## Buy

- 4 self-tapping screws for plastic, 3 × 14 mm, pan head no wider than 6 mm (sold as "PT K30×14" or "ST3.0×14").

## Assembly

1. Put the speaker in its pocket: reach in through the wedge's open front, membrane toward the slots, adhesive side toward the divider, and press it to stick.
2. Lay the frame face down on a soft cloth. Drop the board in, screen down, with the USB-C socket at the opening in the side wall.
3. Plug the speaker into the board's speaker socket, marked "Speak", on the bottom edge.
4. Put the wedge on so its four corner holes sit over the board's corner holes.
5. Drive in the four screws until they are snug, and stop there: threads in PLA strip easily.

## Changing the model

All sizes are numbers at the top of `case.scad`. After a change, run:

```bash
python3 enclosure/build.py
```

It remakes `frame.stl` and `wedge.stl`, checks that the board, the speaker and a USB-C plug fit and that both parts print without supports, and refreshes the 3D view. It needs OpenSCAD 2025 or newer; on macOS: `brew install --cask openscad@snapshot`. Once the change is pushed, GitHub Pages updates the published 3D view within a few minutes.
