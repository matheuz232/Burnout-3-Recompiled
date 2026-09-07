# Win32 bootstrap/window validation v0

Date: 2026-09-07
Branch: `feature/win32-window-validation-v0`
Base: `3fefdf1a60ef0f77c82b0ad9e6688a82c2203905`

## Scope

This bounded milestone strengthens the existing Win32 window lifecycle without adding a graphics API or game-specific behavior.

Validated behavior:

- windowed creation produces a live `HWND`;
- requested 640x360 and 320x180 client areas are preserved;
- windowed mode carries `WS_OVERLAPPEDWINDOW`;
- fullscreen mode carries `WS_POPUP`, excludes `WS_OVERLAPPEDWINDOW`, and uses the current screen dimensions;
- `WM_CLOSE` destroys the window and terminates the message pump through `WM_QUIT`;
- Escape follows the same shutdown contract;
- the same `Win32Window` object can create a second window after shutdown;
- `handle()` becomes `nullptr` after native destruction rather than exposing a stale HWND.

The production fix passes `this` through `CreateWindowExW`, stores the object pointer in `GWLP_USERDATA` during `WM_NCCREATE`, and clears `hwnd_` during `WM_NCDESTROY`.

## TDD evidence

### RED

Commit: `30b636ef9f4aae38600812da789852c002cba95b`
Windows CI: #793 (`34096442708`)

Result:

- Configure: PASS
- Build: PASS
- CTest: 66/67 PASS
- sole failure: `win32_window_smoke_tests`
- exact failure: `windowed WM_CLOSE: handle() did not clear after shutdown`

### GREEN

Commit: `a5a38d04746770520f68b87be691c23ffe1f2ad9`
Windows CI: #794 (`34096651423`)

Result:

- Configure: PASS
- Build: PASS
- Test: PASS
- CTest: 67/67 PASS
- Frame pacing telemetry: PASS
- 120 Hz pacing probe smoke: PASS
- analyzer package validation: PASS
- pacing package validation: PASS

## Status

`CI_VALIDATED`

A physical Windows desktop visual check remains useful as release certification, but is no longer required to establish correctness of the Win32 bootstrap/window lifecycle contract in CI.
