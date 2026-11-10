# Section 3.1 — CPU Caches in the Big Picture

Source: Drepper, *What Every Programmer Should Know About Memory*, pages 13–15 (Figures 3.1–3.3), stopping right before 3.2 "Cache Operation at High Level."

## Diagrams

**Figure 3.1 — Minimum Cache Configuration**
```
                +-------------+
                | Main Memory |
                +-------------+
                       |
             <---------+---------> Bus
                       |
                +-------+       +-----------+
                | Cache |<----->| CPU Core  |
                +-------+       +-----------+
```
CPU core is not directly connected to main memory anymore — it only ever talks to the cache. Main memory and cache both sit on the system bus (the "FSB"); the CPU-core↔cache link is a separate, dedicated, fast connection.

**Figure 3.2 — Processor with Level 3 Cache**
```
                +-------------+
                | Main Memory |
                +-------------+
                       |
                      Bus
                       |
                +-------------+
                |  L3 Cache   |
                +-------------+
                       |
          +-------------+   +-------------+
          |  L2 Cache   |<->|  L1i Cache  |
          +-------------+   +-------------+
                 |                  |
          +-------------+   +-------------+
          |  L1d Cache  |<->|  CPU Core   |
          +-------------+   +-------------+
```
Introduces the naming used for the rest of the document: L1d (level 1 data), L1i (level 1 instruction), L2, L3. Schematic only — data doesn't necessarily physically pass through every higher level on its way from core to main memory.

**Figure 3.3 — Multi-processor, multi-core, multi-thread**
```
                                   Main Memory
                                        |
                                       Bus
              -----------------------------------------------------
              +-----------------------+   +-----------------------+
              |      Processor 1      |   |      Processor 2      |
              |     (L2/L3 shared)    |   |     (L2/L3 shared)    |
              |                       |   |                       |
              |+---------+ +---------+|   |+---------+ +---------+|
              ||  Core A | |  Core B ||   ||  Core A | |  Core B ||
              || (own L1)| | (own L1)||   || (own L1)| | (own L1)||
              || T0   T1 | | T0   T1 ||   || T0   T1 | | T0   T1 ||
              |+---------+ +---------+|   |+---------+ +---------+|
              +-----------------------+   +-----------------------+
```
- Threads on the *same core* share that core's L1 (almost everything except the register set).
- Separate cores on the *same processor/chip* have their own L1 each, but share L2/L3.
- Separate processors (separate chips) share nothing cache-wise — only the bus connecting them.

## Questions and answers

**1. In the minimal cache configuration (Figure 3.1), what changed about how the CPU core connects to main memory compared to even earlier systems?**
It's not a direct connection to main memory anymore — it's via the cache. The cache↔CPU connection is a special, fast dedicated link, faster than the shared system bus connecting cache and main memory.

**2. Why does separating L1i (code) from L1d (data) work well?**
Executing code needs two independent streams: instructions and data. Caching the decoded instruction stream separately also makes the processor's decode step easier/faster (ties into the "trace cache" idea — see Q5).
*Refinement:* the paper's actual framing is about avoiding **contention** in a shared cache, not physical proximity of segments — code and data access patterns are independent of each other, so sharing one cache between them would let one stream evict the other's useful entries for no good reason. Separate caches mean neither stream can pollute the other's cache space.

**3. Why did a single level of cache stop being enough, leading to the L1/L2/L3 hierarchy?**
After introducing a single cache, demand to store more data at that level increased, but directly enlarging L1 was too costly (speed degrades as size grows). So intermediate levels were added instead — less performant than L1 but able to hold much more data, while still far faster than main memory.

**4. What's shared between threads on the same core vs. separate cores vs. separate processors?**
L1 caches end up shared between threads on the same core. Two separate cores (same chip) share L2/L3 but each has its own L1. Two separate processors (separate chips) share nothing cache-wise — only the bus.

**5. Bonus: what's a "trace cache," and why do x86/x86-64 benefit from it specifically?**
x86/x86-64 instructions are variable-length and costly to decode (classic CISC overhead) — turning raw bytes into the micro-ops the pipeline executes takes several pipeline stages, every time. A trace cache stores the *already-decoded micro-ops* in L1i instead of the raw instruction bytes, so a cache hit can skip straight to execution, bypassing the decode stages entirely. This matters most right after a pipeline stall (e.g. a branch misprediction), where skipping redundant decode work saves real cycles getting back to useful execution. This is an L1i-specific trick — L2 and higher are unified (code + data) and store code in plain byte form, so an instruction re-fetched from L2 after falling out of L1i gets decoded again.

**Follow-up: does L1i hold decoded instructions or original bytes?**
For x86/x86-64 specifically: decoded instructions (that's precisely why it's called a trace cache in that context, not a plain instruction cache). This is implementation-specific to x86/x86-64's costly CISC decode step, not a universal property of "L1i" as a concept — a simpler architecture with cheap decoding has less reason to do this and could just cache raw bytes.
