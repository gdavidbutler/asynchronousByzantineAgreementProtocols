# asynchronousByzantineAgreementProtocols

Generated with Claude Code (https://claude.ai/code)

A C library implementing Gabriel Bracha's 1987 paper (Figures 1, 3, and 4 as composable pure state machines; Figure 2 captured for paper completeness and subsumed by Figure 3), and the BKR94 Asynchronous Common Subset (ACS) protocol built from them.

## Overview

An implementation of Bracha 1987 as composable pure state machines, with module boundaries that match the paper's figures. ANSI C89, zero dependencies, up to 256 processes. No I/O, no threads, no dynamic allocation -- the caller provides memory and executes output actions.

Each module boundary matches the paper exactly, so the paper's proofs apply per-module: Lemmas 1-4 and Theorems 1 and 4-5 to Fig 1 (Theorem 5 names Fig 1 the weak-termination Byzantine Generals protocol: a faulty initiator's broadcast may never accept, and no correct process can tell), Lemmas 5-7 to Fig 2/3, Lemmas 8-10 and Theorems 2-3 to Fig 4.

The `bkr94acs` module composes these figures into multi-value agreement: N processes A-Cast arbitrary values, and all honest processes agree on the same common subset of at least n-t A-Casts. This is Ben-Or/Kelmer/Rabin 1994 Section 4 Figure 3 (Protocol Agreement[Q]).

The API reference is the headers themselves: `bracha87.h` and `bkr94acs.h` carry the per-function contracts, and this README does not restate them. The machinery beneath the papers' reliable-channel assumption -- the retry, its retire gates, the sweep-side pacing, and the abandonment model -- is governed by `BPR.md`, this repository's own statement standing in for the paper that does not exist, and this README does not restate it either. **If you are integrating the library**, the load-bearing sections are *When to Use What*, *Message System*, *Bracha Phase Retry*, *Examples*, *Coin Choice*, and *Abandonment*. **If you are auditing the implementation or porting it to another language**, additionally read *Design Rationale*, *Architecture*, *Test Coverage*, *Correctness Audit*, *Implementation Notes*, and *Re-Implementing in Another Language*. **If the subject is unfamiliar**, *The Papers* names the sources and their in-tree extracts.

## When to Use What

This library provides two application-facing agreement primitives. Pick by the shape of your problem.

**Reliable broadcast -- `bracha87Fig1`.** One designated sender announces a value; all correct processes either accept the same value or none do, under up to `t` Byzantine faults at `n > 3t`. Use when you have a known initiator per message: configuration distribution from a designated source, single-writer state replication, one-shot dissemination of a signed announcement, or as a reliable-channel building block inside your own outer protocol. Application surface: `bracha87Fig1Input` per delivered message, `bracha87Fig1RetryStep` over the caller-owned instance array per retry tick. See `example/bracha87Fig1.c`.

**Common subset -- `bkr94acs`.** N processes each A-Cast a value; all correct processes agree on the same common subset of at least `n-t` A-Casts. Use when you need agreement on a batch of contributions among symmetric processes: MPC input bundling, distributed candidate selection -- anything shaped as "agree on the set" rather than "agree on a single value." Application surface: the loop under *Bracha Phase Retry* below. See `example/bkr94acs.c`.

Fig 3 (VALID-set framework) and Fig 4 (binary Byzantine agreement) are exposed for completeness but exist primarily as internal mechanism feeding `bkr94acs`; raw single-bit binary BA has no realistic standalone caller (the seven-year gap between Bracha 1987 and BKR94 1994 is exactly that evidence).

## Message System -- What the Caller Must Provide

The paper's proofs depend on three assumptions about the message system (Section 1, the model; extracted in `Bracha87.txt`):

> "We assume a reliable message system in which no messages are lost or generated. Each process can directly send messages to any other process, and can identify the sender of every message it receives."

These assumptions are not optional -- they are load-bearing requirements of every lemma and theorem in the paper. This library is a pure state machine with no I/O. **The caller is responsible for building a transport layer that satisfies them:**

1. **Eventual delivery under fair-loss.** Every message sent between correct processes must eventually arrive -- but messages may be silently dropped any finite number of times in transit. The Bracha Phase Retry (BPR -- governed by `BPR.md`; the loop below) is offered to close the gap from "fair-loss point-to-point" to "reliable delivery" at the protocol endpoint, so the transport need not provide retransmission of its own; the caller's retry tick is what drives BPR's retries.

2. **No message fabrication.** The transport must not generate messages that were never sent. A Byzantine process may send arbitrary content, but the transport itself must not invent messages. In practice this means authenticated channels.

3. **Sender identification.** The receiver must know which process sent each message, and a Byzantine process must not be able to impersonate a correct one. In practice this means authentication bound to process identity.

The protocol's correctness (both safety and termination) does not depend on any timing assumption -- that is the asynchronous-BFT model. Retry cadence and abandonment thresholds are deployment tuning (see *Abandonment*), not protocol invariants.

Nothing beyond these three assumptions is required. Authenticated point-to-point channels (assumption 3 above) are the entire setup -- provisioned by whatever mechanism the deployment prefers (HMAC over a pre-shared key, TLS, mutual SSH, Noise, etc.); the cost is one symmetric authentication credential per process pair. Nothing else is provisioned at this layer by design (see *What we deliberately did not build*, Design Rationale).

A complete deployment is therefore: this transport, the identity and keys that authenticate it (assumption 3), a coin source (see *Coin Choice -- Caller Responsibility*), and an abandonment policy (see *Abandonment*). The library is protocol-only and supplies none of them.

## Bracha Phase Retry (BPR)

Bracha's correctness proofs presume reliable point-to-point channels between correct processes; over fair-loss datagrams that assumption is not satisfied, and **BPR is offered as a possible closure of the gap, at the protocol endpoint**. `BPR.md` is BPR's governing statement -- the model and the claim posture, the end-to-end placement argument, the retry rules and each retire gate's argument, per-process suppression and the two READY annotations, quiescence, the sweep-side pacing of the step-2 fanout and the BA round turn, and the termination model -- and its *Realization in This Library* section maps each concept to the entry points whose headers carry the contracts. What this README adds is the integrator's deliverable: the loop below.

### Application loop

