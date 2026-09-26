# asynchronousByzantineAgreementProtocols

Generated with Claude Code (https://claude.ai/code)

Bracha 1987 Figures 1, 3 and 4 as composable pure state machines (Figure 2 is captured for completeness and subsumed by Figure 3), and Ben-Or/Kelmer/Rabin 1994 Section 4 Figure 3 -- Protocol Agreement[Q], the asynchronous common subset -- composed from them: N Fig 1 reliable broadcasts carry the A-Casts, N Fig 4 binary agreements decide inclusion. ANSI C89, no dependencies, no I/O, no threads, no dynamic allocation; the caller provides memory, delivers messages and executes output actions. Limits: n up to 256 processes, t up to 85 (n > 3t), values up to 256 bytes, at most 85 phases per binary agreement (three rounds per phase, 255 rounds in an unsigned char). Rounds are 0-based in the code where the papers count from 1.

Two bridges. The headers are the API reference: `bracha87.h` and `bkr94acs.h` carry every per-function contract. `BPR.md` governs the stratum beneath the papers' reliable-channel assumption -- the retry (Bracha Phase Retry, BPR), its retire gates, the sweep-side pacing and the abandonment model. Where this file touches either it points, and keeps only what a deployment wires.

## The papers

This repository is written for a reader who has these five papers on their desk. Without them it will not make much sense: the code is organized by their figures, the departures table below is checked against their words, and the terminology is theirs. The in-tree extracts (`*.txt`) carry the passages the code and this file rest on, but they are extracts, not substitutes -- a little light reading first is the way in, and for a reader new to the subject the order below is the reading order.

- M. J. Fischer, N. A. Lynch, M. S. Paterson, "Impossibility of Distributed Consensus with One Faulty Process," *Journal of the ACM* 32(2), 374-382 (1985). The impossibility everything here answers. Extract `FLP82.txt`, named for the technical report Ben-Or cites.
- Michael Ben-Or, "Another Advantage of Free Choice: Completely Asynchronous Agreement Protocols (Extended Abstract)," PODC '83, 27-30. The escape by randomization, and the protocol beneath Bracha's Figure 4 (Bracha names it with one other). Extract `BenOr83.txt`.
- Gabriel Bracha, "Asynchronous Byzantine Agreement Protocols," *Information and Computation* 75, 130-143 (1987). Implemented: `bracha87.[hc]`; extract `Bracha87.txt`.
- Michael Ben-Or, Boaz Kelmer, Tal Rabin, "Asynchronous Secure Computations with Optimal Resilience (Extended Abstract)," PODC '94, 183-192. Implemented: Section 4 Figure 3 in `bkr94acs.[hc]`; extract `BKR94ACS.txt`.
- J. H. Saltzer, D. P. Reed, D. D. Clark, "End-to-End Arguments in System Design," *ACM Transactions on Computer Systems* 2(4), 277-288 (1984). Decides where a correctness function lives; `BPR.md` applies it to the retry. Extract `SRC84.txt`.
- `BPR.md`, this repository's own statement for the stratum no paper covers -- the retry beneath the papers' reliable-channel assumption, its retire gates, the pacing and the abandonment model. Read last; it assumes the five above.

## Departures from the figures

Every place the code departs from, or reads into, the figures. Each row is checkable against the extracts in this tree (`Bracha87.txt`, `BKR94ACS.txt`) and the named code. Class: **paper** -- the paper's own rule, kept where a reader might expect otherwise; **reading** -- this repository's reading where the paper's words admit more than one; **construction** -- this library's own, with no paper behind it. *Argument* is where the reasoning for the row is written down; *Teeth* is the test that fails if the row's code changes -- the check that bites. "Note N" is an Implementation Note below; "Section X" is a section of `test/test_bkr94acs_blackbox.c` unless another suite is named. A `.dtc` file is a decision table: the paper's rules as rows, in the paper's vocabulary, which `dtc` (the decision table compiler, in the sibling `decisionTableCompiler` repository) compiles into the C dispatch each entry point includes (*The audit chain* below). It is used here because `dtc` refuses a table that is not exhaustive and mutually exclusive, so a figure transcribed as rules cannot silently leave an input combination unhandled or handled twice -- a property a hand-written dispatch has only by testing -- and because the rows stay in the paper's words, so auditing them against the figure is a reading, not a reconstruction. Two provenance facts about the quotations: Fig 1's text is the extract's reconstruction of a garbled scan (the NOTE in `Bracha87.txt`), and BKR94 Lemma 2's "Part A-D" and "case (i)/(ii)" labels are the extract's structuring of the paper's three proof paragraphs, not the paper's numbering.

| # | Figure | The paper writes | The code does | Class | Argument | Teeth |
|---|---|---|---|---|---|---|
| 1 | Bracha Fig 1, steps 1-2 | A bare "(n+t)/2 (echo,v) messages", no relation symbol; the Lemma 1 proof: "more than (n + t)/2" | Echo threshold is `(n+t)/2 + 1` in integer arithmetic (`bracha87.c`, `ecGtHalfNT`) | paper (the Lemma 1 proof's relation, where the figure carries none) | Fig 1 banner in `bracha87.h`; the NOTE in `Bracha87.txt` | `testFig1Thresholds`; `test_bracha87_blackbox` "Fig1 Rule 2 precise echo threshold" at n=4 t=1 and n=7 t=2 |
| 2 | Bracha Fig 1, the steps as a sequence | Section 2.1: "In each step a process waits until it receives enough messages that permit it to send the next message type (including those received at previous steps), then it sends a message to all the processes and moves to the next step." | The rules chain: "echoed" and "readySent" in a later step are read after the earlier steps ran on this same message, so one arrival can return echo, ready and accept together; the accept row's readySent conjunct never withholds an accept | paper for the sequence and the carried counts; reading for evaluating every step a single arrival enables | Fig 1 banner in `bracha87.h`; the chaining note in `bracha87Fig1.dtc` | `testFig1Cascade` (Rule 3 -> 5 -> 6); `test_bracha87_blackbox` "an echo crossing the threshold at an un-echoed process echoes and readys at once" and "t=0: a READY that overtakes the INITIAL crosses both ready thresholds at once" |
| 3 | Bracha Fig 1, step 1 | "in(initial, v) from p" -- the initiator | `bkr94acsAcastInput` / `bkr94acsBaInput` drop an INITIAL whose authenticated sender is not the designated initiator; the bare `bracha87Fig1Input` is not told its initiator, so a bare-layer caller filters | paper, enforced as a protocol check rather than assumed of the transport | Note 14; the INITIAL sender obligation at `bracha87Fig1Input` | `testForgedInitial`; Section A5 "forged INITIAL rejection (Note 14)"; `test_ingress` |
| 4 | Bracha Fig 1, "to all" | Section 2.1: "sends a message to all the processes" and, in the next sentence, "to any other process"; silent on whether a process counts its own echo or ready | A process's own echo and ready count only when delivered back through `bracha87Fig1Input` with `from == self`; sending sets the sent flags and nothing else | construction | *Message System* item 4; `BRACHA87_SKIP_TST` in `bracha87.h` | `test_bracha87_blackbox` "Fig1 delivery to self": with t silent, processes withholding their own hand-back send no READY and accept nowhere, and the same run with self delivered accepts everywhere; the withheld half reds under mutant M07's lowered echo threshold |
| 5 | Bracha Section 1, the model | "a reliable message system in which no messages are lost" | BPR re-sends each owed INITIAL/ECHO/READY under fair loss until a retire gate closes; READY never retires on local state | construction, below the papers | `BPR.md` (*The Gap*, *Retirement*); Notes 10, 11 | `testFig1Bpr`, `testBpr`, `testBprHighDrop`, `testBprByzantineSilent`; Section C |
| 6 | Bracha Fig 1, the message set | (initial, v), (echo, v), (ready, v) | The library's READY carries two annotation bits, ACCEPTED and RECEIVED, read only by the retry's per-process suppression and the quiescence gate | construction | `BPR.md` (*Suppression and the Announcements*); Note 13; the packed wire byte at the message-class defines in `bkr94acs.h` | `testFig1SkipAccept`, `testFig1ResendReceived`, `testBprSkipAccept`; Sections H and P |
| 7 | Bracha Fig 3, VALID^k | "there exist n - t messages m1 ... m_{n-t} in VALID^{k-1} such that v = N(k-1, {m1 ... m_{n-t}})" (VALID^{k-1}_p in the extract; the subscript is elided here) | N is called once on the whole validated set (which grows past n-t) and answers permissive when different n-t subsets disagree; a (d, v) is admitted only if some n-t subset legitimately produces it | reading | Notes 3, 6, 8; `bracha87Nfn` in `bracha87.h` | `test_predicates` (960 `fig4Nfn` inputs, 165 `fig3IsValid` evaluations, n=4 t=1); `testFig4SubsetMajority`, `testFig4SubsetMajorityBoundary`, `testFig4DflagInjection`; `testFig4AdoptWindow` with mutant M53, which reads step 3 (ii) as free |
| 8 | Bracha Fig 3, the recursion; Lemma 6 | Lemma 6: "if p and q are correct then VALID^k_p = VALID^k_q" -- an equality; its proof and Lemma 7 carry the *eventually* | Stored round-(k+1) messages are re-evaluated on every growth of VALID^k past n-t, not only on the first crossing | reading (VALID^k is monotone in VALID^{k-1} by its definition) | Note 7; Rule 3.4 in `bracha87Fig3.dtc` | `testFig3RecascadeOnGrowth`; `test_predicates` cascade (4 delivery permutations) |
| 9 | Bracha Fig 4, every step | "Wait until validate n - t 3i+1-messages" (the scan's bare tag; the extract adds parentheses), and 3i+2, 3i+3; then compute over "the n - t validated messages" | The arrival path only banks evidence; each round is computed from the caller-paced sweep over whatever has validated by then, n-t up to n, and the proofs are read as holding for any validated sample of at least n-t | reading -- of the proofs, not of the figure's words, which say n-t at every step | `BPR.md` (*The Sweep-Side Decisions*, Seam 2); the sweep-side banner in `bkr94acs.h`; "WHEN TO CALL IT" at `bracha87Fig4Round` | Section G (G1-G4); `testTurnDutyVacuityT0` |
| 10 | Bracha Fig 4, step 1 | "value_p := majority value of the n - t validated messages" -- nothing about a tie | The majority tie-breaks to 0, at both sites: `fig4Nfn` (the validation side) and the round-3i transition in `bracha87Fig4Round`; a tie is reachable because the sample can be even | construction (the paper names no tie) | Note 6 | `fig4Nfn`: `testFig4SubsetMajority`, `testFig4SubsetMajorityBoundary`, `test_predicates`; `bracha87Fig4Round`: `testFig4RoundTieBreak`, and mutant M52 in `test/mutants.sh` flips the tie to 1 against that label |
| 11 | Bracha Fig 4, step 3; Section 4 | "Go to round 1 of phase i+1" after every case; "For notational convenience, the protocol in Fig. 4 does not terminate once a decision is made. However, this can be easily accomplished." | A decided process runs the phase after its decision as the figure writes it (the figure has no decided state; Lemma 9 holds its value at the decision) and opens no phase after that: its step-3 turn of that phase returns no broadcast; DECIDE is returned once | construction: the halting the paper says can be accomplished and constructs none, bounded by Theorem 2's Agreement and Lemma 9 | Note 1 (and 2, 9); `bracha87Fig4Round` in `bracha87.h` | `testFig4PostDecide`, `testFig4PostDecideUngated`, `testPostDecideMultiPhase`, `testPostDecideContinuation`, `testPostDecideBound`; `test_bracha87_blackbox` "figure-unchanged arm" and "two-wave arm"; `test_bkr94acs_blackbox` Section R (two waves through the composition; the pin); mutants M84 (unbounded), M85 (cut at its first round), M86 (the composition's round space closed at the decision) and M87 (never closed) |
| 12 | Bracha Fig 4, phases | Unbounded | `maxPhases` 1..85, refused outside; `BRACHA87_EXHAUSTED` when spent without a decision, no substitute decision, COMPLETE unreachable | construction | Note 12; *The phase budget*; `bracha87Fig4Sz` | `testExhausted`, `testExhaustedAmongDecided`, `testFig4MaxPhasesRefused`; Section D |
| 13 | Bracha Fig 4, step 3 (iii) | "coin_toss (0 or 1 with probability 1/2)" | `bracha87CoinFn(closure, instance, phase)`, an oracle the caller supplies; `bkr94acs` names `instance` with the BA's process index | construction (the callback's shape and the coin's name; its value is the caller's) | *Coin Choice*; `bracha87CoinFn` in `bracha87.h` | `testFig4Step3Boundary` (dc == t coins, not adopts), `testFig4MultiPhase`; no arm grades a coin |
| 14 | BKR94 Fig 3, step 2 | "Upon completing 2t+1 BA protocols with output 1"; the Section 4 opener sizes the subset "of size at least n - t >= 2t + 1" | The floor is n-t; equal to 2t+1 only at n = 3t+1, strictly later above it | reading | Note 15; `BPR.md` (Seam 1) | `testFanoutFloorAboveEdge` at n=5 t=1 and n=8 t=2 (the paper's 2t+1 reads HELD, n-t reads TOLERANCE); mutant M35 in `test/mutants.sh` |
| 15 | BKR94 Fig 3, step 2 | "Upon" -- enabling evidence | The fanout is fired from the caller-paced sweep (`bkr94acsFanoutDuty` / `bkr94acsFanout`), not at the instant the count crosses | reading | `BPR.md` (*The Sweep-Side Decisions*, Seam 1); the sweep-side banner in `bkr94acs.h` | Section F (F1-F4); `testFanoutDutyVacuityT0` |
| 16 | BKR94 Fig 3, step 2 | "BA protocols with output 1" | The count is BA decisions of 1 -- never Fig 1 accepts, never Q(j) = 1 events | paper (Lemma 2 Part A case (i)) | header of `bkr94acs.dtc` | `testStepTwoTrigger` |
| 17 | BKR94 Section 4, Q | Q is application-supplied; the paper's example is that "the property may be that a player has properly shared his input"; Section 4 never mentions A-Cast | Q(j) = "Fig 1 for process j has ACCEPTED", and the value agreed on is carried on that Fig 1 A-Cast | construction | header of `bkr94acs.h`; header of `bkr94acs.dtc`; `BKR94ACS.txt` | Section B (B1, Lemma 2 Parts A/B/C/D); Section E (equivocating A-Caster); `testValues` |
| 18 | BKR94 Section 4, BA | BA is taken as given, and the Lemma 2 proof consumes "the correctness property of the BA protocol"; that any BA with correctness, termination and agreement serves is the extract's reading of that proof | Bracha Fig 4 over Fig 1 and Fig 3, one instance per process index | construction | header of `bkr94acs.h` | Section B; `testBasic` |
| 19 | BKR94 Fig 3, step 3 | Output SubSet; nothing after | Past COMPLETE the retry keeps serving every A-Cast except those whose BA decided 0 (the verdict gate), until quiescence or abandonment | construction | `BPR.md` (*The Composition Layer*, *Termination and Abandonment*) | `testBprProcessGate`; Sections H1 and M1 |
| 20 | Bracha Fig 1, what a broadcast carries | (initial, v) and nothing beside it | An application may pair a payload with its A-Cast on a side channel; the receiver feeds no row of that A-Cast until it holds the payload (the hold at Input), and the initiator retires the side channel on the readied set, per process and wholly -- not on the echoed set, which need not close, and not on ACCEPTED | construction | `BPR.md` (*The Scoped-Claim Registry*, the paired payload); `bkr94acs.h` at `bkr94acsAcastAllReadied` | Sections Q1 and Q2; mutants M64 and M65 in `test/mutants.sh` read the echoed set against Q1's labels |

## Build and run

```bash
make            # bracha87.o, bkr94acs.o, example_bracha87Fig1, example_bkr94acs -- seconds
make check      # build and run the eight test binaries (Test coverage below) -- seconds to minutes; every reader
make schedules  # the schedule explorer's full run -- half an hour to hours; for a reader auditing the instruments or changing the machine
make strategies # its adversary configs alone -- half an hour to hours; the same reader
make mutants    # test/mutants.sh: anchored single defects, each graded against a named check -- a quarter of an hour to hours; the same reader
make rules      # regenerate the *Rules.c dispatch snippets from the .dtc tables (needs ../decisionTableCompiler/dtc) -- about a minute, nearly all of it the Fig 1 table; only after editing a .dtc
make clean      # remove build artifacts; make clobber also removes dtc's .psu intermediates
```

A C89 compiler is the only requirement: the generated `*Rules.c` snippets are committed and treated as source, so nothing in the ordinary build runs `dtc` (*The audit chain* below).

`example/bracha87Fig1.c` -- one designated initiator broadcasts a multi-byte value. Every run below ends by quiescence -- the retry has nothing left to re-send, so the wire falls silent -- within 3 sweeps (a sweep is one full pass of the retry over every sent instance), except the equivocating ones, in which nothing can ever quiesce and the example stops at a fixed sweep count:

```bash
./example_bracha87Fig1 4 1 hello                # 4 of 4 accept "hello"; Lemma 2 ok, Lemma 4 ok
./example_bracha87Fig1 -s 42 7 2 transactionXYZ # shuffled delivery, 7 of 7 accept
./example_bracha87Fig1 -v -o 1 4 1 ping         # verbose trace, process 1 is initiator
./example_bracha87Fig1 -b 2 4 1 hello           # Byzantine initiator equivocates at split 2: none of the 3 correct processes accepts -- Theorem 1's second arm (-b 3 the same; -b 1 and -b 4: all 3 accept)
```

`example/bkr94acs.c` -- N processes A-Cast strings and agree on a subset of them:

```bash
./example_bkr94acs 4 1 joe sam sally tim        # 4/4 agreed, every process quiescent at tick 201
./example_bkr94acs -s 42 4 1 joe sam sally tim  # shuffled delivery order
./example_bkr94acs 4 0 joe sam sally tim        # t=0
./example_bkr94acs -v 7 2 alpha bravo charlie delta echo foxtrot golf
./example_bkr94acs -d 3 4 1 joe sam sally tim   # -d: hold that process's A-Cast until step 2 first enables; zero patience
./example_bkr94acs -d 3 -g 1 4 1 joe sam sally tim  # -g: patience in sweeps; the same schedule with one (each example prints its flags when run with none)
./example_bkr94acs -b silent 4 1 joe sam sally tim  # process 0 never speaks: 3/4 agreed, no process quiesces
./example_bkr94acs -b poke 4 1 joe sam sally tim    # process 0 re-arms retired instances forever: 12 of 12 aimed re-sends suppress everyone but the poker
```

The `-d` / `-g` pair demonstrates the sweep-side pacing (`BPR.md`, *The Sweep-Side Decisions*). Process 3's A-Cast is released at the knife edge where step 2 first enables:

```
$ ./example_bkr94acs -d 3 4 1 joe sam sally tim
...
process 3: step 2 fires (tick 3) -- enter 0 in 1 unentered BA(s)
process 3: delayed A-Cast "tim" releases (tick 4)
...
Process 0: common subset (3/4 A-Casts):
  joe
  sally
  sam
...
All processes agree on subset: ok
Delayed process 3 (patience 0 sweeps, 200 ticks): EXCLUDED -- zero patience shut the door
step 2 fired 4 enter-0 act(s); the delayed value was accepted at every process -- participation loss, not value loss

$ ./example_bkr94acs -d 3 -g 1 4 1 joe sam sally tim
...
Process 0: common subset (4/4 A-Casts):
  joe
  sally
  sam
  tim
...
All processes agree on subset: ok
Delayed process 3 (patience 1 sweeps, 202 ticks): INCLUDED -- patience let step 1 win
step 2 fired 0 enter-0 act(s); the delayed value was accepted at every process
```

The `1` is a number for this demo only: the delayed A-Cast is released onto a lossless queue, so the patience here measures only how many sweeps pass before step 2 enables, never the rate at which the retry re-sends a delayed instance on a real transport (*Abandonment* below, and `BPR.md`, *The unit is local*).

Both examples run in one process over an in-memory lossless queue and end by quiescence, the ending in which nothing is owed (`BPR.md`, *Quiescence*); neither reaches abandonment, which only loss makes real. Each `-b` arm leaves a residue no annotation can retire and ends at the example's fixed cap, which is a harness guard and not an abandonment policy. Fig 3 and Fig 4 have no example of their own: a single-bit binary agreement carries nothing an application handed it, and what makes the pair useful is N of them composed, which is `bkr94acs`.

## What the caller provides

### Message System

The paper's proofs presume this (Section 1, the model; `Bracha87.txt`):

> "We assume a reliable message system in which no messages are lost or generated. Each process can directly send messages to any other process, and can identify the sender of every message it receives."

The library is a pure state machine with no I/O. Items 1-3 are the model's three obligations, item 1 in the fair-loss form BPR closes (row 5 above); item 4 is not the paper's but this library's, and falls on the same delivery path:

1. **Eventual delivery under fair loss.** Every message sent between correct processes must eventually arrive, but may be silently dropped any finite number of times in transit. BPR closes the gap from fair-loss datagrams to reliable delivery at the protocol endpoint (`BPR.md`, *The Gap*, *Placement*); the caller's retry tick drives it.

2. **No message fabrication.** The transport must not generate messages that were never sent. A Byzantine process may send arbitrary content, but the transport itself must not invent messages. In practice this means authenticated channels.

3. **Sender identification.** The receiver must know which process sent each message, and a Byzantine process must not be able to impersonate a correct one. In practice this means authentication bound to process identity.

4. **Delivery to self.** A broadcast must reach the process that sent it -- every Fig 1 message, A-Cast and BA alike. The state machines count a process's own echo and ready only when they arrive back through `bracha87Fig1Input` with `from == self`; at n = 3t+1 the echo threshold `(n+t)/2 + 1` equals the count of correct processes, so with t processes silent a process that omits itself stands one short of its own threshold forever and no ready is ever sent anywhere. This is a **local hand-back, not a packet**: a hairpin through the network is wasteful and, behind NAT, unreliable (`BRACHA87_SKIP_TST` in `bracha87.h`).

Nothing beyond the four obligations above is required. A complete deployment is that transport, a coin source, and an abandonment policy. The library is protocol-only and supplies none of them.

### Coin Choice

`bracha87Fig4Init` and `bkr94acsInit` take a `bracha87CoinFn` and closure; the caller supplies the coin and owns the consequences. The callback's contract -- an oracle that returns immediately, never fails, never blocks, named by `(instance, phase)` so the N concurrent BAs of one ACS draw distinct coins -- is at `bracha87CoinFn` in `bracha87.h`, together with why a construction that must exchange messages before it can answer does not fit behind it. The figure's own coin is local: step 3 case (iii) reads `coin_toss (0 or 1 with probability 1/2)`. Three corners the caller may occupy:

- **Global coin** -- Bracha's Section 7 term: "A coin toss such that, after the toss, all the processes are guaranteed to have the same value of the coin is known as a global coin toss." Section 7's expected-two-phases result is stated for Rabin's model, where a dealer secret-shares the sequence beforehand so that "the rth coin toss will be available to processes only in the rth phase." The callback fixes only that a phase's value is in hand when that phase's step-3 turn is made. A sequence every process holds in cleartext before the run is not Rabin's coin -- against a scheduler that knows it, it has the deterministic coin's weakness below -- and nothing in this tree hosts or measures a global coin of either kind.
- **Local coin** -- each process flips independently. The figure's coin, and the one Ben-Or's protocol underneath Fig 4 runs on. Its phase count is Theorem 3 (*Sizing instead of sharing* below).
- **Deterministic coin** -- `phase & 1`, which the bundled examples use for reproducible runs. It meets the global-coin definition (agreement is all it asks) and has zero entropy, so it is unsafe against an adaptive adversary: a scheduler that knows the coin can keep the correct processes from ever entering a phase agreed. Sound only where no such adversary exists, which is a demo and not a deployment.

### The phase budget

`maxPhases` is the caller's, passed to `bracha87Fig4Init` and `bkr94acsInit`, and choosing it is the coin choice priced in rounds. It is not `BRACHA87_MAX_PHASES` (85), which is only where the encoding stops; a budget outside 1..85 is refused at both ends, never clamped (`bracha87Fig4Sz`). What the budget must cover is set by the coin, because the coin is what ends a phase neither decision-count rule ended: a coin that hands correct processes different values drives tie after tie, one phase each. **Under-budget is unrecoverable**: the ceiling raises `BRACHA87_EXHAUSTED`, the BA will never decide, `COMPLETE` becomes unreachable, and the run can end only through the abandonment policy -- Lemma 2 Part C admits no unilateral substitute (Note 12). **Over-budget is paid at Init, not on the wire**: the Fig 1 instance space is O(N^2 x maxRounds) and each instance is O(N) bytes, so `bkr94acsSz` grows as N^3 x maxPhases -- 717,301,112 bytes at N = 100 and maxPhases 85, 11,518,855,944 at N = 256 -- allocated whether or not a phase is ever entered, because an undecided BA may need every round of it. On the wire a decided BA plays one phase past its decision and opens no other (post-decide continuation, Note 1), so a run's length and its BA traffic follow where the decisions land, not the budget (the bundled example decides in phase 0 and plays rounds 0..5 at `MAX_PHASES` 2, the least that holds a phase-0 decision). The budget is counted in the phases of the LAST correct decision: a phase in which some correct processes decide leaves the others adopting, deciding in the next, so a first decision in a BA's last phase leaves an adopter `BRACHA87_EXHAUSTED` beside a decider -- the same BA DECIDED at one process and exhausted at another, `COMPLETE` unreachable at the second. The bundled examples decide in phase 0 and never call the coin, so no run in this tree measures any coin's phase count; the multi-phase arms (`testFig4MultiPhase`) script the coin, and `test_bkr94acs_blackbox` Section R binds a per-process local coin and decides one BA in two waves -- whether a toss was reached is printed, never required (*Test coverage* says why nothing more is owed). Unlike the three quantities under *Abandonment*, this one is read off the coin, not the network.

### Sizing instead of sharing

Bracha defers his Theorem 3 to Ben-Or 1983 (`BenOr83.txt`), whose Theorem 3 reads: "If t = O(sqrt(N)) then the expected number of rounds to reach agreement in protocols A and B is constant, (i.e. does not depend on N)." (The extract notes the scan's "Ift" for "If t" and "VN" for sqrt(N); Ben-Or's introduction glosses the same result as constant expected time "when running the processes synchronously", a qualifier the theorem itself does not carry.) Bracha's version adds a second part, exponential in n at t = c*n, and the parenthesis that the sqrt constant "is exponential in c"; the extended abstract states its theorem without proof. Read as a sizing rule, take c = 1 -- that is `N >= t*t` -- and the local coin's expected phase count no longer grows with N, using the coin the figure already specifies and no shared randomness at all. What that constant is, neither paper says, and Bracha's one quantified per-phase figure, rho >= 2^{-(n-t)}, is not a constant-phase bound: sizing buys an N-independent expectation, not a known number of phases. (`N > 3t` is required regardless, and binds tighter below t = 4.) Bracha's Section 7 route, a dealer distributing the sequence beforehand, reaches an expected two phases, which sizing does not claim -- its constant is unknown, so the two are not ordered -- and he calls that model "not comparable to our protocol" rather than worse. Sizing costs processes: `N >= t*t` is 100 at t = 10, and an ACS run is N reliable broadcasts plus N binary agreements, where every round of every binary agreement is n Fig 1 broadcasts of O(n^2) messages each, over an instance space of O(N^2 x maxRounds) (`bkr94acs.h`, at `BKR94ACS_MAX_ACTS`). A deployment free to choose N reaches the constant with arithmetic; one that cannot chooses between a dealer and a phase count Theorem 3 bounds only as exponential in n at t = c*n. Neither paper makes that choice.

### Abandonment

Termination is an application choice; the library prescribes none. Why the protocol can give evidence of progress but never evidence of death or that stopping is safe, why the only sound policy shape is giving up -- **abandoning** -- after enough consecutive sweeps without progress, and why the gate counts sweeps and never wall time, is `BPR.md`'s (*Termination and Abandonment*; quiescence, the retry's own success-side ending, is its *Quiescence*). This section keeps what a deployment wires and sizes.

**The three quantities a deployment states.** Three numbers adapt the library to its network, and the library derives none of them: the information they need -- round trips, loss profile, how long to wait for an answer -- is the deployment's alone (`BPR.md`, *Placement*, applied one level further out). **None of the three can break agreement.** The tick adds no timing assumption to proofs made under arbitrary asynchrony; deferring an enabled firing at either duty seam costs liveness only (`BPR.md`, *The Sweep-Side Decisions*, and the one-sidedness of that license in *The Scoped-Claim Registry*); a gate sized too tight ends a run without a decision, never against one. What they buy and lose is participation and liveness.

- **The tick** -- one Retry call per process, of which a deployment states the wall-clock interval. A wire rate limit and nothing else: too fast offers the transport more than the retry exists to recover from (the network flood warning in `bracha87.h`); too slow stretches every sweep, and with it both quantities below, in proportion.
- **The patience** -- full cursor passes, read off the retry cursor's `sweeps` wrap count, spent at the two duty seams while their duty reads TOLERANCE. A pass re-sends every owed action of every sent instance once, so a patience is a count of re-sends, not of seconds. At the step-2 fanout it is spent once, and what it buys is fairness (whether a slow process's value makes the subset); at the BA round turn it is spent again on every round of every undecided BA, and what it buys is only a fuller sample (coin luck). Scope the turn's patience to undecided BAs (`bkr94acsBaDecision`): post-decide continuation rounds have nothing left to choose -- Lemma 9 fixes every sample they can meet -- and holding them to it convoys every other process's rounds. The pass is counted at the firing process's own rate, so a patience is not invariant under sustained rate skew (`BPR.md`, *The unit is local*).
- **The abandon gate, S** -- consecutive barren sweeps before the run gives up. Not picked independently: **S = 2 x patience**, a sizing `BPR.md` offers and does not prove (*The Abandon Boundary*; the derived-sizing entry of *The Scoped-Claim Registry*). Too small kills a legitimately late run; too large reports a run that can no longer complete later than it had to.

