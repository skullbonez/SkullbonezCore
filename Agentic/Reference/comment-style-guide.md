# SkullbonezCore Comment Style Guide

Comments explain facts that are difficult to recover from the code itself. They
should make ownership, ordering, lifetime, units, determinism, threading, and
failure risks clear without burying the implementation under a documentation
template.

## Core rule

Write for a capable C++ reader who does not already know this engine. Explain:

- why an indirect or conservative operation is required;
- which owner retains a resource or mutable state;
- which ordering, lifetime, unit, or capacity rule must remain true;
- which crash, race, divergence, or flaky result the code prevents; and
- where an external algorithm or API rule affects the implementation.

Delete comments that only restate syntax, repeat a filename, narrate history, or
promise vague future cleanup.

```cpp
// Skip slot 0 because shaders treat descriptor index 0 as "no texture".
++descriptorIndex;
```

## File comments

A file preamble is optional. If a reader needs orientation before the first
declaration, add a brief `Purpose:` comment and only the non-obvious ownership
or invariant facts needed for the file. There is no required `File`, `Summary`,
`Glossary`, or `Related` template.

Do not move useful local explanations into a large preamble. Keep lifetime,
hazard, unit, concurrency, and ordering comments beside the declaration or
operation they constrain. Shared engine terms belong in
`Agentic/Reference/engine-glossary.md`; explain a file-specific term at its
first dense use.

## Local comment labels

Use these labels only when they make the comment easier to find or understand:

| Label | Use |
|---|---|
| `Concept:` | Explain a dense mechanism in plain language. |
| `Why:` | Explain a surprising or conservative choice. |
| `Invariant:` | State a rule the owner or caller must preserve. |
| `Lifetime:` | State how long a reference or resource remains valid. |
| `Hazard:` | Name the failure, race, drift, or nondeterminism prevented here. |
| `Units:` | State the measurement unit or coordinate convention. |
| `Runtime allocation policy:` | State an allowed phase, hard cap, and storage owner. |

Precise domain labels such as `Caller contract:`, `Precondition:`, `Phase:`,
`Compatibility:`, and `Cold boundary:` are fine when the noun is useful. Do not
invent a new label that merely means concept, reason, rule, lifetime, or risk.

## Ownership and lifetime

State who owns mutable state and how long borrowed data remains valid. Public
API comments should describe the caller-visible contract, not implementation
steps.

```cpp
// Lifetime: the returned span is valid until the next scene topology change.
std::span<const BodyHandle> ActiveBodies() const;
```

```cpp
// Invariant: replay publication copies this value before the frame arena resets.
Publish(snapshot);
```

## Hazards, errors, and validation

Name concrete failure behavior. Use the repository's error categories where
they apply:

- `Fatal Invariant` for corrupted internal state that terminates through
  `SB_FATAL`;
- `Recoverable Error` for bad input or environment failures represented by an
  `SbResult`; and
- `Test Probe` for bounded diagnostic results that are not production errors.

Physics comments that describe behavior compared byte-for-byte must say whether
the change affects the deterministic baseline. Rendering comments should name
resource states, descriptor lifetime, synchronization, or device-loss hazards
when those facts are not obvious from the API calls.

## Algorithm and API sources

Physics uses these labels when distinguishing a published algorithm from a
Skullbonez-specific choice:

```cpp
// CATTO REF:
// Erin Catto, source/equation and the exact part used here.
//
// ENGINE-SPECIFIC:
// The local geometry, ordering, or numerical policy.
```

Link official API documentation beside a non-obvious API-dependent decision,
not as a generic file navigation list.

## What to avoid

Avoid comments like:

```cpp
// Increment i.
// Create DSV.
// Fix weird issue.
// TODO: make this better.
```

Prefer the exact reason:

```cpp
// Reuse the descriptor row so resize cannot invalidate material indices.
// Hazard: a disappearing terrain support point must wake the island next tick.
```

Do not add decorative banners, repeated local definitions of shared engine
terms, or long identity blocks. Keep historical detail only when it explains a
current compatibility constraint. Actionable TODOs name the owning area and the
condition that permits deletion.

## Review checklist

When editing comments, check that:

- every statement still matches the post-change owner and control flow;
- non-obvious lifetime, threading, units, ordering, and allocation facts remain
  visible near the affected code;
- public APIs describe caller-visible preconditions and invalidation points;
- validation-sensitive Physics and Rendering assumptions are explicit;
- shared terminology is not redefined differently in multiple files; and
- direct systems-programming language is used throughout.

Comment-only edits require no repository validation. Any behavior change uses
the validation map in `AGENTS.md`.
