# Low Poly Art Style Rendering Roadmap

## Goal

Transform the current Skullbonez Core renderer from a simple technical demo into a stylized low-poly world similar to:

* Firewatch
* The Witness
* Journey
* Sable
* Pixar concept art

The goal is **not realism**.

The goal is:

* Clean shapes
* Strong color design
* Pleasant lighting
* Readable silhouettes
* Minimal textures
* Artistic atmosphere

---

# Phase 1: Foundation Lighting

## 1. Gradient Sky Shader

### Objective

Replace the current flat sky with an artist-controlled gradient.

### Requirements

Generate sky entirely in shader.

Colors:

```cpp
ZenithColor  = float3(0.55, 0.75, 1.00);
MiddleColor  = float3(0.80, 0.90, 1.00);
HorizonColor = float3(1.00, 0.95, 0.80);
```

### Deliverables

* Vertical sky gradient
* Adjustable horizon height
* Adjustable color palette
* Optional sun glow region

---

## 2. Hemisphere Ambient Lighting

### Objective

Create believable ambient lighting without GI.

### Requirements

Use surface normal Y component.

```cpp
float t = normal.y * 0.5f + 0.5f;
ambient = lerp(GroundColor, SkyColor, t);
```

Example:

```cpp
SkyAmbient    = float3(0.45,0.65,1.0);
GroundAmbient = float3(0.35,0.25,0.15);
```

### Deliverables

* Sky-facing surfaces receive blue tint
* Downward surfaces receive warm bounce light
* Tunable ambient colors

---

## 3. Warm Directional Sun

### Objective

Create a stylized artistic sun.

### Requirements

Use warm sunlight.

```cpp
SunColor = float3(1.0, 0.92, 0.65);
```

### Deliverables

* Warm highlights
* Long shadow support
* Sun angle controls

---

# Phase 2: Atmospheric Depth

## 4. Distance Fog

### Objective

Add depth and scale.

### Requirements

Blend toward sky color by distance.

```cpp
color = lerp(surfaceColor, fogColor, fogFactor);
```

### Deliverables

* Exponential fog
* Tunable density
* Tunable fog color

---

## 5. Height Fog

### Objective

Create layered atmosphere.

### Requirements

Fog density increases toward lower elevations.

### Deliverables

* Ground haze
* Soft horizon blending

---

# Phase 3: Terrain Style

## 6. Terrain Height Gradient

### Objective

Break up large flat color areas.

### Requirements

Terrain color based on world height.

Example:

```cpp
LowColor  = float3(0.45,0.28,0.10);
MidColor  = float3(0.70,0.50,0.20);
HighColor = float3(0.90,0.80,0.45);
```

### Deliverables

* Automatic terrain coloration
* No texture dependency

---

## 7. Vertex Color Support

### Objective

Support handcrafted coloring.

### Requirements

Add vertex color pipeline.

### Deliverables

* Vertex color rendering
* Vertex color interpolation
* Editor visualization

---

# Phase 4: Shadows

## 8. Cascaded Shadow Maps

### Objective

Provide stable outdoor shadows.

### Requirements

Implement 3-4 cascades.

### Deliverables

* Terrain shadows
* Object shadows
* Soft PCF filtering

---

## 9. Contact Shadows

### Objective

Ground floating objects.

### Requirements

Screen-space contact shadow solution.

Alternative:

Simple projected blob shadows.

### Deliverables

* Strong object grounding
* Reduced floating appearance

---

# Phase 5: Stylized Shading

## 10. Flat Shading Mode

### Objective

Expose geometric structure.

### Requirements

Support face normals.

```cpp
PerTriangleNormal
```

instead of

```cpp
InterpolatedVertexNormal
```

### Deliverables

* Flat shaded rendering mode
* Toggle at material level

---

## 11. Toon Lighting

### Objective

Introduce stylized lighting bands.

### Requirements

Quantize NdotL.

Example:

```cpp
if (NdotL > 0.8)
    lighting = 1.0;
else if (NdotL > 0.4)
    lighting = 0.7;
else
    lighting = 0.3;
```

### Deliverables

* Configurable light bands
* Material toggle

---

# Phase 6: Water

## 12. Stylized Water Shader

### Objective

Replace current flat transparent water.

### Requirements

Features:

* Fresnel
* Depth tint
* Shore brightening
* Soft transparency

### Deliverables

* Adjustable water color
* Depth fade
* Simple reflection support

---

# Phase 7: Post Processing

## 13. Bloom

### Objective

Enhance highlights.

### Requirements

Very restrained implementation.

Only:

* Sun
* Bright reflections
* Emissive objects

### Deliverables

* Threshold control
* Intensity control

---

## 14. Color Grading

### Objective

Establish a visual identity.

### Requirements

LUT support.

Create presets:

* Warm Sunset
* Stylized Day
* Dreamy Pastel
* Nordic Winter

### Deliverables

* Runtime LUT switching

---

## 15. Vignette

### Objective

Subtle image framing.

### Deliverables

* Intensity control

---

# Phase 8: Environment

## 16. Cloud System

### Objective

Make the sky feel alive.

### Requirements

Large stylized cloud cards.

No realistic volumetrics.

### Deliverables

* Layered cloud planes
* Wind animation

---

## 17. Wind Animation

### Objective

Add movement to the world.

### Requirements

Vertex displacement.

```cpp
offset = sin(time + position);
```

### Deliverables

* Grass sway
* Tree sway
* Cloud drift

---

# Phase 9: Optional Features

## 18. Outline Rendering

### Objective

Stylized silhouettes.

### Requirements

Post-process edge detection.

Inputs:

* Depth
* Normals

### Deliverables

* Configurable outline thickness

---

## 19. Stylized SSAO

### Objective

Improve contact and depth.

### Requirements

Small-radius AO.

Avoid realistic dark crevices.

### Deliverables

* Tunable strength

---

## 20. Temporal AA

### Objective

Stabilize the image.

### Requirements

Support:

* TAA
* SMAA fallback

### Deliverables

* Reduced shimmer
* Stable distant geometry

---

# Implementation Order

1. Gradient Sky
2. Hemisphere Ambient
3. Warm Sun
4. Distance Fog
5. Terrain Gradient
6. Cascaded Shadows
7. Contact Shadows
8. Flat Shading
9. Toon Lighting
10. Water Shader
11. Bloom
12. Color Grading
13. Clouds
14. Wind Animation
15. Remaining optional systems

---

# Success Criteria

The scene should feel:

* Stylized
* Warm
* Readable
* Atmospheric
* Intentional

It should not feel:

* Photorealistic
* Physically accurate
* Generic Unreal clone
* Pure technology demo

Art direction takes priority over rendering complexity.