With BPR, the application loop is two operations: drain the network and tick the sweep -- the BPR retry plus the sweep-side protocol decisions that ride it (the BA round turns and the BKR94 step-2 fanout, both caller-paced). No *application* bookkeeping -- no per-instance destination mask, no per-process receipt tracking. The per-process suppress mask the broadcast consults (and the accept evidence behind the READY mask) is library-owned, intrinsic protocol state surfaced through `bracha87Fig1Skip` / the `.skip` field; the application just honors it (`BRACHA87_SKIP_TST`) and feeds the two decoded READY annotations back -- `BKR94ACS_ACCEPTED` via `bkr94acs{Acast,Ba}Accepted`, and the ABSENCE of `BKR94ACS_RECEIVED` via `bkr94acs{Acast,Ba}Resend` -- it maintains none of it.

```c
struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(N, MAX_PHASES)]; /* Input out[]: the larger bound */
struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];           /* Retry out[] */
struct bkr94acsAct acastAct;
struct bracha87Retry retry;
unsigned int retriesThisSweep = 0;
unsigned int patienceSpent = 0;
int sweepDone;

bracha87RetryInit(&retry);

/* Self-initiation: mark the local A-Cast Fig 1 as initiator and output
 * one ACAST_SEND action (.type = BRACHA87_INITIAL) for the application
 * to broadcast.  The act's .value points into library storage. */
bkr94acsAcast(a, my_value, &acastAct);
broadcast_action(acastAct);

while (!terminate) {
  /* Drain ingress: Input handles paper rules + cascades. */
  while (network_recv(&msg)) {
    n = (msg.cls == BKR94ACS_CLS_ACAST)
      ? bkr94acsAcastInput(a, ..., acts)
      : bkr94acsBaInput(a, ..., acts);
    if (msg.accepted)         /* BKR94ACS_ACCEPTED wire bit on a READY:
                               * route AFTER the matching Input, so
                               * acFrom stays a subset of rdFrom */
      (msg.cls == BKR94ACS_CLS_ACAST)
        ? bkr94acsAcastAccepted(a, ...)
        : bkr94acsBaAccepted(a, ...);
    if (msg.type == BRACHA87_READY && !msg.received)
      /* The ABSENCE of BKR94ACS_RECEIVED is the sender saying it does
       * not hold OUR accept -- it would have suppressed us otherwise.
       * A caller that parks quiescent instances re-enters them here. */
      (msg.cls == BKR94ACS_CLS_ACAST)
        ? bkr94acsAcastResend(a, ...)
        : bkr94acsBaResend(a, ...);
    for (k = 0; k < n; ++k) broadcast_action(acts[k]);
  }

  /* Retry tick: BPR retries sent actions.  ONE call per tick
   * -- see the network flood warning in bracha87.h. */
  n = bkr94acsRetry(a, &retry, out);
  for (k = 0; k < n; ++k) broadcast_action(out[k]);

  /* A SWEEP is one full pass of that cursor -- every sent Fig 1
   * re-sent once -- and it is the unit the patience
   * below and the barren-sweep abandon gate both count in, never
   * the tick.  Recompute the length each pass: it grows as the BAs
   * advance, so a budget priced in ticks would shrink in real terms
   * over the run. */
  sweepDone = 0;
  if (++retriesThisSweep >= bkr94acsSentFig1Count(a)) {
    retriesThisSweep = 0;
    sweepDone = 1;
  }

  /* BA round turns, paced here: "wait until validate n-t
   * k-messages" is enabling evidence over a still-growing sample,
   * so the Input calls above only BANK evidence and each BA's next
   * round is computed from the sweep.  Zero patience shown -- the
   * literal 1 passes the verdict on every attempt, which is what
   * makes it eager; a deployment counts sweeps per BA while
   * bkr94acsTurnDuty holds TOLERANCE and passes the elapsed signal
   * only when its patience lapses -- a fuller sample can only turn
   * coin phases into deterministic decides.  Drain, so a cascade's
   * already-earned rounds are not metered out one per tick. */
  for (p = 0; p < N; ++p)
    while ((n = bkr94acsTurn(a, p, 1, acts)) > 0)
      for (k = 0; k < n; ++k) broadcast_action(acts[k]);

  /* BKR94 step 2, paced the same way: the n-t count is enabling
   * evidence, not a moment.  Count sweeps while the fanout is
   * enabled and fire when patience elapses.  The count
   * is a clock, not a latch: it re-arms whenever the duty leaves
   * TOLERANCE.  Advancing it only at a boundary costs one boundary
   * of lag, so even PATIENCE 0 here is a boundary later
   * than the turn loop's literal 1 above.  While patience is consumed, the
   * retry is still re-carrying the delayed A-Casts -- the wait is
   * spent on the recovery that can make the firing unnecessary. */
  if (bkr94acsFanoutDuty(a) != BKR94ACS_DUTY_TOLERANCE)
    patienceSpent = 0;
  else if (sweepDone)
    ++patienceSpent;
  if (patienceSpent > PATIENCE) {
    n = bkr94acsFanout(a, acts);   /* guarded: 0 outside TOLERANCE */
    for (k = 0; k < n; ++k) broadcast_action(acts[k]);
  }

  usleep(tickMs * 1000);   /* wire rate limit, NOT a correctness clock */
}
```

`broadcast_action(act)` switches on `act.act` and broadcasts the described Fig 1 message -- field usage per act is documented at `struct bkr94acsAct` in `bkr94acs.h`. `BA_DECIDED` and `COMPLETE` are success signals, not stops, and carry no wire output; `BA_EXHAUSTED` reports a BA that can issue no new phase/round, making `COMPLETE` unreachable (see *Abandonment* below).

`terminate` is the application's abandonment policy -- an application choice, not a library-prescribed one. See *Abandonment* below.

## Building

```bash
make            # build .o and examples
make check      # build and run all six test binaries (see Test Coverage below)
make clean      # remove build artifacts
make clobber    # also remove dtc's leftover .psu intermediates
```

A C89 compiler is the only requirement. The rule dispatch is generated (see *Paper-Faithful Dispatch via DTC* below), but the generated `*Rules.c` snippets are committed and treated as source here: nothing in the ordinary build runs `dtc`, and neither `clean` nor `clobber` removes them.

The reason is what generation costs. `dtc` performs a full search for a depth-minimal dispatch, and the search grows steeply with the table -- a cost the person who edits a `.dtc` should pay once, not a toll on everyone who builds. Committing the snippets also makes the repository self-contained -- `dtc` lives in a second repository, so a build that invoked it could not be run from a clone of this one alone.

