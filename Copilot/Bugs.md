# SkullbonezCore — Known Bugs

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