**One ordering constraint**: the patience must elapse strictly before the gate fires, or its decision lands in a caller that is already leaving. Sizing S above the patience holds that at the step-2 fanout, where the ordering is structural; at the BA round turn the same sizing is necessary and not sufficient -- that window opens with no act to count as progress -- so keeping the turn's patience under the gate stays the caller's obligation (`BPR.md`, *The Abandon Boundary*).

**Progress** is an Input call that returns actions (duplicate deliveries, every BPR retransmission included, return 0 -- a tested black-box contract), a `BKR94ACS_ACT_BA_DECIDED` / `BKR94ACS_ACT_COMPLETE`, or an application-level first-arrival the deployment chooses to count. Nothing else: not the retry egress, not the routine BA sends of post-decide continuation. A sweep -- one full pass of the Retry cursor, whose end is detected by comparing the cursor's `sweeps` count against a saved one, never by counting calls against `bkr94acsFig1SentCount` -- that ends with no progress is **barren**. Worst-case time to the gate is at most `S * bkr94acsFig1SentCount(a) * tick`; that is an upper bound because the count still includes instances whose retries have all retired, which a pass walks past without spending a call.

A run has one exit, abandon, and two markers on the way: `BKR94ACS_ACT_BA_EXHAUSTED` (that BA can issue no new phase, so `COMPLETE` is unreachable; the loop keeps draining and ticking, no substitute decision is permitted, and the gate surfaces it as the failure cause) and `BKR94ACS_ACT_COMPLETE` (the success marker; post-decide continuation requires broadcasting past it, so even a successful run leaves through the gate unless the retry first runs out of work, which is quiescence). A process that abandons without `COMPLETE` reports "gave up without a decision" with empty membership, never a substituted subset. The same policy serves the bare Fig 1 surface (`example/bracha87Fig1.c`): progress is an Input returning actions or ACCEPT, the sweep is one `bracha87Fig1RetryStep` pass, and Fig 1 has no EXHAUSTED -- reliable broadcast has no phase ceiling.

