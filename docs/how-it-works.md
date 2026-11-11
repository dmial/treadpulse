# How it works inside

## Walks

A walk starts when the belt starts moving. It ends when the belt has stood still (or the treadmill has been out of reach) for more than 5 minutes. A shorter stop is a pause within the same walk.

Each walk is a folder in the board's memory:

```
/walks/0042_2026-11-10T0815Z/samples.csv    one line per second
/walks/0042_2026-11-10T0815Z/segments.csv   stretches at constant speed and incline
/walks/0042_2026-11-10T0815Z/summary.csv    written when the walk ends
```

The folder name is the walk number and its start time in UTC. The number keeps growing across uploads and reflashes. If the board didn't know the time when the walk started, the date is replaced by `nodate` and a random tail: `0042_nodate_9f3a1c2e`.

**All times in the files are UTC**, e.g. `2026-11-10T08:15:23Z`. Only the clock on the screen uses the local time zone.

- **`summary.csv`**: number, start, end, walking time, distance, average and top speed, top incline, elevation gain, average and top pulse, number of segments. The pulse counts only seconds when the belt moved; without a strap these columns are empty. The last column, `recovered`, is `yes` for a walk closed after a power loss.
- **`samples.csv`**: every second: time, state (`walk` / `pause` / `no_data`), walking time, speed, incline, distance, the treadmill's own counters and the pulse.
- **`segments.csv`**: how long you walked at each speed and incline, with average and top pulse. A new segment opens once speed or incline has held a new value for 3 seconds, so stepping 3.5 → 3.6 → 3.7 → 3.8 km/h with the buttons is one segment, not four.

The files open in Numbers, Excel or anything that reads CSV.

## Uploads

Wi-Fi shares the radio with Bluetooth, so it stays off while you walk. The board goes online:

- at power-on, before it looks for the treadmill (at most 30 seconds of uploading; the rest waits until after the walk);
- right after a walk ends;
- every 10 minutes after a failed attempt, while no walk is going on.

A walk goes up file by file, the summary last. Once the receiver has confirmed all three files, the board deletes the walk. In the bucket, files keep the same paths: `walks/<walk>/<file>`.

About 9.9 MB of the board's memory is set aside for walks; an hour of walking takes about 250 KB, so roughly 40 hours fit. When memory runs out, starting a new walk deletes the oldest walk that is not uploaded yet.

The receiver (`cloud/`) only accepts writes: nothing can be read back through it. It never overwrites a stored file: the same file again answers "already stored" (a retried upload), a different file under the same name is refused (409).

## When things go wrong

**The treadmill link drops.** The treadmill reports running totals: distance and its own workout time. After reconnecting, the board fills the gap from those totals. Only the per-second detail of the gap is lost.

**The board loses power.** The per-second log is flushed every 10 seconds, so at most 10 seconds are lost. At the next power-on the board finds walks without a summary and closes them: it replays the per-second log, rewrites the segments and writes a summary with `recovered` = `yes`. The walk ends at the last moment the belt moved, as if the board had waited out the 5 minutes. A line cut in half by the power loss is skipped.

If the board restarts mid-walk while the treadmill keeps going, you get two walks: the recovered one and a new one.

## Code map

| Path | What it does |
|---|---|
| `lib/ftms/` | Parses treadmill data (FTMS). No Arduino dependencies |
| `lib/heart_rate/` | Parses heart rate strap data (Heart Rate Measurement). No Arduino dependencies |
| `lib/walk/` | Walking time, walks, segments, distance, elevation gain, pulse stats; the per-second log format and power-loss recovery. No Arduino dependencies |
| `lib/pulse/` | Heart rate zones, the hint to the next threshold, threshold beeps |
| `lib/day/` | Today's total and the walks on the day strip |
| `lib/screen/` | The dashboard (LVGL): layout, pulse chart, ring, day strip; Geologica fonts (`tools/make_fonts.sh`). No Arduino dependencies: the same code renders on a computer (`pio run -e sim`) |
| `src/ble_link.*` | Bluetooth: scanning, connecting, subscribing, reconnecting (NimBLE) |
| `src/treadmill_link.*` | Treadmill link on top of `ble_link` |
| `src/heart_rate_link.*` | Heart rate strap link on top of `ble_link` |
| `src/display.*` | The board's RGB panel and its link to LVGL |
| `src/storage.*` | The board's memory (LittleFS): walk logs, upload queue |
| `src/net.*` | Wi-Fi on for a session, then off |
| `src/uploader.*` | Uploads walks over HTTPS |
| `src/clock_sync.*` | Time from NTP |
| `src/beeper.*` | Beeps through the speaker (I2S) |
| `src/board.h` | Board pins, from Freenove's examples |
| `src/secrets.example.h` | Template for `src/secrets.h`: Wi-Fi, receiver address and key |
| `src/user_config.example.h` | Template for `src/user_config.h`: daily goal, zones, thresholds, time zone |
| `partitions.csv` | Memory layout: two 3 MB firmware slots, ~9.9 MB for walks |
| `cloud/` | The receiver: a Cloudflare Worker that stores files in R2 |
| `test/` | Tests for `lib/`, run on a computer: `pio test -e native` |

Pinned versions: ESP32 Arduino core 3.2.0 (the one Freenove tests its examples with), Arduino_GFX 1.6.5, NimBLE-Arduino 2, LVGL 9.5.

The first firmware build recompiles the Arduino core with the options in `custom_sdkconfig` (`platformio.ini`): code runs from PSRAM, so writing walk logs doesn't make the screen flicker. PlatformIO shares core packages between projects: building another project without these options brings back the stock core, and the next build here recompiles it again.
