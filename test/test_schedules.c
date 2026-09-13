/*
 * test_schedules.c
 *
 * A bounded reachability explorer over the state graph of the two
 * example application loops, under adversarial delivery order and
 * delay.  BKR94ACS.txt's adversary "schedules the delay of every
 * message"; this walks every such schedule inside printed bounds and
 * checks the papers' lemmas, the header contracts, and the repo's
 * ending claims at every state it reaches.
 *
 * WHAT IT CAN FIND, and what it cannot.  Within its printed bounds it
 * finds stranding (no schedule reaches quiescence), agreement
 * violation, act-contract violation, and ending-claim violation under
 * honest adversarial scheduling; and, on the adversary configs (THE
 * ADVERSARY below), a violation of the papers' properties by a
 * well-formed Byzantine content within the bounds it prints.  It does
 * NOT touch the Byzantine-safety arguments for the READY retire gates
 * (README Implementation Note 13) -- those are the annotation forgery
 * tests' work.  Deliberate non-goals: message loss, patience above
 * zero, and coin branching.  The coin here is the examples'
 * deterministic phase%2, so every process gets the same value in
 * Bracha Fig 4 case (iii) and exhaustion requires the schedule to
 * split case (ii)/(iii) every phase.
 *
 *
 * THE TWO SURFACES
 *
 * "Instance" throughout means a Fig 1 instance and nothing else; a
 * whole ACS state is called an ACS state, and the fixed allocation
 * holding one is called its image.
 *
 * SURFACE 1 is the bare Fig 1 array loop of example/bracha87Fig1.c:
 * one Fig 1 instance per process, one designated honest initiator,
 * the caller-side forged-INITIAL filter, wire bits 4/5 framing,
 * ingress ordered Input -> ProcessAccepted (a READY with bit 4) ->
 * ProcessResend (a READY without bit 5) with rotation re-entry, and
 * the self-accept recorded at ACCEPT.
 *
 * SURFACE 2 is the ACS loop of example/bkr94acs.c: the bkr94acsAcast
 * bootstrap, both Input classes with the canonical packed-byte
 * framing, bkr94acs*Accepted / bkr94acs*Resend routing with the same
 * re-entry.
 *
 * The HEADERS define the contract; the examples DEMONSTRATE it and
 * are downstream of bracha87.[hc] / bkr94acs.[hc].  What this
 * explores is the headers' application-loop contract in the shape
 * the examples demonstrate; where a demonstration diverges from its
 * header, the header stands and the demonstration is the defect.
 * The patience clock's unit is the FULL SWEEP -- one complete pass
 * of the Retry cursor (bkr94acs.h) -- and at the zero patience this
 * instrument runs, the clock is inert either way: the call gate
 * does not read the counter's history (see THE ELIMINATED
 * COUNTERS below).
 *
 * THE TWO SURFACES ARE ASYMMETRIC, deliberately, because the loops
 * are:
 *
 *   - example/bracha87Fig1.c skips a quiescent process ENTIRELY
 *     ("if (!fig1[i] || quiescent[i]) continue"), so surface 1 gates
 *     the WHOLE tick on rotation membership.  example/bkr94acs.c
 *     wraps only the retry block in "if (!quiescent[i])" and runs the
 *     turns and the fanout for every process every sweep, which is
 *     exactly why it needs to re-enter a quiesced process whenever a
 *     turn or a fanout produces acts.  So surface 2 gates only the
 *     retry sub-step.
 *   - example/bracha87Fig1.c re-initializes the cursor inside the
 *     tick over a ONE-element array, so one bracha87Fig1RetryStep
 *     call IS a full pass.  Surface 2's cursor is persistent per
 *     process (retry[i]) and one bkr94acsRetryStep call advances ONE
 *     cursor position over a space of N + N*(3*maxPhases)*N.  That is
 *     the whole reason the tick allowance below differs by two orders
 *     of magnitude between config 1 and config 3.
 *
 *
 * THE EVENT MODEL
 *
 * From a state, exactly one of:
 *
 *   deliver(m)  remove one copy of pending content m and run the full
 *               ingress composite atomically.  The branch is over
 *               DISTINCT CONTENTS only: the pool is a multiset keyed
 *               on the full content, ingress is a deterministic
 *               function of (state, content), so which copy is
 *               removed cannot matter.  Copies are NOT coalesced --
 *               ProcessAccepted is idempotent but ProcessResend is
 *               one-shot-per-egress, so k copies are not one copy.
 *   tick(p)     the surface's sweep-tick composite, atomically.  A
 *               caller's loop body is straight-line code, so no
 *               deployment can interleave a delivery inside it.
 *   acast(p)    surface 2 only, and only where a config defers it:
 *               process p's A-Cast SUBMISSION as a schedulable event.
 *               BKR94ACS.txt -- "Each player enters his inputs
 *               asynchronously as evidence accumulates" -- and the
 *               example's -d flag is exactly a deferred submission.
 *
 * The content key carries every field ingress reads.  Surface 2:
 * class, process, round, initiator, type, the ACCEPTED bit, the
 * RECEIVED bit, from, to, the BA binary value with its D_FLAG, and
 * one value bit.  The RECEIVED bit is computed PER RECIPIENT at
 * expansion (struct bracha87Fig1Act.received), not per act, and is
 * part of the key.  The A-Cast value bytes are a function of the
 * A-Cast's process and the value bit: an honest process A-Casts one
 * value, and the bit names the ONE foreign symbol an adversary can
 * put in its place -- the alphabet is two symbols per instance, the
 * grammar comment in main says why -- so the framing region asserts
 * that every honest act's value is one of the two rather than
 * assuming it.
 *
 *
 * THE ADVERSARY
 *
 * BKR94ACS.txt's adversary schedules every message; Bracha87.txt's
 * faulty processes also SAY things.  The adversary configs give one
 * of the n an authenticated identity and nothing else: it holds no
 * library image, every honest message to it is dropped at push, it
 * never ticks, and it speaks only what its STRATEGY seeds into the
 * pool at the root.  It chooses content (value, BA value, and at
 * surface 1 the two annotation bits -- surface 2 seeds one setting,
 * the grammar comment in main says why), the fields naming a third
 * party (process, round, initiator), and -- through the explorer
 * above, as far as its ceiling reaches -- timing, order and silence.
 * It does not choose `from`: that is the authenticated channel, and a
 * forged INITIAL is not a strategy here but a byte the library drops
 * (bkr94acs{Acast,Ba}Input) or the surface-1 caller filter drops.
 *
 * ROOT-SEEDED, NOT INJECTED.  A strategy is a subset of at most J
 * contents from the grammar (one instance's part of it), all pending
 * from the root; the inner explorer is then free to vary when each
 * lands against the honest traffic -- free in the state graph, and
 * as far as the ceiling reaches in the search, which at these
 * ceilings is not far: see "one timing" in the grammar comment.
 * Bracha87.txt's scheduler "determine[s] in each round for each
 * process which n - t messages it receives", and BenOr83.txt's
 * adversary "knows all about the system": content fixed at the root
 * plus explored delivery is that adversary, because a content's
 * effect on a Fig 1 is through per-sender, per-value counts at its
 * receiver, and those do not depend on what the adversary saw.  An
 * adversary EVENT inside the DFS would instead be cut by the ceiling
 * before its root-level alternatives were tried: a sibling event
 * placed after the honest deliveries of the first dive is never
 * explored, and the ceiling-bound counts would then be a prefix over
 * the adversary's CONTENT as well as its timing.  Every strategy runs
 * its own explorer under the config's ceilings, and EVERY COUNT THE
 * CONFIG PRINTS IS A SUM over the strategies it enumerated, with the
 * strategy count printed beside K.  The bounds and the defect each
 * gives up are in the grammar comment in main; J is a knob like K.
 *
 * The papers are the ORACLE here, not the subject: their algorithms
 * are assumed correct up to t, so a red on a paper-named check under
 * Byzantine content is this implementation deviating from what they
 * prove, never a finding about them.
 *
 * WHAT THE ORACLE SAYS UNDER IT is scoped to the honest processes and
 * named by the papers (Bracha87.txt, BenOr83.txt, BKR94ACS.txt):
 *
 *   surface 1, adversary initiator (b1): Theorem 1 property 2 --
 *     all correct processes agree on a value or none accepts.  Lemma
 *     2 pairwise at every transition is the "agree" half; the "or
 *     none" half is eventual and is not asserted.  Lemma 1 pairwise
 *     among honest READY senders.  QUIESCENT needs the adversary to
 *     announce to all three besides broadcasting, J = 6, so at J = 4
 *     no b1 strategy reaches one; the witness there is every honest
 *     process accepted.
 *   surface 1, honest initiator (b2): Theorem 1 property 1 -- every
 *     honest accept carries the initiator's value (Lemma 4's value
 *     half), and no honest echo carries any other (Lemma 1's
 *     threshold argument: more than (n+t)/2 echoes or t+1 readys of a
 *     value the initiator never sent would need an honest first
 *     sender of it).
 *   surface 2 (b3, the adversary's own broadcasts; b4, its echoes and
 *     readys on the honest ones): Lemma 2 on every A-Cast (accepted
 *     values agree), Theorem 2 on every BA (honest decisions agree),
 *     C1 -- BenOr83's validity, which Theorem 2 inherits -- once every
 *     honest process has entered one value into a BA no honest
 *     decision on it is the other, all at every transition; and at
 *     every completion BKR94
 *     Lemma 2 Parts A, C and D: |SubSet| >= n-t (A), SubSet agreement
 *     (C), and D -- a SubSet member's BA was entered
 *     1 by an honest process holding its accepted A-Cast.  Part D is
 *     the one the hand-picked arms never reach end to end: it is the
 *     check that a BA decided 1 on the adversary's say-so alone.
 *     Part B (every BA terminates) is the per-strategy completion
 *     witness: every strategy of b3 and b4 completes, measured, and
 *     the config asserts it.
 *
 * WHAT QUIESCENCE MEANS UNDER IT.  A READY retires only when every
 * process has announced its accept and holds this one's
 * (bracha87.h's retry banner), so an adversary silent on an instance
 * holds every honest READY on it open forever.  At surface 1 a
 * strategy reaches QUIESCENT only if it announces to every honest
 * process; at surface 2 the adversary is silent on every instance but
 * the one its strategy names, so QUIESCENT is unreachable by
 * construction and the reachability witnesses there are completion,
 * |SubSet| = n (the adversary A-Cast honestly and is in), and
 * |SubSet| < n (the adversary is out).  Surface 1's witness is every
 * honest process accepted.  None of this is a defect: it is the
 * honest residue the header names, and the measured fact that
 * silence is the cheapest strategy.
 *
 * SENSITIVITY, NOT DETECTION.  A count that moves under a strategy is
 * a story; a paper property that fails is a finding.  EXHAUSTED under
 * an adversary is sensitivity: the coin is the deterministic phase%2,
 * README says that coin is unsafe against an adaptive adversary, and
 * maxPhases is a knob.  The frozen counts on the adversary configs are
 * sums and regression constants like every other count here.
 *
 * The pool holds COPIES of act values (here, the one-byte values
 * indexed by process).  struct bkr94acsAct.value is a borrowed
 * pointer that the next mutating library call invalidates
 * (struct bkr94acsAct.value); do not "optimize" the copy away.
 *
 *
 * STATE, SNAPSHOT, AND THE VISITED SET
 *
 * A state is the library images, the harness rotation and quiescence
 * flags, the pool multiset, the per-process remaining tick allowance,
 * the surface-2 persistent retry cursors, and the pending A-Cast
 * submissions.
 *
 * SNAPSHOT AUDIT.  Library images live at FIXED allocations for the
 * whole run and snapshot/restore is byte-copy IN PLACE ONLY.  This is
 * what makes byte comparison sound: bracha87Fig4Init passes the
 * enclosing Fig 4 as the embedded Fig 3's N-closure
 * (bracha87Fig4Init, dereferenced as a Fig 4 in fig4Nfn),
 * and every BA's Fig 4 is carved out of the ACS state's own data[]
 * tail, so a bkr94acs image holds pointers into itself.  With the
 * allocation fixed those words are per-process constants for the run,
 * so process i's image is only ever compared against process i's
 * image and the pointer words contribute a constant.  No masking is
 * needed, and the EQUALITY RELATION -- hence every count below -- is
 * address-independent even though the addresses are not.
 *
 * FORBIDDEN, and the reason the restore is a memcpy back: a snapshot
 * buffer's pointer words are stale with respect to its own location.
 * Never cast a snapshot buffer to struct bkr94acs * or struct
 * bracha87Fig4 * and pass it to a library entry, and never hand one to
 * anything that walks it as a struct.  Fingerprints read bytes only.
 *
 * Byte comparison OVER-SPLITS (two states differing in a byte the
 * machine no longer reads are counted twice) but can never
 * UNDER-MERGE, since equal bytes at a fixed address is equal state.
 * Over-splitting costs states and never correctness, so it is the
 * safe direction; do not "improve" this into a semantic fingerprint.
 * Every Init memsets its whole Sz extent (every bracha87Fig*Init, and
 * bkr94acsInit), so padding is deterministic.
 *
 * The visited set is MEMBERSHIP-ONLY.  Nothing iterates it and no
 * printed count depends on bucket order -- the rule covers the hash
 * table itself, not only the pointer values inside a state.
 *
 * THE ELIMINATED COUNTERS.  The example's turnSweeps / fanoutSweeps
 * carry NO information at zero patience and are absent from the state
 * here.  The example computes, per (ACS state, BA):
 *
 *   if (duty == TOLERANCE) { if (sweepDone) ++turnSweeps; }
 *   else turnSweeps = 0;
 *   if (duty == MET || turnSweeps >= patience || BaDecision != 0xFF)
 *     call the turn;
 *
 * At patience 0 the compare is >= 0, constant-true for an unsigned
 * count: the turn is called on every attempt no matter what the
 * counter holds, so neither the update order nor the duty can route
 * through it.  Turn firing then depends on duty alone -- the library
 * fires whenever the duty is not HELD -- and the fanout's own >= 0
 * gate likewise always calls into bkr94acsFanout's internal duty
 * guard.  At
 * n = 4 the two counters would be n*n + n = 20 harness bits, a factor
 * of 2^20 on the state space of the config whose tractability is in
 * question.  THE ELIMINATION IS ZERO-PATIENCE-ONLY: patience above zero
 * revives them as live state, where capping each at patience + 1 (a
 * bisimulation, since the capped domain is closed under both the
 * increment and the reset) is the sound form.
 *
 *
 * BOUNDS -- frozen per config and PRINTED, never silent
 *
 *   K            the tick allowance per process.  Exhaustive means
 *                exhaustive over schedules with at most K ticks per
 *                process, and nothing more.  Measured per config, not
 *                inherited: surface 1 at n=2 t=0 needs 3 ticks per
 *                process (announce, marked re-send, observe the
 *                retire), while surface 2's floor is a full
 *                retry-cursor pass, N +
 *                N*(3*maxPhases)*N calls -- 52 at n=4 maxPhases=1.
 *   maxPhases    surface 2, small, and A KNOB.  Whether the EXHAUSTED
 *                class is empty is a reading of this knob, never a
 *                protocol fact.
 *   ceilings     caps on states and on descent depth, LOUDLY reported
 *                when hit.  A ceiling voids the exhaustive-within-K
 *                claim for that config and says so on the line it
 *                prints.
 *
 * EVERY CONFIG HERE IS CEILING-BOUND.  The smallest configuration --
 * surface 1, n=2, t=0, two Fig 1 instances and ten root contents --
 * CLOSES at K=3: 84,708,681 distinct states, 632,200,620 edges, 135 s
 * and a 1.5 GB visited table (-c 200000000 -b 27), so its 4,000,000
 * ceiling is a budget, and the closed graph's four QUIESCENT terminals
 * are the same four the ceiling-bound prefix finds.  No larger config
 * has been run to closure.  What grows the space is the pool: each
 * tick pushes fresh copies of contents whose count had dropped, and a
 * duplicate unmarked READY re-arms its sender after an egress consumed
 * the previous arm (bracha87Fig1ProcessResend), so copies are semantically
 * live and cannot be coalesced.  What a ceiling costs is the ONE
 * property a completed search would have had: the counts below are a
 * deterministic PREFIX under the branch order, so they are regression
 * constants at every config, not order-independent facts.  Keying the
 * allowance still buys the tiny configs a search-order-independent
 * state IDENTITY -- it is only the cut that the order decides.
 * What a ceiling does NOT cost: every assertion that fired, fired on a
 * real reachable state, and every reachability witness found is
 * found.  Only the "no schedule does X" direction weakens, and the
 * report prints the state count at which the clean machine first
 * reached a QUIESCENT terminal so a red arm can be read against it.
 *
 * The pool grows or aborts; it never truncates.  A silent drop would
 * fake the very silence quiescence is read from -- the hazard
 * example/bracha87Fig1.c and example/bkr94acs.c work
 * around with their queue-index reset.
 *
 * THE TICK ALLOWANCE AND THE VISITED SET.  At the tiny configs the
 * allowance vector is part of the state KEY, so a state's IDENTITY
 * does not depend on the order it was reached in.  At the large-K
 * configs it cannot be: the
 * allowance space is on the order of K^n, so the rule there is PARETO
 * DOMINANCE -- re-expand a re-reached state UNLESS some prior visit
 * carried an allowance vector that dominates the new one
 * componentwise, keeping a Pareto frontier per state key.  What
 * licenses that is a property of THIS system and not a generality:
 * the allowance is harness-only state that no library call reads and
 * that gates nothing but tick(p) enablement, so more allowance never
 * disables an event and never alters a transition's effect, which is
 * what makes a dominating prior visit's successor set a superset.
 * Under dominance even the state identity depends on the order states
 * are first reached.  Either way, and for the ceiling reason above,
 * EVERY frozen count here is a REGRESSION CONSTANT rather than a
 * property of the system: a branch-order or ceiling change
 * re-baselines them and has not found anything.
 *
 * BRANCH ORDER (deterministic, and load-bearing for config 3b):
 * deliveries in ascending content-key order, then ticks LEAST-TICKED
 * FIRST (which makes the first dive's ticks the examples' fair
 * round-robin sweep -- process order starves every process past the
 * first), then A-Cast submissions in process order.  Submissions
 * LAST is what puts the honest-exclusion witness on the first dive.
 *
 *
 * TERMINAL CLASSES -- explicit precedence, so they partition
 *
 *   1. EXHAUSTED   any BA reported BA_EXHAUSTED.
 *   2. QUIESCENT   every in-rotation process quiescent AND the pool
 *                  empty, and no BA exhausted.
 *   3. ALLOWANCE-EXHAUSTED   no event enabled within the bounds.
 *                  Counted and reported, NEVER asserted against: a
 *                  schedule that ran out of ticks is a bound, not a
 *                  defect.
 *
 * Without the precedence the classes overlap -- a state with BA_2
 * exhausted, every process quiesced and the pool empty satisfies
 * QUIESCENT's predicate, and the QUIESCENT battery's "all complete"
 * and "no 0xFE anywhere" would then be false on a CORRECT machine.
 *
 *
 * THE ORACLE
 *
 * At every transition, schedule-independently:
 *
 *   - act-count bounds: A-Cast input <= 3, BA input <= 2, turn <= 3,
 *     retry <= BKR94ACS_RETRY_MAX_ACTS, fanout <= N, where N is the
 *     ACTUAL process count.  BKR94ACS_MAX_ACTS writes the fanout
 *     bound as "N = n + 1" because n is the ENCODED byte; asserting
 *     against the encoded n would be one too few and would fire on a
 *     full fanout.
 *   - the terminal acts emerge ONLY from a turn: BA_DECIDED,
 *     BA_EXHAUSTED and COMPLETE from bkr94acsBaInput would be a
 *     contract violation (bkr94acsBaInput's contract).
 *   - duty monotonicity for bkr94acsFanoutDuty ONLY (MET absorbing,
 *     TOLERANCE never back to HELD).  A dedicated opening-carries-
 *     an-act clause (FanoutDuty HELD -> TOLERANCE implies a
 *     BA_DECIDED in the transition) was tried and REMOVED 2026-08-23:
 *     at maxPhases = 1 -- every standing surface-2 config -- a
 *     deciding turn under the co-emission defect returns 0 acts, so
 *     the act-derived turned-tracking sees a phantom within-round
 *     regression and reds FIRST at every such config; the clause is
 *     structurally shadowed and could never witness its own red
 *     here.  The defect's designated detector is the black-box F4
 *     co-emission check (mutants.sh M34); this instrument co-detects
 *     it through the fell-back clause, a witnessed fact.  bkr94acsTurnDuty is NOT
 *     monotone by design -- it classifies the BA's NEXT round, and a
 *     turn advances the round so the count legitimately restarts
 *     (bkr94acsTurn's contract) -- so the sound schedule-independent form
 *     is the WITHIN-ROUND one, asserted only across transitions in
 *     which that BA turned no round.  Both duties are functions of
 *     the image, so the pre/post pair across one transition is the
 *     whole monotonicity chain and nothing is stored in the state.
 *   - acFrom \ {self} subset of rdFrom after every ingress.  The
 *     naive subset is FALSE: the self-accept is recorded at ACCEPT
 *     with the local index and no rdFrom guard
 *     (bracha87Fig1ProcessAccepted), while the process's
 *     own (ready, v) can still be pending in the pool.
 *   - the RECEIVED mask is present only on a READY act
 *     (struct bracha87Fig1Act.received, struct bkr94acsAct.received).
 *   - HARNESS SELF-CHECKS, labeled as such because they prove framer
 *     discipline and not library behavior: a marked READY is never
 *     routed to *Resend, and the wire RECEIVED bit is set for
 *     recipient p only where the RECEIVED mask says so.  The second
 *     would be an identity at the library layer anyway --
 *     bracha87Fig1Received RETURNS acFrom.
 *   - single input per BA: bkr94acsBaEntered latched once entered.
 *   - Bracha Lemma 1 (surface 1): under an honest initiator every
 *     instance whose echoed value exists carries the initiator's
 *     value; and pairwise, any two honest READY senders carry the
 *     same value -- the form that is live under an adversary
 *     initiator.
 *   - Bracha Lemma 2 (surface 1): any two accepts of one broadcast
 *     agree.  Surface 2: the same on every A-Cast, over the
 *     ACCEPT-gated bkr94acsAcastValue.
 *   - Bracha Theorem 2 (surface 2), per BA: honest decisions agree.
 *   - C1 (surface 2), per BA: once every honest process has entered
 *     one value, no honest decision is the other.  The entered value
 *     is read off the image -- the INITIATOR slot of the round-0 BA
 *     Fig 1 this process initiated -- not tracked.
 *
 * At every completion (surface 2, every honest process complete):
 *
 *   - SubSet agreement and |SubSet| >= n-t (BKR94 Lemma 2 Parts C
 *     and A), and Part D: every SubSet member's BA was entered 1 by
 *     an honest process that holds its accepted A-Cast.
 *
 * At every QUIESCENT terminal:
 *
 *   - surface 1: Lemma 4 and the value -- every process accepted the
 *     initiator's value -- AND the ending claim itself, that every
 *     process's READY suppress mask covers all n.  Lemma 4 alone does
 *     not separate a machine that retired READY on the forbidden LOCAL
 *     accept (Notes 10/13) from one that closed the remote
 *     all-accepted gate: both quiesce and both accept the one value,
 *     because there is only one value under honest-no-loss.  The mask
 *     is what says whose evidence closed it.  Lemma 4 is sound AT A
 *     QUIESCENCE LEAF and only there:
 *     quiescence is the 0 return of a full pass, and an
 *     ECHOED-but-not-ACCEPTED instance always outputs ECHO_ALL
 *     (bracha87Fig1Bpr gates ECHO retirement on ACCEPTED alone)
 *     while RDSENT implies ECHOED, so quiescence structurally implies
 *     ACCEPTED.  Pool empty.
 *   - surface 2: every process complete; |SubSet| >= n-t and never
 *     required to be n, since honest exclusion is legal
 *     (BKR94ACS.txt, Remarks for implementers); SubSet byte-identical
 *     across processes;
 *     every SubSet member's A-Cast value present and byte-equal
 *     everywhere; no 0xFE anywhere -- sound ONLY under the class
 *     precedence above.  Pool empty.  AND the ending claim per owned
 *     Fig 1 instance, read through bkr94acsAcastFig1 / bkr94acsBaFig1:
 *     every SENT instance THE RETRY STILL SERVES has a RECEIVED mask
 *     covering all n.  The scope matters: the decided-0 retry gate
 *     (bkr94acsRetryProcessGate) skips an excluded process's A-Cast walk,
 *     so there the gate itself is the retire, and a late-submitted
 *     excluded A-Cast legitimately quiesces accepted-everywhere with
 *     a short mask and outstanding arms nothing will consume.  The
 *     argument that makes "sent" the right guard: at quiescence a sent
 *     instance is ECHOED (a never-echoed initiator can retire INITIAL
 *     neither way -- ACCEPTED needs readySent needs echoed, and
 *     all-echoed counts the initiator's own echo), ECHOED is ACCEPTED
 *     (ECHO retires only there), and an ACCEPTED instance's READY
 *     retires only on the remote all-accepted gate.
 *     bracha87Fig1Value non-null is the sent test: both ready paths
 *     require ECHOED, and an INITIATOR carries its value.
 *
 * Whole-config reachability, which is the half a per-state battery
 * cannot carry (an assertion battery that never fires proves nothing
 * about coverage):
 *
 *   - some schedule reaches a QUIESCENT terminal, asserted only where
 *     the correct-machine baseline is MEASURED to reach one.  That
 *     measurement is what makes it a usable detector; asserting it at
 *     a config whose baseline does not reach the class would be a
 *     false red on a correct library.
 *   - at t=0 the EXHAUSTED class is empty.  Structural: n-t = n, so
 *     TurnDuty has no TOLERANCE band and a turn fires only at MET
 *     over the full sample; every correct process computes each round
 *     over the identical set, they agree from round 3i+1 onward, step
 *     2 gives (d,v) everywhere, and step 3's ">2t" is ">0" -- a
 *     decision in phase 0, no coin, no exhaustion.
 *   - config 3a reaches |SubSet| = n; config 3b reaches |SubSet| < n.
 *     These read COMPLETION, not quiescence: completion lands early
 *     and the retirement tail after it is long, so a subset witness
 *     behind a full quiescence requirement would be unreachable
 *     inside any affordable bound.
 *   - the frozen state count, edge count and per-class terminal
 *     counts.  For a defect class the semantic oracle is structurally
 *     blind to under honest-no-loss -- a threshold LOWERED, say,
 *     where every echo carries the same value so a false accept
 *     accepts the right value -- the frozen counts are the designated
 *     detector, and a count mismatch is SENSITIVITY to a behavioral
 *     change, not a detected defect.  Keeping those two claims apart
 *     is the difference between this and a harness that lies upward.
 *
 * On ANY failure the WITNESS -- the event sequence from the root -- is
 * printed in the form the re-run mode consumes, and the run stops.  A
 * defect found is not worked around here.
 *
 *
 * USAGE
 *
 *   test_schedules [-m] [-k ticks] [-J contents] [-c states] [-D depth]
 *                  [-b hashbits] [-s strategy] [-w witness] config
 *
 *   config      1 | 2 | 3a | 3b | 4 | b1 | b2 | b3 | b4 | smoke
 *               | strategies | all
 *   -m          report the frozen counts, do not assert them
 *   -k ticks    tick allowance per process override -- K above, so a
 *               run under it is exhaustive over a SMALLER schedule set
 *   -J contents contents per strategy override -- J above, the same
 *               way, on the adversary configs
 *   -c states   state ceiling override (per strategy)
 *   -D depth    depth ceiling override
 *   -b hashbits visited-table size, 1 << hashbits entries
 *   -s strategy run ONE hand-seeded strategy instead of the
 *               enumeration: comma-separated content keys in the
 *               form a failure prints
 *   -w witness  re-derive one event sequence from the root with a
 *               trace, in the comma-separated form a failure prints;
 *               with -s, under that strategy
 *
 * Style: C89, -pedantic -Wall -Wextra, Unix kernel style, 2-space
 * indent.  The explorer is one recursive function with labeled
 * regions; the shared act-expansion paths are reached by goto rather
 * than factored into single-caller helpers.  Its per-descent working
 * arrays are file-scope because they are consumed entirely before the
 * recursive call, and a C stack frame carrying them would not survive
 * the descent depth these bounds allow.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bracha87.h"
#include "bkr94acs.h"

/*--------------------------------------------------------------------------*/
/*  Fixed limits                                                            */
/*--------------------------------------------------------------------------*/

