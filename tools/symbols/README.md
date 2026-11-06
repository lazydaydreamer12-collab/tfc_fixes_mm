# tfc.dll / tfc.so cross-symbolizer

Names the functions in the shipped **`tfc.dll`** (MSVC, almost no symbols) by
porting them from **`tfc.so`** (GCC, full DWARF symbols). The two are the same
TFC source built by two compilers, so `tfc.so` is a Rosetta Stone for reading
the Windows DLL — and it is how the plugin finds the Windows vtable slots and
field offsets it has to patch (e.g. `conc_edge`'s slot 62).

## Run

Under WSL / Linux with binutils (`objdump`, `nm`, `c++filt`) and `python3`:

```sh
cd tools/symbols
./run.sh
```

Override the binaries with `TFC_SO` / `TFC_DLL`; the defaults point at the local
Half-Life install. Everything is regenerated into `out/`.

## How it works

Both binaries are non-PIC/absolute-addressed, so `objdump -d` gives directly
usable disassembly and data references on each side. The matcher runs in stages,
most-certain first:

1. **Exports** — `tfc.dll` exports ~430 functions as MSVC-mangled names (the
   GoldSrc save/restore table). Demangle to `Class::method`, match to the same
   `tfc.so` name. Exact (confidence 1.00).
2. **Vtables** — read every class vtable from both binaries (`tfc.so` by
   `_ZTV` symbol, `tfc.dll` by MSVC RTTI: TypeDescriptor → COL → vftable). The
   only layout difference is the destructor (GCC emits 2 slots, MSVC 1), found
   by name in the `tfc.so` vtable, so the slots align exactly. Names every
   virtual, including vtable-only ones Ghidra's auto-analysis misses (0.98).
3. **Fingerprint + call graph** — for the rest (static helpers, free functions,
   operators), match on shared string literals and float/int constants, weighted
   by rarity, with a greedy 1:1 assignment and a margin gate, then propagate
   along the call graph. Confidence-tiered 0.68–0.90.

MSVC `/OPT:ICF` folds identical trivial functions (empty bodies, `return 0`,
constant returns, destructors) to a few shared stub addresses, so one DLL
address can legitimately be many source functions. Those are listed separately
(`icf_folded`) and **not** given a single misleading name; the per-slot vtable
cross-map still names each slot correctly (you hook by slot, not address).

## Outputs (`out/`)

| file | what |
|---|---|
| `tfc_dll_symbols.json` | every named address → name, signature, tfc.so address, how, confidence; plus the `icf_folded` ambiguous set |
| `apply_tfc_names.py`   | Ghidra (Jython) script: run it with `tfc.dll` open to label every function and annotate the folded stubs |
| `tfc_dll.names`        | `ImportSymbolsScript` format (confidence ≥ 0.8), for Ghidra's *File → Import Symbols* |
| `VTABLES.md`           | per-class vtable cross-map: DLL slot ↔ method ↔ DLL address — the reference for finding a slot to hook |

## Coverage (current build)

~1637 of Ghidra's 2055 functions named (**~80%**): 430 by export, 1026 by
vtable, 181 by fingerprint/call-graph; 66 ICF-folded stubs catalogued
ambiguously. A 17,612-row vtable cross-map over 219 classes. The unnamed
remainder are mostly functions one compiler inlined and the other did not (no
1:1 counterpart) or featureless stubs.

Validated against known ground truth: the pipeline independently reproduced
`CBasePlayer::TeamFortress_TakeConcussionBlast` at `0x10046c90` (hand-RE'd for
`conc_edge`) and `CBasePlayer::TakeHealth` at `0x10044c50` (fix #7's hook), and
correctly named `CBasePlayer::Spawn` at `0x10043240` — a vtable-only function
Ghidra had left undefined.

## Files

`extract.py` (section maps, disassembly parse, per-function fingerprints, PE
export table), `vtables.py` (both binaries' vtables + RTTI), `match.py`
(name/demangle helpers), `pipeline.py` (the stages + emitters), `run.sh`.
Regenerate after a TFC update to re-check every slot and offset against the new
binaries — the export-directory RVA and image base are read from the PE header,
not hard-coded, so it survives an update.
