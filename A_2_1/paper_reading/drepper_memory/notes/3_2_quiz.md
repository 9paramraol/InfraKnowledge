# Section 3.2 — Quiz

**1. Why does a 64-byte cache line mean the address split can be done with pure bit-masking instead of real division — and what's the Offset field, mechanically?**
Answer: tag and set can be evaluated using the mask and offset, it does not have to be stored, and since the divisor is power of 2 bit shift/masking etc are way faster.
Grading: correct on the core mechanism (power-of-2 divisor → bit-shift/mask instead of real division). One precision fix: Offset and Set are not stored, but Tag **is** stored — specifically because it needs to survive for comparison on every future lookup. Set is computed-and-discarded; Tag is computed-and-kept.

**2. What's the core reason caches use a Set index instead of being fully associative, and what does the Set field save you from having to do on every lookup?**
Answer: the tag comparison for the entire cache will be costly so the entries corresponding to a set index are only compared.
Grading: correct, exactly the point.

**3. Besides the Tag, what else is stored in a single cache-line entry (one way, one set), and why is the Set itself not one of the stored fields?**
Answer: set well can be inferred from the index of the cache line, there is dirty bit and other bit that are stored for mgmt purposes.
Grading: correct that Set is inferred from slot position, and correct to flag Valid bit and Dirty bit. Missing the most obvious one: the Data itself — the actual cached bytes copied from RAM — is also stored, easy to overlook since it's the "obvious" part.

**4. Walk through what happens on a write miss: why can't the hardware just write your few changed bytes into an empty/reclaimed slot and call it done? What has to happen first, and why?**
Answer: cache line is filled then marked dirty, then synced. If the empty cache line is written then when finally its synced the written value will be gone, plus we don't know where to store what we are writing into the main memory, no tag is there in the cache.
Grading: incorrect framing of the actual failure mode. When hardware claims an empty/evicted slot for your address, it sets the tag to your real address immediately — so "where to write it back" is never actually unknown. The real problem is the other, untouched bytes in the line: if you only wrote your few changed bytes and immediately marked the slot valid+dirty, the rest of the line would be garbage sitting under a now-correct tag — and that garbage gets written back to RAM on eviction, corrupting real memory outside of what you meant to change. That's why the real line must be fetched first, merged with your changes, then marked dirty.

**5. Give the core distinction between an inclusive and an exclusive cache (think Intel vs. AMD/VIA), and name one real advantage of each.**
Answer: intel - all the caches have a given line, if evicted its evicted from the top levels. For AMD its cascading removal from top goes to the bottom, so on. Intel's advantage is that eviction does not trigger cascade, its just on the top faster level. The advantage of AMD is space management, no duplication, so no wastage occurs.
Grading: correct and well explained, both advantages stated accurately.

**6. The access-time table showed Register ≤1, L1d ~3, L2 ~14, Main Memory ~240 cycles — but these aren't always fully "felt" as added runtime. What determines whether a load's latency gets hidden versus causing a real stall, and why does this work reliably for L1d but essentially never for main memory?**
Answer: the hardware has some tasks it can do in parallel so by the time the load happens, or within lesser time, the next instruction is ready — but the issue is if the load time itself is very long, hardware has no other option but to stall.
Grading: correct core idea (independent parallel work fills the wait; stall happens once that runs out). Missing layer: how much parallel work is available is bounded by the pipeline's depth/instruction window size. A longer pipeline holds more independent instructions in flight — why L1d's ~3 cycles hides on almost any pipeline, L2's ~14 only hides on long pipelines, and main memory's ~240 essentially never fully hides, since no realistic instruction window supplies that much ready independent work.
