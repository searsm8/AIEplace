# third_party patches

Local modifications that the *vendored* copy of a third-party library carried, kept here so the
switch to submodules did not silently drop them. **None of these is applied by the build.** They
are archaeology: if a bug ever traces back to one of these files, this is where to look first.

## `limbo-3.5.2-gdsdb-round.patch`

Recovered 2026-08-05 when `third_party/Limbo` went from 4195 vendored files back to a real
submodule (TODO #9). The vendored tree turned out to be upstream tag **3.5.2** (`81b64433`) plus
exactly this one difference: `std::round` around the SREF rotate/magnify position arithmetic in
`limbo/parsers/gdsii/gdsdb/GdsObjectHelpers.h`.

Upstream has a semantically equivalent fix in commit `0ce68951` ("gdsdb: fix numerical bug in
SREF and PATH position calculation"), but on a gdsdb lineage that differs from 3.5.2 in 11 files,
and `git log -S` finds this exact code on no upstream ref — so it was applied locally, by hand,
by whoever vendored Limbo.

**Deliberately NOT carried forward.** AIEplace links only `lefparseradapt`, `defparseradapt`,
`bookshelfparser` and `gzstream`, and includes only `gdsii/stream/GdsWriter.h`; nothing here
touches `gdsdb`. Re-applying it would mean maintaining a Limbo fork for code we do not compile.
If AIEplace ever starts reading or writing GDSII cell references, revisit — the right move then
is to move the submodule pin onto an upstream commit that contains the fix, not to re-patch.
