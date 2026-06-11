# Web AMS Display — Design

**Date:** 2026-06-11
**Status:** Approved (design), pending implementation plan

## Goal

Show the Bambu AMS (filament slots) in the embedded web interface, mirroring what
the touch display already renders. The AMS data already exists end-to-end in
`PrinterStatus.ams` (`AmsInfo`); it is simply not serialized into the web status
JSON nor rendered in the web UI. This feature closes that gap.

## Scope

- Serialize `PrinterStatus.ams` into `/api/status` JSON.
- Render the AMS in a **dedicated "AMS" tab** in the web UI, with **full** detail
  (color swatch, filament type, remaining %, per-unit humidity, active-slot
  highlight) — feature-parity with the touch UI.

Out of scope: editing/assigning filament from the web UI, Spoolman integration,
any new polling endpoint.

## Data model (already present)

From `src/printer.h`:

- `AmsInfo`: `present`, `units` (1..4), `activeUnit` (-1 = none), `activeSlot`
  (-1 = none), `unit[4]`.
- `AmsUnit`: `present`, `count`, `humidity` (1..5 dryness, -1 = unknown),
  `slot[4]`.
- `AmsSlot`: `present`, `type` ("" = empty), `color` (0xRRGGBB), `remain`
  (%, -1 = unknown).

## Backend — `buildStatusJson()` (`src/web_portal.cpp`)

Append an `ams` object to the existing status document. Only emit it meaningfully
when `g_status.ams.present` is true; otherwise emit `{"present": false}`.

Shape:

```json
"ams": {
  "present": true,
  "units": 2,
  "activeUnit": 0,
  "activeSlot": 1,
  "unit": [
    {
      "present": true,
      "count": 4,
      "humidity": 2,
      "slots": [
        { "used": true,  "type": "PLA", "color": "#1ABC9C", "remain": 85 },
        { "used": false }
      ]
    }
  ]
}
```

Serialization rules (mirror touch-UI semantics):

- `color`: formatted as a CSS hex string `#RRGGBB` from the `0xRRGGBB` integer,
  so the browser can use it directly as `background`.
- `remain`: emit only when `>= 0` (omit when unknown).
- `humidity`: emit only when `>= 0` (omit when unknown).
- `used`: maps from `AmsSlot.present`. When `false`, no `type`/`color`/`remain`
  fields are emitted for that slot.
- Only iterate `unit[i]` where `unit[i].present`; emit each present unit's four
  slots (slot objects always present so the grid is stable).

The 4×4 maximum payload is tiny — no streaming/filtering needed, keep it inline
with the rest of `buildStatusJson()`.

## Frontend — new "AMS" tab (`src/web_index.h`)

### Tab structure

- Add a fourth tab button **"AMS"** alongside Status / Settings / Log.
- Extend the existing `tab()` switcher array (`['status','settings','log','ams']`)
  and the per-button `.on` toggling accordingly.
- The AMS tab is **always visible** (no dynamic show/hide of the button) to avoid
  layout shifts and extra JS. When no AMS is present it shows a placeholder.

### Rendering

A `renderAms(d.ams)` function, called from the existing `loadStatus()` loop
(every 2 s) — no new endpoint, no new polling.

- If `!ams || !ams.present`: show a single placeholder card "No AMS connected."
- Otherwise, for each present `unit`:
  - A **card** with a header row: title **"AMS N"** (1-based) on the left and
    **"Humidity h/5"** on the right (hidden when `humidity` is absent).
  - A **4-slot grid** of tiles. Each tile:
    - `background` = slot `color` (empty/greyed when `used` is false).
    - Filament **type** ("empty" when unused) + **remaining %** ("" when absent).
    - Text color black or white chosen by perceived luminance of the swatch color
      (threshold 140, same formula as the touch UI), for contrast.
    - **Active slot** (matching `activeUnit` + `activeSlot`) gets a cyan border
      (3px) vs the normal 2px swatch-colored border.

### Styling

Reuse existing CSS variables (`--card`, `--bd`, `--ac`, etc.). Add minimal CSS for
the slot grid (4 columns) and tile. Keep visual language consistent with the
current cards.

## Data flow & update

- No change to the polling model: `loadStatus()` already fetches `/api/status`
  every 2 s and `setInterval(loadStatus, 2000)` keeps it live. `renderAms()` runs
  inside `loadStatus()` so the AMS tab updates in lockstep with the Status tab.

## Validation

- `pio run` must still pass (baseline ~44% RAM / ~44% app partition).
- Manual: with a Bambu printer + AMS, the AMS tab shows each unit, correct
  colors, types, remaining %, humidity, and highlights the active slot; with
  Klipper / no AMS, the tab shows the placeholder.

## Non-goals / future

- Writing filament data back, Spoolman sync, AMS controls — not in this change.