Editing a `.dtc` therefore means regenerating deliberately, and the per-table targets exist because of that same asymmetry:

```bash
make rules              # re-run dtc + psu.awk for all four tables
make rules-bkr94acs     # or just the one whose .dtc changed
```

`make rules && git diff --exit-code` is the check that no `.dtc` edit went unbuilt -- `dtc` is deterministic, so a nonempty diff means a real one. That path needs `../decisionTableCompiler/dtc` and `awk`; the regenerated snippet is reviewed as a diff and committed alongside the `.dtc` that caused it. The `.psu` is dtc's intermediate -- `psu.awk` is its only consumer -- and stays untracked.

Compiler flags: `-std=c89 -pedantic -Wall -Wextra -Os -g`

## Examples

Two runnable examples sit in `example/`, one per application-facing API surface. Both run in a single process over an in-memory lossless queue, and they end differently on purpose. `example_bracha87Fig1` ends by **quiescence** -- the owing ending (BPR.md *Quiescence*): every process announces its own accept on the READY it is already retrying and confirms the accepts it has recorded, each instance's evidence reaches all n on both counts, READY retires with it, and a full `bracha87Fig1RetryStep` pass owes nothing, so every process leaves the sweep rotation and the wire falls silent. `example_bkr94acs` runs the full application loop -- drain ingress, then a sweep carrying the BPR retry and the paced turn/fanout decisions -- and now ends the same way, per process, on a `bkr94acsRetry` pass that owes nothing; completion is an assertion the results section checks rather than the gate. Neither demo reaches the give-up half of abandonment, which only loss makes real. The nearest either comes to it is `-b split`: a Byzantine initiator runs no state machine, so it never announces an accept, the evidence can never complete, and what ends the run is the first example's sweep cap -- a harness guard standing in for the policy, carrying no evidence of anything.

The low-level Fig 3 and Fig 4 entry points have no dedicated examples -- they exist as internal mechanism feeding `bkr94acs` with no realistic standalone caller (see *When to Use What*); their behavior is exercised through `bkr94acs` and through the test suites.

`example/bracha87Fig1.c` -- reliable broadcast (Theorem 1). One designated initiator broadcasts a multi-byte value; all correct processes either accept the same value or none accept (Lemmas 2 and 4):

```bash
./example_bracha87Fig1 4 1 hello                # 4 processes, 1 Byzantine fault, broadcast "hello"
./example_bracha87Fig1 -s 42 7 2 transactionXYZ # shuffled delivery
./example_bracha87Fig1 -b 2 4 1 hello           # Byzantine initiator equivocates (split=1 and split=4: all correct processes accept; split=2 and split=3: none accepts -- Theorem 1's second arm)
./example_bracha87Fig1 -v -o 1 4 1 ping         # verbose trace, process 1 is initiator
```

`example/bkr94acs.c` -- multi-value agreement on arbitrary strings:

```bash
./example_bkr94acs 4 1 joe sam sally tim        # 4 processes A-Cast strings
./example_bkr94acs -s 42 4 1 joe sam sally tim  # shuffled delivery (different order; subset may differ)
./example_bkr94acs 4 0 joe sam sally tim        # t=0: all A-Casts included
./example_bkr94acs -v 7 2 alpha bravo charlie delta echo foxtrot golf
./example_bkr94acs -d 3 4 1 joe sam sally tim   # WAN laggard, eager: EXCLUDED (3/4) -- its value still accepted
./example_bkr94acs -d 3 -g 8 4 1 joe sam sally tim  # same schedule, 8 sweeps of patience: INCLUDED (4/4), step 2 never fires
```

The `-d`/`-g` pair is the sweep-side pacing demonstration (`BPR.md`, *The Sweep-Side Decisions*): one delayed honest A-Cast, released at the knife edge where step 2 first enables, excluded by the eager schedule and included by patience -- and in the eager run the late value still accepts everywhere, pinning that exclusion is participation loss, never value loss.

The `8` is not a transferable number, and the demo is deliberately narrow about which half it exercises. The delayed A-Cast is *released* by a direct `bkr94acsAcast` onto a lossless queue, so it reaches every process at once: the patience here measures only sweeps-to-enablement, never the rate at which a Retry cursor re-carries a delayed instance. A deployment's patience has to span that rate instead -- one full cursor pass per re-send of any particular instance -- which on a real transport is a much larger number of ticks and a much smaller number of sweeps. Size it directly against that re-send rate, counting `bkr94acsSentFig1Count(a)` Retry calls to the pass, and not against this example -- never by way of S, which derives from the patience rather than the other way round (*Abandonment*).

## Coin Choice -- Caller Responsibility

**The library is coin-agnostic.** Both `bracha87Fig4Init` and `bkr94acsInit` take a `bracha87CoinFn` callback plus closure; the caller supplies the coin and owns the consequences of that choice. The bundled `example/bkr94acs.c` uses a **deterministic alternating coin** chosen for reproducible demo runs -- the example source explicitly notes this is for demonstration only. This section is reference material to inform the caller's choice.

Fig 4 step 3 case (iii) -- when neither decision-count rule fires -- calls the coin. The coin is how Bracha meets FLP impossibility: deterministic asynchronous consensus is impossible, and the paper does not evade that -- it changes the termination requirement, with randomization buying probabilistic termination. Under a **common-coin** assumption each phase reaches agreement with constant probability, so the expected number of phases is O(1); under other coin choices the bound depends on the choice. (Bracha's numbered performance result, Theorem 3, analyzes the local/free-choice coin -- see below; his Theorem 2 is the resilience theorem, not a phase-count bound.) The callback is synchronous: Fig 4 invokes it inline at step 3 case (iii), and the state machine cannot suspend mid-step -- a coin that needs its own message rounds must deliver per-phase values ahead of need (dealt into the callback's closure); local and deterministic coins compute inline. Options the caller may supply via `bracha87CoinFn`:

- **Common coin** (same value across all processes per phase): whatever shared-randomness construction the deployment already trusts. It brings its own setup, which the library does not require for any other reason.
- **Local coin** (each process flips independently): e.g. `arc4random_buf` per process. The simplest adversarial-safe option; no shared-randomness infrastructure required. The performance claim is Bracha's own Theorem 3, whose proof the paper places in Ben-Or 1983 ("Another Advantage of Free Choice (Extended Abstract): Completely Asynchronous Agreement Protocols," PODC '83): at `t = c*sqrt(n)` the expected number of phases is a constant independent of n (though exponential in c); at `t = c*n` it is exponential in n.
- **Deterministic coin** (e.g. `phase & 1`): zero entropy under an adversarial scheduler. Useful for reproducible tests and for non-adversarial deployments -- used by the bundled examples for demo reproducibility, not safe under an adaptive adversary.

