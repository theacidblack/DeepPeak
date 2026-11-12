# DeepPeak

A tiny Windows tray widget that tells you when DeepSeek V4.1 Flash is billed at
**peak** vs **off-peak** rates, and how long you have left in the current tier.

Built for people running DeepSeek V4.1 Flash through **OpenCode Go** and
**CommandCode GOAT**, both of which pass through DeepSeek's time-of-day pricing.

## Why it matters

Peak tokens cost exactly **2x** off-peak tokens. Both subscriptions meter you in
dollars (monthly pool plus rolling 5-hour and weekly windows), so a peak session
burns the pool twice as fast -- roughly halving the usable month.

| | Off-peak | Peak |
|---|---|---|
| Input / output per 1M | $0.15 / $0.60 | $0.30 / $1.20 |
| Effective usage | 100% | ~50% |

**Peak: 01:00-04:00 and 06:00-10:00 UTC, Monday-Friday.** Everything else,
including all weekend, is off-peak (17h/day, and 48h on weekends).

For Kyiv (UTC+2 winter, UTC+3 summer) that is 03:00-06:00 and 08:00-12:00 in
winter, shifting to 04:00-07:00 and 09:00-13:00 in summer. The widget does all
peak math in UTC and only converts for display, so DST is handled by Windows
automatically -- no hardcoded offset.

## What it shows

- Tray icon: green when off-peak, red when peak, tooltip with the countdown.
- Panel (left-click the tray icon):
  - current tier and cost multiplier (1x / 2x)
  - time left in the current tier ("Cheap for 10h 34m" / "Expensive for 2h 12m")
  - what happens next and at which local time
  - a 24-hour timeline strip so you can see the shape of the day at a glance
  - the peak schedule rendered in your local time

## Features

- Native Win32, no runtime, no framework, no network access.
- ~180 KB exe, ~2.5 MB private memory, ~11 MB working set.
- Single instance; lives in the tray with a hidden window.
- Per-monitor DPI aware (v2), hand-built anti-aliased icons, rounded dark panel.
- Tray menu: show/hide panel, copy status, start with Windows, exit.
- `--show` flag opens the panel immediately.

## Build

Requires Visual Studio Build Tools with the C++ workload.

```powershell
.\build.ps1
```

Output: `build\DeepPeak.exe`.

## Run

```powershell
.\build\DeepPeak.exe
```

Optionally add it to startup from the tray menu ("Start with Windows"), which
writes a single `HKCU\...\CurrentVersion\Run` value.

## Notes

- Peak detection is pure local clock math against the UTC schedule; the widget
  never contacts a provider, so it cannot leak keys or break if an endpoint moves.
- The schedule is the DeepSeek API's own peak/off-peak window, which both
  subscriptions resell. If a provider changes its schedule, update
  `kPeak1Start`/`kPeak1End`/`kPeak2Start`/`kPeak2End` in `src/main.cpp`.
- Weekend handling is part of the same UTC rule (Saturday and Sunday are fully
  off-peak), so no extra configuration is needed.

## Layout

```
src/main.cpp   entire application
build.ps1      MSVC build script
```