#### Scenarios

Every scenario resolves to the same gate; what differs is only how the local evidence stream looks. The treatments are `BPR.md`'s (*Termination and Abandonment*, *Quiescence*); the names are kept because the tests cite them as cross-reference labels:

- **Partition** -- indistinguishable from arbitrarily slow links; the side holding n-t correct processes completes, and a heal is carried by the survivors' never-retired READY alone.
- **Asymmetric flow** -- the two halves of one broken link correctly reach opposite outcomes; no protocol signal reconciles them.
- **Slow versus dead** -- indistinguishable in principle; counting sweeps makes the policy commensurate with the protocol.
- **Byzantine-silent processes** -- excluded at no cost but the retry tail toward them, which is correct: a silent process is indistinguishable from a laggard that still needs the traffic.
- **Byzantine trickle** -- fresh acts that lead nowhere stretch the gate; value-blind per-sender dedup over a finite instance space bounds the supply, so the stretch cannot hold it open.
- **Staggered start** -- a late process is byte-identical to a dead one until its first message, and the others' retries are the bootstrap it missed; only the gate can kill a legitimately late run.
- **After COMPLETE** -- success is not a stop; the two READY annotations give the retry tail a true end, and the residue they cannot end -- the never-announcer and its mirror, the re-arming forger -- is what the barren-sweep backstop is for.

