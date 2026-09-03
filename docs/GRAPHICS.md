# Graphics

## Status

**NOT IMPLEMENTED.** The bootstrap creates only a native Win32 window.

## Direction

The project is Windows-only. The preferred renderer is Direct3D 12, with Direct3D 11 acceptable if it materially reduces risk for the first functional visual milestone.

The graphics layer will be separated into focused areas such as:

```text
Renderer/
Graphics/
Shaders/
Textures/
Materials/
PostProcessing/
```

The first objective is functional correctness and observable game output, not immediate pixel-perfect reproduction of every PS2 GS effect.

The D3D11/D3D12 choice remains open until the original rendering behavior and translation requirements are better understood.
