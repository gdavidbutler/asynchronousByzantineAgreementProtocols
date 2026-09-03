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
 * honest adversarial scheduling.  It does NOT touch the
 * Byzantine-safety arguments for the READY retire gates (README
 * Implementation Note 16) -- those are other instruments' work.
 * Deliberate non-goals: message loss, Byzantine behavior, patience
 * above zero, and coin branching.  The coin here is the
 * examples' deterministic phase%2, so every process gets the same
 * value in Bracha Fig 4 case (iii) and exhaustion requires the
 * schedule to split case (ii)/(iii) every phase.
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
 * instrument runs, the clock is inert either way: the elapsed
 * predicate does not read the counter's history (see THE ELIMINATED
 * COUNTERS below).
 *
 * THE TWO SURFACES ARE ASYMMETRIC, deliberately, because the loops
 * are:
 *
 *   - example/bracha87Fig1.c:625 skips a quiescent process ENTIRELY
 *     ("if (!fig1[i] || quiescent[i]) continue"), so surface 1 gates
 *     the WHOLE tick on rotation membership.  example/bkr94acs.c
 *     wraps only the retry block in "if (!quiescent[i])" (:769-779)
 *     and runs the turns (:798-816) and the fanout (:825-843) for
 *     every process every sweep, which is exactly why it needs the
 *     re-entry-on-turn-acts logic at :808-814 and :835-838.  So
 *     surface 2 gates only the retry sub-step.
 *   - example/bracha87Fig1.c:629 re-initializes the cursor inside the
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
 *               BKR94ACS.txt:112-113 -- "Each player enters his inputs
 *               asynchronously as evidence accumulates" -- and the
 *               example's -d flag is exactly a deferred submission.
 *
 * The content key carries every field ingress reads.  Surface 2:
 * class, process, round, initiator, type, the ACCEPTED bit, the
 * RECEIVED bit, from, to, and the BA binary value with its D_FLAG.
 * The RECEIVED bit is computed PER RECIPIENT at expansion
 * (bracha87.h:1000-1006), not per act, and is part of the key.  The
 * A-Cast value bytes are a function of the A-Cast's process here --
 * honest processes, one value each, no equivocation -- so `process`
 * carries them, and the framing region asserts that identity rather
 * than assuming it.
 *
 * The pool holds COPIES of act values (here, the one-byte values
 * indexed by process).  struct bkr94acsAct.value is a borrowed
 * pointer that the next mutating library call invalidates
 * (bkr94acs.h:210-215); do not "optimize" the copy away.
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
 * (bracha87.c:1368-1370, dereferenced as a Fig 4 at bracha87.c:1193),
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
 * Every Init memsets its whole Sz extent (bracha87.c:130, :734, :860,
 * :1357; bkr94acs.c:297), so padding is deterministic.
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
 *   elapsed = (turnSweeps >= patience) || (BaDecision != 0xFF);
 *
 * At patience 0 the compare is >= 0, constant-true for an unsigned
 * count: the elapsed signal is 1 on every attempt no matter what the
 * counter holds, so neither the update order nor the duty can route
 * through it.  Turn firing then depends on duty alone -- MET fires
 * free, TOLERANCE fires on the constant-true signal -- and the
 * fanout's own >= 0 gate likewise always passes into
 * bkr94acsFanout's internal duty guard (bkr94acs.h:686-690).  At
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
 * EVERY CONFIG HERE IS CEILING-BOUND, and that is a measurement, not a
 * preference.  The smallest configuration here -- surface 1, n=2,
 * t=0, two Fig 1 instances and ten root contents -- was run to
 * 100,663,296 distinct states at K=3 without closing, which is where
 * a 1.5 GB visited table fills.  The blow-up is the pool: each
 * tick pushes fresh copies of contents whose count had dropped, and a
 * duplicate unmarked READY re-arms its sender after an egress consumed
 * the previous arm (bracha87.c:624-625), so copies are semantically
 * live and cannot be coalesced.  What that costs is the ONE property
 * a completed search would have had: the counts below are a
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
 * example/bracha87Fig1.c:612-616 and example/bkr94acs.c:708-712 work
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
 *     ACTUAL process count.  bkr94acs.h:326-328 writes the fanout
 *     bound as "N = n + 1" because n is the ENCODED byte; asserting
 *     against the encoded n would be one too few and would fire on a
 *     full fanout.
 *   - the terminal acts emerge ONLY from a turn: BA_DECIDED,
 *     BA_EXHAUSTED and COMPLETE from bkr94acsBaInput would be a
 *     contract violation (bkr94acs.h:381-390).
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
 *     (bkr94acs.h:847-851) -- so the sound schedule-independent form
 *     is the WITHIN-ROUND one, asserted only across transitions in
 *     which that BA turned no round.  Both duties are functions of
 *     the image, so the pre/post pair across one transition is the
 *     whole monotonicity chain and nothing is stored in the state.
 *   - acFrom \ {self} subset of rdFrom after every ingress.  The
 *     naive subset is FALSE: the self-accept is recorded at ACCEPT
 *     with the local index and no rdFrom guard
 *     (bracha87.h:388-392, bracha87.c:585-600), while the process's
 *     own (ready, v) can still be pending in the pool.
 *   - the RECEIVED mask is present only on a READY act
 *     (bracha87.h:1000-1006, bkr94acs.h:229-238).
 *   - HARNESS SELF-CHECKS, labeled as such because they prove framer
 *     discipline and not library behavior: a marked READY is never
 *     routed to *Resend, and the wire RECEIVED bit is set for
 *     recipient p only where the RECEIVED mask says so.  The second
 *     would be an identity at the library layer anyway --
 *     bracha87Fig1Received RETURNS acFrom (bracha87.c:679-685).
 *   - single input per BA: bkr94acsBaEntered latched once entered.
 *   - Bracha Lemma 1 (surface 1), in its observable form: every
 *     instance whose echoed value exists carries the initiator's
 *     value, so no two READY senders can carry different ones.
 *   - Bracha Lemma 2 (surface 1): any two accepts of one broadcast
 *     agree.
 *
 * At every QUIESCENT terminal:
 *
 *   - surface 1: Lemma 4 and the value -- every process accepted the
 *     initiator's value -- AND the ending claim itself, that every
 *     process's READY suppress mask covers all n.  Lemma 4 alone does
 *     not separate a machine that retired READY on the forbidden LOCAL
 *     accept (Notes 10/16) from one that closed the remote
 *     all-accepted gate: both quiesce and both accept the one value,
 *     because there is only one value under honest-no-loss.  The mask
 *     is what says whose evidence closed it.  Lemma 4 is sound AT A
 *     QUIESCENCE LEAF and only there:
 *     quiescence is the 0 return of a full pass, and an
 *     ECHOED-but-not-ACCEPTED instance always outputs ECHO_ALL
 *     (bracha87.c:511-512 gates ECHO retirement on ACCEPTED alone)
 *     while RDSENT implies ECHOED, so quiescence structurally implies
 *     ACCEPTED.  Pool empty.
 *   - surface 2: every process complete; |SubSet| >= n-t and never
 *     required to be n, since honest exclusion is legal
 *     (BKR94ACS.txt:201-206); SubSet byte-identical across processes;
 *     every SubSet member's A-Cast value present and byte-equal
 *     everywhere; no 0xFE anywhere -- sound ONLY under the class
 *     precedence above.  Pool empty.  AND the ending claim per owned
 *     Fig 1 instance, read through bkr94acsAcastFig1 / bkr94acsBaFig1:
 *     every SENT instance THE RETRY STILL SERVES has a RECEIVED mask
 *     covering all n.  The scope matters: the decided-0 retry gate
 *     (bkr94acs.c:744-777) skips an excluded process's A-Cast walk,
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
 *   test_schedules [-m] [-c states] [-D depth] [-b hashbits]
 *                  [-w witness] config
 *
 *   config      1 | 2 | 3a | 3b | 4 | smoke | all
 *   -m          report the frozen counts, do not assert them
 *   -c states   state ceiling override
 *   -D depth    depth ceiling override
 *   -b hashbits visited-table size, 1 << hashbits entries
 *   -w witness  re-derive one event sequence from the root with a
 *               trace, in the comma-separated form a failure prints
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

