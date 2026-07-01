# Accurate Collision-Shape Picker Plan

Status: In progress
Created: 2026-07-01
Owner: Future implementation agent

## Summary

Replace broad bounding-sphere model picking with exact CPU ray tests against each
model's collision shape. The goal is not GPU pixel-perfect picking; the selected
target should be accurate to the authored collision geometry so narrow objects
like tree trunks are not hidden behind huge foliage or branch bounding spheres.

Apply the change to all centralized model-pick purposes:

- Editor selection
- Attach-camera target picking
- Replay path target picking
- Manipulator pickup

Tree parts remain individual selectable models. Shift-select remains the
multi-part workflow for trees or any other object collection.

## Findings From Investigation

- `RuntimePickService::TryPickModel(...)` is the central picker used by editor
  selection, attach-camera picking, replay path target picking, and manipulator
  pickup.
- Selection-style picks currently test a padded bounding sphere derived from
  `GetShapeBoundingRadius(...)`, then choose by projected center distance.
- Manipulator pickup also uses a padded sphere and skips fixed bodies.
- Tree trunks and foliage tiers are separate `GameModel` instances. The trunk
  hull is narrow, while foliage tiers have much larger bounding radii, which is
  why lower branches or foliage win when clicking near the trunk.
- A GPU ID-buffer picker would be true rendered-pixel accuracy, but that is out
  of scope. Collision-shape accuracy is the selected alternative.

## Implementation Plan

1. Keep the public picker interface source-compatible:
   - `RuntimePickService::TryPickModel(...)`
   - `RuntimePickRequest`
   - `RuntimePickResult`

2. Add an internal runtime picking geometry helper, preferably near
   `SkullbonezSource/Runtime/RuntimePickService.cpp` unless the implementation
   needs a separate file for CPU-only tests.

3. Implement exact ray intersections for each `CollisionShape` variant:
   - Sphere: use the real sphere radius and local offset.
   - Box: transform the ray into box-local space with the model orientation and
     run a slab test against `BoundingBox::GetHalfExtents()`.
   - Convex hull: transform the ray into hull-local space, then clip against
     every `ConvexHullFace` plane using `normalLocal` and `planeOffsetLocal`.

4. Update `RuntimePickService::TryPickModel(...)` to use one closest-hit loop:
   - Preserve the existing null-models guard.
   - Preserve purpose-specific filtering, especially fixed-body skipping for
     `RuntimePickPurpose::ManipulatorPickup`.
   - Call the exact shape helper for each candidate.
   - Pick the smallest non-negative `rayT`.
   - Preserve deterministic ties by keeping the earlier model when hit distances
     are effectively equal.

5. Treat `RuntimePickRequest::modelRadiusPadding` as legacy compatibility only.
   It should not expand exact selectable geometry after this change.

6. Do not add tree grouping or whole-tree selection behavior in this slice.

## Test Plan

Add CPU-only regression coverage through
`tools\validate_runtime_interaction_policy.bat`.

Required cases:

- A ray inside the old bounding sphere but outside a narrow box misses.
- A ray through `tree_trunk_faceted.hull` hits the trunk hull.
- A ray inside the trunk's old bounding radius but outside its convex faces
  misses.
- Rotated boxes and convex hulls return stable nearest `rayT`.
- Manipulator pickup still skips fixed bodies but no longer grabs dynamic bodies
  through their old broad sphere envelope.

If direct `RuntimePickService` tests pull too much runtime linkage into the
existing test target, keep the exact geometry helper testable with a narrow
CPU-only surface and add only one service-level policy smoke test.

## Validation

This plan file is documentation-only and requires no repository validation.

When implementing source changes, use the following gates before commit or PR:

```bat
tools\validate_runtime_interaction_policy.bat
tools\validate_fast.bat
```

If implementation edits physics collision-shape files such as `BoundingBox*`,
`ConvexHullShape*`, or `CollisionShape*`, also run:

```bat
tools\validate_physics.bat
```

Manual acceptance:

- In the editor, clicking a tree trunk selects the trunk part.
- Clicking foliage selects foliage.
- Manipulator pickup starts only when the ray actually hits the model shape.

## Comment And Handoff Requirements

- Follow the source comment standard for any touched `.cpp`, `.h`, `.hpp`,
  `.inl`, or tool script files.
- After touching source-bearing files, inspect every touched source file with
  `Agentic/Skills/comment-style-audit/skill.md` before reporting done.
- Protect user-owned dirty files. Run `git status --short --branch` before
  editing and again before any commit.
- Implementation work from this plan should use
  `Agentic/Skills/orchestrator/SKILL.md` unless the user explicitly asks to
  bypass it.

## Assumptions

- Collision-geometry accuracy is sufficient; rendered-pixel perfection is out of
  scope.
- Tree parts should remain exact selectable elements rather than selecting a
  whole tree group.
- The current branch may differ from `Agentic/SessionState.md`; trust the live
  `git status` output before implementation.