#define MAX_N        8     /* processes this instrument will host */
#define POOL_MAX  8192     /* distinct live contents; overflow aborts */
#define MAX_ACTS    64     /* out[] for any single library call here */
#define GRAM_MAX  4096     /* the adversary's grammar, distinct contents */
#define SEL_MAX      8     /* J: contents per strategy this will host */

/*
 * Content-key field layout.  One unsigned long per distinct wire
 * content; 30 bits used.
 */
#define KEY_TYPE_SH   0   /* BRACHA87_INITIAL / ECHO / READY  */
#define KEY_ACC_SH    2   /* wire ACCEPTED bit                */
#define KEY_RCV_SH    3   /* wire RECEIVED bit, per recipient */
#define KEY_CLS_SH    4   /* 0 = A-Cast, 1 = BA               */
#define KEY_BAV_SH    5   /* bit 0 binary value, bit 1 D_FLAG */
#define KEY_PROC_SH   7
#define KEY_ROUND_SH 11
#define KEY_INIT_SH  17
#define KEY_FROM_SH  21
#define KEY_TO_SH    25
#define KEY_VAL_SH   29   /* A-Cast / surface-1 value symbol: 0 = the
                           * instance's own, 1 = the foreign one.  Under
                           * an equivocating adversary `process` no
                           * longer determines the value bytes. */

#define KEY_FLD(k, sh, w) (((k) >> (sh)) & ((1UL << (w)) - 1))


/*
 * The two READY annotations packed for bkr94acs{Acast,Ba}Input's annot
 * argument.  The library reads them off the byte itself, and only when
 * the type is a READY.
 */
#define KEY_ANNOT(k) \
  ((KEY_FLD((k), KEY_ACC_SH, 1) ? BKR94ACS_ACCEPTED : 0) \
 | (KEY_FLD((k), KEY_RCV_SH, 1) ? BKR94ACS_RECEIVED : 0))
#define FNV_PRIME 1099511628211UL
#define FNV_BASE  14695981039346656037UL

#define EV_DELIVER 1
#define EV_TICK    2
#define EV_ACAST   3

/*--------------------------------------------------------------------------*/
/*  Configuration table                                                     */
/*                                                                          */
/*  expStates == 0 means the counts are not frozen yet: the run reports     */
/*  them and asserts nothing.  Freezing is a deliberate edit after two      */
/*  reproduced measurements.                                                */
/*--------------------------------------------------------------------------*/

struct config {
  const char *name;
  const char *note;
  unsigned long ceilStates;
  unsigned long ceilDepth;
  unsigned long expStates;
  unsigned long expEdges;
  unsigned long expQuiescent;
  unsigned long expExhausted;
  unsigned long expAllowance;
  unsigned long expCeiling;
  unsigned int hashBits;
  unsigned int k;           /* tick allowance per process */
  unsigned char surface;    /* 1 or 2 */
  unsigned char n;
  unsigned char t;
  unsigned char maxPhases;  /* surface 2 */
  unsigned char defer;      /* surface 2: deferred A-Cast, 0xFF = none */
  unsigned char keyAllow;   /* 1 = allowance in the key, 0 = dominance */
  unsigned char expectQuiescent;
  unsigned char expectNoExhausted;
  unsigned char expectSubsetFull;
  unsigned char expectSubsetShort;
  unsigned char expectAllAccepted; /* surface 1: every honest process
                                    * accepted -- 1 = some strategy
                                    * reaches it, 2 = every strategy */
  unsigned char expectComplete;    /* surface 2: completion, same code */
  unsigned char smoke;
  unsigned char adv;        /* the Byzantine process, 0xFF = none */
  unsigned char advInit;    /* surface 1: the adversary is the initiator */
  unsigned char advInst;    /* surface 2: ADV_INST_* instance classes */
  unsigned char j;          /* J: at most this many contents per strategy */
};

/* Surface-2 instance classes the adversary's grammar covers. */
#define ADV_INST_OWN_ACAST 1  /* its own A-Cast, as initiator          */
#define ADV_INST_OWN_BA    2  /* its own BA broadcasts, as initiator    */
#define ADV_INST_ACAST     4  /* honest A-Casts, echo and ready only    */
#define ADV_INST_BA        8  /* honest BA broadcasts, echo and ready   */

static struct config Configs[] = {
  /* s1, s2 -- the smoke subset `make check` runs: the same two shapes
   * as configs 1 and 4 under a ceiling measured to keep each under a
   * second.  They carry the reachability assertions, which is what a
   * subsecond run can honestly carry: the clean machine reaches its
   * first QUIESCENT terminal at state 19 (surface 1) and 199 (surface
   * 2), so a ceiling three orders of magnitude past that is a real
   * detector and not a token. */
  { "s1", "smoke: surface 1, n=2 t=0, K=3",
    60000UL, 4000UL,
    60000UL, 254604UL, 4UL, 0UL, 35UL, 1UL,
    18, 3, 1, 2, 0, 0, 0xFF, 1, 1, 0, 0, 0, 1, 0, 1, 0xFF, 0, 0, 0 },

