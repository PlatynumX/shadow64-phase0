# Shadow64 software renderer plan

R11 deliberately does not use libdragon preview OpenGL.

The renderer path is:

```text
Build/JFBuild-style data
  -> N64 asset bank
  -> CPU projection
  -> vertical software wall columns
  -> libdragon 16-bit framebuffer
```

Short-term renderer milestones:

```text
R11: software-only first-person wall renderer baseline
R12: add proper visible-sector traversal / portal recursion
R13: add floor and ceiling spans
R14: add sprite billboard columns
R15: add collision and sector-height camera behavior
```

Rules for this branch:

```text
- stay on standard libdragon trunk unless there is a non-renderer reason to change
- do not depend on OpenGL
- do not depend on shaders or programmable pipeline features
- keep top-down map view as a fallback debug mode
- keep asset bank small and map-specific until the renderer is stable
```