A slow-converging coin drives Fig 4 step 3 case (iii) ties round after round, advancing the phase counter toward the encoding-imposed `maxPhases=85` ceiling (`BRACHA87_MAX_PHASES`, `bracha87.h`). Hitting that ceiling raises `BRACHA87_EXHAUSTED`: the BA can issue no new phase/round, so it will never decide, `COMPLETE` becomes unreachable, and the run can end only through the abandonment policy -- Lemma 2 Part C admits no unilateral substitute (Implementation Note 12; see *Abandonment*).

## Abandonment

Termination is an application choice -- the library prescribes none. The model behind that -- why the protocol can give evidence of progress but never evidence of death or evidence that stopping is safe, why the only sound policy shape is giving up -- **abandoning** -- after enough consecutive protocol steps without progress, and why the gate counts sweeps and never wall time -- is `BPR.md`'s (*Termination and Abandonment*; quiescence, the retry's own success-side ending, is its *Quiescence*). This section keeps what a deployment wires and sizes.

A run has exactly one exit -- **abandon** -- and two markers the policy reads on the way there:

- **`BKR94ACS_ACT_BA_EXHAUSTED`** -- that process's BA consumed its round encoding (`maxPhases`; Fig 4's `BRACHA87_EXHAUSTED` surfaced) and can issue no new phase/round, so `COMPLETE` is unreachable and the run can only end in abandonment. It is not itself an exit -- the loop keeps draining and ticking, no unilateral substitute decision is permitted (Lemma 2 Part C; Implementation Note 12), and when the gate fires the application surfaces EXHAUSTED as the failure cause.
- **`BKR94ACS_ACT_COMPLETE`** -- the success marker. Post-decide continuation requires broadcasting past it, so even a successful run leaves through the abandonment gate (or through quiescence -- see the last scenario).

Whatever the policy's shape, two loop obligations stand: do not stop on `BA_DECIDED`/`COMPLETE` (post-decide continuation, Implementation Note 1), and call Retry exactly once per tick (the network flood warning in `bracha87.h` -- the tick rate is the wire rate limit). And a process that abandons without `COMPLETE` must surface "gave up without a decision" as its own outcome, with empty membership -- never a substituted subset.

### The policy is one knob

**Progress** is an Input call that returns actions (duplicate deliveries -- including every BPR retransmission -- return 0, a tested black-box contract), a `BKR94ACS_ACT_BA_DECIDED` / `BKR94ACS_ACT_COMPLETE`, or an application-level first-arrival the deployment chooses to count. The unit is the **retry sweep**: one full pass of the Retry cursor over every sent Fig 1 instance -- `bkr94acsSentFig1Count(a)` Retry calls (recompute it; the count grows as BAs advance rounds). A sweep that ends with no progress observed is **barren**; abandon after S consecutive barren sweeps. Reaching `COMPLETE` before the gate fires is the success flavor of the same question -- there is no separate "success timeout." Why progress is exactly this narrow and why the unit is the sweep are argued in `BPR.md`.

The knob a deployment actually turns is the **patience** the two duty seams consume, counted in full cursor passes -- `bkr94acsSentFig1Count(a)` Retry calls to the pass -- and S derives from it: **S = 2 x patience**. One patience is the window itself, the passes that re-carry a delayed A-Cast; the same again covers the rounds the BAs entered with 0 still take, entered last and exchanging INITIAL and READY while the rest of the cohort has long since readied. Converting a deployment's own environment into passes is the deployment's business and nothing this library speaks to. The derivation speaks for a patience of at least one pass: a zero-patience deployment runs both seams eagerly and sizes S on its own ground. Why the second patience-worth, and the ordering the doubling buys at the step-2 fanout, are `BPR.md`'s (*The Abandon Boundary*).

Worst-case time to the gate is computable in advance: at most `S * bkr94acsSentFig1Count(a) * tick`, where the sweep length is itself bounded by the Fig 1 instance space (n A-Cast instances plus n BAs x rounds x n initiators) -- check S against that product, not against intuition about seconds, remembering that S is derived rather than picked and that the count is not steady: entering a BA marks its round-0 Fig 1 instance, so the fanout's enter-0 into every unentered BA raises `bkr94acsSentFig1Count` in one step and lengthens every sweep after it. Wall time's one legitimate role is pacing the tick, a wire rate limit whose accuracy correctness never depends on; pace in seconds, abandon in sweeps.

The same policy serves the bare Fig 1 surface (`example/bracha87Fig1.c`): progress is a `bracha87Fig1Input` that returns actions or an ACCEPT; the sweep is one full `bracha87Fig1RetryStep` pass over the caller's array (`bracha87Fig1SentCount` calls); and Fig 1 has no EXHAUSTED -- reliable broadcast has no phase ceiling, so abandonment and quiescence are the only exits that layer has. The example takes the second: it wires both READY annotations, which is what makes the quiescence retire reachable there at all, and it re-enters a quiescent instance in the rotation whenever an unmarked READY arrives.

### Scenarios

Every scenario resolves to the same gate; what differs is only how the local evidence stream looks, which is the point -- full asynchrony means the policy can read nothing else. The treatments are `BPR.md`'s (*Termination and Abandonment*, *Quiescence*); the names are kept here because the tests and examples cite them as cross-reference labels:

- **Partition** -- indistinguishable from arbitrarily slow links, so the policy must not try to tell them apart; the side holding n-t correct processes completes, and a heal is carried by the survivors' never-retired READY alone.
- **Asymmetric flow** -- the two halves of one broken link correctly reach opposite outcomes (a receive-only process can COMPLETE while a send-only one feeds everyone and abandons); progress evidence is local by construction, and no protocol signal reconciles the sides.
- **Slow versus dead** -- indistinguishable in principle; counting sweeps makes the policy commensurate with the protocol, so wall time changes how long the gate takes, never whether it fires.
- **Byzantine-silent processes** -- excluded at no cost but the retry tail toward them, which is correct rather than waste: a silent process is indistinguishable from a laggard that still needs the traffic.
- **Byzantine trickle** -- genuinely fresh acts that lead nowhere stretch the gate; the supply is bounded by value-blind per-sender dedup over a finite instance space, so the stretch defers the gate and cannot hold it open -- part of the adversary's allowance, priced by nothing.
- **Staggered start** -- a late process is byte-identical to a dead one until its first message, and the others' retries are precisely the bootstrap it missed; only the gate can kill a legitimately late run, so size the patience generously and S follows it.
- **After COMPLETE** -- success is not a stop; the two READY annotations give the retry tail a true end (quiescence, with its caller re-entry obligation), and the residue no annotation can reach -- the never-announcer -- is what the barren-sweep backstop is for.