  { "s2", "smoke: surface 2, n=2 t=0 maxPhases=1, K=40",
    60000UL, 4000UL,
    60000UL, 190885UL, 676UL, 0UL, 7UL, 1UL,
    18, 40, 2, 2, 0, 1, 0xFF, 1, 1, 1, 0, 0, 0, 1, 1, 0xFF, 0, 0, 0 },

  /* s3 -- the adversary smoke: b2's shape at J=2 under a ceiling
   * measured to keep the 466 strategies under a fifth of a second.
   * Two contents cannot announce to three honest processes, so no
   * strategy quiesces; the witness it carries is every honest process
   * accepted (Lemma 4 under an adversary), and the per-transition
   * checks -- Lemma 1, Lemma 2, the honest initiator's value -- run
   * on every state of every strategy. */
  { "s3", "smoke: surface 1, n=4 t=1, K=3, honest initiator 0,"
          " adversary 3, J=2",
    500UL, 4000UL,
    233000UL, 632500UL, 0UL, 0UL, 2800UL, 466UL,
    12, 3, 1, 4, 1, 0, 0xFF, 1, 0, 0, 0, 0, 2, 0, 1, 3, 0, 0, 2 },

  /* 1 -- THE ANCHOR.  Surface 1, n=2 t=0.  K=3 is the measured floor
   * and not a guess: a process must tick once to announce its accept
   * (the ACCEPTED annotation rides only on retry READYs -- an
   * Input-produced READY passes a literal 0,
   * example/bracha87Fig1.c), once for the marked re-send the
   * announcement's unmarked arrival arms, and once more to read the
   * retired 0 return that IS quiescence.  It is the anchor because it
   * is the smallest shape that reaches quiescence at all.  It is also
   * the one config that has been run to closure (the ceiling paragraph
   * in the banner); the 4,000,000 ceiling here is a budget. */
  { "1", "surface 1, n=2 t=0, K=3 -- the anchor",
    4000000UL, 4000UL,
    4000000UL, 24075069UL, 4UL, 0UL, 64UL, 1UL,
    24, 3, 1, 2, 0, 0, 0xFF, 1, 1, 0, 0, 0, 1, 0, 0, 0xFF, 0, 0, 0 },

  /* 2 -- surface 1 at real thresholds.  n=4 t=1: echo threshold
   * (n+t)/2+1 = 3, ready t+1 = 2, accept 2t+1 = 3. */
  { "2", "surface 1, n=4 t=1, K=3 -- real thresholds",
    4000000UL, 4000UL,
    4000000UL, 21752396UL, 15UL, 0UL, 833UL, 1UL,
    24, 3, 1, 4, 1, 0, 0xFF, 1, 1, 0, 0, 0, 1, 0, 0, 0xFF, 0, 0, 0 },

  /* 3a -- THE TARGET.  Surface 2, n=4 t=1, maxPhases=1, every A-Cast
   * submitted at the root.  The |SubSet| = n witness is on the first
   * dive: a drain that runs to empty accepts every A-Cast at every
   * process, step 1 enters 1 into all N BAs before the first tick, so
   * the unentered set is empty and FanoutDuty is MET forever. */
  { "3a", "surface 2, n=4 t=1 maxPhases=1, all A-Casts at the root",
    400000UL, 6000UL,
    400000UL, 2351444UL, 1UL, 0UL, 0UL, 1UL,
    21, 200, 2, 4, 1, 1, 0xFF, 0, 1, 0, 1, 0, 0, 1, 0, 0xFF, 0, 0, 0 },

  /* 3b -- the same, with process 3's A-Cast behind an acast() event.
   * With it unfired, processes 0/1/2's A-Casts accept everywhere, all
   * four processes enter 1 into BA_0/1/2, those three decide 1
   * (n-t = 3), and FanoutDuty leaves HELD with BA_3 unentered -- so
   * the honest-exclusion witness is a whole SUBTREE rather than a
   * needle, and the branch order puts the explorer inside it first. */
  { "3b", "surface 2, n=4 t=1 maxPhases=1, process 3's A-Cast deferred",
    400000UL, 6000UL,
    400000UL, 3591919UL, 19UL, 0UL, 0UL, 1UL,
    21, 200, 2, 4, 1, 1, 3, 0, 1, 0, 0, 1, 0, 1, 0, 0xFF, 0, 0, 0 },

  /* 4 -- the degenerate control.  At t=0, n-t = n, so TurnDuty's
   * TOLERANCE band (">= n-t and < n") is empty by arithmetic and
   * FanoutDuty can never classify TOLERANCE either.  None of the
   * sweep-paced machinery is alive; what it does check is the ending
   * claims and the EXHAUSTED-empty structural fact. */
  { "4", "surface 2, n=2 t=0 maxPhases=1 -- degenerate control",
    1000000UL, 4000UL,
    1000000UL, 3783178UL, 676UL, 0UL, 7UL, 1UL,
    22, 40, 2, 2, 0, 1, 0xFF, 1, 1, 1, 0, 0, 0, 1, 0, 0xFF, 0, 0, 0 },

  /* THE ADVERSARY CONFIGS.  Process 3 holds no library image, receives
   * nothing, and speaks only what its strategy seeds at the root; the
   * inner explorer above varies WHEN each seeded content lands.  Every
   * count is a SUM over the strategies enumerated, and the per-strategy
   * ceiling is hit by design -- see THE ADVERSARY in the banner.  The
   * counts were frozen from two identical measurement passes. */

  /* b1 -- surface 1, n=4 t=1, the adversary IS the initiator: Theorem 1
   * property 2 (Lemma 2 pairwise at every transition; "all accepted" at
   * a QUIESCENT terminal, which J=4 never reaches -- see below), Lemma
   * 1 pairwise among honest READY senders.
   * J=4 is MEASURED, not chosen: a non-strict echo threshold (>= (n+t)/2
   * for the proofs' >) is reached by INITIAL a to one process, INITIAL b
   * to another, and one supporting echo to each -- four contents -- and
   * by nothing at J=3; the Lemma 1 red on that mutant is the witness.
   * Announcing to all three besides would take J=6, so QUIESCENT is not
   * expected here; the witness is every honest process accepted. */
  { "b1", "surface 1, n=4 t=1, K=3, adversary 3 is the initiator",
    2000UL, 4000UL,
    104504656UL, 426320190UL, 0UL, 0UL, 88580UL, 34781UL,
    14, 3, 1, 4, 1, 0, 0xFF, 1, 0, 0, 0, 0, 1, 0, 0, 3, 1, 0, 4 },

  /* b2 -- surface 1, n=4 t=1, honest initiator 0, adversary 3 echoes
   * and readies: Theorem 1 property 1 -- every honest accept carries
   * the initiator's value (Lemma 4's value half) -- and Lemma 1. */
  { "b2", "surface 1, n=4 t=1, K=3, honest initiator 0, adversary 3",
    2000UL, 4000UL,
    9052000UL, 26128026UL, 256UL, 0UL, 86228UL, 4526UL,
    14, 3, 1, 4, 1, 0, 0xFF, 1, 1, 0, 0, 0, 2, 0, 0, 3, 0, 0, 3 },

  /* b3 -- surface 2, n=4 t=1 maxPhases=1, adversary 3 plays its own
   * broadcasts: its A-Cast and its BA round-r INITIALs (round 2 as a
   * (d,v) decision candidate), driven through Fig 3 validation into
   * Fig 4 at the composition.  Theorem 2 per BA at every transition,
   * BKR94 Lemma 2 Parts A-D at every completion.  QUIESCENT is
   * unreachable here by construction (an instance the adversary is
   * silent on never retires its READY -- bracha87.h's retry banner),
   * so the reachability witnesses are the completion ones. */
  { "b3", "surface 2, n=4 t=1 maxPhases=1, adversary 3 plays its own"
          " broadcasts",
    3000UL, 6000UL,
    38496000UL, 71435612UL, 0UL, 0UL, 702UL, 12832UL,
    15, 200, 2, 4, 1, 1, 0xFF, 0, 0, 0, 1, 1, 0, 2, 0,
    3, 0, ADV_INST_OWN_ACAST | ADV_INST_OWN_BA, 3 },

  /* b4 -- the same, with the adversary echoing and readying on the
   * HONEST broadcasts: every honest A-Cast and every honest BA round
   * broadcast, its own silent.  Lemma 2 on each A-Cast and Lemma 1's
   * threshold argument under the composition, Theorem 2 and C1 on
   * every BA.  |SubSet| = n is out of reach by construction -- the
   * adversary never A-Casts -- so only the exclusion witness stands. */
  { "b4", "surface 2, n=4 t=1 maxPhases=1, adversary 3 plays the honest"
          " broadcasts",
    3000UL, 6000UL,
    34869000UL, 62836112UL, 0UL, 0UL, 1989UL, 11623UL,
    15, 200, 2, 4, 1, 1, 0xFF, 0, 0, 0, 0, 1, 0, 2, 0,
    3, 0, ADV_INST_ACAST | ADV_INST_BA, 3 }
};

/*--------------------------------------------------------------------------*/
/*  Run state                                                               */
/*--------------------------------------------------------------------------*/

static struct config *Cfg;
static unsigned int N;          /* actual process count */
static unsigned int T;
static unsigned int Initiator;  /* surface 1 */

static unsigned char *Img[MAX_N];   /* the FIXED library allocations */
static unsigned long ImgSz;

static struct bracha87Retry Cursor[MAX_N];  /* surface 2, persistent */
static unsigned char Quiescent[MAX_N];
static unsigned char Accepted[MAX_N];       /* surface 1 */
static unsigned char AcceptVal[MAX_N];      /* surface 1, 1-byte values */
static unsigned char Pending[MAX_N];        /* surface 2 A-Cast submission */
static unsigned char Allow[MAX_N];

static unsigned char Val1 = 'v';            /* surface 1 broadcast value */
static unsigned char Aval[MAX_N];           /* surface 2 A-Cast values */
static unsigned char Sym[2];                /* surface 1: Val1, foreign */
static unsigned char Alt = 'z';             /* surface 2 foreign A-Cast */

/* The adversary.  0xFF is no adversary, and every honest-only loop
 * below compares against it, so the no-adversary path is the one the
 * frozen counts were measured on. */
static unsigned int Adv;

/* The adversary's grammar -- every content its strategies draw from,
 * partitioned by instance -- and the strategy under exploration, as
 * indices into it.  A hand-seeded strategy (-s) IS the grammar. */
static unsigned long Gram[GRAM_MAX];
static unsigned int GramCnt;
static unsigned int PartLo[64];
static unsigned int PartHi[64];
static unsigned int PartCnt;
static unsigned int Sel[SEL_MAX];
static unsigned int SelCnt;

/* Per-config sums over strategies, and how many strategies reached
 * each witness.  Under no adversary there is one strategy and these
 * equal the per-run counts. */
static unsigned long TotStates;
static unsigned long TotEdges;
static unsigned long TotQuiescent;
static unsigned long TotExhausted;
static unsigned long TotAllowance;
static unsigned long TotCeiling;
static unsigned long TotMaxDepth;
static unsigned long StratCnt;
static unsigned long StratQuiescent;
static unsigned long StratComplete;
static unsigned long StratSubFull;
static unsigned long StratSubShort;
static unsigned long StratExhausted;
static unsigned long StratAdvIn;
static unsigned long StratAllAccepted;

static unsigned long PoolKey[POOL_MAX];     /* ascending, distinct */
static unsigned int PoolCnt[POOL_MAX];
static unsigned int PoolLive;
static unsigned long PoolTot;

/* The descent stacks.  Three typed arenas rather than one byte arena
 * so no frame is ever read through a cast; they grow by realloc and
 * every frame is addressed by offset, never by a held pointer. */
static unsigned long *KeyStk;
static unsigned long KeyStkSz;
static unsigned long KeyStkTop;
static unsigned int *NumStk;
static unsigned long NumStkSz;
static unsigned long NumStkTop;
static unsigned char *BytStk;
static unsigned long BytStkSz;
static unsigned long BytStkTop;

/* The visited set: membership only.  Slot i holds a fingerprint and,
 * at the dominance configs, the head of that state's Pareto frontier
 * of allowance vectors. */
static unsigned long *Hash;
static unsigned int *HashHead;
static unsigned long HashSz;
static unsigned long HashCnt;
static unsigned char *FrAllow;   /* MAX_N bytes per frontier entry */
static unsigned int *FrNext;
static unsigned int FrCnt;
static unsigned int FrCap;

/* The path from the root, printed as the witness on any failure. */
static unsigned char *PathKind;
static unsigned long *PathArg;
static unsigned long PathCap;

/* Per-descent working storage.  Consumed entirely before the
 * recursive call, so one copy serves every depth. */
static struct bkr94acsAct Acts[MAX_ACTS];
static struct bracha87Fig1Act Pacts[BRACHA87_FIG1_RETRY_MAX_ACTS];
static unsigned char Out1[3];
static unsigned char PreFan[MAX_N];
static unsigned char PreTurn[MAX_N][MAX_N];
static unsigned char PreEnt[MAX_N][MAX_N];
static unsigned char Turned[MAX_N][MAX_N];
static unsigned char Subset[MAX_N];
static unsigned char FirstSub[MAX_N];
static struct bracha87Fig1 *F1p;
static struct bracha87Fig1 *F1arr[1];
static struct bracha87Retry F1cursor;
static struct bkr94acs *Acsp;
static const unsigned char *Skip;
static const unsigned char *Received;
static unsigned char PushType;
static unsigned char PushAcc;
static unsigned char PushVal;
static unsigned char PreDec;
static unsigned int Self;
static unsigned int NActs;
static unsigned int ActsRet;
static unsigned int PushRet;

static unsigned long States;
static unsigned long Edges;
static unsigned long TermQuiescent;
static unsigned long TermExhausted;
static unsigned long TermAllowance;
static unsigned long CeilingCuts;
static unsigned long MaxDepthSeen;
static int CeilingHit;
static int TableFull;

static unsigned long FirstQuiescent;  /* the state count at the first one */
static int SawQuiescent;
static int SawSubsetFull;
static int SawSubsetShort;
static int SawComplete;
static int SawAdvIn;                  /* the adversary in an honest SubSet */
static int SawAllAccepted;            /* surface 1: every honest process */

static const char *FailMsg;
static int Failed;

/* Re-run mode: the forced event sequence, consumed one per depth. */
static unsigned char *WitKind;
static unsigned long *WitArg;
static unsigned long WitLen;
static int WitMode;
static int WitStuck;

/*--------------------------------------------------------------------------*/
/*  The coin.  Deterministic alternating, the bundled examples' choice:      */
/*  every honest process gets the same value in Bracha Fig 4 case (iii),     */
/*  so a phase in which case (iii) fires everywhere ends agreed.  Coin       */
/*  branching is a documented non-goal, and this is why maxPhases is the     */
/*  dominant knob for the EXHAUSTED class.                                   */
/*--------------------------------------------------------------------------*/

static unsigned char
demoCoin(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
  return ((unsigned char)(phase % 2));
}

/*--------------------------------------------------------------------------*/
/*  The pool -- a sorted multiset of distinct wire contents.                */
/*                                                                          */
/*  Sorted by construction so the fingerprint and the branch order are      */
/*  insertion-order-independent.  Three expansion regions inside the         */
/*  explorer feed it; it takes one argument and reads no enclosing state,    */
/*  so it stands alone rather than as another gosub label.                   */
/*--------------------------------------------------------------------------*/

static void
poolPush(
  unsigned long key
){
  unsigned int lo;
  unsigned int hi;
  unsigned int mid;

  lo = 0;
  hi = PoolLive;
  while (lo < hi) {
    mid = (lo + hi) / 2;
    if (PoolKey[mid] < key)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo < PoolLive && PoolKey[lo] == key) {
    ++PoolCnt[lo];
    ++PoolTot;
    return;
  }
  if (PoolLive >= POOL_MAX) {
    fprintf(stderr,
            "test_schedules: pool overflow at %u distinct contents --"
            " the pool grows or aborts, it never truncates\n",
            (unsigned)POOL_MAX);
    exit(2);
  }
  for (mid = PoolLive; mid > lo; --mid) {
    PoolKey[mid] = PoolKey[mid - 1];
    PoolCnt[mid] = PoolCnt[mid - 1];
  }
  PoolKey[lo] = key;
  PoolCnt[lo] = 1;
  ++PoolLive;
  ++PoolTot;
}