## Bracha Phase Retry -- the application loop

Two operations: drain the network, tick the sweep. The sweep carries the BPR retry and the two caller-paced decisions that ride it, the BA round turns and the step-2 fanout. No application bookkeeping: the suppress masks and the accept evidence are library-owned protocol state the application honors, and the received message's discriminator byte is handed to `bkr94acs{Acast,Ba}Input` as `annot`.

```c
/* MAX_PROCESSES: the caller's compile-time bound on N -- C89 has no VLA.
 * The macro takes the encoded n (actual = n + 1), as the n parameter of every Sz and Init. */
struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES - 1)];
struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
struct bkr94acsAct acastAct;
struct bracha87Retry retry;
unsigned int lastSweeps = 0;
unsigned int patienceSpent = 0;
int sweepDone;

bracha87RetryInit(&retry);

bkr94acsAcast(a, my_value, &acastAct);       /* one ACAST_SEND/INITIAL; BPR carries it after */
broadcast_action(acastAct);

while (!terminate) {
  while (network_recv(&msg)) {               /* drain ingress: Input banks evidence */
    n = (msg.cls == BKR94ACS_CLS_ACAST)
      ? bkr94acsAcastInput(a, ..., msg.byte, ..., acts)   /* msg.byte is annot */
      : bkr94acsBaInput(a, ..., msg.byte, ..., acts);
    if (msg.type == BRACHA87_READY && !(msg.byte & BKR94ACS_RECEIVED))
      unpark(a);                             /* caller obligation at bkr94acsAcastInput */
    for (k = 0; k < n; ++k) broadcast_action(acts[k]);
  }

  n = bkr94acsRetryStep(a, &retry, out);     /* ONE call per tick (the flood warning) */
  for (k = 0; k < n; ++k) broadcast_action(out[k]);

  sweepDone = 0;                             /* a sweep closes on the wrap count: compare, never +1 */
  if (retry.sweeps != lastSweeps) {
    lastSweeps = retry.sweeps;
    sweepDone = 1;
  }

  /* BA round turns, zero patience shown: the turn is called on every attempt
   * and fires unless its duty is HELD.  A deployment counts sweeps per undecided
   * BA while bkr94acsTurnDuty reads TOLERANCE and calls only at MET or when its
   * patience lapses.  Drain, so a cascade's rounds are not metered out one per
   * tick. */
  for (p = 0; p < N; ++p)
    while ((n = bkr94acsTurn(a, p, acts)) > 0)
      for (k = 0; k < n; ++k) broadcast_action(acts[k]);

  /* Step-2 fanout: count sweeps while TOLERANCE; the count re-arms whenever the
   * duty leaves it.  bkr94acsFanout fires only at TOLERANCE, so the call
   * is safe. */
  if (bkr94acsFanoutDuty(a) != BKR94ACS_DUTY_TOLERANCE)
    patienceSpent = 0;
  else if (sweepDone)
    ++patienceSpent;
  if (patienceSpent >= PATIENCE) {
    n = bkr94acsFanout(a, acts);
    for (k = 0; k < n; ++k) broadcast_action(acts[k]);
  }

  usleep(tickMs * 1000);                     /* wire rate limit, NOT a correctness clock */
}
```

