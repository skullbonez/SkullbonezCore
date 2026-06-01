# SkullbonezCore — Known Bugs

## TECH DEBT

## TODO: Rewrite CLI argument parsing in SkullbonezInit.cpp

The entire `ParseCommandLine` family of functions uses raw `const char*` pointer arithmetic — manual `strstr`, `+=` by magic literal length offsets, manual whitespace skipping, `atof`/`_strnicmp`. This is fragile (off-by-one on the offset constant silently reads garbage), hard to test, and not the quality standard we hold the rest of the codebase to. It should be rewritten using proper string types (`std::string_view`, `std::string`) and a proper tokeniser that splits `lpCmdLine` into an `argv`-style vector first, then dispatches by token — the way every modern CLI parser works. The quote-stripping fix for `--scene`/`--suite` paths is a symptom of the same problem and would become unnecessary with correct tokenisation.

## TODO: CPU/GPU spike histogram overlay

The existing profiler overlay shows rolling averages and bar graphs of frame time, but gives no visibility into outlier frames — a single 50ms spike is invisible when averaged into a 60fps rolling window. Add a spike histogram: a fixed-width ring buffer of the last N frame times (CPU and GPU separately), rendered as a scrolling bar graph in the HUD overlay. Each bar is one frame; bars exceeding the frame budget (e.g. 16.6ms at 60Hz) are drawn in a distinct colour. This would make hitching, GC-style stalls, and DX12 pipeline bubbles immediately visible without needing to export a perf CSV and analyse it offline.

## TODO Command line args

I never want to type this again: --scene SkullbonezData/scenes/stacking.scene
Should simply be --scene stacking or --suite myTests

## TODO: Clock speeds

Why is DX tests running slow and GL fast?

## TODO: Physics text file

20K lines for our physics regression test...  This is overkill.  We should just keep a line at a 60hz rate.

## TODO: Scene reset

Allow scene reset on press of R key

## PHYSICS BUGS

## TODO: Fix stacking

Boxes stacked on top of each other (see `stacking.scene`) tend to drift or
topple slowly over several hundred frames rather than reaching a rock-solid
rest. The solver converges for individual resting contacts but multi-body
stacks expose gaps in the constraint ordering and lack of warm-starting across
frames. Likely needs persistent contact caching (warm-start accumulated
impulses from the previous frame) and/or position-stabilisation correction
applied to the full stack chain, not just individual contact pairs.

## TODO: Fix balls contact resting state

Balls resting on terrain (see `at_rest.scene`) exhibit a visible micro-bounce
or jitter before the sleep threshold kicks in. The ImpulseSolver's restitution
path applies a small bounce impulse even at near-zero normal velocity, keeping
the ball alive longer than it should be. The fix is to gate restitution behind
a minimum separation velocity (typically 1–2 × the Baumgarte bias speed) so
that low-energy contacts go straight to the resting / positional-correction
path rather than bouncing. The same issue affects box-on-terrain resting.

## TODO: Box-ball interpenetration

Interpenetration can be seen during ball settle time between box and ball around frame 570 of at_rest.scene
