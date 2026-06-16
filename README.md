# asynchronousByzantineAgreementProtocols

Generated with Claude Code (https://claude.ai/code)

A C library implementing Gabriel Bracha's 1987 paper (Figures 1, 3, and 4 as composable pure state machines; Figure 2 captured for paper completeness and subsumed by Figure 3), plus the BKR94 Asynchronous Common Subset (ACS) protocol built from them.

## Overview

An implementation of Bracha 1987 as composable pure state machines, with module boundaries that match the paper's figures. ANSI C89, zero dependencies, up to 256 processes. No I/O, no threads, no dynamic allocation -- the caller provides memory and executes output actions.

Each module boundary matches the paper exactly, so the paper's proofs apply per-module: Lemmas 1-4 and Theorem 1 to Fig 1, Lemmas 5-7 to Fig 2/3, Lemmas 8-10 and Theorems 2-3 to Fig 4.

The `bkr94acs` module composes these figures into multi-value agreement: N processes A-Cast arbitrary values, and all honest processes agree on the same common subset of at least n-t A-Casts. This is Ben-Or/Kelmer/Rabin 1994 Section 4 Figure 3 (Protocol Agreement[Q]).

The API reference is the headers themselves: `bracha87.h` and `bkr94acs.h` carry the per-function contracts, and this README does not restate them. **If you are integrating the library**, the load-bearing sections are *When to Use What*, *System Model*, *Bracha Phase Retry*, *Examples*, *Coin Choice*, and *Abandonment*. **If you are auditing the implementation or porting it to another language**, additionally read *Design Rationale*, *Architecture*, *Test Coverage*, *Correctness Audit*, *Implementation Notes*, and *Re-Implementing in Another Language*. **If the subject is unfamiliar**, *The Papers* names the sources and their in-tree extracts.

## When to Use What

This library provides two application-facing primitives. Pick by the shape of your problem.

**Reliable broadcast — `bracha87Fig1`.** One designated sender announces a value; all correct processes either accept the same value or none do, under up to `t` Byzantine faults at `n > 3t`. Use when you have a known initiator per message: configuration distribution from a designated source, single-writer state replication, one-shot dissemination of a signed announcement, or as a reliable-channel building block inside your own outer protocol. Application surface: `bracha87Fig1Input` per delivered message, `bracha87Fig1RetryStep` over the caller-owned instance array per retry tick. See `example/bracha87Fig1.c`.

**Common subset — `bkr94acs`.** N processes each A-Cast a value; all correct processes agree on the same common subset of at least `n-t` A-Casts. Use when you need agreement on a batch of contributions among symmetric processes: atomic-broadcast batching, MPC input bundling, distributed candidate selection — anything shaped as "agree on the set" rather than "agree on a single value." Application surface: the loop under *Bracha Phase Retry* below. See `example/bkr94acs.c`.

Fig 3 (VALID-set framework) and Fig 4 (binary Byzantine agreement) are exposed for completeness but exist primarily as internal mechanism feeding `bkr94acs`; raw single-bit binary BA has no realistic standalone caller (the seven-year gap between Bracha 1987 and BKR94 1994 is exactly that evidence).

## System Model — What the Caller Must Provide

The paper's proofs depend on three assumptions about the communication system (Section 2):

> "We assume a reliable message system in which no messages are lost or generated. Each process can directly send messages to any other process, and can identify the sender of every message it receives."

These assumptions are not optional — they are load-bearing requirements of every lemma and theorem in the paper. This library is a pure state machine with no I/O. **The caller is responsible for building a transport layer that satisfies them:**

1. **Eventual delivery under fair-loss.** Every message sent between correct processes must eventually arrive — but messages may be silently dropped any finite number of times in transit. The Bracha Phase Retry (BPR, see below) is offered to close the gap from "fair-loss point-to-point" to "reliable delivery" at the protocol endpoint, so the transport need not provide retransmission of its own; the caller's retry tick is what drives BPR's retries.

2. **No message fabrication.** The transport must not generate messages that were never sent. A Byzantine process may send arbitrary content, but the transport itself must not invent messages. In practice this means authenticated channels.

3. **Sender identification.** The receiver must know which process sent each message, and a Byzantine process must not be able to impersonate a correct one. In practice this means authentication bound to process identity.

The protocol's correctness (both safety and termination) does not depend on any timing assumption — that is the asynchronous-BFT model. Retry cadence and abandonment thresholds are deployment tuning (see *Abandonment*), not protocol invariants.

Nothing beyond these three assumptions is required. Authenticated point-to-point channels (assumption 3 above) are the entire setup — provisioned by whatever mechanism the deployment prefers (HMAC over a pre-shared key, TLS, mutual SSH, Noise, etc.); the cost is one symmetric authentication credential per process pair. The machinery a comparable deployment might expect to provision at this layer is absent by design — see *What we deliberately did not build* (Design Rationale).

A complete deployment is therefore: this transport, the identity and keys that authenticate it (assumption 3), a coin source (see *Coin Choice — Caller Responsibility*), and an abandonment policy (see *Abandonment*). The library is protocol-only and supplies none of them.

## Bracha Phase Retry (BPR)

Bracha's correctness proofs presume reliable point-to-point channels between correct processes. Over fair-loss datagrams that assumption is not satisfied. **We offer BPR as a possible solution for Bracha's reliable-network assumption over fair loss.** No paper covers BPR, and we make no theorem-grade claim for it: its support is the per-gate retire/suppress reasoning documented at the entry points that implement it, plus the drop-convergence tests. What it replaces is the application-layer retry bookkeeping.

### Why BPR exists

BPR is the end-to-end argument (Saltzer, Reed, Clark 1984 — see `SRC84.txt`) applied to Bracha. The reliability function — "deliver INITIAL/ECHO/READY despite a fair-loss network" — depends on knowledge that lives only at the Bracha state-machine endpoint (the `F1_INITIATOR`/`F1_ECHOED`/`F1_RDSENT` flags and the BKR per-process BA-decided state). A lower link layer cannot decide when to retire without being told by Bracha — i.e., without folding the function back into the endpoint anyway — so the principled placement is at the endpoint itself.

Without BPR, an application using this library would need its own bookkeeping to track per-record destinations, per-process evidence of receipt, and a cursor-driven retransmit retry — the standard "deployment-layer reliability" pattern. Three properties make BPR a better fit:

1. **All retry state is already in the protocol.** The sent-action flags (`F1_ECHOED`, `F1_RDSENT`) plus a one-bit `F1_INITIATOR` flag (set by `bracha87Fig1Initiator`) encode everything needed to decide which actions to retry. No parallel data structure.
2. **The retry is event-driven, not wall-clock-driven.** The application's retry tick is the only event; no `retryNs` floor, no per-record evidence tracking, no destination masks.
3. **Asynchrony is preserved.** No timing predicate appears anywhere in the protocol; the application's tick *is* the event, and silent ticks output nothing.

The retry rules themselves — which sent actions retry, the retire gate and per-process suppress mask each one carries, and the reasoning that makes each gate sound — are per-function contracts: see `bracha87Fig1Bpr`, `bracha87Fig1Skip`, and the retry-infrastructure banner in `bracha87.h`; `bkr94acsRetry` and the `bkr94acsAcastAccepted` / `bkr94acsBaAccepted` ingress entries in `bkr94acs.h`.

### Application loop

With BPR, the application loop is two operations: drain the network and tick the retry. No *application* bookkeeping — no per-record destination mask, no per-process receipt tracking. The per-process suppress mask the broadcast consults (and the `acFrom` evidence behind the READY mask) is library-owned, intrinsic protocol state surfaced through `bracha87Fig1Skip` / the `.skip` field; the application just honours it (`BRACHA87_SKIP_TST`) and feeds the decoded `BKR94ACS_ACCEPTED` bit back via `bkr94acs{Acast,Ba}Accepted` — it maintains none of it.

```c
struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(N, MAX_PHASES)]; /* Input out[]: the larger bound */
struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];           /* Retry out[] */
struct bkr94acsAct propAct;
struct bracha87Retry retry;

bracha87RetryInit(&retry);

/* Self-initiation: mark the local A-Cast Fig 1 as initiator and output
 * one ACAST_SEND action (.type = BRACHA87_INITIAL) for the application
 * to broadcast.  The act's .value points into library storage. */
bkr94acsAcast(a, my_value, &propAct);
broadcast_action(propAct);

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
    for (k = 0; k < n; ++k) broadcast_action(acts[k]);
  }

  /* Retry tick: BPR retries sent actions.  ONE call per tick
   * — see the network flood warning in bracha87.h. */
  n = bkr94acsRetry(a, &retry, out);
  for (k = 0; k < n; ++k) broadcast_action(out[k]);

  sleep(tickMs);
}
```

`broadcast_action(act)` switches on `act.act` and broadcasts the described Fig 1 message — field usage per act is documented at `struct bkr94acsAct` in `bkr94acs.h`. `BA_DECIDED` and `COMPLETE` are observability signals with no wire output; `BA_EXHAUSTED` reports a BA that can issue no new phase/round, making `COMPLETE` unreachable (see *Abandonment* below).

`terminate` is the application's abandonment policy — an application choice, not a library-prescribed one. See *Abandonment* below.

## Building

```bash
make            # build .o and examples
make check      # build and run all five test binaries (see Test Coverage below)
make clean      # remove build artifacts
make clobber    # remove DTC generated .c files
```

Building requires `../decisionTableCompiler/dtc` and `awk` to compile the `.dtc` rule tables to dispatch C snippets; the Makefile invokes both automatically. The generated `.psu` and `*Fig{1,3,4}.c` / `bkr94acsRules.c` files are reproducible artifacts (`make clobber` removes them, the next `make` regenerates).

Compiler flags: `-std=c89 -pedantic -Wall -Wextra -Os -g`

## Examples

Two runnable examples sit in `example/`, one per application-facing API surface. Each runs in a single process with a synchronous in-memory queue (no loss, no reordering, no asynchrony) — they exercise the protocol state machines and the BPR retry but do **not** exercise a deployment-time termination policy (when to give up, abandonment) needed under real asynchronous transport.

The low-level Fig 3 and Fig 4 entry points have no dedicated examples — they exist as internal mechanism feeding `bkr94acs` with no realistic standalone caller (see *When to Use What*); their behaviour is exercised through `bkr94acs` and through the test suites.

`example/bracha87Fig1.c` — reliable broadcast (Theorem 1). One designated initiator broadcasts a multi-byte value; all correct processes either accept the same value or none accept (Lemmas 3 and 4):

```bash
./example_bracha87Fig1 4 1 hello                # 4 processes, 1 Byzantine fault, broadcast "hello"
./example_bracha87Fig1 -s 42 7 2 transactionXYZ # shuffled delivery
./example_bracha87Fig1 -b 2 4 1 hello           # Byzantine initiator equivocates (split=2 stalls; split=1 or 3 converges)
./example_bracha87Fig1 -v -o 1 4 1 ping         # verbose trace, process 1 is initiator
```

`example/bkr94acs.c` — multi-value agreement on arbitrary strings:

```bash
./example_bkr94acs 4 1 joe sam sally tim        # 4 processes A-Cast strings
./example_bkr94acs -s 42 4 1 joe sam sally tim  # shuffled delivery (different subset)
./example_bkr94acs 4 0 joe sam sally tim        # t=0: all A-Casts included
./example_bkr94acs -v 7 2 alpha bravo charlie delta echo foxtrot golf
```

## Coin Choice — Caller Responsibility

**The library is coin-agnostic.** Both `bracha87Fig4Init` and `bkr94acsInit` take a `bracha87CoinFn` callback plus closure; the caller supplies the coin and owns the consequences of that choice. The bundled `example/bkr94acs.c` uses a **deterministic alternating coin** chosen for reproducible demo runs — the example source explicitly notes this is for demonstration only. This section is reference material to inform the caller's choice.

Fig 4 step 3 case (iii) — when neither decision-count rule fires — calls the coin. The coin is how Bracha escapes FLP impossibility: deterministic asynchronous consensus is impossible, and randomization buys probabilistic termination. Under a **common-coin** assumption each phase reaches agreement with constant probability, so the expected number of phases is O(1); under other coin choices the bound depends on the choice. (Bracha's numbered performance result, Theorem 3, analyzes the local/free-choice coin — see below; his Theorem 2 is the resilience theorem, not a phase-count bound.) (This is the same FLP-escape mechanism as other randomized async BFT protocols; partial-synchrony designs like PBFT/Tendermint/HotStuff escape FLP through timing assumptions instead.) The callback is synchronous: Fig 4 invokes it inline at step 3 case (iii), and the state machine cannot suspend mid-step — a coin that needs its own message rounds must deliver per-phase values ahead of need (dealt into the callback's closure); local and deterministic coins compute inline. Options the caller may supply via `bracha87CoinFn`:

- **Common coin** (same value across all processes per phase): a verifiable random beacon, a distributed coin protocol, or a threshold-signature-based scheme. Brings additional setup (DKG, threshold keys, etc.) that the library does not require for any other reason.
- **Local coin** (each process flips independently): e.g. `arc4random_buf` per process. The simplest adversarial-safe option; no shared-randomness infrastructure required. The performance claim is Bracha's own Theorem 3, whose proof the paper places in Ben-Or 1983 ("Another Advantage of Free Choice (Extended Abstract): Completely Asynchronous Agreement Protocols," PODC '83): at `t = c*sqrt(n)` the expected number of phases is a constant independent of n (though exponential in c); at `t = c*n` it is exponential in n.
- **Deterministic coin** (e.g. `phase & 1`): zero entropy under an adversarial scheduler. Useful for reproducible tests and for non-adversarial deployments — used by the bundled examples for demo reproducibility, not safe under an adaptive adversary.