`broadcast_action(act)` switches on `act.act` and sends the described Fig 1 message per recipient, in the packed layout at the message-class defines in `bkr94acs.h` (field usage per act is at `struct bkr94acsAct`): skip every process in `act.skip` (`BRACHA87_SKIP_TST`); on a READY set the wire ACCEPTED bit from `act.accepted`, one fact about the sender, and set RECEIVED for recipient `p` only where `act.received` carries p's bit. That last bit is why a READY egress is n addressed messages and not one frame; marking a process outside the mask silences the re-sends it is waiting for, and marking none leaves every READY arriving unmarked so the retire never converges (`BPR.md`, *Suppression and the Announcements*). Delivery to self rides the same path as a local hand-back. `BA_DECIDED` and `COMPLETE` carry no wire output and are not stops; `terminate` is the abandonment policy above.

## Why these papers, and what is absent by design

**Bracha 1987** is reliable broadcast at n > 3t over nothing more than authenticated point-to-point channels, and a module boundary the proofs respect: Lemmas 1-4 and Theorems 1 and 5 apply to Fig 1, Lemmas 5-7 to Fig 2/3, Lemmas 8-10 and Theorems 2-3 to Fig 4. That per-figure boundary is what makes the audit chain below possible.

**BKR94 stops at ACS.** Section 4's Agreement[Q] reaches a common subset with no setup and no distinguished process. The paper continues to asynchronous secure computation; that has no caller here, and its model adds secure channels ("cannot be heard by other players", Section 2) that ACS does not need and cannot bootstrap. Its secret-sharing side carries the non-termination probability the paper itself contrasts "with the asynchronous Byzantine Agreement problem where the randomized protocol terminates with probability 1" (`BKR94ACS.txt`); ACS inherits the latter.

