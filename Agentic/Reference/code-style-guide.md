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

## Width

- Treat 125 characters as a soft limit. A slightly longer indivisible token or
  a clearer compact expression is preferable to an artificial wrap, but avoid
  substantially exceeding it.