A slow-converging coin drives Fig 4 step 3 case (iii) ties round after round, advancing the phase counter toward the encoding-imposed `maxPhases=85` ceiling (`BRACHA87_MAX_PHASES`, `bracha87.h`). Hitting that ceiling raises `BRACHA87_EXHAUSTED`: the BA can issue no new phase/round, so it will never decide, `COMPLETE` becomes unreachable, and the run can end only through the abandonment policy — Lemma 2 Part C admits no unilateral substitute (Implementation Note 12; see *Abandonment*).

## Abandonment

Under arbitrary asynchrony the library can give the application evidence of progress; it can never give evidence of death or evidence that stopping is safe. Those two — "the run has failed, stop" and "the run has succeeded, stopping is now harmless" — are what every termination instinct reaches for, and the asynchronous model forbids both: a dead process and a slow one present the same evidence stream, and under unbounded latency no process can know that another no longer needs its messages. What remains is exactly one sound policy shape: watch the evidence of progress, and give up — **abandon** — after enough consecutive protocol steps without any.

A run has exactly one exit — **abandon** — and two markers the policy reads on the way there:

- **`BKR94ACS_ACT_BA_EXHAUSTED`** — that process's BA consumed its round encoding (`maxPhases`; Fig 4's `BRACHA87_EXHAUSTED` surfaced) and can issue no new phase/round. The consequence: that BA will never decide locally, so `COMPLETE` is unreachable and the run can only end in abandonment. It is not itself an exit — the loop keeps draining and ticking (BPR keeps retrying that process's instances; other processes may still benefit), no unilateral substitute decision is permitted (Lemma 2 Part C; Implementation Note 12), and when the gate fires the application surfaces EXHAUSTED as the failure cause.
- **`BKR94ACS_ACT_COMPLETE`** — the success marker. Post-decide continuation requires broadcasting past it, so even a successful run leaves through the abandonment gate (or through quiescence — see the last scenario).

Whatever the policy's shape, two loop obligations stand: do not stop on `BA_DECIDED`/`COMPLETE` (post-decide continuation, Implementation Note 1), and call Retry exactly once per tick (the network flood warning in `bracha87.h` — the tick rate is the wire rate limit). And a process that abandons without `COMPLETE` must surface "gave up without a decision" as its own outcome, with empty membership — never a substituted subset.

### Progress is events counted in sweeps, never wall time

The progress signals the library provides:

- **A fresh state advance:** an Input call that returns actions (`nacts > 0`). Duplicate deliveries — including every BPR retransmission — return 0, so a progress counter built on `nacts` is stable under retry noise. This dedup is a tested black-box contract, not an accident.
- **A decision:** `BKR94ACS_ACT_BA_DECIDED` or `BKR94ACS_ACT_COMPLETE`.
- **Application-level arrivals** the deployment chooses to count (e.g. the first arrival of a side-channel payload paired with an A-Cast).

The unit the policy counts in is the **retry sweep**: one full pass of the Retry cursor over every sent Fig 1 instance — `bkr94acsSentFig1Count(a)` Retry calls (recompute it; the count grows as BAs advance rounds). A sweep that ends with no progress signal observed is **barren**. The policy is one knob: abandon after S consecutive barren sweeps. Reaching `COMPLETE` before the gate fires is the success flavor of the same question — there is no separate "success timeout." Worst-case time to the gate is computable in advance: at most `S * bkr94acsSentFig1Count(a) * tick`, where the sweep length is itself bounded by the Fig 1 instance space (n A-Cast instances plus n BAs x rounds x n initiators) — budget S against that product, not against intuition about seconds.

The same policy serves the bare Fig 1 surface (`example/bracha87Fig1.c`): progress is a `bracha87Fig1Input` that returns actions (the same dedup keeps the counter retransmission-stable) or an ACCEPT; the sweep is one full `bracha87Fig1RetryStep` pass over the caller's array (`bracha87Fig1SentCount` calls); and Fig 1 has no EXHAUSTED — reliable broadcast has no phase ceiling, so abandonment is the only exit that layer has.

Wall time has exactly one legitimate role here: pacing the tick. The tick is a wire rate limit — a performance tunable under SRC84's carve-outs, like inter-shard delay — and correctness does not depend on its accuracy; a LAN tick of 10 ms and a WAN tick of 2 s run the same protocol. Driving *abandonment* off wall time is different in kind: it encodes a synchrony bound the protocol does not assume, and turns "slow" into "failed" (see *Slow versus dead* below). Pace in seconds; abandon in sweeps.

### Scenarios

Every scenario below resolves to the same gate. What differs is only how the local evidence stream looks — which is the point: full asynchrony means the policy can read nothing else.

**Partition.** A process cut off mid-run sees exactly what a process behind arbitrarily slow links sees: nothing fresh, barren count climbing, abandon. No signal distinguishes the two, so the policy must not try. The side still holding n-t correct processes runs to `COMPLETE` without the cut-off process — it is one of the t tolerated, its A-Cast included or excluded by how far it had spread before the cut, and the survivors agree either way. If the partition heals while the others are still draining and ticking, their never-retired READY retries carry the returning process to the same subset — READY alone suffices, since the value rides with it and the t+1-readys rules re-bootstrap the missed INITIAL/ECHO. That carry is what post-decide continuation buys, and it lasts exactly until the others' own gates fire.

**Asymmetric flow.** Fair-loss does not promise symmetry; a firewall or routing asymmetry can pass one direction and starve the other, and the two halves of one broken link then see opposite evidence. A receive-only process (its sends are lost) observes constant progress: it validates, enters BAs, and can run all the way to `COMPLETE` — with the same subset as everyone else, its own unheard A-Cast consistently excluded — while every other process correctly counts it among the t silent faults. A send-only process (its receives are lost) feeds everyone — the others may well include its A-Cast in the agreed subset — yet sees pure barrenness and abandons, its own value agreed on by everyone but itself. Progress evidence is local by construction; no protocol signal exists to reconcile the two sides, and both behaved correctly.

**Slow versus dead.** Indistinguishable in principle, not merely in practice — the same impossibility the coin answers at the decision level. A wall-clock abandon window is a bet that "no message for B seconds" means dead; the protocol makes no such promise, so the bet silently converts a high-RTT deployment into a failing one. Counting barren sweeps makes the policy commensurate with the protocol: a slow run takes more wall time per sweep but the same number of sweeps, so wall time changes how long the gate takes to fire, never whether it fires.

**Byzantine-silent processes.** Up to t processes that never send are not a failure mode; the run completes with their A-Casts excluded. Their cost is the retry tail: every gate that needs all n — all-echoed, the all-n-accepted READY quiescence — can never close, so READY retries toward the silent processes continue until abandonment retires them. That tail is correct, not waste: a silent process is indistinguishable from a laggard that still needs the READY (previous scenario), and per-process suppression has already dropped every process that crossed. The conservative default — retry until the application abandons — is the right one.

**Byzantine trickle.** The mirror of silence: a Byzantine process can aim at the gate itself, feeding genuinely fresh state advances that lead nowhere — an echo or a ready for an instance that will never accept, an INITIAL for a BA round far ahead of need — and every fresh act resets the barren count. The supply is bounded: per-sender dedup admits one echo and one ready per sender per Fig 1 instance, and the instance space is finite, so up to t such processes can stretch the gate but never hold it open. Like partitions and asymmetric flows, the threat lands in the abandonment policy, not in agreement — safety never depended on the gate — so budget S knowing the stretch is part of the adversary's allowance.

**Staggered start.** A process that starts minutes after the others is, until its first message arrives, byte-identical to a dead one — and the others' BPR retries are precisely the bootstrap it missed: sockets not yet bound silently drop, and the retries keep coming. The only thing that can kill a legitimately late run is the abandon gate itself. Size S generously: patience costs wall time only (a silent tick outputs nothing), while a too-tight gate converts a slow deployment into a failed one.

**After COMPLETE.** Success is not a stop: other processes may still be below their thresholds, and post-decide continuation owes them the READY traffic that will carry them (the partition scenario, seen from the other side). The ACCEPTED annotation gives the tail a true end when the announcements survive loss: once every process has announced accept on every sent instance, Retry's full sweep returns 0 — quiescence — and nothing more is owed to anyone, so stopping then needs no further patience. But quiescence is best-effort, not a guaranteed terminal state (the final announcements can themselves be lost), so the barren-sweep gate remains the backstop: a `COMPLETE` process keeps draining and ticking, and leaves through the same gate — the success flavor of the one policy.

---

## The Papers

Gabriel Bracha, "Asynchronous Byzantine Agreement Protocols," *Information and Computation* 75, 130-143 (1987). Implemented in `bracha87.[hc]`.

Michael Ben-Or, Boaz Kelmer, Tal Rabin, "Asynchronous Secure Computations with Optimal Resilience (Extended Abstract)," PODC '94, pages 183-192. Section 4 Figure 3 (Protocol Agreement[Q]) is implemented in `bkr94acs.[hc]`.

J. H. Saltzer, D. P. Reed, D. D. Clark, "End-To-End Arguments in System Design," *ACM Transactions on Computer Systems* 2(4), 277-288 (1984). Cited as the design rationale for placing the BPR (Bracha Phase Retry) retry at the protocol endpoint rather than in a lower transport layer.

`Bracha87.txt` is a companion summary of the Bracha 1987 paper: figures, rules, VALID set definitions, all lemma/theorem statements, and a mapping from lemmas to the tests that verify them (including the paper's Fig 1 echo-threshold typo, also documented at the rule table in `bracha87.h`).

`BKR94ACS.txt` is the line-by-line extract of BKR94 Section 4 used as `bkr94acs.[hc]`'s reference.

`SRC84.txt` is the relevant extract of the End-to-End paper used as the design citation for BPR.

## Design Rationale — Why These Three Papers

This library exists because the alternatives we evaluated all required machinery we did not want to depend on. The three papers above were chosen as the smallest combination that satisfies our constraints — authenticated multi-value agreement under fair-loss asynchrony, no trusted setup, no pairing-based crypto, no DKG, embeddable in C89 with no dynamic allocation. The choice was load-bearing on each constraint; this section names the alternatives and the reason each was rejected.

### Why Bracha 1987 for reliable broadcast

The reliability primitive we needed is authenticated reliable broadcast (RBC) at `n > 3t`, signature-free, setup-free.

Bracha's three-phase counting-threshold mechanism (initial / echo / ready) is the only RBC primitive we found that works at `n > 3t` with neither signatures nor a setup ceremony — authenticated point-to-point channels are sufficient. Equally important, the paper's module boundary is a crisp algebraic interface (Fig 1 is reliable broadcast, Fig 3 is VALID-set validation, Fig 4 is consensus), so each lemma applies per-module and the audit chain shown later in this document is possible. A signature-based RBC bundles cryptographic verification into the protocol logic and forecloses that decomposition.

### Why BKR94 for asynchronous common subset

The agreement primitive we needed is multi-value agreement on a common subset of `n-t` A-Casts, asynchronous, Byzantine-resilient, with every process symmetric — no process plays a distinguished role, so no machinery exists to elect, follow, or replace one.

BKR94 alone is the smallest piece that does ACS with no setup, no distinguished process, and no threshold cryptography — `n` Bracha Fig 1 instances feed `n` binary agreements, and the step-2 trigger ("`n-t` BAs decided 1, enter 0 in the rest") closes it out. We deliberately stopped at ACS: BKR94 itself continues to ASC (asynchronous secure computation, the MPC layer), but ASC has no caller in the stack we are building, and pulling it in would require a private-channels mesh that ACS itself cannot bootstrap.

### Why Saltzer-Reed-Clark 1984 for BPR placement

Something must close the gap between the paper's reliable-channel assumption and fair-loss datagrams (*Bracha Phase Retry* states the gap; BPR is our offered closure). The end-to-end argument (Saltzer/Reed/Clark 1984) is the principle that decides where: the reliability function should live at the layer that has the complete information needed to perform it correctly. For our reliability function — "deliver INITIAL/ECHO/READY despite a fair-loss network until the protocol no longer owes them" — the complete information lives in the Bracha state machine itself (`F1_INITIATOR`, `F1_ECHOED`, `F1_RDSENT`, plus the BKR per-process BA-decided byte). BPR is a retry that runs over exactly that state, with no parallel data structure and no timing predicate. Lower-layer wire optimizations (RSEC, batching, inter-shard delay) remain admissible under SRC84's performance carve-outs (P1, P2) — they tune retry frequency without taking on the correctness obligation.

### What we deliberately did not build

For comparison shoppers: the following are absent by design, not by oversight.

- **No DKG, no trusted setup, no threshold signatures, no pairing-based crypto, no verifiable random beacon.** Authenticated point-to-point channels (see *System Model*) are the entire setup.
- **No bundled coin source.** The library is coin-agnostic — both `bracha87Fig4Init` and `bkr94acsInit` take a `bracha87CoinFn` callback that the caller supplies. The bundled examples use a deterministic alternating coin (demo only); adversarial deployments are expected to supply their own. See *Coin Choice — Caller Responsibility*.
- **No transaction layer, no atomic broadcast wrapper, no application semantics.** BKR94 ACS outputs SubSet — the agreed set of at least `n-t` processes and their A-Cast values; what those bytes mean, and any ordering over them, is the caller's choice.
- **No run identity.** One `struct bkr94acs` is one run, and nothing the library outputs distinguishes runs on the wire. Keeping successive or concurrent runs apart on shared channels — and any rejoin or catch-up across runs — is application framing, like the rest of the wire format.
- **No partial-synchrony assumption.** The papers' correctness claims — safety, and probabilistic termination (see *Coin Choice — Caller Responsibility*) — are proven under arbitrary asynchrony; this library adds no timing assumption to them.
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
                                   N BA instances -> Fig1+Fig3+Fig4 each -> common subset
```

Per-figure contracts — rule tables, thresholds, state, and action semantics — are in the section banners and function documentation of `bracha87.h`; the ACS composition's are in `bkr94acs.h`. Two facts of the composition are kept here because no single entry point's documentation owns them:

The step-2 trigger is "n-t BAs decided with output 1," not "n-t Fig 1 ACCEPTs." The two coincide in benign runs but diverge under asynchrony or Byzantine scheduling, and only the decide-1 trigger satisfies Part A case (i) of the BKR94 Lemma 2 proof.

Every message's per-message discriminator — the Bracha87 type, the class, and (for a BA message) the binary value plus decision flag — packs bit-disjoint into a single byte, so an application's wire framer carries the whole discriminator in one byte and a BA message carries no payload at all. It matters because ACS is message-dense — N reliable broadcasts plus N binary BAs, each O(phases) of O(n^2) Fig 1 traffic — so a byte saved per message compounds across the run. The canonical bit layout (a packer contract, not a library serialization) is documented at the message-class defines in `bkr94acs.h`; the example framer, `example/bkr94acs.c`, follows it.

## Test Coverage

`make check` runs five test binaries that exercise the library at three different scopes. Each scope catches a different class of regression; together they form a defense in depth.

| Binary | Scope | What it catches |
|---|---|---|
| `test_predicates` | Algorithmic primitives (white-box) | `fig4Nfn`, `fig3IsValid`, and the Fig 3 cascade enumerated against a paper-direct subset-enumeration reference at n=4, t=1 — anchors the predicates beneath the DTC dispatch. |
| `test_bracha87` | Protocol white-box (bracha87) | Per-rule units, composed simulation, inline lemma/theorem assertions, Byzantine equivocation, post-decide preservation, BPR retirement invariants, the n≫3t regime; reads internal flags directly. |
| `test_bracha87_blackbox` | Protocol black-box (bracha87) | Header-contract drift: validity/agreement/totality, precise echo thresholds, the BPR retirement contract, array Retry — derived from `bracha87.h` and `Bracha87.txt` only. |
| `test_bkr94acs` | Protocol white-box (bkr94acs) | All-to-all simulation, step-2 trigger and post-decide-continuation regressions, BPR drop-convergence and Byzantine-silent canaries, EXHAUSTED handling; reaches into internal layout. |
| `test_bkr94acs_blackbox` | Protocol black-box (bkr94acs) | Header-contract drift: Lemma 2 Parts A–D, Input dedup (the invariant a progress counter rests on), Retry/quiescence under drop, EXHAUSTED, equivocating A-Caster — no `.c` reads. |

The white-box / black-box pairing surfaces a different class of bug at each layer. White-box catches internal-invariant regressions (a state-machine flag set wrong, a count left unbumped). Black-box catches API contract drift — header text and code behavior pulling apart over time. Recent contract-drift fix caught by the black-box suite: `bkr94acsAcastValue`'s ACCEPT-gate (header documented "0 if not yet accepted" but pre-fix returned ECHOED-stored bytes, exposing pre-Lemma-2 values to callers).

The black-box suites stay strict about scope: only `*.h`, paper-extract `.txt`, and the matching black-box-style sibling are read while writing tests. When a test fails, the contract sources alone determine whether to tighten the code or rewrite the comment.

## Correctness Audit

The audit story is a four-link chain from paper to running code, with one human inspection step (boundary I/O wiring) and one exhaustive test step (the algorithmic predicates). The chain establishes that the code implements the papers' rules; the rules' correctness at general (n, t) is the papers' claim — read the papers, not this repository, for those proofs.

```
paper rules            <-> .dtc files                human, rule-by-rule comments
.dtc files              -> compiled dispatch         dtc, exhaustive/exclusive
C wrapper boundary I/O                               human inspection
fig3IsValid, fig4Nfn, Fig 3 cascade                  test/test_predicates.c —
                                                     exhaustive enumeration vs
                                                     paper-direct reference at
                                                     n=4, t=1
```

The decision-table layer (`*.dtc`) is paper vocabulary, rule-by-rule commented with the paper's rule numbers. `dtc` enforces exhaustiveness and exclusivity at compile time and outputs depth-optimal dispatch (for `dtc`'s own verification story, see the decisionTableCompiler repository's README). The C wrapper sits below the dispatch and is one line per boundary input/output — each line is either a flag/count/bit-test mapping or a boolean-to-side-effect; small enough to read.

The two algorithmic predicates that the dispatch delegates to — `fig3IsValid` (recursive existential), `fig4Nfn` (case analysis with permissive D_FLAG encoding) — and the Fig 3 cascade (iterative re-validation) are the only places where search/recursion/iteration sits below the bridge. They are anchored by `test_predicates.c`: 960 `fig4Nfn` inputs, 165 `fig3IsValid` evaluations, 4 cascade delivery permutations, all at n=4 t=1, against a paper-direct subset-enumeration reference. All agree.

`fig3IsValid` is paper-correct **given a caller's N that exposes the existential subset quantifier via `rc > 0`**. The Fig 3 dispatch invokes N once on the full validated set; N's responsibility is to answer "could some n-t subset legitimately produce this value?" If a caller supplies an N whose permissive return is suppressed, `fig3IsValid` correctly rejects values the paper definition would admit via a strict subset. `fig4Nfn` is the canonical N for Fig 4 and exposes the existential analytically; the 960-input correspondence test against paper-direct subset enumeration anchors that delegation. The two predicates verify each other transitively: `fig4Nfn` ↔ paper at all bounded inputs, and `fig3IsValid` ↔ paper *given* that delegation.

## Implementation Notes

Each item below is a paper-vs-code divergence that any from-scratch implementation will encounter. We caught them by reading the paper rule-by-rule against composed-simulation runs and against fair-loss retry; isolation testing missed almost all of them, because the divergences only manifest under multi-figure interaction or under network conditions that simulated reliable channels never produce. They are the cost of building this from the papers — listed here so a porter does not pay it twice, and so a reader evaluating "should I trust this implementation?" can see what was actually verified and what regression test catches each one.

1. **Post-decide continuation.** The paper says "Go to round 1 of phase i+1" after all three step 3 cases. A decided process must continue broadcasting so others can reach consensus. `BRACHA87_DECIDE | BRACHA87_BROADCAST` is returned exactly once; subsequent rounds return `BRACHA87_BROADCAST` only.

2. **D_FLAG leak.** After deciding, step 2 may set the D_FLAG on the value. Step 3's decided path restores the plain decision value to prevent D_FLAG from leaking into step 1 broadcasts of the next phase.

3. **N function existential quantifier.** The paper defines VALID^k with "there exist n-t messages..." Passing only the first n-t to N rejects messages that a correct process produced from a different subset. Fix: pass all validated messages; N returns permissive when subsets could disagree.

4. **Dead cascade after INITIAL.** The cascade after INITIAL could never fire -- if any threshold were met, `echoed` would already be set via Rule 2/3. Removed; comment explains the proof.

5. **Echoed value memcpy.** The memcpy on Rules 4/5/6 appears redundant but is essential. A Byzantine initial can store the wrong value first; the memcpy corrects it when the threshold-reaching value differs from the echoed value.

6. **Subset-majority reachability threshold (step 1).** Under N's tie-break-to-0, value 0 is reachable in some n-t subset iff `cnt[0] >= (nt+1)/2` (unified formula: equals `nt/2` for even n-t, `nt/2+1` for odd); value 1 is reachable iff `cnt[1] >= nt/2+1` (strict majority). Permissive iff both reachable. Using the symmetric `>= nt/2+1` test on both sides wrongly rejects honest tie-subset 0s when n-t is even. Verified by exhaustive enumeration for n=4..16.

7. **Forward cascade fires on every growth past n-t, not only first crossing.** `VALID^r_p` is existential over n-t subsets of `VALID^{r-1}_p` and monotone in it (paper definition + Lemma 6), so new validated messages at round k unlock stored unvalidated messages at k+1 even after round k first reached n-t. Gating the forward re-check on "first crossing only" strands honest round-(k+1) messages when validation of them depended on subsets that only exist after k grew.

8. **Permissive D_FLAG permission conveyed via `*result`.** On permissive return from Fig 4's N function (`rc > 0`), `*result & BRACHA87_D_FLAG` is set only when some n-t subset legitimately produces a decision candidate. Fig 3 rejects incoming D_FLAG when that bit is clear, preventing Byzantine d-injection in the no-majority windows of step 3 cases 1 and 2.

9. **Post-decide value preservation across sub-rounds.** During post-decide continuation (Note 1), `b->value` is preserved as the decision through every sub-round of subsequent phases. The .dtc-faithful Fig 4 dispatch zeroes the `setMajority` and `setDMajority` outputs when `have_decided = yes`, so adversarial inputs whose majority disagrees with the decision cannot drift the broadcast value away from it. Verified by `testFig4PostDecideAdversarial` (which would have failed against a pre-DTC version that overwrote `b->value` with majority/(d, majority) at sub-rounds 0 and 1 of post-decide phases).

10. **BPR (ready, v) retry must NOT short-circuit on accepted.** An accepted process owes its READY to processes still below the 2t+1 threshold; retry is the only mechanism Bracha provides for getting it there under loss. The "ACCEPTED → stop retrying READY" optimisation strands slow processes — READY is exactly what the ready-amplification tail consumes. This is the asymmetry with Note 11: ACCEPTED *does* retire INITIAL and ECHO (they are bootstrap-only) but must *never* retire READY. Regression check: `testFig1Bpr` post-accept assertions (READY survives; INITIAL/ECHO retired).

11. **BPR (initial, v) / (echo, v) retry: retire only on a stop strictly stronger than local-echo.** INITIAL and ECHO are bootstrap-only — they drive the system to the point where `t+1` correct processes have sent READY, after which amplification is self-sustaining and consumes neither. Two stops are sound and minimal: (a) **ACCEPTED** — accept requires `2t+1` readys, ≥ `t+1` of them correct and retried forever, so every correct process reaches accept on readys alone; this retires both INITIAL and ECHO. (b) **all-echoed** (`echoSenders == n`) — INITIAL only induces echoes, so if every process has echoed there is nothing to induce; this retires INITIAL. The *forbidden* gate is the weaker "stop INITIAL once we ECHOed locally": at the n = 3t+1 boundary the (n+t)/2 + 1 echo threshold equals the honest count, so at local-echo time no readys may exist yet, the rescue set is not established, and a process that missed the bootstrap can be left one echo short forever — only the initiator breaks it. ACCEPTED and all-echoed are both strictly stronger than that gate. Under ≤t byzantine-silent processes `echoSenders` cannot reach `n`, so that path self-disables and ACCEPTED carries the retirement. Regression checks: `testBprByzantineSilent` (n=4 t=1, one silent Byzantine process — converges in 1 sweep; the original `!ECHOED` gate stalled at |SubSet|=1 over 50000+ sweeps) and `testFig1Bpr` all-echoed assertions (echoSenders==n retires INITIAL without accept).

12. **Fig 4 EXHAUSTED means no new phase/round; no unilateral substitute at the BKR94 layer.** When `bracha87Fig4Round` returns `BRACHA87_EXHAUSTED` (probabilistic termination did not converge within the unsigned-char round encoding's 85-phase ceiling), the local BA has no decision. BKR94 Lemma 2 Part B's "all BAs terminate" assumption is violated, and Part C (SubSet agreement) is unrecoverable locally — any unilateral substitute (decide 0 or 1) could disagree with another process's actual decision (different local-coin sequence or message ordering). The library surfaces `BKR94ACS_ACT_BA_EXHAUSTED` and marks the affected process's BA state as exhausted (`bkr94acsBaDecision(acs, process)` returns 0xFE thereafter); the 0xFE sentinel does not match the decided-count scan, so an exhausted BA never counts as decided (no decision was made) and `BKR94ACS_F_COMPLETE` stays clear in `acs->flags`. The application surfaces this as the run's failure cause and exits through its abandonment policy (see *Abandonment*). BPR continues retrying for that process so other processes may still benefit from earlier-round echoes/readys. EXHAUSTED is mutually exclusive with DECIDE per Fig 4 semantics, so single output is structural — no dedup guard needed. Regression check: `testExhausted`.

13. **READY's only sound retire is *remote* all-accepted — never *local* accept (the per-process refinement of Note 10).** Per-process suppression and the all-`n`-accepted READY quiescence gate both read PROCESSES' accepts, announced via the `BKR94ACS_ACCEPTED` wire bit and recorded in `acFrom` — not the local `ACCEPTED` flag that Note 10 forbids. Per-process: skip READY to a process once *it* has accepted (its Rule 6 fired on 2t+1 readys, so it consumes no further ready). Whole-action: stop outputting only when *all* `n` have accepted. Two byzantine-safety facts are load-bearing: a forged `ACCEPTED` marks only its own sender, so it retires our retry to that liar but can never strand a correct laggard (whose bit is set solely by its own true accept); and reaching the all-`n` gate requires every *correct* process's true accept regardless of what the ≤t Byzantine processes claim. Do **not** add a `≥2t+1 accepted → stop` threshold shortcut — Byzantine forgeries could trip it while a correct process is still below 2t+1 readys. Regression checks: `testFig1SkipAccept`, `testBprSkipAccept`, and `runWithRetry` drop-convergence with suppression active.

14. **INITIAL must come from the designated initiator — `from == process` (A-Cast) / `from == initiator` (consensus) is enforced, not assumed.** A Fig 1 instance is keyed to ONE designated initiator. Only that initiator may send `(initial, v)`; ECHO and READY arrive legitimately from any process (`from != initiator` is normal for them and is sender-deduped). A non-initiator INITIAL is a *forged broadcast* — a Byzantine process injecting a value the correct initiator never sent — and because Rule 1 echoes the first INITIAL unconditionally, an attacker reaching every correct process drives the `(n+t)/2+1` echo cascade to a false ACCEPT, violating reliable-broadcast validity. Authenticated channels do **not** close this: they bind `from` to the true sender but not the message's *claimed* initiator (initiator ≠ from is a valid ECHO/READY), so the binding is a protocol-semantic check, not a transport one. `bkr94acsAcastInput` / `bkr94acsBaInput` drop the message when `type == BRACHA87_INITIAL && from != process/initiator`. The bare `bracha87Fig1Input` cannot self-enforce (it is not told its own initiator index — see the INITIAL-sender obligation in its header doc), so a direct bare-layer caller must filter before calling — `example/bracha87Fig1.c` shows the filter in its delivery loops. This trap is invisible to honest-only tests: every honest generator sends INITIALs with `from == process`, and even an equivocating *initiator* still has `from == process` — so nothing exercised the forged-non-initiator path until it was added explicitly. Regression checks: `testForgedInitial` (white-box) and Section A's forged-INITIAL contract case (black-box).

---

## Re-Implementing in Another Language

A port that wants to preserve this library's correctness story has two pieces of machinery to either reproduce or replace:

1. The **decision-table compilation pipeline** (described below), which lifts paper rules into depth-optimal dispatch.
2. The **trap list and predicate corpus** for cross-checking the result. Implementation Notes #1–#14 above are paper-vs-code (and model-precondition) traps; `test/test_predicates.c` is the exhaustive paper-direct reference for the algorithmic predicates that sit below the dispatch.

### Paper-Faithful Dispatch via DTC

Each module's per-call decision logic is captured in a CSV decision table written in the paper's vocabulary (`bracha87Fig{1,3,4}.dtc`, `bkr94acs.dtc`). A small bridge per module (`*ToC.dtc`) maps domain names and values to C identifiers and constants. The decisionTableCompiler (`../decisionTableCompiler/dtc`) co-compiles each pair to an optimal-depth pseudocode dispatch, which a local `psu.awk` translates to a C snippet the entry-point function `#include`s.

| Source | Bridge | Generated snippet | Entry point | Depth |
|--------|--------|-------------------|-------------|-------|
| `bracha87Fig1.dtc` | `bracha87Fig1ToC.dtc` | `bracha87Fig1.c` | `bracha87Fig1Input` | 7 |
| `bracha87Fig2.dtc` | (none — Fig 3 subsumes) | — | — | — |
| `bracha87Fig3.dtc` | `bracha87Fig3ToC.dtc` | `bracha87Fig3.c` | `bracha87Fig3Accept` | 4 |
| `bracha87Fig4.dtc` | `bracha87Fig4ToC.dtc` | `bracha87Fig4.c` | `bracha87Fig4Round` | 6 |
| `bkr94acs.dtc` | `bkr94acsToC.dtc` | `bkr94acsRules.c` | both bkr94acs entry points (one snippet, two `#include`s) | 7 |

`dtc` enforces exhaustiveness and exclusivity of the rules at compile time. Depths are full-optimum (full search confirms each is depth-minimal for its boundary-input set). The C wrapper computes boundary inputs, `#include`s the dispatch, and applies the boolean outputs as side effects in an order that is the API contract (e.g. for Fig 1: `echo` before `ready` before `accept`). See `decisionTableCompiler/README.md` for the bridge mechanism.

A re-implementation that does not want a DTC dependency can transcribe the dispatch by hand from each `.dtc`'s rule table — the `.dtc` files are the readable source of record, and the generated `.c` snippets are large nested `if`/`switch` ladders that a competent developer can read directly. The constraint is that the transcription must preserve exhaustiveness and exclusivity (every input combination has exactly one matching rule), which `dtc` proves at compile time and a hand-port must prove by inspection.

### Where to start

- **`Bracha87.txt`** and **`BKR94ACS.txt`** are the paper extracts. Start here.
- **`bracha87Fig{1,3,4}.dtc`** and **`bkr94acs.dtc`** are the paper-vocabulary decision tables, rule-by-rule commented to the paper. These are the API contract for the dispatch.
- **`test/test_predicates.c`** is the paper-direct reference for `fig3IsValid`, `fig4Nfn`, and the Fig 3 cascade — exhaustive enumeration at n=4, t=1. A port should pass this corpus.
- **`test/test_bracha87.c`** and **`test/test_bkr94acs.c`** are the integration-test corpus, including the regression checks named in Implementation Notes #9–#14.
- **Implementation Notes #1–#14 above** are the traps. Each one names a specific paper-vs-code divergence and (where applicable) the regression test that catches it.

## License

LGPL v3 or later. See `COPYING.LESSER` and `COPYING`.