**Saltzer/Reed/Clark 1984** decides where a correctness function lives; that the retry is one, and the two boundary facts of the citation, are `BPR.md`'s (*Placement*).

**FLP82** and **Ben-Or 1983** are not implemented: the first is the impossibility Fig 4 answers, against which every sentence here written in FLP's name is checked; the second is the protocol underneath Fig 4 and the paper Theorem 3 defers to.

Absent by design: no bundled coin, no timing assumption, no distinguished process and no machinery to replace one, no dynamic allocation, no I/O, no threads, and no provisioning beyond one authentication credential per process pair.

## The audit chain

Four links from paper rule to running code:

```
paper rules            <-> .dtc files                human, rule-by-rule comments
.dtc files              -> compiled dispatch         dtc, exhaustive/exclusive
C wrapper boundary I/O                               human inspection
fig3IsValid, fig4Nfn                                 test/test_predicates.c --
                                                     exhaustive enumeration vs
                                                     subset-enumeration
                                                     reference at n=4, t=1
Fig 3 cascade                                        test/test_predicates.c --
                                                     4 sampled delivery
                                                     permutations vs the same
                                                     reference
one dispatch, many sites                             test/test_predicates.c --
                                                     every input combination of
                                                     each snippet; each site's
                                                     reads constant over what
                                                     it feeds by fiat
```

Each module's per-call decision logic is a decision table in the paper's vocabulary, commented rule-by-rule with the paper's rule numbers (`bracha87Fig{1,3,4}.dtc`, `bkr94acs.dtc`, with `BPR.md`'s rules as sub-tables of the first and last); a bridge per module (`*ToC.dtc`) maps names and values to C identifiers. `../decisionTableCompiler/dtc` co-compiles each pair to a depth-minimal dispatch, proving exhaustiveness and exclusivity of the rules as it does, and `psu.awk` translates that to the C snippet the entry point `#include`s. The C wrapper computes the boundary inputs and applies the boolean outputs as side effects in the order that is the API contract (Fig 1: echo before ready before accept). The chain establishes that the code implements the papers' rules; the rules' correctness at general (n, t) is the papers' claim.

| Table | Bridge | Snippet | Entry point | Depth |
|---|---|---|---|---|
| `bracha87Fig1.dtc` | `bracha87Fig1ToC.dtc` | `bracha87Fig1Rules.c` | `bracha87Fig1Input` and `bracha87Fig1Bpr` (one snippet, two `#include`s; the six paper rules and BPR's five as sub-tables, chained) | 13 |
| `bracha87Fig2.dtc` | (none -- Fig 3 subsumes) | -- | -- | -- |
| `bracha87Fig3.dtc` | `bracha87Fig3ToC.dtc` | `bracha87Fig3Rules.c` | `bracha87Fig3Accept` | 4 |
| `bracha87Fig4.dtc` | `bracha87Fig4ToC.dtc` | `bracha87Fig4Rules.c` | `bracha87Fig4Round` | 6 |
| `bkr94acs.dtc` | `bkr94acsToC.dtc` | `bkr94acsRules.c` | `bkr94acsAcastInput`, `bkr94acsTurn`, the retry's verdict gate and the duty site behind `bkr94acsFanoutDuty` / `bkr94acsFanout` / `bkr94acsTurnDuty` / `bkr94acsTurn` (one snippet, four `#include`s; the three ACS steps and BPR's two sub-tables) | 11 |

The depth is the longest chain of tests any input walks through the generated dispatch, recorded in each snippet's header; `dtc` searches for the minimum. `dtc`'s search grows steeply with the table, which is why `make rules` is its own target with one per table; these four regenerate together in a minute -- the Fig 1 table, at twelve inputs, is where `dtc`'s search spends it (`dtc -q` for a quick, non-minimal dispatch while editing). The snippets are committed so that a clone builds without `dtc`, which lives in a second repository; `make rules && git diff --exit-code -- '*Rules.c'` is the check that no `.dtc` edit went unbuilt. BPR's rules are sub-tables of the paper tables they belong beside, not tables of their own: in `bracha87Fig1.dtc` the three retires and the two annotation rules read the paper outputs (`accept(v)`, the send rules) and the paper inputs beside their own, and `dtc` compiles the chain -- which is how the accepting message's own arm lands without a caller ordering anything; in `bkr94acs.dtc` the retry's verdict gate and the sweep-side duty trichotomy sit beside the three ACS steps, one dispatch reached from every site. What stays in C rather than in a dispatch -- the fanout loop, the round computation, the suppress masks, the predicates the rows read -- is recorded in the `.dtc` text sections.

Two algorithmic predicates sit below the dispatch, `fig3IsValid` (the recursive existential) and `fig4Nfn` (case analysis with the permissive D_FLAG encoding), plus the Fig 3 cascade. `test/test_predicates.c` anchors them against a subset-enumeration reference at n=4 t=1: 960 `fig4Nfn` inputs, 165 `fig3IsValid` evaluations, 4 cascade delivery permutations, all agreeing. The same binary enumerates every input combination of the two multi-site snippets (6,144 for Fig 1, 576 for the ACS) and requires, for each of the six C sites (Input once whole and once per kind of message, the turn once per BA output), that the outputs the site reads do not move when the inputs it feeds by fiat do -- which is what lets one dispatch serve sites that discard each other's outputs. It is a fact of the rows, not of the tables' shape (the echo and ready retries chain on send rules that read the kind of message Bpr fixes), which is why it is enumerated rather than argued. `fig3IsValid` is paper-correct given an N that exposes the existential through its permissive return; `fig4Nfn` is that N for Fig 4, so the two verify each other transitively.