---

## The Papers

Gabriel Bracha, "Asynchronous Byzantine Agreement Protocols," *Information and Computation* 75, 130-143 (1987). Implemented in `bracha87.[hc]`.

Michael Ben-Or, Boaz Kelmer, Tal Rabin, "Asynchronous Secure Computations with Optimal Resilience (Extended Abstract)," PODC '94, pages 183-192. Section 4 Figure 3 (Protocol Agreement[Q]) is implemented in `bkr94acs.[hc]`.

J. H. Saltzer, D. P. Reed, D. D. Clark, "End-To-End Arguments in System Design," *ACM Transactions on Computer Systems* 2(4), 277-288 (1984). Cited as the design rationale for placing the BPR (Bracha Phase Retry) retry at the protocol endpoint rather than in a lower transport layer.

`Bracha87.txt` is a companion summary of the Bracha 1987 paper: figures, rules, VALID set definitions, all lemma/theorem statements, and a mapping from lemmas to the tests that verify them (including the paper's Fig 1 echo-threshold typo, also documented at the rule table in `bracha87.h`).

`BKR94ACS.txt` is the line-by-line extract of BKR94 Section 4 used as `bkr94acs.[hc]`'s reference.

`SRC84.txt` is the relevant extract of the End-to-End paper used as the design citation for BPR.

`BPR.md` is this repository's own governing statement for the stratum no published paper covers -- BPR and the machinery beneath the papers' reliable-channel assumption. It stands in for the missing paper: mechanism in protocol vocabulary, each retire gate with its argument, the termination model, and a registry of every claim with its scope and the tests that stand behind it.

## Design Rationale -- Why These Three Papers

The three papers above are the smallest combination that satisfies this library's constraints -- authenticated multi-value agreement under fair-loss asynchrony, authenticated point-to-point channels as the entire setup, embeddable in C89 with no dynamic allocation. This section records what each paper carries.

### Why Bracha 1987 for reliable broadcast

The reliability primitive this stack rests on is reliable broadcast at `n > 3t` over nothing more than authenticated point-to-point channels, and Bracha's three-phase counting-threshold mechanism (initial / echo / ready) provides exactly that. Equally important, the paper's module boundary is a crisp algebraic interface (Fig 1 is reliable broadcast, Fig 3 is VALID-set validation, Fig 4 is consensus), so each lemma applies per-module and the audit chain shown later in this document is possible.

### Why BKR94 for asynchronous common subset

The agreement primitive we needed is multi-value agreement on a common subset of `n-t` A-Casts, asynchronous, Byzantine-resilient, with every process symmetric -- no process plays a distinguished role, so no machinery exists to elect, follow, or replace one.

BKR94 alone is the smallest piece that does ACS with no setup and no distinguished process -- `n` Bracha Fig 1 instances feed `n` binary agreements, and the step-2 trigger ("`n-t` BAs decided 1, enter 0 in the rest") closes it out. We deliberately stopped at ACS: BKR94 itself continues to ASC (asynchronous secure computation, the MPC layer), but ASC has no caller in the stack we are building, and pulling it in would require a private-channels mesh that ACS itself cannot bootstrap.

### Why Saltzer-Reed-Clark 1984 for BPR placement

Something must close the gap between the paper's reliable-channel assumption and fair-loss datagrams (*Bracha Phase Retry* states the gap; BPR is our offered closure). The end-to-end argument (Saltzer/Reed/Clark 1984) is the principle that decides where: the reliability function should live at the layer that has the complete information needed to perform it correctly, which for this function is the Bracha state machine itself. The application of the argument -- the "still owed" predicate, why a lower stubborn-link layer cannot retire correctly, and the performance carve-outs that keep wire optimizations (RSEC, batching, inter-shard delay) admissible as tuning -- is `BPR.md`'s *Placement*.

### What we deliberately did not build

The following are absent by design, not by oversight.

- **Authenticated point-to-point channels are the entire setup** (see *Message System*); nothing else is provisioned.
- **No bundled coin source.** The library is coin-agnostic -- both `bracha87Fig4Init` and `bkr94acsInit` take a `bracha87CoinFn` callback that the caller supplies. The bundled examples use a deterministic alternating coin (demo only); adversarial deployments are expected to supply their own. See *Coin Choice -- Caller Responsibility*.
- **No transaction layer, no ordering wrapper above SubSet, no application semantics.** BKR94 ACS outputs SubSet -- the agreed set of at least `n-t` processes and their A-Cast values; what those bytes mean, and any ordering over them, is the caller's choice.
- **No run identity.** One `struct bkr94acs` is one run, and nothing the library outputs distinguishes runs on the wire. Keeping successive or concurrent runs apart on shared channels -- and any rejoin or catch-up across runs -- is application framing, like the rest of the wire format.
- **No timing assumption.** The papers' correctness claims -- safety, and probabilistic termination (see *Coin Choice -- Caller Responsibility*) -- are proven under arbitrary asynchrony; this library adds no timing assumption to them.
- **All processes are symmetric.** No distinguished role, no machinery to replace one, no liveness scheduler.
- **No dynamic allocation, no I/O, no threads.** The library is a pure state machine; the caller provides memory and a transport.

## Architecture

### Binary Consensus Pipeline (bracha87)

```
message -> Fig1(n,t) -> accept -> Fig3(N) -> round complete -> Fig4(coin) -> decision
```

### BKR94 Asynchronous Common Subset (bkr94acs)

```
N A-Casts -> N Fig1(n,t,vLen) -> accept -> enter 1 in BA
                                   n-t BAs decided 1 -> enter 0 in remaining BAs
                                     (fired from the BPR sweep, caller-paced)
                                   N BA instances -> Fig1+Fig3 bank evidence on arrival;
                                     each BA round turned from the BPR sweep
                                     (caller-paced sample) -> Fig4 -> common subset
```

Per-figure contracts -- rule tables, thresholds, state, and action semantics -- are in the section banners and function documentation of `bracha87.h`; the ACS composition's are in `bkr94acs.h`. Two facts of the ACS composition are kept here:

The step-2 trigger is "n-t BAs decided with output 1," not "n-t Fig 1 ACCEPTs." The two coincide in benign runs but diverge under asynchrony or Byzantine scheduling, and only the decide-1 trigger satisfies Part A case (i) of the BKR94 Lemma 2 proof. The trigger, like Fig 4's "wait until", is enabling evidence rather than a moment: the arrival paths only bank evidence, and both the step-2 fanout and each BA round turn fire from the BPR sweep under caller-paced patience (`bkr94acsFanoutDuty` / `bkr94acsFanout`, `bkr94acsTurnDuty` / `bkr94acsTurn`; the paced loop above), zero patience recovering the eager schedule. The reading, the two seams' different cost classes -- exclusion at the fanout, coin luck at the turn -- and the safety license are `BPR.md`'s (*The Sweep-Side Decisions*).

Every message's per-message discriminator -- the Bracha87 type, the class, and (for a BA message) the binary value plus decision flag -- packs bit-disjoint into a single byte, so an application's wire framer carries the whole discriminator in one byte and a BA message carries no payload at all. It matters because ACS is message-dense -- N reliable broadcasts plus N binary BAs, each O(phases) of O(n^2) Fig 1 traffic -- so a byte saved per message compounds across the run. The canonical bit layout (a packer contract, not a library serialization) is documented at the message-class defines in `bkr94acs.h`; the example framer, `example/bkr94acs.c`, follows it.

## Test Coverage

`make check` runs six test binaries, each scoped to catch a different class of regression; together they form a defense in depth.

| Binary | Scope | What it catches |
|---|---|---|
| `test_predicates` | Algorithmic primitives (white-box) | `fig4Nfn`, `fig3IsValid`, and the Fig 3 cascade enumerated against a paper-direct subset-enumeration reference at n=4, t=1 -- anchors the predicates beneath the DTC dispatch. |
| `test_bracha87` | Protocol white-box (bracha87) | Per-rule units, composed simulation, inline lemma/theorem assertions, Byzantine equivocation, post-decide preservation, BPR retirement invariants, the n >> 3t regime; reads internal flags directly. |
| `test_bracha87_blackbox` | Protocol black-box (bracha87) | Header-contract drift: validity/agreement/totality, precise echo thresholds, the BPR retirement contract, array Retry -- derived from `bracha87.h` and `Bracha87.txt` only. |
| `test_bkr94acs` | Protocol white-box (bkr94acs) | All-to-all simulation, step-2 trigger and post-decide-continuation regressions, BPR drop-convergence and Byzantine-silent canaries, EXHAUSTED handling (decide/exhaust acts drained from the sweep-side turn, as deployed); reaches into internal layout. |
| `test_bkr94acs_blackbox` | Protocol black-box (bkr94acs) | Header-contract drift: Lemma 2 Parts A-D, Input dedup (the invariant a progress counter rests on), Retry/quiescence under drop, EXHAUSTED, equivocating A-Cast initiator, step-2 pacing (the eager schedule excludes a delayed honest A-Cast, patience includes it, finite patience completes past a dead slot), turn pacing (deliveries alone decide nothing; TOLERANCE needs the elapsed signal, MET fires free; turns quiescent at completion) -- no `.c` reads. |
| `test_schedules` | Schedule explorer (instrument) | Bounded reachability over the two example loops' state graphs under adversarial delivery order and delay: a per-transition oracle plus a quiescent-terminal battery whose all-n ending-evidence check is the one detector separating the forbidden local-accept READY retire from the remote gate; frozen counts are regression constants. `make check` runs its subsecond smoke subset; `make schedules` is the deliberate full run. |

The white-box / black-box pairing surfaces a different class of bug at each layer. White-box catches internal-invariant regressions (a state-machine flag set wrong, a count left unbumped). Black-box catches API contract drift -- header text and code behavior pulling apart over time. Recent contract-drift fix caught by the black-box suite: `bkr94acsAcastValue`'s ACCEPT-gate (header documented "0 if not yet accepted" but pre-fix returned ECHOED-stored bytes, exposing pre-Lemma-2 values to callers).

The black-box suites stay strict about scope: only `*.h`, paper-extract `.txt`, and the matching black-box-style sibling are read while writing tests. When a test fails, the contract sources alone determine whether to tighten the code or rewrite the comment.

## Correctness Audit

The audit story is a four-link chain from paper to running code, with one human inspection step (boundary I/O wiring) and one exhaustive test step (the algorithmic predicates). The chain establishes that the code implements the papers' rules; the rules' correctness at general (n, t) is the papers' claim -- read the papers, not this repository, for those proofs.

```
paper rules            <-> .dtc files                human, rule-by-rule comments
.dtc files              -> compiled dispatch         dtc, exhaustive/exclusive
C wrapper boundary I/O                               human inspection
fig3IsValid, fig4Nfn, Fig 3 cascade                  test/test_predicates.c --
                                                     exhaustive enumeration vs
                                                     paper-direct reference at
                                                     n=4, t=1
```

The decision-table layer (`*.dtc`) is paper vocabulary, rule-by-rule commented with the paper's rule numbers. `dtc` enforces exhaustiveness and exclusivity at compile time and outputs depth-optimal dispatch (for `dtc`'s own verification story, see the decisionTableCompiler repository's README). The C wrapper sits below the dispatch and is one line per boundary input/output -- each line is either a flag/count/bit-test mapping or a boolean-to-side-effect; small enough to read.

The two algorithmic predicates that the dispatch delegates to -- `fig3IsValid` (recursive existential), `fig4Nfn` (case analysis with permissive D_FLAG encoding) -- and the Fig 3 cascade (iterative re-validation) are the only places where search/recursion/iteration sits below the bridge. They are anchored by `test_predicates.c`: 960 `fig4Nfn` inputs, 165 `fig3IsValid` evaluations, 4 cascade delivery permutations, all at n=4 t=1, against a paper-direct subset-enumeration reference. All agree.

`fig3IsValid` is paper-correct **given a caller's N that exposes the existential subset quantifier via `rc > 0`**. The Fig 3 dispatch invokes N once on the full validated set; N's responsibility is to answer "could some n-t subset legitimately produce this value?" If a caller supplies an N whose permissive return is suppressed, `fig3IsValid` correctly rejects values the paper definition would admit via a strict subset. `fig4Nfn` is the canonical N for Fig 4 and exposes the existential analytically; the 960-input correspondence test against paper-direct subset enumeration anchors that delegation. The two predicates verify each other transitively: `fig4Nfn` <-> paper at all bounded inputs, and `fig3IsValid` <-> paper *given* that delegation.

## Implementation Notes

Each item below is a paper-vs-code divergence that any from-scratch implementation will encounter. We caught them by reading the paper rule-by-rule against composed-simulation runs and against fair-loss retry; isolation testing missed almost all of them, because the divergences only manifest under multi-figure interaction or under network conditions that simulated reliable channels never produce. They are the cost of building this from the papers -- listed here so a porter does not pay it twice, and so a reader evaluating "should I trust this implementation?" can see what was actually verified and what regression test catches each one.

1. **Post-decide continuation.** The paper says "Go to round 1 of phase i+1" after all three step 3 cases. A decided process must continue broadcasting so others can reach consensus. `BRACHA87_DECIDE | BRACHA87_BROADCAST` is returned exactly once; subsequent rounds return `BRACHA87_BROADCAST` only.

2. **D_FLAG leak.** After deciding, step 2 may set the D_FLAG on the value. Step 3's decided path restores the plain decision value to prevent D_FLAG from leaking into step 1 broadcasts of the next phase.

3. **N function existential quantifier.** The paper defines VALID^k with "there exist n-t messages..." Passing only the first n-t to N rejects messages that a correct process produced from a different subset. Fix: pass all validated messages; N returns permissive when subsets could disagree.

4. **Dead cascade after INITIAL.** The cascade after INITIAL could never fire -- if any threshold were met, `echoed` would already be set via Rule 2/3. Removed; comment explains the proof.

5. **Echoed value memcpy.** The memcpy on Rules 4/5/6 appears redundant but is essential. A Byzantine initial can store the wrong value first; the memcpy corrects it when the threshold-reaching value differs from the echoed value.

6. **Subset-majority reachability threshold (step 1).** Under N's tie-break-to-0, value 0 is reachable in some n-t subset iff `cnt[0] >= (nt+1)/2` (unified formula: equals `nt/2` for even n-t, `nt/2+1` for odd); value 1 is reachable iff `cnt[1] >= nt/2+1` (strict majority). Permissive iff both reachable. Using the symmetric `>= nt/2+1` test on both sides wrongly rejects honest tie-subset 0s when n-t is even. Verified by exhaustive enumeration for n=4..16.

7. **Forward cascade fires on every growth past n-t, not only first crossing.** `VALID^r_p` is existential over n-t subsets of `VALID^{r-1}_p` and monotone in it (paper definition + Lemma 6), so new validated messages at round k unlock stored unvalidated messages at k+1 even after round k first reached n-t. Gating the forward re-check on "first crossing only" strands honest round-(k+1) messages when validation of them depended on subsets that only exist after k grew.

8. **Permissive D_FLAG permission conveyed via `*result`.** On permissive return from Fig 4's N function (`rc > 0`), `*result & BRACHA87_D_FLAG` is set only when some n-t subset legitimately produces a decision candidate. Fig 3 rejects incoming D_FLAG when that bit is clear, preventing Byzantine d-injection in the no-majority windows of steps 2 and 3.

9. **Post-decide value preservation across sub-rounds.** During post-decide continuation (Note 1), `b->value` is preserved as the decision through every sub-round of subsequent phases. The .dtc-faithful Fig 4 dispatch zeroes the `setMajority` and `setDMajority` outputs when `have_decided = yes`, so adversarial inputs whose majority disagrees with the decision cannot drift the broadcast value away from it. Verified by `testFig4PostDecideAdversarial` (which would have failed against a pre-DTC version that overwrote `b->value` with majority/(d, majority) at the C's 0-based sub-rounds 0 and 1 (paper steps 1 and 2) of post-decide phases).

10. **BPR (ready, v) retry must NOT short-circuit on accepted.** An accepted process owes its READY to processes still below the 2t+1 threshold -- READY is the amplification carrier, and the asymmetry with Note 11 is that ACCEPTED *does* retire the bootstrap-only INITIAL and ECHO but must *never* retire READY. The argument is `BPR.md`'s (*Retirement*). Regression check: `testFig1Bpr` post-accept assertions (READY survives; INITIAL/ECHO retired).

11. **BPR (initial, v) / (echo, v) retry: retire only on a stop strictly stronger than local-echo.** ACCEPTED (retires both) and all-echoed (retires INITIAL) are the two sound stops; the forbidden weaker gate, "stop INITIAL once we ECHOed locally," strands a process that missed the bootstrap at the n = 3t+1 boundary, where the echo threshold equals the honest count. The argument is `BPR.md`'s (*Retirement*). Regression checks: `testBprByzantineSilent` (n=4 t=1, one silent Byzantine process -- converges in 1 sweep; the original `!ECHOED` gate stalled at |SubSet|=1 over 50000+ sweeps) and `testFig1Bpr` all-echoed assertions (echoSenders==n retires INITIAL without accept).

12. **Fig 4 EXHAUSTED means no new phase/round; no unilateral substitute at the BKR94 layer.** When `bracha87Fig4Round` returns `BRACHA87_EXHAUSTED` (probabilistic termination did not converge within the unsigned-char round encoding's 85-phase ceiling), the local BA has no decision. BKR94 Lemma 2 Part B's "all BAs terminate" assumption is violated, and Part C (SubSet agreement) is unrecoverable locally -- any unilateral substitute (decide 0 or 1) could disagree with another process's actual decision (different local-coin sequence or message ordering). The library surfaces `BKR94ACS_ACT_BA_EXHAUSTED` and marks the affected process's BA state as exhausted (`bkr94acsBaDecision(acs, process)` returns 0xFE thereafter); the 0xFE sentinel does not match the decided-count scan, so an exhausted BA never counts as decided (no decision was made) and `acs->complete` stays clear. The application surfaces this as the run's failure cause and exits through its abandonment policy (see *Abandonment*). BPR continues retrying for that process so other processes may still benefit from earlier-round echoes/readys. EXHAUSTED is mutually exclusive with DECIDE per Fig 4 semantics, so single output is structural -- no dedup guard needed. Regression check: `testExhausted`.

13. **READY's only sound retire is *remote*, and it takes TWO facts -- never *local* accept (the per-process refinement of Note 10).** "q has accepted" and "q has received MY accept" are different facts; suppressing on the first alone strands q's own gate one bit short for good, so the second needs its own wire bit -- `BKR94ACS_RECEIVED`, whose ABSENCE re-arms the re-send toward its sender, routed to `bkr94acs{Acast,Ba}Resend` -- and no `>=2t+1 accepted -> stop` threshold shortcut is admissible. The arguments, including the Byzantine containment of both annotations, are `BPR.md`'s (*Suppression and the Announcements*). Regression checks: `testFig1SkipAccept`, `testBprSkipAccept`, and `runWithRetry` drop-convergence with suppression active.

14. **INITIAL must come from the designated initiator -- `from == process` (A-Cast) / `from == initiator` (consensus) is enforced, not assumed.** A Fig 1 instance is keyed to ONE designated initiator. Only that initiator may send `(initial, v)`; ECHO and READY arrive legitimately from any process (`from != initiator` is normal for them and is sender-deduped). A non-initiator INITIAL is a *forged broadcast* -- a Byzantine process injecting a value the correct initiator never sent -- and because Rule 1 echoes the first INITIAL unconditionally, an attacker reaching every correct process drives the `(n+t)/2+1` echo cascade to a false ACCEPT, violating reliable-broadcast validity. Authenticated channels do **not** close this: they bind `from` to the true sender but not the message's *claimed* initiator (initiator != from is a valid ECHO/READY), so the binding is a protocol-semantic check, not a transport one. `bkr94acsAcastInput` / `bkr94acsBaInput` drop the message when `type == BRACHA87_INITIAL && from != process/initiator`. The bare `bracha87Fig1Input` cannot self-enforce (it is not told its own initiator index -- see the INITIAL-sender obligation in its header doc), so a direct bare-layer caller must filter before calling -- `example/bracha87Fig1.c` shows the filter in its delivery loops. This trap is invisible to honest-only tests: every honest generator sends INITIALs with `from == process`, and even an equivocating *initiator* still has `from == process` -- so nothing exercised the forged-non-initiator path until it was added explicitly. Regression checks: `testForgedInitial` (white-box) and Section A's forged-INITIAL contract case (black-box).

---

## Re-Implementing in Another Language

A port that wants to preserve this library's correctness story has two pieces of machinery to either reproduce or replace:

1. The **decision-table compilation pipeline** (described below), which lifts paper rules into depth-optimal dispatch.
2. The **trap list and predicate corpus** for cross-checking the result. Implementation Notes #1-#14 above are paper-vs-code (and model-precondition) traps; `test/test_predicates.c` is the exhaustive paper-direct reference for the algorithmic predicates that sit below the dispatch.

### Paper-Faithful Dispatch via DTC

Each module's per-call decision logic is captured in a CSV decision table written in the paper's vocabulary (`bracha87Fig{1,3,4}.dtc`, `bkr94acs.dtc`). A small bridge per module (`*ToC.dtc`) maps domain names and values to C identifiers and constants. The decisionTableCompiler (`../decisionTableCompiler/dtc`) co-compiles each pair to an optimal-depth pseudocode dispatch, which a local `psu.awk` translates to a C snippet the entry-point function `#include`s.

| Source | Bridge | Generated snippet | Entry point | Depth |
|--------|--------|-------------------|-------------|-------|
| `bracha87Fig1.dtc` | `bracha87Fig1ToC.dtc` | `bracha87Fig1Rules.c` | `bracha87Fig1Input` | 7 |
| `bracha87Fig2.dtc` | (none -- Fig 3 subsumes) | -- | -- | -- |
| `bracha87Fig3.dtc` | `bracha87Fig3ToC.dtc` | `bracha87Fig3Rules.c` | `bracha87Fig3Accept` | 4 |
| `bracha87Fig4.dtc` | `bracha87Fig4ToC.dtc` | `bracha87Fig4Rules.c` | `bracha87Fig4Round` | 6 |
| `bkr94acs.dtc` | `bkr94acsToC.dtc` | `bkr94acsRules.c` | both bkr94acs entry points (one snippet, two `#include`s) | 7 |

`dtc` enforces exhaustiveness and exclusivity of the rules at compile time. Depths are full-optimum (full search confirms each is depth-minimal for its boundary-input set). The C wrapper computes boundary inputs, `#include`s the dispatch, and applies the boolean outputs as side effects in an order that is the API contract (e.g. for Fig 1: `echo` before `ready` before `accept`). See `../decisionTableCompiler/README.md` for the bridge mechanism.

The generated snippets are committed, so building this repository needs no DTC dependency at all; `dtc` is invoked only by `make rules` when a `.dtc` changes (see *Building*). A re-implementation in another language can transcribe the dispatch by hand from each `.dtc`'s rule table -- the `.dtc` files are the readable source of record, and the generated `.c` snippets are large nested `if`/`switch` ladders that a competent developer can read directly. The constraint is that the transcription must preserve exhaustiveness and exclusivity (every input combination has exactly one matching rule), which `dtc` proves at compile time and a hand-port must prove by inspection.

### Where to start

- **`Bracha87.txt`** and **`BKR94ACS.txt`** are the paper extracts. Start here.
- **`BPR.md`** is the governing statement for everything beneath the papers' reliable-channel assumption -- the retry and its gates, the pacing, the abandonment model -- with its own claim registry and the mapping to the tests that stand behind it. A port that runs over fair loss re-implements this stratum too.
- **`bracha87Fig{1,3,4}.dtc`** and **`bkr94acs.dtc`** are the paper-vocabulary decision tables, rule-by-rule commented to the paper. These are the API contract for the dispatch.
- **`test/test_predicates.c`** is the paper-direct reference for `fig3IsValid`, `fig4Nfn`, and the Fig 3 cascade -- exhaustive enumeration at n=4, t=1. A port should pass this corpus.
- **`test/test_bracha87.c`** and **`test/test_bkr94acs.c`** are the integration-test corpus, including the regression checks named in Implementation Notes #9-#14.
- **Implementation Notes #1-#14 above** are the traps. Each one names a specific paper-vs-code divergence and (where applicable) the regression test that catches it.

## License

LGPL v3 or later. See `COPYING.LESSER` and `COPYING`.
