# Plan: Water Reflections

## Problem
The water surface currently renders as a flat deep-ocean blue. We want the balls (and sky) to be
reflected in it, with the reflection visually rippling in sync with the animated surface waves.

## Compatibility With Vertex-Animated Ripples

The reflection must look like it is being distorted by the same waves that displace the geometry.
The approach chosen guarantees this:

- The reflection pre-pass renders a planar mirror image into an FBO texture (flat mirror assumption).
- In `water.frag`, the FBO texture is sampled at a UV computed from clip-space position
  (projective texturing).
- **That UV is then perturbed** by evaluating the same sine-wave functions as `water.vert`
  — same frequencies, same speeds, same `uTime` — at the fragment's world-space XZ position
  (passed as `vWorldXZ` varying from the vertex shader).
- Result: geometric ripples and reflected-image ripples are phase-locked. The reflection
  shimmers in exact sync with the surface motion.

This technique (planar reflection + wave-phase UV distortion) is the standard game approach
and requires no changes to the wave parameters in `water.vert`.

## Architecture

### New class: `SkullbonezFramebuffer`
A lightweight FBO wrapper: color texture (`GL_RGB`, 800×600) + depth renderbuffer.
Follows the same Skullbonez conventions as `Mesh` and `Shader` (GL resource cleanup in
destructor, `ResetGLResources()` to rebuild after context recreation).

```
Framebuffer(int width, int height)   — create FBO + color tex + depth RBO
~Framebuffer()                       — delete FBO resources
Bind() / Unbind()                    — bind/unbind the FBO
GLuint GetColorTexture() const       — returns the color texture handle
void ResetGLResources()              — delete and recreate all GL objects
```

### Reflection pre-pass (added to `SkullbonezRun::Render()`)
Before the main render, each frame:
1. Compute reflected camera — mirror eye and center about Y = `FLUID_HEIGHT`, flip up to (0,-1,0):
   ```
   eye'    = (eye.x,    2*FLUID_HEIGHT - eye.y,    eye.z)
   center' = (center.x, 2*FLUID_HEIGHT - center.y, center.z)
   up'     = (0, -1, 0)
   reflView = Matrix4::LookAt(eye', center', up')
   ```
   Eye and center come from `GetCameraTranslation()` and `GetCameraView()` — both already public.
   Up is always (0,1,0) across all cameras so no new accessor is needed.

2. Bind `cReflectionFBO`, clear, set viewport to 800×600.
3. Render **skybox** and **game models** using `reflView` as the view matrix.
   - Terrain is skipped (mostly below water, would look wrong upside-down).
   - Shadows skipped (not visible as reflection).
   - **Clip plane required** — balls partially submerge. Without clipping, the underwater
     half of a partially-submerged ball would appear in the reflection.
   - Clip plane equation `(0, 1, 0, -FLUID_HEIGHT)`: keep fragments where `worldPos.y >= FLUID_HEIGHT`.
   - Enabled via `glEnable(GL_CLIP_DISTANCE0)` + `gl_ClipDistance[0]` in the vertex shader.
   - Designed to be reusable for a future underwater pass (negate the plane to clip above water instead).
4. Unbind FBO, disable `GL_CLIP_DISTANCE0`, restore main viewport.

### Water shader changes
`water.vert` gains two new outputs (no change to existing wave logic):
```glsl
out vec4 vClipPos;   // clip-space position for projective UV
out vec2 vWorldXZ;   // undisplaced world XZ for wave-phase perturbation
```

`water.frag` gains reflection sampling:
```glsl
uniform sampler2D uReflectionTex;
uniform float     uReflectionStrength;  // 0.0–1.0, default 0.35
uniform float     uTime;                // same uTime as vertex shader

// Projective UV (maps reflection texture onto water surface in screen space)
vec2 reflUV = (vClipPos.xy / vClipPos.w) * 0.5 + 0.5;

// Perturb UV using same wave functions as water.vert — phase-locks distortion to geometry
float waveX = sin(vWorldXZ.x * 0.04 + uTime * 1.2) * 1.5
            + sin(vWorldXZ.y * 0.06 + uTime * 0.8) * 1.0;
reflUV += vec2(waveX * 0.002, waveX * 0.002);

vec4 reflection = texture(uReflectionTex, reflUV);
FragColor = mix(uColorTint, reflection, uReflectionStrength);
```

### `WorldEnvironment::RenderFluid` signature update
```cpp
void RenderFluid(const Matrix4& view, const Matrix4& proj, float time, GLuint reflectionTex);
```
Binds `reflectionTex` to texture unit 1 before drawing. Sets `uReflectionTex = 1`.

## Tasks

| ID | File | Change |
|----|------|--------|
| `wr1` | `SkullbonezFramebuffer.h` / `.cpp` | New FBO class: color tex + depth RBO, `Bind/Unbind`, `GetColorTexture`, `ResetGLResources` |
| `wr2` | `SkullbonezRun.h` | Add `std::unique_ptr<Framebuffer> cReflectionFBO` member |
| `wr3` | `SkullbonezRun.cpp Initialise()` | Create and init `cReflectionFBO(SCREEN_X, SCREEN_Y)`; call `ResetGLResources` |
| `wr4` | `lit_textured.vert` | Add `uClipPlane` (vec4 uniform) + `gl_ClipDistance[0] = dot(vec4(worldPos,1), uClipPlane)`. Default uniform = `(0,1,0,1e9)` (always-pass) so main render is unaffected |
| `wr5` | `SkullbonezRun.cpp Render()` | Add reflection pre-pass: reflected `Matrix4::LookAt`, bind FBO, `glEnable(GL_CLIP_DISTANCE0)`, set `uClipPlane=(0,1,0,-FLUID_HEIGHT)` on sphere shader, render skybox+models, `glDisable(GL_CLIP_DISTANCE0)`, unbind FBO |
| `wr6` | `water.vert` | Add `vClipPos` and `vWorldXZ` varyings; no change to wave displacement logic |
| `wr7` | `water.frag` | Add `uReflectionTex`, `uReflectionStrength`, `uTime`; projective UV + wave-phase perturbation + mix |
| `wr8` | `SkullbonezWorldEnvironment.h` / `.cpp` | `RenderFluid` accepts `GLuint reflectionTex`; binds to unit 1; sets `uReflectionTex` uniform |
| `wr9` | `SkullbonezRun.cpp` (render call) | Pass `cReflectionFBO->GetColorTexture()` to `RenderFluid` |

## Difficulty

**Moderate.** The FBO class is new but self-contained and straightforward. The most
failure-prone step is the projective UV in `water.frag` — if `vClipPos` perspective division
or the [0,1] remap is off, the reflection will be stretched or mirrored incorrectly.
The reflected camera math is simple given that up is always (0,1,0).

The clip plane (`gl_ClipDistance[0]`) must be added to `lit_textured.vert`. The default
uniform value `(0,1,0,1e9)` ensures the main render pass is unaffected. This same
infrastructure will be reused for the future underwater ball rendering pass (flip the plane).

The wave-phase UV perturbation is copied directly from `water.vert` so it cannot drift
out of sync with the geometry.

## Pipeline
Build (0 errors, 0 warnings) → render test → archive baselines → update baselines
→ perf test → smoke test → commit.