/*--------------------------------------------------------------------------*/
/*  The explorer.                                                           */
/*                                                                          */
/*  One recursive function: fingerprint, visited test, classify, snapshot,   */
/*  then one branch per enabled event.  The act-expansion paths are shared   */
/*  through labeled regions, since each must run with the enclosing frame's  */
/*  locals in scope.                                                        */
/*--------------------------------------------------------------------------*/

static void
explore(
  unsigned long depth
){
  unsigned long koff;      /* this frame's base in KeyStk          */
  unsigned long noff;      /* this frame's base in NumStk          */
  unsigned long boff;      /* this frame's base in BytStk          */
  unsigned long nLive;     /* pool live count at the snapshot      */
  unsigned long key;
  unsigned long i;
  unsigned long j;
  unsigned long evArg;
  unsigned long ev;
  unsigned long frameSum;  /* this frame's arena witness; see the restore */
  unsigned long sum;
  unsigned int p;
  unsigned int q;
  unsigned int evKind;
  unsigned int nEnabled;
  unsigned int cnt;
  unsigned int firstCnt;
  unsigned char order[MAX_N];
  int took;
  int haveFirst;
  int anyExhausted;
  int allComplete;
  int allQuiescent;
  int allAccepted;

  if (Failed || CeilingHit || TableFull)
    return;
  if (depth > MaxDepthSeen)
    MaxDepthSeen = depth;
  /* No event chosen at this depth yet.  A failure in the classification
   * below is a failure OF this state, so the witness that reaches it
   * ends one event short; the 0 is where the printer stops. */
  PathKind[depth] = 0;

  /*------------------------------------------------------------------*/
  /*  Visited test.  Skipped in re-run mode, which walks one given     */
  /*  sequence rather than a search.                                   */
  /*------------------------------------------------------------------*/

  if (!WitMode) {
    unsigned long h;
    unsigned long slot;
    unsigned int fr;
    unsigned int prev;
    int dominated;

    h = FNV_BASE;
    for (p = 0; p < N; ++p) {
      for (j = 0; j < ImgSz; ++j) {
        h ^= Img[p][j];
        h *= FNV_PRIME;
      }
      h ^= Quiescent[p];        h *= FNV_PRIME;
      h ^= Accepted[p];         h *= FNV_PRIME;
      h ^= AcceptVal[p];        h *= FNV_PRIME;
      h ^= Pending[p];          h *= FNV_PRIME;
      h ^= Cursor[p].pos;       h *= FNV_PRIME;
      h ^= Cursor[p].sweepActs; h *= FNV_PRIME;
      /*
       * Cursor[p].sweeps is DELIBERATELY out of the key, and out of
       * the push/restore below with it.  It is monotone and unbounded
       * -- it counts completed passes and never resets -- so keying on
       * it would make every state unique, explode the space, and empty
       * the frozen counts of meaning.  It is caller-facing bookkeeping
       * (the pass boundary a patience clock reads), not protocol
       * state: no library decision reads it, so two states equal in
       * every other field are the same state.  Nothing in this file
       * reads it either.
       */
      if (Cfg->keyAllow) {
        h ^= Allow[p];
        h *= FNV_PRIME;
      }
    }
    for (i = 0; i < PoolLive; ++i) {
      h ^= PoolKey[i]; h *= FNV_PRIME;
      h ^= PoolCnt[i]; h *= FNV_PRIME;
    }
    if (!h)
      h = 1;

    slot = h & (HashSz - 1);
    for (;;) {
      if (!Hash[slot]) {
        Hash[slot] = h;
        HashHead[slot] = 0;
        ++HashCnt;
        if (HashCnt * 4 > HashSz * 3) {
          TableFull = 1;
          return;
        }
        break;
      }
      if (Hash[slot] == h)
        break;
      slot = (slot + 1) & (HashSz - 1);
    }

    if (Cfg->keyAllow) {
      /* The allowance is in the fingerprint, so membership decides. */
      if (HashHead[slot])
        return;
      HashHead[slot] = 1;
    } else {
      /* Pareto frontier: skip only if a prior visit's allowance vector
       * dominates this one componentwise. */
      dominated = 0;
      for (fr = HashHead[slot]; fr; fr = FrNext[fr]) {
        dominated = 1;
        for (p = 0; p < N; ++p)
          if (FrAllow[fr * MAX_N + p] < Allow[p]) {
            dominated = 0;
            break;
          }
        if (dominated)
          break;
      }
      if (dominated)
        return;
      prev = 0;
      fr = HashHead[slot];
      while (fr) {
        int under;

        under = 1;
        for (p = 0; p < N; ++p)
          if (Allow[p] < FrAllow[fr * MAX_N + p]) {
            under = 0;
            break;
          }
        if (under) {
          if (prev)
            FrNext[prev] = FrNext[fr];
          else
            HashHead[slot] = FrNext[fr];
          fr = prev ? FrNext[prev] : HashHead[slot];
        } else {
          prev = fr;
          fr = FrNext[fr];
        }
      }
      if (FrCnt >= FrCap) {
        TableFull = 1;
        return;
      }
      for (p = 0; p < N; ++p)
        FrAllow[FrCnt * MAX_N + p] = Allow[p];
      FrNext[FrCnt] = HashHead[slot];
      HashHead[slot] = FrCnt;
      ++FrCnt;
    }
    ++States;
    if (States >= Cfg->ceilStates) {
      CeilingHit = 1;
      ++CeilingCuts;
      return;
    }
  }

  /*------------------------------------------------------------------*/
  /*  Classify.  EXHAUSTED first, then QUIESCENT, then                 */
  /*  ALLOWANCE-EXHAUSTED, so the three partition the terminals.       */
  /*------------------------------------------------------------------*/

  anyExhausted = 0;
  allComplete = (Cfg->surface == 2);
  allQuiescent = 1;
  allAccepted = (Cfg->surface == 1);
  for (p = 0; p < N; ++p) {
    if (p == Adv)
      continue;
    if (!Quiescent[p])
      allQuiescent = 0;
    if (!Accepted[p])
      allAccepted = 0;
    if (Cfg->surface != 2)
      continue;
    Acsp = (struct bkr94acs *)Img[p];
    if (!Acsp->complete)
      allComplete = 0;
    for (q = 0; q < N; ++q)
      if (bkr94acsBaDecision(Acsp, (unsigned char)q) == 0xFE)
        anyExhausted = 1;
  }

  /* The reachability witnesses read the OUTCOME, not quiescence:
   * every honest accept (surface 1) and completion (surface 2) land
   * early, and the retirement tail after them is long. */
  if (allAccepted)
    SawAllAccepted = 1;
  if (allComplete) {
    haveFirst = 0;
    firstCnt = 0;
    for (p = 0; p < N; ++p) {
      if (p == Adv)
        continue;
      cnt = bkr94acsSubset((struct bkr94acs *)Img[p], Subset);
      if (!haveFirst) {
        firstCnt = cnt;
        memcpy(FirstSub, Subset, cnt);
        haveFirst = 1;
      } else if (cnt != firstCnt || memcmp(FirstSub, Subset, cnt)) {
        FailMsg = "SubSet disagreement between complete processes";
        goto fail;
      }
    }
    if (firstCnt < N - T) {
      FailMsg = "|SubSet| below n-t";
      goto fail;
    }
    SawComplete = 1;
    if (firstCnt == N)
      SawSubsetFull = 1;
    else
      SawSubsetShort = 1;
    /* BKR94 Lemma 2 Part D: j is in SubSet only because "at least one
     * honest player entered 1 as his input to BA_j", and step 1 enters
     * 1 on Q(j) = 1, so that process holds j's accepted A-Cast.  The
     * entered value is read off the image, not tracked: the round-0
     * BA Fig 1 this process initiated carries it in its INITIATOR
     * slot.  Under an adversary this is the check that a BA decided 1
     * on the adversary's say-so alone. */
    for (i = 0; i < firstCnt; ++i) {
      if (FirstSub[i] == Adv)
        SawAdvIn = 1;
      for (p = 0; p < N; ++p) {
        const unsigned char *ev;

        if (p == Adv)
          continue;
        Acsp = (struct bkr94acs *)Img[p];
        if (!bkr94acsBaEntered(Acsp, FirstSub[i]))
          continue;
        ev = bracha87Fig1Value(bkr94acsBaFig1(Acsp, FirstSub[i], 0, p));
        if (ev && (*ev & 1) && bkr94acsAcastValue(Acsp, FirstSub[i]))
          break;
      }
      if (p >= N) {
        FailMsg = "BKR94 Lemma 2 Part D: a SubSet member's BA was never"
                  " entered 1 by an honest process holding its A-Cast";
        goto fail;
      }
    }
  }

  nEnabled = PoolLive;
  for (p = 0; p < N; ++p) {
    if (Allow[p] && (Cfg->surface == 2 || !Quiescent[p]))
      ++nEnabled;
    if (Pending[p])
      ++nEnabled;
  }

  if (anyExhausted) {
    /* The exhaustion contract.  Single output per BA and the 0xFE
     * sentinel are checked where the act appears; here: complete
     * never set, and agreement among the processes that did complete
     * (checked above).  Their SubSets are never read further. */
    ++TermExhausted;
    for (p = 0; p < N; ++p) {
      if (p == Adv)
        continue;
      Acsp = (struct bkr94acs *)Img[p];
      for (q = 0; q < N; ++q)
        if (bkr94acsBaDecision(Acsp, (unsigned char)q) == 0xFE
         && Acsp->complete) {
          FailMsg = "a process with an EXHAUSTED BA reports complete";
          goto fail;
        }
    }
    return;
  }

  if (allQuiescent && !PoolTot) {
    ++TermQuiescent;
    if (!SawQuiescent)
      FirstQuiescent = States;
    SawQuiescent = 1;
    if (Cfg->surface == 1) {
      for (p = 0; p < N; ++p) {
        const unsigned char *sk;

        if (p == Adv)
          continue;
        /* Under an adversary initiator the property is Theorem 1's
         * second: all correct processes agree on a value or none
         * accepts.  Quiescence is the all-sent case, and a sent
         * instance quiesces only accepted, so here it is "all";
         * pairwise agreement rode every transition (Lemma 2). */
        if (!Accepted[p]) {
          FailMsg = Cfg->advInit
            ? "Theorem 1 property 2: a quiescent process never accepted"
            : "Lemma 4: a quiescent process never accepted";
          goto fail;
        }
        if (!Cfg->advInit && AcceptVal[p] != Val1) {
          FailMsg = "Lemma 4: the accepted value is not the initiator's";
          goto fail;
        }
        /* THE ENDING CLAIM, and the reason it is checked rather than
         * inferred from the 0 return: example/bracha87Fig1.c's header
         * says quiescence is reached because "each instance's suppress
         * mask reaches all n, READY retires with it, and a full
         * bracha87Fig1RetryStep pass owes nothing".  The 0 return
         * alone is the weaker fact -- a machine that retired READY at
         * LOCAL accept (Notes 10/13, the forbidden gate) would
         * also return 0, quiesce sooner, and still satisfy Lemma 4,
         * because every honest process here accepts the one value
         * either way.  What separates the two is WHOSE evidence closed
         * the gate: the mask is full only when every process has
         * announced its own accept and holds this one's. */
        sk = bracha87Fig1Skip((struct bracha87Fig1 *)Img[p],
                              BRACHA87_READY_ALL);
        if (!sk) {
          FailMsg = "quiescent terminal: no READY suppress mask";
          goto fail;
        }
        for (q = 0; q < N; ++q)
          if (!BRACHA87_SKIP_TST(sk, q)) {
            FailMsg = "quiescent terminal: the READY suppress mask is"
                      " short of all n -- quiescence did not come from"
                      " the remote all-accepted gate";
            goto fail;
          }
      }
    } else {
      haveFirst = 0;
      firstCnt = 0;
      for (p = 0; p < N; ++p) {
        if (p == Adv)
          continue;
        Acsp = (struct bkr94acs *)Img[p];
        if (!Acsp->complete) {
          FailMsg = "a quiescent surface-2 process is not complete";
          goto fail;
        }
        cnt = bkr94acsSubset(Acsp, Subset);
        if (cnt < N - T) {
          FailMsg = "quiescent terminal: |SubSet| below n-t";
          goto fail;
        }
        if (!haveFirst) {
          firstCnt = cnt;
          memcpy(FirstSub, Subset, cnt);
          haveFirst = 1;
        } else if (cnt != firstCnt || memcmp(FirstSub, Subset, cnt)) {
          FailMsg = "quiescent terminal: SubSets differ";
          goto fail;
        }
        for (q = 0; q < N; ++q)
          if (bkr94acsBaDecision(Acsp, (unsigned char)q) == 0xFE) {
            FailMsg = "quiescent terminal: a 0xFE sentinel is present";
            goto fail;
          }
        /* THE ENDING CLAIM, per owned Fig 1 instance -- checked, not
         * inferred from the Retry 0 return, the same distinction the
         * surface-1 arm turns on (Notes 10/13): at quiescence a
         * SENT instance is ECHOED (a never-echoed initiator can
         * retire INITIAL neither way -- ACCEPTED needs readySent
         * needs echoed, and all-echoed counts the initiator's own
         * echo), ECHOED is ACCEPTED (ECHO retires only there), and an
         * ACCEPTED instance's READY retires only on the remote
         * all-accepted gate: the RECEIVED mask covers all n.  Value
         * non-null is the sent test. */
        {
          unsigned int r;
          unsigned int b;
          unsigned int w;
          const struct bracha87Fig1 *f1;
          const unsigned char *am;

          for (b = 0; b < N; ++b) {
            /* The decided-0 retry gate OUTRANKS the annotation
             * exchange for an excluded process's A-Cast
             * (bkr94acsRetryProcessGate: BA decided 0 -> the retry skips
             * the A-Cast walk; the gate itself is the retire).  A
             * late-submitted excluded A-Cast can therefore quiesce
             * accepted-everywhere with a permanently short RECEIVED
             * mask and outstanding arms nothing will ever consume --
             * reachable, and benign: nothing is owed, the exclusion
             * already conveyed the outcome.  The ending claim is
             * scoped to the instances the retry still serves. */
            if (bkr94acsBaDecision(Acsp, (unsigned char)b) == 0)
              continue;
            f1 = bkr94acsAcastFig1(Acsp, (unsigned char)b);
            if (!f1 || !bracha87Fig1Value(f1))
              continue;
            if (!(am = bracha87Fig1Received(f1))) {
              FailMsg = "quiescent terminal: a sent A-Cast Fig 1 has"
                        " no RECEIVED mask";
              goto fail;
            }
            for (q = 0; q < N; ++q)
              if (!BRACHA87_SKIP_TST(am, q)) {
                FailMsg = "quiescent terminal: a sent A-Cast Fig 1's"
                          " RECEIVED mask is short of all n -- its READY"
                          " did not retire on the remote gate";
                goto fail;
              }
          }
          for (b = 0; b < N; ++b)
            for (r = 0; r < 3u * Cfg->maxPhases; ++r)
              for (w = 0; w < N; ++w) {
                f1 = bkr94acsBaFig1(Acsp, (unsigned char)b,
                                    (unsigned char)r, (unsigned char)w);
                if (!f1 || !bracha87Fig1Value(f1))
                  continue;
                if (!(am = bracha87Fig1Received(f1))) {
                  FailMsg = "quiescent terminal: a sent BA Fig 1 has"
                            " no RECEIVED mask";
                  goto fail;
                }
                for (q = 0; q < N; ++q)
                  if (!BRACHA87_SKIP_TST(am, q)) {
                    FailMsg = "quiescent terminal: a sent BA Fig 1's"
                              " RECEIVED mask is short of all n -- its"
                              " READY did not retire on the remote"
                              " gate";
                    goto fail;
                  }
              }
        }
      }
      for (i = 0; i < firstCnt; ++i)
        for (p = 0; p < N; ++p) {
          const unsigned char *cv;

          if (p == Adv)
            continue;
          cv = bkr94acsAcastValue((struct bkr94acs *)Img[p], FirstSub[i]);
          if (!cv) {
            FailMsg = "quiescent terminal: a SubSet member's value is absent";
            goto fail;
          }
          /* An honest member's value is the one it A-Cast; the
           * adversary's is whichever of its two symbols got through,
           * and that it is the same at every honest process is Lemma
           * 2, checked at every transition below. */
          if (FirstSub[i] == Adv
              ? (*cv != Aval[Adv] && *cv != Alt)
              : *cv != Aval[FirstSub[i]]) {
            FailMsg = "quiescent terminal: a SubSet member's value differs";
            goto fail;
          }
        }
    }
    return;
  }

  if (!nEnabled) {
    ++TermAllowance;
    return;
  }

  if (depth + 2 >= Cfg->ceilDepth) {
    ++CeilingCuts;
    CeilingHit = 1;
    return;
  }

  /*------------------------------------------------------------------*/
  /*  Snapshot.  Byte-copy of the fixed allocations plus the harness   */
  /*  state and the pool; the frame is never dereferenced as a struct  */
  /*  and the restore below is a memcpy back into the same             */
  /*  allocations.                                                     */
  /*------------------------------------------------------------------*/

  nLive = PoolLive;
  koff = KeyStkTop;
  noff = NumStkTop;
  boff = BytStkTop;

  if (KeyStkTop + nLive + 1 > KeyStkSz) {
    KeyStkSz = (KeyStkSz * 2 > KeyStkTop + nLive + 64)
             ? KeyStkSz * 2 : KeyStkTop + nLive + 64;
    if (!(KeyStk = realloc(KeyStk, KeyStkSz * sizeof (unsigned long)))) {
      fprintf(stderr, "test_schedules: key stack allocation failed\n");
      exit(2);
    }
  }
  if (NumStkTop + nLive + 2 * MAX_N + 1 > NumStkSz) {
    NumStkSz = (NumStkSz * 2 > NumStkTop + nLive + 2 * MAX_N + 64)
             ? NumStkSz * 2 : NumStkTop + nLive + 2 * MAX_N + 64;
    if (!(NumStk = realloc(NumStk, NumStkSz * sizeof (unsigned int)))) {
      fprintf(stderr, "test_schedules: number stack allocation failed\n");
      exit(2);
    }
  }
  if (BytStkTop + N * ImgSz + 5 * MAX_N > BytStkSz) {
    BytStkSz = (BytStkSz * 2 > BytStkTop + N * ImgSz + 5 * MAX_N + 65536)
             ? BytStkSz * 2 : BytStkTop + N * ImgSz + 5 * MAX_N + 65536;
    if (!(BytStk = realloc(BytStk, BytStkSz))) {
      fprintf(stderr, "test_schedules: byte stack allocation failed\n");
      exit(2);
    }
  }

  /*
   * frameSum is this frame's own witness that its arena words come back
   * the way they went in.  It is a LOCAL, so the recursion gives every
   * frame its own copy with no indexing of its own to get wrong -- the
   * point of the check is to be independent of the offset arithmetic it
   * audits, and a checksum kept in a depth-indexed array would share
   * exactly the mechanism under test.  Accumulated in the loops that
   * already walk these words, so it costs a multiply-add per word.
   */
  frameSum = 0;
  for (i = 0; i < nLive; ++i) {
    KeyStk[koff + i] = PoolKey[i];
    NumStk[noff + i] = PoolCnt[i];
    frameSum = frameSum * 31 + PoolKey[i] + PoolCnt[i];
  }
  for (p = 0; p < N; ++p) {
    NumStk[noff + nLive + 2 * p] = Cursor[p].pos;
    NumStk[noff + nLive + 2 * p + 1] = Cursor[p].sweepActs;
    frameSum = frameSum * 31 + Cursor[p].pos + Cursor[p].sweepActs;
    memcpy(BytStk + boff + p * ImgSz, Img[p], ImgSz);
  }
  memcpy(BytStk + boff + N * ImgSz + 0 * MAX_N, Quiescent, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 1 * MAX_N, Accepted, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 2 * MAX_N, AcceptVal, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 3 * MAX_N, Pending, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 4 * MAX_N, Allow, MAX_N);

  /* Each arena has its OWN base.  The number arena holds nLive counts
   * plus 2 * MAX_N cursor words per frame, so its frames are longer
   * than the key arena's by the cursor words; a child frame based at
   * the key arena's top lands on this frame's cursor words, and the
   * restore below then reads the child's counts back as the cursors. */
  KeyStkTop = koff + nLive;
  NumStkTop = noff + nLive + 2 * MAX_N;
  BytStkTop = boff + N * ImgSz + 5 * MAX_N;

  /*------------------------------------------------------------------*/
  /*  One branch per enabled event: deliveries in ascending            */
  /*  content-key order, then ticks LEAST-TICKED FIRST, then A-Cast    */
  /*  submissions.                                                     */
  /*                                                                   */
  /*  Every branch is explored either way, so the order changes only   */
  /*  which schedule the FIRST dive is, and both choices matter.       */
  /*  Deliveries first makes the first dive a drain.  Least-ticked     */
  /*  first makes the ticks inside that drain a fair round-robin, the  */
  /*  examples' sweep: taking them in process order instead spends     */
  /*  every tick on process 0 and starves the rest, which reaches no   */
  /*  completion at all and buries the witnesses behind a deep         */
  /*  backtrack.  A-Cast submissions LAST is what puts config 3b's     */
  /*  honest-exclusion witness inside the subtree the explorer enters  */
  /*  first.                                                           */
  /*------------------------------------------------------------------*/

  for (p = 0; p < N; ++p)
    order[p] = (unsigned char)p;
  for (p = 0; p + 1 < N; ++p)
    for (q = p + 1; q < N; ++q)
      if (Allow[order[q]] > Allow[order[p]]) {
        unsigned char sw;

        sw = order[p];
        order[p] = order[q];
        order[q] = sw;
      }

  took = 0;
  for (ev = 0; ev < nLive + 2 * N; ++ev) {
    if (Failed || CeilingHit || TableFull)
      break;

    if (ev < nLive) {
      evKind = EV_DELIVER;
      evArg = KeyStk[koff + ev];
    } else if (ev < nLive + N) {
      p = order[ev - nLive];
      if (!Allow[p] || (Cfg->surface == 1 && Quiescent[p]))
        continue;
      evKind = EV_TICK;
      evArg = p;
    } else {
      p = (unsigned int)(ev - nLive - N);
      if (!Pending[p])
        continue;
      evKind = EV_ACAST;
      evArg = p;
    }

    if (WitMode) {
      /* Re-run: take only the recorded event at this depth. */
      if (depth >= WitLen)
        break;
      if (WitKind[depth] != evKind || WitArg[depth] != evArg)
        continue;
    }

    PathKind[depth] = (unsigned char)evKind;
    PathArg[depth] = evArg;
    ++Edges;
    took = 1;

    if (WitMode) {
      printf("  %5lu ", depth);
      if (evKind == EV_DELIVER)
        printf("deliver cls=%lu proc=%lu round=%lu init=%lu type=%lu"
               " from=%lu to=%lu acc=%lu rcv=%lu bav=%lu val=%lu\n",
               KEY_FLD(evArg, KEY_CLS_SH, 1),
               KEY_FLD(evArg, KEY_PROC_SH, 4),
               KEY_FLD(evArg, KEY_ROUND_SH, 6),
               KEY_FLD(evArg, KEY_INIT_SH, 4),
               KEY_FLD(evArg, KEY_TYPE_SH, 2),
               KEY_FLD(evArg, KEY_FROM_SH, 4),
               KEY_FLD(evArg, KEY_TO_SH, 4),
               KEY_FLD(evArg, KEY_ACC_SH, 1),
               KEY_FLD(evArg, KEY_RCV_SH, 1),
               KEY_FLD(evArg, KEY_BAV_SH, 2),
               KEY_FLD(evArg, KEY_VAL_SH, 1));
      else
        printf("%s process %lu\n",
               (evKind == EV_TICK) ? "tick   " : "acast  ", evArg);
    }

    /* The duty and entered readings this transition is measured
     * against. */
    if (Cfg->surface == 2)
      for (p = 0; p < N; ++p) {
        Acsp = (struct bkr94acs *)Img[p];
        PreFan[p] = bkr94acsFanoutDuty(Acsp);
        for (q = 0; q < N; ++q) {
          PreTurn[p][q] = bkr94acsTurnDuty(Acsp, (unsigned char)q);
          PreEnt[p][q] = (unsigned char)
            bkr94acsBaEntered(Acsp, (unsigned char)q);
          Turned[p][q] = 0;
        }
      }
    PreDec = 0xFF;

    switch (evKind) {
    case EV_DELIVER: goto doDeliver;
    case EV_TICK:    goto doTick;
    default:         goto doAcast;
    }

    /*--------------------------------------------------------------*/
    /*  deliver(m) -- the ingress composite, atomically.             */
    /*--------------------------------------------------------------*/

   doDeliver:
    key = evArg;
    {
      unsigned long lo;
      unsigned long hi;
      unsigned long mid;

      lo = 0;
      hi = PoolLive;
      while (lo < hi) {
        mid = (lo + hi) / 2;
        if (PoolKey[mid] < key)
          lo = mid + 1;
        else
          hi = mid;
      }
      /* In the search this cannot fire -- the key came from this
       * frame's own live list.  In re-run mode it is the guard on a
       * hand-supplied sequence. */
      if (lo >= PoolLive || PoolKey[lo] != key) {
        FailMsg = "the named content is not pending in this state";
        goto fail;
      }
      if (!--PoolCnt[lo]) {
        for (mid = lo; mid + 1 < PoolLive; ++mid) {
          PoolKey[mid] = PoolKey[mid + 1];
          PoolCnt[mid] = PoolCnt[mid + 1];
        }
        --PoolLive;
      }
      --PoolTot;
    }
    Self = (unsigned int)KEY_FLD(key, KEY_TO_SH, 4);

    if (Cfg->surface == 1) {
      F1p = (struct bracha87Fig1 *)Img[Self];

      /* The caller-side forged-INITIAL filter (README Implementation
       * Note 14): the bare Fig 1 entry is not told its designated
       * initiator, so a bare-layer caller must drop a non-initiator
       * INITIAL before it reaches the echo cascade. */
      if (KEY_FLD(key, KEY_TYPE_SH, 2) == BRACHA87_INITIAL
       && KEY_FLD(key, KEY_FROM_SH, 4) != Initiator)
        goto applied;

      NActs = bracha87Fig1Input(F1p,
                                (unsigned char)KEY_FLD(key, KEY_TYPE_SH, 2),
                                (unsigned char)KEY_FLD(key, KEY_FROM_SH, 4),
                                &Sym[KEY_FLD(key, KEY_VAL_SH, 1)], Out1);
      if (NActs > 3) {
        FailMsg = "bracha87Fig1Input output more than 3 acts";
        goto fail;
      }
      if (KEY_FLD(key, KEY_TYPE_SH, 2) == BRACHA87_READY) {
        if (KEY_FLD(key, KEY_ACC_SH, 1))
          bracha87Fig1ProcessAccepted(F1p,
            (unsigned char)KEY_FLD(key, KEY_FROM_SH, 4));
        /* HARNESS SELF-CHECK: a marked READY is never routed to
         * *Resend -- a marked READY that re-armed its sender would
         * ping-pong and the pair would never fall silent. */
        if (!KEY_FLD(key, KEY_RCV_SH, 1)) {
          bracha87Fig1ProcessResend(F1p,
            (unsigned char)KEY_FLD(key, KEY_FROM_SH, 4));
          Quiescent[Self] = 0;
        }
      }
      for (i = 0; i < NActs; ++i) {
        const unsigned char *cv;

        cv = bracha87Fig1Value(F1p);
        if (!cv)
          continue;
        if (*cv != Sym[0] && *cv != Sym[1]) {
          FailMsg = "a Fig 1 act carries a value outside the run's alphabet";
          goto fail;
        }
        /* Under an honest initiator every honest echo carries its
         * value: Lemma 1's threshold argument -- more than (n+t)/2
         * echoes, or t+1 readys, of a value the initiator never sent
         * would need honest senders of it, and there is no first one.
         * Under an adversary initiator any symbol is legal here and
         * the pairwise forms at `applied` carry Lemmas 1 and 2. */
        if (!Cfg->advInit && *cv != Val1) {
          FailMsg = "an honest echo carries a value the initiator never"
                    " sent -- a Rule 2 or 3 threshold fired below the"
                    " proofs' (Lemma 1's pigeonhole)";
          goto fail;
        }
        PushVal = (*cv == Sym[1]);
        if (Out1[i] == BRACHA87_ACCEPT) {
          Accepted[Self] = 1;
          AcceptVal[Self] = *cv;
          /* The instance is not told its own index, so the caller
           * supplies it for the self-accept the all-n gate counts. */
          bracha87Fig1ProcessAccepted(F1p, (unsigned char)Self);
          continue;
        }
        PushType = (unsigned char)((Out1[i] == BRACHA87_ECHO_ALL)
                                   ? BRACHA87_ECHO : BRACHA87_READY);
        PushAcc = 0;
        Skip = bracha87Fig1Skip(F1p, Out1[i]);
        Received = (Out1[i] == BRACHA87_READY_ALL)
               ? bracha87Fig1Received(F1p) : 0;
        PushRet = 1;
        goto push1;
       push1r1:
        ;
      }
      goto applied;
    }

    Acsp = (struct bkr94acs *)Img[Self];
    if (!KEY_FLD(key, KEY_CLS_SH, 1)) {
      NActs = bkr94acsAcastInput(Acsp,
                (unsigned char)KEY_FLD(key, KEY_PROC_SH, 4),
                (unsigned char)KEY_FLD(key, KEY_TYPE_SH, 2),
                KEY_ANNOT(key),
                (unsigned char)KEY_FLD(key, KEY_FROM_SH, 4),
                KEY_FLD(key, KEY_VAL_SH, 1)
                  ? &Alt : &Aval[KEY_FLD(key, KEY_PROC_SH, 4)], Acts);
      if (NActs > 3) {
        FailMsg = "bkr94acsAcastInput output more than 3 acts";
        goto fail;
      }
      /* The annotations rode in on annot above; what stays here is this
       * harness's parking policy -- an unmarked READY says something is
       * still owed, so the process rejoins the rotation. */
      if (KEY_FLD(key, KEY_TYPE_SH, 2) == BRACHA87_READY
       && !KEY_FLD(key, KEY_RCV_SH, 1))
        Quiescent[Self] = 0;
    } else {
      NActs = bkr94acsBaInput(Acsp,
                (unsigned char)KEY_FLD(key, KEY_PROC_SH, 4),
                (unsigned char)KEY_FLD(key, KEY_ROUND_SH, 6),
                (unsigned char)KEY_FLD(key, KEY_INIT_SH, 4),
                (unsigned char)KEY_FLD(key, KEY_TYPE_SH, 2),
                KEY_ANNOT(key),
                (unsigned char)KEY_FLD(key, KEY_FROM_SH, 4),
                (unsigned char)((KEY_FLD(key, KEY_BAV_SH, 2) & 1)
                                | ((KEY_FLD(key, KEY_BAV_SH, 2) & 2)
                                   ? BRACHA87_D_FLAG : 0)),
                Acts);
      if (NActs > 2) {
        FailMsg = "bkr94acsBaInput output more than 2 acts";
        goto fail;
      }
      if (KEY_FLD(key, KEY_TYPE_SH, 2) == BRACHA87_READY
       && !KEY_FLD(key, KEY_RCV_SH, 1))
        Quiescent[Self] = 0;
    }
    ActsRet = 1;
    goto qActs;
   qActsr1:
    goto applied;

    /*--------------------------------------------------------------*/
    /*  tick(p) -- the surface's sweep-tick composite, atomically.    */
    /*--------------------------------------------------------------*/

   doTick:
    Self = (unsigned int)evArg;
    --Allow[Self];

    if (Cfg->surface == 1) {
      /* The example re-initializes the cursor per tick over a
       * one-element array, so this call IS a full pass. */
      F1arr[0] = (struct bracha87Fig1 *)Img[Self];
      bracha87RetryInit(&F1cursor);
      NActs = bracha87Fig1RetryStep(F1arr, 1, &F1cursor, Pacts);
      if (NActs > BRACHA87_FIG1_RETRY_MAX_ACTS) {
        FailMsg = "bracha87Fig1RetryStep exceeded its act bound";
        goto fail;
      }
      if (!NActs && bracha87Fig1SentCount(F1arr, 1)) {
        Quiescent[Self] = 1;
        goto applied;
      }
      for (i = 0; i < NActs; ++i) {
        if (Pacts[i].act != BRACHA87_READY_ALL && Pacts[i].received) {
          FailMsg = "a RECEIVED mask rode an act that is not READY_ALL";
          goto fail;
        }
        if (!Pacts[i].value
         || (*Pacts[i].value != Sym[0] && *Pacts[i].value != Sym[1])) {
          FailMsg = "a Fig 1 retry act carries a value outside the run's"
                    " alphabet";
          goto fail;
        }
        PushVal = (*Pacts[i].value == Sym[1]);
        PushType = (unsigned char)
          (Pacts[i].act == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
         : Pacts[i].act == BRACHA87_ECHO_ALL    ? BRACHA87_ECHO
         :                                        BRACHA87_READY);
        PushAcc = Pacts[i].accepted;
        Skip = Pacts[i].skip;
        Received = Pacts[i].received;
        PushRet = 2;
        goto push1;
       push1r2:
        ;
      }
      goto applied;
    }

    /* Surface 2: the retry sub-step is rotation-gated, the turns and
     * the fanout are not -- example/bkr94acs.c. */
    Acsp = (struct bkr94acs *)Img[Self];

    if (!Quiescent[Self]) {
      NActs = bkr94acsRetryStep(Acsp, &Cursor[Self], Acts);
      if (NActs > BKR94ACS_RETRY_MAX_ACTS) {
        FailMsg = "bkr94acsRetryStep exceeded BKR94ACS_RETRY_MAX_ACTS";
        goto fail;
      }
      if (!NActs && bkr94acsFig1SentCount(Acsp))
        Quiescent[Self] = 1;
      ActsRet = 2;
      goto qActs;
     qActsr2:
      ;
    }

    for (q = 0; q < N; ++q) {
      /* The zero-patience turn, with turnSweeps eliminated: the
       * example's >= 0 compare is constant-true, so it calls the turn
       * on every attempt and the turn fires whenever its duty is not
       * HELD -- an unconditional call here is firing-identical. */
      PreDec = bkr94acsBaDecision(Acsp, (unsigned char)q);
      NActs = bkr94acsTurn(Acsp, (unsigned char)q, Acts);
      if (NActs > 3) {
        FailMsg = "bkr94acsTurn output more than 3 acts";
        goto fail;
      }
      if (NActs) {
        Turned[Self][q] = 1;
        Quiescent[Self] = 0;
      }
      ActsRet = 3;
      goto qActs;
     qActsr3:
      ;
    }
    PreDec = 0xFF;

    /* Step 2, after the turns because only a turn produces the
     * decisions it counts.  The guard is duty == TOLERANCE, which is
     * what the eliminated fanoutTicks reduced to and is redundant
     * with bkr94acsFanout's own guard. */
    if (bkr94acsFanoutDuty(Acsp) == BKR94ACS_DUTY_TOLERANCE) {
      NActs = bkr94acsFanout(Acsp, Acts);
      if (NActs > N) {
        FailMsg = "bkr94acsFanout output more than N acts";
        goto fail;
      }
      if (NActs)
        Quiescent[Self] = 0;
      ActsRet = 4;
      goto qActs;
     qActsr4:
      ;
    }
    goto applied;

    /*--------------------------------------------------------------*/
    /*  acast(p) -- the A-Cast submission as a schedulable event.     */
    /*--------------------------------------------------------------*/

   doAcast:
    Self = (unsigned int)evArg;
    Pending[Self] = 0;
    NActs = bkr94acsAcast((struct bkr94acs *)Img[Self], &Aval[Self], Acts);
    if (NActs > 1) {
      FailMsg = "bkr94acsAcast output more than 1 act";
      goto fail;
    }
    /* The bootstrap broadcast honors no suppress mask and marks
     * nobody, exactly as example/bkr94acs.c pushes it. */
    for (i = 0; i < NActs; ++i)
      for (j = 0; j < N; ++j) {
        if (j == Adv)
          continue;
        poolPush(((unsigned long)BRACHA87_INITIAL << KEY_TYPE_SH)
                 | ((unsigned long)(Acts[i].accepted ? 1 : 0) << KEY_ACC_SH)
                 | ((unsigned long)Acts[i].process << KEY_PROC_SH)
                 | ((unsigned long)Self << KEY_FROM_SH)
                 | (j << KEY_TO_SH));
      }
    goto applied;

    /*--------------------------------------------------------------*/
    /*  Shared act-expansion regions.                                */
    /*--------------------------------------------------------------*/

   push1:
    /* Surface 1: one Fig 1 act to every unsuppressed recipient, with
     * the per-recipient RECEIVED bit read off the RECEIVED mask.  The
     * HARNESS SELF-CHECK here is that the bit is set for recipient j
     * only where the mask says so -- framer discipline, since
     * bracha87Fig1Received RETURNS acFrom. */
    for (j = 0; j < N; ++j) {
      if (j == Adv || (Skip && BRACHA87_SKIP_TST(Skip, j)))
        continue;
      poolPush(((unsigned long)PushType << KEY_TYPE_SH)
               | ((unsigned long)(PushAcc ? 1 : 0) << KEY_ACC_SH)
               | ((unsigned long)((Received && BRACHA87_SKIP_TST(Received, j))
                                  ? 1 : 0) << KEY_RCV_SH)
               | ((unsigned long)Self << KEY_FROM_SH)
               | (j << KEY_TO_SH)
               | ((unsigned long)PushVal << KEY_VAL_SH));
    }
    switch (PushRet) {
    case 1:  goto push1r1;
    default: goto push1r2;
    }

   qActs:
    /* Surface 2: the canonical packed-byte framing, shared by the
     * delivery, the retry, the turn and the fanout -- they differ
     * only in what enabled them, never in how their acts are framed
     * (example/bkr94acs.c's qActs). */
    for (i = 0; i < MAX_ACTS && i < NActs; ++i) {
      unsigned long base;

      switch (Acts[i].act) {

      case BKR94ACS_ACT_ACAST_SEND:
        if (!Acts[i].value)
          break;
        /* The key carries the A-Cast value through `process` and the
         * value bit; this is where that identity is checked rather
         * than assumed.  The foreign symbol enters a run only from the
         * adversary, so under none this is the old equality. */
        if (*Acts[i].value == Aval[Acts[i].process])
          base = 0;
        else if (*Acts[i].value == Alt)
          base = 1UL << KEY_VAL_SH;
        else {
          FailMsg = "an A-Cast act carries a value outside the run's"
                    " alphabet";
          goto fail;
        }
        goto frame;

      case BKR94ACS_ACT_BA_SEND:
        base = (1UL << KEY_CLS_SH)
             | ((unsigned long)Acts[i].round << KEY_ROUND_SH)
             | ((unsigned long)Acts[i].initiator << KEY_INIT_SH)
             | ((unsigned long)(Acts[i].baValue & 1) << KEY_BAV_SH)
             | ((Acts[i].baValue & BRACHA87_D_FLAG)
                ? (2UL << KEY_BAV_SH) : 0);
       frame:
        if (Acts[i].type != BRACHA87_READY && Acts[i].received) {
          FailMsg = "a RECEIVED mask rode an act that is not a READY";
          goto fail;
        }
        base |= ((unsigned long)Acts[i].type << KEY_TYPE_SH)
              | ((unsigned long)(Acts[i].accepted ? 1 : 0) << KEY_ACC_SH)
              | ((unsigned long)Acts[i].process << KEY_PROC_SH)
              | ((unsigned long)Self << KEY_FROM_SH);
        for (j = 0; j < N; ++j) {
          if (j == Adv || (Acts[i].skip && BRACHA87_SKIP_TST(Acts[i].skip, j)))
            continue;
          poolPush(base
                   | ((unsigned long)((Acts[i].received
                                       && BRACHA87_SKIP_TST(Acts[i].received, j))
                                      ? 1 : 0) << KEY_RCV_SH)
                   | (j << KEY_TO_SH));
        }
        break;

      case BKR94ACS_ACT_BA_DECIDED:
        if (ActsRet != 3) {
          FailMsg = "BA_DECIDED came from something other than a turn";
          goto fail;
        }
        if (PreDec != 0xFF) {
          FailMsg = "BA_DECIDED for a BA that already had a decision";
          goto fail;
        }
        break;

      case BKR94ACS_ACT_COMPLETE:
        if (ActsRet != 3) {
          FailMsg = "COMPLETE came from something other than a turn";
          goto fail;
        }
        break;

      case BKR94ACS_ACT_BA_EXHAUSTED:
        if (ActsRet != 3) {
          FailMsg = "BA_EXHAUSTED came from something other than a turn";
          goto fail;
        }
        if (PreDec != 0xFF) {
          FailMsg = "BA_EXHAUSTED output more than once for one BA";
          goto fail;
        }
        if (bkr94acsBaDecision((struct bkr94acs *)Img[Self],
                               Acts[i].process) != 0xFE) {
          FailMsg = "BA_EXHAUSTED without the 0xFE sentinel";
          goto fail;
        }
        if (((struct bkr94acs *)Img[Self])->complete) {
          FailMsg = "BA_EXHAUSTED with complete set";
          goto fail;
        }
        break;

      default:
        FailMsg = "an act carried an unknown BKR94ACS_ACT_* value";
        goto fail;
      }
    }
    switch (ActsRet) {
    case 1:  goto qActsr1;
    case 2:  goto qActsr2;
    case 3:  goto qActsr3;
    default: goto qActsr4;
    }

    /*--------------------------------------------------------------*/
    /*  Post-transition oracle, then descend.                        */
    /*--------------------------------------------------------------*/

   applied:
    if (Cfg->surface == 1) {
      for (p = 0; p < N; ++p) {
        const unsigned char *ac;
        const unsigned char *rd;

        if (p == Adv)
          continue;
        /* acFrom \ {self} is a subset of rdFrom.  acFrom raw is the
         * RECEIVED mask; rdFrom is the ECHO_ALL suppress mask.  The self
         * bit is excluded because the self-accept is recorded with no
         * rdFrom guard while this process's own (ready, v) can still
         * be pending. */
        ac = bracha87Fig1Received((struct bracha87Fig1 *)Img[p]);
        rd = bracha87Fig1Skip((struct bracha87Fig1 *)Img[p],
                              BRACHA87_ECHO_ALL);
        if (ac && rd)
          for (q = 0; q < N; ++q) {
            if (q == p)
              continue;
            if (BRACHA87_SKIP_TST(ac, q) && !BRACHA87_SKIP_TST(rd, q)) {
              FailMsg = "acFrom \\ {self} is not a subset of rdFrom";
              goto fail;
            }
          }
        /* Lemma 1: two correct READY senders carry the same value.
         * Lemma 2: any two accepts of one broadcast agree.  Both
         * pairwise over the honest processes; under an honest
         * initiator the per-act value check above already implies
         * them, under an adversary initiator they are the live ones. */
        for (q = p + 1; q < N; ++q) {
          const struct bracha87Fig1 *fp;
          const struct bracha87Fig1 *fq;

          if (q == Adv)
            continue;
          fp = (struct bracha87Fig1 *)Img[p];
          fq = (struct bracha87Fig1 *)Img[q];
          if ((fp->flags & BRACHA87_F1_RDSENT)
           && (fq->flags & BRACHA87_F1_RDSENT)
           && *bracha87Fig1Value(fp) != *bracha87Fig1Value(fq)) {
            FailMsg = "Lemma 1: two READY senders carry different values";
            goto fail;
          }
          if (Accepted[p] && Accepted[q] && AcceptVal[p] != AcceptVal[q]) {
            FailMsg = "Lemma 2: two accepts of one broadcast disagree";
            goto fail;
          }
        }
      }
    } else {
      for (p = 0; p < N; ++p) {
        if (p == Adv)
          continue;
        Acsp = (struct bkr94acs *)Img[p];
        if (bkr94acsFanoutDuty(Acsp) < PreFan[p]) {
          FailMsg = "bkr94acsFanoutDuty is not monotone";
          goto fail;
        }
        for (q = 0; q < N; ++q) {
          if (!Turned[p][q]
           && bkr94acsTurnDuty(Acsp, (unsigned char)q) < PreTurn[p][q]) {
            FailMsg = "bkr94acsTurnDuty fell back within one round";
            goto fail;
          }
          if (PreEnt[p][q] && !bkr94acsBaEntered(Acsp, (unsigned char)q)) {
            FailMsg = "bkr94acsBaEntered is not latched";
            goto fail;
          }
        }
      }
      /* Per process j, over the honest processes:
       *   - Lemma 2 on A-Cast j: the accepted values agree
       *     (bkr94acsAcastValue is ACCEPT-gated for non-self);
       *   - Theorem 2 on BA_j: the decisions agree;
       *   - C1 (BenOr83, the validity Theorem 2 inherits): once every
       *     honest process has entered BA_j with one value v, no
       *     honest decision on it is the other.  Decisions latch, so a
       *     decision taken before the last entry is caught by the
       *     transition that makes it.  The entered value is the
       *     INITIATOR slot of the round-0 BA Fig 1 this process
       *     initiated, read off the image. */
      for (q = 0; q < N; ++q) {
        const unsigned char *av;
        unsigned int dec;
        unsigned int ent;
        int allEnt;

        av = 0;
        dec = 0xFF;
        ent = 0xFF;
        allEnt = 1;
        for (p = 0; p < N; ++p) {
          const unsigned char *v;
          unsigned char d;

          if (p == Adv)
            continue;
          Acsp = (struct bkr94acs *)Img[p];
          if ((v = bkr94acsAcastValue(Acsp, q))) {
            if (!av)
              av = v;
            else if (*av != *v) {
              FailMsg = "Lemma 2: two accepts of one A-Cast disagree";
              goto fail;
            }
          }
          d = bkr94acsBaDecision(Acsp, q);
          if (d <= 1) {
            if (dec == 0xFF)
              dec = d;
            else if (dec != d) {
              FailMsg = "Theorem 2: two honest processes decided one BA"
                        " differently";
              goto fail;
            }
          }
          if (!bkr94acsBaEntered(Acsp, q)) {
            allEnt = 0;
            continue;
          }
          if (!(v = bracha87Fig1Value(bkr94acsBaFig1(Acsp, q, 0, p)))) {
            FailMsg = "an entered BA's round-0 Fig 1 carries no value";
            goto fail;
          }
          if (ent == 0xFF)
            ent = *v & 1;
          else if (ent != (*v & 1))
            ent = 2;
        }
        if (allEnt && ent <= 1 && dec <= 1 && dec != ent) {
          FailMsg = "C1: every honest process entered one value and an"
                    " honest process decided the other";
          goto fail;
        }
      }
    }

    explore(depth + 1);

    /*--------------------------------------------------------------*/
    /*  Restore -- memcpy back into the SAME allocations.            */
    /*--------------------------------------------------------------*/

    PoolLive = (unsigned int)nLive;
    PoolTot = 0;
    sum = 0;
    for (i = 0; i < nLive; ++i) {
      PoolKey[i] = KeyStk[koff + i];
      PoolCnt[i] = NumStk[noff + i];
      PoolTot += PoolCnt[i];
      sum = sum * 31 + PoolKey[i] + PoolCnt[i];
    }
    for (p = 0; p < N; ++p) {
      Cursor[p].pos = NumStk[noff + nLive + 2 * p];
      Cursor[p].sweepActs = NumStk[noff + nLive + 2 * p + 1];
      sum = sum * 31 + Cursor[p].pos + Cursor[p].sweepActs;
      memcpy(Img[p], BytStk + boff + p * ImgSz, ImgSz);
    }
    /*
     * THE INSTRUMENT AUDITING ITSELF, and it is here because this
     * exact check was missing when it was needed.  The typed arenas
     * are the one place in this file with offset arithmetic a frame
     * can get wrong, and a frame that overwrites its parent's words
     * corrupts a search that still reports counts and still passes:
     * on 2026-09-10 the number arena was based at the key arena's
     * top, every child frame's pool counts landed on the parent's
     * cursor words, and every restore read them back as cursors.  It
     * survived two full mutation runs and re-baselined every frozen
     * count in the table before a reading of the arithmetic found it.
     * Measured against that defect reintroduced: this reds inside the
     * first seventeen events of the smallest config, and prints the
     * witness that reaches it.
     *
     * Scope: the two typed arenas only.  The image bytes go through
     * BytStk by memcpy and checksumming them would double the copy
     * this search spends most of its time in -- so a corruption of
     * the image arena is still caught only by the counts moving,
     * which is the weaker signal this whole file calls sensitivity.
     */
    if (sum != frameSum) {
      FailMsg = "the snapshot arenas did not survive the descent -- a"
                " frame's pool or cursor words came back changed, so"
                " the search below this state explored something other"
                " than this state";
      goto fail;
    }
    memcpy(Quiescent, BytStk + boff + N * ImgSz + 0 * MAX_N, MAX_N);
    memcpy(Accepted,  BytStk + boff + N * ImgSz + 1 * MAX_N, MAX_N);
    memcpy(AcceptVal, BytStk + boff + N * ImgSz + 2 * MAX_N, MAX_N);
    memcpy(Pending,   BytStk + boff + N * ImgSz + 3 * MAX_N, MAX_N);
    memcpy(Allow,     BytStk + boff + N * ImgSz + 4 * MAX_N, MAX_N);
  }

  if (WitMode && depth < WitLen && !took) {
    WitStuck = 1;
    printf("  ** the re-run's event at depth %lu is not enabled in the"
           " state it reaches -- the sequence does not fit this config"
           " or this machine **\n", depth);
  }

  KeyStkTop = koff;
  NumStkTop = noff;
  BytStkTop = boff;
  return;

 fail:
  Failed = 1;
  printf("\nFAILURE: %s\n", FailMsg);
  if (Adv != 0xFF) {
    printf("strategy: ");
    for (i = 0; i < SelCnt; ++i)
      printf("%s%lx", i ? "," : "", Gram[Sel[i]]);
    printf("%s\n", SelCnt ? "" : "(silent)");
  }
  printf("witness: ");
  for (i = 0; i <= depth && PathKind[i]; ++i) {
    if (i)
      printf(",");
    if (PathKind[i] == EV_DELIVER)
      printf("d%lx", PathArg[i]);
    else
      printf("%c%lu", (PathKind[i] == EV_TICK) ? 't' : 'a', PathArg[i]);
  }
  printf("\nre-run: ./test_schedules %s-w <the sequence above> %s\n",
         (Adv != 0xFF && SelCnt) ? "-s <the strategy above> " : "",
         Cfg->name);
}