/*
 * Content-key field layout.  One unsigned long per distinct wire
 * content; 29 bits used.
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
  unsigned char smoke;
};

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
    60000UL, 188184UL, 30UL, 0UL, 770UL, 1UL,
    18, 3, 1, 2, 0, 0, 0xFF, 1, 1, 0, 0, 0, 1 },

  { "s2", "smoke: surface 2, n=2 t=0 maxPhases=1, K=40",
    60000UL, 4000UL,
    60000UL, 112533UL, 19822UL, 0UL, 0UL, 1UL,
    18, 40, 2, 2, 0, 1, 0xFF, 1, 1, 1, 0, 0, 1 },

  /* 1 -- THE ANCHOR.  Surface 1, n=2 t=0.  K=3 is the measured floor
   * and not a guess: a process must tick once to announce its accept
   * (the ACCEPTED annotation rides only on retry READYs -- an
   * Input-produced READY passes a literal 0,
   * example/bracha87Fig1.c:599-603), once for the marked re-send the
   * announcement's unmarked arrival arms, and once more to read the
   * retired 0 return that IS quiescence.  It is the anchor because it
   * is the smallest shape that reaches quiescence at all, not because
   * it closes: see the ceiling paragraph in the banner. */
  { "1", "surface 1, n=2 t=0, K=3 -- the anchor",
    4000000UL, 4000UL,
    4000000UL, 18835154UL, 43UL, 0UL, 2585UL, 1UL,
    24, 3, 1, 2, 0, 0, 0xFF, 1, 1, 0, 0, 0, 0 },

  /* 2 -- surface 1 at real thresholds.  n=4 t=1: echo threshold
   * (n+t)/2+1 = 3, ready t+1 = 2, accept 2t+1 = 3. */
  { "2", "surface 1, n=4 t=1, K=3 -- real thresholds",
    4000000UL, 4000UL,
    4000000UL, 11300072UL, 473UL, 0UL, 129349UL, 1UL,
    24, 3, 1, 4, 1, 0, 0xFF, 1, 1, 0, 0, 0, 0 },

  /* 3a -- THE TARGET.  Surface 2, n=4 t=1, maxPhases=1, every A-Cast
   * submitted at the root.  The |SubSet| = n witness is on the first
   * dive: a drain that runs to empty accepts every A-Cast at every
   * process, step 1 enters 1 into all N BAs before the first tick, so
   * the unentered set is empty and FanoutDuty is MET forever. */
  { "3a", "surface 2, n=4 t=1 maxPhases=1, all A-Casts at the root",
    400000UL, 6000UL,
    400000UL, 1399492UL, 112916UL, 0UL, 0UL, 1UL,
    21, 200, 2, 4, 1, 1, 0xFF, 0, 1, 0, 1, 0, 0 },

  /* 3b -- the same, with process 3's A-Cast behind an acast() event.
   * With it unfired, processes 0/1/2's A-Casts accept everywhere, all
   * four processes enter 1 into BA_0/1/2, those three decide 1
   * (n-t = 3), and FanoutDuty leaves HELD with BA_3 unentered -- so
   * the honest-exclusion witness is a whole SUBTREE rather than a
   * needle, and the branch order puts the explorer inside it first. */
  { "3b", "surface 2, n=4 t=1 maxPhases=1, process 3's A-Cast deferred",
    400000UL, 6000UL,
    400000UL, 2478544UL, 3842UL, 0UL, 0UL, 1UL,
    21, 200, 2, 4, 1, 1, 3, 0, 1, 0, 0, 1, 0 },

  /* 4 -- the degenerate control.  At t=0, n-t = n, so TurnDuty's
   * TOLERANCE band (">= n-t and < n") is empty by arithmetic and
   * FanoutDuty can never classify TOLERANCE either.  None of the
   * sweep-paced machinery is alive; what it does check is the ending
   * claims and the EXHAUSTED-empty structural fact. */
  { "4", "surface 2, n=2 t=0 maxPhases=1 -- degenerate control",
    1000000UL, 4000UL,
    1000000UL, 2234911UL, 188183UL, 0UL, 0UL, 1UL,
    22, 40, 2, 2, 0, 1, 0xFF, 1, 1, 1, 0, 0, 0 }
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
  unsigned long koff;      /* this frame's base in KeyStk / NumStk */
  unsigned long boff;      /* this frame's base in BytStk          */
  unsigned long nLive;     /* pool live count at the snapshot      */
  unsigned long key;
  unsigned long i;
  unsigned long j;
  unsigned long evArg;
  unsigned long ev;
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
  for (p = 0; p < N; ++p) {
    if (!Quiescent[p])
      allQuiescent = 0;
    if (Cfg->surface != 2)
      continue;
    Acsp = (struct bkr94acs *)Img[p];
    if (!Acsp->complete)
      allComplete = 0;
    for (q = 0; q < N; ++q)
      if (bkr94acsBaDecision(Acsp, (unsigned char)q) == 0xFE)
        anyExhausted = 1;
  }

  /* The SubSet reachability witnesses read COMPLETION, not
   * quiescence: completion lands early and the retirement tail after
   * it is long. */
  if (allComplete) {
    haveFirst = 0;
    firstCnt = 0;
    for (p = 0; p < N; ++p) {
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
    if (firstCnt == N)
      SawSubsetFull = 1;
    else
      SawSubsetShort = 1;
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

        if (!Accepted[p]) {
          FailMsg = "Lemma 4: a quiescent process never accepted";
          goto fail;
        }
        if (AcceptVal[p] != Val1) {
          FailMsg = "Lemma 4: the accepted value is not the initiator's";
          goto fail;
        }
        /* THE ENDING CLAIM, and the reason it is checked rather than
         * inferred from the 0 return: example/bracha87Fig1.c's header
         * says quiescence is reached because "each instance's suppress
         * mask reaches all n, READY retires with it, and a full
         * bracha87Fig1RetryStep pass owes nothing".  The 0 return
         * alone is the weaker fact -- a machine that retired READY at
         * LOCAL accept (Notes 10/16, the forbidden gate) would
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
         * surface-1 arm turns on (Notes 10/16): at quiescence a
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
             * (bkr94acs.c:744-777: BA decided 0 -> the retry skips
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

          cv = bkr94acsAcastValue((struct bkr94acs *)Img[p], FirstSub[i]);
          if (!cv) {
            FailMsg = "quiescent terminal: a SubSet member's value is absent";
            goto fail;
          }
          if (*cv != Aval[FirstSub[i]]) {
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

  for (i = 0; i < nLive; ++i) {
    KeyStk[koff + i] = PoolKey[i];
    NumStk[koff + i] = PoolCnt[i];
  }
  for (p = 0; p < N; ++p) {
    NumStk[koff + nLive + 2 * p] = Cursor[p].pos;
    NumStk[koff + nLive + 2 * p + 1] = Cursor[p].sweepActs;
    memcpy(BytStk + boff + p * ImgSz, Img[p], ImgSz);
  }
  memcpy(BytStk + boff + N * ImgSz + 0 * MAX_N, Quiescent, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 1 * MAX_N, Accepted, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 2 * MAX_N, AcceptVal, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 3 * MAX_N, Pending, MAX_N);
  memcpy(BytStk + boff + N * ImgSz + 4 * MAX_N, Allow, MAX_N);

  KeyStkTop = koff + nLive;
  NumStkTop = koff + nLive + 2 * MAX_N;
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
               " from=%lu to=%lu acc=%lu ans=%lu bav=%lu\n",
               KEY_FLD(evArg, KEY_CLS_SH, 1),
               KEY_FLD(evArg, KEY_PROC_SH, 4),
               KEY_FLD(evArg, KEY_ROUND_SH, 6),
               KEY_FLD(evArg, KEY_INIT_SH, 4),
               KEY_FLD(evArg, KEY_TYPE_SH, 2),
               KEY_FLD(evArg, KEY_FROM_SH, 4),
               KEY_FLD(evArg, KEY_TO_SH, 4),
               KEY_FLD(evArg, KEY_ACC_SH, 1),
               KEY_FLD(evArg, KEY_RCV_SH, 1),
               KEY_FLD(evArg, KEY_BAV_SH, 2));
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
       * Note 17): the bare Fig 1 entry is not told its designated
       * initiator, so a bare-layer caller must drop a non-initiator
       * INITIAL before it reaches the echo cascade. */
      if (KEY_FLD(key, KEY_TYPE_SH, 2) == BRACHA87_INITIAL
       && KEY_FLD(key, KEY_FROM_SH, 4) != Initiator)
        goto applied;

      NActs = bracha87Fig1Input(F1p,
                                (unsigned char)KEY_FLD(key, KEY_TYPE_SH, 2),
                                (unsigned char)KEY_FLD(key, KEY_FROM_SH, 4),
                                &Val1, Out1);
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
        if (*cv != Val1) {
          FailMsg = "Lemma 1: an echoed value is not the initiator's";
          goto fail;
        }
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
                &Aval[KEY_FLD(key, KEY_PROC_SH, 4)], Acts);
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
     * the fanout are not -- example/bkr94acs.c:769-843. */
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
      unsigned char duty;

      duty = bkr94acsTurnDuty(Acsp, (unsigned char)q);
      /* The zero-patience elapsed signal, with turnSweeps eliminated:
       * the example's >= 0 compare is constant-true, so at TOLERANCE
       * the signal is 1 on every attempt -- the predicate below is
       * firing-identical -- and patience is scoped to undecided BAs. */
      PreDec = bkr94acsBaDecision(Acsp, (unsigned char)q);
      NActs = bkr94acsTurn(Acsp, (unsigned char)q,
                           (unsigned char)((duty == BKR94ACS_DUTY_TOLERANCE)
                                           || (PreDec != 0xFF)),
                           Acts);
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
     * what the eliminated fanoutTicks reduced to and is doubly
     * redundant with bkr94acsFanout's own guard. */
    if (bkr94acsFanoutDuty(Acsp) == BKR94ACS_DUTY_TOLERANCE) {
      NActs = bkr94acsFanout(Acsp, 1, Acts);
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
     * nobody, exactly as example/bkr94acs.c:556-560 pushes it. */
    for (i = 0; i < NActs; ++i)
      for (j = 0; j < N; ++j)
        poolPush(((unsigned long)BRACHA87_INITIAL << KEY_TYPE_SH)
                 | ((unsigned long)(Acts[i].accepted ? 1 : 0) << KEY_ACC_SH)
                 | ((unsigned long)Acts[i].process << KEY_PROC_SH)
                 | ((unsigned long)Self << KEY_FROM_SH)
                 | (j << KEY_TO_SH));
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
      if (Skip && BRACHA87_SKIP_TST(Skip, j))
        continue;
      poolPush(((unsigned long)PushType << KEY_TYPE_SH)
               | ((unsigned long)(PushAcc ? 1 : 0) << KEY_ACC_SH)
               | ((unsigned long)((Received && BRACHA87_SKIP_TST(Received, j))
                                  ? 1 : 0) << KEY_RCV_SH)
               | ((unsigned long)Self << KEY_FROM_SH)
               | (j << KEY_TO_SH));
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
        /* The key carries the A-Cast value through `process`; this is
         * where that identity is checked rather than assumed. */
        if (*Acts[i].value != Aval[Acts[i].process]) {
          FailMsg = "an A-Cast act carries a value no process A-Cast";
          goto fail;
        }
        base = 0;
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
          if (Acts[i].skip && BRACHA87_SKIP_TST(Acts[i].skip, j))
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
        /* Lemma 2: any two accepts of one broadcast agree. */
        for (q = p + 1; q < N; ++q)
          if (Accepted[p] && Accepted[q] && AcceptVal[p] != AcceptVal[q]) {
            FailMsg = "Lemma 2: two accepts of one broadcast disagree";
            goto fail;
          }
      }
    } else
      for (p = 0; p < N; ++p) {
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

    explore(depth + 1);

    /*--------------------------------------------------------------*/
    /*  Restore -- memcpy back into the SAME allocations.            */
    /*--------------------------------------------------------------*/

    PoolLive = (unsigned int)nLive;
    PoolTot = 0;
    for (i = 0; i < nLive; ++i) {
      PoolKey[i] = KeyStk[koff + i];
      PoolCnt[i] = NumStk[koff + i];
      PoolTot += PoolCnt[i];
    }
    for (p = 0; p < N; ++p) {
      Cursor[p].pos = NumStk[koff + nLive + 2 * p];
      Cursor[p].sweepActs = NumStk[koff + nLive + 2 * p + 1];
      memcpy(Img[p], BytStk + boff + p * ImgSz, ImgSz);
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
  NumStkTop = koff;
  BytStkTop = boff;
  return;

 fail:
  Failed = 1;
  printf("\nFAILURE: %s\n", FailMsg);
  printf("witness: ");
  for (i = 0; i <= depth && PathKind[i]; ++i) {
    if (i)
      printf(",");
    if (PathKind[i] == EV_DELIVER)
      printf("d%lx", PathArg[i]);
    else
      printf("%c%lu", (PathKind[i] == EV_TICK) ? 't' : 'a', PathArg[i]);
  }
  printf("\nre-run: ./test_schedules -w <the sequence above> %s\n",
         Cfg->name);
}

/*--------------------------------------------------------------------------*/
/*  Main -- set a config up, run it, report, assert.                        */
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
  const char *witness;
  const char *want;
  unsigned int c;
  unsigned int i;
  unsigned int ran;

  measure = 0;
  exitCode = 0;
  argCeilStates = 0;
  argCeilDepth = 0;
  argHashBits = 0;
  argK = 0;
  witness = 0;
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
    } else if (argv[arg][1] == 'D' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argCeilDepth = strtoul(argv[arg++], 0, 10);
    } else if (argv[arg][1] == 'b' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      argHashBits = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'w' && !argv[arg][2]) {
      if (++arg >= argc) goto usage;
      witness = argv[arg++];
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
     && !(!strcmp(want, "smoke") && Cfg->smoke))
      continue;
    ++ran;

    N = Cfg->n;
    T = Cfg->t;
    Initiator = 0;
    if (argK)
      Cfg->k = argK;
    if (Cfg->k > 255) {
      fprintf(stderr, "test_schedules: K above 255 does not fit the"
              " allowance vector\n");
      return (2);
    }
    if (argCeilStates)
      Cfg->ceilStates = argCeilStates;
    if (argCeilDepth)
      Cfg->ceilDepth = argCeilDepth;

    /*----------------------------------------------------------------*/
    /*  Fixed allocations, held for the whole run: that is what makes  */
    /*  the byte fingerprint sound (see the snapshot audit above).     */
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
      if (Cfg->surface == 1)
        bracha87Fig1Init((struct bracha87Fig1 *)Img[i],
                         (unsigned char)(N - 1), (unsigned char)T, 0);
      else
        bkr94acsInit((struct bkr94acs *)Img[i], (unsigned char)(N - 1),
                     (unsigned char)T, 0, Cfg->maxPhases,
                     (unsigned char)i, demoCoin, 0);
      bracha87RetryInit(&Cursor[i]);
      Allow[i] = (unsigned char)Cfg->k;
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
    FirstQuiescent = 0;
    HashCnt = 0;
    FrCnt = 1;

    bits = argHashBits ? argHashBits : Cfg->hashBits;
    HashSz = 1UL << bits;
    FrCap = Cfg->keyAllow ? 2 : (unsigned int)(HashSz / 2);
    PathCap = Cfg->ceilDepth + 8;
    KeyStkSz = NumStkSz = 4096;
    BytStkSz = 1UL << 20;
    KeyStkTop = NumStkTop = BytStkTop = 0;

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
    /*  The root state.                                                */
    /*----------------------------------------------------------------*/

    if (Cfg->surface == 1) {
      bracha87Fig1Initiator((struct bracha87Fig1 *)Img[Initiator], &Val1);
      for (i = 0; i < N; ++i)
        poolPush(((unsigned long)BRACHA87_INITIAL << KEY_TYPE_SH)
                 | ((unsigned long)Initiator << KEY_FROM_SH)
                 | ((unsigned long)i << KEY_TO_SH));
    } else
      for (i = 0; i < N; ++i) {
        struct bkr94acsAct act;
        unsigned int j;

        if (Cfg->defer == i) {
          /* A deferred submission is an EVENT, not a root fact --
           * BKR94ACS.txt:112-113, and the example's -d is exactly this. */
          Pending[i] = 1;
          continue;
        }
        if (bkr94acsAcast((struct bkr94acs *)Img[i], &Aval[i], &act) != 1)
          continue;
        for (j = 0; j < N; ++j)
          poolPush(((unsigned long)BRACHA87_INITIAL << KEY_TYPE_SH)
                   | ((unsigned long)(act.accepted ? 1 : 0) << KEY_ACC_SH)
                   | ((unsigned long)act.process << KEY_PROC_SH)
                   | ((unsigned long)i << KEY_FROM_SH)
                   | ((unsigned long)j << KEY_TO_SH));
      }

    /*----------------------------------------------------------------*/
    /*  Announce the bounds, then run.                                 */
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

    /*----------------------------------------------------------------*/
    /*  Report, then the whole-config assertions.                      */
    /*----------------------------------------------------------------*/

    printf("  states=%lu edges=%lu maxDepth=%lu\n",
           States, Edges, MaxDepthSeen);
    printf("  terminals: QUIESCENT=%lu EXHAUSTED=%lu"
           " ALLOWANCE-EXHAUSTED=%lu\n",
           TermQuiescent, TermExhausted, TermAllowance);
    if (TableFull) {
      printf("  ** VISITED TABLE FULL at %lu entries -- the search is"
             " INCOMPLETE; raise -b **\n", HashCnt);
      exitCode = 1;
    }
    if (SawQuiescent)
      printf("  first QUIESCENT terminal at state %lu -- the depth at"
             " which the reachability detector separates a stall from a"
             " ceiling\n", FirstQuiescent);
    if (CeilingHit)
      printf("  ** CEILING HIT (%lu cuts) -- exhaustive-within-K is VOID"
             " for this config; the counts are a deterministic prefix"
             " of the search under the branch order above, not the whole"
             " of it **\n", CeilingCuts);
    else if (!witness && !TableFull)
      printf("  complete within the bounds: every schedule with at most"
             " %u ticks per process was covered\n", Cfg->k);

    if (Failed)
      exitCode = 1;
    else if (!witness) {
      if (Cfg->expectQuiescent && !SawQuiescent) {
        printf("  FAILURE: no schedule reached a QUIESCENT terminal\n");
        exitCode = 1;
      }
      if (Cfg->expectNoExhausted && TermExhausted) {
        printf("  FAILURE: the EXHAUSTED class is non-empty at t=0\n");
        exitCode = 1;
      }
      if (Cfg->expectSubsetFull && !SawSubsetFull) {
        printf("  FAILURE: no schedule reached |SubSet| = n\n");
        exitCode = 1;
      }
      if (Cfg->expectSubsetShort && !SawSubsetShort) {
        printf("  FAILURE: no schedule reached |SubSet| < n"
               " (honest exclusion)\n");
        exitCode = 1;
      }
      if (Cfg->surface == 2)
        printf("  reachability: |SubSet|=n %s, |SubSet|<n %s\n",
               SawSubsetFull ? "reached" : "not reached",
               SawSubsetShort ? "reached" : "not reached");

      if (!Cfg->expStates)
        printf("  frozen counts: NOT SET -- measurement only, nothing"
               " asserted\n");
      else if (measure)
        printf("  frozen counts: not asserted (-m)\n");
      else if (States != Cfg->expStates || Edges != Cfg->expEdges
            || TermQuiescent != Cfg->expQuiescent
            || TermExhausted != Cfg->expExhausted
            || TermAllowance != Cfg->expAllowance
            || CeilingCuts != Cfg->expCeiling) {
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
    "usage: test_schedules [-m] [-c states] [-D depth] [-b hashbits]"
    " [-w witness] config\n"
    "  config      1 | 2 | 3a | 3b | 4 | smoke | all\n"
    "  -m          report the frozen counts, do not assert them\n"
    "  -k ticks    tick allowance per process override\n"
    "  -c states   state ceiling override\n"
    "  -D depth    depth ceiling override\n"
    "  -b hashbits visited table size, 1 << hashbits entries\n"
    "  -w witness  re-derive one event sequence from the root\n");
  return (2);
}
