# C++ Code Style

This guide records the owner-directed layout rules for SkullbonezCore. The
formatter enforces mechanical whitespace and wrapping; review owns semantic
placement and parameter-order decisions.

## Function Preconditions

- Put assertions and precondition checks at the top of the function whenever
  control flow and required setup allow it.
- Leave one blank line after the assertion/precondition block.
- If setup must precede a check, keep the dependency obvious rather than moving
  the check above values it needs.

## Conditions, Loops, And Comments

- Leave one blank line above and below an `if`, `switch`, `for`, `while`, or
  `do` block.
- Keep `else`, `else if`, and a `do` block's trailing `while` attached to their
  control-flow chain.
- Leave one blank line above every standalone comment group.
- A blank line below a comment is optional; use it when the comment introduces
  a distinct paragraph rather than the immediately following statement.

## Parameters And Calls

- Keep one to three short parameters or arguments on one line when the complete
  line fits within the 125-character soft limit.
- Split signatures and calls when they have at least four parameters or when
  keeping them together would press past the soft limit.
- The first parameter or argument stays on the same line as the opening
  parenthesis. Never leave a function/call line ending with an empty `(`.
- Align every continuation parameter beneath the first parameter.
- Prefer pointer, reference, and other complex-type parameters before primitive
  value parameters. Ownership, ABI compatibility, or a clearer call-flow
  grouping may justify an exception; make that reason evident in the API.

### Never Introduce A Type To Shorten A Signature

A long parameter list is a readable problem. A struct that exists only to hide it
is a hidden one, and `AGENTS.md` bans it under the Invariant Ownership Rule. Two
cases are settled and need no debate:

- **One borrowed member without behavior is never worth a type.**
  `Foo( Bar& bar )` beats `struct FooContext { Bar& bar; };` plus
  `Foo( FooContext )` on every axis: fewer names, fewer lines, no by-value copy
  of a reference wrapper, and no reader wondering what the context owns. A
  one-field behavior owner or tested strong scalar type is a different shape.
- **If the callee destructures it immediately, it is a courier.** When the first
  lines of the body copy members into locals, the type carried nothing. Widen the
  signature instead. Reaching the 12-parameter qualitative review trigger means
  the *operation* needs an explicit owner ruling or decomposition — never a bag
  created to hide the number.

Introduce an aggregate when it owns a rule its absence would let a caller break:
a phase order, a lifetime, an arbitration policy. Say which in an `Invariant:`
block and exercise it in a test. `tools/inventory_authority_free_aggregates.py`
reports the mechanically decidable part and `validate_fast` fails on an unruled
row.

Do not name a local after a member either. An `m_`-prefixed local claims owner
state it does not have — see the Extraction Scar Rule in `AGENTS.md`.

`Agentic/Skills/collapse_params.py` is a **line-layout formatter only**: it joins
a multi-line parameter list onto one line to match the width rules above. Its name
invites the opposite reading. It is never authority to collapse parameters into a
type.

## Width

- Treat 125 characters as a soft limit. A slightly longer indivisible token or
  a clearer compact expression is preferable to an artificial wrap, but avoid
  substantially exceeding it.

## Test File Ownership

- Name a test file for the subsystem whose behavior it pins, never for a gate,
  metric, campaign, or plan.
- Raise coverage by adding behavioral cases to the owning subsystem's test
  file. A coverage result is validation evidence, not a test-file owner.
- Put value-only fixtures shared by multiple subsystem test files in clearly
  named shared test support; do not create a gate-named collection point.

## Profiling Markers

- `PROFILE_SCOPED`, `PROFILE_BEGIN`, and `PROFILE_END` take only the marker
  name. They resolve the active process profiler internally and may be used from
  any source location that includes `Core/Profiler.h`.
- Never add a profiler parameter, context field, callback, or retained member
  solely to emit a CPU marker.
- Worker timing remains explicit through `PROFILE_WORKER_SCOPED` because it
  records worker identity and accumulation rather than an ordinary CPU scope.