/*--------------------------------------------------------------------------*/
/*  Main -- set a config up, run it once per strategy, report, assert.      */
/*                                                                          */
/*  Under no adversary there is exactly one strategy, the empty one, and    */
/*  the run is the one the frozen counts were measured on.  Under an        */
/*  adversary the grammar is built, then walked: the silent strategy        */
/*  first, then every subset of at most J contents drawn from one           */
/*  instance's part of the grammar, each seeded at the root of its own      */
/*  explorer run.  Re-seeding is a full reset of the run state; the         */
/*  allocations are made once per config.                                   */
/*--------------------------------------------------------------------------*/

int
main(
  int argc
 ,char *argv[]
){
  int arg;
  int measure;
  int exitCode;
  unsigned long argCeilStates;
  unsigned long argCeilDepth;
  unsigned int argHashBits;
  unsigned int argK;
  unsigned int argJ;
  const char *witness;
  const char *strategy;
  const char *want;
  unsigned int c;
  unsigned int i;
  unsigned int ran;
  unsigned int part;
  unsigned long silentStates;
  unsigned long silentQuiescent;
  unsigned long silentExhausted;
  unsigned long silentAllowance;
  int silentComplete;

  measure = 0;
  exitCode = 0;
  argCeilStates = 0;
  argCeilDepth = 0;
  argHashBits = 0;
  argK = 0;
  argJ = 0;
  witness = 0;
  strategy = 0;
  ran = 0;

  arg = 1;
  while (arg < argc && argv[arg][0] == '-') {
    if (argv[arg][1] == 'm' && !argv[arg][2]) {
      measure = 1;
      ++arg;
    } else if (argv[arg][1] == 'c' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argCeilStates = strtoul(argv[arg++], 0, 10);
    } else if (argv[arg][1] == 'k' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argK = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'J' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argJ = atoi(argv[arg++]);
    } else if (argv[arg][1] == 'D' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argCeilDepth = strtoul(argv[arg++], 0, 10);
    } else if (argv[arg][1] == 'b' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argHashBits = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'w' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      witness = argv[arg++];
    } else if (argv[arg][1] == 's' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      strategy = argv[arg++];
    } else
      goto usage;
  }
  if (arg >= argc)
    goto usage;
  want = argv[arg];

  if (sizeof (unsigned long) < 8) {
    fprintf(stderr,
            "test_schedules: needs a 64-bit unsigned long -- a 32-bit"
            " state fingerprint collides, and a collision UNDER-MERGES\n");
    return (2);
  }

  printf("test_schedules: bounded reachability over the example loops'"
         " state graph\n");

  for (c = 0; c < sizeof (Configs) / sizeof (Configs[0]); ++c) {
    unsigned int bits;

    Cfg = &Configs[c];
    if (strcmp(want, "all")
     && strcmp(want, Cfg->name)
     && !(!strcmp(want, "smoke") && Cfg->smoke)
     && !(!strcmp(want, "strategies") && Cfg->adv != 0xFF && !Cfg->smoke))
      continue;
    ++ran;

    N = Cfg->n;
    T = Cfg->t;
    Adv = Cfg->adv;
    Initiator = Cfg->advInit ? Adv : 0;
    Sym[0] = Val1;
    Sym[1] = 'x';
    if (argK)
      Cfg->k = argK;
    if (Cfg->k > 255) {
      fprintf(stderr, "test_schedules: K above 255 does not fit the"
              " allowance vector\n");
      return (2);
    }
    if (argJ > SEL_MAX || Cfg->j > SEL_MAX) {
      fprintf(stderr, "test_schedules: J above %u does not fit the"
              " strategy\n", (unsigned)SEL_MAX);
      return (2);
    }
    if (argJ)
      Cfg->j = argJ;
    if (strategy && Adv == 0xFF) {
      fprintf(stderr, "test_schedules: -s needs a config with an"
              " adversary\n");
      return (2);
    }
    if (argCeilStates)
      Cfg->ceilStates = argCeilStates;
    if (argCeilDepth)
      Cfg->ceilDepth = argCeilDepth;

    /*----------------------------------------------------------------*/
    /*  Fixed allocations, held for the whole config: that is what     */
    /*  makes the byte fingerprint sound (see the snapshot audit       */
    /*  above).  The adversary's image is allocated and initialized    */
    /*  like the others and never called, so the fingerprint loop is   */
    /*  the same loop with or without one.                             */
    /*----------------------------------------------------------------*/

    if (Cfg->surface == 1)
      ImgSz = bracha87Fig1Sz(N - 1, 0);
    else
      ImgSz = bkr94acsSz(N - 1, 0, Cfg->maxPhases);

    for (i = 0; i < N; ++i) {
      if (!(Img[i] = calloc(1, ImgSz))) {
        fprintf(stderr, "test_schedules: image allocation failed\n");
        return (2);
      }
      Aval[i] = (unsigned char)('A' + i);
    }

    bits = argHashBits ? argHashBits : Cfg->hashBits;
    HashSz = 1UL << bits;
    FrCap = Cfg->keyAllow ? 2 : (unsigned int)(HashSz / 2);
    PathCap = Cfg->ceilDepth + 8;
    KeyStkSz = NumStkSz = 4096;
    BytStkSz = 1UL << 20;

    if (!(Hash = calloc(HashSz, sizeof (unsigned long)))
     || !(HashHead = calloc(HashSz, sizeof (unsigned int)))
     || !(FrAllow = calloc(FrCap, MAX_N))
     || !(FrNext = calloc(FrCap, sizeof (unsigned int)))
     || !(PathKind = calloc(PathCap, 1))
     || !(PathArg = calloc(PathCap, sizeof (unsigned long)))
     || !(KeyStk = calloc(KeyStkSz, sizeof (unsigned long)))
     || !(NumStk = calloc(NumStkSz, sizeof (unsigned int)))
     || !(BytStk = calloc(BytStkSz, 1))) {
      fprintf(stderr, "test_schedules: run allocation failed\n");
      return (2);
    }

    /*----------------------------------------------------------------*/
    /*  The adversary's grammar, partitioned by instance.  Every       */
    /*  content here is well-formed and authenticated as the           */
    /*  adversary: `from` is fixed, and an INITIAL appears only on an  */
    /*  instance the adversary initiates (the library drops the        */
    /*  others at ingress -- bkr94acs{Acast,Ba}Input -- and surface    */
    /*  1's caller filter does the same, so a forged INITIAL is not a  */
    /*  strategy but a dropped byte).  `to` ranges over the honest     */
    /*  processes.  The value alphabet per instance is TWO symbols: a  */
    /*  A-Cast's own value and one foreign symbol, {0, 1} for a BA     */
    /*  round 3i / 3i+1, {(d,0), (d,1)} for a round 3i+2.  A READY     */
    /*  carries all four annotation settings at surface 1 and the one   */
    /*  announcing, marked setting at surface 2 (the loop below).       */
    /*                                                                 */
    /*  THE BOUNDS, and the defect each would miss:                    */
    /*    one identity    -- t = 1 here and one process speaks; a       */
    /*                       threshold defect that only two colluding   */
    /*                       identities reach (2t+1 readys served by 2t */
    /*                       at t = 2, say) is outside every config.    */
    /*    two symbols     -- every pairwise property checked here       */
    /*                       (Lemmas 1 and 2, Theorem 2's agreement) is */
    /*                       stated over two values, and a third can    */
    /*                       only lower a per-value count.  The BA      */
    /*                       alphabet is where this bites: a round has  */
    /*                       four well-formed bytes, {0, 1} x D_FLAG,   */
    /*                       and the grammar emits two per round, so    */
    /*                       Fig 3's rejection of a (d,v) below a 3i+2  */
    /*                       round, or of a plain v at one, never sees  */
    /*                       a Byzantine byte.                          */
    /*    one instance    -- a strategy's contents all name one Fig 1   */
    /*                       instance; a defect needing the adversary   */
    /*                       to act on two instances at once (a cross-  */
    /*                       instance count, a BA round fed from two    */
    /*                       forged broadcasts) is outside it.          */
    /*    J               -- at most J contents; at n = 4 a full        */
    /*                       equivocation with supporting echoes needs  */
    /*                       more than the floor of three, so a defect  */
    /*                       that only a J+1-content strategy reaches   */
    /*                       is outside it.  Printed like K.            */
    /*    one copy        -- a content is seeded once.  READYs that     */
    /*                       differ only in annotation are copies of    */
    /*                       one (ready, v) to Fig 1's per-sender       */
    /*                       dedup, so a surface-1 strategy can carry   */
    /*                       up to four copies to one receiver, two of  */
    /*                       them unmarked -- the re-arming forger's    */
    /*                       duplicate -- and no third unmarked one; a  */
    /*                       surface-2 strategy carries one copy, which */
    /*                       arms nothing.                              */
    /*    one timing      -- THE BOUND THAT BINDS.  Every content is    */
    /*                       pending from the root, and the DFS drains  */
    /*                       deliveries in key order before it ticks;   */
    /*                       under the per-strategy ceiling the         */
    /*                       backtracking never climbs back to an       */
    /*                       adversary delivery's siblings (measured:   */
    /*                       at s3, b2, b3 no seeded content lands      */
    /*                       after any honest tick, and none is ever    */
    /*                       delivered in a second order).  So at every */
    /*                       config but b1 -- where the adversary is    */
    /*                       the only root sender -- a content's TIMING */
    /*                       is one sample, the key order's: by value   */
    /*                       bit, then recipient, then sender (honest   */
    /*                       before the adversary), and only then       */
    /*                       INITIAL before ECHO before READY.  A defect */
    /*                       that needs the adversary's READY to land   */
    /*                       after an honest egress consumed an arm, or */
    /*                       its round-0 value to land after the honest */
    /*                       turn, is outside it.  The content axis is  */
    /*                       what this enumerates; the order axis it    */
    /*                       inherits only as far as the ceiling lets   */
    /*                       the first dive's tail vary.                */
    /*----------------------------------------------------------------*/

    GramCnt = 0;
    PartCnt = 0;
    if (Adv != 0xFF && !strategy) {
      unsigned long base;
      unsigned int inst;
      unsigned int r;
      unsigned int w;
      unsigned int v;
      unsigned int a;
      int own;

      if (Cfg->surface == 1) {
        inst = 0;
        own = Cfg->advInit;
        base = 0;
        goto gramInst;
      }
      for (inst = 0; inst < N * (1 + 3u * Cfg->maxPhases * N); ++inst) {
        /* inst enumerates A-Cast[p], then BA[p][r][w]. */
        if (inst < N) {
          own = ((unsigned int)inst == Adv);
          if (!(Cfg->advInst & (own ? ADV_INST_OWN_ACAST : ADV_INST_ACAST)))
            continue;
          base = ((unsigned long)inst << KEY_PROC_SH);
        } else {
          w = (inst - N) % N;
          r = ((inst - N) / N) % (3u * Cfg->maxPhases);
          own = (w == Adv);
          if (!(Cfg->advInst & (own ? ADV_INST_OWN_BA : ADV_INST_BA)))
            continue;
          base = (1UL << KEY_CLS_SH)
               | ((unsigned long)((inst - N) / N / (3u * Cfg->maxPhases))
                  << KEY_PROC_SH)
               | ((unsigned long)r << KEY_ROUND_SH)
               | ((unsigned long)w << KEY_INIT_SH);
        }
       gramInst:
        if (PartCnt >= sizeof (PartLo) / sizeof (PartLo[0])) {
          fprintf(stderr, "test_schedules: instance overflow\n");
          return (2);
        }
        PartLo[PartCnt] = GramCnt;
        for (i = 0; i < N; ++i) {
          if (i == Adv)
            continue;
          for (v = 0; v < 2; ++v) {
            unsigned long k;

            k = base | ((unsigned long)Adv << KEY_FROM_SH)
                     | ((unsigned long)i << KEY_TO_SH);
            /* The value: bit 29 for an A-Cast or surface-1 symbol,
             * the BA value field for a BA content -- {0, 1} below a
             * 3i+2 round, {(d,0), (d,1)} at one. */
            if (!KEY_FLD(base, KEY_CLS_SH, 1))
              k |= (unsigned long)v << KEY_VAL_SH;
            else if (KEY_FLD(base, KEY_ROUND_SH, 6) % 3 != 2)
              k |= (unsigned long)v << KEY_BAV_SH;
            else
              k |= (unsigned long)(2 + v) << KEY_BAV_SH;
            if (GramCnt + 6 > GRAM_MAX) {
              fprintf(stderr, "test_schedules: grammar overflow\n");
              return (2);
            }
            if (own)
              Gram[GramCnt++] = k | ((unsigned long)BRACHA87_INITIAL
                                     << KEY_TYPE_SH);
            Gram[GramCnt++] = k | ((unsigned long)BRACHA87_ECHO
                                   << KEY_TYPE_SH);
            /* Surface 1 seeds a READY under all four annotation
             * settings; surface 2 seeds it announcing and marked
             * (ACCEPTED and RECEIVED both set), the setting that arms
             * nothing.  The annotation exchange's Byzantine arms are
             * the annotation forgery tests', the surface-1 configs
             * here carry every setting through the same Fig 1, and
             * QUIESCENT is unreachable at surface 2 under an adversary
             * silent on any instance -- so what this bound gives up
             * is a composition-level defect reachable only through
             * an unannounced or unmarked READY on one instance. */
            for (a = (Cfg->surface == 1) ? 0 : 3; a < 4; ++a)
              Gram[GramCnt++] = k | ((unsigned long)BRACHA87_READY
                                     << KEY_TYPE_SH)
                                  | ((unsigned long)(a & 1) << KEY_ACC_SH)
                                  | ((unsigned long)(a >> 1) << KEY_RCV_SH);
          }
        }
        PartHi[PartCnt] = GramCnt;
        ++PartCnt;
        if (Cfg->surface == 1)
          break;
      }
    }

    if (strategy) {
      /* A hand-seeded strategy is the whole grammar and the one
       * strategy, so the failure printer and the seeding read it the
       * same way. */
      const char *s;

      s = strategy;
      while (*s) {
        if (GramCnt >= SEL_MAX)
          goto usage;
        Gram[GramCnt] = strtoul(s, 0, 16);
        /* A hand key is the harness's own index space: a recipient
         * outside the honest processes, or a sender that is not the
         * adversary, is refused rather than indexed. */
        if (KEY_FLD(Gram[GramCnt], KEY_TO_SH, 4) >= N
         || KEY_FLD(Gram[GramCnt], KEY_TO_SH, 4) == Adv
         || KEY_FLD(Gram[GramCnt], KEY_FROM_SH, 4) != Adv
         || KEY_FLD(Gram[GramCnt], KEY_PROC_SH, 4) >= N
         || KEY_FLD(Gram[GramCnt], KEY_INIT_SH, 4) >= N
         || (KEY_FLD(Gram[GramCnt], KEY_CLS_SH, 1)
             && KEY_FLD(Gram[GramCnt], KEY_ROUND_SH, 6)
                >= 3u * Cfg->maxPhases)) {
          fprintf(stderr, "test_schedules: -s key %lx names a process,"
                  " sender or round outside this config\n", Gram[GramCnt]);
          return (2);
        }
        Sel[GramCnt] = GramCnt;
        ++GramCnt;
        while (*s && *s != ',')
          ++s;
        if (*s == ',')
          ++s;
      }
      PartLo[0] = 0;
      PartHi[0] = GramCnt;
      PartCnt = 1;
    }

    /*----------------------------------------------------------------*/
    /*  Announce the bounds.                                           */
    /*----------------------------------------------------------------*/

    printf("\n--- config %s: %s ---\n", Cfg->name, Cfg->note);
    printf("  bounds: K=%u ticks per process (%s),"
           " state ceiling=%lu, depth ceiling=%lu\n",
           Cfg->k,
           (Cfg->surface == 1)
             ? "one tick = one full retry pass"
             : "one tick = one retry CALL = one cursor position",
           Cfg->ceilStates, Cfg->ceilDepth);
    if (Cfg->surface == 2)
      printf("  maxPhases=%u (A KNOB -- an empty EXHAUSTED class is a"
             " reading of it, never a protocol fact); a full retry pass"
             " is %lu calls, N + N*(3*maxPhases)*N\n",
             (unsigned)Cfg->maxPhases,
             (unsigned long)N
             + (unsigned long)N * (3UL * Cfg->maxPhases) * (unsigned long)N);
    printf("  allowance: %s; every frozen count below is a"
           " regression constant, not a property of the system\n",
           Cfg->keyAllow
             ? "in the state key, so a state's IDENTITY does not depend"
               " on the order it was reached in"
             : "Pareto dominance, so even a state's identity depends on"
               " the order it was reached in");
    if (Adv != 0xFF) {
      unsigned long count;

      /* The strategy count: 1 (silent) + per part, sum over sizes
       * 1..J of C(|part|, size). */
      count = 1;
      for (part = 0; part < PartCnt; ++part) {
        unsigned long g;
        unsigned long comb;
        unsigned int k;

        g = PartHi[part] - PartLo[part];
        comb = 1;
        for (k = 1; k <= Cfg->j && k <= g; ++k) {
          comb = comb * (g - k + 1) / k;
          count += comb;
        }
      }
      printf("  adversary: process %u, %s; grammar |G|=%u contents over"
             " %u instance%s (one identity, two symbols per instance, one"
             " instance per strategy, one copy per content, and under the"
             " ceiling ONE TIMING per content -- the key order's); J=%u"
             " contents per strategy; %lu strategies, each its own"
             " explorer run under the ceilings above -- every count below"
             " is a SUM over them\n",
             Adv,
             Cfg->surface == 1
               ? (Cfg->advInit ? "the initiator" : "not the initiator")
               : (Cfg->advInst & (ADV_INST_OWN_ACAST | ADV_INST_OWN_BA))
                 ? "its own broadcasts" : "the honest broadcasts",
             GramCnt, PartCnt, PartCnt == 1 ? "" : "s",
             (unsigned)Cfg->j, strategy ? 1UL : count);
    }

    if (witness) {
      const char *s;

      WitLen = 0;
      if (!(WitKind = calloc(PathCap, 1))
       || !(WitArg = calloc(PathCap, sizeof (unsigned long)))) {
        fprintf(stderr, "test_schedules: witness allocation failed\n");
        return (2);
      }
      s = witness;
      while (*s && WitLen < PathCap) {
        if (*s == 'd') {
          WitKind[WitLen] = EV_DELIVER;
          WitArg[WitLen] = strtoul(s + 1, 0, 16);
        } else if (*s == 't' || *s == 'a') {
          WitKind[WitLen] = (unsigned char)
            ((*s == 't') ? EV_TICK : EV_ACAST);
          WitArg[WitLen] = strtoul(s + 1, 0, 10);
        } else
          goto usage;
        ++WitLen;
        while (*s && *s != ',')
          ++s;
        if (*s == ',')
          ++s;
      }
    }

    TotStates = TotEdges = 0;
    TotQuiescent = TotExhausted = TotAllowance = TotCeiling = 0;
    TotMaxDepth = 0;
    StratCnt = StratQuiescent = StratComplete = 0;
    StratSubFull = StratSubShort = StratExhausted = StratAdvIn = 0;
    StratAllAccepted = 0;
    silentStates = silentQuiescent = silentExhausted = silentAllowance = 0;
    silentComplete = 0;
    part = 0;
    SelCnt = strategy ? GramCnt : 0;

    /*----------------------------------------------------------------*/
    /*  One explorer run per strategy.                                 */
    /*----------------------------------------------------------------*/

   nextStrategy:
    for (i = 0; i < N; ++i) {
      if (Cfg->surface == 1)
        bracha87Fig1Init((struct bracha87Fig1 *)Img[i],
                         (unsigned char)(N - 1), (unsigned char)T, 0);
      else
        bkr94acsInit((struct bkr94acs *)Img[i], (unsigned char)(N - 1),
                     (unsigned char)T, 0, Cfg->maxPhases,
                     (unsigned char)i, demoCoin, 0);
      bracha87RetryInit(&Cursor[i]);
      Allow[i] = (i == Adv) ? 0 : Cfg->k;
    }

    memset(Quiescent, 0, sizeof (Quiescent));
    memset(Accepted, 0, sizeof (Accepted));
    memset(AcceptVal, 0, sizeof (AcceptVal));
    memset(Pending, 0, sizeof (Pending));
    PoolLive = 0;
    PoolTot = 0;

    States = Edges = 0;
    TermQuiescent = TermExhausted = TermAllowance = 0;
    CeilingCuts = 0;
    MaxDepthSeen = 0;
    CeilingHit = TableFull = Failed = 0;
    SawQuiescent = SawSubsetFull = SawSubsetShort = 0;
    SawComplete = SawAdvIn = SawAllAccepted = 0;
    FirstQuiescent = 0;
    memset(Hash, 0, HashSz * sizeof (unsigned long));
    memset(HashHead, 0, HashSz * sizeof (unsigned int));
    HashCnt = 0;
    FrCnt = 1;
    KeyStkTop = NumStkTop = BytStkTop = 0;

    /* The root state: the honest processes' broadcasts, then the
     * strategy's contents. */
    if (Cfg->surface == 1) {
      if (!Cfg->advInit) {
        bracha87Fig1Initiator((struct bracha87Fig1 *)Img[Initiator], &Val1);
        for (i = 0; i < N; ++i) {
          if (i == Adv)
            continue;
          poolPush(((unsigned long)BRACHA87_INITIAL << KEY_TYPE_SH)
                   | ((unsigned long)Initiator << KEY_FROM_SH)
                   | ((unsigned long)i << KEY_TO_SH));
        }
      }
    } else
      for (i = 0; i < N; ++i) {
        struct bkr94acsAct act;
        unsigned int j;

        if (i == Adv)
          continue;
        if (Cfg->defer == i) {
          /* A deferred submission is an EVENT, not a root fact --
           * BKR94ACS.txt, and the example's -d is exactly this. */
          Pending[i] = 1;
          continue;
        }
        if (bkr94acsAcast((struct bkr94acs *)Img[i], &Aval[i], &act) != 1)
          continue;
        for (j = 0; j < N; ++j) {
          if (j == Adv)
            continue;
          poolPush(((unsigned long)BRACHA87_INITIAL << KEY_TYPE_SH)
                   | ((unsigned long)(act.accepted ? 1 : 0) << KEY_ACC_SH)
                   | ((unsigned long)act.process << KEY_PROC_SH)
                   | ((unsigned long)i << KEY_FROM_SH)
                   | ((unsigned long)j << KEY_TO_SH));
        }
      }
    for (i = 0; i < SelCnt; ++i)
      poolPush(Gram[Sel[i]]);

    if (witness) {
      WitMode = 1;
      WitStuck = 0;
      printf("  re-deriving %lu events from the root:\n", WitLen);
      explore(0);
      WitMode = 0;
      if (WitStuck)
        exitCode = 1;
      printf("  re-derivation ended: QUIESCENT=%lu EXHAUSTED=%lu"
             " ALLOWANCE-EXHAUSTED=%lu\n",
             TermQuiescent, TermExhausted, TermAllowance);
    } else
      explore(0);

    TotStates += States;
    TotEdges += Edges;
    TotQuiescent += TermQuiescent;
    TotExhausted += TermExhausted;
    TotAllowance += TermAllowance;
    TotCeiling += CeilingCuts;
    if (MaxDepthSeen > TotMaxDepth)
      TotMaxDepth = MaxDepthSeen;
    ++StratCnt;
    StratQuiescent += SawQuiescent ? 1 : 0;
    StratComplete += SawComplete ? 1 : 0;
    StratSubFull += SawSubsetFull ? 1 : 0;
    StratSubShort += SawSubsetShort ? 1 : 0;
    StratExhausted += TermExhausted ? 1 : 0;
    StratAdvIn += SawAdvIn ? 1 : 0;
    StratAllAccepted += SawAllAccepted ? 1 : 0;
    if (StratCnt == 1) {
      silentStates = States;
      silentQuiescent = TermQuiescent;
      silentExhausted = TermExhausted;
      silentAllowance = TermAllowance;
      silentComplete = SawComplete;
    }
    if (Failed || TableFull)
      exitCode = 1;

    /* The next strategy: the subsets of one part in ascending size,
     * each size in lexicographic order; then the next part. */
    if (Adv != 0xFF && !strategy && !witness && !Failed && !TableFull) {
      unsigned int lo;
      unsigned int hi;
      unsigned int k;

      for (;;) {
        lo = PartLo[part];
        hi = PartHi[part];
        if (SelCnt) {
          k = SelCnt;
          while (k > 0) {
            --k;
            if (Sel[k] + 1 < hi - (SelCnt - 1 - k)) {
              ++Sel[k];
              for (++k; k < SelCnt; ++k)
                Sel[k] = Sel[k - 1] + 1;
              goto nextStrategy;
            }
          }
        }
        if (SelCnt < Cfg->j && SelCnt < hi - lo) {
          ++SelCnt;
          for (k = 0; k < SelCnt; ++k)
            Sel[k] = lo + k;
          goto nextStrategy;
        }
        if (++part >= PartCnt)
          break;
        SelCnt = 0;
      }
    }

    /*----------------------------------------------------------------*/
    /*  Report, then the whole-config assertions.                      */
    /*----------------------------------------------------------------*/

    if (Adv != 0xFF) {
      printf("  strategies run: %lu\n", StratCnt);
      printf("  %s: states=%lu QUIESCENT=%lu"
             " EXHAUSTED=%lu ALLOWANCE-EXHAUSTED=%lu%s\n",
             strategy ? "this strategy" : "the silent strategy alone",
             silentStates, silentQuiescent, silentExhausted,
             silentAllowance,
             Cfg->surface == 2
               ? (silentComplete ? " completion reached"
                                 : " completion not reached")
               : "");
      printf("  strategies reaching: QUIESCENT %lu, EXHAUSTED %lu",
             StratQuiescent, StratExhausted);
      if (Cfg->surface == 1)
        printf(", every honest process accepted %lu", StratAllAccepted);
      else
        printf(", completion %lu, |SubSet|=n %lu, |SubSet|<n %lu,"
               " the adversary in SubSet %lu",
               StratComplete, StratSubFull, StratSubShort, StratAdvIn);
      printf("\n");
    }
    printf("  states=%lu edges=%lu maxDepth=%lu\n",
           TotStates, TotEdges, TotMaxDepth);
    printf("  terminals: QUIESCENT=%lu EXHAUSTED=%lu"
           " ALLOWANCE-EXHAUSTED=%lu\n",
           TotQuiescent, TotExhausted, TotAllowance);
    if (TableFull)
      printf("  ** VISITED TABLE FULL at %lu entries -- the search is"
             " INCOMPLETE; raise -b **\n", HashCnt);
    if (SawQuiescent && StratCnt == 1)
      printf("  first QUIESCENT terminal at state %lu -- the depth at"
             " which the reachability detector separates a stall from a"
             " ceiling\n", FirstQuiescent);
    if (TotCeiling)
      printf("  ** CEILING HIT (%lu cuts) -- exhaustive-within-K is VOID"
             " for this config; the counts are a deterministic prefix"
             " of the search under the branch order above, not the whole"
             " of it **\n", TotCeiling);
    else if (!witness && !TableFull)
      printf("  complete within the bounds: every schedule with at most"
             " %u ticks per process was covered\n", Cfg->k);

    if (Cfg->surface == 2)
      printf("  reachability: |SubSet|=n %s, |SubSet|<n %s\n",
             StratSubFull ? "reached" : "not reached",
             StratSubShort ? "reached" : "not reached");
    else
      printf("  reachability: every honest process accepted %s\n",
             StratAllAccepted ? "reached" : "not reached");

    /* The whole-config assertions hold only for the bounds they were
     * measured under: a -k / -J / -c / -D override shrinks or grows
     * the search, and the banner's own rule is that a reachability
     * assertion at a config whose baseline does not reach the class
     * is a false red on a correct library.  Under an override the
     * run reports and asserts nothing. */
    if (Failed)
      exitCode = 1;
    else if (argK || argJ || argCeilStates || argCeilDepth)
      printf("  assertions: none -- the bounds are overridden\n");
    else if (!witness && !strategy) {
      if (Cfg->expectQuiescent && !StratQuiescent) {
        printf("  FAILURE: no schedule reached a QUIESCENT terminal\n");
        exitCode = 1;
      }
      if (Cfg->expectNoExhausted && TotExhausted) {
        printf("  FAILURE: the EXHAUSTED class is non-empty at t=0\n");
        exitCode = 1;
      }
      if (Cfg->expectSubsetFull && !StratSubFull) {
        printf("  FAILURE: no schedule reached |SubSet| = n\n");
        exitCode = 1;
      }
      if (Cfg->expectSubsetShort && !StratSubShort) {
        printf("  FAILURE: no schedule reached |SubSet| < n (%s"
               " exclusion)\n", Adv == 0xFF ? "honest" : "the adversary's");
        exitCode = 1;
      }
      /* The 2-coded witnesses are per-strategy: every strategy of the
       * config reaches the outcome, which is what a baseline measured
       * at every strategy licenses -- Lemma 4 under b2 (every honest
       * process accepts whatever the adversary echoes or readies) and
       * BKR94 Lemma 2 Part B under b3/b4 (completion). */
      if (Cfg->expectAllAccepted
       && StratAllAccepted < (Cfg->expectAllAccepted == 2 ? StratCnt : 1)) {
        printf("  FAILURE: %s reached every honest process accepted\n",
               Cfg->expectAllAccepted == 2 ? "not every strategy"
                                          : "no schedule");
        exitCode = 1;
      }
      if (Cfg->expectComplete
       && StratComplete < (Cfg->expectComplete == 2 ? StratCnt : 1)) {
        printf("  FAILURE: %s reached completion\n",
               Cfg->expectComplete == 2 ? "not every strategy"
                                       : "no schedule");
        exitCode = 1;
      }
      if (!Cfg->expStates)
        printf("  frozen counts: NOT SET -- measurement only, nothing"
               " asserted\n");
      else if (measure)
        printf("  frozen counts: not asserted (-m)\n");
      else if (TotStates != Cfg->expStates || TotEdges != Cfg->expEdges
            || TotQuiescent != Cfg->expQuiescent
            || TotExhausted != Cfg->expExhausted
            || TotAllowance != Cfg->expAllowance
            || TotCeiling != Cfg->expCeiling) {
        printf("  FAILURE: frozen counts differ.\n"
               "    expected states=%lu edges=%lu QUIESCENT=%lu"
               " EXHAUSTED=%lu ALLOWANCE=%lu ceilingCuts=%lu\n",
               Cfg->expStates, Cfg->expEdges, Cfg->expQuiescent,
               Cfg->expExhausted, Cfg->expAllowance, Cfg->expCeiling);
        printf("    A count mismatch is SENSITIVITY to a behavioral"
               " change, not by itself a detected defect.\n");
        exitCode = 1;
      } else
        printf("  frozen counts match\n");
    }

    free(Hash);
    free(HashHead);
    free(FrAllow);
    free(FrNext);
    free(PathKind);
    free(PathArg);
    free(KeyStk);
    free(NumStk);
    free(BytStk);
    free(WitKind);
    free(WitArg);
    Hash = 0;
    HashHead = 0;
    FrAllow = 0;
    FrNext = 0;
    PathKind = 0;
    PathArg = 0;
    KeyStk = 0;
    NumStk = 0;
    BytStk = 0;
    WitKind = 0;
    WitArg = 0;
    for (i = 0; i < N; ++i) {
      free(Img[i]);
      Img[i] = 0;
    }
  }

  if (!ran) {
    fprintf(stderr, "test_schedules: no such config: %s\n", want);
    return (2);
  }

  printf("\n=================================\n");
  printf("test_schedules: %s\n", exitCode ? "FAILED" : "PASSED");
  return (exitCode);

 usage:
  fprintf(stderr,
    "usage: test_schedules [-m] [-k ticks] [-J contents] [-c states]"
    " [-D depth] [-b hashbits] [-s strategy] [-w witness] config\n"
    "  config      1 | 2 | 3a | 3b | 4 | b1 | b2 | b3 | b4 | smoke"
    " | strategies | all\n"
    "  -m          report the frozen counts, do not assert them\n"
    "  -k ticks    tick allowance per process override\n"
    "  -J contents contents per strategy override (adversary configs)\n"
    "  -c states   state ceiling override\n");
  fprintf(stderr,
    "  -D depth    depth ceiling override\n"
    "  -b hashbits visited table size, 1 << hashbits entries\n"
    "  -s strategy run one hand-seeded strategy, comma-separated"
    " content keys in the form a failure prints\n"
    "  -w witness  re-derive one event sequence from the root\n");
  return (2);
}
