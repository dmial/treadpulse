# How it works inside

## Walks

A walk starts when the belt starts moving. It ends a few seconds after you press stop on the treadmill, if the treadmill reports it over Bluetooth (the Fitness Machine Status characteristic; the Merach W50 does). Otherwise it ends when the belt has stood still (or the treadmill has been out of reach) for more than 5 minutes. The treadmill's pause button, or any shorter stop, is a pause within the same walk. Starting again after a reported stop is a new walk, just as the treadmill starts a new workout.

A walk with less than a minute of walking (pauses don't count) is not kept: an accidental start or a quick try of the buttons leaves no trace in the board's memory, the cloud or the day's total, and the screen goes back to the previous walk.

Each walk is a folder in the board's memory:

```
/walks/0042_2026-11-10T0815Z/samples.csv    one line per second
/walks/0042_2026-11-10T0815Z/segments.csv   stretches at constant speed and incline
/walks/0042_2026-11-10T0815Z/summary.csv    written when the walk ends
```

The folder name is the walk number and its start time in UTC. The number keeps growing across uploads and reflashes. If the board didn't know the time when the walk started, the date is replaced by `nodate` and a random tail: `0042_nodate_9f3a1c2e`.

**All times in the files are UTC**, e.g. `2026-11-10T08:15:23Z`. Only the clock on the screen uses the local time zone.

- **`summary.csv`**: number, start, end, walking time, distance, steps, average and top speed, top incline, elevation gain, average and top pulse, number of segments. The pulse counts only seconds when the belt moved; without a strap these columns are empty. The last column, `recovered`, is `yes` for a walk closed after a power loss.
- **`samples.csv`**: every second: time, state (`walk` / `pause` / `no_data`), walking time, speed, incline, distance, the treadmill's own counters, the pulse and the steps so far.
- **`segments.csv`**: how long you walked at each speed and incline, with average and top pulse. A new segment opens once speed or incline has held a new value for 3 seconds, so stepping 3.5 → 3.6 → 3.7 → 3.8 km/h with the buttons is one segment, not four.

The files open in Numbers, Excel or anything that reads CSV.

## Steps

The treadmill doesn't count steps, so the board estimates them from the belt speed: each second of walking adds a second's worth of steps at the current speed. Steps per minute follow a straight line in speed, set in `src/user_config.h`:

> steps per minute = `kStepsPerMinuteBase` + `kStepsPerMinutePerKmh` × speed in km/h

The walk's steps show under its timer, today's inside the ring, and each walk's go to `summary.csv` and `samples.csv`. If the treadmill link drops mid-walk, the gap is filled at the speed the treadmill shows once it is back.

The example values, 57 + 11.4 × speed, were measured on one walker, 187 cm tall: 99 steps a minute at 3.7 km/h, 111 at 4.7. Over 3.0–5.0 km/h the line was within 2 steps a minute of every count, and incline from 1 to 11 % made no difference. Your steps will differ (taller people take fewer, longer ones), so measure your own:

1. Fill in a table like this one, with the speeds and inclines you actually walk at. Leave out the cells you never use.

   | Incline \ speed, km/h | 3.0 | 3.5 | 4.0 | 4.5 | 5.0 |
   |---|---|---|---|---|---|
   | the lowest you use | | | | | |
   | in between | | | | | |
   | the steepest you use | | | | | |

2. After changing speed or incline, walk 30 seconds before counting, so the step settles.
3. Count for exactly one minute by a timer. Counting one foot and doubling is easier than counting every step.
4. Count the cells you walk at most three times, on different walks, and take the average: one count by hand is off by 2–3 steps.
5. If the rows come out alike, incline doesn't matter: fit one straight line through all the counts. Any spreadsheet does it (`INTERCEPT` and `SLOPE`, or a trend line on a chart): the intercept is `kStepsPerMinuteBase`, the slope is `kStepsPerMinutePerKmh`. If the rows differ by more than a few steps, fit the line to the incline you walk at most; the formula has no incline term.

Outside the measured speeds, such as the seconds of speeding up and slowing down, the estimate is rougher, but those seconds barely move a walk's total.

## History

Walk folders leave the board once they are uploaded, so the board also keeps a short line for every walk it has kept, in `/history.csv`, and never deletes it. It works without the cloud, and making room for a new walk deletes only walk folders, never the history. About 100 bytes a walk: years of walking take less than a megabyte.

Each line: start, end, walking time, steps, distance, elevation gain, walking seconds with a pulse, the same seconds by heart rate zone (below zone 1, then zones 1 to 5), average and top pulse. The zones are counted with the borders in `src/user_config.h` on the day of the walk and stay as counted: after a new test moves the borders, older walks still show the zones they were walked in. Without a strap the pulse columns are 0 and empty.

A walk that ends before the board knows the time gets its line once the clock is set; if the power goes before that, it has none (just as it is missing from the day ring). A walk recovered after a power loss gets its line at the next power-on.

**The statistics page** (a sideways swipe on the dashboard) shows the last 14 days from this history plus the walk in progress. A walk counts on the day it started, as on the day ring. The average day, its minutes by pulse zone and the days on goal count the days since the history began, today included; days before it show no bar, a day without walking shows a dash. The dashboard comes back when the belt starts.

**Walks from before the history.** Their per-second logs are in the cloud, and a computer-side tool turns them into history lines with the same code the board runs:

1. Download each walk's `samples.csv` from the bucket into a folder per walk: `<dir>/0042_2026-11-10T0815Z/samples.csv`.
2. `pio run -e backfill && .pio/build/backfill/program history <dir> src/history_seed.h`
3. Flash the firmware. At power-on it adds the walks its history lacks (a walk starting within a minute of one already there is the same walk), so flashing it again changes nothing.

`src/history_seed.h` holds your walks, so git ignores it. Distance and walking time come from the replay, not from the log, so the lines match what the board would write today.

**Walk files written by an older firmware.** The same tool rewrites them as today's firmware would: `program rewrite <dir> <out dir>` replays each `samples.csv`, puts the replayed walking time, distance and steps into its columns (time, pulse, speed and incline stay as logged) and rebuilds `segments.csv` and `summary.csv`; walks with under a minute of walking are left out. The receiver never overwrites a file, so the rewritten files go to the bucket with `wrangler r2 object put`. This is how the walks of 2026-11-11 and 12 were fixed: the firmware before then counted the distance twice and wrote no steps.

## Uploads

Wi-Fi shares the radio with Bluetooth, so it stays off while you walk. The board goes online:

- at power-on, before it looks for the treadmill (at most 30 seconds of uploading; the rest waits until after the walk);
- a minute after a walk ends, so that starting again right away isn't held up by Wi-Fi;
- after a failed attempt, while no walk is going on: every minute while the clock is not set yet (without it the daily ring shows only walks since power-on), otherwise every 10 minutes.

Each time it tries to join the network twice, 10 seconds each, and writes to the serial log why the network refused.

A walk goes up file by file, the summary last. Once the receiver has confirmed all three files, the board deletes the walk. In the bucket, files keep the same paths: `walks/<walk>/<file>`.

About 9.9 MB of the board's memory is set aside for walks; an hour of walking takes about 250 KB, so roughly 40 hours fit. When memory runs out, starting a new walk deletes the oldest walk that is not uploaded yet.

The receiver (`cloud/`) only accepts writes: nothing can be read back through it. It never overwrites a stored file: the same file again answers "already stored" (a retried upload), a different file under the same name is refused (409).

## When things go wrong

**The treadmill link drops.** The treadmill reports running totals: distance and its own workout time. After reconnecting, the board fills the gap from those totals. Only the per-second detail of the gap is lost.

**The treadmill's totals read zero.** They do during a pause (and come back where they were when you resume) and after a stop (a new workout counts up from zero). The board takes a zero as "no reading", not as a reset, so distance never counts twice. The Merach W50 also sends, about once a walk, a single reading with speed, distance and time all at zero while the belt keeps moving; the board drops it. A real stop looks different: the speed drops to zero first, while distance and time still hold.

**The board loses power.** The per-second log is flushed every 10 seconds, so at most 10 seconds are lost. At the next power-on the board finds walks without a summary and closes them: it replays the per-second log, rewrites the segments and writes a summary with `recovered` = `yes`. The walk ends at the last moment the belt moved, as if the board had waited out the 5 minutes; one with less than a minute of walking is deleted instead. A line cut in half by the power loss is skipped. The recovered walk is added to the history.

If the board restarts mid-walk while the treadmill keeps going, you get two walks: the recovered one and a new one.

## Code map

| Path | What it does |
|---|---|
| `lib/ftms/` | Parses treadmill data (FTMS). No Arduino dependencies |
| `lib/heart_rate/` | Parses heart rate strap data (Heart Rate Measurement). No Arduino dependencies |
| `lib/walk/` | Walking time, walks, segments, distance, elevation gain, pulse stats; the per-second log format and power-loss recovery. No Arduino dependencies |
| `lib/pulse/` | Heart rate zones, the hint to the next threshold, threshold beeps |
| `lib/day/` | Today's total and the walks on the day strip |
| `lib/archive/` | The history: one line per kept walk, read back and merged. No Arduino dependencies |
| `lib/stats/` | The last two weeks for the statistics page: days from the history, totals over them. No Arduino dependencies |
| `lib/screen/` | The dashboard (LVGL): layout, pulse chart, ring, day strip; Geologica fonts (`tools/make_fonts.sh`). No Arduino dependencies: the same code renders on a computer (`pio run -e sim`) |
| `src/ble_link.*` | Bluetooth: scanning, connecting, subscribing, reconnecting (NimBLE) |
| `src/treadmill_link.*` | Treadmill link on top of `ble_link` |
| `src/heart_rate_link.*` | Heart rate strap link on top of `ble_link` |
| `src/display.*` | The board's RGB panel and its link to LVGL |
| `src/touch.*` | The touch controller (GT911), polled over I2C, as LVGL's pointer |
| `src/storage.*` | The board's memory (LittleFS): walk logs, upload queue, day log, history |
| `src/backfill/` | Computer-side tool for walk logs downloaded from the cloud: history lines for the board, walk files rewritten as today's firmware writes them (`pio run -e backfill`) |
| `src/net.*` | Wi-Fi on for a session, then off |
| `src/uploader.*` | Uploads walks over HTTPS |
| `src/clock_sync.*` | Time from NTP |
| `src/beeper.*` | Beeps through the speaker (I2S) |
| `src/board.h` | Board pins, from Freenove's examples |
| `src/secrets.example.h` | Template for `src/secrets.h`: Wi-Fi, receiver address and key |
| `src/user_config.example.h` | Template for `src/user_config.h`: daily goal, zones, thresholds, time zone, step formula |
| `partitions.csv` | Memory layout: two 3 MB firmware slots, ~9.9 MB for walks |
| `cloud/` | The receiver: a Cloudflare Worker that stores files in R2 |
| `test/` | Tests for `lib/`, run on a computer: `pio test -e native` |

Pinned versions: ESP32 Arduino core 3.2.0 (the one Freenove tests its examples with), Arduino_GFX 1.6.5, NimBLE-Arduino 2, LVGL 9.5.

The first firmware build recompiles the Arduino core with the options in `custom_sdkconfig` (`platformio.ini`): code runs from PSRAM, so writing walk logs doesn't make the screen flicker. PlatformIO shares core packages between projects: building another project without these options brings back the stock core, and the next build here recompiles it again.
