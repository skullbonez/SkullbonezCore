# SkullbonezCore — Known Bugs

| # | Bug | Area | Status |
|---|-----|------|--------|
| 1 | Water renders through to the back faces of spheres when they intersect the water surface | Rendering / Water | 🐛 Open |

## Details

### Bug 1: Water draws through sphere back faces
When a sphere partially intersects the water surface, the water plane is visible through the back faces of the sphere — i.e. the water is not correctly occluded by the sphere geometry. Likely a depth test / draw-order issue between the water mesh and the sphere meshes (water may be drawn after spheres without depth write, or sphere back-face culling interacts incorrectly with the semi-transparent water pass).
