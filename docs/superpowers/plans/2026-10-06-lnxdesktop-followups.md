# lnxDesktop — follow-ups left open after the first implementation

Non-blocking findings from the per-task and final reviews, kept so they are
not lost. None affects correctness of the core features.

## Worth doing soon
- Arrow-key navigation does not scroll an off-screen icon into view (desktop-canvas.c).
- A failed folder copy leaves the partial destination tree behind (actions.c tree copy).
- Stale preseeded position: a drop that fails to copy leaves its entry in positions.ini; a later file with that name inherits it.
- `ATTRIBUTE_CHANGED` monitor events are ignored, so `touch`/chmod leaves the Info date stale (desktop-model.c).
- Parking order: stored-but-not-fitting items are parked mixed with unpositioned ones instead of first (layout.c).

## Cosmetic / robustness
- Rubber band: Ctrl+band does not restore items swept out again; band colour is the foreground colour, not an accent; band edges are fractional (blurred 1 px border); 5–8 px press jitter neither drags nor narrows the selection.
- Thumbnails: temp file name is pid-only; chmod happens after the rename; cancellation only checked before decoding; all mapped icons request at once (no visibility ordering).
- Temp files from atomic saves (`name.XXXXXX`, `.part`) flicker as icons briefly.
- Position store: any load error (not only parse errors) triggers the `.bak` rename; write failures are only logged.
- Info window: a load error after the window was destroyed would show an unparented dialog; a popover that never maps is only unparented on dispose.
- Directory copies do not preserve directory permissions or timestamps.
- Tests: no layout test with a non-default pitch; no test for negative stored positions; model tests do not cover hidden↔visible renames; smoke test omits `lib_sources`.

## Needs a human pass (no input injection available during development)
See the Status section in README.md: drag out, drop in, niri width toggle,
screen move (2× ↔ 1×), rename/trash/copy/paste/Open With/Info, Menu key,
single-click-then-rename vs double-click-open on a selected label.
