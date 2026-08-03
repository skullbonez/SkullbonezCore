# Source Modernization MZ3 — Convention Rulings

Date: 2026-08-03
Branch: `nightrunner-3rd-AUG-26`
Plan phase: MZ3 of `Agentic/Plans/TODO/source-modernization-sweep.md`
Impact: documentation only

## Decisions

### Retain `Normalise`, `TryNormalise`, And `TryNormalised`

The British spelling is retained. It is a coherent API family rather than an
isolated typo: `Vector3` owns all three forms, `Quaternion` uses the same verb,
and the family is referenced across 29 production/test files. CodeGraph finds
direct production and test callers for each public `Vector3` form. Renaming the
family would change a widely included public spelling and numerous comments,
tests, and call sites without changing meaning, behavior, or ownership.

The engine may use American spelling in prose or in unrelated third-party-style
helpers; that does not make this established first-party API defective. This is
a retain ruling, not a new requirement that future APIs use British spelling.

### Retain The `Maths/` Directory And `Math::` Namespace

The physical directory and namespace are retained because they name different
axes. `Maths/` is the 15-file build/module location and matches the project and
include-path vocabulary; `Math::` is the singular conceptual domain containing
Vector, Orientation, and Transformation subdomains. The split has remained
readable and dependency direction is already visible from the physical path.

Reconciling the names would require either broad include/project-path churn or a
broad namespace/API rename across 66 current `Vector3.h` source/test reference
files, without repairing behavior or dependency ownership. No forwarding path,
namespace alias, or compatibility spelling is introduced.

## Validation Impact

MZ3 changes documentation only. No repository validation is required for this
phase. Because both conventions are retained, MZ3 adds no validation scope or
baseline risk to the already binding MZ4 Physics, deep Physics, performance,
full, audit, and independent-review gates.