## Test coverage

Everything under `test/` validates the implementation against the papers, never the papers: that the code does what the figures say is the claim tested here; that the figures achieve agreement and terminate with probability 1 is theirs, taken as given. So no arm drives Fig 4 through coin phases under an adversarial schedule, and none is owed.

`make check` runs eight binaries:

| Binary | Scope | What it catches |
|---|---|---|
| `test_predicates` | Algorithmic primitives and the dispatches (white-box) | `fig4Nfn`, `fig3IsValid` and the Fig 3 cascade against the subset-enumeration reference above; the site independence of the two multi-site dispatch snippets |
| `test_bracha87` | Protocol white-box (bracha87) | Per-rule units, composed simulation with inline lemma/theorem checks, equivocation, post-decide continuation and its bound, BPR retirement invariants, both-sided thresholds; reads internal flags |
| `test_bracha87_blackbox` | Protocol black-box (bracha87) | Validity/agreement/totality, precise echo thresholds, delivery to self withheld and honored, the BPR retirement contract, array Retry, the cursor's `sweeps` count (with a witness that one call can complete two passes) -- derived from `bracha87.h` and `Bracha87.txt` only |
| `test_bkr94acs` | Protocol white-box (bkr94acs) | All-to-all simulation, the step-2 trigger and floor, post-decide continuation and its bound, BPR drop-convergence, the Byzantine-silent canary, EXHAUSTED handling, the forged INITIAL; reaches into internal layout |
| `test_bkr94acs_blackbox` | Protocol black-box (bkr94acs) | Sections A-R: Lemma 2 Parts A-D, Input dedup, Retry and quiescence under drop, EXHAUSTED, the equivocating A-Caster, step-2 and round-turn pacing, the *Abandonment* scenarios (Sections I-N), a BA's decision versus this process's input, annotation forgery, the paired payload's retire and hold, and (Section R) a decision in two waves through the composition, each process on its own local coin: a first wave of two, and a lone first-wave decider whose next rounds the second wave completes -- no `.c` reads |
| `test_schedules` | Schedule explorer (instrument) | Bounded reachability over the two example loops' state graphs under adversarial delivery order and delay -- and, on its adversary configs, under every well-formed Byzantine content inside a printed bound (strategies b1-b4) -- with a per-transition oracle and a quiescent-terminal battery; frozen counts are regression constants. `make check` runs its smoke subset; `make schedules` the full run, `make strategies` the adversary configs |
| `test_ingress` | Ingress contract (instrument) | Hostile bytes at every entry a message reaches, from an adversary who holds this library: each wire-derived argument swept over its whole field, the boundary as a cross product, all 256 packed discriminator bytes. Asserts an argument the headers do not admit is refused and leaves the receiver byte-identical |
| `test_ceiling` | The 256-process ceiling (black-box) | One instance of each figure and one ACS instance at n=256, t=85: every count that can reach the process count driven to 256 and what reads it required to hold there, the ACS instance's 256 BAs reaching decision through `bkr94acsBaInput` and `bkr94acsTurn` alone (256 BAs, 3 rounds, 256 initiators, 2t+1 READYs each); the rest of the battery runs at 37 processes and below, where a count narrowed to a byte is invisible |

White-box arms witness internal invariants; black-box arms derive every case from the headers and extracts alone, so header text and code behavior cannot drift apart silently. `test/mutants.sh` (`make mutants`) applies anchored single defects and grades each against one named check (`BPR.md`, *The Review of Record*).

## Implementation Notes

**These numbers are cited by code comments, the `.dtc` text sections, the examples and the tests, so they never change.** Add new entries at the end.

Each is a paper-vs-code divergence a from-scratch implementation will meet, almost all of them visible only under multi-figure interaction or under loss that simulated reliable channels never produce.

1. **Post-decide continuation, and its bound.** Fig 4 says "Go to round 1 of phase i+1" after all three step-3 cases, so the figure never halts, and Theorem 2 is proved for that figure: Lemma 8's no-deadlock proof takes every correct process to have broadcast at the first blocked round. A process that stops sending at its decision is, to every other process, a faulty transmitter -- in the model there is no way to distinguish between a "slow" message and a message not sent (Section 1) -- and the paper's only word on halting is one unproved clause, "this can be easily accomplished". Theorem 2's Agreement supplies the bound: the first correct decision, at phase r on 2t+1 (d, v), leaves at least t+1 of them in every correct step-3 sample of phase r (a sample omits at most t senders), so every correct process sets v there, and by Lemma 9 all decide at phase r+1 -- which needs every correct process broadcasting that phase's three rounds, and nothing after it. So a decided process broadcasts the phase after its decision and opens no other; `BRACHA87_DECIDE` is returned once, with `BRACHA87_BROADCAST` whenever a next round exists, alone on the last phase, so callers test the bit, and the step-3 turn of the phase after the decision returns 0 (the argument in full is at `bracha87Fig4Round`). A process that first decides in that phase opens one more and can be left short of n-t there for good -- a decided BA whose turn duty reads HELD, which costs it nothing. Its Fig 1 duties are not bounded: it echoes and readies every instance that reaches it, and BPR retries what it sent, since Lemma 4's echo count at n = 3t+1 needs every correct process -- witnessed by the loss arms (Sections F and H), where a retry withheld after a decision strands a peer, not by Section R, which runs lossless. Regression: `testFig4PostDecide`, `testPostDecideContinuation`, `testPostDecideMultiPhase`, `testPostDecideBound`, `test_bracha87_blackbox` "two-wave arm", `test_bkr94acs_blackbox` Section R; mutants M84, M85, M86 and M87.

2. **D_FLAG leak.** Case (i) is two assignments, `decision_p := value_p := v`, and only the first is once-only. An implementation that gates the whole case on "not yet decided" leaves the value where step 2 put it, `(d, v)`, and the flag rides into the next phase's step-1 broadcast -- which no correct process sends (every step-3 case writes a bare value) and VALID therefore rejects. The dispatch gates `decide v` alone; `adopt v` (the `value_p := v` half, shared with case (ii)) fires every phase. Under the continuation's bound (Note 1) the one decided step-3 turn is the last and opens no phase, so the leak no longer reaches the wire; the rule stays the figure's reading. Regression: `testPostDecideMultiPhase`; mutant M42 gates the value half on the decided state against its state check.

3. **N's existential quantifier.** VALID^k reads "there exist n - t messages"; passing only the first n-t to N rejects messages a correct process produced from a different subset. N receives every validated message and returns permissive when subsets could disagree. Regression: `test_predicates`.

4. **No cascade after INITIAL.** An INITIAL arrival evaluates no threshold: if any were met, `echoed` would already be set by Rule 2 or 3. The Fig 1 input's INITIAL branch computes no counts. No regression arm; the property is the dispatch's.

