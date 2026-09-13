# DX12 screenshot candidate review

These are the three fresh Canvas captures from native parallel isolation run
20260910T141607Z. The concurrent Automation smoke used its own Editor preferences;
DX12 retained Canvas and the inherited parent preferences remained unchanged.
All three comparisons were visually inspected. DX12 reports zero validation errors.

The old references predate the committed panel-animation, theme and dock changes.
Current captures omit the header and Details tab during the initial animation
and show the updated blue pause badge. Water and Three Body are byte-identical
outside the previously defined UI regions. Solver differs at eleven non-UI pixels
by at most one channel value. That diagnostic does not modify the gate or its
full-image threshold. Exact PNG and current producer hashes are in the manifest.

No reference was changed. The original e9f02a925 screenshot producer has not
been located. The retained pre-FP8 producer belongs to later bf8bbcd9b source
and is not a substitute for the missing executable. AGENTS.md line 964 says
that if the exact prior first-party producer is unavailable, the golden cannot
be replaced. Replacing these three specific references therefore needs an
explicit exception to that preservation requirement. Old reference PNGs and
the exact current first-party producer are preserved here for review.


The user accepted these exact screenshots and requested the work be committed,
resolving the specific missing-original-producer exception requested above.
owner-approval.json records the scope and exact old/new hashes. The three
approved PNGs have been installed byte-for-byte. The archived producing
executable was verified; no substitute is claimed for the unavailable original.
