# Section 3.2 — Cache Operation at High Level (Tag / Set / Offset)

## Why addresses split into Tag / Set / Offset

A 64-byte cache line means a block boundary is just a power-of-2 (64 = 2^6) — so finding which block an address falls into is `floor(address / 64) * 64`, which is hardware-equivalent to just zeroing the low 6 bits. No division ever actually happens in silicon; it's a direct bit read.

**Offset (low O bits):** which byte inside the already-found cache line you want. Read directly from the address, no computation — e.g. address `12345` with a 64-byte line: block = `floor(12345/64)*64 = 12288`, offset = `12345 - 12288 = 57` (the low 6 bits of 12345).

**Set (next S bits, significance — the key point):** the reason caches aren't fully associative (any block → any slot) is cost. A fully-associative cache would need one tag comparator per slot, checked in parallel on every lookup — 512 comparators for a 512-line cache. That's too expensive in chip area/power to scale beyond tiny structures (e.g. TLBs).

**So the Set field exists specifically so we don't have to compare against every tag in the whole cache.** Each address is restricted to one small fixed group of slots (a "set"), and *which* set is computed directly from the address bits — a plain index, not a search. Only once you've landed on that one set (e.g. 8 slots, for an 8-way cache) do any tag comparisons happen — 8 comparators instead of 512. The Set bits turn "search the whole cache" into "jump to the right small group, then search only that."

**Tag (remaining top bits):** the only part actually stored per line and compared — but now only against the handful of tags in the one set the Set bits already pointed to.

## Mechanism recap (see the ASCII flow diagram from this session for the full picture)
1. Split address → Tag | Set | Offset.
2. Set bits → direct index to one of `2^S` sets (no comparison).
3. Tag bits → compared in parallel only against the few ways in that one set.
4. Match + valid → hit; use Offset to pick the exact byte from the matched line.
5. No match → miss; fetch from the next level, evict a way in that set (e.g. LRU), retry.

## What's precisely stored in a cache line entry (one way, one set)
Tag (identifies the block; Set is implicit in slot position, Offset isn't stored at all) + Data (the full line, copied from RAM) + Valid bit + Dirty bit.

## Writes: why a line must be loaded first on a miss, and the dirty-bit lifecycle
1. Tag comparison gives hit vs. miss — never lets you mistake another address's data for yours.
2. **Hit:** full correct line already resident — just overwrite the target bytes, mark dirty.
3. **Miss ("write-allocate"):** slot is empty w.r.t. your address (after eviction if needed), so the real full line must be fetched from the next level first, your bytes merged in, then marked dirty — otherwise the untouched bytes would be garbage that later corrupts RAM on eviction. (Exception: write-combining, section 6.1 — not yet covered.)
4. **Dirty bit:** set the instant a write diverges the line from RAM; cleared the instant it's written back in sync. Purely a cache/RAM inconsistency marker, nothing more.

## Inclusive vs. exclusive caches (L1d/L2 relationship)
1. **Inclusive (Intel):** every line in L1d is guaranteed to also exist in L2 — deliberately duplicated. A miss fills the line into all levels at once (L1d, L2, and L3 in a 3-level hierarchy).
2. **Exclusive (AMD/VIA):** a line lives in exactly one level at a time, never duplicated. A miss fills only L1d; evicting from L1d demotes the victim down into L2 (not a discard), which can cascade further into L3 then memory if those levels' sets are also full.
3. **Why inclusive wins:** evicting from L1d is just a drop, no data movement, since L2 already independently holds that line. The cost is wasted duplicate storage, but with a large enough L2 that waste is a small fraction of total capacity, so it "pays off."
4. **Why exclusive wins:** no wasted duplication, every cached byte occupies exactly one level, so effective total capacity is higher. Filling a new line only touches L1d, cheaper than inclusive's 3-level fill. Cost: evictions can cascade through multiple levels instead of being a simple drop, though a large L2 reduces how often that cascade has to go all the way down.

## Hiding load latency behind the pipeline (why the ~3/~14/~240 cycle costs aren't always fully felt)
The raw access-time numbers (Register ≤1, L1d ~3, L2 ~14, Main Memory ~240 cycles) don't all land as pure added runtime every time, because modern CPUs are pipelined: a memory load started early (e.g. at decode) can run in parallel with other, independent instructions advancing through their own fetch/decode/execute stages, instead of the CPU idling while it waits.
Whether the cost is fully hidden depends on whether there's enough already-available independent instruction work to fill the exact number of cycles the load takes — if the supply of independent work runs out before the load finishes, the remaining cycles become an exposed stall.
A longer/deeper pipeline (bigger instruction window) can hold more independent work in flight at once, which is why L1d's ~3 cycles hides easily on any pipeline, L2's ~14 cycles only hides on processors with long enough pipelines, and main memory's ~240 cycles essentially never fully hides — no realistic instruction window supplies 240 cycles' worth of independent, ready work, so memory stalls are almost always felt as real lost time.

```
L1d load (3 cycles) -- SHORT pipeline, easily hidden:
Cycle:         1  2  3  4  5  6
Load in flt:  ## ## ##
Other instrs: F1 D1 E1 F2 D2 E2
Stall/bubble: -- -- -- -- -- --
  -> by cycle 3 the value is ready; independent instrs (F1..E2) already
     filled every cycle. No exposed stall. Fully hidden.

L2 load (14 cycles) -- pipeline only has ~6 cyc of ready independent work:
Cycle:         1  2  3  4  5  6  7  8  9 10 11 12 13 14
Load in flt:  ## ## ## ## ## ## ## ## ## ## ## ## ## ##
Other instrs: F1 D1 F2 D2 F3 D3 -- -- -- -- -- -- -- --
Stall/bubble: -- -- -- -- -- -- XX XX XX XX XX XX XX XX
  -> first 6 cycles hidden behind independent work (F1..D3), but no more
     independent instructions ready -> remaining 8 cycles are exposed stall.
     A DEEPER pipeline (bigger instruction window) could supply independent
     work for all 14 cycles -> fully hidden instead.

Main memory load (240 cycles) -- no realistic window is that deep:
Load in flight:  [##################################...### 240 cycles ...]
Other instrs:    [F1 D1 F2 D2 ... maybe 20-40 ready instrs, then none left]
Stall/bubble:    [--------------------------][XXXXXXXXXXXXXXXXXXXXXXXXXXXX]
  -> only a small fraction of 240 cycles can ever be hidden this way;
     the rest is an exposed stall almost every time.
```