5. **Echoed value memcpy.** The value copy on Rules 4/5/6 is essential, not redundant: a Byzantine initial can store the wrong value first, and the copy corrects it when the threshold-reaching value differs. Regression: `testFig1ValueSwitch`.

6. **Subset-majority reachability (step 1).** Under N's tie-break-to-0, value 0 is reachable in some n-t subset iff `cnt[0] >= (nt+1)/2` (equals `nt/2` for even n-t, `nt/2+1` for odd); value 1 iff `cnt[1] >= nt/2+1`. Permissive iff both. The symmetric `>= nt/2+1` test on both sides wrongly rejects honest tie-subset 0s when n-t is even. Regression: `testFig4SubsetMajority`, `testFig4SubsetMajorityBoundary`, `test_predicates`.

7. **The forward cascade fires on every growth past n-t.** VALID^r is existential over n-t subsets of VALID^{r-1} and monotone in it by the definition, so new validations at round k unlock stored messages at k+1 after k first reached n-t. Gating the re-check on the first crossing strands honest round-(k+1) messages. Regression: `testFig3RecascadeOnGrowth`.

8. **Permissive D_FLAG permission via `*result`.** On a permissive return, `*result & BRACHA87_D_FLAG` is set only when some n-t subset legitimately produces a decision candidate; Fig 3 rejects an incoming D_FLAG when that bit is clear, closing Byzantine d-injection in the no-majority windows of steps 2 and 3. Regression: `testFig4DflagInjection`.

9. **The decided state gates one output and the bound, never a value update.** Figure 4 has no decided state: every case ends "Go to round 1 of phase i+1", so a decided process sets the majority at step 1 and `(d, v)` at step 2 of the phase after its decision exactly as an undecided one does, and what holds its value at the decision is Lemma 9 (after a decision every correct process opens the next phase with v), not a rule. Guarding the value updates on "have decided" to "preserve the decision" reads as safety and is a liveness defect: the decided process then broadcasts a bare v at round 3i+3, and Figure 3 at every peer rejects it, because more than n/2 of the round-(3i+2) messages agree, so N demands `(d, v)`. Theorem 2's Agreement proof consumes exactly the message the guard withholds -- an undecided q that adopted v at case (ii) decides at phase r+1 on 2t+1 `(d, v)`, the decided processes' among them -- and with the faulty silent and fewer than n-t undecided correct processes, q's round never completes. The visible symptom, even when every correct process decided in the same phase, is a continuation that dies one round into the next phase: the decided processes' step-3 broadcasts validate nowhere. Regression: `test_bracha87_blackbox` "figure-unchanged arm", which plays a decided process through the phase after its decision and has a peer's Fig 3 validate its round-5 `(d, v)`; mutant M21 restores the step-2 guard against that label. Step 1 and the coin cannot be witnessed that way -- on every sample the model presents after a decision the majority is the decision and case (iii) is unreachable -- so `testFig4PostDecideUngated` feeds a sample Lemma 9 excludes and requires the figure's answer; mutants M62 and M63 guard those two rules against it. The step-1 rule's value goes out in the phase after the decision; the coin's, at that phase's step 3, is the bound turn's and is never sent, so M63 is caught on the machine's state alone.

10. **BPR (ready, v) retry must NOT short-circuit on accepted.** An accepted process owes its READY to processes still below 2t+1 -- READY is the amplification carrier -- and the asymmetry with Note 11 is that ACCEPTED retires the bootstrap-only INITIAL and ECHO but never READY (`BPR.md`, *Retirement*). Regression: `testFig1Bpr` post-accept assertions (READY survives; INITIAL/ECHO retired).

11. **BPR (initial, v) / (echo, v) retire only on a stop strictly stronger than local echo.** ACCEPTED (retires both) and all-echoed (retires INITIAL) are the sound stops; stopping INITIAL once locally echoed strands a process that missed the bootstrap at n = 3t+1, where the echo threshold equals the honest count (`BPR.md`, *Retirement*). Regression: `testBprByzantineSilent` (n=4 t=1, one silent process: 3 honest converge, |SubSet| = 3, in 1 sweep) and `testFig1Bpr`'s all-echoed assertions.

12. **Fig 4 EXHAUSTED means no new phase; no unilateral substitute at the BKR94 layer.** When `bracha87Fig4Round` returns `BRACHA87_EXHAUSTED` (the caller's `maxPhases` spent without a decision), Lemma 2 Part B (all n BAs terminate) is violated for that instance and Part C is unrecoverable locally: any substitute could disagree with another process's actual decision. The library surfaces `BKR94ACS_ACT_BA_EXHAUSTED`, `bkr94acsBaDecision` answers 0xFE thereafter, and the sentinel never counts as decided, so `complete` stays clear; the application exits through its abandonment policy. BPR keeps retrying that process's traffic for the others' sake. Regression: `testExhausted`, `testExhaustedAmongDecided`.

13. **READY's only sound retire is remote, and it takes TWO facts -- never local accept** (the per-process refinement of Note 10). "q has accepted" and "q has received MY accept" are different facts; suppressing on the first alone strands q's own gate one bit short for good, so the second has its own wire bit, `BKR94ACS_RECEIVED`, whose absence re-arms the re-send toward its sender, read off the `annot` argument of `bkr94acs{Acast,Ba}Input`; no `>= 2t+1 accepted -> stop` shortcut is admissible (`BPR.md`, *Suppression and the Announcements*, including the Byzantine containment of both annotations). Regression: `testFig1SkipAccept`, `testBprSkipAccept`, `runWithRetry` drop-convergence with suppression active; the containment half is Section P.

14. **INITIAL must come from the designated initiator** -- `from == process` (A-Cast) / `from == initiator` (BA) is enforced, not assumed. A non-initiator INITIAL is a forged broadcast: Rule 1 echoes the first INITIAL unconditionally, so an attacker reaching every correct process drives the `(n+t)/2+1` cascade to a false ACCEPT. Authenticated channels bind `from` to the true sender but not to the message's claimed initiator (initiator != from is a valid ECHO/READY), so the binding is a protocol-semantic check. `bkr94acsAcastInput` / `bkr94acsBaInput` drop the message; the bare `bracha87Fig1Input` is not told its initiator, so a bare-layer caller filters first (`example/bracha87Fig1.c` does, in its delivery loop). Honest-only generators never exercise this path -- even an equivocating initiator has `from == process`. Regression: `testForgedInitial`; Section A5.

15. **Step 2's trigger is n-t BA outputs of 1, where the paper writes 2t+1.** `bkr94acsFanoutDuty` compares against n-t; the two are equal only at n = 3t+1, and at every larger n the paper's trigger would fire earlier, so this library fires strictly later on more evidence. Lemma 2 goes through either way -- Part A case (i) needs the precondition to imply 2t+1 outputs, and n-t >= 2t+1 throughout the supported range -- and the paper states the size two ways -- "at least n - t >= 2t + 1" in the Section 4 opener, "at least 2t + 1" in Lemma 2 -- of which only n-t attains the former above n = 3t+1. No arm at n = 3t+1 can see it, and in a lossless all-honest run every BA is entered by step 1 before any decides, so the duty answers MET without reaching the comparison. Regression: `testFanoutFloorAboveEdge` (n=5 t=1: three decided-1 reads HELD, four reads TOLERANCE; n=8 t=2: five and six); mutant M35 lowers the floor to 2t+1 against that label.

## License

LGPL v3 or later. See `COPYING.LESSER` and `COPYING`.
