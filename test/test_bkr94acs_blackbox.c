/*
 * test_bkr94acs_blackbox.c
 *
 * Black-box test for the public C API in bkr94acs.h.
 *
 * Tests are derived ONLY from the documented contract in bkr94acs.h
 * and BKR94ACS.txt (the line-by-line extract of Ben-Or/Kelmer/Rabin
 * 1994 Section 4 Figure 3 that the implementation is aligned to).
 * No part of this file inspects bkr94acs.c, peeks at private fields
 * via the data[] tail, or otherwise reaches past the public surface.
 *
 * Sections (see in-file "Section X -- ..." markers in main() for
 * the authoritative list):
 *
 *   A. API edges -- Sz/Init, A-Cast round-trip, defensive nulls.
 *   B. Lemma 2 Parts A/B/C/D + paper-direct invariants -- honest
 *      convergence at n=4/n=7, identical acasts, multi-byte
 *      values, step-2 BA-decision trigger, single-input-per-BA,
 *      single COMPLETE / BA_DECIDED, honest-exclusion allowance.
 *   C. BPR / Retry -- idle-on-fresh, post-A-Cast self-INITIAL,
 *      MAX_ACTS bound, SentFig1Count monotone, barren-sweep
 *      signal, drop convergence, silent-Byzantine canary, Input
 *      dedup (retried wire returns 0 acts).
 *   D. EXHAUSTED -- single output (read off the zero-patience turn
 *      drain, the only place the act can appear) + 0xFE sentinel +
 *      permanent !complete + HELD forever after; Retry continues
 *      post-EXHAUSTED.
 *   E. Byzantine -- equivocating A-Caster (Bracha Lemma 2 inheritance).
 *   F. Step 2 pacing -- the same delayed-A-Cast schedule under two
 *      patience values: the fanout at enabling excludes the delayed
 *      honest process (F1), patience includes it (F2); a dead slot
 *      holds TOLERANCE forever
 *      and finite patience completes past it (F3).  Duty
 *      trichotomy monotone (MET absorbing, TOLERANCE never back to
 *      HELD) at every fDrive sweep.
 *   G. Round-turn pacing -- deliveries bank and decide nothing (G1),
 *      TOLERANCE waits until the caller calls, then fires (G2), MET
 *      fires free (G3), a drained instance is turn-quiescent (G4).
 *   H. Quiescence is REACHABLE at the ACS surface (H1), the Resend
 *      ingress entries' contracts (H2), and the two caller obligations
 *      forfeited -- no re-entry after a placed loss, RECEIVED never
 *      carried (H3).
 *   I. Partition heal -- READY re-sends alone carry a returner that
 *      holds zero evidence of an instance (I1); a 2/2 cut leaves
 *      neither side n-t and heals (I2).
 *   J. Asymmetric flow -- the receive-only half completes and agrees
 *      (J1); the send-only half feeds everyone and sees pure
 *      barrenness (J2).
 *   K. Byzantine trickle -- a bounded stretch of the barren gate, the
 *      exhaustion of the trickler's supply, value-blind dedup, sweep
 *      inflation, and the derived ceiling (K1).
 *   L. Staggered start -- the pre-accept lane bootstraps on live
 *      INITIAL re-sends (L1); the post-fanout lane still completes
 *      and agrees (L2).
 *   M. After COMPLETE -- a never-announcing leaver holds every
 *      survivor's READY gate open, and the barren backstop is what
 *      ends the drive (M1); an announced-then-silent leaver lets
 *      every survivor quiesce (M2).
 *   N. The sustained-rate skew lane -- the fairness non-invariant and
 *      its cost scaling (N1); duty verdicts are pure functions of
 *      state (N2).
 *   O. A BA's decision versus this process's input to it -- a BA
 *      decides 1 for an A-Cast this process never accepted (O1), two
 *      fanout enter-0 exclusions at n=7 t=2 (O2), and a BA this
 *      process entered with 0 deciding 1 (O3).
 *   P. Annotation forgery -- the two READY annotations are the one
 *      wire field no paper backs.  A forged accept announcement is
 *      contained to its own sender and strands no correct laggard
 *      (P1); a forged unmarked READY reaches the forger alone, and the
 *      backlog it accumulates is priced by stopping the forger and
 *      draining it, at one and at four times the honest rate (P2); and
 *      the residue it leaves is mask-complete and perpetually re-armed,
 *      ending in the barren gate, with or without the announcement lie
 *      (P3).
 *   Q. The paired payload -- its retire set closes for every correct
 *      process under the order that separates the readied set from
 *      the echoed one, lossless and under drop, and stays open under
 *      a silent process (Q1); the hold is at Input -- one holder is
 *      refused, the ECHO-only hold is accepted, t+1 holders decide 1
 *      with the non-holder's value absent and its READY owed (Q2).
 *   R. A decision in two waves through the composition, each process
 *      on its own local coin -- the bound on post-decide continuation
 *      (bracha87.h, at bracha87Fig4Round): a first wave of two stops
 *      at the (d, v) of the phase after its decision and its next
 *      round is spent (R1); a lone first-wave decider, whose next
 *      rounds the second wave completes, is pinned there (R2).
 *
 * Sections I through N are the README "Abandonment" scenarios
 * mechanized.  Every assertion about the MACHINE is grounded in a
 * header or paper sentence, cited at the section; the README scenario
 * names appear as cross-reference labels only.  The one stated
 * exception is the shared barren-sweep policy machinery below, which
 * is HARNESS application code and cites the bundled application loop
 * as its operational reference.
 *
 * Caller discipline (bkr94acs.h): the arrival path only banks
 * evidence.  BKR94 step 2 (bkr94acsFanout) and the BA round turn
 * (bkr94acsTurn) fire from the caller's sweep, so no driver here
 * pays a patience: the per-input drivers (deliverWire, runWithRetry,
 * and main's inline drive loops) turn and fan out after every input
 * -- a firing at enabling; feedBAAccept turns only where its caller
 * asks and never fans out; the scenario drivers turn once per tick
 * from the sweep.  A section that isolates the fanout or the turn
 * paces that one seam instead.
 *
 * Header encoding convention (CRITICAL):
 *   n parameter is encoded; actual process count = n + 1
 *   vLen parameter is encoded; actual value length = vLen + 1
 *
 * Style: C89, K&R, 2-space indent, single monolithic main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bkr94acs.h"

/* ------------------------------------------------------------------ */
/*  test plumbing                                                     */
/* ------------------------------------------------------------------ */

static int Failures = 0;
static int Checks   = 0;
static const char *CurTest = "<none>";

#define CHECK(cond, msg) do {                                  \
    ++Checks;                                                  \
    if (!(cond)) {                                             \
      ++Failures;                                              \
      fprintf(stderr, "FAIL [%s]: %s  (%s:%d)\n",              \
              CurTest, (msg), __FILE__, __LINE__);             \
    }                                                          \
  } while (0)

#define BANNER(name) do { CurTest = (name); } while (0)

/* Every turn drain in this file is `while ((n = bkr94acsTurn(...)) > 0
 * && turnDrained())`.  A drain ends because the turn advances the
 * round it computed or refuses at HELD; a machine that emitted acts
 * without advancing would spin it.  This counts every turn call that
 * returned acts inside a drain against a ceiling no correct run
 * approaches (a fired turn advances its round, so acts per process are
 * bounded by the round space; the whole suite takes about 15,000) and
 * aborts past it -- announced, like a queue overflow, never a silent
 * hang. */
#define TURN_CALL_CAP (1u << 24)
static unsigned long TurnCalls = 0;

static int
turnDrained(
  void
){
  if (++TurnCalls > TURN_CALL_CAP) {
    fprintf(stderr, "FATAL [%s]: turn drain runaway -- bkr94acsTurn"
            " returned acts %lu times\n", CurTest, TurnCalls);
    abort();
  }
  return (1);
}

#define MAX_PROCESSES  16
#define MAX_VLEN   32
#define QCAP       (1u << 18)

/* ------------------------------------------------------------------ */
/*  Coin -- deterministic alternating.  Adequate for tests;           */
/*  adversarial deployments should pass a local random source.        */
/* ------------------------------------------------------------------ */

static unsigned char
testCoin(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
  return ((unsigned char)(phase & 1));
}

/* ------------------------------------------------------------------ */
/*  Repeatable scheduler RNG                                          */
/* ------------------------------------------------------------------ */

static unsigned long Rng = 0x9e3779b97f4a7c15UL;

static unsigned int
rngNext(
  void
){
  Rng = Rng * 6364136223846793005UL + 1442695040888963407UL;
  return ((unsigned int)(Rng >> 33));
}

static void
rngSeed(
  unsigned long s
){
  Rng = s ? s : 1;
  (void)rngNext();
}

/* ------------------------------------------------------------------ */
/*  Wire queue -- carries both A-Cast-class and BA-class     */
/*  Fig1 messages between processes.  cls discriminates which API the     */
/*  receiver dispatches to.                                           */
/* ------------------------------------------------------------------ */

struct wire {
  unsigned char cls;          /* BKR94ACS_CLS_ACAST | _BA */
  unsigned char process;
  unsigned char round;        /* BA only */
  unsigned char initiator;  /* BA only */
  unsigned char type;         /* BRACHA87_INITIAL/ECHO/READY */
  unsigned char from;         /* wire sender */
  unsigned char to;           /* recipient */
  unsigned char baValue;     /* BA only (binary) */
  unsigned char accepted;     /* READY only: BKR94ACS_ACCEPTED wire bit */
  unsigned char received;     /* READY only: BKR94ACS_RECEIVED wire bit --
                               * decided per recipient from the act's
                               * .received mask; its ABSENCE is the arm */
  unsigned char value[MAX_VLEN]; /* ACAST only (vLen bytes) */
};

/*
 * The two READY annotations as the one byte bkr94acs{Acast,Ba}Input
 * takes for annot.  The wire model above carries them as separate
 * flags; the library reads BKR94ACS_ACCEPTED and BKR94ACS_RECEIVED off
 * whatever byte it is handed, and only when the type is a READY.
 */
static unsigned char
wireAnnot(
  const struct wire *w
){
  return ((w->accepted ? BKR94ACS_ACCEPTED : 0)
        | (w->received ? BKR94ACS_RECEIVED : 0));
}

/*
 * For the unit drivers below, which inject protocol traffic without
 * modeling the annotation exchange.  BKR94ACS_RECEIVED says the sender
 * already holds this process re-send arm, so no arm is taken and the
 * driver exercises the Fig 1 rules alone.
 */
#define ANNOT_NO_ARM BKR94ACS_RECEIVED

static struct wire WireQ[QCAP];
static unsigned int QHead = 0;
static unsigned int QTail = 0;

static void
qReset(
  void
){
  QHead = QTail = 0;
}

static unsigned int
qSize(
  void
){
  return (QTail - QHead);
}

static void
qPush(
  const struct wire *w
){
  if (qSize() >= QCAP) {
    fprintf(stderr, "FATAL [%s]: wire queue overflow\n", CurTest);
    abort();
  }
  WireQ[QTail % QCAP] = *w;
  ++QTail;
}

static int
qPopHead(
  struct wire *out
){
  if (qSize() == 0)
    return (0);
  *out = WireQ[QHead % QCAP];
  ++QHead;
  return (1);
}

/* Uniform-random pop via swap-with-last.  Preserves the heap's
 * QHead..QTail-1 occupancy invariant.  Mixes poorly with qPopHead;
 * a given drive picks one strategy. */
static int
qPopRandom(
  struct wire *out
){
  unsigned int sz, pick, idx, lastIdx;

  sz = qSize();
  if (sz == 0)
    return (0);
  pick = rngNext() % sz;
  idx = (QHead + pick) % QCAP;
  *out = WireQ[idx];
  --QTail;
  lastIdx = QTail % QCAP;
  if (idx != lastIdx)
    WireQ[idx] = WireQ[lastIdx];
  return (1);
}

/* ------------------------------------------------------------------ */
/*  Per-process black-box observations.  Updated as acts are returned    */
/*  from any API call.  Every assertion in section B reads from here  */
/*  or from the public accessors -- never from the bkr94acs struct's  */
/*  data[] tail.                                                      */
/* ------------------------------------------------------------------ */

struct processObs {
  unsigned int completeCount;                /* BKR94ACS_ACT_COMPLETE */
  unsigned int baDecidedCount[MAX_PROCESSES];    /* BKR94ACS_ACT_BA_DECIDED per process */
  unsigned int baDecidedValue[MAX_PROCESSES];    /* last baValue seen on BA_DECIDED */
  unsigned char selfInputValue[MAX_PROCESSES];   /* 0xFF = not yet observed; else recorded baValue
                                                of first round-0 self-INITIAL output */
  unsigned int selfInputDisagree[MAX_PROCESSES]; /* > 0 iff a later self-INITIAL value disagrees */
  unsigned int selfInputAny[MAX_PROCESSES];      /* set iff any self-INITIAL output (1/0) */
  unsigned int exhaustedCount[MAX_PROCESSES];    /* BKR94ACS_ACT_BA_EXHAUSTED per process */
};

static void
obsInit(
  struct processObs *o
){
  unsigned int j;
  memset(o, 0, sizeof (*o));
  for (j = 0; j < MAX_PROCESSES; ++j)
    o->selfInputValue[j] = 0xFF;
}

/* Observe acts output BY process 'self' (regardless of which API
 * call produced them).  Updates obs counters; outputs wire messages
 * to all 'nAct' processes (including 'self' -- loopback through queue
 * per the project's "feed self through the network" rule).
 *
 * dropPercent  0..99 -- per-recipient probability the wire is dropped
 *                       at output rather than queued.  Models a
 *                       lossy network for BPR-retry convergence
 *                       tests.  0 = no drops.
 * silentProcess   -1 = none; otherwise wires destined to this process are
 *                       not queued (the silent process never receives,
 *                       its A-Cast/Retry are never called, so it
 *                       never outputs -- modeling a Byzantine-silent
 *                       crash from the rest of the cluster's POV). */
static void
observeAndOutput(
  struct processObs *obs
 ,unsigned char self
 ,unsigned int nAct
 ,const struct bkr94acsAct *acts
 ,unsigned int n
 ,unsigned int vBytes
 ,unsigned int dropPercent
 ,int silentProcess
){
  unsigned int i, j;
  struct wire w;

  for (i = 0; i < n; ++i) {
    switch (acts[i].act) {
    case BKR94ACS_ACT_ACAST_SEND:
      for (j = 0; j < nAct; ++j) {
        if (silentProcess >= 0 && (int)j == silentProcess)
          continue;
        /* BPR per-process suppress mask: skip recipients that provably no
         * longer consume this action.  Sound under loss -- the mask is
         * built only from messages already received from j. */
        if (acts[i].skip && BRACHA87_SKIP_TST(acts[i].skip, j))
          continue;
        if (dropPercent > 0 && (rngNext() % 100) < dropPercent)
          continue;
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_ACAST;
        w.process = acts[i].process;
        w.type = acts[i].type;
        w.from = self;
        w.to = (unsigned char)j;
        w.accepted = acts[i].accepted;
        w.received = acts[i].received
                  && BRACHA87_SKIP_TST(acts[i].received, j);
        if (acts[i].value && vBytes <= sizeof (w.value))
          memcpy(w.value, acts[i].value, vBytes);
        qPush(&w);
      }
      break;
    case BKR94ACS_ACT_BA_SEND:
      /* "Self input" to BA_self is the local process broadcasting
       * its own input value (1 from step 1, 0 from step 2 fanout).
       * Surfaces as the round-0 BA_SEND with initiator == self &&
       * type == INITIAL.  Subsequent rounds also output BA_SEND with
       * initiator == self / type == INITIAL but those are Fig4
       * round-r values, not BKR94-layer inputs -- filter them out. */
      if (acts[i].initiator == self
       && acts[i].type == BRACHA87_INITIAL
       && acts[i].round == 0) {
        unsigned int oj = acts[i].process;
        obs->selfInputAny[oj] = 1;
        if (obs->selfInputValue[oj] == 0xFF)
          obs->selfInputValue[oj] = acts[i].baValue;
        else if (obs->selfInputValue[oj] != acts[i].baValue)
          ++obs->selfInputDisagree[oj];
      }
      for (j = 0; j < nAct; ++j) {
        if (silentProcess >= 0 && (int)j == silentProcess)
          continue;
        if (acts[i].skip && BRACHA87_SKIP_TST(acts[i].skip, j))
          continue;
        if (dropPercent > 0 && (rngNext() % 100) < dropPercent)
          continue;
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_BA;
        w.process = acts[i].process;
        w.round = acts[i].round;
        w.initiator = acts[i].initiator;
        w.type = acts[i].type;
        w.from = self;
        w.to = (unsigned char)j;
        w.baValue = acts[i].baValue;
        w.accepted = acts[i].accepted;
        w.received = acts[i].received
                  && BRACHA87_SKIP_TST(acts[i].received, j);
        qPush(&w);
      }
      break;
    case BKR94ACS_ACT_BA_DECIDED:
      ++obs->baDecidedCount[acts[i].process];
      obs->baDecidedValue[acts[i].process] = acts[i].baValue;
      break;
    case BKR94ACS_ACT_COMPLETE:
      ++obs->completeCount;
      break;
    case BKR94ACS_ACT_BA_EXHAUSTED:
      ++obs->exhaustedCount[acts[i].process];
      break;
    default:
      break;
    }
  }
}

/* BA round turns at enabling -- the drain bkr94acs.h prescribes at
 * bkr94acsTurn: after any delivery or retry that may have banked
 * evidence, turn every BA that became turnable.  The while() is
 * required (cascaded validation can unlock several successive
 * rounds), and the sweep runs over ALL BAs of the instance because
 * an A-Cast accept enters round 0 of a BA the arrival did not name.
 * BA_SENDs go to the wire; BA_DECIDED / COMPLETE / BA_EXHAUSTED are
 * observation-only. */
static void
drainTurns(
  struct bkr94acs *process
 ,struct processObs *obs
 ,unsigned char self
 ,unsigned int nAct
 ,unsigned int vBytes
 ,struct bkr94acsAct *out
 ,unsigned int dropPercent
 ,int silentProcess
){
  unsigned int b, n;

  for (b = 0; b < nAct; ++b)
    while ((n = bkr94acsTurn(process, (unsigned char)b, out)) > 0 && turnDrained()) {
      CHECK(n <= 3, "turn outputs at most 3 acts");
      observeAndOutput(obs, self, nAct, out, n, vBytes, dropPercent,
                     silentProcess);
    }
}

/* Deliver one wire message to its recipient process; observe the
 * resulting acts. */
static void
deliverWire(
  struct bkr94acs *process
 ,struct processObs *obs
 ,const struct wire *w
 ,unsigned int nAct
 ,unsigned int vBytes
 ,struct bkr94acsAct *out
 ,unsigned int outCap
){
  unsigned int n;

  if (w->cls == BKR94ACS_CLS_ACAST) {
    n = bkr94acsAcastInput(process, w->process, w->type, wireAnnot(w), w->from,
                              w->value, out);
    CHECK(n <= 3, "A-Cast input act count within its bound (2 + enter-1)");
  } else {
    n = bkr94acsBaInput(process, w->process, w->round, w->initiator,
                               w->type, wireAnnot(w), w->from, w->baValue, out);
    CHECK(n <= 2, "BA input act count within its bound (echo/ready only)");
  }
  observeAndOutput(obs, w->to, nAct, out, n, vBytes, 0, -1);

  /* Sweep-side decisions at enabling, turns first
   * (only a turn produces the decisions the fanout counts; a fanout
   * cannot make a round turnable -- it writes only entered[] and
   * round-0 initiator state, which no turn duty reads). */
  drainTurns(process, obs, w->to, nAct, vBytes, out, 0, -1);
  n = bkr94acsFanout(process, out);
  CHECK(n <= outCap, "fanout act count within MAX_ACTS bound");
  observeAndOutput(obs, w->to, nAct, out, n, vBytes, 0, -1);
}

/* ------------------------------------------------------------------ */
/*  Honest-run simulator: every process acasts, all messages are       */
/*  delivered (no drops), drive until the queue is empty.             */
/*  Retry is not invoked -- under no-loss the protocol converges       */
/*  organically.  Section C drives Retry and fault injection; D adds   */
/*  EXHAUSTED setup.                                                  */
/* ------------------------------------------------------------------ */

static int
runHonest(
  unsigned int nAct
 ,unsigned int vLen
 ,const unsigned char *acasts  /* nAct * vLen bytes */
 ,int shuffled
 ,struct bkr94acs **processes          /* allocated and Init'd by caller */
 ,struct processObs *obs              /* zeroed by caller */
){
  unsigned long actsCap;
  struct bkr94acsAct *out;
  struct bkr94acsAct acastOut[1];
  unsigned int i, n;
  struct wire w;

  qReset();

  actsCap = BKR94ACS_MAX_ACTS(nAct - 1);
  out = malloc(actsCap * sizeof (*out));
  if (!out)
    return (-1);

  /* Each process A-Casts its value; broadcast ACAST_SEND/INITIAL to all. */
  for (i = 0; i < nAct; ++i) {
    n = bkr94acsAcast(processes[i], acasts + i * vLen, acastOut);
    CHECK(n == 1, "A-Cast returns 1 act");
    if (n == 1) {
      CHECK(acastOut[0].act == BKR94ACS_ACT_ACAST_SEND, "A-Cast outputs ACAST_SEND");
      CHECK(acastOut[0].process == (unsigned char)i, "A-Cast process == self");
      CHECK(acastOut[0].type == BRACHA87_INITIAL, "A-Cast type == INITIAL");
    }
    observeAndOutput(&obs[i], (unsigned char)i, nAct, acastOut, n, vLen, 0, -1);
  }

  /* Drain. */
  while (qSize() > 0) {
    int got;
    got = shuffled ? qPopRandom(&w) : qPopHead(&w);
    if (!got)
      break;
    deliverWire(processes[w.to], &obs[w.to], &w, nAct, vLen, out, actsCap);
  }

  free(out);
  return (0);
}

/* Shared assertion helper: Lemma 2 Parts A/B/C/D plus the paper-direct
 * invariants.  Operates entirely through public accessors and obs[]. */
static void
assertLemma2(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,unsigned int nAct
 ,unsigned int t
){
  unsigned char subset0[MAX_PROCESSES];
  unsigned char subsetI[MAX_PROCESSES];
  unsigned int sz0, szI;
  unsigned int i, j;

  /* Part B: all processes complete (a->complete). */
  for (i = 0; i < nAct; ++i)
    CHECK(processes[i]->complete,
          "Lemma 2 Part B: process completed");

  /* Part A: |SubSet| >= n - t for every process. */
  sz0 = bkr94acsSubset(processes[0], subset0);
  CHECK(sz0 >= nAct - t, "Lemma 2 Part A: |SubSet| >= n-t (>= 2t+1)");

  /* Part C: every process's SubSet equals process 0's. */
  for (i = 1; i < nAct; ++i) {
    szI = bkr94acsSubset(processes[i], subsetI);
    CHECK(szI == sz0, "Lemma 2 Part C: SubSet sizes agree");
    if (szI == sz0)
      CHECK(memcmp(subset0, subsetI, sz0) == 0,
            "Lemma 2 Part C: SubSet contents agree");
  }

  /* Part D: Q(j)=1 for every j in SubSet.  In this deployment Q(j) is
   * "Fig1 reliable broadcast for process j has ACCEPTED", surfaced as
   * bkr94acsAcastValue(j) returning a non-null pointer. */
  for (j = 0; j < sz0; ++j) {
    unsigned int oj = subset0[j];
    for (i = 0; i < nAct; ++i)
      CHECK(bkr94acsAcastValue(processes[i], (unsigned char)oj) != 0,
            "Lemma 2 Part D: AcastValue(j) != NULL for j in SubSet");
  }

  /* Single COMPLETE per process. */
  for (i = 0; i < nAct; ++i)
    CHECK(obs[i].completeCount == 1, "single COMPLETE per process");

  /* Single BA_DECIDED per (process, process); decided values agree across processes. */
  for (j = 0; j < nAct; ++j) {
    unsigned int v0 = obs[0].baDecidedValue[j];
    for (i = 0; i < nAct; ++i) {
      CHECK(obs[i].baDecidedCount[j] == 1,
            "single BA_DECIDED per (process, process)");
      CHECK(obs[i].baDecidedValue[j] == v0,
            "BA_j decisions agree across processes");
      CHECK(bkr94acsBaDecision(processes[i], (unsigned char)j) == v0,
            "BaDecision accessor matches BA_DECIDED act");
    }
  }

  /* Single input per BA per process (paper Implementer remark): every
   * honest process enters exactly one VALUE into every BA -- 1 from
   * step 1 once Q(j)=1 is learned, or 0 from step 2's enter-0 fanout.
   * "Step 1 and step 2 stop touching it" once the input is entered.
   *
   * Under loss, BPR retries the round-0 INITIAL many times for
   * delivery, but always with the same value -- retries do not
   * constitute "entering" a new input.  Verify by witnessing that
   * the value stayed consistent across all observed self-INITIAL
   * round-0 outputs (no BKR94 step-1/step-2 disagreement), and
   * that every BA received some input (under all-honest no-loss
   * runs every process enters every BA by completion). */
  for (i = 0; i < nAct; ++i)
    for (j = 0; j < nAct; ++j) {
      CHECK(obs[i].selfInputAny[j],
            "every BA received an input from every honest process");
      CHECK(obs[i].selfInputDisagree[j] == 0,
            "single input value per BA per process (no step-1/step-2 disagreement)");
    }

  /* No EXHAUSTED in honest runs. */
  for (i = 0; i < nAct; ++i)
    for (j = 0; j < nAct; ++j)
      CHECK(obs[i].exhaustedCount[j] == 0, "no EXHAUSTED in honest runs");
}

/* ------------------------------------------------------------------ */
/*  Allocate and initialize a process cluster of size nAct (encoded as   */
/*  nEnc = nAct - 1) at given t / vLen / maxPhases.                   */
/* ------------------------------------------------------------------ */

static int
allocCluster(
  struct bkr94acs **processes
 ,unsigned int nAct
 ,unsigned int t
 ,unsigned int vLenEnc
 ,unsigned int maxPhases
){
  unsigned long sz;
  unsigned int i;

  sz = bkr94acsSz(nAct - 1, vLenEnc, maxPhases);
  for (i = 0; i < nAct; ++i) {
    processes[i] = calloc(1, sz);
    if (!processes[i])
      return (-1);
    bkr94acsInit(processes[i],
                 (unsigned char)(nAct - 1),
                 (unsigned char)t,
                 (unsigned char)vLenEnc,
                 (unsigned char)maxPhases,
                 (unsigned char)i,
                 testCoin, 0);
  }
  return (0);
}

static void
freeCluster(
  struct bkr94acs **processes
 ,unsigned int nAct
){
  unsigned int i;
  for (i = 0; i < nAct; ++i) {
    free(processes[i]);
    processes[i] = 0;
  }
}

/* ------------------------------------------------------------------ */
/*  Retry-driven driver with optional drops + silent process.             */
/*                                                                    */
/*  Used by Section C/D/E.  Each iteration:                           */
/*    1. Drain wire queue, calling deliverWire for every popped wire  */
/*       (silent process's wires are skipped at output, not delivery). */
/*    2. Call bkr94acsRetryStep once per non-silent process; output acts.   */
/*    3. Verify process-level invariants (Retry act count <= MAX,         */
/*       SentFig1Count monotone non-decreasing).                 */
/*    4. Exit when all non-silent processes carry complete.    */
/*                                                                    */
/*  Silent process (-1 = none): never receives wires, never has its      */
/*  A-Cast/Retry called, never appears in completion check.           */
/*                                                                    */
/*  Returns 0 on convergence, -1 on iter cap or alloc failure.        */
/*  Witness counters: maxRetryActs, monotoneViolations.                */
/* ------------------------------------------------------------------ */

static int
runWithRetry(
  unsigned int nAct
 ,unsigned int vLen
 ,const unsigned char *acasts  /* nAct * vLen bytes; entry for silent process ignored */
 ,unsigned int dropPercent        /* 0..99 */
 ,int silentProcess                  /* -1 = none */
 ,unsigned int maxIters           /* outer loop safety cap */
 ,struct bkr94acs **processes
 ,struct processObs *obs
 ,unsigned int *maxRetryActsOut    /* witness: max acts ever output by Retry */
 ,unsigned int *monotoneViolationsOut /* witness: SentFig1Count regressions */
){
  struct bracha87Retry cursors[MAX_PROCESSES];
  unsigned long actsCap;
  struct bkr94acsAct *out;
  struct bkr94acsAct acastOut[1];
  struct bkr94acsAct retryOut[BKR94ACS_RETRY_MAX_ACTS];
  unsigned int prevSent[MAX_PROCESSES];
  unsigned int i, n, iter;
  unsigned int maxRetryActs = 0;
  unsigned int monotoneViolations = 0;
  struct wire w;
  int allComplete;

  qReset();
  for (i = 0; i < nAct; ++i) {
    bracha87RetryInit(&cursors[i]);
    prevSent[i] = 0;
  }

  actsCap = BKR94ACS_MAX_ACTS(nAct - 1);
  out = malloc(actsCap * sizeof (*out));
  if (!out)
    return (-1);

  /* Each non-silent process A-Casts; ACAST_SEND/INITIAL outputs to wire. */
  for (i = 0; i < nAct; ++i) {
    if (silentProcess >= 0 && (int)i == silentProcess)
      continue;
    n = bkr94acsAcast(processes[i], acasts + i * vLen, acastOut);
    observeAndOutput(&obs[i], (unsigned char)i, nAct, acastOut, n, vLen,
                   dropPercent, silentProcess);
  }

  for (iter = 0; iter < maxIters; ++iter) {
    /* Drain queue. */
    while (qSize() > 0) {
      qPopHead(&w);
      /* Silent process never receives -- defensive (output already
       * skipped them). */
      if (silentProcess >= 0 && (int)w.to == silentProcess)
        continue;
      if (w.cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w), w.from,
                                  w.value, out);
        /* BPR ACCEPTED annotation rides on a READY; feed it AFTER Input
         * (which records rdFrom) so acFrom stays a subset of rdFrom. */
        /* The RECEIVED bit's ABSENCE is the arm: the sender has not
         * recorded our accept, so un-suppress it for the next egress. */
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, wireAnnot(&w), w.from,
                                   w.baValue, out);
      }
      observeAndOutput(&obs[w.to], w.to, nAct, out, n, vLen,
                     dropPercent, silentProcess);
      /* Sweep-side decisions at enabling, turns first (see
       * deliverWire).  The wires ride the same lossy output path; a
       * dropped INITIAL is BPR-retried like any other, so firing
       * here stays loss-safe. */
      drainTurns(processes[w.to], &obs[w.to], w.to, nAct, vLen, out,
                 dropPercent, silentProcess);
      n = bkr94acsFanout(processes[w.to], out);
      observeAndOutput(&obs[w.to], w.to, nAct, out, n, vLen,
                     dropPercent, silentProcess);
    }

    /* Retry every non-silent process once.  Per .h:
     *   - returns at most BKR94ACS_RETRY_MAX_ACTS
     *   - returns 0 only when full sweep finds no sent instance
     *     (pre-broadcast / shutdown -- never expected mid-run after
     *     A-Cast has set INITIATOR). */
    for (i = 0; i < nAct; ++i) {
      if (silentProcess >= 0 && (int)i == silentProcess)
        continue;
      n = bkr94acsRetryStep(processes[i], &cursors[i], retryOut);
      if (n > maxRetryActs)
        maxRetryActs = n;
      observeAndOutput(&obs[i], (unsigned char)i, nAct, retryOut, n, vLen,
                     dropPercent, silentProcess);
      drainTurns(processes[i], &obs[i], (unsigned char)i, nAct, vLen, out,
                 dropPercent, silentProcess);
      n = bkr94acsFanout(processes[i], out);
      observeAndOutput(&obs[i], (unsigned char)i, nAct, out, n, vLen,
                     dropPercent, silentProcess);
    }

    /* Monotone SentFig1Count check. */
    for (i = 0; i < nAct; ++i) {
      unsigned int cur;
      if (silentProcess >= 0 && (int)i == silentProcess)
        continue;
      cur = bkr94acsFig1SentCount(processes[i]);
      if (cur < prevSent[i])
        ++monotoneViolations;
      prevSent[i] = cur;
    }

    /* Exit when all non-silent processes have completed. */
    allComplete = 1;
    for (i = 0; i < nAct; ++i) {
      if (silentProcess >= 0 && (int)i == silentProcess)
        continue;
      if (!processes[i]->complete) {
        allComplete = 0;
        break;
      }
    }
    if (allComplete)
      break;
  }

  free(out);
  if (maxRetryActsOut)
    *maxRetryActsOut = maxRetryActs;
  if (monotoneViolationsOut)
    *monotoneViolationsOut = monotoneViolations;
  return (allComplete ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/*  Synthetic Fig1 ACCEPT helper for Section D's EXHAUSTED setup and  */
/*  Section G's duty arms.  Drives a single (process, round,           */
/*  initiator) BA Fig1 to ACCEPT at process 'a' with the given binary  */
/*  value, by feeding 1 INITIAL + 3 distinct READYs (Bracha87 Rule 5   */
/*  then Rule 6 fires).  Entirely public-API.                         */
/*                                                                    */
/*  The inputs only BANK evidence -- per bkr94acs.h an accept can      */
/*  produce nothing but echo/ready acts.  BA_EXHAUSTED (like DECIDED   */
/*  and COMPLETE) emerges from bkr94acsTurn, so 'turned' selects the   */
/*  caller's schedule: nonzero drains turns after every input, at      */
/*  enabling (the schedule D1/D2 want, counting EXHAUSTED              */
/*  from the turn's acts), zero banks without turning (Section G,      */
/*  which must read a duty class over a round the caller has not yet   */
/*  consumed).                                                        */
/*                                                                    */
/*  FeedLastActs holds the act count of the LAST bkr94acsBaInput this  */
/*  helper made -- the delivery that carried the round to ACCEPT.      */
/*  Section N2 reads it to witness that the delivery which moves       */
/*  TurnDuty HELD -> TOLERANCE returns no acts of its own.             */
/* ------------------------------------------------------------------ */

static unsigned int FeedLastActs = 0;

static unsigned int
feedBAAccept(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char value
 ,struct bkr94acsAct *out
 ,unsigned int turned
 ,unsigned int *exhaustedSeen
){
  unsigned int total = 0;
  unsigned int n, k;
  unsigned char sender;

  n = bkr94acsBaInput(a, process, round, initiator,
                             BRACHA87_INITIAL, ANNOT_NO_ARM, initiator, value, out);
  CHECK(n <= 2, "feedBAAccept: BA input outputs at most 2 acts");
  FeedLastActs = n;
  total += n;
  if (turned)
    while ((n = bkr94acsTurn(a, process, out)) > 0 && turnDrained()) {
      for (k = 0; k < n; ++k)
        if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED
         && out[k].process == process)
          ++*exhaustedSeen;
      total += n;
    }

  /* Three distinct READYs trip Rule 5 (rd>=t+1) then Rule 6 (rd>=2t+1)
   * -> ACCEPT.  Senders 1, 2, 3 (initiator's own READY isn't needed
   * since echoed is set after INITIAL). */
  for (sender = 1; sender <= 3; ++sender) {
    n = bkr94acsBaInput(a, process, round, initiator,
                               BRACHA87_READY, ANNOT_NO_ARM, sender, value, out);
    CHECK(n <= 2, "feedBAAccept: BA input outputs at most 2 acts");
    FeedLastActs = n;
    total += n;
    if (turned)
      while ((n = bkr94acsTurn(a, process, out)) > 0 && turnDrained()) {
        for (k = 0; k < n; ++k)
          if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED
           && out[k].process == process)
            ++*exhaustedSeen;
        total += n;
      }
  }
  return (total);
}

/* ------------------------------------------------------------------ */
/*  Section F driver: drain + retry sweeps with CALLER-PACED step 2   */
/*  per the bkr94acs.h discipline -- count completed sweeps while     */
/*  bkr94acsFanoutDuty holds TOLERANCE, call bkr94acsFanout when the  */
/*  count exceeds the patience.  n = 4, no loss.                      */
/*                                                                    */
/*    patience < 0 never fire the fanout                              */
/*    patience >= 0 per process, fire after patience TOLERANCE sweeps */
/*    silent >= 0  that process is dead: never receives, never        */
/*                 retries, excluded from pacing and completion       */
/*                                                                    */
/*  Also pins the duty trichotomy's monotonicity at every live        */
/*  process: MET is absorbing and TOLERANCE never regresses to HELD   */
/*  (decides and entries only accumulate).                            */
/*  Returns 0 when every live process completed within maxIters.      */
/* ------------------------------------------------------------------ */

static int
fDrive(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,int patience
 ,int silent
 ,unsigned int maxIters
 ,unsigned int *toleranceSweepsMax  /* out: max per-process TOLERANCE sweeps */
 ,unsigned int *fanoutActsTotal     /* out: total fanout acts, all processes */
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct wire w;
  unsigned int sweeps[4];
  unsigned char prevDuty[4];
  unsigned int n, p, iter;
  int allDone;

  for (p = 0; p < 4; ++p) {
    sweeps[p] = 0;
    prevDuty[p] = bkr94acsFanoutDuty(processes[p]);
  }
  *toleranceSweepsMax = 0;
  *fanoutActsTotal = 0;

  allDone = 0;
  for (iter = 0; iter < maxIters && !allDone; ++iter) {
    while (qSize() > 0) {
      qPopHead(&w);
      if (silent >= 0 && (int)w.to == silent)
        continue;
      if (w.cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                  wireAnnot(&w),
                                  w.from, w.value, out);
        /* The RECEIVED bit's ABSENCE is the arm: the sender has not
         * recorded our accept, so un-suppress it for the next egress. */
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, wireAnnot(&w), w.from,
                                   w.baValue, out);
      }
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, silent);
    }

    for (p = 0; p < 4; ++p) {
      unsigned char duty;

      if (silent >= 0 && (int)p == silent)
        continue;
      n = bkr94acsRetryStep(processes[p], &cursors[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, silent);

      /* The round turn is drained at ZERO patience before the
       * fanout: the decisions a turn produces are what enables
       * step 2, so only the FANOUT's pacing is the variable this
       * section isolates.  No drain after it -- a fanout writes
       * only entered[] and round-0 initiator state, which no turn
       * duty reads, so it cannot make a round turnable. */
      drainTurns(processes[p], &obs[p], (unsigned char)p, 4, 1, out, 0,
                 silent);

      duty = bkr94acsFanoutDuty(processes[p]);
      CHECK(!(prevDuty[p] == BKR94ACS_DUTY_MET
              && duty != BKR94ACS_DUTY_MET),
            "F: MET is absorbing");
      CHECK(!(prevDuty[p] == BKR94ACS_DUTY_TOLERANCE
              && duty == BKR94ACS_DUTY_HELD),
            "F: TOLERANCE never regresses to HELD");
      prevDuty[p] = duty;

      if (duty == BKR94ACS_DUTY_TOLERANCE) {
        ++sweeps[p];
        if (sweeps[p] > *toleranceSweepsMax)
          *toleranceSweepsMax = sweeps[p];
        if (patience >= 0 && sweeps[p] > (unsigned int)patience) {
          n = bkr94acsFanout(processes[p], out);
          *fanoutActsTotal += n;
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0,
                         silent);
        }
      }
    }

    allDone = 1;
    for (p = 0; p < 4; ++p) {
      if (silent >= 0 && (int)p == silent)
        continue;
      if (!processes[p]->complete) {
        allDone = 0;
        break;
      }
    }
  }
  return (allDone ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/*  THE BARREN-SWEEP POLICY -- the harness's own abandonment machinery */
/*                                                                    */
/*  A suite harness IS an application, and the scenario sections below */
/*  assert against the evidence stream an application's termination    */
/*  policy reads, so the policy's shape is fixed once here rather than */
/*  re-invented per arm.  This is HARNESS application code, not a      */
/*  machine assertion: its operational reference is the bundled        */
/*  application loop (example/bkr94acs.c), the same standing           */
/*  runWithRetry's loop shape already has.                            */
/*                                                                    */
/*  PROGRESS at a process, exactly the README's own list: an Input     */
/*  call (bkr94acsAcastInput / bkr94acsBaInput) returning one or more  */
/*  acts, or a decision act (BKR94ACS_ACT_BA_DECIDED /                 */
/*  BKR94ACS_ACT_COMPLETE) from a turn.  NOTHING ELSE -- routine       */
/*  BA_SEND acts from a turn or from the fanout are not progress, and  */
/*  the bkr94acsRetryStep egress is not progress (it is the               */
/*  retransmission stream, and its duplicates produce 0 acts at the    */
/*  receiver, the C8 invariant).  The narrowness is load-bearing:      */
/*  post-decide continuation turns produce BA_SENDs each sweep through */
/*  the phase after the decision, so a definition counting them would  */
/*  make M1's monotone climb false against a CORRECT machine.          */
/*                                                                    */
/*  SWEEP boundary at a process, the header's rule: the retry          */
/*  cursor's `sweeps` wrap count changed -- compared, never assumed    */
/*  to advance by one, since one call can complete two passes -- or,   */
/*  for a process already marked quiescent and skipping its Retry      */
/*  call, one idle sweep per tick.  Counting calls against             */
/*  bkr94acsFig1SentCount would close late by the retired count.       */
/*                                                                    */
/*  BARREN = a completed sweep that observed no progress.  The policy  */
/*  fires after S consecutive barren sweeps; budget compares use >=,   */
/*  so a zero budget fires on the first evaluation, before any sweep   */
/*  completes.                                                         */
/*                                                                    */
/*  The counter is per process and is harness policy, never library    */
/*  state.                                                            */
/* ------------------------------------------------------------------ */

#define BARREN_S 8    /* the harness policy's S */

struct sweepPolicy {
  unsigned int lastSweeps; /* last-seen cursor wrap count */
  unsigned int progress; /* progress events seen in the sweep under way */
  unsigned int sweeps;   /* completed sweeps */
  unsigned int barren;   /* consecutive barren sweeps */
};

/* One tick's contribution to a process's sweep counter.  'skipped' is
 * the quiescent-process branch: no Retry call this tick, so the pass
 * it would have made owes nothing and completes at once. */
static unsigned int
spTick(
  struct sweepPolicy *sp
 ,unsigned int cursorSweeps
 ,unsigned int skipped
){
  unsigned int done;

  /* The header's rule: the cursor's wrap count IS the pass boundary.
   * Compare it -- one call can complete two passes -- and never count
   * calls against bkr94acsFig1SentCount, which is an upper bound that
   * drifts longer as instances retire. */
  done = 0;
  if (skipped)
    done = 1;
  else if (cursorSweeps != sp->lastSweeps) {
    sp->lastSweeps = cursorSweeps;
    done = 1;
  }
  if (done) {
    ++sp->sweeps;
    if (sp->progress)
      sp->barren = 0;
    else
      ++sp->barren;
    sp->progress = 0;
  }
  return (done);
}

/* ------------------------------------------------------------------ */
/*  Section F4 driver -- the two sweep clocks running side by side.   */
/*                                                                    */
/*  F1/F2/F3 pace the fanout alone.  This driver runs the SAME        */
/*  delayed-A-Cast schedule with the barren-sweep policy above        */
/*  running beside it, so the patience the fanout charges and the     */
/*  barren count the abandon gate reads advance on the same           */
/*  boundary -- the premise the ordering rests on.                    */
/*                                                                    */
/*  Per tick, per process: drain, retry, drain the turns at zero      */
/*  patience (only the FANOUT's pacing is the variable, the same      */
/*  isolation Section F takes), CLOSE THE SWEEP, then read the duty.  */
/*  Closing after the turns is what banks a turn's decision in the    */
/*  sweep it happened in, which is the granularity the ordering is    */
/*  stated at.                                                        */
/*                                                                    */
/*    patience      G, in completed sweeps; the fanout fires once the */
/*                  charged count exceeds it                          */
/*    abandonS      S, in consecutive barren sweeps                   */
/*    releaseSweep  the delayed A-Cast is submitted once process 0    */
/*                  has charged this many patience sweeps; 0 = never  */
/*                                                                    */
/*  Returns 0 when all four processes completed, -1 otherwise (a      */
/*  process reached S consecutive barren sweeps, or the tick cap).    */
/* ------------------------------------------------------------------ */

static int
fbDrive(
  unsigned int patience
 ,unsigned int abandonS
 ,unsigned int releaseSweep
 ,unsigned int maxTicks
 ,unsigned int *patienceMaxOut   /* out: highest patience charged */
 ,unsigned int *barrenMaxOut     /* out: highest barren count reached */
 ,unsigned int *includedOut      /* out: delayed process in the subset? */
 ,unsigned int *fanoutActsOut    /* out: total fanout acts */
 ,unsigned int *abandonedOut     /* out: 1 + the first process to abandon */
){
  struct bkr94acs *processes[4];
  struct processObs obs[4];
  struct bracha87Retry cursors[4];
  struct sweepPolicy pol[4];
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct bkr94acsAct acastOut[1];
  struct wire w;
  unsigned char acast[4];
  unsigned char subset[4];
  unsigned char prevDuty[4];
  unsigned char duty;
  unsigned int spent[4];
  unsigned int opening[4];
  unsigned int delayed = 3;
  unsigned int tick, p, b, j, n, sz;
  unsigned int retryActs, sweepDone, decidedTick, released;
  int done;

  *patienceMaxOut = 0;
  *barrenMaxOut = 0;
  *includedOut = 0;
  *fanoutActsOut = 0;
  *abandonedOut = 0;
  if (allocCluster(processes, 4, 1, 0, 8))
    return (-1);
  qReset();
  for (p = 0; p < 4; ++p) {
    obsInit(&obs[p]);
    bracha87RetryInit(&cursors[p]);
    memset(&pol[p], 0, sizeof (pol[p]));
    spent[p] = 0;
    opening[p] = 0;
    prevDuty[p] = bkr94acsFanoutDuty(processes[p]);
    acast[p] = (unsigned char)(0xB0 + p);
  }
  /* The same schedule F1/F2 run: 0-2 A-Cast now, 3 is the laggard
   * whose OUTBOUND A-Cast is held.  The process itself participates. */
  for (p = 0; p < 3; ++p) {
    n = bkr94acsAcast(processes[p], &acast[p], acastOut);
    observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
  }

  released = 0;
  done = 0;
  for (tick = 0; tick < maxTicks && !done && !*abandonedOut; ++tick) {
    while (qSize() > 0) {
      qPopHead(&w);
      if (w.cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                  wireAnnot(&w),
                                  w.from, w.value, out);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, wireAnnot(&w), w.from,
                                   w.baValue, out);
      }
      if (n)
        ++pol[w.to].progress;
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
    }

    for (p = 0; p < 4; ++p) {
      retryActs = bkr94acsRetryStep(processes[p], &cursors[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, retryActs, 1, 0,
                       -1);

      decidedTick = 0;
      for (b = 0; b < 4; ++b)
        while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out))
               > 0 && turnDrained()) {
          for (j = 0; j < n; ++j)
            if (out[j].act == BKR94ACS_ACT_BA_DECIDED) {
              ++decidedTick;
              ++pol[p].progress;
            } else if (out[j].act == BKR94ACS_ACT_COMPLETE)
              ++pol[p].progress;
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }

      sweepDone = spTick(&pol[p], cursors[p].sweeps, 0);
      if (pol[p].barren > *barrenMaxOut)
        *barrenMaxOut = pol[p].barren;

      duty = bkr94acsFanoutDuty(processes[p]);
      if (prevDuty[p] == BKR94ACS_DUTY_HELD
       && duty != BKR94ACS_DUTY_HELD) {
        /* The window's opening carries an act.  The only way out of
         * HELD is the n-t'th BA output of 1, and the decision byte is
         * recorded in the same straight-line block that outputs the
         * act -- so the caller was handed a progress event in the
         * very tick the duty moved. */
        CHECK(decidedTick > 0,
              "F4: the fanout's window opens on a BA_DECIDED act");
        opening[p] = 1;
      }
      prevDuty[p] = duty;
      if (opening[p] && sweepDone) {
        /* Stated at the boundary, not at the instant: the barren
         * count resets at the COMPLETION of the opening sweep. */
        CHECK(pol[p].barren == 0, "F4: the opening sweep is not barren");
        opening[p] = 0;
      }

      if (duty == BKR94ACS_DUTY_TOLERANCE) {
        if (sweepDone) {
          ++spent[p];
          if (spent[p] > *patienceMaxOut)
            *patienceMaxOut = spent[p];
          /* Both clocks take this boundary; the patience takes every
           * one of them and the barren count only the ones without
           * progress, and the opening sweep had progress -- so the
           * barren count trails the patience for as long as the
           * window stands. */
          CHECK(pol[p].barren < spent[p],
                "F4: the barren count trails the patience at every"
                " sweep boundary");
        }
        if (spent[p] > patience) {
          n = bkr94acsFanout(processes[p], out);
          *fanoutActsOut += n;
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }
      } else
        spent[p] = 0;

      /* Reaching COMPLETE first is the success flavor of the same
       * question, so only a process still running can abandon. */
      if (!processes[p]->complete && pol[p].barren >= abandonS)
        *abandonedOut = p + 1;
    }

    if (!released && releaseSweep && spent[0] >= releaseSweep) {
      released = 1;
      n = bkr94acsAcast(processes[delayed], &acast[delayed], acastOut);
      observeAndOutput(&obs[delayed], (unsigned char)delayed, 4, acastOut,
                       n, 1, 0, -1);
    }

    done = 1;
    for (p = 0; p < 4; ++p)
      if (!processes[p]->complete)
        done = 0;
  }

  if (done) {
    sz = bkr94acsSubset(processes[0], subset);
    for (j = 0; j < sz; ++j)
      if (subset[j] == (unsigned char)delayed)
        *includedOut = 1;
  }
  freeCluster(processes, 4);
  return (done ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/*  Section J driver -- the asymmetric-flow cut.                      */
/*                                                                    */
/*  Wires are discarded at DELIVERY, which is the socket-level cut     */
/*  bkr94acs.h's fair-loss posture describes: 'cutFrom' drops every    */
/*  wire a process SENT (its egress half, self-delivery included),     */
/*  'cutTo' drops every wire ADDRESSED to it (its ingress half,        */
/*  self-delivery included).  Sparing self-delivery would inject       */
/*  exactly one progress event -- the process's own INITIAL returning  */
/*  its echo -- and falsify the barrenness claim.  No other loss: at   */
/*  n=4 t=1 the lane is exactly tight (echo threshold 3 = available    */
/*  echoers, 2t+1 = 3 readys, n-t = 3 deciders).                      */
/*                                                                    */
/*  The drive ends when every process the cut leaves able to complete  */
/*  has completed AND, when an ingress cut stands, the cut process's   */
/*  barren counter has reached the harness policy's S -- the only exit */
/*  that process has.                                                 */
/* ------------------------------------------------------------------ */

static int
jDrive(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,int cutFrom
 ,int cutTo
 ,unsigned int maxIters
 ,unsigned int *zeroRetriesOut  /* out: Retry 0 returns at the cut process */
 ,unsigned int *barrenDropsOut  /* out: barren regressions at the cut process */
 ,unsigned int *itersOut
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct wire w;
  unsigned int iter, p, b, n, k;
  unsigned int prevBarren[4];
  int done;

  *zeroRetriesOut = 0;
  *barrenDropsOut = 0;
  for (p = 0; p < 4; ++p)
    prevBarren[p] = 0;

  done = 0;
  for (iter = 0; iter < maxIters && !done; ++iter) {
    while (qSize() > 0) {
      qPopHead(&w);
      if (cutFrom >= 0 && (int)w.from == cutFrom)
        continue;
      if (cutTo >= 0 && (int)w.to == cutTo)
        continue;
      if (w.cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                  wireAnnot(&w),
                                  w.from, w.value, out);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, wireAnnot(&w), w.from,
                                   w.baValue, out);
      }
      if (n)
        ++pol[w.to].progress;   /* PROGRESS: an Input returning acts */
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
    }

    for (p = 0; p < 4; ++p) {
      n = bkr94acsRetryStep(processes[p], &cursors[p], out);
      if (!n && cutTo >= 0 && (int)p == cutTo)
        ++*zeroRetriesOut;
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      spTick(&pol[p], cursors[p].sweeps, 0);
      if (pol[p].barren < prevBarren[p]
       && cutTo >= 0 && (int)p == cutTo)
        ++*barrenDropsOut;
      prevBarren[p] = pol[p].barren;

      for (b = 0; b < 4; ++b)
        while ((n = bkr94acsTurn(processes[p], (unsigned char)b,
                                 out)) > 0 && turnDrained()) {
          for (k = 0; k < n; ++k)
            if (out[k].act == BKR94ACS_ACT_BA_DECIDED
             || out[k].act == BKR94ACS_ACT_COMPLETE)
              ++pol[p].progress;  /* PROGRESS: a decision act from a turn */
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }
      n = bkr94acsFanout(processes[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    }

    done = 1;
    for (p = 0; p < 4; ++p) {
      if (cutTo >= 0 && (int)p == cutTo)
        continue;
      if (!processes[p]->complete)
        done = 0;
    }
    if (done && cutTo >= 0 && pol[cutTo].barren < BARREN_S)
      done = 0;
  }
  *itersOut = iter;
  return (done ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/*  Section I driver -- the partition.                                */
/*                                                                    */
/*  side[] names which side of the cut each process is on: a wire is   */
/*  delivered iff sender and recipient share a side, so an all-equal   */
/*  side[] is a healed network and any other assignment is a cut in    */
/*  BOTH directions.  Healing is one assignment away, which is what    */
/*  lets an arm read the survivors' egress at the instant of heal.     */
/*                                                                    */
/*  The witness follows ONE A-Cast instance at ONE recipient: how many */
/*  INITIAL and (foreign) ECHO inputs it took for that instance, and   */
/*  how many of its own ECHOes a READY input drew out of it -- Fig 1   */
/*  row 3, the t+1-readys rule, which is the only bootstrap left once  */
/*  every survivor has retired its INITIAL and ECHO retries.           */
/* ------------------------------------------------------------------ */

struct iWitness {
  unsigned int initials;      /* INITIAL inputs for the watched instance */
  unsigned int foreignEchoes; /* ECHO inputs from a process other than self */
  unsigned int readys;        /* READY inputs */
  unsigned int rowThree;      /* READY inputs that drew our own ECHO out */
  unsigned char watchTo;
  unsigned char watchProcess;
  unsigned char armed;
};

static void
iTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,const unsigned char *side
 ,struct iWitness *wit
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct wire w;
  unsigned int p, b, n, k;

  while (qSize() > 0) {
    qPopHead(&w);
    if (side[w.from] != side[w.to])
      continue;
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w), w.from,
                                w.value, out);
      if (wit && wit->armed && w.to == wit->watchTo
       && w.process == wit->watchProcess) {
        if (w.type == BRACHA87_INITIAL)
          ++wit->initials;
        else if (w.type == BRACHA87_ECHO && w.from != wit->watchTo)
          ++wit->foreignEchoes;
        else if (w.type == BRACHA87_READY) {
          ++wit->readys;
          for (k = 0; k < n; ++k)
            if (out[k].act == BKR94ACS_ACT_ACAST_SEND
             && out[k].process == wit->watchProcess
             && out[k].type == BRACHA87_ECHO)
              ++wit->rowThree;
        }
      }
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, wireAnnot(&w), w.from,
                                 w.baValue, out);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
  }

  for (p = 0; p < 4; ++p) {
    n = bkr94acsRetryStep(processes[p], &cursors[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    spTick(&pol[p], cursors[p].sweeps, 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
        for (k = 0; k < n; ++k)
          if (out[k].act == BKR94ACS_ACT_BA_DECIDED
           || out[k].act == BKR94ACS_ACT_COMPLETE)
            ++pol[p].progress;
        observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      }
    n = bkr94acsFanout(processes[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
  }
}

/* ------------------------------------------------------------------ */
/*  Section O plumbing -- the held-instance schedule.                 */
/*                                                                    */
/*  The other drivers cut the network by SIDE, which is a partition:  */
/*  a process is on the wire or it is not.  Section O needs a finer   */
/*  cut -- one A-Cast instance withheld from one receiver while every */
/*  other instance flows to it -- because that is what separates the  */
/*  two ways a BA can be entered.  Step 1 enters BA_j with 1 on       */
/*  accepting j's A-Cast; the step-2 fanout enters an un-entered BA   */
/*  with 0.  Withholding only instance j from process p is the way to */
/*  make p reach BA_j by the second route while the rest of the       */
/*  cluster reaches it by the first, so the BA's decision and p's own */
/*  input to it come apart.  Holding the BA class as well delays p's  */
/*  view of the decision past its own fanout, which is what puts the  */
/*  enter-0 BEFORE the decision rather than after it.                 */
/*                                                                    */
/*  Masks are bit-per-instance; a 0xFF receiver means every receiver. */
/*  Both are cleared on the way out of every arm.                     */
/* ------------------------------------------------------------------ */

static unsigned int OAcastHold = 0;
static unsigned char OAcastTo = 0xFF;
static unsigned int OBaHold = 0;
static unsigned char OBaTo = 0xFF;

static void
oTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,unsigned int nAct
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(6)];  /* encoded n; 7 processes */
  struct wire w;
  unsigned int p, b, n;

  while (qSize() > 0) {
    qPopHead(&w);
    if (w.cls == BKR94ACS_CLS_ACAST) {
      if ((OAcastHold & (1u << w.process))
       && (OAcastTo == 0xFF || w.to == OAcastTo))
        continue;
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                             wireAnnot(&w), w.from, w.value, out);
    } else {
      if ((OBaHold & (1u << w.process))
       && (OBaTo == 0xFF || w.to == OBaTo))
        continue;
      n = bkr94acsBaInput(processes[w.to], w.process, w.round, w.initiator,
                          w.type, wireAnnot(&w), w.from, w.baValue, out);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, nAct, out, n, 1, 0, -1);
  }

  for (p = 0; p < nAct; ++p) {
    n = bkr94acsRetryStep(processes[p], &cursors[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
    spTick(&pol[p], cursors[p].sweeps, 0);
    for (b = 0; b < nAct; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained())
        observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
    n = bkr94acsFanout(processes[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
  }
}

/* Run one held-instance schedule to process 0's completion (or the tick
 * cap).  Returns the tick it stopped on; releases both holds on exit. */
static unsigned int
oDrive(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,unsigned char *acasts
 ,unsigned int nAct
 ,unsigned int acastHold
 ,unsigned char acastTo
 ,unsigned int baHold
 ,unsigned char baTo
 ,unsigned int releaseBaAt
 ,unsigned int cap
){
  struct bracha87Retry cursors[MAX_PROCESSES];
  struct sweepPolicy pol[MAX_PROCESSES];
  struct bkr94acsAct acastOut[1];
  unsigned int p, tick, n;

  for (p = 0; p < MAX_PROCESSES; ++p)
    obsInit(&obs[p]);
  for (p = 0; p < nAct; ++p) {
    bracha87RetryInit(&cursors[p]);
    memset(&pol[p], 0, sizeof (pol[p]));
  }
  qReset();
  OAcastHold = acastHold;
  OAcastTo = acastTo;
  OBaHold = baHold;
  OBaTo = baTo;
  for (p = 0; p < nAct; ++p) {
    acasts[p] = (unsigned char)(0x70 + p);
    n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
    observeAndOutput(&obs[p], (unsigned char)p, nAct, acastOut, n, 1, 0, -1);
  }
  for (tick = 0; tick < cap; ++tick) {
    unsigned int done;

    if (tick == releaseBaAt)
      OBaHold = 0;
    oTick(processes, obs, cursors, pol, nAct);
    /* bkr94acs.h: a SubSet read before complete reports the partial
     * decided-1 set, so every arm here compares CLOSED SubSets. */
    done = 0;
    for (p = 0; p < nAct; ++p)
      if (processes[p]->complete)
        ++done;
    if (done == nAct)
      break;
  }
  OAcastHold = 0;
  OBaHold = 0;
  return (tick);
}

/* ------------------------------------------------------------------ */
/*  Section K plumbing -- the Byzantine trickler.                     */
/*                                                                    */
/*  The trickler runs no state machine, so it is not a struct         */
/*  bkr94acs at all: the harness synthesizes its wires and delivers   */
/*  them by hand, exactly as Section E synthesizes the equivocating   */
/*  A-Caster's split INITIAL.  Its whole message set is retained in   */
/*  KSupply so the arm can re-deliver it complete -- the exhaustion   */
/*  assert needs the SET, not a sample of it.                         */
/* ------------------------------------------------------------------ */

#define K_HONEST  3   /* processes 0..2; process 3 is the trickler */
#define K_ROUNDS  6   /* maxPhases 2 * BRACHA87_ROUNDS_PER_PHASE */
#define K_SUPPLY_MAX 128

static struct wire KSupply[K_SUPPLY_MAX];
static unsigned int KSupplyN = 0;

static void
kSupplyAdd(
  const struct wire *w
){
  if (KSupplyN >= K_SUPPLY_MAX) {
    fprintf(stderr, "FATAL [%s]: trickle supply overflow\n", CurTest);
    abort();
  }
  KSupply[KSupplyN++] = *w;
}

/* Deliver one trickled wire to one honest receiver; return the acts it
 * produced.  Whatever egress it induces rides the queue like any other
 * traffic, so the honest cascade a trickle starts is followed here the
 * same way an honest one is. */
static unsigned int
kDeliver(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,const struct wire *w
 ,unsigned char to
 ,unsigned int vBytes
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  unsigned int n;

  if (w->cls == BKR94ACS_CLS_ACAST)
    n = bkr94acsAcastInput(processes[to], w->process, w->type, wireAnnot(w), w->from,
                              w->value, out);
  else
    n = bkr94acsBaInput(processes[to], w->process, w->round,
                               w->initiator, w->type, wireAnnot(w), w->from,
                               w->baValue, out);
  observeAndOutput(&obs[to], to, 4, out, n, vBytes, 0, -1);
  return (n);
}

/* One tick of the honest cluster: drain, one Retry per process, the
 * zero-patience turn drain, the fanout.  Wires addressed to the trickler
 * are discarded -- it holds no state to deliver them to. */
static void
kTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,unsigned int vBytes
 ,unsigned int *zeroRetriesOut
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct wire w;
  unsigned int p, b, n, k;

  while (qSize() > 0) {
    qPopHead(&w);
    if (w.to >= K_HONEST)
      continue;
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w), w.from,
                                w.value, out);
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, wireAnnot(&w), w.from,
                                 w.baValue, out);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, 4, out, n, vBytes, 0, -1);
  }

  for (p = 0; p < K_HONEST; ++p) {
    n = bkr94acsRetryStep(processes[p], &cursors[p], out);
    if (!n && zeroRetriesOut)
      ++*zeroRetriesOut;
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, vBytes, 0, -1);
    spTick(&pol[p], cursors[p].sweeps, 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
        for (k = 0; k < n; ++k)
          if (out[k].act == BKR94ACS_ACT_BA_DECIDED
           || out[k].act == BKR94ACS_ACT_COMPLETE)
            ++pol[p].progress;
        observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, vBytes, 0, -1);
      }
    n = bkr94acsFanout(processes[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, vBytes, 0, -1);
  }
}

/* ------------------------------------------------------------------ */
/*  Section L driver -- the staggered start.                          */
/*                                                                    */
/*  down[p] means process p is not up yet: it makes no library call    */
/*  and every wire addressed to it is dropped, which is what a socket  */
/*  that is not bound yet does.  The witness counts what the late      */
/*  starter takes IN once it comes up -- and because every INITIAL     */
/*  sent before that moment was dropped, an INITIAL input afterward is */
/*  necessarily a BPR re-send and nothing else.                        */
/* ------------------------------------------------------------------ */

struct lWitness {
  unsigned int initialsIn;    /* foreign INITIAL inputs at the late starter */
  unsigned int echoesIn;      /* foreign ECHO inputs at the late starter */
  unsigned char late;
};

static void
lTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,const unsigned char *down
 ,unsigned int maxDeliver     /* wires to deliver this tick; 0 = drain */
 ,struct lWitness *wit
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct wire w;
  unsigned int p, b, n, k;
  unsigned int delivered;
  int silent;

  /* A process that is not up yet is dropped at OUTPUT, not at
   * delivery: an unbound socket loses the datagram, it does not hold
   * it for later.  A wire merely left in the queue would reach it as
   * soon as it came up, which is not a staggered start at all. */
  silent = -1;
  for (p = 0; p < 4; ++p)
    if (down[p])
      silent = (int)p;

  delivered = 0;
  while (qSize() > 0 && (!maxDeliver || delivered < maxDeliver)) {
    qPopHead(&w);
    if (down[w.to])
      continue;
    ++delivered;
    /* A-Cast class only, and never from the late starter itself: every
     * A-Cast INITIAL sent before it came up was dropped at its socket,
     * so one arriving afterward is necessarily a BPR re-send.  BA
     * round INITIALs are not -- a turn issues those fresh every round,
     * and counting them would blur the two sources. */
    if (wit && w.cls == BKR94ACS_CLS_ACAST
     && w.to == wit->late && w.from != wit->late) {
      if (w.type == BRACHA87_INITIAL)
        ++wit->initialsIn;
      else if (w.type == BRACHA87_ECHO)
        ++wit->echoesIn;
    }
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w), w.from,
                                w.value, out);
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, wireAnnot(&w), w.from,
                                 w.baValue, out);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, silent);
  }

  for (p = 0; p < 4; ++p) {
    if (down[p])
      continue;
    n = bkr94acsRetryStep(processes[p], &cursors[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, silent);
    spTick(&pol[p], cursors[p].sweeps, 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
        for (k = 0; k < n; ++k)
          if (out[k].act == BKR94ACS_ACT_BA_DECIDED
           || out[k].act == BKR94ACS_ACT_COMPLETE)
            ++pol[p].progress;
        observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, silent);
      }
    n = bkr94acsFanout(processes[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, silent);
  }
}

/* ------------------------------------------------------------------ */
/*  Section M driver -- the honest residue after COMPLETE.            */
/*                                                                    */
/*  One process is pinned as the LEAVER.  In the never-announcer mode  */
/*  (M1) it runs, echoes and readys like anyone else, and LEAVES at   */
/*  the first egress that would carry its own ACCEPTED annotation --  */
/*  that batch is dropped and it is never ticked again.  In the       */
/*  announced-then-silent mode (M2, leaveQuiescent) it leaves on its  */
/*  own quiescent 0 return: the survivors' masks fill, nothing ever   */
/*  re-arms, and a correct machine quiesces.                          */
/* ------------------------------------------------------------------ */

static void
mTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,unsigned int leaver
 ,unsigned int leaveQuiescent   /* 0: at its first announcing egress;
                                 * 1: at its own quiescent 0 return */
 ,unsigned int *gone
 ,unsigned int *deliveredOut
 ,unsigned int *zeroRetriesOut
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct wire w;
  unsigned int p, b, n, k, announces;

  while (qSize() > 0) {
    qPopHead(&w);
    if (*gone && w.to == leaver)
      continue;
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w), w.from,
                                w.value, out);
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, wireAnnot(&w), w.from,
                                 w.baValue, out);
    }
    if (n)
      ++pol[w.to].progress;
    if (w.to != leaver)
      ++*deliveredOut;
    announces = 0;
    if (w.to == leaver && !leaveQuiescent)
      for (k = 0; k < n; ++k)
        if (out[k].accepted)
          announces = 1;
    if (announces) {
      *gone = 1;
      continue;
    }
    observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
  }

  for (p = 0; p < 4; ++p) {
    if (*gone && p == leaver)
      continue;
    n = bkr94acsRetryStep(processes[p], &cursors[p], out);
    announces = 0;
    if (p == leaver && !leaveQuiescent)
      for (k = 0; k < n; ++k)
        if (out[k].accepted)
          announces = 1;
    if (p == leaver && leaveQuiescent && !n
     && bkr94acsFig1SentCount(processes[p]))
      announces = 1;              /* quiescent: everything announced */
    if (announces) {
      *gone = 1;
      continue;
    }
    if (!n && p != leaver && zeroRetriesOut)
      ++*zeroRetriesOut;
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    spTick(&pol[p], cursors[p].sweeps, 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
        for (k = 0; k < n; ++k)
          if (out[k].act == BKR94ACS_ACT_BA_DECIDED
           || out[k].act == BKR94ACS_ACT_COMPLETE)
            ++pol[p].progress;
        observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      }
    n = bkr94acsFanout(processes[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
  }
}

/* ------------------------------------------------------------------ */
/*  Section N driver -- the sustained-rate skew.                      */
/*                                                                    */
/*  Section F's delayed-A-Cast schedule, run with one process ticking  */
/*  once every k ticks while the rest tick every tick.  The delayed    */
/*  A-Cast's own direct egress is LOST, so the only thing that can     */
/*  carry it is that process's BPR re-send -- which arrives at ITS     */
/*  cursor rate, while the patience that would wait for it is counted  */
/*  in the FIRING process's own completed sweeps.  Those are two       */
/*  different clocks, and the lane is what happens when they run at    */
/*  different rates.                                                  */
/*                                                                    */
/*  Patience is in COMPLETED SWEEPS (the shared boundary), loop counts */
/*  in ticks, and the patience compare is >=.  Turns are drained at    */
/*  zero patience so only the fanout's pacing is the variable, the     */
/*  same isolation Section F takes.                                   */
/* ------------------------------------------------------------------ */

static int
nDrive(
  unsigned int slow          /* the k-slow process */
 ,unsigned int k             /* it ticks once every k ticks */
 ,unsigned int patience      /* in completed sweeps */
 ,unsigned int releaseTick   /* submission, in the delayed process's OWN ticks */
 ,unsigned int maxTicks
 ,unsigned int *ticksOut
 ,unsigned int *includedOut  /* delayed process in the agreed subset? */
 ,unsigned int *fanoutActsOut
){
  struct bkr94acs *processes[4];
  struct processObs obs[4];
  struct bracha87Retry cursors[4];
  struct sweepPolicy pol[4];
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct bkr94acsAct acastOut[1];
  struct wire w;
  unsigned char acast[4];
  unsigned char subset[4];
  unsigned int spent[4];
  unsigned int delayed = 3;
  unsigned int tick, p, b, n, j, sz, sweepDone, released, ownTicks;
  int done;

  *ticksOut = 0;
  *includedOut = 0;
  *fanoutActsOut = 0;
  if (allocCluster(processes, 4, 1, 0, 8))
    return (-1);
  qReset();
  for (p = 0; p < 4; ++p) {
    obsInit(&obs[p]);
    bracha87RetryInit(&cursors[p]);
    memset(&pol[p], 0, sizeof (pol[p]));
    spent[p] = 0;
    acast[p] = (unsigned char)(0xA0 + p);
  }
  for (p = 0; p < 3; ++p) {
    n = bkr94acsAcast(processes[p], &acast[p], acastOut);
    observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
  }

  released = 0;
  ownTicks = 0;
  done = 0;
  for (tick = 0; tick < maxTicks && !done; ++tick) {
    while (qSize() > 0) {
      qPopHead(&w);
      if (w.cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w), w.from,
                                  w.value, out);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, wireAnnot(&w), w.from,
                                   w.baValue, out);
      }
      if (n)
        ++pol[w.to].progress;
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
    }

    for (p = 0; p < 4; ++p) {
      if (p == slow && k > 1 && (tick % k))
        continue;

      /* The delayed A-Cast releases at the first tick its own process
       * observes step 2 leave HELD -- the knife edge -- and the
       * release itself is LOST.  From here only its own re-send can
       * carry it, at its own cursor rate. */
      if (p == delayed)
        ++ownTicks;
      if (p == delayed && !released && ownTicks > releaseTick) {
        released = 1;
        bkr94acsAcast(processes[delayed], &acast[delayed], acastOut);
      }

      n = bkr94acsRetryStep(processes[p], &cursors[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      sweepDone = spTick(&pol[p], cursors[p].sweeps, 0);

      for (b = 0; b < 4; ++b)
        while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
          for (j = 0; j < n; ++j)
            if (out[j].act == BKR94ACS_ACT_BA_DECIDED
             || out[j].act == BKR94ACS_ACT_COMPLETE)
              ++pol[p].progress;
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }

      if (bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_TOLERANCE) {
        if (sweepDone)
          ++spent[p];
      } else
        spent[p] = 0;
      if (spent[p] >= patience) {
        n = bkr94acsFanout(processes[p], out);
        *fanoutActsOut += n;
        observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      }
    }

    done = 1;
    for (p = 0; p < 4; ++p)
      if (!processes[p]->complete)
        done = 0;
  }
  *ticksOut = tick;
  if (done) {
    sz = bkr94acsSubset(processes[0], subset);
    for (j = 0; j < sz; ++j)
      if (subset[j] == (unsigned char)delayed)
        *includedOut = 1;
  }
  freeCluster(processes, 4);
  return (done ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/*  Section P plumbing -- the annotation forger.                      */
/*                                                                    */
/*  Every other adversary in this file lies with a PROTOCOL message:  */
/*  A5 forges an INITIAL's initiator, E1 equivocates its own A-Cast,  */
/*  K1 trickles fresh state advances, C7 says nothing at all.  None   */
/*  of them touches the two READY annotations -- every arm above      */
/*  derives those bits honestly from the library's own act output.    */
/*  The forger below is a full struct bkr94acs running the protocol   */
/*  correctly and lying ONLY in those two bits, which is the whole of */
/*  what an attacker holding an authenticated identity can do to them */
/*  (README, Message System assumptions 2 and 3: it chooses content,  */
/*  never its own 'from').                                            */
/*                                                                    */
/*  The lie is applied at DELIVERY, per receiver, over the existing   */
/*  queue -- Section O's shape rather than a coarse side[] cut -- so   */
/*  the wire the forger's own machine produced is still intact in w   */
/*  and the non-vacuity counters can say what the lie actually        */
/*  changed.  Self-delivery is never forged: a broadcast to self is a */
/*  local hand-back (README, Message System item 4), not a packet an  */
/*  attacker rewrites on its way anywhere.                            */
/*                                                                    */
/*  Every flag below is cleared on the way out of every arm.          */
/* ------------------------------------------------------------------ */

/* Instance keys at the sizes this section runs: n + n * (maxPhases *
 * BRACHA87_ROUNDS_PER_PHASE) * n, largest at n=7 maxPhases=2. */
#define P_INST_MAX 320

static int PForger = -1;                /* which process lies; -1 = nobody */
static unsigned int PForgeAccepted = 0; /* claim ACCEPTED it does not hold */
static unsigned int PForgeUnmarked = 0; /* strip the RECEIVED it does hold */
static unsigned char PCut = 0xFF;       /* process off the network; 0xFF none */
static unsigned int PRate = 1;          /* forger Retry calls per honest tick */

/* Non-vacuity: what the lie actually changed on the wire. */
static unsigned int PForgedEarly = 0;
static unsigned int PStripped = 0;

/* The announcement ledger.  PAnnounced[to][from][process] is set when an
 * A-Cast READY carrying the HONEST accepted bit reached 'to' -- read off
 * w before pAnnot forges anything, so it records true announcements
 * only, which is what makes the containment audit a real question. */
static unsigned char PAnnounced[MAX_PROCESSES][MAX_PROCESSES][MAX_PROCESSES];

/* READY egress accounting, armed once the honest processes have covered
 * each other.  The recipient set is read off the act's own skip mask,
 * so "aimed at" is a fact about the honest machine's egress and never
 * about what the harness chose to deliver. */
static unsigned int PAimArmed = 0;
static unsigned int PReadyEgress = 0;
static unsigned int PAimedAtForger = 0;
static unsigned int PReachingCorrect = 0;

/* The re-arming half of the adversary.  Suppression is per RECIPIENT and
 * the forger's own machine honors it, so a forger that lies and then
 * runs its retry honestly goes SILENT toward everyone who announced --
 * it has nothing left to send and nothing to strip.  Holding a gate open
 * therefore takes the other thing an authenticated attacker chooses:
 * MULTIPLICITY.  The forger keeps re-sending its own READY for every
 * instance it ever sent one for, unmarked, ignoring the mask its own
 * machine computed -- which is the "keeps re-arming" process
 * bracha87.h's retry banner prices.  Its content is its own authentic
 * READY, replayed; only the two annotation bits are chosen. */
static unsigned int PReplayOn = 0;
static struct wire PReplay[P_INST_MAX];
static unsigned char PReplayHas[P_INST_MAX];
static unsigned int PArmsSent = 0;

/* "Never strands a correct laggard", read off the egress rather than
 * inferred from the laggard finishing.  Stranding is an honest process
 * dropping the laggard from a READY recipient set for an A-Cast the
 * laggard does not hold, so the reading is that recipient set, taken at
 * every egress while the value is still missing there. */
static int PLagWatch = -1;
static unsigned int PLagSuppressed = 0;
static unsigned int PLagServed = 0;
static unsigned int PLagOther = 0;

static void
pReset(
  void
){
  PForger = -1;
  PForgeAccepted = 0;
  PForgeUnmarked = 0;
  PCut = 0xFF;
  PRate = 1;
  PForgedEarly = 0;
  PStripped = 0;
  PAimArmed = 0;
  PReadyEgress = 0;
  PAimedAtForger = 0;
  PReachingCorrect = 0;
  PReplayOn = 0;
  PArmsSent = 0;
  PLagWatch = -1;
  PLagSuppressed = 0;
  PLagServed = 0;
  PLagOther = 0;
  memset(PAnnounced, 0, sizeof (PAnnounced));
  memset(PReplayHas, 0, sizeof (PReplayHas));
}

/* The annot byte as the forger's victim sees it. */
static unsigned char
pAnnot(
  const struct wire *w
){
  unsigned char annot;

  annot = wireAnnot(w);
  if (PForger < 0 || (int)w->from != PForger || w->from == w->to
   || w->type != BRACHA87_READY)
    return (annot);
  if (PForgeAccepted && !(annot & BKR94ACS_ACCEPTED)) {
    ++PForgedEarly;
    annot |= BKR94ACS_ACCEPTED;
  }
  if (PForgeUnmarked) {
    if (annot & BKR94ACS_RECEIVED)
      ++PStripped;
    annot &= ~BKR94ACS_RECEIVED;
  }
  return (annot);
}

/* The Fig 1 instance a wire names, in the cursor's own linear space
 * (A-Casts first, then process x round x initiator) -- the walk order
 * bkr94acs.h documents at bkr94acsRetryStep. */
static unsigned int
pWireInst(
  const struct wire *w
 ,unsigned int nAct
 ,unsigned int mr
){
  if (w->cls == BKR94ACS_CLS_ACAST)
    return (w->process);
  return (nAct + ((unsigned int)w->process * mr + w->round) * nAct
          + w->initiator);
}

/* The containment audit.  bracha87Fig1Received is acFrom itself, and
 * acFrom has exactly one writer: the ACCEPTED annotation, which marks
 * the SENDER of the READY that carried it -- a process's own hand-back
 * included.  So a bit for q != self is the claim that q
 * announced -- 'violations' counts bits no announcement, true or
 * forged, accounts for, and 'unearned' counts the ones the forgery
 * bought.  Run every tick, so the property is a standing fact rather
 * than a reading at one instant.
 *
 * Scoped to the A-Cast instances: the BA class routes its annotations
 * through the same bracha87Fig1ProcessAccepted call on the same
 * argument, and a per-instance ledger over the BA space would cost more
 * than the second reading of one mechanism is worth. */
static void
pMaskAudit(
  struct bkr94acs **processes
 ,unsigned int nAct
 ,unsigned int *unearned
 ,unsigned int *violations
){
  const struct bracha87Fig1 *f1;
  const unsigned char *ans;
  unsigned int p, j, q;

  for (p = 0; p < nAct; ++p) {
    if ((int)p == PForger)
      continue;
    for (j = 0; j < nAct; ++j) {
      if (!(f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j))
       || !bracha87Fig1Value(f1))
        continue;
      if (!(ans = bracha87Fig1Received(f1)))
        continue;
      for (q = 0; q < nAct; ++q) {
        if (q == p || !BRACHA87_SKIP_TST(ans, q) || PAnnounced[p][q][j])
          continue;
        if ((int)q == PForger)
          ++*unearned;
        else
          ++*violations;
      }
    }
  }
}

/* One honest process's READY egress, priced.  The recipient set is the
 * one observeAndOutput would broadcast to -- the act's own suppress
 * mask, read the same way -- so "aimed at the forger alone" is read off
 * the machine's egress and not inferred from what arrives. */
static void
pAimCount(
  const struct bkr94acsAct *acts
 ,unsigned int n
 ,unsigned int nAct
){
  unsigned int k, q, alone, recipients;

  for (k = 0; k < n; ++k) {
    if (acts[k].type != BRACHA87_READY)
      continue;
    alone = 1;
    recipients = 0;
    for (q = 0; q < nAct; ++q) {
      if (acts[k].skip && BRACHA87_SKIP_TST(acts[k].skip, q))
        continue;
      ++recipients;
      if ((int)q != PForger)
        alone = 0;
    }
    if (!recipients)
      continue;
    ++PReadyEgress;
    if (alone)
      ++PAimedAtForger;
    else
      ++PReachingCorrect;
  }
}

static void
pTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,unsigned int nAct
 ,unsigned int mr
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(6)];  /* encoded n; 7 processes */
  struct wire w;
  unsigned int p, b, n, k, rep, inst;

  while (qSize() > 0) {
    qPopHead(&w);
    /* The cut takes a process off the network in both directions; its
     * own loopback still lands, since delivery to self is a local
     * hand-back and not something a network partition reaches. */
    if (PCut != 0xFF && w.from != w.to
     && (w.from == PCut || w.to == PCut))
      continue;
    if (PReplayOn && (int)w.from == PForger && w.from != w.to
     && w.type == BRACHA87_READY
     && (inst = pWireInst(&w, nAct, mr)) < P_INST_MAX) {
      PReplay[inst] = w;
      PReplayHas[inst] = 1;
    }
    if (w.cls == BKR94ACS_CLS_ACAST) {
      if (w.type == BRACHA87_READY && w.accepted)
        PAnnounced[w.to][w.from][w.process] = 1;
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                             pAnnot(&w), w.from, w.value, out);
    } else
      n = bkr94acsBaInput(processes[w.to], w.process, w.round, w.initiator,
                          w.type, pAnnot(&w), w.from, w.baValue, out);
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, nAct, out, n, 1, 0, -1);
  }

  /* The re-arming, PRate rounds of it per honest tick.  Delivered
   * straight, the way Section K delivers its trickler's wires: the
   * forger runs no queue discipline anyone can hold it to. */
  for (rep = 0; PReplayOn && rep < PRate; ++rep)
    for (inst = 0; inst < P_INST_MAX; ++inst) {
      if (!PReplayHas[inst])
        continue;
      w = PReplay[inst];
      for (p = 0; p < nAct; ++p) {
        const struct bracha87Fig1 *f1;
        const unsigned char *ans;

        if ((int)p == PForger)
          continue;
        w.to = (unsigned char)p;
        /* What the forger's OWN machine would put on the wire for THIS
         * recipient, read now rather than carried over from whoever the
         * captured copy was addressed to.  Only against that is the
         * stripped bit a lie about this recipient's accept rather than
         * a bit that was never there. */
        f1 = (w.cls == BKR94ACS_CLS_ACAST)
           ? bkr94acsAcastFig1(processes[PForger], w.process)
           : bkr94acsBaFig1(processes[PForger], w.process, w.round,
                            w.initiator);
        ans = f1 ? bracha87Fig1Received(f1) : 0;
        /* The mask records nothing before the instance is accepted, and
         * the sender's own bit enters it on its own hand-back carrying
         * ACCEPTED -- one pass after the honest egress first announces
         * (which reads the ACCEPTED flag, not the mask), so this replay
         * under-announces by that pass and never over-announces. */
        w.accepted = (ans && BRACHA87_SKIP_TST(ans, w.from)) ? 1 : 0;
        w.received = (ans && BRACHA87_SKIP_TST(ans, p)) ? 1 : 0;
        if (w.cls == BKR94ACS_CLS_ACAST)
          n = bkr94acsAcastInput(processes[p], w.process, w.type,
                                 pAnnot(&w), w.from, w.value, out);
        else
          n = bkr94acsBaInput(processes[p], w.process, w.round, w.initiator,
                              w.type, pAnnot(&w), w.from, w.baValue, out);
        ++PArmsSent;
        if (n)
          ++pol[p].progress;
        observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
      }
    }

  for (p = 0; p < nAct; ++p) {
    n = bkr94acsRetryStep(processes[p], &cursors[p], out);
    if (PAimArmed && (int)p != PForger)
      pAimCount(out, n, nAct);
    /* CORRECT senders only, and only while the laggard is reachable:
     * an egress counted while the cut stands is discarded at :pTick's
     * filter below, so counting it would credit the carrying with
     * messages that provably never landed.  The forger is excluded
     * because the question is what the HONEST cohort still sends. */
    if (PLagWatch >= 0 && PCut == 0xFF && (int)p != PLagWatch
     && (int)p != PForger)
      for (k = 0; k < n; ++k) {
        if (out[k].act != BKR94ACS_ACT_ACAST_SEND
         || bkr94acsAcastValue(processes[PLagWatch], out[k].process))
          continue;
        if (out[k].type != BRACHA87_READY) {
          /* What else is still going out for an instance the laggard
           * lacks.  A reading of the drain discipline as much as of
           * the retire gates: this harness runs the queue to fixpoint
           * before any retry egress, so an instance is accepted at its
           * sender -- INITIAL and ECHO retired -- before the first act
           * is ever counted here. */
          if (!out[k].skip || !BRACHA87_SKIP_TST(out[k].skip, PLagWatch))
            ++PLagOther;
          continue;
        }
        if (out[k].skip && BRACHA87_SKIP_TST(out[k].skip, PLagWatch))
          ++PLagSuppressed;
        else
          ++PLagServed;
      }
    observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
    spTick(&pol[p], cursors[p].sweeps, 0);
    for (b = 0; b < nAct; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
        for (k = 0; k < n; ++k)
          if (out[k].act == BKR94ACS_ACT_BA_DECIDED
           || out[k].act == BKR94ACS_ACT_COMPLETE)
            ++pol[p].progress;
        observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
      }
    n = bkr94acsFanout(processes[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, nAct, out, n, 1, 0, -1);
  }
}

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/*  Section R plumbing -- a coin that SPLITS: a per-process           */
/*  pseudo-random bit, deterministic per seed, counted through its    */
/*  closure so a lane can print whether the coin was reached.  The    */
/*  file's                                                            */
/*  testCoin is parity, the same at every process, which never        */
/*  exercises case (iii) across a cluster.                            */
/* ------------------------------------------------------------------ */

struct rCoin {
  unsigned int self;
  unsigned int seed;
  unsigned int calls;
  unsigned int instance;   /* of the last toss */
  unsigned int phase;      /* of the last toss */
};

static unsigned char
rCoinFn(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  struct rCoin *c = closure;
  unsigned int x;

  ++c->calls;
  c->instance = instance;
  c->phase = phase;
  x = c->seed * 2654435761u + c->self * 40503u + instance * 9973u + phase * 7919u;
  x ^= x >> 13;
  x *= 0x5bd1e995u;
  x ^= x >> 15;
  return (x & 1);
}

#define R_NEVER 0xFFFFu

/* What the driver reads off BA 3's turns, per process: the round the
 * deciding turn opened (the BA_SEND beside its BA_DECIDED; R_NEVER
 * until it decides), and how many turns FIRED and wrote nothing --
 * the header's one such turn, a decided BA's bound turn, which ends
 * its round space (bkr94acs.h, at bkr94acsTurn). */
static unsigned int RDecideRound[4];
static unsigned int RZeroTurns[4];

/* The turn drain of the Section R driver: drainTurns with the two
 * readings above taken on BA 3, and the header's promise asserted at
 * every fired turn that writes nothing -- the duty reads HELD from
 * then on. */
static void
rDrain(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,unsigned int p
 ,struct bkr94acsAct *out
){
  unsigned int b, n, k, duty;

  for (b = 0; b < 4; ++b)
    for (;;) {
      duty = bkr94acsTurnDuty(processes[p], (unsigned char)b);
      n = bkr94acsTurn(processes[p], (unsigned char)b, out);
      if (n == 0) {
        if (duty != BKR94ACS_DUTY_HELD) {
          if (b == 3)
            ++RZeroTurns[p];
          CHECK(bkr94acsTurnDuty(processes[p], (unsigned char)b) == BKR94ACS_DUTY_HELD,
                "R: a fired turn that writes nothing ends the BA's round space -- the duty reads HELD");
        }
        break;
      }
      (void)turnDrained();
      CHECK(n <= 3, "turn outputs at most 3 acts");
      if (b == 3) {
        unsigned int decided = 0, sent = R_NEVER;

        for (k = 0; k < n; ++k) {
          if (out[k].act == BKR94ACS_ACT_BA_DECIDED)
            decided = 1;
          if (out[k].act == BKR94ACS_ACT_BA_SEND)
            sent = out[k].round;
        }
        if (decided)
          RDecideRound[p] = sent;
      }
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    }
}

/* ------------------------------------------------------------------ */
/*  Section R driver.  n = 4, t = 1, BA 3 entered 0 by processes 0    */
/*  and 1 and 1 by processes 2 and 3 -- a two-two split that only the  */
/*  fanout's timing can produce: a Fig 1 accepted at one correct      */
/*  process is accepted at every other within a hop (Lemma 3), so     */
/*  entries differ only by which side of the fanout's firing each     */
/*  process's accept of A-Cast 3 lands on.  The schedule: A-Casts 0,  */
/*  1 and 2 go out and are drained with turns at every process but    */
/*  the fanout called at 0 and 1 ONLY, so those two enter 0 in BA 3   */
/*  once BAs 0-2 decide; then A-Cast 3 goes out, every process        */
/*  accepts it, and 2 and 3 -- whose fanout was never called -- enter */
/*  1.  From there the cluster ticks: drain the wire in a seeded      */
/*  random order with turns at enabling, one retry step, and the      */
/*  fanout at zero patience.  With delivery order random, the step-1  */
/*  and step-3 samples of BA 3 differ across processes, so some seeds */
/*  decide BA 3 in two waves and some may reach case (iii); the arms  */
/*  search seeds for the waves and print the tosses.                  */
/*                                                                    */
/*  The tick cap is a harness guard.  Returns the tick at which every */
/*  process had completed, or R_NEVER.                                 */
/* ------------------------------------------------------------------ */

static unsigned int
rDrive(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct rCoin *coins
 ,unsigned int seed
 ,unsigned int maxTicks
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
  struct bkr94acsAct acastOut[1];
  struct bracha87Retry cursors[4];
  struct wire stash[16];       /* BA 3's round-0 INITIALs from 0 and 1, to all */
  unsigned int nStash;
  struct wire w;
  unsigned char acast[4];
  unsigned int n, p, tick;
  int done;

  qReset();
  rngSeed(seed);
  nStash = 0;
  for (p = 0; p < 4; ++p) {
    bracha87RetryInit(&cursors[p]);
    coins[p].self = p;
    coins[p].seed = seed;
    coins[p].calls = 0;
    acast[p] = (unsigned char)(0xA0 + p);
    RDecideRound[p] = R_NEVER;
    RZeroTurns[p] = 0;
  }

  /* A-Casts 0-2, drained with turns at 0 and 1 only, then their fanout */
  for (p = 0; p < 3; ++p) {
    n = bkr94acsAcast(processes[p], &acast[p], acastOut);
    observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
  }
  for (tick = 0; tick < 64; ++tick) {
    while (qSize() > 0) {
      qPopHead(&w);
      /* BA 3's round-0 INITIALs from the fanout are held aside: they
       * join the randomized queue with 2's and 3's, so a process's
       * first n-t of the four is any three -- the split. */
      if (w.cls == BKR94ACS_CLS_BA && w.process == 3 && w.round == 0
       && w.type == BRACHA87_INITIAL && nStash < 16) {
        stash[nStash++] = w;
        continue;
      }
      if (w.cls == BKR94ACS_CLS_ACAST)
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w),
                               w.from, w.value, out);
      else
        n = bkr94acsBaInput(processes[w.to], w.process, w.round, w.initiator,
                            w.type, wireAnnot(&w), w.from, w.baValue, out);
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
    }
    for (p = 0; p < 4; ++p) {
      rDrain(processes, obs, p, out);
      if (p < 2) {
        n = bkr94acsFanout(processes[p], out);
        observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      }
    }
    if (bkr94acsBaEntered(processes[0], 3) && bkr94acsBaEntered(processes[1], 3)
     && qSize() == 0)
      break;
  }
  CHECK(bkr94acsBaEntered(processes[0], 3) && bkr94acsBaEntered(processes[1], 3),
        "R: processes 0 and 1 entered BA 3 by the fanout");
  CHECK(!bkr94acsBaEntered(processes[2], 3) && !bkr94acsBaEntered(processes[3], 3),
        "R: processes 2 and 3 have not entered BA 3");

  /* A-Cast 3: everyone accepts it; 2 and 3 enter 1 */
  n = bkr94acsAcast(processes[3], &acast[3], acastOut);
  observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, 0, -1);
  while (qSize() > 0) {
    qPopHead(&w);
    if (w.cls == BKR94ACS_CLS_BA && w.process == 3 && w.round == 0
     && w.type == BRACHA87_INITIAL && nStash < 16) {
      stash[nStash++] = w;
      continue;
    }
    if (w.cls == BKR94ACS_CLS_ACAST)
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w),
                             w.from, w.value, out);
    else
      n = bkr94acsBaInput(processes[w.to], w.process, w.round, w.initiator,
                          w.type, wireAnnot(&w), w.from, w.baValue, out);
    observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
  }
  CHECK(bkr94acsBaEntered(processes[2], 3) && bkr94acsBaEntered(processes[3], 3),
        "R: processes 2 and 3 entered BA 3 by step 1");
  CHECK(obs[0].selfInputValue[3] == 0 && obs[1].selfInputValue[3] == 0
     && obs[2].selfInputValue[3] == 1 && obs[3].selfInputValue[3] == 1,
        "R: BA 3's inputs are split two-two");

  CHECK(nStash == 16, "R: the sixteen round-0 INITIALs of BA 3 were held aside");
  for (p = 0; p < nStash; ++p)
    qPush(&stash[p]);

  /* the cluster ticks */
  for (tick = 0; tick < maxTicks; ++tick) {
    while (qSize() > 0) {
      qPopRandom(&w);
      if (w.cls == BKR94ACS_CLS_ACAST)
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type, wireAnnot(&w),
                               w.from, w.value, out);
      else
        n = bkr94acsBaInput(processes[w.to], w.process, w.round, w.initiator,
                            w.type, wireAnnot(&w), w.from, w.baValue, out);
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
      /* turns at enabling, so a round's sample is the first n-t
       * validated in this delivery order and differs across
       * processes: the split that can reach the coin */
      rDrain(processes, obs, w.to, out);
    }
    for (p = 0; p < 4; ++p) {
      n = bkr94acsRetryStep(processes[p], &cursors[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      rDrain(processes, obs, p, out);
      n = bkr94acsFanout(processes[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    }
    done = 1;
    for (p = 0; p < 4; ++p)
      if (!processes[p]->complete)
        done = 0;
    if (done)
      return (tick);
  }
  return (R_NEVER);
}

int
main(
  int argc
 ,char **argv
){
  struct bkr94acs *processes[MAX_PROCESSES];
  struct processObs obs[MAX_PROCESSES];
  unsigned char acasts[MAX_PROCESSES * MAX_VLEN];
  unsigned int i;

  (void)argc;
  (void)argv;

  rngSeed(0xC0FFEE);

  /* ---------------------------------------------------------------- */
  /*  Section A -- API edges                                          */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("A1: Sz/Init contract on a fresh process");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    unsigned char buf[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned int j;

    sz = bkr94acsSz(3, 0, 10);
    CHECK(sz > 0, "Sz returns nonzero");

    a = calloc(1, sz);
    CHECK(a != 0, "alloc cluster");
    if (!a) goto a1_done;

    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK(a->complete == 0,
          "fresh: complete clear");
    CHECK(bkr94acsFig1SentCount(a) == 0,
          "fresh: SentFig1Count == 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsBaDecision(a, (unsigned char)j) == 0xFF,
            "fresh: BaDecision == 0xFF (undecided)");
    CHECK(bkr94acsSubset(a, buf) == 0, "fresh: Subset returns 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsAcastValue(a, (unsigned char)j) == 0,
            "fresh: AcastValue == 0");

    /* No evidence banked, so no BA has a complete round: every duty
     * reads HELD and an unconditional turn outputs nothing. */
    for (j = 0; j < 4; ++j) {
      CHECK(bkr94acsTurnDuty(a, (unsigned char)j) == BKR94ACS_DUTY_HELD,
            "fresh: TurnDuty == HELD");
      CHECK(bkr94acsTurn(a, (unsigned char)j, out) == 0,
            "fresh: Turn at HELD outputs nothing");
    }
    CHECK(bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_HELD,
          "fresh: FanoutDuty == HELD");

    free(a);
  }
  a1_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("A1b: Sz refuses a configuration that cannot be built");
  /* ---------------------------------------------------------------- */
  /* Header: "or 0 if the configuration cannot be built ... n and     */
  /* vLen are refused ... they are WIDER here than bkr94acsInit's     */
  /* unsigned char, so Sz is the only entry that can see one out of   */
  /* range.  maxPhases outside 1..BRACHA87_MAX_PHASES is refused ...  */
  /* bkr94acsInit refuses the same values by returning 0."            */
  /*                                                                  */
  /* Each pair brackets a boundary: the last value Init can carry,    */
  /* then the first it cannot.                                        */
  /* ---------------------------------------------------------------- */
  {
    CHECK(bkr94acsSz(255, 0, 1) != 0, "Sz takes n 255");
    CHECK(bkr94acsSz(256, 0, 1) == 0, "Sz refuses n 256");
    CHECK(bkr94acsSz(3, 255, 1) != 0, "Sz takes vLen 255");
    CHECK(bkr94acsSz(3, 256, 1) == 0, "Sz refuses vLen 256");
    CHECK(bkr94acsSz(3, 0, BRACHA87_MAX_PHASES) != 0,
          "Sz takes maxPhases at the ceiling");
    CHECK(bkr94acsSz(3, 0, BRACHA87_MAX_PHASES + 1) == 0,
          "Sz refuses maxPhases past the ceiling");
    CHECK(bkr94acsSz(3, 0, 0) == 0, "Sz refuses maxPhases 0");

    /* An Init the size call refused must leave the caller's memory
     * alone: the refusal is the whole contract, and a partly-built
     * machine behind a 0 size would be worse than none.
     *
     * Probed at maxPhases 0 only, and the buffer is why.  A refused
     * Init writes nothing, so any buffer would do for the passing
     * case -- but the check has to SURVIVE a regressed Init to report
     * one, and a regressed Init lays out mr = maxPhases * 3 rounds of
     * BA Fig1 while the memset above it copies bkr94acsSz's refusing
     * 0.  At maxPhases 0 that layout is mr = 0 and fits inside a
     * buffer sized at the ceiling; at maxPhases 85 + 1 it is three
     * rounds LARGER than that ceiling and would run off the end
     * before the check could read intact.  Sz's refusal of the
     * over-ceiling value is tested above; this arm covers the half
     * a buffer can hold. */
    {
      unsigned long guard;
      unsigned char *probe;
      unsigned long j;
      unsigned int intact;

      guard = bkr94acsSz(3, 0, BRACHA87_MAX_PHASES);
      CHECK(guard > 0, "probe guard size available");
      if ((probe = malloc(guard)) != 0) {
        memset(probe, 0xAA, guard);
        CHECK(bkr94acsInit((struct bkr94acs *)probe, 3, 1, 0, 0, 0,
                           testCoin, 0) == 0,
              "Init returns 0 when maxPhases is refused");
        CHECK(bkr94acsInit((struct bkr94acs *)probe, 2, 1, 0, 4, 0,
                           testCoin, 0) == 0,
              "Init refuses N = 3 with t = 1 (N == 3t)");
        CHECK(bkr94acsInit(0, 3, 1, 0, 4, 0, testCoin, 0) == 0,
              "Init refuses a null instance");
        CHECK(bkr94acsInit((struct bkr94acs *)probe, 3, 1, 0, 4, 9,
                           testCoin, 0) == 0,
              "Init refuses self outside 0..n");
        intact = 1;
        for (j = 0; j < guard; ++j)
          if (probe[j] != 0xAA)
            intact = 0;
        CHECK(intact, "Init writes nothing when maxPhases is refused");
        free(probe);
      }
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("A2: A-Cast contract and AcastValue round-trip");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[1];
    unsigned char v1[1];
    unsigned char v2[1];
    const unsigned char *pv;
    unsigned int n;

    sz = bkr94acsSz(3, 0, 10);
    a = calloc(1, sz);
    if (!a) goto a2_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    v1[0] = 0xAB;
    n = bkr94acsAcast(a, v1, out);
    CHECK(n == 1, "A-Cast: 1 act");
    if (n == 1) {
      CHECK(out[0].act == BKR94ACS_ACT_ACAST_SEND, "A-Cast: ACAST_SEND");
      CHECK(out[0].process == 0, "A-Cast: process == self (0)");
      CHECK(out[0].type == BRACHA87_INITIAL, "A-Cast: type == INITIAL");
      CHECK(out[0].value != 0, "A-Cast: value pointer non-null");
      if (out[0].value)
        CHECK(out[0].value[0] == 0xAB, "A-Cast: value bytes match");
    }

    pv = bkr94acsAcastValue(a, 0);
    CHECK(pv != 0, "AcastValue(self) != 0 after A-Cast");
    if (pv)
      CHECK(pv[0] == 0xAB, "AcastValue(self) bytes round-trip");

    /* Idempotency: re-A-Cast overwrites stored value, still outputs 1 act. */
    v2[0] = 0xCD;
    n = bkr94acsAcast(a, v2, out);
    CHECK(n == 1, "Re-A-Cast: 1 act");
    pv = bkr94acsAcastValue(a, 0);
    if (pv)
      CHECK(pv[0] == 0xCD, "Re-A-Cast: AcastValue updated");

    free(a);
  }
  a2_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("A4: Defensive nulls and out-of-range process");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    unsigned char dv[1];
    struct bkr94acsAct dout[3];  /* A-Cast wants 1, Turn wants 3 */
    struct bkr94acsAct fout[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct rout[BKR94ACS_RETRY_MAX_ACTS];
    struct bracha87Retry cur;
    unsigned char procs[4];

    dv[0] = 0;

    CHECK(bkr94acsBaDecision(0, 0) == 0xFF, "BaDecision(NULL): 0xFF");
    CHECK(bkr94acsFig1SentCount(0) == 0,
          "SentFig1Count(NULL): 0");
    CHECK(bkr94acsAcast(0, dv, dout) == 0, "A-Cast(NULL a): 0");
    /* Per .h TurnDuty reads HELD on bad args ("no turnable round"),
     * and Turn is safe to call unconditionally. */
    CHECK(bkr94acsTurnDuty(0, 0) == BKR94ACS_DUTY_HELD,
          "TurnDuty(NULL): HELD");
    CHECK(bkr94acsTurn(0, 0, dout) == 0, "Turn(NULL a): 0");

    sz = bkr94acsSz(3, 0, 10);
    a = calloc(1, sz);
    if (!a) goto a4_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK(bkr94acsBaDecision(a, 4) == 0xFF,
          "BaDecision(process == n): 0xFF");
    CHECK(bkr94acsBaDecision(a, 255) == 0xFF,
          "BaDecision(process 255): 0xFF");
    CHECK(bkr94acsTurnDuty(a, 4) == BKR94ACS_DUTY_HELD,
          "TurnDuty(process == n): HELD");
    CHECK(bkr94acsTurnDuty(a, 255) == BKR94ACS_DUTY_HELD,
          "TurnDuty(process 255): HELD");
    CHECK(bkr94acsTurn(a, 4, dout) == 0, "Turn(process == n): 0");
    CHECK(bkr94acsTurn(a, 255, dout) == 0, "Turn(process 255): 0");

    CHECK(bkr94acsSubset(0, procs) == 0, "Subset(NULL): 0");
    CHECK(bkr94acsAcastValue(0, 0) == 0, "AcastValue(NULL): null");
    CHECK(bkr94acsAcastValue(a, 4) == 0 && bkr94acsAcastValue(a, 255) == 0,
          "AcastValue(process out of range): null");
    CHECK(bkr94acsRetryStep(0, &cur, rout) == 0, "RetryStep(NULL a): 0");
    CHECK(bkr94acsFanoutDuty(0) == BKR94ACS_DUTY_HELD, "FanoutDuty(NULL): HELD");
    CHECK(bkr94acsFanout(0, fout) == 0, "Fanout(NULL a): 0");
    CHECK(bkr94acsFanout(a, 0) == 0, "Fanout(NULL out): 0");
    CHECK(bkr94acsBaFig1(a, 4, 0, 0) == 0 && bkr94acsBaFig1(a, 0, 255, 0) == 0
       && bkr94acsBaFig1(a, 0, 0, 4) == 0,
          "BaFig1(process, round or initiator out of range): null");

    free(a);
  }
  a4_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("A5: forged INITIAL rejection (Note 14)");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned char v[1];
    unsigned int n;

    /*
     * Contract: an INITIAL is the designated initiator's message.
     * bkr94acsAcastInput requires from == process; bkr94acsBa-
     * Input requires from == initiator.  A mismatched INITIAL is a
     * forged broadcast and must be dropped (0 actions).  ECHO/READY
     * from any sender remain valid.  n=4, t=1, self=0.
     */
    sz = bkr94acsSz(3, 0, 10);
    a = calloc(1, sz);
    if (!a) goto a5_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    v[0] = 0x42;

    /* A-Cast INITIAL with from != process: dropped. */
    n = bkr94acsAcastInput(a, /*process=*/1, BRACHA87_INITIAL,
                              ANNOT_NO_ARM,
                              /*from=*/2, v, out);
    CHECK(n == 0, "forged A-Cast INITIAL (from != process): 0 acts");
    CHECK(bkr94acsAcastValue(a, 1) == 0,
          "forged A-Cast INITIAL: process 1 stays unaccepted");

    /* A-Cast INITIAL with from == process: echoes (1 act). */
    n = bkr94acsAcastInput(a, /*process=*/1, BRACHA87_INITIAL,
                              ANNOT_NO_ARM,
                              /*from=*/1, v, out);
    CHECK(n == 1 && out[0].act == BKR94ACS_ACT_ACAST_SEND
                 && out[0].type == BRACHA87_ECHO,
          "honest A-Cast INITIAL (from == process): ACAST_SEND/ECHO");

    /* An ECHO from a non-process sender is legitimate (sender-deduped),
     * NOT subject to the INITIAL rule. */
    n = bkr94acsAcastInput(a, /*process=*/1, BRACHA87_ECHO,
                              ANNOT_NO_ARM,
                              /*from=*/3, v, out);
    CHECK(n <= 1, "non-process ECHO accepted (not dropped as forged)");

    /* BA INITIAL with from != initiator: dropped. */
    n = bkr94acsBaInput(a, /*process=*/1, /*round=*/0,
                               /*initiator=*/2, BRACHA87_INITIAL,
                               ANNOT_NO_ARM,
                               /*from=*/3, /*value=*/1, out);
    CHECK(n == 0, "forged BA INITIAL (from != initiator): 0 acts");

    /* BA INITIAL with from == initiator: echoes. */
    n = bkr94acsBaInput(a, /*process=*/1, /*round=*/0,
                               /*initiator=*/2, BRACHA87_INITIAL,
                               ANNOT_NO_ARM,
                               /*from=*/2, /*value=*/1, out);
    CHECK(n == 1 && out[0].act == BKR94ACS_ACT_BA_SEND
                 && out[0].type == BRACHA87_ECHO,
          "honest BA INITIAL (from == initiator): BA_SEND/ECHO");

    free(a);
  }
  a5_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section B -- Lemma 2 Parts A/B/C/D + paper-direct invariants    */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("B1: Lemma 2 Parts A/B/C/D -- n=4 t=1, ordered delivery");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)('A' + i);

      runHonest(n, vLen, acasts, 0 /*ordered*/, processes, obs);
      assertLemma2(processes, obs, n, t);

      /* Lemma 2 Part D -- explicit value-match check (the implementation
       * of Q(j) = "Fig1 ACCEPTED" also implies the accepted bytes
       * equal what j A-Cast). */
      {
        unsigned char subset[MAX_PROCESSES];
        unsigned int sz, j, p;
        sz = bkr94acsSubset(processes[0], subset);
        for (j = 0; j < sz; ++j) {
          unsigned int oj = subset[j];
          for (p = 0; p < n; ++p) {
            const unsigned char *v = bkr94acsAcastValue(processes[p],
                                       (unsigned char)oj);
            CHECK(v != 0 && v[0] == (unsigned char)('A' + oj),
                  "Part D: accepted value matches A-Cast");
          }
        }
      }

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B2: Lemma 2 -- n=4 t=1, shuffled delivery");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)('a' + i);

      runHonest(n, vLen, acasts, 1 /*shuffled*/, processes, obs);
      assertLemma2(processes, obs, n, t);

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B3: Lemma 2 -- n=7 t=2, shuffled delivery");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 7, t = 2, vLen = 1, mp = 10;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)(0x10 + i);

      runHonest(n, vLen, acasts, 1 /*shuffled*/, processes, obs);
      assertLemma2(processes, obs, n, t);

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B4: Lemma 2 -- identical A-Casts (degenerate values)");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = 0x42;  /* every process A-Casts the same byte */

      runHonest(n, vLen, acasts, 1, processes, obs);
      assertLemma2(processes, obs, n, t);

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B5: Lemma 2 -- multi-byte values (vLen=8)");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 8, mp = 10;
    unsigned int j;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        for (j = 0; j < vLen; ++j)
          acasts[i * vLen + j] = (unsigned char)((i << 4) | (j & 0x0F));

      runHonest(n, vLen, acasts, 1, processes, obs);
      assertLemma2(processes, obs, n, t);

      /* Multi-byte value-match check. */
      {
        unsigned char subset[MAX_PROCESSES];
        unsigned int sz, p, q;
        sz = bkr94acsSubset(processes[0], subset);
        for (j = 0; j < sz; ++j) {
          unsigned int oj = subset[j];
          for (p = 0; p < n; ++p) {
            const unsigned char *v = bkr94acsAcastValue(processes[p],
                                       (unsigned char)oj);
            CHECK(v != 0, "multi-byte: AcastValue non-null");
            if (v) {
              for (q = 0; q < vLen; ++q)
                CHECK(v[q] == (unsigned char)((oj << 4) | (q & 0x0F)),
                      "multi-byte: AcastValue bytes round-trip");
            }
          }
        }
      }

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B6: Step-2 trigger uses BA-decision count, not Fig1-ACCEPT");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Paper Part A Case (i): step 2 fires iff "2t+1 BAs have already
     * terminated with output 1".  In the n=3t+1 regime that's n-t.
     * A derived-presentation optimization uses Fig1-ACCEPT count instead;
     * BKR94ACS.txt and bkr94acs.h's own commentary flag this as a
     * deviation (only the decide-1 trigger satisfies Part A case (i)
     * of the BKR94 Lemma 2 proof").
     *
     * Construction (n=4 t=1, single process P0): deliver a complete
     * A-Cast-message cascade for processes 0/1/2 (ACAST_SEND traffic
     * from process 0 is the cascade roots; ECHO/READY for those Fig1s
     * is delivered to process 0 from itself + processes 1/2/3 by direct
     * AcastInput synthesis).  Deliver NOTHING for process 3's Fig1
     * and NO BA-class messages at all.
     *
     * After P0 ACCEPTs Fig1 for 0, 1, 2:
     *   P0 has output BA_SEND/INITIAL/baValue=1/process={0,1,2}
     *     (step-1 inputs, expected).
     *   P0 has decided ZERO BAs (no BA traffic delivered).
     *   Step-2 trigger condition is therefore unmet.
     *
     * Black-box assertion: P0 has NOT output any
     *   BA_SEND/INITIAL/baValue=0/process=3
     * (the enter-0 fanout that step 2 would produce).  A buggy
     * implementation that triggered on n-t Fig1-ACCEPTs would have.
     */
    unsigned int nAct = 4, t = 1, vLen = 1, mp = 10;
    unsigned long sz;
    struct bkr94acs *p0;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned char val0;
    unsigned int o, src, k;
    unsigned int countProcess0 = 0;
    unsigned int countProcess1 = 0;
    unsigned int countProcess2 = 0;
    unsigned int prematureFanout = 0;

    (void)t;
    sz = bkr94acsSz(nAct - 1, vLen - 1, mp);
    p0 = calloc(1, sz);
    if (!p0) goto b6_done;
    bkr94acsInit(p0, (unsigned char)(nAct - 1), 1, (unsigned char)(vLen - 1),
                 (unsigned char)mp, 0, testCoin, 0);

    /* Process 0 -- process 0 acasts, then synthesizes the all-honest
     * cascade locally (INITIAL from process 0; ECHO from 0/1/2/3;
     * READY from 0/1/2/3 once each process's threshold trips).  Since
     * we're driving only P0, we synthesize these as direct
     * AcastInput calls with the relevant 'from' field.  No wire
     * queue used in this banner. */
    val0 = 0x33;
    {
      struct bkr94acsAct acastOut[1];
      unsigned int n;
      n = bkr94acsAcast(p0, &val0, acastOut);
      CHECK(n == 1, "B6: A-Cast process 0 outputs 1 act");
    }

    /* For each of processes 0, 1, 2: deliver INITIAL from process's
     * A-Caster, then ECHO from all four senders, then READY from all
     * four senders.  This drives Fig1 at P0 to ACCEPT for those
     * processes.  Track BA_SEND outputs per process to confirm the
     * step-1 input, and to confirm no premature step-2 fanout to
     * process 3. */
    for (o = 0; o < 3; ++o) {
      unsigned char ov = (unsigned char)(0x30 + o);
      unsigned int n;

      /* INITIAL from the process itself (loopback for o=0; "remote"
       * for o=1, 2). */
      n = bkr94acsAcastInput(p0, (unsigned char)o, BRACHA87_INITIAL,
                                ANNOT_NO_ARM,
                                (unsigned char)o, &ov, out);
      for (k = 0; k < n; ++k) {
        if (out[k].act == BKR94ACS_ACT_BA_SEND
         && out[k].initiator == 0
         && out[k].type == BRACHA87_INITIAL) {
          if (out[k].process == 0) ++countProcess0;
          else if (out[k].process == 1) ++countProcess1;
          else if (out[k].process == 2) ++countProcess2;
          else if (out[k].process == 3 && out[k].baValue == 0)
            ++prematureFanout;
        }
      }

      /* ECHO from each of 0..3. */
      for (src = 0; src < nAct; ++src) {
        n = bkr94acsAcastInput(p0, (unsigned char)o, BRACHA87_ECHO,
                                  ANNOT_NO_ARM,
                                  (unsigned char)src, &ov, out);
        for (k = 0; k < n; ++k) {
          if (out[k].act == BKR94ACS_ACT_BA_SEND
           && out[k].initiator == 0
           && out[k].type == BRACHA87_INITIAL) {
            if (out[k].process == 0) ++countProcess0;
            else if (out[k].process == 1) ++countProcess1;
            else if (out[k].process == 2) ++countProcess2;
            else if (out[k].process == 3 && out[k].baValue == 0)
              ++prematureFanout;
          }
        }
      }

      /* READY from each of 0..3. */
      for (src = 0; src < nAct; ++src) {
        n = bkr94acsAcastInput(p0, (unsigned char)o, BRACHA87_READY,
                                  ANNOT_NO_ARM,
                                  (unsigned char)src, &ov, out);
        for (k = 0; k < n; ++k) {
          if (out[k].act == BKR94ACS_ACT_BA_SEND
           && out[k].initiator == 0
           && out[k].type == BRACHA87_INITIAL) {
            if (out[k].process == 0) ++countProcess0;
            else if (out[k].process == 1) ++countProcess1;
            else if (out[k].process == 2) ++countProcess2;
            else if (out[k].process == 3 && out[k].baValue == 0)
              ++prematureFanout;
          }
        }
      }
    }

    /* Step-1 inputs for processes 0/1/2 must have fired exactly once each. */
    CHECK(countProcess0 == 1, "B6: step-1 input for process 0 fired exactly once");
    CHECK(countProcess1 == 1, "B6: step-1 input for process 1 fired exactly once");
    CHECK(countProcess2 == 1, "B6: step-1 input for process 2 fired exactly once");

    /* No BA has decided yet -- no BA traffic delivered. */
    CHECK(bkr94acsBaDecision(p0, 0) == 0xFF,
          "B6: BA_0 still undecided (no BA delivered)");
    CHECK(bkr94acsBaDecision(p0, 1) == 0xFF, "B6: BA_1 undecided");
    CHECK(bkr94acsBaDecision(p0, 2) == 0xFF, "B6: BA_2 undecided");
    CHECK(bkr94acsBaDecision(p0, 3) == 0xFF, "B6: BA_3 undecided");

    /* Step-2 trigger MUST NOT have fired -- Fig1-ACCEPT count is now
     * 3 (= n-t) but BA-decision-with-output-1 count is 0. */
    CHECK(prematureFanout == 0,
          "B6: NO premature step-2 fanout on Fig1-ACCEPT count "
          "(BKR94 Part A Case (i) regression)");

    /* The all-echoed gate, read here through the COMPOSITION --
     * bkr94acsAcastFig1 for the instance, then the Fig 1 accessor
     * (bracha87.h: exposed so a checker can read the INITIAL retire's
     * gate rather than infer it).
     *
     * Processes 0/1/2 each received an ECHO from all n processes
     * before any READY, so the bit latched at n before ACCEPT and
     * holds; process 3 received nothing.  The composition inherits the
     * null / out-of-range guard: bkr94acsAcastFig1 answers 0 for a bad
     * argument and bracha87Fig1AllEchoed answers 0 for a null Fig 1. */
    CHECK(bracha87Fig1AllEchoed(bkr94acsAcastFig1(p0, 0)) == 1,
          "B6: AllEchoed 1 for fully-echoed process 0 (latched across accept)");
    CHECK(bracha87Fig1AllEchoed(bkr94acsAcastFig1(p0, 1)) == 1,
          "B6: AllEchoed 1 for process 1");
    CHECK(bracha87Fig1AllEchoed(bkr94acsAcastFig1(p0, 2)) == 1,
          "B6: AllEchoed 1 for process 2");
    CHECK(bracha87Fig1AllEchoed(bkr94acsAcastFig1(p0, 3)) == 0,
          "B6: AllEchoed 0 for un-echoed process 3");
    CHECK(bkr94acsAcastFig1(0, 0) == 0, "B6: AcastFig1 NULL -> 0");
    CHECK(bkr94acsAcastFig1(p0, 200) == 0,
          "B6: AcastFig1 out-of-range process -> 0");
    CHECK(bracha87Fig1AllEchoed(bkr94acsAcastFig1(p0, 200)) == 0,
          "B6: AllEchoed out-of-range process -> 0 through the composition");

    /* The INITIAL skip mask is the per-process refinement of the same
     * gate: the A-Cast's echoed-process bitmap.  Process 0 (fully
     * echoed) -> every bit set (all processes suppressed, == all-echoed);
     * process 3 (no echoes) -> empty mask (nobody suppressed). */
    {
      const unsigned char *sk0;
      const unsigned char *sk3;

      sk0 = bracha87Fig1Skip(bkr94acsAcastFig1(p0, 0),
                             BRACHA87_INITIAL_ALL);
      sk3 = bracha87Fig1Skip(bkr94acsAcastFig1(p0, 3),
                             BRACHA87_INITIAL_ALL);
      CHECK(sk0 != 0, "B6: INITIAL skip non-null for valid process 0");
      CHECK(sk0 && BRACHA87_SKIP_TST(sk0, 0) && BRACHA87_SKIP_TST(sk0, 1)
            && BRACHA87_SKIP_TST(sk0, 2) && BRACHA87_SKIP_TST(sk0, 3),
            "B6: INITIAL skip all bits set for fully-echoed process 0");
      CHECK(sk3 && !BRACHA87_SKIP_TST(sk3, 0) && !BRACHA87_SKIP_TST(sk3, 1),
            "B6: INITIAL skip empty for un-echoed process 3");
      CHECK(bracha87Fig1Skip(bkr94acsAcastFig1(0, 0),
                             BRACHA87_INITIAL_ALL) == 0,
            "B6: INITIAL skip NULL -> 0");
      CHECK(bracha87Fig1Skip(bkr94acsAcastFig1(p0, 200),
                             BRACHA87_INITIAL_ALL) == 0,
            "B6: INITIAL skip out-of-range process -> 0");

      /* The two purpose-named accessors are the side channel's own
       * spelling of the READIED set -- bkr94acsAcastAllReadied for the
       * all-or-nothing stop, bkr94acsAcastReadied for its per-process
       * refinement -- and per bkr94acs.h at bkr94acsAcastReadied the
       * set IS "the A-Cast Fig 1's ECHO_ALL suppress mask,
       * bracha87Fig1Skip(bkr94acsAcastFig1(a, process),
       * BRACHA87_ECHO_ALL), the same bitmap", so they must agree with
       * the composition at every argument, in range and out.
       * Processes 0/1/2 took a READY from all n; process 3 took
       * nothing. */
      {
        unsigned int q;
        unsigned int r;
        unsigned int cnt;
        const unsigned char *rd;

        for (q = 0; q < 4; ++q) {
          rd = bracha87Fig1Skip(bkr94acsAcastFig1(p0, (unsigned char)q),
                                BRACHA87_ECHO_ALL);
          CHECK(bkr94acsAcastReadied(p0, (unsigned char)q) == rd,
                "B6: AcastReadied is the A-Cast's ECHO_ALL suppress mask");
          cnt = 0;
          for (r = 0; rd && r < 4; ++r)
            if (BRACHA87_SKIP_TST(rd, r))
              ++cnt;
          CHECK(bkr94acsAcastAllReadied(p0, (unsigned char)q) == (cnt == 4),
                "B6: AcastAllReadied is that mask covering all n");
          CHECK(bkr94acsAcastAllReadied(p0, (unsigned char)q) == (q < 3),
                "B6: AcastAllReadied 1 for the fully-readied processes only");
        }
        CHECK(bkr94acsAcastAllReadied(0, 0) == 0,
              "B6: AcastAllReadied NULL -> 0");
        CHECK(bkr94acsAcastAllReadied(p0, 200) == 0,
              "B6: AcastAllReadied out-of-range -> 0");
        CHECK(bkr94acsAcastReadied(0, 0) == 0, "B6: AcastReadied NULL -> 0");
        CHECK(bkr94acsAcastReadied(p0, 200) == 0,
              "B6: AcastReadied out-of-range -> 0");
      }
    }

    free(p0);
  }
  b6_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("B7: Honest exclusion is allowed (BKR94ACS.txt remark)");
  /* ---------------------------------------------------------------- */
  {
    /*
     * From BKR94ACS.txt: "SubSet need not contain every honest
     * player: an honest P_h whose "Q(h)=1" evidence reaches fewer
     * than n-t honest players before step 2 fires for them may be
     * excluded."  Honest exclusion is a feature of the asynchronous
     * model rather than a defect -- Section 2's own words are that
     * "the missing inputs are not necessarily of the faulty players".
     *
     * This banner does NOT try to engineer exclusion (which depends
     * on adversarial scheduling that the simple wire-queue
     * simulator can't reliably produce).  Instead it documents the
     * contract: in the all-honest no-loss runs above, |SubSet|
     * happens to equal n every time, but the suite must NOT assert
     * that.  The Part A check (|SubSet| >= n-t) is the only
     * contractual lower bound.  Run a small cluster and confirm the
     * weaker bound holds even though the stronger one might.
     */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      unsigned char subset[MAX_PROCESSES];
      unsigned int sz;

      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)i;

      runHonest(n, vLen, acasts, 1, processes, obs);
      sz = bkr94acsSubset(processes[0], subset);
      CHECK(sz >= n - t, "B7: |SubSet| >= n-t (lower bound is contractual)");
      CHECK(sz <= n, "B7: |SubSet| <= n (upper bound is structural)");
      /* No assertion that sz == n -- that would over-specify. */

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B8: A-Cast ACCEPT before the last ready leaves the retire live");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Per bkr94acs.h at bkr94acsAcastAllReadied, the A-Cast's own
     * ACCEPTED is not the side channel's stop: "2t+1 readys, up to t
     * of them Byzantine, leave correct processes that still lack the
     * payload."  So a READY arriving AFTER the A-Cast's accept must
     * still be recorded -- the stop reaches 1 on the n-th ready sender
     * and the set gains it -- or the retire is pinned to ACCEPTED by
     * omission.  Per .h bkr94acsAcastInput the return is the number of
     * actions; an accepted A-Cast has no rule left to fire, so the
     * late READY is recorded silently.
     */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned char v[1];
    const unsigned char *m;
    unsigned int nact;

    sz = bkr94acsSz(3, 0, 4);
    a = calloc(1, sz);
    if (!a) goto b8_done;
    bkr94acsInit(a, 3, 1, 0, 4, 0, testCoin, 0);
    v[0] = 1;

    bkr94acsAcastInput(a, 0, BRACHA87_INITIAL, ANNOT_NO_ARM, 0, v, out);
    for (i = 0; i < 4; ++i)
      bkr94acsAcastInput(a, 0, BRACHA87_ECHO, ANNOT_NO_ARM, (unsigned char)i, v, out);
    CHECK(bkr94acsAcastAllReadied(a, 0) == 0,
          "B8: stop 0 with every process echoed and none readied");

    /* 2t+1 readys accept ahead of the last one. */
    for (i = 0; i < 3; ++i)
      bkr94acsAcastInput(a, 0, BRACHA87_READY, ANNOT_NO_ARM, (unsigned char)i, v, out);
    CHECK(bkr94acsAcastValue(a, 0) != 0, "B8: accepted on 2t+1 readys");
    CHECK(bkr94acsAcastAllReadied(a, 0) == 0, "B8: stop still 0 at accept");
    m = bkr94acsAcastReadied(a, 0);
    CHECK(m != 0, "B8: readied set non-null");
    if (m)
      CHECK(BRACHA87_SKIP_TST(m, 0) && BRACHA87_SKIP_TST(m, 1)
            && BRACHA87_SKIP_TST(m, 2) && !BRACHA87_SKIP_TST(m, 3),
            "B8: set holds the three readiers and lacks the late one at accept");

    /* The last READY arrives after accept. */
    nact = bkr94acsAcastInput(a, 0, BRACHA87_READY, ANNOT_NO_ARM, 3, v, out);
    CHECK(nact == 0, "B8: post-accept ready outputs 0 acts");
    CHECK(bkr94acsAcastAllReadied(a, 0) == 1,
          "B8: stop 1 on the n-th ready sender past accept");
    m = bkr94acsAcastReadied(a, 0);
    if (m)
      CHECK(BRACHA87_SKIP_TST(m, 3), "B8: set gains the late readier");

    free(a);
  }
  b8_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("B9: bkr94acsBaEntered / bkr94acsBaGetValid contracts");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Both accessors are read-only views of state the sweep-side
     * decisions already consume, and the header states each as a
     * correspondence with the duty query beside it:
     *
     *   bkr94acsBaEntered -- "1 iff this process has entered a value
     *   into the BA for 'process' ... bkr94acsFanoutDuty reads it
     *   (MET is nothing unentered)".  So MET and a BA reading 0 are
     *   incompatible.  Latched: "Set once, never cleared".
     *
     *   bkr94acsBaGetValid -- "the count is the one bkr94acsTurnDuty
     *   classifies from: >= n-t is its TOLERANCE-or-MET boundary,
     *   == n its MET", and the set is the paper's (q, k, v), so the
     *   senders are process indices and the values are what the
     *   drive's wires carried.
     *
     * Nothing here reads an internal: the entering evidence is an
     * A-Cast ACCEPT (step 1) or the fanout (step 2), and the VALID
     * set is fed by BA-class wires this banner delivers itself.
     */
    unsigned int nAct = 4, t = 1, vLen = 1, mp = 10;
    unsigned long sz;
    struct bkr94acs *p0;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned char senders[MAX_PROCESSES];
    unsigned char values[MAX_PROCESSES];
    unsigned char fedValue[MAX_PROCESSES];
    unsigned char val1;
    unsigned int src, k, p, b, cnt, duty, untouched, distinct;

    sz = bkr94acsSz(nAct - 1, vLen - 1, mp);
    p0 = calloc(1, sz);
    if (!p0) goto b9_done;
    bkr94acsInit(p0, nAct - 1, t, vLen - 1, mp, 0, testCoin, 0);

    /* Defensive guards, both accessors. */
    CHECK(bkr94acsBaEntered(0, 0) == 0, "B9: BaEntered NULL -> 0");
    CHECK(bkr94acsBaEntered(p0, 200) == 0,
          "B9: BaEntered out-of-range process -> 0");
    memset(senders, 0xAA, sizeof (senders));
    memset(values, 0xAA, sizeof (values));
    CHECK(bkr94acsBaGetValid(0, 0, senders, values) == 0,
          "B9: BaGetValid NULL -> 0");
    CHECK(bkr94acsBaGetValid(p0, 200, senders, values) == 0,
          "B9: BaGetValid out-of-range process -> 0");
    CHECK(bkr94acsBaGetValid(p0, 0, 0, values) == 0,
          "B9: BaGetValid NULL senders -> 0");
    CHECK(bkr94acsBaGetValid(p0, 0, senders, 0) == 0,
          "B9: BaGetValid NULL values -> 0");
    untouched = 1;
    for (k = 0; k < nAct; ++k)
      if (senders[k] != 0xAA || values[k] != 0xAA)
        untouched = 0;
    CHECK(untouched, "B9: a refused BaGetValid touches neither array");

    /* Fresh: nothing entered, no VALID set anywhere. */
    for (b = 0; b < nAct; ++b) {
      CHECK(bkr94acsBaEntered(p0, b) == 0,
            "B9: BaEntered 0 for every BA of a fresh instance");
      CHECK(bkr94acsBaGetValid(p0, b, senders, values) == 0,
            "B9: BaGetValid 0 for every BA of a fresh instance");
    }

    /* The entering evidence for BA_1: process 1's A-Cast ACCEPTs, so
     * step 1 enters 1.  Nothing else is entered by it. */
    val1 = 0x51;
    bkr94acsAcastInput(p0, 1, BRACHA87_INITIAL, ANNOT_NO_ARM, 1, &val1, out);
    for (src = 0; src < nAct; ++src)
      bkr94acsAcastInput(p0, 1, BRACHA87_ECHO, ANNOT_NO_ARM, src, &val1, out);
    for (src = 0; src < nAct; ++src)
      bkr94acsAcastInput(p0, 1, BRACHA87_READY, ANNOT_NO_ARM, src, &val1, out);
    CHECK(bkr94acsBaEntered(p0, 1) == 1,
          "B9: BaEntered 1 after the entering evidence");
    CHECK(bkr94acsBaEntered(p0, 0) == 0 && bkr94acsBaEntered(p0, 2) == 0
       && bkr94acsBaEntered(p0, 3) == 0,
          "B9: BaEntered 0 for the BAs no evidence reached");
    CHECK(bkr94acsFanoutDuty(p0) != BKR94ACS_DUTY_MET,
          "B9: fanout duty is not MET while a BA is unentered");

    /* Latched: further A-Cast traffic for the same process enters
     * nothing more and cannot clear the record. */
    for (src = 0; src < nAct; ++src)
      bkr94acsAcastInput(p0, 1, BRACHA87_READY, ANNOT_NO_ARM, src, &val1, out);
    CHECK(bkr94acsBaEntered(p0, 1) == 1,
          "B9: BaEntered latched across duplicate A-Cast traffic");

    /* BA_0's round 0, one BA-class ACCEPT per initiator, banked
     * WITHOUT turning so the accessor keeps answering round 0.  The
     * count and the duty class must agree at every step, and the set
     * must be exactly the wires delivered. */
    for (b = 0; b < nAct; ++b) {
      fedValue[b] = (b < 2) ? 0 : 1;
      feedBAAccept(p0, 0, 0, b, fedValue[b], out, 0, 0);

      memset(senders, 0xAA, sizeof (senders));
      memset(values, 0xAA, sizeof (values));
      cnt = bkr94acsBaGetValid(p0, 0, senders, values);
      duty = bkr94acsTurnDuty(p0, 0);
      CHECK(cnt == b + 1,
            "B9: BaGetValid count == the BA wires the drive validated");
      CHECK((cnt >= nAct - t)
            == (duty == BKR94ACS_DUTY_TOLERANCE
             || duty == BKR94ACS_DUTY_MET),
            "B9: count >= n-t exactly when TurnDuty is TOLERANCE or MET");
      CHECK((cnt == nAct) == (duty == BKR94ACS_DUTY_MET),
            "B9: count == n exactly when TurnDuty is MET");

      /* Senders are process indices, distinct, and each carries the
       * value its own wires carried. */
      distinct = 1;
      for (k = 0; k < cnt; ++k) {
        unsigned int j;

        if (senders[k] >= nAct)
          distinct = 0;
        for (j = 0; j < k; ++j)
          if (senders[j] == senders[k])
            distinct = 0;
        if (values[k] != fedValue[senders[k]])
          distinct = 0;
      }
      CHECK(distinct,
            "B9: senders distinct and in range, values as delivered");
      untouched = 1;
      for (k = cnt; k < nAct; ++k)
        if (senders[k] != 0xAA || values[k] != 0xAA)
          untouched = 0;
      CHECK(untouched, "B9: BaGetValid writes only the count it returns");
    }

    free(p0);

    /* The fanout's own correspondence, over a converged cluster: at
     * MET nothing is unentered, so every BA reads 1 at every process.
     * The same run pins the duty correspondence at quiescence, where
     * every BA is out of complete rounds. */
    if (allocCluster(processes, nAct, t, vLen - 1, mp) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < nAct; ++i)
        acasts[i * vLen] = 0x70 + i;

      runHonest(nAct, vLen, acasts, 0 /*ordered*/, processes, obs);

      for (p = 0; p < nAct; ++p) {
        CHECK(bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_MET,
              "B9: fanout duty MET at a converged process");
        for (b = 0; b < nAct; ++b) {
          CHECK(bkr94acsBaEntered(processes[p], b) == 1,
                "B9: MET means every BA reads BaEntered 1");
          cnt = bkr94acsBaGetValid(processes[p], b, senders, values);
          duty = bkr94acsTurnDuty(processes[p], b);
          CHECK((cnt >= nAct - t)
                == (duty == BKR94ACS_DUTY_TOLERANCE
                 || duty == BKR94ACS_DUTY_MET),
                "B9: the boundary holds at quiescence too");
          distinct = 1;
          for (k = 0; k < cnt; ++k) {
            unsigned int j;

            if (senders[k] >= nAct)
              distinct = 0;
            for (j = 0; j < k; ++j)
              if (senders[j] == senders[k])
                distinct = 0;
          }
          CHECK(distinct,
                "B9: quiescent senders distinct and in range");
        }
      }
      freeCluster(processes, nAct);
    }
  }
  b9_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section C -- BPR / Retry                                          */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("C1: Retry idle on fresh process (no A-Cast)");
  /* ---------------------------------------------------------------- */
  {
    /* Per .h: "Returns 0 only when a full sweep finds no sent
     * instance -- pre-broadcast / shutdown state".  A freshly-Init'd
     * process that has not A-Cast and received no inputs has no
     * sent Fig1 instances; every Retry call must return 0,
     * regardless of cursor position. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Retry cursor;
    struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
    unsigned int j, n;

    sz = bkr94acsSz(3, 0, 10);
    a = calloc(1, sz);
    if (!a) goto c1_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bracha87RetryInit(&cursor);
    /* Walk well past the cursor space (A-Cast Fig1s + every owned
     * BA Fig1 slot).  All return 0. */
    for (j = 0; j < 1024; ++j) {
      n = bkr94acsRetryStep(a, &cursor, out);
      CHECK(n == 0, "C1: fresh process Retry returns 0 every call");
      if (n != 0) break;
    }
    free(a);
  }
  c1_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("C2: Retry after A-Cast outputs self A-Cast INITIAL");
  /* ---------------------------------------------------------------- */
  {
    /* A-Cast sets the INITIATOR bit on self's A-Cast Fig1.  Per .h
     * BPR rules: INITIATOR -> output INITIAL_ALL on every Bpr call
     * until ACCEPTED or all-echoed (Note 11).
     * The cursor must visit self's A-Cast Fig1 in finite calls and
     * surface the retry. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Retry cursor;
    struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
    struct bkr94acsAct acastOut[1];
    unsigned char val = 0xC2;
    unsigned int j, k, n;
    int sawSelfInitial = 0;

    sz = bkr94acsSz(3, 0, 10);
    a = calloc(1, sz);
    if (!a) goto c2_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bkr94acsAcast(a, &val, acastOut);
    bracha87RetryInit(&cursor);

    /* 32 calls is plenty: cursor starts at 0 = A-Cast Fig1 initiator 0
     * (= self), so the first call should already output. */
    for (j = 0; j < 32; ++j) {
      n = bkr94acsRetryStep(a, &cursor, out);
      CHECK(n <= BKR94ACS_RETRY_MAX_ACTS, "C2: Retry within MAX_ACTS bound");
      for (k = 0; k < n; ++k) {
        if (out[k].act == BKR94ACS_ACT_ACAST_SEND
         && out[k].process == 0
         && out[k].type == BRACHA87_INITIAL) {
          sawSelfInitial = 1;
          /* Borrowed pointer matches stored value. */
          CHECK(out[k].value != 0
             && out[k].value == bkr94acsAcastValue(a, 0)
             && out[k].value[0] == val,
                "C2: Retry output carries A-Cast value");
        }
      }
    }
    CHECK(sawSelfInitial, "C2: Retry traversal surfaces self A-Cast INITIAL");

    free(a);
  }
  c2_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("C3+C4: Retry-driven all-honest run, MAX_ACTS + monotone witness");
  /* ---------------------------------------------------------------- */
  {
    /* Drive an all-honest n=4 t=1 run with Retry in the loop (no
     * drops, no silent process).  Verify witnesses:
     *   C3: max acts output by any Retry call <= BKR94ACS_RETRY_MAX_ACTS
     *   C4: SentFig1Count is monotone non-decreasing per process
     * Plus the standard Lemma 2 properties for sanity. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned int maxRetryActs = 999;
    unsigned int monotoneViolations = 999;
    int rc;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)('p' + i);

      rc = runWithRetry(n, vLen, acasts, 0, -1, 1000, processes, obs,
                       &maxRetryActs, &monotoneViolations);
      CHECK(rc == 0, "C3+C4: all-honest Retry run converges");
      CHECK(maxRetryActs <= BKR94ACS_RETRY_MAX_ACTS,
            "C3: Retry never exceeds BKR94ACS_RETRY_MAX_ACTS");
      CHECK(monotoneViolations == 0,
            "C4: SentFig1Count monotone non-decreasing");

      assertLemma2(processes, obs, n, t);

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("C5: Retry full-sweep idle return = 0 (barren-sweep signal)");
  /* ---------------------------------------------------------------- */
  {
    /* The .h documents Retry returning 0 only on full-sweep idle --
     * the only contractual case is "pre-broadcast / shutdown".  This
     * banner re-anchors that on a fresh process (same as C1, formalized
     * as the barren-sweep exit signal a deployment uses). */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Retry cursor;
    struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
    unsigned int j, n;
    unsigned int zeros = 0;

    sz = bkr94acsSz(3, 0, 10);
    a = calloc(1, sz);
    if (!a) goto c5_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bracha87RetryInit(&cursor);
    for (j = 0; j < 256; ++j) {
      n = bkr94acsRetryStep(a, &cursor, out);
      if (n == 0) ++zeros;
    }
    CHECK(zeros == 256,
          "C5: pre-A-Cast Retry returns 0 every call (idle-sweep signal)");

    free(a);
  }
  c5_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("C6: Retry-driven convergence at 50% drop");
  /* ---------------------------------------------------------------- */
  {
    /* High-loss network: 50% of every output wire is dropped at
     * source.  The protocol's only mechanism for recovering is BPR
     * retry via Retry.  Convergence under loss exercises the retry
     * rules (INITIATOR -> INITIAL until ACCEPTED or all-echoed,
     * ECHOED -> ECHO until ACCEPTED, RDSENT -> READY until every
     * process has announced accept) end-to-end. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned int maxRetryActs;
    unsigned int monotoneViolations;
    int rc;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)(i + 1);

      rc = runWithRetry(n, vLen, acasts, 50, -1, 5000, processes, obs,
                       &maxRetryActs, &monotoneViolations);
      CHECK(rc == 0, "C6: 50% drop run converges");
      CHECK(maxRetryActs <= BKR94ACS_RETRY_MAX_ACTS,
            "C6: Retry within MAX_ACTS bound under loss");
      CHECK(monotoneViolations == 0,
            "C6: SentFig1Count monotone under loss");
      if (rc == 0)
        assertLemma2(processes, obs, n, t);

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("C7: Silent Byzantine process canary (Note 11 regression)");
  /* ---------------------------------------------------------------- */
  {
    /* n=4 t=1, process 3 is Byzantine-silent: never A-Casts, never
     * receives, never outputs.  Honest processes 0/1/2 must converge --
     * SubSet excludes process 3 via step-2 enter-0 fanout for process 3.
     *
     * This is the regression for Note 11: the initiator INITIAL
     * retry must NOT short-circuit on local ECHOED.  Each honest
     * process is an initiator of its own A-Cast; their Retry calls must
     * keep retrying INITIAL until that A-Cast is accepted (the
     * sound stop), NOT merely until they echoed locally.  At the
     * n=3t+1 boundary Bracha's echo threshold ((n+t)/2+1) equals the
     * honest count, so any process that missed the bootstrap depends on
     * the initiator's continued INITIAL retry to complete its echo
     * count.  The original gap-4 design (`INITIATOR && !ECHOED -> output`)
     * stalled at |SubSet|=1 in this setup. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned int maxRetryActs;
    unsigned int monotoneViolations;
    int rc;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      unsigned char subset[MAX_PROCESSES];
      unsigned int sz, p, j;

      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)(0xA0 + i);

      /* 12.5% drop on top of the silent process, matching the
       * white-box testBprByzantineSilent setup. */
      rc = runWithRetry(n, vLen, acasts, 12, 3 /* silentProcess */,
                       5000, processes, obs,
                       &maxRetryActs, &monotoneViolations);
      CHECK(rc == 0, "C7: silent Byzantine process -- honest processes converge");
      CHECK(monotoneViolations == 0,
            "C7: SentFig1Count monotone with silent process");

      /* Honest processes (0/1/2) agree on a SubSet, of size >= n-t=3.
       * Process 3 must be excluded (its Fig1 never accepts at any
       * honest process because process 3 never broadcasts its INITIAL). */
      sz = bkr94acsSubset(processes[0], subset);
      CHECK(sz >= n - t, "C7: |SubSet| >= n-t");
      for (p = 1; p < 3; ++p) {
        unsigned char other[MAX_PROCESSES];
        unsigned int szOther = bkr94acsSubset(processes[p], other);
        CHECK(szOther == sz, "C7: honest processes agree on SubSet size");
        if (szOther == sz)
          CHECK(memcmp(subset, other, sz) == 0,
                "C7: honest processes agree on SubSet contents");
      }
      for (j = 0; j < sz; ++j)
        CHECK(subset[j] != 3, "C7: SubSet excludes silent process");

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("C8: Input dedup -- retried wire returns 0 acts (barren-sweep invariant)");
  /* ---------------------------------------------------------------- */
  {
    /* Load-bearing invariant for deployment-layer barren-sweep gate
     * exit: the per-process progress count advances only when AcastInput /
     * BAInput returns nacts > 0.  BPR Retry keeps retrying
     * un-retired actions (READY forever; INITIAL/ECHO until accept)
     * onto already-delivered wires (Notes 10/11); if those
     * re-deliveries returned acts > 0, the barren-sweep count would
     * never reach S and the exit could never form.
     *
     * Drive a small honest cluster to convergence, capturing along
     * the way one ACAST and one BA wire whose FIRST
     * delivery produced acts.  Then re-deliver each (same args,
     * same target process) and assert n == 0. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned long actsCap;
    struct bkr94acsAct *out;
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct bkr94acsAct acastOut[1];
    struct bkr94acsAct retryOut[BKR94ACS_RETRY_MAX_ACTS];
    struct wire acastSample;
    struct wire baSample;
    int haveAcastSample;
    int haveBaSample;
    struct wire w;
    unsigned int iter;
    unsigned int nDeliv;
    unsigned int nRetry;
    int allComplete;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i)
        acasts[i * vLen] = (unsigned char)(0xA0 + i);

      actsCap = BKR94ACS_MAX_ACTS(n - 1);
      out = malloc(actsCap * sizeof (*out));
      if (out) {
        qReset();
        for (i = 0; i < n; ++i)
          bracha87RetryInit(&cursors[i]);
        haveAcastSample = 0;
        haveBaSample = 0;

        for (i = 0; i < n; ++i) {
          nDeliv = bkr94acsAcast(processes[i], acasts + i * vLen, acastOut);
          observeAndOutput(&obs[i], (unsigned char)i, n, acastOut, nDeliv,
                         vLen, 0, -1);
        }

        allComplete = 0;
        for (iter = 0; iter < 5000 && !allComplete; ++iter) {
          while (qSize() > 0) {
            qPopHead(&w);
            if (w.cls == BKR94ACS_CLS_ACAST)
              nDeliv = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                             wireAnnot(&w),
                                             w.from, w.value, out);
            else
              nDeliv = bkr94acsBaInput(processes[w.to], w.process,
                                              w.round, w.initiator,
                                              w.type, wireAnnot(&w), w.from, w.baValue,
                                              out);
            if (nDeliv > 0) {
              if (w.cls == BKR94ACS_CLS_ACAST && !haveAcastSample) {
                acastSample = w;
                haveAcastSample = 1;
              } else if (w.cls == BKR94ACS_CLS_BA && !haveBaSample) {
                baSample = w;
                haveBaSample = 1;
              }
            }
            observeAndOutput(&obs[w.to], w.to, n, out, nDeliv, vLen, 0, -1);
            drainTurns(processes[w.to], &obs[w.to], w.to, n, vLen, out, 0, -1);
            nDeliv = bkr94acsFanout(processes[w.to], out);
            observeAndOutput(&obs[w.to], w.to, n, out, nDeliv, vLen, 0, -1);
          }
          for (i = 0; i < n; ++i) {
            nDeliv = bkr94acsRetryStep(processes[i], &cursors[i], retryOut);
            observeAndOutput(&obs[i], (unsigned char)i, n, retryOut, nDeliv,
                           vLen, 0, -1);
            drainTurns(processes[i], &obs[i], (unsigned char)i, n, vLen, out,
                       0, -1);
            nDeliv = bkr94acsFanout(processes[i], out);
            observeAndOutput(&obs[i], (unsigned char)i, n, out, nDeliv,
                           vLen, 0, -1);
          }
          allComplete = 1;
          for (i = 0; i < n; ++i)
            if (!processes[i]->complete) {
              allComplete = 0;
              break;
            }
        }
        CHECK(allComplete, "C8: cluster converged");
        CHECK(haveAcastSample,
              "C8: captured an ACAST wire whose first delivery output acts");
        CHECK(haveBaSample,
              "C8: captured a BA wire whose first delivery output acts");

        /* Retry: identical args, same target process.  The receiver's
         * Bracha state has already consumed this exact (process, type,
         * sender [+ round, initiator, baValue]) tuple; per Fig1
         * Rule 1/2/3 dedup the dispatch must produce zero acts. */
        if (haveAcastSample) {
          nRetry = bkr94acsAcastInput(processes[acastSample.to],
                                          acastSample.process, acastSample.type,
                                          ANNOT_NO_ARM,
                                          acastSample.from, acastSample.value,
                                          out);
          CHECK(nRetry == 0,
                "C8: re-delivered ACAST returns 0 acts (Input dedup)");
        }
        if (haveBaSample) {
          nRetry = bkr94acsBaInput(processes[baSample.to],
                                           baSample.process, baSample.round,
                                           baSample.initiator,
                                           baSample.type, ANNOT_NO_ARM, baSample.from,
                                           baSample.baValue, out);
          CHECK(nRetry == 0,
                "C8: re-delivered BA returns 0 acts (Input dedup)");
        }

        free(out);
      }
      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section D -- EXHAUSTED                                          */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("D1: BA_EXHAUSTED single output, 0xFE sentinel, !complete");
  /* ---------------------------------------------------------------- */
  {
    /* maxPhases=1 -> BA has only 1 phase (3 sub-rounds) to terminate.
     * Drive split values across all 3 sub-rounds at every initiator
     * so neither the >2t case (i) nor the >t case (ii) of Fig4
     * step 3 fires.  Fig4 returns BRACHA87_EXHAUSTED.  BKR94 surfaces
     * BKR94ACS_ACT_BA_EXHAUSTED exactly once, sets baDecision[0]=0xFE,
     * and never sets complete (no unilateral substitute is safe --
     * Part C of Lemma 2 agreement would break).
     *
     * The arrival path banks; the act comes from the zero-patience turn
     * drain feedBAAccept runs after every input, and the last round's
     * turn is the one that carries it (after it TurnDuty is HELD
     * forever -- the round space is consumed). */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PROCESSES - 1)];
    unsigned int round, b, n, k;
    unsigned int exhaustedSeen = 0;

    sz = bkr94acsSz(3, 0, 1);   /* n=4, vLen=1, maxPhases=1 */
    a = calloc(1, sz);
    if (!a) goto d1_done;
    bkr94acsInit(a, 3, 1, 0, 1, 0, testCoin, 0);

    /* Drive every (round, initiator) Fig1 in phase 0 to ACCEPT
     * with a value that splits 2/2 across initiators per round. */
    for (round = 0; round < 3; ++round)
      for (b = 0; b < 4; ++b)
        feedBAAccept(a, 0, (unsigned char)round, (unsigned char)b,
                            (b < 2) ? 0 : 1, out, 1, &exhaustedSeen);

    CHECK(exhaustedSeen == 1, "D1: BA_EXHAUSTED output exactly once");
    CHECK(bkr94acsBaDecision(a, 0) == 0xFE,
          "D1: baDecision[0] == 0xFE (exhausted sentinel)");
    CHECK(a->complete == 0,
          "D1: complete remains clear (no unilateral substitute)");
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_HELD,
          "D1: TurnDuty HELD after EXHAUSTED (round space consumed)");

    /* Subsequent BA input for the exhausted process must NOT
     * retry BA_EXHAUSTED -- neither on the input (which can only
     * output echo/ready) nor on the turn that follows it. */
    n = bkr94acsBaInput(a, 0, 0, 0, BRACHA87_READY, ANNOT_NO_ARM, 0, 0, out);
    CHECK(n <= 2, "D1: later BA input within the 2-act bound");
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED)
        ++exhaustedSeen;
    while ((n = bkr94acsTurn(a, 0, out)) > 0 && turnDrained())
      for (k = 0; k < n; ++k)
        if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED)
          ++exhaustedSeen;
    CHECK(exhaustedSeen == 1, "D1: no duplicate BA_EXHAUSTED on later input");

    free(a);
  }
  d1_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("D2: Retry continues past EXHAUSTED for that process");
  /* ---------------------------------------------------------------- */
  {
    /* Per .h: "BPR retry continues for that process (0xFE != 0 in the
     * retry gate) so other processes may still benefit from earlier-round
     * echoes / readys."  After EXHAUSTED for process 0, Retry must
     * still output retries for the BA Fig1s belonging to
     * process 0 (the ones that ACCEPTed earlier). */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Retry cursor;
    struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
    struct bkr94acsAct synthOut[BKR94ACS_MAX_ACTS(MAX_PROCESSES - 1)];
    unsigned int round, b, j, k, n;
    unsigned int exhaustedSeen = 0;
    unsigned int process0Retries = 0;

    sz = bkr94acsSz(3, 0, 1);
    a = calloc(1, sz);
    if (!a) goto d2_done;
    bkr94acsInit(a, 3, 1, 0, 1, 0, testCoin, 0);

    /* Set up an EXHAUSTED state same as D1. */
    for (round = 0; round < 3; ++round)
      for (b = 0; b < 4; ++b)
        feedBAAccept(a, 0, (unsigned char)round, (unsigned char)b,
                            (b < 2) ? 0 : 1, synthOut, 1, &exhaustedSeen);
    CHECK(exhaustedSeen == 1, "D2: EXHAUSTED setup OK");
    CHECK(bkr94acsFig1SentCount(a) > 0,
          "D2: post-EXHAUSTED SentFig1Count > 0");

    /* Sweep Retry enough to traverse all Fig1 slots; count BA_SEND
     * retries for process 0 (the EXHAUSTED process). */
    bracha87RetryInit(&cursor);
    for (j = 0; j < 2048; ++j) {
      n = bkr94acsRetryStep(a, &cursor, out);
      for (k = 0; k < n; ++k) {
        if (out[k].act == BKR94ACS_ACT_BA_SEND && out[k].process == 0)
          ++process0Retries;
      }
    }
    CHECK(process0Retries > 0,
          "D2: Retry continues to retry BA Fig1s for EXHAUSTED process");

    free(a);
  }
  d2_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section E -- Byzantine                                          */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("E1: Equivocating A-Caster (Bracha Lemma 2 inheritance)");
  /* ---------------------------------------------------------------- */
  {
    /*
     * n=4 t=1, process 0 is Byzantine and equivocates its own A-Cast:
     *   INITIAL/v1 -> processes 1, 2
     *   INITIAL/v2 -> process 3
     * Process 0 sends nothing else (no echoes, no readys, no BA).
     *
     * Bracha 1987 Lemma 2: "if two correct processes accept u and v,
     * then u = v."  Composed at the BKR94 layer: any honest process
     * that ACCEPTs process 0's Fig1 must accept the same value as any
     * other honest process that ACCEPTs.  In this split it's likely
     * neither v1 nor v2 reaches the (n+t)/2+1=3 echo threshold at
     * any honest process, so Fig1 initiator 0 never accepts -> BA_0 decides
     * 0 via step-2 fanout -> SubSet excludes process 0.
     *
     * Black-box assertion: ACS still completes; honest processes agree
     * on SubSet; if any honest process's bkr94acsAcastValue(0) is
     * non-null, all honest processes see the same bytes there (Lemma 2);
     * |SubSet| >= n-t.  Honest processes 1, 2, 3 A-Cast and run
     * normally; the harness manually injects process 0's split INITIAL.
     */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned char v1 = 0xE1;
    unsigned char v2 = 0xE2;

    if (allocCluster(processes, n, t, vLen - 1, mp) == 0) {
      struct bracha87Retry cursors[MAX_PROCESSES];
      unsigned long actsCap;
      struct bkr94acsAct *out;
      struct bkr94acsAct acastOut[1];
      struct bkr94acsAct retryOut[BKR94ACS_RETRY_MAX_ACTS];
      struct wire w;
      unsigned int iter, j, p, q, sz;
      int allComplete;
      unsigned char subset[MAX_PROCESSES];

      { unsigned int oi; for (oi = 0; oi < MAX_PROCESSES; ++oi) obsInit(&obs[oi]); }
      qReset();

      actsCap = BKR94ACS_MAX_ACTS(n - 1);
      out = malloc(actsCap * sizeof (*out));
      if (!out) { freeCluster(processes, n); goto e1_done; }

      for (i = 0; i < n; ++i)
        bracha87RetryInit(&cursors[i]);

      /* Process 0's Byzantine equivocation: split-INITIAL output only.
       * No A-Cast, no Retry for process 0 -- this attacker only sends
       * the bootstrap INITIAL, then is silent. */
      memset(&w, 0, sizeof (w));
      w.cls = BKR94ACS_CLS_ACAST;
      w.process = 0;
      w.type = BRACHA87_INITIAL;
      w.from = 0;
      for (q = 1; q <= 2; ++q) {
        w.to = (unsigned char)q;
        w.value[0] = v1;
        qPush(&w);
      }
      w.to = 3;
      w.value[0] = v2;
      qPush(&w);

      /* Honest processes 1, 2, 3 A-Cast. */
      for (p = 1; p < n; ++p) {
        unsigned char val = (unsigned char)(0xB0 + p);
        unsigned int nact = bkr94acsAcast(processes[p], &val, acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, n, acastOut, nact, vLen,
                       0, -1);
      }

      for (iter = 0; iter < 2000; ++iter) {
        while (qSize() > 0) {
          unsigned int nact;
          qPopHead(&w);
          if (w.to == 0)
            continue;  /* Byzantine process 0 is also silent on receive */
          if (w.cls == BKR94ACS_CLS_ACAST)
            nact = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                         wireAnnot(&w),
                                         w.from, w.value, out);
          else
            nact = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                          w.initiator, w.type, wireAnnot(&w), w.from,
                                          w.baValue, out);
          observeAndOutput(&obs[w.to], w.to, n, out, nact, vLen, 0, -1);
          drainTurns(processes[w.to], &obs[w.to], w.to, n, vLen, out, 0, -1);
          nact = bkr94acsFanout(processes[w.to], out);
          observeAndOutput(&obs[w.to], w.to, n, out, nact, vLen, 0, -1);
        }
        for (p = 1; p < n; ++p) {
          unsigned int nact = bkr94acsRetryStep(processes[p], &cursors[p], retryOut);
          observeAndOutput(&obs[p], (unsigned char)p, n, retryOut, nact, vLen,
                         0, -1);
          drainTurns(processes[p], &obs[p], (unsigned char)p, n, vLen, out,
                     0, -1);
          nact = bkr94acsFanout(processes[p], out);
          observeAndOutput(&obs[p], (unsigned char)p, n, out, nact, vLen,
                         0, -1);
        }
        allComplete = 1;
        for (p = 1; p < n; ++p)
          if (!processes[p]->complete) { allComplete = 0; break; }
        if (allComplete) break;
      }
      free(out);

      /* Honest processes (1, 2, 3) all completed. */
      for (p = 1; p < n; ++p)
        CHECK(processes[p]->complete,
              "E1: honest process completes despite equivocating A-Caster");

      /* Honest processes agree on SubSet (Lemma 2 Part C). */
      sz = bkr94acsSubset(processes[1], subset);
      CHECK(sz >= n - t, "E1: |SubSet| >= n-t");
      for (p = 2; p < n; ++p) {
        unsigned char other[MAX_PROCESSES];
        unsigned int szOther = bkr94acsSubset(processes[p], other);
        CHECK(szOther == sz, "E1: honest SubSet sizes agree");
        if (szOther == sz)
          CHECK(memcmp(subset, other, sz) == 0,
                "E1: honest SubSet contents agree");
      }

      /* Bracha Lemma 2 inheritance via the bkr94acs.h contract:
       *
       *   "Returns pointer to the vLen + 1 byte value, or 0 if not
       *    yet accepted (or, for self-process, not yet A-Cast)."
       *
       * For a non-self process, AcastValue is non-null iff the
       * local Fig1 has ACCEPTED.  Bracha Lemma 2 then guarantees any
       * two honest acceptors agree on the value.  Equivocation by
       * process 0 must not produce a state where process A's
       * bkr94acsAcastValue(0) == v1 and process B's == v2.
       *
       * (BA_0 deciding 0 across all processes -- i.e. SubSet excludes
       * process 0 -- is the expected case here, since neither v1 nor v2
       * can reach the (n+t)/2+1 echo threshold under this split.) */
      {
        for (p = 1; p < n; ++p) {
          const unsigned char *v_a = bkr94acsAcastValue(processes[p], 0);
          unsigned int q2;
          for (q2 = p + 1; q2 < n; ++q2) {
            const unsigned char *v_b = bkr94acsAcastValue(processes[q2], 0);
            if (v_a && v_b)
              CHECK(v_a[0] == v_b[0],
                    "E1: Bracha Lemma 2 -- accepted values agree across honest processes");
          }
        }
        /* Honest processes' own A-Cast values must round-trip
         * (orthogonal to process 0's equivocation). */
        for (p = 1; p < n; ++p) {
          for (q = 1; q < n; ++q) {
            const unsigned char *v = bkr94acsAcastValue(processes[p],
                                       (unsigned char)q);
            CHECK(v != 0 && v[0] == (unsigned char)(0xB0 + q),
                  "E1: honest A-Cast values preserved");
          }
        }
      }

      /* SubSet contents include only processes for which Q(j)=1, i.e.
       * Fig1 ACCEPTED at the local process.  This is Lemma 2 Part D
       * inherited from Section B. */
      for (j = 0; j < sz; ++j) {
        unsigned char oj = subset[j];
        for (p = 1; p < n; ++p)
          CHECK(bkr94acsAcastValue(processes[p], oj) != 0,
                "E1: Part D -- SubSet members have accepted values");
      }

      freeCluster(processes, n);
    }
  }
  e1_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section F -- Step 2 pacing (bkr94acsFanoutDuty / bkr94acsFanout)*/
  /*                                                                  */
  /*  The same delayed-A-Cast schedule under two patience values:     */
  /*  the fanout at enabling (F1) excludes the delayed honest process */
  /*  and patience (F2) includes it -- the pair is the WAN            */
  /*  exclusion seed and its remedy.  F3 is the liveness half: a      */
  /*  dead slot holds TOLERANCE forever, patience bounds the tax,     */
  /*  and firing after it completes the instance.  F4 adds the        */
  /*  second sweep clock beside the first: the barren count an        */
  /*  abandonment policy reads, and the sizing that orders the two.   */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("F1: fanout at enabling excludes a delayed honest A-Cast");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct bkr94acsAct acastOut[1];
    unsigned int tolSweeps, fanActs;
    unsigned int n, p;

    if (allocCluster(processes, 4, 1, 0, 8) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) bracha87RetryInit(&cursors[p]);
      qReset();

      /* Processes 0-2 A-Cast now; process 3 is the WAN laggard --
       * only its OUTBOUND A-Cast is delayed.  The process itself
       * runs: it receives, retries, enters, completes. */
      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0xE0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      CHECK(fDrive(processes, obs, cursors, 0, -1, 500,
                   &tolSweeps, &fanActs) == 0,
            "F1: all four complete without 3's A-Cast");
      /* Every process fired exactly one enter-0 (BA_3), and BA_3
       * decided 0 -- honest 3 shut out of SubSet. */
      CHECK(fanActs == 4, "F1: one enter-0 act per process");
      for (p = 0; p < 4; ++p) {
        unsigned char subset[4];

        CHECK(bkr94acsSubset(processes[p], subset) == 3,
              "F1: |SubSet| == 3");
        CHECK(bkr94acsBaDecision(processes[p], 3) == 0,
              "F1: delayed process's BA decided 0");
        CHECK(bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_MET,
              "F1: duty MET after the fanout");
      }

      /* The delayed A-Cast arrives after the close: it still accepts
       * everywhere (the value is not lost) but the subset is fixed --
       * the paper's per-instance honest-exclusion allowance. */
      acasts[3] = 0xE3;
      n = bkr94acsAcast(processes[3], &acasts[3], acastOut);
      observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, 0, -1);
      fDrive(processes, obs, cursors, 0, -1, 50, &tolSweeps, &fanActs);
      for (p = 0; p < 4; ++p) {
        unsigned char subset[4];

        CHECK(bkr94acsAcastValue(processes[p], 3) != 0,
              "F1: late A-Cast accepted everywhere (value not lost)");
        CHECK(bkr94acsSubset(processes[p], subset) == 3,
              "F1: subset unchanged by the late arrival");
        CHECK(bkr94acsBaDecision(processes[p], 3) == 0,
              "F1: exclusion final for this instance");
      }
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("F1b: quiescence under the decided-0 retire mechanism");
  /* ---------------------------------------------------------------- */
  /*  F1's schedule carried past COMPLETE to the Retry 0 return at    */
  /*  every process, with both READY annotations exchanged.  A short  */
  /*  SubSet is not a separate ending -- it is a COMPLETE, and the    */
  /*  contract is |SubSet| >= n-t -- so what this arm is for is the   */
  /*  RETIRE MECHANISM, which differs here: bkr94acs.h states the     */
  /*  per-process retry gate skips the A-Cast walk of a BA that       */
  /*  decided 0, so for that instance the gate itself is the retire   */
  /*  and its READY mask can stay permanently short.                  */
  /*                                                                  */
  /*  The mask claim is therefore SCOPED to the instances the retry   */
  /*  still SERVES.  An unscoped claim would fail against the correct */
  /*  machine, on this very schedule.                                 */
  /*                                                                  */
  /*  Its own phases and its own cap, deliberately: F1 allocates 8    */
  /*  phases and stops at COMPLETE, while the retirement tail runs    */
  /*  well past completion over the whole round space.                */
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    struct wire w;
    unsigned int tolSweeps, fanActs;
    unsigned int quiesced[4];
    unsigned int nQuiesced;
    unsigned int served, covered, gated, gatedShort;
    unsigned int iter, n, p, j, r, b, q;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        quiesced[p] = 0;
      }
      qReset();

      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0xF0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }
      CHECK(fDrive(processes, obs, cursors, 0, -1, 500,
                   &tolSweeps, &fanActs) == 0,
            "F1b: all four complete without 3's A-Cast");

      /* The late submission: its value is still accepted everywhere,
       * and the subset is already fixed -- participation loss. */
      acasts[3] = 0xF3;
      n = bkr94acsAcast(processes[3], &acasts[3], acastOut);
      observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, 0, -1);

      nQuiesced = 0;
      for (iter = 0; iter < 20000 && nQuiesced < 4; ++iter) {
        while (qSize() > 0) {
          qPopHead(&w);
          if (w.cls == BKR94ACS_CLS_ACAST) {
            n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                      wireAnnot(&w),
                                      w.from, w.value, out);
            /* Leaving the rotation is provisional: only a tick can
             * re-send, so an outstanding arm puts the process back. */
            if (w.type == BRACHA87_READY && !w.received) {
              if (quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
            }
          } else {
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                       w.initiator, w.type, wireAnnot(&w), w.from,
                                       w.baValue, out);
            if (w.type == BRACHA87_READY && !w.received) {
              if (quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
            }
          }
          observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
        }
        for (p = 0; p < 4; ++p) {
          if (!quiesced[p]) {
            n = bkr94acsRetryStep(processes[p], &cursors[p], out);
            if (!n && bkr94acsFig1SentCount(processes[p])) {
              quiesced[p] = 1;
              ++nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          }
          for (b = 0; b < 4; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b,
                                     out)) > 0 && turnDrained()) {
              if (quiesced[p]) {
                quiesced[p] = 0;
                --nQuiesced;
              }
              observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
            }
          n = bkr94acsFanout(processes[p], out);
          if (n) {
            if (quiesced[p]) {
              quiesced[p] = 0;
              --nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          }
        }
      }
      CHECK(nQuiesced == 4, "F1b: every process reached the Retry 0 return");
      CHECK(qSize() == 0, "F1b: the wire is silent at quiescence");

      for (p = 0; p < 4; ++p) {
        unsigned char subset[4];

        CHECK(processes[p]->complete, "F1b: quiescence past COMPLETE");
        CHECK(bkr94acsRetryStep(processes[p], &cursors[p], out) == 0,
              "F1b: and the 0 return is stable");
        CHECK(bkr94acsSubset(processes[p], subset) == 3,
              "F1b: |SubSet| == 3");
        CHECK(bkr94acsBaDecision(processes[p], 3) == 0,
              "F1b: the excluded process's BA decided 0");
        CHECK(bkr94acsAcastValue(processes[p], 3) != 0,
              "F1b: the late A-Cast is accepted everywhere");
      }

      /* The scoped ending evidence.  An A-Cast whose BA decided 0 is
       * out of the walk -- the gate is its retire -- so it is counted
       * and skipped, never required to cover. */
      served = 0;
      covered = 0;
      gated = 0;
      gatedShort = 0;
      for (p = 0; p < 4; ++p) {
        for (j = 0; j < 4; ++j) {
          const struct bracha87Fig1 *f1;
          const unsigned char *skip;

          if (!(f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j))
           || !bracha87Fig1Value(f1))
            continue;
          skip = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
          for (q = 0; q < 4; ++q)
            if (!skip || !BRACHA87_SKIP_TST(skip, q))
              break;
          if (!bkr94acsBaDecision(processes[p], (unsigned char)j)) {
            ++gated;
            if (q < 4)
              ++gatedShort;
            continue;
          }
          ++served;
          if (q == 4)
            ++covered;
        }
        for (j = 0; j < 4; ++j)
          for (r = 0; r < 6; ++r)
            for (b = 0; b < 4; ++b) {
              const struct bracha87Fig1 *f1;
              const unsigned char *skip;

              if (!(f1 = bkr94acsBaFig1(processes[p], (unsigned char)j,
                                        (unsigned char)r, (unsigned char)b))
               || !bracha87Fig1Value(f1))
                continue;
              ++served;
              skip = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
              for (q = 0; q < 4; ++q)
                if (!skip || !BRACHA87_SKIP_TST(skip, q))
                  break;
              if (q == 4)
                ++covered;
            }
      }
      CHECK(gated == 4, "F1b: the excluded A-Cast is gated at every process");
      CHECK(served > 0, "F1b: served instances were actually examined");
      CHECK(served == covered,
            "F1b: every instance the retry still serves has a full READY mask");
      printf("      F1b: %u served instances, %u covered, %u gated"
             " (%u of them with a short mask)\n",
             served, covered, gated, gatedShort);
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("F2: patience includes the same delayed honest A-Cast");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct bkr94acsAct acastOut[1];
    struct bkr94acsAct fout[4];
    unsigned int tolSweeps, fanActs;
    unsigned int n, p;

    if (allocCluster(processes, 4, 1, 0, 8) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) bracha87RetryInit(&cursors[p]);
      qReset();

      /* Identical schedule to F1 -- but the patience never elapses. */
      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0xE0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      /* Without the fanout the instance parks at TOLERANCE: BAs 0-2
       * decide 1, BA_3 stays unentered, completion is impossible --
       * and the sweep keeps retrying (BPR gate: undecided -> retry). */
      CHECK(fDrive(processes, obs, cursors, -1, -1, 30,
                   &tolSweeps, &fanActs) != 0,
            "F2: incomplete while patience holds");
      CHECK(fanActs == 0, "F2: fanout never fired");
      CHECK(tolSweeps >= 5, "F2: TOLERANCE held across the sweeps");
      for (p = 0; p < 4; ++p)
        CHECK(bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_TOLERANCE,
              "F2: duty TOLERANCE at every process while waiting");

      /* The delayed A-Cast arrives INSIDE the patience window: step 1 enters 1,
       * BA_3 decides 1, and the fanout is never needed. */
      acasts[3] = 0xE3;
      n = bkr94acsAcast(processes[3], &acasts[3], acastOut);
      observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, 0, -1);
      CHECK(fDrive(processes, obs, cursors, -1, -1, 500,
                   &tolSweeps, &fanActs) == 0,
            "F2: all four complete inside the patience window");
      CHECK(fanActs == 0, "F2: completion without any enter-0");
      for (p = 0; p < 4; ++p) {
        unsigned char subset[4];

        CHECK(bkr94acsSubset(processes[p], subset) == 4,
              "F2: |SubSet| == 4 -- the delayed honest process included");
        CHECK(bkr94acsBaDecision(processes[p], 3) == 1,
              "F2: delayed process's BA decided 1");
        CHECK(bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_MET,
              "F2: duty MET with nothing given up");
        CHECK(bkr94acsFanout(processes[p], fout) == 0,
              "F2: fanout at MET outputs nothing");
      }
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("F3: finite patience completes past a dead slot");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct bkr94acsAct acastOut[1];
    unsigned int tolSweeps, fanActs;
    unsigned int n, p;

    if (allocCluster(processes, 4, 1, 0, 8) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) bracha87RetryInit(&cursors[p]);
      qReset();

      /* Process 3 is DEAD, not delayed: it never A-Casts and never
       * runs.  TOLERANCE cannot resolve on its own -- nothing can
       * enter BA_3 with 1 -- so the patience is a pure tax here, and
       * firing after it is what completes the instance.  This is why
       * the patience must be bounded: slow and dead are locally
       * indistinguishable, and only the fanout ends the wait. */
      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0xE0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, 3);
      }

      CHECK(fDrive(processes, obs, cursors, 3, 3, 500,
                   &tolSweeps, &fanActs) == 0,
            "F3: three live processes complete past the dead slot");
      CHECK(tolSweeps > 3, "F3: the full patience was waited out");
      CHECK(fanActs == 3, "F3: one enter-0 act per live process");
      for (p = 0; p < 3; ++p) {
        unsigned char subset[4];

        CHECK(bkr94acsSubset(processes[p], subset) == 3,
              "F3: |SubSet| == 3");
        CHECK(bkr94acsBaDecision(processes[p], 3) == 0,
              "F3: dead slot decided 0");
        CHECK(bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_MET,
              "F3: duty MET after the patience-elapsed fanout");
      }
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("F4: the fanout's patience elapses before the abandon gate");
  /* ---------------------------------------------------------------- */
  /*  Grounding: bkr94acs.h's two-sizings block -- "a patience clock  */
  /*  and a barren clock advance on the same boundaries, and patience */
  /*  that does not expire strictly before the abandon gate fires the */
  /*  decision into a caller that is already leaving ... Size that    */
  /*  gate above the patience and the FANOUT's ordering is            */
  /*  structural: its window opens on a BA output of 1, an act the    */
  /*  caller counts as progress" -- and BPR.md's Budget discipline    */
  /*  and Abandon Boundary.                                           */
  /*                                                                  */
  /*  fbDrive runs the harness's barren-sweep policy beside the       */
  /*  fanout's patience on F1's schedule, charging both at the same   */
  /*  boundary, and checks the two structural halves there: the       */
  /*  window opens on a BA_DECIDED act, and the barren count trails   */
  /*  the patience at every boundary the window stands.  What is left */
  /*  for this block is the SIZING -- the paired endings.             */
  /* ---------------------------------------------------------------- */
  {
    unsigned int pMax[2], bMax[2], incl[2], fan[2], aband[2];
    int rc[2];

    /* The derived sizing: a patience of 12 passes with the gate at
     * twice that, and the delayed A-Cast released inside the window.
     * The patience is never spent out here -- the recovery it was
     * bought for arrives first -- and the gate stays far from
     * firing throughout. */
    rc[0] = fbDrive(12, 24, 6, 200000,
                    &pMax[0], &bMax[0], &incl[0], &fan[0], &aband[0]);

    /* The control: the same schedule and the same patience with the
     * gate sized AT OR BELOW it.  Nothing about the machine changed;
     * the caller's own two numbers did, and the run ends the other
     * way. */
    rc[1] = fbDrive(12, 3, 6, 200000,
                    &pMax[1], &bMax[1], &incl[1], &fan[1], &aband[1]);

    CHECK(rc[0] == 0,
          "F4: at abandon = 2 x patience every process completes");
    CHECK(aband[0] == 0, "F4: and none of them abandons");
    CHECK(incl[0] == 1,
          "F4: the delayed honest process keeps its participation");
    CHECK(fan[0] == 0, "F4: no enter-0 -- the recovery landed first");
    CHECK(bMax[0] < pMax[0],
          "F4: the barren count never caught the patience");
    CHECK(bMax[0] < 24, "F4: and never came near the derived gate");

    CHECK(rc[1] != 0,
          "F4 control: at abandon <= patience the run does not complete");
    CHECK(aband[1] != 0, "F4 control: a process reached S barren sweeps");
    CHECK(bMax[1] >= 3, "F4 control: the gate fired at its own S");
    CHECK(pMax[1] <= 12,
          "F4 control: with the patience still unelapsed");
    CHECK(fan[1] == 0, "F4 control: so the fanout never fired");
    CHECK(incl[1] == 0, "F4 control: no subset was ever agreed there");

    printf("      F4: derived rc %d patience %u barren %u included %u"
           " fanout %u abandoned %u; control rc %d patience %u barren %u"
           " included %u fanout %u abandoned %u\n",
           rc[0], pMax[0], bMax[0], incl[0], fan[0], aband[0],
           rc[1], pMax[1], bMax[1], incl[1], fan[1], aband[1]);
  }

  /* ---------------------------------------------------------------- */
  /*  Section G -- Round-turn pacing (bkr94acsTurnDuty/bkr94acsTurn)  */
  /*                                                                  */
  /*  Section F isolates the fanout's pacing; this isolates the BA    */
  /*  round turn's.  The arrival path banks evidence and decides      */
  /*  nothing (G1); a round complete at n-t validated moves nothing  */
  /*  until the caller calls, then fires (G2); a round complete at   */
  /*  all n -- the full sample, nothing left to wait for -- fires    */
  /*  free (G3); and a zero-patience drained instance is             */
  /*  turn-quiescent (G4).                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("G1: deliveries alone decide nothing");
  /* ---------------------------------------------------------------- */
  {
    /* A full honest exchange at n=4 t=1 with every wire delivered and
     * NO turn called.  Per bkr94acs.h the inputs store, validate and
     * cascade; BA_DECIDED / COMPLETE / BA_EXHAUSTED emerge only from
     * bkr94acsTurn.  The fanout is not called either -- step 2 needs
     * n-t BAs decided 1, which no delivery can produce. */
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    struct wire w;
    unsigned int n, p, b;
    unsigned int turnable = 0;

    if (allocCluster(processes, 4, 1, 0, 8) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      qReset();

      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0xF0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      while (qSize() > 0) {
        qPopHead(&w);
        if (w.cls == BKR94ACS_CLS_ACAST) {
          n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                    wireAnnot(&w),
                                    w.from, w.value, out);
          CHECK(n <= 3, "G1: A-Cast input within its bound");
        } else {
          n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                     w.initiator, w.type, wireAnnot(&w), w.from,
                                     w.baValue, out);
          CHECK(n <= 2, "G1: BA input within its bound (echo/ready only)");
        }
        observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
      }

      for (p = 0; p < 4; ++p) {
        CHECK(!processes[p]->complete,
              "G1: no process completes without a turn");
        CHECK(obs[p].completeCount == 0, "G1: no COMPLETE observed");
        for (b = 0; b < 4; ++b) {
          CHECK(obs[p].baDecidedCount[b] == 0, "G1: no BA_DECIDED observed");
          CHECK(obs[p].exhaustedCount[b] == 0,
                "G1: no BA_EXHAUSTED observed");
          CHECK(bkr94acsBaDecision(processes[p], (unsigned char)b) == 0xFF,
                "G1: every BA still undecided");
          if (bkr94acsTurnDuty(processes[p], (unsigned char)b)
              != BKR94ACS_DUTY_HELD)
            ++turnable;
        }
      }
      /* The evidence IS banked -- the duty query says turns are owed.
       * Without this arm the section would pass on a machine that
       * simply ate the exchange. */
      CHECK(turnable > 0,
            "G1: turns owed after the exchange (evidence banked)");

      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("G2: TOLERANCE is enabled -- the turn fires when called");
  /* ---------------------------------------------------------------- */
  {
    /* Three of BA_0's four round-0 Fig1s accept, all carrying the same
     * value: the round is complete at n-t = 3 validated but the sample
     * can still grow to n, so the turn is enabled and waiting is still
     * worth something -- TOLERANCE.  Whether to wait is the caller's;
     * until it calls, nothing moves. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned int dummy = 0;
    unsigned int sentBefore;
    unsigned int sawNextRound = 0;
    unsigned int n, k;

    sz = bkr94acsSz(3, 0, 8);
    a = calloc(1, sz);
    if (!a) goto g2_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (k = 0; k < 3; ++k)
      feedBAAccept(a, 0, 0, (unsigned char)k, 1, out, 0, &dummy);

    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_TOLERANCE,
          "G2: duty TOLERANCE at n-t of n validated");

    /* The duty query is read-only: asking moves nothing. */
    sentBefore = bkr94acsFig1SentCount(a);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_TOLERANCE,
          "G2: the query left the duty unchanged");
    CHECK(bkr94acsFig1SentCount(a) == sentBefore,
          "G2: the query started no round");
    CHECK(bkr94acsBaDecision(a, 0) == 0xFF, "G2: BA_0 still undecided");
    CHECK(a->complete == 0, "G2: not complete");

    n = bkr94acsTurn(a, 0, out);
    CHECK(n > 0, "G2: turn fires at TOLERANCE when called");
    CHECK(n <= 3, "G2: turn outputs at most 3 acts");
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_BA_SEND
       && out[k].type == BRACHA87_INITIAL
       && out[k].initiator == 0
       && out[k].round == 1)
        ++sawNextRound;
    CHECK(sawNextRound == 1,
          "G2: the fired turn broadcasts the next round's INITIAL");

    free(a);
  }
  g2_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("G3: MET fires free");
  /* ---------------------------------------------------------------- */
  {
    /* Same construction with the fourth Fig1 accepted too: the round is
     * complete with ALL n validated, so waiting buys nothing and the
     * turn is free -- a caller needs no patience to call it. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned int dummy = 0;
    unsigned int n, k;

    sz = bkr94acsSz(3, 0, 8);
    a = calloc(1, sz);
    if (!a) goto g3_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (k = 0; k < 4; ++k)
      feedBAAccept(a, 0, 0, (unsigned char)k, 1, out, 0, &dummy);

    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_MET,
          "G3: duty MET with all n validated");
    n = bkr94acsTurn(a, 0, out);
    CHECK(n > 0, "G3: MET turn fires");
    CHECK(n <= 3, "G3: turn outputs at most 3 acts");

    /* One turn per call: round 1 has no messages, so the duty drops
     * back to HELD and a second call outputs nothing. */
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_HELD,
          "G3: next round incomplete -- duty back to HELD");
    CHECK(bkr94acsTurn(a, 0, out) == 0,
          "G3: nothing left to turn");

    free(a);
  }
  g3_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("G4: turns are quiescent at completion");
  /* ---------------------------------------------------------------- */
  {
    /* A zero-patience drained convergence (runHonest turns after every
     * delivery).  Post-decide continuation runs the turns past DECIDE
     * through the phase after the decision, so at quiescence every BA is
     * either out of rounds or has no complete round left: HELD
     * everywhere, and a further turn outputs nothing. */
    unsigned int nAct = 4, t = 1, vLen = 1, mp = 10;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned int p, b;

    if (allocCluster(processes, nAct, t, vLen - 1, mp) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < nAct; ++i)
        acasts[i * vLen] = (unsigned char)(0x60 + i);

      runHonest(nAct, vLen, acasts, 0 /*ordered*/, processes, obs);

      for (p = 0; p < nAct; ++p) {
        CHECK(processes[p]->complete, "G4: the zero-patience drain converged");
        /* The same exchange G1 ran, with turns: the acts G1 never saw
         * are all here, so the HELD reading below is quiescence and
         * not an inert machine. */
        CHECK(obs[p].completeCount == 1, "G4: COMPLETE observed once");
        for (b = 0; b < nAct; ++b) {
          CHECK(obs[p].baDecidedCount[b] == 1,
                "G4: BA_DECIDED observed once per BA");
          CHECK(bkr94acsTurnDuty(processes[p], (unsigned char)b)
                == BKR94ACS_DUTY_HELD,
                "G4: every TurnDuty HELD at quiescence");
          CHECK(bkr94acsTurn(processes[p], (unsigned char)b, out) == 0,
                "G4: re-calling Turn outputs nothing");
        }
      }
      freeCluster(processes, nAct);
    }
  }

  /* ================================================================ */
  /*  Section H -- quiescence is REACHABLE at the ACS surface         */
  /* ================================================================ */
  /*  bkr94acs.h, bkr94acsRetryStep: 0 means "every sent instance has     */
  /*  retired all its retries -- quiescence."  bracha87.h's retry     */
  /*  banner makes that a pair of remote facts: every process has     */
  /*  announced its accept AND holds this one's.  Only the second     */
  /*  needs a wire annotation this layer did not have, so H drives a  */
  /*  cluster past COMPLETE to the 0 return at every process.  The    */
  /*  drive round-trips both READY bits (observeAndOutput /           */
  /*  runWithRetry above); the RECEIVED half is what makes 0          */
  /*  reachable rather than merely hoped for.                         */
  /* ---------------------------------------------------------------- */
  BANNER("H1: every process reaches the Retry 0 return");
  {
    struct bkr94acs *processes[MAX_PROCESSES];
    struct processObs obs[MAX_PROCESSES];
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    unsigned char acasts[4];
    struct wire w;
    unsigned int quiesced[MAX_PROCESSES];
    unsigned int nQuiesced;
    unsigned int iter;
    unsigned int p;
    unsigned int n;
    unsigned int drop;
    unsigned int di;
    unsigned int callsInPass[MAX_PROCESSES];
    unsigned int passSweeps[MAX_PROCESSES];
    unsigned int boundBroken;
    static const unsigned int drops[] = { 0, 25 };

    /* Lossless first, then a fair-loss drive.  Loss is where the claim
     * bites: a marked re-send can itself be dropped, and its target's
     * next unmarked re-send has to re-arm the one that replaces it.
     * Alongside, the diagnostic bound bkr94acs.h states at
     * bkr94acsFig1SentCount -- "A pass costs AT MOST this many calls"
     * -- is checked on every completed pass: the calls the pass took,
     * against the sent count read as it closes (the count only grows,
     * so the closing read is the pass's ceiling) plus one, since the
     * call that crosses the wrap is charged to the pass it closes
     * while the instance it returns on belongs to the next. */
    for (di = 0; di < sizeof (drops) / sizeof (drops[0]); ++di) {
    drop = drops[di];
    rngSeed(0x5A5A00u + drop);
    if (allocCluster(processes, 4, 1, 0, 4) == 0) {
      qReset();
      boundBroken = 0;
      for (p = 0; p < 4; ++p) {
        obsInit(&obs[p]);
        bracha87RetryInit(&cursors[p]);
        quiesced[p] = 0;
        callsInPass[p] = 0;
        passSweeps[p] = 0;
      }
      /* Process 3's A-Cast is held back one drain -- the WAN-laggard
       * shape.  It is what puts the processes on different cursor
       * phases, which is the schedule that strands a bare acFrom
       * suppress: the laggard records the others' accepts before its
       * own announcement can leave. */
      nQuiesced = 0;
      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0xA0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, drop, -1);
      }
      for (iter = 0; iter < 20000 && nQuiesced < 4; ++iter) {
        while (qSize() > 0) {
          qPopHead(&w);
          if (w.cls == BKR94ACS_CLS_ACAST) {
            n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                      wireAnnot(&w),
                                      w.from, w.value, out);
            /* Leaving the rotation is PROVISIONAL: an unmarked READY is
             * exactly the evidence that something is still owed, and a
             * process that has stopped ticking can never re-send.
             * Re-enter. */
            if (w.type == BRACHA87_READY && !w.received) {
              if (quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
            }
          } else {
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                       w.initiator, w.type, wireAnnot(&w), w.from,
                                       w.baValue, out);
            if (w.type == BRACHA87_READY && !w.received) {
              if (quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
            }
          }
          observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, drop, -1);
        }
        if (iter == 1) {
          acasts[3] = 0xA3;
          n = bkr94acsAcast(processes[3], &acasts[3], acastOut);
          observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, drop, -1);
        }
        for (p = 0; p < 4; ++p) {
          unsigned int b;

          if (!quiesced[p]) {
            n = bkr94acsRetryStep(processes[p], &cursors[p], out);
            ++callsInPass[p];
            if (cursors[p].sweeps != passSweeps[p]) {
              if (callsInPass[p] > bkr94acsFig1SentCount(processes[p]) + 1)
                ++boundBroken;
              passSweeps[p] = cursors[p].sweeps;
              callsInPass[p] = 0;
            }
            if (!n && bkr94acsFig1SentCount(processes[p])) {
              quiesced[p] = 1;
              ++nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, drop, -1);
          }
          for (b = 0; b < 4; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b,
                                     out)) > 0 && turnDrained()) {
              if (quiesced[p]) {
                quiesced[p] = 0;
                --nQuiesced;
              }
              observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, drop, -1);
            }
          n = bkr94acsFanout(processes[p], out);
          if (n) {
            if (quiesced[p]) {
              quiesced[p] = 0;
              --nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, drop, -1);
          }
        }
      }
      CHECK(nQuiesced == 4, "H1: every process reached the Retry 0 return");
      CHECK(boundBroken == 0,
            "H1: no pass cost more calls than bkr94acsFig1SentCount"
            " (plus the call that crosses the wrap)");
      for (p = 0; p < 4; ++p) {
        CHECK(processes[p]->complete, "H1: quiescence past COMPLETE");
        CHECK(bkr94acsRetryStep(processes[p], &cursors[p], out) == 0,
              "H1: and the 0 return is stable");
      }
      CHECK(qSize() == 0, "H1: the wire is silent at quiescence");
      freeCluster(processes, 4);
    }
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("H3: the two caller obligations, forfeited");
  /* ---------------------------------------------------------------- */
  /* The header states two consequences and H1 shows only their        */
  /* positives.  bkr94acs.h at bkr94acsAcastInput: "a caller that      */
  /* parks a process on bkr94acsRetryStep's quiescent 0 return must    */
  /* un-park it when an unmarked READY arrives ... Skipping the        */
  /* re-entry ... forfeits the whole fair-loss recovery the moment a   */
  /* marked re-send is dropped: its target re-sends unmarked forever   */
  /* into a process that has stopped listening"; and "0 IS NOT A       */
  /* NEUTRAL ANNOT.  On a READY the ABSENCE of RECEIVED is itself a    */
  /* claim ... and it arms the re-send ... passing 0 instead re-arms   */
  /* every post-accept READY, so the suppress mask never holds and the */
  /* READY retire never converges".                                    */
  /*                                                                   */
  /* Lanes 0 and 1 are the forfeit and its control.  The loss is       */
  /* placed, not drawn: every READY carrying process 3's announcement  */
  /* toward process 0 is dropped until 3 first parks, and the wire is  */
  /* lossless otherwise.  3 sweeps twice per iteration -- a park can   */
  /* only precede the unmarked READY that should re-open it when the   */
  /* parker's pass closes between two of the other's re-sends, which   */
  /* lock-step equal sweeps never allow (the exchange re-arms 3 every  */
  /* pass and it never returns 0).  Lane 0 never re-enters a parked    */
  /* process on an unmarked READY (turns and the fanout re-enter as in */
  /* H1, and take nothing here); lane 1 re-enters as H1 does, and      */
  /* quiesces in H1's lossless iteration count.  Lane 2 strips RECEIVED alone from      */
  /* every READY (ACCEPTED still carried): the arm, not a missing      */
  /* announcement, is what then holds every mask open.                 */
  {
    struct bkr94acs *processes[MAX_PROCESSES];
    struct processObs obs[MAX_PROCESSES];
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    unsigned char acasts[4];
    struct wire w;
    unsigned int quiesced[MAX_PROCESSES];
    unsigned int nQuiesced;
    unsigned int iter;
    unsigned int p;
    unsigned int n;
    unsigned int lane;
    unsigned int dropped;
    unsigned int parkedOnce;
    unsigned int owed;

    for (lane = 0; lane < 3; ++lane) {
      dropped = 0;
      parkedOnce = 0;
      if (allocCluster(processes, 4, 1, 0, 4) != 0)
        continue;
      qReset();
      for (p = 0; p < 4; ++p) {
        obsInit(&obs[p]);
        bracha87RetryInit(&cursors[p]);
        quiesced[p] = 0;
      }
      nQuiesced = 0;
      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0xF0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }
      for (iter = 0; iter < 20000 && nQuiesced < 4; ++iter) {
        while (qSize() > 0) {
          qPopHead(&w);
          /* The loss: every READY carrying process 3's announcement
           * toward process 0, until 3 parks -- the shape in which the
           * lost re-send is the one the target was waiting for.  After
           * 3 parks the wire is lossless. */
          if (lane < 2 && !parkedOnce && w.type == BRACHA87_READY
           && w.accepted && w.from == 3 && w.to == 0) {
            ++dropped;
            continue;
          }
          if (lane == 2)
            w.received = 0;                       /* RECEIVED never carried */
          if (w.cls == BKR94ACS_CLS_ACAST)
            n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                   wireAnnot(&w), w.from, w.value, out);
          else
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                w.initiator, w.type, wireAnnot(&w), w.from,
                                w.baValue, out);
          /* Re-entry on an unmarked READY: the obligation lane 0 drops. */
          if (lane != 0 && w.type == BRACHA87_READY && !w.received
           && quiesced[w.to]) {
            quiesced[w.to] = 0;
            --nQuiesced;
          }
          observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
        }
        for (p = 0; p < 4; ++p) {
          unsigned int b;
          unsigned int ticks;

          /* Process 3 sweeps twice per iteration in the loss lanes: a
           * faster sweeper's pass can close between two of a slower
           * process's re-sends, which is when a park can precede the
           * unmarked READY that should re-open it. */
          for (ticks = (lane < 2 && p == 3) ? 2 : 1; ticks && !quiesced[p];
               --ticks) {
            n = bkr94acsRetryStep(processes[p], &cursors[p], out);
            if (!n && bkr94acsFig1SentCount(processes[p])) {
              quiesced[p] = 1;
              ++nQuiesced;
              if (p == 3)
                parkedOnce = 1;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          }
          for (b = 0; b < 4; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained()) {
              if (quiesced[p]) {
                quiesced[p] = 0;
                --nQuiesced;
              }
              observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
            }
          n = bkr94acsFanout(processes[p], out);
          if (n) {
            if (quiesced[p]) {
              quiesced[p] = 0;
              --nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          }
        }
      }
      for (p = 0; p < 4; ++p)
        CHECK(processes[p]->complete, "H3: the protocol completes regardless");
      if (lane < 2)
        CHECK(dropped && parkedOnce,
              "H3: the announcement was lost and the faster sweeper parked");
      if (lane == 1) {
        CHECK(nQuiesced == 4,
              "H3: with re-entry the parked process re-sends and all quiesce");
      } else if (lane == 0) {
        /* A parked process is still owed a READY: some un-parked
         * survivor's READY mask on a served instance -- A-Cast or BA
         * -- lacks a parked process. */
        owed = 0;
        for (p = 0; p < 4; ++p) {
          unsigned int j, q, r, i;

          if (quiesced[p])
            continue;
          for (j = 0; j < 4; ++j)
            for (r = 0; r <= 12; ++r)
              for (i = 0; i < 4; ++i) {
                const struct bracha87Fig1 *f1;
                const unsigned char *sk;

                if (r == 12) {
                  if (i)
                    break;
                  if (bkr94acsBaDecision(processes[p], (unsigned char)j) == 0)
                    continue;
                  f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j);
                } else
                  f1 = bkr94acsBaFig1(processes[p], (unsigned char)j,
                                      (unsigned char)r, (unsigned char)i);
                if (!f1 || !bracha87Fig1Value(f1))
                  continue;
                sk = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
                for (q = 0; q < 4; ++q)
                  if (quiesced[q] && sk && !BRACHA87_SKIP_TST(sk, q))
                    ++owed;
              }
        }
        CHECK(nQuiesced < 4,
              "H3: without re-entry the lost announcement strands a process for good");
        CHECK(owed > 0,
              "H3: a parked process is still owed a READY it will never re-send for");
      } else {
        CHECK(nQuiesced == 0,
              "H3: with RECEIVED never carried no process reaches the Retry 0 return");
        /* And what holds it open is the arm: no accepted A-Cast
         * instance's READY mask holds a single bit. */
        owed = 0;
        for (p = 0; p < 4; ++p) {
          unsigned int j, q;

          for (j = 0; j < 4; ++j) {
            const struct bracha87Fig1 *f1;
            const unsigned char *sk;

            f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j);
            if (!f1 || !(f1->flags & BRACHA87_F1_ACCEPTED))
              continue;
            sk = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
            for (q = 0; q < 4; ++q)
              if (sk && BRACHA87_SKIP_TST(sk, q))
                ++owed;
          }
        }
        CHECK(owed == 0,
              "H3: and no READY suppress bit ever holds, re-armed every pass");
      }
      printf("      H3 lane %u (%s): %u iterations, %u quiesced of 4\n", lane,
             lane == 0 ? "announcement lost until the park, no re-entry"
             : lane == 1 ? "announcement lost until the park, re-entry"
             : "RECEIVED never carried", iter, nQuiesced);
      freeCluster(processes, 4);
    }
  }

  BANNER("H2: the annotation ingress contracts, now on the Input entries");
  {
    struct bkr94acs *processes[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PROCESSES - 1)];
    unsigned char val[1];

    if (allocCluster(processes, 4, 1, 0, 4) == 0) {
      val[0] = 1;
      /* Defensive: an unmarked READY (annot with no BKR94ACS_RECEIVED)
       * carrying a null state or any out-of-range index is refused with
       * 0 acts and routes nothing.  The arm rides the same entry as the
       * message, so it inherits that entry's guards rather than needing
       * its own. */
      CHECK(bkr94acsAcastInput(0, 0, BRACHA87_READY, 0, 0, val, out) == 0,
            "H2: null state refused");
      CHECK(bkr94acsAcastInput(processes[0], 99, BRACHA87_READY, 0, 0,
                                  val, out) == 0,
            "H2: out-of-range process refused");
      CHECK(bkr94acsAcastInput(processes[0], 0, BRACHA87_READY, 0, 99,
                                  val, out) == 0,
            "H2: out-of-range from refused");
      CHECK(bkr94acsBaInput(0, 0, 0, 0, BRACHA87_READY, 0, 0, 0, out) == 0,
            "H2: BA null state refused");
      CHECK(bkr94acsBaInput(processes[0], 99, 0, 0, BRACHA87_READY, 0, 0,
                                   0, out) == 0,
            "H2: BA out-of-range process refused");
      CHECK(bkr94acsBaInput(processes[0], 0, 250, 0, BRACHA87_READY, 0, 0,
                                   0, out) == 0,
            "H2: BA out-of-range round refused");
      CHECK(bkr94acsBaInput(processes[0], 0, 0, 99, BRACHA87_READY, 0, 0,
                                   0, out) == 0,
            "H2: BA out-of-range initiator refused");
      CHECK(bkr94acsBaInput(processes[0], 0, 0, 0, BRACHA87_READY, 0, 99,
                                   0, out) == 0,
            "H2: BA out-of-range from refused");
      /* Pre-accept an arm records nothing, so an A-Cast that nobody has
       * accepted still suppresses nobody and marks nobody -- there is
       * no state for a forged unmarked READY to disturb. */
      bkr94acsAcastInput(processes[0], 1, BRACHA87_READY, 0, 2, val, out);
      CHECK(bkr94acsAcastReadied(processes[0], 1) != 0,
            "H2: the A-Cast readied set is unaffected by an arm");
      freeCluster(processes, 4);
    }
  }

  /* ================================================================ */
  /*  Section I -- partition heal                                     */
  /* ================================================================ */
  /*  Grounding: bracha87.h's BPR retry banner (READY never retires on */
  /*  local state; INITIAL retires at ACCEPTED or all-echoed; ECHO at  */
  /*  ACCEPTED); Bracha87.txt Fig 1 rows 3/5/6 -- the ready-driven     */
  /*  re-bootstrap chain, which closes because n-t >= 2t+1 exactly     */
  /*  when n >= 3t+1; BKR94ACS.txt's t-resilience.                     */
  /*  Cross-reference label: README "Abandonment" / Partition.         */
  /*                                                                  */
  /*  The side still holding n-t correct processes runs to COMPLETE    */
  /*  without the cut-off one.  If the partition heals while the       */
  /*  others are still draining and ticking, their never-retired READY */
  /*  retries carry the returning process to the same subset -- READY  */
  /*  ALONE, since the value rides with it and the t+1-readys rule     */
  /*  re-bootstraps the INITIAL and ECHO it never saw.                 */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("I1: READY re-sends alone carry a returner holding zero evidence");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    struct iWitness wit;
    unsigned char side[4];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int healInitials, healEchoes, healReadys;
    unsigned int decidedZero;
    unsigned int tick, n, p, j, k, sz0, szP, pass;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
        side[p] = 0;
      }
      qReset();
      memset(&wit, 0, sizeof (wit));
      wit.watchTo = 3;
      wit.watchProcess = 2;

      /* Pre-cut: processes 0 and 1 A-Cast and the whole cluster,
       * process 3 included, banks their evidence. */
      for (p = 0; p < 2; ++p) {
        acasts[p] = (unsigned char)(0x50 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }
      for (tick = 0; tick < 500; ++tick) {
        iTick(processes, obs, cursors, pol, side, 0);
        if (bkr94acsAcastValue(processes[3], 0)
         && bkr94acsAcastValue(processes[3], 1))
          break;
      }
      CHECK(tick < 500, "I1: the returner holds real pre-cut evidence");

      /* THE CUT.  Process 3 is off the network in both directions.
       * Processes 2 and 3 A-Cast only now, so process 3 holds ZERO
       * evidence of A-Cast 2 -- not one INITIAL, not one echo. */
      side[3] = 1;
      wit.armed = 1;
      for (p = 2; p < 4; ++p) {
        acasts[p] = (unsigned char)(0x50 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }
      for (tick = 0; tick < 20000; ++tick) {
        iTick(processes, obs, cursors, pol, side, &wit);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete)
          break;
      }
      CHECK(tick < 20000, "I1: the n-t side completes while the cut stands");
      CHECK(!processes[3]->complete,
            "I1: the cut-off process completes nothing");
      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "I1: |SubSet| >= n-t on the surviving side");
      for (p = 1; p < 3; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "I1: survivor SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "I1: survivor SubSet contents byte-identical");
      }
      decidedZero = 0;
      for (j = 0; j < 4; ++j)
        if (!bkr94acsBaDecision(processes[0], (unsigned char)j))
          ++decidedZero;
      CHECK(decidedZero > 0,
            "I1: the survivors decided at least one BA 0");

      /* Carry the surviving side past COMPLETE to rest, so the guard
       * below reads a settled state rather than a cascade in flight.
       * Resting costs the carry nothing: READY never retires on local
       * state, so the re-sends stand until the survivors' own gates
       * fire -- which is exactly the window this heal lands in. */
      for (tick = 0; tick < 20000; ++tick) {
        iTick(processes, obs, cursors, pol, side, &wit);
        if (pol[0].barren && pol[1].barren && pol[2].barren)
          break;
      }
      CHECK(tick < 20000, "I1: the surviving side comes to rest");

      /* THE CONSTRUCTION GUARD, read at the instant of heal: accepted
       * implies retired (bracha87.h's retire conditions), so a full
       * survivor sweep across the healed link carries READY re-sends
       * and nothing else. */
      side[3] = 0;
      healInitials = 0;
      healEchoes = 0;
      healReadys = 0;
      for (p = 0; p < 3; ++p) {
        pass = bkr94acsFig1SentCount(processes[p]);
        for (tick = 0; tick < pass; ++tick) {
          n = bkr94acsRetryStep(processes[p], &cursors[p], out);
          for (k = 0; k < n; ++k) {
            if (out[k].type == BRACHA87_INITIAL)
              ++healInitials;
            else if (out[k].type == BRACHA87_ECHO)
              ++healEchoes;
            else if (out[k].type == BRACHA87_READY)
              ++healReadys;
          }
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }
      }
      CHECK(healReadys > 0, "I1: the healed link carries READY re-sends");
      CHECK(healInitials == 0 && healEchoes == 0,
            "I1: and nothing else -- INITIAL and ECHO have retired");

      /* THE HEAL.  The returner runs the whole application-loop
       * discipline from here: retry, turns, fanout under its own duty.
       * Its own step 2 is held only until its own A-Cast comes back
       * accepted -- patience that has not elapsed -- so its entry into
       * its OWN BA is step 1's 1 and not step 2's 0.  That is the
       * interesting case for the decision check below. */
      for (tick = 0; tick < 20000; ++tick) {
        iTick(processes, obs, cursors, pol, side, &wit);
        if (processes[3]->complete)
          break;
      }
      CHECK(tick < 20000, "I1: the returner reaches COMPLETE");

      /* THE WITNESS.  For the zero-evidence instance the returner took
       * no INITIAL and no foreign ECHO at all; a READY input drew its
       * own ECHO out of it (row 3), it proceeded to ACCEPT, and the
       * all-echoed gate never closed there. */
      CHECK(wit.initials == 0,
            "I1: the returner took zero INITIAL inputs for the instance");
      CHECK(wit.foreignEchoes == 0,
            "I1: and zero ECHO inputs from any other process");
      CHECK(wit.rowThree > 0,
            "I1: a READY input drew its own ECHO out (Fig 1 row 3)");
      CHECK(bkr94acsAcastValue(processes[3], 2) != 0,
            "I1: and the instance reached ACCEPT at the returner");
      CHECK(bracha87Fig1AllEchoed(bkr94acsAcastFig1(processes[3], 2)) == 0,
            "I1: bracha87Fig1AllEchoed stays 0 there");

      /* The returner lands on the identical subset, and where the
       * survivors decided a BA 0 its decision matches -- even where
       * its own entered value was 1. */
      szP = bkr94acsSubset(processes[3], subsetP);
      CHECK(szP == sz0, "I1: the returner's SubSet size matches");
      if (szP == sz0)
        CHECK(memcmp(subset0, subsetP, sz0) == 0,
              "I1: the returner's SubSet is byte-identical");
      for (j = 0; j < 4; ++j)
        CHECK(bkr94acsBaDecision(processes[3], (unsigned char)j)
              == bkr94acsBaDecision(processes[0], (unsigned char)j),
              "I1: every BA decision matches the survivors'");
      /* Where the survivors decided a BA 0, the returner's decision
       * matches -- its own A-Cast's BA included, whichever value it
       * itself entered there. */
      CHECK(bkr94acsBaDecision(processes[3], 3) == 0,
            "I1: its own BA decides 0 with the survivors");
      CHECK(bkr94acsBaEntered(processes[3], 3) == 1,
            "I1: and it did enter a value of its own into that BA");
      printf("      I1: |SubSet| %u, %u BA(s) decided 0, returner entered"
             " %u into its own BA, %u row-3 echoes\n",
             sz0, decidedZero, (unsigned)obs[3].selfInputValue[3],
             wit.rowThree);

      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("I2: a 2/2 cut leaves neither side n-t, and heals");
  /* ---------------------------------------------------------------- */
  {
    /* Neither side holds n-t = 3, so no threshold anywhere can close.
     * That NOTHING completes is a STANDING fact re-asserted across a
     * bounded further drive, not a moment; the heal then converges the
     * whole cluster on one subset. */
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct acastOut[1];
    unsigned char side[4];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int standing;
    unsigned int tick, n, p, sz0, szP;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
        side[p] = (unsigned char)(p >> 1);
      }
      qReset();

      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0x60 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      standing = 0;
      for (tick = 0; tick < 400; ++tick) {
        iTick(processes, obs, cursors, pol, side, 0);
        for (p = 0; p < 4; ++p)
          if (processes[p]->complete)
            ++standing;
      }
      CHECK(standing == 0,
            "I2: no process completes while the 2/2 cut stands");
      for (p = 0; p < 4; ++p)
        CHECK(bkr94acsBaDecision(processes[p], (unsigned char)p) == 0xFF,
              "I2: and no BA has decided anywhere");

      side[2] = 0;
      side[3] = 0;
      for (tick = 0; tick < 20000; ++tick) {
        iTick(processes, obs, cursors, pol, side, 0);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete && processes[3]->complete)
          break;
      }
      CHECK(tick < 20000, "I2: the healed cluster completes");
      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "I2: |SubSet| >= n-t after the heal");
      for (p = 1; p < 4; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "I2: SubSet sizes agree after the heal");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "I2: SubSet contents byte-identical after the heal");
      }
      printf("      I2: healed at |SubSet| %u\n", sz0);

      freeCluster(processes, 4);
    }
  }

  /* ================================================================ */
  /*  Section J -- asymmetric flow                                    */
  /* ================================================================ */
  /*  Grounding: bracha87.h's fair-loss posture and its BPR retry     */
  /*  banner; bkr94acs.h's bkr94acsRetryStep (0 only on an idle sweep),   */
  /*  bkr94acsFanoutDuty / bkr94acsFanout (the enter-0 path that      */
  /*  closes over an unheard A-Cast); BKR94ACS.txt steps 1-3.         */
  /*  Cross-reference label: README "Abandonment" / Asymmetric flow.  */
  /*                                                                  */
  /*  Fair loss promises nothing about symmetry, and the two halves   */
  /*  of one broken link see OPPOSITE evidence.  The lane is exactly  */
  /*  tight at n=4 t=1 -- the cut removes one participant from every  */
  /*  threshold -- so neither half carries additional loss.           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("J1: receive-only process completes and agrees");
  /* ---------------------------------------------------------------- */
  {
    /* Every wire process 3 SENDS is dropped; every wire addressed to
     * it is delivered.  It validates, enters, and runs all the way to
     * COMPLETE with the same subset as everyone else, while the other
     * three correctly count it among the t silent faults -- its own
     * A-Cast consistently excluded. */
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct acastOut[1];
    unsigned char subset0[4];
    unsigned char subsetP[4];
    unsigned int zeroRetries, barrenDrops, iters;
    unsigned int n, p, j, sz0, szP;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();

      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0x90 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      CHECK(jDrive(processes, obs, cursors, pol, 3 /*cutFrom*/, -1,
                   4000, &zeroRetries, &barrenDrops, &iters) == 0,
            "J1: every process completes under the egress cut");

      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "J1: |SubSet| >= n-t");
      for (p = 0; p < 4; ++p) {
        CHECK(processes[p]->complete, "J1: process completed");
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "J1: SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "J1: SubSet contents byte-identical");
        CHECK(bkr94acsBaDecision(processes[p], 3) == 0,
              "J1: the receive-only process's A-Cast decided 0 everywhere");
        /* The egress cut severs the loopback of the process's own
         * INITIAL, so at the receive-only process step 1 can never
         * enter BA_self -- its own fanout is the only route, and the
         * duty's MET reading is the evidence it took it.  This is the
         * half's directional fact: the mirror in J2 reads all-zero. */
        CHECK(bkr94acsFanoutDuty(processes[p]) == BKR94ACS_DUTY_MET,
              "J1: fanout duty MET -- nothing left unentered");
        for (j = 0; j < 4; ++j)
          CHECK(bkr94acsBaEntered(processes[p], (unsigned char)j) == 1,
                "J1: every BA entered, BA_self included");
      }

      /* Its own copy of every included value matches the others'. */
      for (j = 0; j < sz0; ++j) {
        const unsigned char *mine;
        const unsigned char *theirs;

        mine = bkr94acsAcastValue(processes[3], subset0[j]);
        theirs = bkr94acsAcastValue(processes[0], subset0[j]);
        CHECK(mine != 0 && theirs != 0 && mine[0] == theirs[0],
              "J1: the receive-only process holds the same member value");
      }
      printf("      J1: %u ticks, |SubSet| %u\n", iters, sz0);

      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("J2: send-only process feeds everyone and sees pure barrenness");
  /* ---------------------------------------------------------------- */
  {
    /* The mirror: every wire addressed to process 3 is dropped, its
     * own sends all land.  The others may well include its A-Cast in
     * the agreed subset; it observes no progress at all and leaves
     * through the barren gate, its value agreed on by everyone but
     * itself.  Both sides behaved correctly. */
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct acastOut[1];
    unsigned char subset1[4];
    unsigned char subsetP[4];
    unsigned int zeroRetries, barrenDrops, iters;
    unsigned int n, p, j, sz1, szP, included;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();

      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0xB0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      CHECK(jDrive(processes, obs, cursors, pol, -1, 3 /*cutTo*/,
                   4000, &zeroRetries, &barrenDrops, &iters) == 0,
            "J2: the fed cluster completes and the cut process reaches S");

      sz1 = bkr94acsSubset(processes[1], subset1);
      included = 0;
      for (j = 0; j < sz1; ++j)
        if (subset1[j] == 3)
          included = 1;
      CHECK(sz1 >= 3, "J2: |SubSet| >= n-t");
      CHECK(included,
            "J2: the send-only process's A-Cast is INCLUDED");
      for (p = 0; p < 3; ++p) {
        CHECK(processes[p]->complete, "J2: fed process completed");
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz1, "J2: SubSet sizes agree");
        if (szP == sz1)
          CHECK(memcmp(subset1, subsetP, sz1) == 0,
                "J2: SubSet contents byte-identical");
      }

      /* The cut process's own evidence stream.  Its subset is never
       * read -- bkr94acsSubset is contract-valid only after complete
       * -- and its outcome record is no-decision, empty membership. */
      CHECK(pol[3].progress == 0 && pol[3].barren == pol[3].sweeps,
            "J2: the cut process observed progress EXACTLY 0");
      CHECK(barrenDrops == 0,
            "J2: its barren counter climbs monotonically");
      CHECK(pol[3].barren >= BARREN_S,
            "J2: it reaches the policy's S -- the only exit it has");
      CHECK(bkr94acsFig1SentCount(processes[3]) == 1,
            "J2: SentFig1Count == 1 (its own A-Cast, nothing else sent)");
      CHECK(zeroRetries == 0,
            "J2: its Retry never idles -- the INITIAL retry never retires");
      CHECK(processes[3]->complete == 0,
            "J2: complete stays clear (no unilateral substitute)");
      /* The mirror of J1's directional fact: nothing ever arrived, so
       * neither step 1 nor step 2 ever touched a BA here. */
      CHECK(bkr94acsFanoutDuty(processes[3]) == BKR94ACS_DUTY_HELD,
            "J2: fanout duty HELD -- no BA decided 1 here");
      for (j = 0; j < 4; ++j)
        CHECK(bkr94acsBaEntered(processes[3], (unsigned char)j) == 0,
              "J2: no BA entered at the cut process");
      printf("      J2: %u ticks, |SubSet| %u, cut-process sweeps %u\n",
             iters, sz1, pol[3].sweeps);

      freeCluster(processes, 4);
    }
  }

  /* ================================================================ */
  /*  Section K -- Byzantine trickle                                  */
  /* ================================================================ */
  /*  Grounding: bracha87.h's per-sender dedup ("at most one ECHO and  */
  /*  one READY from each sender contribute to thresholds, regardless  */
  /*  of how many duplicates or differing-value copies arrive") and    */
  /*  the initiator block ("Only that initiator may send (initial, v); */
  /*  a non-initiator INITIAL is a forged broadcast the echo cascade   */
  /*  would carry to a false ACCEPT", with bkr94acsAcastInput /        */
  /*  bkr94acsBaInput enforcing from == process / initiator on the     */
  /*  caller's behalf); bkr94acs.h's Retry cursor walk for the         */
  /*  instance space (n A-Casts + n x R x n BA Fig 1s, R = maxPhases * */
  /*  BRACHA87_ROUNDS_PER_PHASE) and its SentFig1Count contract        */
  /*  (ahead-round INITIALs leave sent ECHOED instances).              */
  /*  Cross-reference label: README "Abandonment" / Byzantine trickle. */
  /*                                                                  */
  /*  A Byzantine process can aim at the abandonment gate itself,      */
  /*  feeding genuinely fresh state advances that lead nowhere.  The   */
  /*  supply is BOUNDED: per-sender dedup admits one echo and one      */
  /*  ready per sender per instance and the instance space is finite,  */
  /*  so the trickle stretches the gate and can never hold it open.    */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("K1: trickle stretches the gate, then exhausts");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct acastOut[1];
    struct wire w;
    unsigned char bogusA[MAX_VLEN];
    unsigned char bogusB[MAX_VLEN];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned char valBefore[MAX_PROCESSES];
    unsigned int sentBefore[K_HONEST];
    unsigned int actProducing[K_HONEST];
    unsigned int vLen = 4;
    unsigned int inst, ceiling, bResets, reActs, zeroRetries;
    unsigned int burst1, sz0, szP, delta, tick;
    unsigned int n, p, j, r, k;

    if (allocCluster(processes, 4, 1, vLen - 1, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();
      KSupplyN = 0;
      bResets = 0;
      reActs = 0;
      zeroRetries = 0;
      for (p = 0; p < K_HONEST; ++p)
        actProducing[p] = 0;
      memset(bogusA, 0, sizeof (bogusA));
      memset(bogusB, 0, sizeof (bogusB));
      for (k = 0; k < vLen; ++k) {
        bogusA[k] = (unsigned char)(0xC0 + k);
        bogusB[k] = (unsigned char)(0x40 + k);
      }

      /* ---- the forged non-initiator INITIALs -------------------- */
      /* Fed FIRST, while every target instance is still untouched:
       * an already-echoed instance has no Rule 1 left to fire, so a
       * later feed would be a no-op even against a machine that had
       * dropped the binding.  The correct machine drops all of them
       * for 0 acts, spending nothing against the ceiling. */
      memset(&w, 0, sizeof (w));
      for (j = 0; j < 4; ++j) {
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_BA;
        w.process = (unsigned char)j;
        w.round = 0;
        w.initiator = (unsigned char)((j + 1) % K_HONEST);
        w.type = BRACHA87_INITIAL;
        w.from = 3;
        w.baValue = 1;
        kSupplyAdd(&w);
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_BA;
        w.process = (unsigned char)j;
        w.round = 1;
        w.initiator = (unsigned char)((j + 2) % K_HONEST);
        w.type = BRACHA87_INITIAL;
        w.from = 3;
        w.baValue = 0;
        kSupplyAdd(&w);
      }
      for (j = 0; j < K_HONEST; ++j) {
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_ACAST;
        w.process = (unsigned char)j;
        w.type = BRACHA87_INITIAL;
        w.from = 3;
        memcpy(w.value, bogusA, vLen);
        kSupplyAdd(&w);
      }
      burst1 = KSupplyN;
      for (k = 0; k < burst1; ++k)
        for (p = 0; p < K_HONEST; ++p) {
          n = kDeliver(processes, obs, &KSupply[k], (unsigned char)p, vLen);
          CHECK(n == 0,
                "K1: a forged non-initiator INITIAL produces 0 acts");
          if (n)
            ++actProducing[p];
        }

      /* ---- the honest cluster runs ------------------------------ */
      memset(acasts, 0, sizeof (acasts));
      for (p = 0; p < K_HONEST; ++p) {
        for (k = 0; k < vLen; ++k)
          acasts[p * vLen + k] = (unsigned char)((p << 4) | k);
        n = bkr94acsAcast(processes[p], acasts + p * vLen, acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, vLen,
                       0, -1);
      }
      for (tick = 0; tick < 4000; ++tick) {
        kTick(processes, obs, cursors, pol, vLen, 0);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete)
          break;
      }
      CHECK(tick < 4000, "K1: the honest cluster completes alongside the trickler");
      /* Let the cluster go barren so the stretch below is attributable. */
      for (tick = 0; tick < 20000; ++tick) {
        kTick(processes, obs, cursors, pol, vLen, 0);
        if (pol[0].barren && pol[1].barren && pol[2].barren)
          break;
      }
      CHECK(tick < 20000, "K1: the honest cluster goes barren before the trickle");
      for (p = 0; p < K_HONEST; ++p)
        sentBefore[p] = bkr94acsFig1SentCount(processes[p]);

      /* ---- the trickle proper ----------------------------------- */
      /* Ahead-round INITIALs for which the trickler IS the designated
       * initiator: each is self-sufficiently act-producing via Rule 1,
       * and each leaves a sent ECHOED instance behind. */
      for (j = 0; j < 4; ++j)
        for (r = 0; r < 4; ++r) {
          memset(&w, 0, sizeof (w));
          w.cls = BKR94ACS_CLS_BA;
          w.process = (unsigned char)j;
          w.round = (unsigned char)r;
          w.initiator = 3;
          w.type = BRACHA87_INITIAL;
          w.from = 3;
          w.baValue = (unsigned char)((j + r) & 1);
          kSupplyAdd(&w);
        }
      /* Echo and ready for A-Cast instances that are long since
       * accepted -- recording continues past ACCEPT and returns 0. */
      for (j = 0; j < K_HONEST; ++j) {
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_ACAST;
        w.process = (unsigned char)j;
        w.type = BRACHA87_ECHO;
        w.from = 3;
        memcpy(w.value, bogusA, vLen);
        kSupplyAdd(&w);
        w.type = BRACHA87_READY;
        kSupplyAdd(&w);
      }
      /* Echo and ready for instances that will NEVER accept: the last
       * round of the trickler's own initiator plane, which it never
       * initials, so nobody else ever echoes there.  One echo and one
       * ready is the whole per-sender budget those instances have --
       * which is what makes the re-delivery below a real question and
       * not a formality. */
      for (j = 0; j < 4; ++j) {
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_BA;
        w.process = (unsigned char)j;
        w.round = K_ROUNDS - 1;
        w.initiator = 3;
        w.type = BRACHA87_ECHO;
        w.from = 3;
        w.baValue = 1;
        kSupplyAdd(&w);
        w.type = BRACHA87_READY;
        kSupplyAdd(&w);
      }

      for (k = burst1; k < KSupplyN; ++k) {
        for (p = 0; p < K_HONEST; ++p) {
          n = kDeliver(processes, obs, &KSupply[k], (unsigned char)p, vLen);
          if (n) {
            ++actProducing[p];
            if (pol[p].barren)
              ++bResets;          /* the stretch, attributed to b */
            ++pol[p].progress;
          }
        }
        kTick(processes, obs, cursors, pol, vLen, 0);
      }
      CHECK(bResets > 0,
            "K1: a b-sourced Input returning acts reset a barren counter");

      /* ---- the cascade the trickle started settles -------------- */
      for (tick = 0; tick < 20000; ++tick) {
        kTick(processes, obs, cursors, pol, vLen, 0);
        if (pol[0].barren && pol[1].barren && pol[2].barren)
          break;
      }
      CHECK(tick < 20000, "K1: the trickled cascade settles");

      /* ---- THE EXHAUSTION ASSERT -------------------------------- */
      /* The supply is spent: a complete re-delivery of the trickler's
       * ENTIRE message set returns 0 acts everywhere.  Three full
       * passes, because a machine whose per-sender dedup admitted
       * re-registration would need more than one duplicate to cross a
       * threshold -- one pass would under-state the claim. */
      for (r = 0; r < 3; ++r)
        for (k = 0; k < KSupplyN; ++k)
          for (p = 0; p < K_HONEST; ++p) {
            n = kDeliver(processes, obs, &KSupply[k], (unsigned char)p,
                         vLen);
            reActs += n;
          }
      CHECK(reActs == 0,
            "K1: a complete re-delivery of the trickler's set returns 0 acts");

      /* ---- VALUE-BLIND DEDUP ------------------------------------ */
      /* Dedup is per SENDER, not per (sender, value): a second echo
       * from the same sender for the same instance carrying different
       * multi-byte content is still one echo.  Were it value-keyed the
       * A-Cast echo term of the bound below would be unbounded. */
      for (p = 0; p < K_HONEST; ++p) {
        const unsigned char *pv;

        pv = bkr94acsAcastValue(processes[p], 0);
        valBefore[p] = pv ? pv[0] : 0;
      }
      for (p = 0; p < K_HONEST; ++p) {
        const unsigned char *pv;
        unsigned int sentNow;

        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_ACAST;
        w.process = 0;
        w.type = BRACHA87_ECHO;
        w.from = 3;
        memcpy(w.value, bogusB, vLen);
        sentNow = bkr94acsFig1SentCount(processes[p]);
        n = kDeliver(processes, obs, &w, (unsigned char)p, vLen);
        CHECK(n == 0,
              "K1: a same-sender echo with a different value returns 0 acts");
        pv = bkr94acsAcastValue(processes[p], 0);
        CHECK(pv != 0 && pv[0] == valBefore[p],
              "K1: and changes nothing observable");
        CHECK(bkr94acsFig1SentCount(processes[p]) == sentNow,
              "K1: nor the sent count");
      }

      /* ---- the barren counter runs clean to S ------------------- */
      for (p = 0; p < K_HONEST; ++p) {
        pol[p].barren = 0;
        pol[p].progress = 0;
      }
      for (tick = 0; tick < 40000; ++tick) {
        kTick(processes, obs, cursors, pol, vLen, &zeroRetries);
        if (pol[0].barren >= BARREN_S && pol[1].barren >= BARREN_S
         && pol[2].barren >= BARREN_S)
          break;
      }
      CHECK(tick < 40000,
            "K1: with the supply spent the barren counter runs clean to S");
      /* ENDINGS: the trickler never announces an accept, so like any
       * silent process it holds the quiescence ending open -- the
       * abandonment gate is what ends this run.  Standing fact,
       * re-asserted across a bounded further drive. */
      CHECK(zeroRetries == 0,
            "K1: no honest Retry idles -- quiescence stays open");
      for (tick = 0; tick < 50; ++tick)
        kTick(processes, obs, cursors, pol, vLen, &zeroRetries);
      CHECK(zeroRetries == 0,
            "K1: and non-quiescence still stands after a further drive");

      /* ---- SWEEP INFLATION -------------------------------------- */
      /* The ahead-round ECHOED instances the trickle left behind
       * inflate the sweep unit and never shrink; the harness sweep
       * boundary RECOMPUTES the count for exactly this reason. */
      for (p = 0; p < K_HONEST; ++p) {
        delta = bkr94acsFig1SentCount(processes[p]) - sentBefore[p];
        CHECK(bkr94acsFig1SentCount(processes[p]) >= sentBefore[p],
              "K1: the sent count never shrinks under trickle");
        CHECK(delta <= 4 * K_ROUNDS,
              "K1: inflation bounded by n*R per trickler");
      }

      /* ---- THE CEILING and the frozen count --------------------- */
      /* I = n + n*R*n instances.  Per-sender dedup admits one echo and
       * one ready per instance (2*I), and Rule 1 admits one INITIAL
       * per instance for which the trickler is the DESIGNATED
       * initiator -- its own A-Cast plus every BA instance it
       * initiates, n*R + 1.  Nothing else the trickler sends can
       * produce an act at an honest receiver. */
      inst = 4 + 4 * K_ROUNDS * 4;
      ceiling = 2 * inst + 4 * K_ROUNDS + 1;
      for (p = 0; p < K_HONEST; ++p) {
        CHECK(actProducing[p] <= ceiling,
              "K1: b-sourced act-producing inputs within the derived ceiling");
        CHECK(actProducing[p] == 16,
              "K1: b-sourced act-producing inputs match the frozen count");
      }

      /* ---- honest outcome unharmed ------------------------------ */
      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "K1: |SubSet| >= n-t");
      for (p = 0; p < K_HONEST; ++p) {
        CHECK(processes[p]->complete, "K1: honest process completed");
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "K1: honest SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "K1: honest SubSet contents byte-identical");
        for (j = 0; j < sz0; ++j) {
          const unsigned char *pv;

          pv = bkr94acsAcastValue(processes[p], subset0[j]);
          CHECK(pv != 0 && pv[0] == (unsigned char)(subset0[j] << 4),
                "K1: Lemma 2 -- member values agree and are unaltered");
        }
      }
      printf("      K1: supply %u messages, %u act-producing per receiver,"
             " ceiling %u, %u attributed resets\n",
             KSupplyN, actProducing[0], ceiling, bResets);

      freeCluster(processes, 4);
    }
  }

  /* ================================================================ */
  /*  Section L -- staggered start                                    */
  /* ================================================================ */
  /*  Grounding: bracha87.h's BPR retry banner -- the retire           */
  /*  conditions are what decide which retry types are still live at   */
  /*  a given moment; Bracha87.txt Fig 1 rows 1/3/5/6; BKR94ACS.txt    */
  /*  steps 1-3.  Cross-reference label: README "Abandonment" /        */
  /*  Staggered start.                                                */
  /*                                                                  */
  /*  A process that starts after the others is, until its first       */
  /*  message arrives, byte-identical to a dead one -- and the others' */
  /*  BPR retries are precisely the bootstrap it missed.  Two lanes    */
  /*  separate WHICH retries do the carrying: the PRE-ACCEPT lane,     */
  /*  where INITIAL and ECHO are still live, and the POST-FANOUT lane, */
  /*  where they have retired and READY alone is left.                 */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("L1: a late starter bootstraps on live INITIAL re-sends");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    struct lWitness wit;
    unsigned char down[4];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int guardInitials, guardEchoes, guardReadys;
    unsigned int included;
    unsigned int tick, n, p, j, k, sz0, szP, pass;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
        down[p] = 0;
      }
      qReset();
      memset(&wit, 0, sizeof (wit));
      wit.late = 3;
      down[3] = 1;

      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0x70 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, 3);
      }

      /* K is placed here: the three bootstrap INITIALs are delivered
       * and nothing else, so every survivor has ECHOED all three
       * A-Casts and none has accepted -- INITIAL and ECHO retries are
       * both live.  Every INITIAL sent so far was dropped at the late
       * starter's unbound socket. */
      lTick(processes, obs, cursors, pol, down, 3, &wit);

      down[3] = 0;
      n = bkr94acsAcast(processes[3], &acasts[3], acastOut);
      acasts[3] = 0x73;
      observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, 0, -1);

      /* THE CONSTRUCTION GUARD.  The Fig 1 accessors cannot express
       * retry-type liveness, so the machine's own retry EGRESS is the
       * observable: one full survivor sweep across the newly-open link
       * carries a BRACHA87_INITIAL act, which is what the late starter
       * will bootstrap on. */
      guardInitials = 0;
      guardEchoes = 0;
      guardReadys = 0;
      for (p = 0; p < 3; ++p) {
        pass = bkr94acsFig1SentCount(processes[p]);
        for (tick = 0; tick < pass; ++tick) {
          n = bkr94acsRetryStep(processes[p], &cursors[p], out);
          for (k = 0; k < n; ++k) {
            if (out[k].type == BRACHA87_INITIAL)
              ++guardInitials;
            else if (out[k].type == BRACHA87_ECHO)
              ++guardEchoes;
            else if (out[k].type == BRACHA87_READY)
              ++guardReadys;
          }
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }
      }
      CHECK(guardInitials > 0,
            "L1: the survivors' retry egress at K carries a live INITIAL");
      CHECK(guardEchoes > 0,
            "L1: and a live ECHO -- nothing has accepted yet");

      for (tick = 0; tick < 20000; ++tick) {
        lTick(processes, obs, cursors, pol, down, 0, &wit);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete && processes[3]->complete)
          break;
      }
      CHECK(tick < 20000, "L1: all four complete after the late start");
      CHECK(wit.initialsIn > 0,
            "L1: the late starter's bootstrap consumed a re-sent A-Cast"
            " INITIAL");

      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "L1: |SubSet| >= n-t");
      for (p = 1; p < 4; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "L1: SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "L1: SubSet contents byte-identical");
      }

      /* LANE 3, drift armor.  K also precedes the step-2 fanout on
       * this schedule, so the late starter's fate is not forced by
       * anything the contract states -- it is recorded as a frozen
       * fact, and only a change in the machine's timing moves it. */
      included = 0;
      for (j = 0; j < sz0; ++j)
        if (subset0[j] == 3)
          included = 1;
      CHECK(included == 1,
            "L1 lane 3: the late starter's inclusion fate (frozen fact)");
      CHECK(sz0 == 4,
            "L1 lane 3: |SubSet| == 4 on this schedule (frozen fact)");
      printf("      L1: %u INITIAL / %u ECHO / %u READY retry acts at K,"
             " %u re-sent A-Cast INITIALs taken in, |SubSet| %u\n",
             guardInitials, guardEchoes, guardReadys, wit.initialsIn, sz0);

      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("L2: a post-fanout late starter still completes and agrees");
  /* ---------------------------------------------------------------- */
  {
    /* K after the survivors' step-2 fanout fired, so their INITIAL and
     * ECHO retries retired long ago and READY re-sends are all that
     * is left.  The mechanism deliberately overlaps I1's; what this
     * lane claims is only the composition-level outcome. */
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct acastOut[1];
    struct lWitness wit;
    unsigned char down[4];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int tick, n, p, sz0, szP;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
        down[p] = 0;
      }
      qReset();
      memset(&wit, 0, sizeof (wit));
      wit.late = 3;
      down[3] = 1;

      for (p = 0; p < 3; ++p) {
        acasts[p] = (unsigned char)(0x80 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, 3);
      }
      for (tick = 0; tick < 20000; ++tick) {
        lTick(processes, obs, cursors, pol, down, 0, &wit);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete)
          break;
      }
      CHECK(tick < 20000, "L2: the started cluster completes without it");
      CHECK(bkr94acsBaDecision(processes[0], 3) == 0,
            "L2: the fanout closed the late starter out");

      down[3] = 0;
      acasts[3] = 0x83;
      n = bkr94acsAcast(processes[3], &acasts[3], acastOut);
      observeAndOutput(&obs[3], 3, 4, acastOut, n, 1, 0, -1);
      for (tick = 0; tick < 20000; ++tick) {
        lTick(processes, obs, cursors, pol, down, 0, &wit);
        if (processes[3]->complete)
          break;
      }
      CHECK(tick < 20000, "L2: the late starter completes anyway");

      sz0 = bkr94acsSubset(processes[0], subset0);
      for (p = 1; p < 4; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "L2: SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "L2: SubSet contents byte-identical");
      }
      printf("      L2: |SubSet| %u, %u A-Cast INITIAL / %u A-Cast ECHO"
             " inputs at the late starter\n",
             sz0, wit.initialsIn, wit.echoesIn);

      freeCluster(processes, 4);
    }
  }

  /* ================================================================ */
  /*  Section M -- after COMPLETE: the residue vs the backstop        */
  /* ================================================================ */
  /*  Grounding: bracha87.h's BPR retry banner, the honest-residue     */
  /*  paragraph ("a process that abandons early, or one that never     */
  /*  announces at all, keeps every other process's count below n")    */
  /*  and the Skip contract (the READY retire gate is the Skip mask    */
  /*  reaching all n; Skip is the accepted set MINUS the outstanding   */
  /*  arms, so it is a subset of the RECEIVED mask); bkr94acs.h's      */
  /*  Retry 0-return contract.  Cross-reference label: README          */
  /*  "Abandonment" / After COMPLETE.                                  */
  /*                                                                  */
  /*  H1 has the REACHABLE half and F1b the decided-0 scope.  This arm */
  /*  claims the RESIDUE only: what no annotation can reach, and the   */
  /*  state the barren-sweep backstop exists to end.                  */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("M1: a never-announcing leaver holds every survivor's gate open");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int leaver = 3;
    unsigned int gone, delivered, zeroRetries, prevDelivered;
    unsigned int served, shortByLeaver, aimed, gated;
    unsigned int barrenAt, tick, n, p, j, r, b, q, sz0, szP;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();
      gone = 0;
      delivered = 0;
      zeroRetries = 0;

      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0xD0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }

      /* Past COMPLETE at every survivor, with its own iteration cap. */
      for (tick = 0; tick < 30000; ++tick) {
        mTick(processes, obs, cursors, pol, leaver, 0, &gone, &delivered,
              &zeroRetries);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete)
          break;
      }
      CHECK(tick < 30000, "M1: the survivors complete past the leaver");
      CHECK(gone, "M1: the leaver left before announcing an accept of its own");

      /* Run on to the stable point the masks must be read at: a full
       * pass's egress with no arm pending, i.e. a tick that delivered
       * nothing to any survivor.  bracha87.h's one-duplicate-at-worst
       * window is exactly what this avoids reading into. */
      prevDelivered = delivered + 1;
      for (tick = 0; tick < 30000 && prevDelivered != delivered; ++tick) {
        prevDelivered = delivered;
        mTick(processes, obs, cursors, pol, leaver, 0, &gone, &delivered,
              &zeroRetries);
      }
      CHECK(tick < 30000, "M1: the survivors reach a stable point");

      /* THE STANDING FACTS.  Read once here, then re-asserted below
       * across a bounded further drive. */
      zeroRetries = 0;
      barrenAt = pol[0].barren;
      served = 0;
      shortByLeaver = 0;
      aimed = 0;
      gated = 0;
      for (p = 0; p < 3; ++p) {
        for (j = 0; j < 4; ++j) {
          const struct bracha87Fig1 *f1;
          const unsigned char *ans;
          const unsigned char *skip;

          if (!(f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j))
           || !bracha87Fig1Value(f1))
            continue;
          /* Scope per F1b: a BA that decided 0 takes its A-Cast out of
           * the retry walk, so there the gate itself is the retire and
           * the masks carry no claim.  Counted and skipped. */
          if (!bkr94acsBaDecision(processes[p], (unsigned char)j)) {
            ++gated;
            continue;
          }
          ++served;
          ans = bracha87Fig1Received(f1);
          skip = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
          if (ans && !BRACHA87_SKIP_TST(ans, leaver)) {
            for (q = 0; q < 3; ++q)
              if (!BRACHA87_SKIP_TST(ans, q))
                break;
            if (q == 3)
              ++shortByLeaver;
          }
          if (skip && !BRACHA87_SKIP_TST(skip, leaver)) {
            for (q = 0; q < 3; ++q)
              if (!BRACHA87_SKIP_TST(skip, q))
                break;
            if (q == 3)
              ++aimed;
          }
        }
        for (j = 0; j < 4; ++j)
          for (r = 0; r < 6; ++r)
            for (b = 0; b < 4; ++b) {
              const struct bracha87Fig1 *f1;
              const unsigned char *ans;
              const unsigned char *skip;

              if (!(f1 = bkr94acsBaFig1(processes[p], (unsigned char)j,
                                        (unsigned char)r, (unsigned char)b))
               || !bracha87Fig1Value(f1))
                continue;
              ++served;
              ans = bracha87Fig1Received(f1);
              skip = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
              if (ans && !BRACHA87_SKIP_TST(ans, leaver)) {
                for (q = 0; q < 3; ++q)
                  if (!BRACHA87_SKIP_TST(ans, q))
                    break;
                if (q == 3)
                  ++shortByLeaver;
              }
              if (skip && !BRACHA87_SKIP_TST(skip, leaver)) {
                for (q = 0; q < 3; ++q)
                  if (!BRACHA87_SKIP_TST(skip, q))
                    break;
                if (q == 3)
                  ++aimed;
              }
            }
      }
      CHECK(served > 0, "M1: served instances were actually examined");
      CHECK(served == shortByLeaver,
            "M1: every served instance's RECEIVED mask is short by exactly"
            " the leaver's bit");
      CHECK(served == aimed,
            "M1: and its READY re-sends are aimed at the leaver alone");

      /* A bounded further drive: the facts above are STANDING, not a
       * moment.  The barren counters climb monotonically over it under
       * the shared PROGRESS definition -- the state the policy exists
       * to end -- and the harness policy is what ends this drive. */
      for (tick = 0; tick < 30000; ++tick) {
        mTick(processes, obs, cursors, pol, leaver, 0, &gone, &delivered,
              &zeroRetries);
        if (pol[0].barren >= BARREN_S && pol[1].barren >= BARREN_S
         && pol[2].barren >= BARREN_S)
          break;
      }
      CHECK(tick < 30000,
            "M1: the survivors' barren counters reach the policy's S");
      CHECK(pol[0].barren >= barrenAt && pol[1].barren >= barrenAt
         && pol[2].barren >= barrenAt,
            "M1: the barren counters only climbed");
      CHECK(zeroRetries == 0,
            "M1: no survivor ever reaches the Retry 0 return");
      for (p = 0; p < 3; ++p)
        CHECK(bkr94acsRetryStep(processes[p], &cursors[p], out) > 0,
              "M1: and the non-zero return is stable");

      served = 0;
      shortByLeaver = 0;
      for (p = 0; p < 3; ++p)
        for (j = 0; j < 4; ++j)
          for (r = 0; r < 6; ++r)
            for (b = 0; b < 4; ++b) {
              const struct bracha87Fig1 *f1;
              const unsigned char *ans;

              if (!(f1 = bkr94acsBaFig1(processes[p], (unsigned char)j,
                                        (unsigned char)r, (unsigned char)b))
               || !bracha87Fig1Value(f1))
                continue;
              ++served;
              ans = bracha87Fig1Received(f1);
              if (ans && !BRACHA87_SKIP_TST(ans, leaver)) {
                for (q = 0; q < 3; ++q)
                  if (!BRACHA87_SKIP_TST(ans, q))
                    break;
                if (q == 3)
                  ++shortByLeaver;
              }
            }
      CHECK(served == shortByLeaver,
            "M1: the shortfall still stands after the further drive");

      /* The success half is unharmed -- a residue is not a failure. */
      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "M1: |SubSet| >= n-t");
      for (p = 0; p < 3; ++p) {
        CHECK(processes[p]->complete, "M1: survivor completed");
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "M1: survivor SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "M1: survivor SubSet contents byte-identical");
      }
      printf("      M1: %u served instances, %u short by the leaver's bit,"
             " %u gated, |SubSet| %u\n",
             served, shortByLeaver, gated, sz0);

      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("M2: an announced-then-silent leaver lets every survivor quiesce");
  /* ---------------------------------------------------------------- */
  /* BPR.md (Quiescence): "a process that announces its accept and THEN  */
  /* leaves lets the others quiesce -- their evidence fills on the       */
  /* announcement and nothing ever re-arms -- so the honest residue      */
  /* class is exactly the never-announcer."  The leaver here runs until  */
  /* its OWN quiescent 0 return -- every instance of its announced, and  */
  /* it holding everyone's -- then never ticks or receives again.  The   */
  /* survivors must reach the 0 return too, and every served instance's  */
  /* RECEIVED mask at each survivor must cover all n, the leaver's bit   */
  /* included.                                                           */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    unsigned int leaver = 3;
    unsigned int gone, delivered, zeroRetries;
    unsigned int quiesced, tick, n, p, j, q;

    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();
      gone = 0;
      delivered = 0;
      zeroRetries = 0;
      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0xE0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }
      quiesced = 0;
      for (tick = 0; tick < 30000 && quiesced < 3; ++tick) {
        mTick(processes, obs, cursors, pol, leaver, 1, &gone, &delivered,
              &zeroRetries);
        if (!gone)
          continue;
        /* A second retry call per survivor per tick, its acts sent
         * like any other, so the probe loses nothing. */
        quiesced = 0;
        for (p = 0; p < 3; ++p) {
          n = bkr94acsRetryStep(processes[p], &cursors[p], out);
          if (!n)
            ++quiesced;
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }
      }
      CHECK(gone, "M2: the leaver left on its own quiescent 0 return");
      CHECK(quiesced == 3, "M2: every survivor reaches the Retry 0 return");
      for (p = 0; p < 3; ++p)
        CHECK(processes[p]->complete, "M2: and is complete");
      for (p = 0; p < 3; ++p)
        for (j = 0; j < 4; ++j) {
          unsigned int r;
          unsigned int i;

          for (r = 0; r <= 6; ++r)
            for (i = 0; i < 4; ++i) {
              const struct bracha87Fig1 *f1;
              const unsigned char *ans;

              if (r == 6) {                       /* the A-Cast, once */
                if (i)
                  break;
                if (bkr94acsBaDecision(processes[p], (unsigned char)j) == 0)
                  continue;
                f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j);
              } else
                f1 = bkr94acsBaFig1(processes[p], (unsigned char)j,
                                    (unsigned char)r, (unsigned char)i);
              if (!f1 || !bracha87Fig1Value(f1))
                continue;
              ans = bracha87Fig1Received(f1);
              for (q = 0; q < 4; ++q)
                CHECK(ans && BRACHA87_SKIP_TST(ans, q),
                      "M2: every served instance's evidence covers all n,"
                      " the leaver's bit included");
            }
        }
      printf("      M2: survivors quiescent at tick %u\n", tick);
      freeCluster(processes, 4);
    }
  }

  /* ================================================================ */
  /*  Section N -- the sustained-rate skew lane                       */
  /* ================================================================ */
  /*  Grounding: bkr94acs.h's duty trichotomy block ("derived by       */
  /*  scanning baDecision[] and the entered set; nothing stored"),     */
  /*  the FanoutDuty / TurnDuty semantics, and the sweep-unit block    */
  /*  ("THE UNIT IS THE FULL SWEEP ... a budget denominated in calls   */
  /*  rather than passes re-sends only its own count out of            */
  /*  bkr94acsFig1SentCount and can buy nothing at all").              */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("N1: the fairness non-invariant under a sustained rate skew");
  /* ---------------------------------------------------------------- */
  {
    /* The SAME schedule and the SAME patience, with only the RATE of one
     * process changed.  A patience that includes the delayed honest
     * A-Cast when everyone runs at one rate excludes it when the
     * cohort runs faster: the patience is spent in the FIRING process's
     * own completed sweeps, while the recovery it buys -- the delayed
     * instance's re-send -- arrives at the DELAYED process's cursor
     * rate.  Two clocks, and the patience prices only one of them.
     *
     * The skewed process must be the delayed A-Cast's initiator.
     * Skewing an unrelated process changes neither clock and produces
     * noise, which the mirror lane below records as the fact it is. */
    static const unsigned int ks[] = { 1, 2, 4 };
    unsigned int ticks[3];
    unsigned int included[3];
    unsigned int fanActs[3];
    unsigned int mTicks[3];
    unsigned int mIncluded[3];
    unsigned int mFanActs[3];
    unsigned int ki;

    for (ki = 0; ki < sizeof (ks) / sizeof (ks[0]); ++ki) {
      CHECK(nDrive(3 /*the delayed initiator*/, ks[ki], 2 /*sweeps*/,
                   6 /*own ticks to submission*/, 60000,
                   &ticks[ki], &included[ki], &fanActs[ki]) == 0,
            "N1: the k-slow-initiator lane completes");
      CHECK(nDrive(0 /*a cohort process*/, ks[ki], 2, 6, 60000,
                   &mTicks[ki], &mIncluded[ki], &mFanActs[ki]) == 0,
            "N1 mirror: the k-slow-cohort lane completes");
    }

    /* Regression facts, per lane direction and per k.  Nothing in the
     * contract forces them, which is the point -- a sweep-denominated
     * patience is not a fairness guarantee, and the process the skew
     * lands on is the one that loses its participation. */
    CHECK(included[0] == 1,
          "N1: k=1, the delayed initiator is INCLUDED (frozen fact)");
    CHECK(included[1] == 0,
          "N1: k=2, the same A-Cast is EXCLUDED (frozen fact)");
    CHECK(included[2] == 0,
          "N1: k=4, the same A-Cast is EXCLUDED (frozen fact)");
    CHECK(fanActs[0] == 0 && fanActs[1] > 0 && fanActs[2] > 0,
          "N1: and the exclusion is the enter-0 fanout firing");

    CHECK(mIncluded[0] == 1,
          "N1 mirror: k=1, INCLUDED (frozen fact)");
    CHECK(mIncluded[1] == 1,
          "N1 mirror: k=2 (frozen fact)");
    CHECK(mIncluded[2] == 1,
          "N1 mirror: k=4 (frozen fact)");

    /* (ii) COST SCALING, a harness sanity check and not a claim about
     * the machine: the arithmetic of a k-slow participant is that it
     * takes at least as many iterations to reach the same protocol
     * events.  Monotone only -- no rate is asserted. */
    CHECK(ticks[0] <= ticks[1] && ticks[1] <= ticks[2],
          "N1: iteration count grows monotonically with k (harness sanity)");

    printf("      N1: slow initiator ticks %u/%u/%u included %u/%u/%u;"
           " slow cohort ticks %u/%u/%u included %u/%u/%u\n",
           ticks[0], ticks[1], ticks[2],
           included[0], included[1], included[2],
           mTicks[0], mTicks[1], mTicks[2],
           mIncluded[0], mIncluded[1], mIncluded[2]);
  }

  /* ---------------------------------------------------------------- */
  BANNER("N2: duty verdicts are pure functions of state");
  /* ---------------------------------------------------------------- */
  {
    /* The header states the verdicts are "derived by scanning ...;
     * nothing stored".  A query cannot be the thing that fixes an
     * answer, so applying a KNOWN state delta must move the class the
     * way the trichotomy dictates -- and a state fingerprint reached
     * by two different query cadences must read the same vector.
     *
     * Deliberately NOT a re-proof of G2/G3: those read a class AT a
     * state; this reads it BEFORE and AFTER a delta on one instance,
     * and compares vectors ACROSS cadences at a matched fingerprint
     * where the verdict is not forced. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    unsigned char decis[3][4];
    unsigned char enterd[3][4];
    unsigned char valcnt[3][4];
    unsigned char duties[3][4];
    unsigned char fduty[3];
    unsigned char senders[MAX_PROCESSES];
    unsigned char values[MAX_PROCESSES];
    static const unsigned int ks[] = { 1, 2, 4 };
    static const unsigned char roundValue[] = { 1, 1, BRACHA87_D_FLAG | 1 };
    unsigned int dummy = 0;
    unsigned int ki, step, b, r, j;

    /* -- TurnDuty, the state-transition form ------------------------ */
    sz = bkr94acsSz(3, 0, 8);
    a = calloc(1, sz);
    if (!a) goto n2_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (b = 0; b < 2; ++b)
      feedBAAccept(a, 0, 0, (unsigned char)b, 1, out, 0, &dummy);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_HELD,
          "N2: TurnDuty HELD below n-t validated");
    feedBAAccept(a, 0, 0, 2, 1, out, 0, &dummy);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_TOLERANCE,
          "N2: the n-t'th validated message moves it HELD -> TOLERANCE");
    /* And that delivery returned NOTHING.  The header states the
     * arrival path only banks -- an accept is stored, validated and
     * cascaded, and BA_DECIDED / COMPLETE / BA_EXHAUSTED emerge only
     * from a turn -- so the turn's patience window opens with no act
     * for a caller to count.  That is why the turn's ordering against
     * the abandon gate is caller discipline and not structure: unlike
     * the fanout's, this window's opening is invisible in the act
     * stream a progress counter reads. */
    CHECK(FeedLastActs == 0,
          "N2: the delivery opening the turn's window returns 0 acts");
    feedBAAccept(a, 0, 0, 3, 1, out, 0, &dummy);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_MET,
          "N2: the n'th moves it TOLERANCE -> MET");
    free(a);

    /* -- FanoutDuty, the same form ---------------------------------- */
    a = calloc(1, sz);
    if (!a) goto n2_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (j = 0; j < 3; ++j) {
      for (r = 0; r < 3; ++r)
        for (b = 0; b < 4; ++b)
          feedBAAccept(a, (unsigned char)j, (unsigned char)r,
                       (unsigned char)b, roundValue[r], out, 1, &dummy);
      CHECK(bkr94acsBaDecision(a, (unsigned char)j) == 1,
            "N2: the constructed BA decides 1");
      if (j < 2)
        CHECK(bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_HELD,
              "N2: FanoutDuty HELD below the n-t decided-1 floor");
      else
        CHECK(bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_TOLERANCE,
              "N2: the n-t'th decided-1 moves it HELD -> TOLERANCE"
              " with a BA still unentered");
    }
    free(a);

    /* -- the across-cadence vector ---------------------------------- */
    /* Identical evidence, three query cadences.  The fingerprint that
     * results and the duty vector over all n BAs must be the same at
     * every cadence: nothing a query does can be state. */
    for (ki = 0; ki < sizeof (ks) / sizeof (ks[0]); ++ki) {
      a = calloc(1, sz);
      if (!a) goto n2_done;
      bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

      /* A deliberately mixed mid-run state, where no BA's verdict is
       * forced: BA_0 at n-t validated, BA_1 below it, BA_2 and BA_3
       * untouched. */
      for (step = 0; step < 5; ++step) {
        if (step < 3)
          feedBAAccept(a, 0, 0, (unsigned char)step, 1, out, 0, &dummy);
        else
          feedBAAccept(a, 1, 0, (unsigned char)(step - 3), 1, out, 0,
                       &dummy);
        if (!(step % ks[ki])) {
          for (b = 0; b < 4; ++b)
            (void)bkr94acsTurnDuty(a, (unsigned char)b);
          (void)bkr94acsFanoutDuty(a);
        }
      }
      for (b = 0; b < 4; ++b) {
        decis[ki][b] = bkr94acsBaDecision(a, (unsigned char)b);
        enterd[ki][b] = (unsigned char)bkr94acsBaEntered(a, (unsigned char)b);
        valcnt[ki][b] = (unsigned char)
          bkr94acsBaGetValid(a, (unsigned char)b, senders, values);
        duties[ki][b] = bkr94acsTurnDuty(a, (unsigned char)b);
      }
      fduty[ki] = bkr94acsFanoutDuty(a);
      free(a);
    }
    for (ki = 1; ki < 3; ++ki) {
      CHECK(!memcmp(decis[0], decis[ki], 4)
         && !memcmp(enterd[0], enterd[ki], 4)
         && !memcmp(valcnt[0], valcnt[ki], 4),
            "N2: the state fingerprint matches across query cadences");
      CHECK(!memcmp(duties[0], duties[ki], 4) && fduty[0] == fduty[ki],
            "N2: and the whole duty vector reads the same there");
    }
    /* Non-vacuity: the matched state carries more than one class. */
    CHECK(duties[0][0] != duties[0][1],
          "N2: the matched fingerprint is not a single-class state");
  }
  n2_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section O -- a BA's decision versus this process's input to it   */
  /* ---------------------------------------------------------------- */
  /*  Lemma 2 Part C says every correct process closes the SAME        */
  /*  SubSet.  It says nothing about the relationship between that     */
  /*  SubSet and what any one process put INTO the BAs, and the two    */
  /*  do come apart: a process reaches BA_j either by accepting j's    */
  /*  A-Cast (step 1, input 1) or by the step-2 fanout (input 0), and  */
  /*  the BA's decision is the cluster's, not its own.  Section B      */
  /*  agrees the SubSets under schedules where the two coincide; the   */
  /*  arms below hold one A-Cast instance away from process 0 so they  */
  /*  do not, and require the agreement to survive it.                 */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("O1: a BA decides 1 for an A-Cast this process never accepted");
  /* ---------------------------------------------------------------- */
  {
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    struct bracha87Retry oCur[MAX_PROCESSES];
    struct sweepPolicy oPol[MAX_PROCESSES];
    unsigned int tick, p, sz0, szP, found;

    if (allocCluster(processes, 4, 1, 0, 8) == 0) {
      tick = oDrive(processes, obs, acasts, 4,
                    1u << 3 /*hold A-Cast 3*/, 0 /*from process 0*/,
                    0, 0xFF, 0, 500);
      CHECK(tick < 500, "O1: the cluster completes without A-Cast 3 at process 0");
      CHECK(bkr94acsAcastValue(processes[0], 3) == 0,
            "O1: process 0 still holds no value for A-Cast 3");
      CHECK(bkr94acsBaDecision(processes[0], 3) == 1,
            "O1: BA_3 decided 1 all the same");
      sz0 = bkr94acsSubset(processes[0], subset0);
      found = 0;
      for (p = 0; p < sz0; ++p)
        if (subset0[p] == 3)
          found = 1;
      CHECK(found, "O1: SubSet includes a process whose value we lack");
      for (p = 1; p < 4; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "O1: SubSet sizes agree across processes");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "O1: SubSet contents agree across processes");
      }

      /* The value is owed, not lost: releasing the instance delivers it
       * without disturbing the closed SubSet. */
      OAcastHold = 0;
      for (p = 0; p < 4; ++p) {
        bracha87RetryInit(&oCur[p]);
        memset(&oPol[p], 0, sizeof (oPol[p]));
      }
      for (tick = 0; tick < 200; ++tick) {
        oTick(processes, obs, oCur, oPol, 4);
        if (bkr94acsAcastValue(processes[0], 3))
          break;
      }
      CHECK(bkr94acsAcastValue(processes[0], 3) != 0,
            "O1: the withheld value arrives after the close");
      CHECK(bkr94acsSubset(processes[0], subsetP) == sz0
            && memcmp(subset0, subsetP, sz0) == 0,
            "O1: the late value does not move the SubSet");
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("O2: two fanout enter-0 exclusions at n=7 t=2");
  /* ---------------------------------------------------------------- */
  /* At n = 4, t = 1 the fanout can exclude at most one BA, so the     */
  /* enter-0 it fires is always the last BA to decide.  t = 2 admits   */
  /* two, which is the only way a fanout-entered BA decides while      */
  /* another is still outstanding.                                     */
  {
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int tick, p, sz0, szP, zeros;

    if (allocCluster(processes, 7, 2, 0, 8) == 0) {
      tick = oDrive(processes, obs, acasts, 7,
                    (1u << 5) | (1u << 6), 0xFF /*from everyone*/,
                    0, 0xFF, 0, 2000);
      CHECK(tick < 2000, "O2: the cluster completes with two A-Casts held");
      zeros = 0;
      for (p = 0; p < 7; ++p)
        if (bkr94acsBaDecision(processes[0], (unsigned char)p) == 0)
          ++zeros;
      CHECK(zeros == 2, "O2: exactly two BAs decided 0");
      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 == 5, "O2: |SubSet| == n-t");
      for (p = 1; p < 7; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "O2: SubSet sizes agree across processes");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "O2: SubSet contents agree across processes");
      }
      freeCluster(processes, 7);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("O3: a BA this process entered with 0 decides 1");
  /* ---------------------------------------------------------------- */
  /* Holding the BA class too keeps process 0's view of BA_5 and BA_6  */
  /* behind its own fanout, so it enters both with 0 and only then     */
  /* learns the cluster decided 1 -- its input and the decision        */
  /* disagree, and the SubSet must still agree with everyone else's.   */
  {
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int tick, p, sz0, szP, ones, zeros;

    if (allocCluster(processes, 7, 2, 0, 8) == 0) {
      tick = oDrive(processes, obs, acasts, 7,
                    (1u << 5) | (1u << 6), 0 /*A-Casts held from process 0*/,
                    (1u << 5) | (1u << 6), 0 /*and their BAs too*/,
                    8 /*released at tick 8*/, 2000);
      CHECK(tick < 2000, "O3: the cluster completes");
      /* The whole point of the hold is that process 0's own input to
       * these two BAs is the fanout's 0, not step 1's 1 -- without
       * that, every check below passes on a schedule with no hold at
       * all.  obs records the value carried by each self-INITIAL. */
      zeros = 0;
      ones = 0;
      for (p = 5; p < 7; ++p) {
        CHECK(obs[0].selfInputAny[p],
              "O3: process 0 did enter the held BA");
        if (obs[0].selfInputValue[p] == 0)
          ++zeros;
        if (bkr94acsBaDecision(processes[0], (unsigned char)p) == 1)
          ++ones;
      }
      CHECK(zeros == 2, "O3: process 0's own input to both held BAs was 0");
      CHECK(ones == 2, "O3: both held BAs decided 1 against that 0 input");
      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 == 7, "O3: |SubSet| == n -- nothing was excluded");
      for (p = 1; p < 7; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "O3: SubSet sizes agree across processes");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "O3: SubSet contents agree across processes");
      }
      freeCluster(processes, 7);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section P -- annotation forgery                                  */
  /* ---------------------------------------------------------------- */
  /*  The two READY annotations are the one part of the wire NO paper   */
  /*  backs -- BPR is this repository's own -- and bkr94acs.h states    */
  /*  their Byzantine safety in two sentences at bkr94acs{Acast,Ba}-    */
  /*  Input:                                                           */
  /*                                                                   */
  /*    "A forged ACCEPTED marks only its own sender, so it retires     */
  /*     this process's retry to the liar alone and can never strand a  */
  /*     correct laggard.  A forged missing RECEIVED un-suppresses only */
  /*     its own sender, buying the forger one masked READY per         */
  /*     instance per sweep of the RECEIVING process's cursor, aimed    */
  /*     at itself."                                                    */
  /*                                                                   */
  /*  The per-sender containment MECHANISM is unit-tested; both         */
  /*  sentences above are end-to-end and quantitative, and no arm       */
  /*  anywhere forged an annotation.  These three do.  Nothing here     */
  /*  specifies what the answer should be: the arms measure, and the    */
  /*  header's sentence stands or is corrected by what they say.        */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("P1: a forged accept announcement is contained to its own sender");
  /* ---------------------------------------------------------------- */
  {
    /* n=7 t=2.  Process 6 announces ACCEPTED on every READY from its
     * first one, long before it holds any accept.  Process 5 is a
     * correct laggard, cut off the network in both directions while
     * A-Cast 4 is raised, so it holds ZERO evidence of that instance --
     * the I1 shape, which is what makes "strand a correct laggard"
     * something the arm can actually witness.  The connected side is
     * exactly n-t = 5 honest processes plus the forger. */
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct sweepPolicy pol[MAX_PROCESSES];
    struct bkr94acsAct acastOut[1];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int nAct = 7;
    unsigned int mr = 6;
    unsigned int unearned = 0;
    unsigned int violations = 0;
    unsigned int tick, n, p, j, sz0, szP, done;

    if (allocCluster(processes, 7, 2, 0, 2) == 0) {
      pReset();
      PForger = 6;
      PForgeAccepted = 1;
      PLagWatch = 5;
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < nAct; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();

      /* Pre-cut: 0-3 A-Cast into a whole network and the laggard banks
       * real evidence, so the cut below is a laggard and not a process
       * that never started. */
      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0x20 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, nAct, acastOut, n, 1, 0,
                         -1);
      }
      for (tick = 0; tick < 2000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        pMaskAudit(processes, nAct, &unearned, &violations);
        if (bkr94acsAcastValue(processes[5], 0)
         && bkr94acsAcastValue(processes[5], 3))
          break;
      }
      CHECK(tick < 2000, "P1: the laggard holds real pre-cut evidence");

      /* THE CUT, and the remaining A-Casts raised behind it. */
      PCut = 5;
      for (p = 4; p < nAct; ++p) {
        acasts[p] = (unsigned char)(0x20 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, nAct, acastOut, n, 1, 0,
                         -1);
      }
      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        pMaskAudit(processes, nAct, &unearned, &violations);
        done = 1;
        for (p = 0; p < 5; ++p)
          if (!processes[p]->complete)
            done = 0;
        if (done)
          break;
      }
      CHECK(tick < 40000,
            "P1: the connected honest side completes while the cut stands");

      /* NON-VACUITY.  The forgery has to have reached the evidence, or
       * every containment reading below is about nothing. */
      CHECK(PForgedEarly > 0,
            "P1: the forger announced accepts it did not hold");
      CHECK(unearned > 0,
            "P1: and the lie is in the honest processes' accepted evidence");
      CHECK(bkr94acsAcastValue(processes[5], 4) == 0,
            "P1: the laggard holds nothing for the instance raised at the cut");

      /* THE HEAL.  The laggard must still be carried even though the
       * forger has by now "announced" for every instance at every
       * honest process.  What does the carrying is measured below, not
       * assumed here. */
      PCut = 0xFF;
      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        pMaskAudit(processes, nAct, &unearned, &violations);
        if (processes[5]->complete)
          break;
      }
      CHECK(tick < 40000, "P1: the laggard reaches COMPLETE after the heal");
      /* COMPLETE is not the carrying: a BA decides for an A-Cast this
       * process never accepted (O1), so the value is owed past the
       * close and the carrying is what arrives after it. */
      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        pMaskAudit(processes, nAct, &unearned, &violations);
        if (bkr94acsAcastValue(processes[5], 4)
         && bkr94acsAcastValue(processes[5], 6))
          break;
      }
      CHECK(tick < 40000,
            "P1: and ACCEPT on the instances it held nothing for");

      /* THE CONTAINMENT READING, standing across every tick above. */
      CHECK(violations == 0,
            "P1: no honest process ever recorded an accept nobody announced");
      /* The stranding reading, taken off the egress itself.  The guard
       * admits egress whenever the laggard is reachable, which includes
       * the pre-cut warm-up; it contributes nothing there only because
       * this harness drains to fixpoint before the first retry, so the
       * pre-cut A-Casts are accepted everywhere before any retry egress
       * exists to count.  What it reads, in effect, is the heal: while
       * an A-Cast's value was missing at the laggard, the honest cohort
       * kept it in that instance's READY recipient set.  The load-bearing witness is the ACCEPT loop
       * above, which is end-to-end; this one adds only that the
       * carrying is visible in the egress the whole way.
       *
       * NOT witnessed here: that any counted READY was delivered, and
       * that the eventual ACCEPT was caused by the counted egress
       * rather than by another path.  There is no check on
       * PLagSuppressed, because its being zero is entailed by the
       * guard above rather than measured: the guard admits an act only
       * while the laggard has not accepted, so the laggard announced
       * no accept, so its bit is in no sender's accepted mask, so it
       * cannot be in a suppress mask, which is that mask net of arms.
       * Only a mis-indexed announcement could break it, and that is
       * what `violations` already covers. */
      CHECK(PLagServed > 0,
            "P1: honest READY egress carried the laggard while it lacked");
      /* A reading of the drain discipline in THIS arm, not a law. */
      CHECK(PLagOther == 0,
            "P1: READY alone did the carrying -- no INITIAL, no ECHO");

      /* What the announcement lie costs its teller: nothing lasting.
       * Measured, against the obvious reading.  The honest processes do
       * drop the forger from their READY recipient sets on its false
       * announcement -- but the forger's own machine then re-sends
       * unmarked toward each of them, since it genuinely lacks their
       * accepts, and each such re-send arms its receiver, whose next
       * egress carries the announcement.  After a long drive the forger
       * stands short on exactly the instance every honest process is
       * short on, the decided-0 one the retry gate skips.  The lie buys
       * one pass of delay per instance and leaves no mark. */

      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= nAct - 2, "P1: |SubSet| >= n-t");
      for (p = 1; p < 6; ++p) {
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "P1: honest SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "P1: honest SubSet contents byte-identical");
      }
      for (j = 0; j < sz0; ++j)
        for (p = 0; p < 6; ++p)
          CHECK(bkr94acsAcastValue(processes[p], subset0[j]) != 0,
                "P1: Part D -- SubSet members have accepted values");

      /* The audit runs per tick, so its two counts are bit READINGS
       * accumulated over the drive rather than distinct bits.  Its
       * DISCRIMINATING window is early convergence only: PAnnounced is
       * monotone and never cleared, so once every honest process has
       * announced every instance to every other, a later re-scan can
       * no longer find an unearned bit.  The zero is standing; the
       * power to detect is not. */
      printf("      P1: %u forged announcements, %u unearned bit readings,"
             " %u unannounced, %u egresses carried the laggard and %u"
             " dropped it (%u non-READY), |SubSet| %u, forger completed %u\n",
             PForgedEarly, unearned, violations, PLagServed, PLagSuppressed,
             PLagOther, sz0, processes[6]->complete);
      pReset();
      freeCluster(processes, 7);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("P2: a forged unmarked READY buys one re-send per pass, aimed at"
         " the forger");
  /* ---------------------------------------------------------------- */
  {
    /* n=4 t=1, process 3 forging the RECEIVED half alone -- the
     * sentence's own subject.  It announces its accepts honestly, so
     * the honest processes announce back and it really does hold their
     * accepts; every READY it re-sends then DENIES holding them.  Each
     * re-send is built from the forger's OWN mask as of that moment,
     * so the mark stripped is the one its honest egress would have
     * carried to that same recipient -- a forged missing RECEIVED
     * rather than an honestly unmarked one, which is what PStripped
     * counts.
     *
     * Once the honest three have covered each other the only READY
     * egress left anywhere is the one the lie buys, so the two
     * questions the sentence asks -- how many, and aimed where -- are
     * both readable off that egress.
     *
     * Run at two forger rates, and then STOPPED at both.  Nothing here
     * says what the answer should be: while the forger keeps re-arming
     * there is an arm outstanding at every egress whatever the arm is
     * made of, so the running yield is the cursor's cadence and settles
     * nothing.  The drain after the stop is the lane where a bitmap and
     * a counter would differ, and what it prints is what this arm
     * claims. */
    static const unsigned int rates[] = { 1, 4 };
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct sweepPolicy pol[MAX_PROCESSES];
    struct bkr94acsAct acastOut[1];
    unsigned int aimed[2];
    unsigned int reaching[2];
    unsigned int drained[2];
    unsigned int lastInc[2];
    unsigned int passes[2];
    unsigned int stripped[2];
    unsigned int nAct = 4;
    unsigned int mr = 6;
    unsigned int ri, tick, n, p, armedAt, drainAt;

    for (ri = 0; ri < sizeof (rates) / sizeof (rates[0]); ++ri) {
      aimed[ri] = 0;
      reaching[ri] = 0;
      drained[ri] = 0;
      passes[ri] = 0;
      stripped[ri] = 0;
      if (allocCluster(processes, 4, 1, 0, 2))
        continue;
      pReset();
      PForger = 3;
      PForgeUnmarked = 1;
      PReplayOn = 1;
      PRate = rates[ri];
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < nAct; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();
      for (p = 0; p < nAct; ++p) {
        acasts[p] = (unsigned char)(0x30 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, nAct, acastOut, n, 1, 0,
                         -1);
      }

      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete)
          break;
      }
      CHECK(tick < 40000, "P2: the honest processes complete beside the forger");

      /* Settle first: the arm prices what is left once the honest
       * processes have finished covering each other, so the measurement
       * window must not include the ordinary exchange. */
      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        if (pol[0].barren && pol[1].barren && pol[2].barren)
          break;
      }
      CHECK(tick < 40000, "P2: the honest processes settle");

      /* THE SETTLE'S PREMISE, witnessed rather than inferred.  A barren
       * sweep is an input-act fact; what the window below needs is a
       * MASK fact -- that no honest process still owes a READY to
       * another -- and the two are different.  So read the masks: a
       * non-forger bit still clear in any honest instance's READY
       * suppress mask means the settle was cursor luck. */
      {
        unsigned int openBits = 0, hp, hq, hj, hr, hb;

        for (hp = 0; hp < nAct; ++hp) {
          const struct bracha87Fig1 *f1;
          const unsigned char *sk;

          if ((int)hp == PForger) continue;
          for (hj = 0; hj < nAct; ++hj) {
            if ((f1 = bkr94acsAcastFig1(processes[hp], (unsigned char)hj))
             && bracha87Fig1Value(f1)
             && (sk = bracha87Fig1Skip(f1, BRACHA87_READY_ALL)))
              for (hq = 0; hq < nAct; ++hq)
                if ((int)hq != PForger && hq != hp && !BRACHA87_SKIP_TST(sk, hq))
                  ++openBits;
            for (hr = 0; hr < mr; ++hr)
              for (hb = 0; hb < nAct; ++hb)
                if ((f1 = bkr94acsBaFig1(processes[hp], (unsigned char)hj,
                                         (unsigned char)hr, (unsigned char)hb))
                 && bracha87Fig1Value(f1)
                 && (sk = bracha87Fig1Skip(f1, BRACHA87_READY_ALL)))
                  for (hq = 0; hq < nAct; ++hq)
                    if ((int)hq != PForger && hq != hp && !BRACHA87_SKIP_TST(sk, hq))
                      ++openBits;
          }
        }
        CHECK(openBits == 0,
              "P2: at the settle the honest masks are complete toward"
              " each other");
      }
      armedAt = pol[0].sweeps + pol[1].sweeps + pol[2].sweeps;
      PAimArmed = 1;
      for (tick = 0; tick < 2000; ++tick)
        pTick(processes, obs, cursors, pol, nAct, mr);
      passes[ri] = pol[0].sweeps + pol[1].sweeps + pol[2].sweeps - armedAt;
      aimed[ri] = PAimedAtForger;
      reaching[ri] = PReachingCorrect;

      /* THE DRAIN, and it is the only lane that can tell a bitmap from
       * a counter.  While the forger keeps re-arming there is an arm
       * outstanding at every egress either way, so the yield under a
       * counter would look exactly like the yield under a bitmap --
       * which is why the two rates above cannot settle the question.
       * Stop the forger entirely -- no replay, no stripping of its own
       * machine's honest egress -- and keep counting.
       *
       * The two readings that separate the shapes, and why a relative
       * comparison between the lanes could not: three honest cursors
       * emit at most one aimed READY each per tick, so any backlog
       * deeper than the window saturates it, and a counter would
       * saturate it in BOTH lanes alike.  A bitmap holds one bit per
       * (instance, sender), so it owes at most one masked READY per
       * armed instance per honest process -- an ABSOLUTE bound, not a
       * ratio -- and it owes them inside the next sweep, after which
       * the last increment tick stops moving. */
      PReplayOn = 0;
      PForgeUnmarked = 0;
      drainAt = PAimedAtForger;
      lastInc[ri] = 0;
      {
        unsigned int prev = PAimedAtForger;

        for (tick = 0; tick < 2000; ++tick) {
          pTick(processes, obs, cursors, pol, nAct, mr);
          if (PAimedAtForger != prev) {
            lastInc[ri] = tick;
            prev = PAimedAtForger;
          }
        }
      }
      drained[ri] = PAimedAtForger - drainAt;
      PAimArmed = 0;

      CHECK(PReadyEgress > 0,
            "P2: the forgery keeps a READY egress alive after the settle");
      /* WHAT THIS WITNESSES.  By the settle above, nothing is owed to a
       * correct process inside this window -- that is what made the
       * residue egress readable in isolation.  So this is the
       * RECIPIENT SET reading, not an end-to-end non-displacement one:
       * every egress the lie keeps alive names the forger and nobody
       * else.  Non-displacement rests on that plus the per-recipient
       * mask, and P1 is where a correct process is concurrently owed. */
      CHECK(PReachingCorrect == 0,
            "P2: and every one of them is aimed at the forger alone");
      CHECK(PArmsSent > 0,
            "P2: the forger really did keep re-sending unmarked");
      CHECK(PStripped > 0,
            "P2: and the unmarked bit is a LIE -- it held the accept");
      stripped[ri] = PStripped;
      pReset();
      freeCluster(processes, 4);
    }
    /* WHAT THESE TWO LANES DO AND DO NOT SETTLE.  That the armed yield
     * is one masked READY per instance per pass is NOT a fact about
     * the annotation: the retry cursor visits an instance once per
     * pass and one visit emits at most one READY act, so any
     * non-quiescent instance is served at exactly that cadence.  The
     * lanes are here for the two things that ARE the annotation's --
     * that nothing owed to a correct process is displaced at either
     * rate, and that stopping the forger drains at a cost the forger's
     * rate did not buy. */
    CHECK(reaching[0] == 0 && reaching[1] == 0,
          "P2: the residue egress names the forger alone at either rate");
    CHECK(passes[0] > 0 && passes[1] > 0,
          "P2: both lanes completed passes to price");
    CHECK(drained[0] > 0 && drained[1] > 0,
          "P2: stopping the forger leaves the accumulated arms owed");
    /* One masked READY per armed instance per honest process is the
     * most a bitmap can owe; the sweep length bounds the armed
     * instances and there are nAct-1 honest processes.  A counter that
     * accumulated one arm per re-send would owe the window's whole
     * capacity in both lanes. */
    CHECK(drained[0] <= (nAct - 1) * (nAct + nAct * mr * nAct)
       && drained[1] <= (nAct - 1) * (nAct + nAct * mr * nAct),
          "P2: the drain owes at most one per armed instance per honest"
          " process at either rate");
    CHECK(lastInc[0] < 2 * (nAct + nAct * mr * nAct)
       && lastInc[1] < 2 * (nAct + nAct * mr * nAct),
          "P2: and falls silent within a sweep at either rate");
    printf("      P2: rate 1: %u aimed / %u passes, %u drained (silent at"
           " tick %u), %u stripped; rate 4: %u aimed / %u passes, %u"
           " drained (silent at tick %u), %u stripped; %u reaching a"
           " correct process\n",
           aimed[0], passes[0], drained[0], lastInc[0], stripped[0],
           aimed[1], passes[1], drained[1], lastInc[1], stripped[1],
           reaching[0] + reaching[1]);
  }

  /* ---------------------------------------------------------------- */
  BANNER("P3: the re-armed residue is mask-complete and still owed");
  /* ---------------------------------------------------------------- */
  {
    /* M1's residue is the never-announcer: its bit is MISSING from every
     * survivor's accepted evidence, which is why their gates stand
     * short.  This residue is the opposite shape and the same outcome.
     * The forger announces -- falsely, from its first READY -- so the
     * evidence is COMPLETE at all n, and what holds the gate open is
     * the arm its unmarked re-sends keep taking.  The ending is the
     * same one M1 reaches: the barren gate, on the harness policy's own
     * S, with the honest outcome unharmed.
     *
     * TWO LANES, because the obvious reading of the paragraph above is
     * wrong and only a counterfactual can say so.  Lane 0 forges the
     * announcement, lane 1 does not; everything else is identical.  If
     * the announcement forgery were what completes the masks, lane 1
     * would come up short.  The comparison is the arm -- the prose
     * follows whatever it prints, and the CHECK below is written to
     * the measurement rather than to the expectation. */
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct sweepPolicy pol[MAX_PROCESSES];
    struct bkr94acsAct acastOut[1];
    unsigned char subset0[MAX_PROCESSES];
    unsigned char subsetP[MAX_PROCESSES];
    unsigned int nAct = 4;
    unsigned int mr = 6;
    unsigned int served, complete, tick, n, p, j, r, b, q, sz0, szP;
    unsigned int lane;
    unsigned int laneServed[2];
    unsigned int laneComplete[2];
    unsigned int laneAimed[2];

    for (lane = 0; lane < 2; ++lane) {
    laneServed[lane] = 0;
    laneComplete[lane] = 0;
    laneAimed[lane] = 0;
    if (allocCluster(processes, 4, 1, 0, 2) == 0) {
      pReset();
      PForger = 3;
      PForgeAccepted = (lane == 0) ? 1 : 0;
      PForgeUnmarked = 1;
      PReplayOn = 1;
      for (p = 0; p < MAX_PROCESSES; ++p)
        obsInit(&obs[p]);
      for (p = 0; p < nAct; ++p) {
        bracha87RetryInit(&cursors[p]);
        memset(&pol[p], 0, sizeof (pol[p]));
      }
      qReset();
      for (p = 0; p < nAct; ++p) {
        acasts[p] = (unsigned char)(0x40 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, nAct, acastOut, n, 1, 0,
                         -1);
      }
      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        if (processes[0]->complete && processes[1]->complete
         && processes[2]->complete)
          break;
      }
      CHECK(tick < 40000, "P3: the honest processes complete");

      /* The ending: the harness policy's S, reached under the shared
       * narrow progress definition -- the forger's re-sends are
       * duplicates and return no acts, so they are not progress. */
      for (tick = 0; tick < 40000; ++tick) {
        pTick(processes, obs, cursors, pol, nAct, mr);
        if (pol[0].barren >= BARREN_S && pol[1].barren >= BARREN_S
         && pol[2].barren >= BARREN_S)
          break;
      }
      CHECK(tick < 40000,
            "P3: the honest barren counters reach the policy's S");

      /* THE SHAPE.  Every served instance's accepted evidence covers
       * all n -- the forger's bit among them, and unearned.  M1's
       * survivors read the opposite here. */
      served = 0;
      complete = 0;
      for (p = 0; p < 3; ++p) {
        for (j = 0; j < nAct; ++j) {
          const struct bracha87Fig1 *f1;
          const unsigned char *ans;

          if (!(f1 = bkr94acsAcastFig1(processes[p], (unsigned char)j))
           || !bracha87Fig1Value(f1))
            continue;
          /* Scope per F1b: a BA that decided 0 takes its A-Cast out of
           * the retry walk, so there the gate is the retire and the
           * evidence carries no claim. */
          if (!bkr94acsBaDecision(processes[p], (unsigned char)j))
            continue;
          ++served;
          ans = bracha87Fig1Received(f1);
          for (q = 0; q < nAct; ++q)
            if (!ans || !BRACHA87_SKIP_TST(ans, q))
              break;
          if (q == nAct)
            ++complete;
        }
        for (j = 0; j < nAct; ++j)
          for (r = 0; r < mr; ++r)
            for (b = 0; b < nAct; ++b) {
              const struct bracha87Fig1 *f1;
              const unsigned char *ans;

              if (!(f1 = bkr94acsBaFig1(processes[p], (unsigned char)j,
                                        (unsigned char)r, (unsigned char)b))
               || !bracha87Fig1Value(f1))
                continue;
              ++served;
              ans = bracha87Fig1Received(f1);
              for (q = 0; q < nAct; ++q)
                if (!ans || !BRACHA87_SKIP_TST(ans, q))
                  break;
              if (q == nAct)
                ++complete;
            }
      }
      CHECK(served > 0, "P3: served instances were actually examined");
      CHECK(served == complete,
            "P3: every served instance's accepted evidence covers all n");

      /* And yet nothing rests: the arm is retaken on every unmarked
       * re-send, so the retry keeps owing across a bounded further
       * drive -- and owes it to the forger alone. */
      PAimArmed = 1;
      for (tick = 0; tick < 500; ++tick)
        pTick(processes, obs, cursors, pol, nAct, mr);
      PAimArmed = 0;
      CHECK(PAimedAtForger > 0,
            "P3: the retry still owes after the gate, mask-complete or not");
      CHECK(PReachingCorrect == 0,
            "P3: and owes it to the forger alone");
      CHECK(PArmsSent > 0, "P3: the forger kept re-sending unmarked");
      /* PArmsSent counts unmarked READYs DELIVERED by the replay, not
       * arms recorded -- ProcessResend records nothing at a receiver
       * that has not accepted.  In this lossless run the two coincide;
       * the name is the honest one. */
      /* Lane 0 only, and it is not a non-vacuity guard for anything
       * below it: the lane comparison at the end of the arm is what
       * says whether this lie carried any of the weight. */
      if (lane == 0)
        CHECK(PForgedEarly > 0,
              "P3: lane 0 announced accepts it did not hold");
      laneServed[lane] = served;
      laneComplete[lane] = complete;
      laneAimed[lane] = PAimedAtForger;

      sz0 = bkr94acsSubset(processes[0], subset0);
      CHECK(sz0 >= 3, "P3: |SubSet| >= n-t");
      for (p = 0; p < 3; ++p) {
        CHECK(processes[p]->complete, "P3: honest process completed");
        szP = bkr94acsSubset(processes[p], subsetP);
        CHECK(szP == sz0, "P3: honest SubSet sizes agree");
        if (szP == sz0)
          CHECK(memcmp(subset0, subsetP, sz0) == 0,
                "P3: honest SubSet contents byte-identical");
      }
      printf("      P3 lane %u (announcement forgery %s): %u served,"
             " %u covering all n, %u arms, %u aimed, %u forged,"
             " |SubSet| %u\n",
             lane, lane == 0 ? "on" : "off", served, complete,
             PArmsSent, PAimedAtForger, PForgedEarly, sz0);
      pReset();
      freeCluster(processes, 4);
    }
    }

    /* THE COUNTERFACTUAL, and what it can and cannot say.  The claim
     * is that the announcement forgery moves none of the residue: the
     * masks fill because this forger goes on to accept and announce
     * like anyone else, and the gate stays open because of the
     * re-arming alone.  Lane 1 is the witness -- every served instance
     * mask-complete with the announcement lie never told, and the arm
     * still owing.  The lane's own per-lane checks already required
     * both; these two name the counterfactual as such.
     *
     * NOT compared across lanes: the aimed count and the arm count.
     * The replay arms every captured instance before every honest
     * RetryStep in the same tick, so the aimed count is honest
     * processes x drive ticks in either lane by construction, and the
     * arm count is ticks x captured x honest.  Their equality would be
     * schedule identity, not the property.  Nor does the lie cost the
     * forger anything here: it re-arms every tick, so the suppression
     * its announcement invites never holds long enough to matter. */
    CHECK(laneComplete[1] == laneServed[1],
          "P3: without the announcement lie every served instance is"
          " still mask-complete");
    CHECK(laneAimed[1] > 0,
          "P3: and the gate is still held open by the arm alone");
  }

  /* ================================================================ */
  /*  Section Q -- the paired payload's retire.                       */
  /*                                                                  */
  /*  bkr94acs.h names two entries for an application that pairs a    */
  /*  side-channel payload with its A-Cast: an all-or-nothing stop    */
  /*  and its per-process refinement, both read at the initiator.     */
  /*  The header's claim is that a correct process leaves the side    */
  /*  channel's recipient set once it has proven it holds the         */
  /*  payload, and that the stop is reached once every process has.   */
  /*  Q1 drives the composition with every mask honored and the       */
  /*  annotation exchange run to quiescence, and reads the two        */
  /*  entries at every honest initiator.  Q2 places the hold.         */
  /* ================================================================ */

  /* ---------------------------------------------------------------- */
  BANNER("Q1: the paired payload's retire set closes for every correct process");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Per bkr94acs.h at bkr94acsAcastReadied, "under the hold at Input
     * a process proves it holds the paired payload the moment it
     * readies, and the side channel stops re-sending to it then"; at
     * bkr94acsAcastAllReadied "every correct process's READY eventually
     * reaches the initiator", and "Under <= t silent processes this
     * never returns 1".  So at quiescence of an all-honest run every process is in
     * the set at every initiator and the stop reads 1; with one
     * silent process the stop stays 0 and the set lacks exactly the
     * silent one.
     *
     * The schedule is the one that separates the readied set from the
     * echoed set: one process R receives no row of initiator X's
     * A-Cast until X has sent READY, so R's own first echo of it goes
     * out under a mask that already holds X (bkr94acsAcastInput
     * attaches bracha87Fig1Skip to the first send too), and no later
     * re-send reaches X either -- ECHO is suppressed toward readied
     * processes and retires at ACCEPTED.  A set that closes must
     * close through the READY re-send and the annotation exchange,
     * which never retire toward a process that has not announced.
     * Loss is a second lane; it reaches the same state without any
     * ordering at all.
     */
    struct bkr94acs *processes[MAX_PROCESSES];
    struct processObs obs[MAX_PROCESSES];
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(6)];
    struct bkr94acsAct acastOut[1];
    unsigned char acasts[MAX_PROCESSES];
    struct wire w;
    static struct wire held[4096];
    unsigned int nHeld;
    unsigned int quiesced[MAX_PROCESSES];
    unsigned int nQuiesced;
    unsigned int nLive;
    unsigned int iter;
    unsigned int p;
    unsigned int q;
    unsigned int b;
    unsigned int n;
    unsigned int li;
    static const struct {
      unsigned int n;
      unsigned int t;
      unsigned int drop;
      int silent;         /* -1 = none */
      unsigned int iters; /* the drive's cap; a silent lane never quiesces */
      unsigned int hold;  /* 0: no rows held -- with a silent process X
                           * needs every live echo to ready, so the
                           * schedule cannot be forced there */
    } lanes[] = {
      { 4, 1,  0, -1, 20000, 1 },
      { 4, 1, 25, -1, 20000, 1 },
      { 7, 2,  0, -1, 20000, 1 },
      { 7, 2, 25, -1, 20000, 1 },
      { 4, 1,  0,  3,   400, 0 },
      /* t = 0: the set must still gain self, through its own hand-back
       * (bracha87.h at bracha87Fig1ProcessAccepted).  Under this FIFO
       * drive every echo crosses before a READY arrives, so READY and
       * ACCEPT come out of different Inputs here; the one-Input shape
       * is driven in the bracha87 black-box annotation section and by
       * the explorer's s1.  No hold: X needs every echo. */
      { 2, 0,  0, -1, 20000, 0 },
    };
    const unsigned int X = 0;

    for (li = 0; li < sizeof (lanes) / sizeof (lanes[0]); ++li) {
      const unsigned int N = lanes[li].n;
      const unsigned int R = N - 1 - (lanes[li].silent == (int)(N - 1) ? 1 : 0);
      unsigned int released;

      rngSeed(0x0A10u + li);
      if (allocCluster(processes, N, lanes[li].t, 0, 4) != 0)
        continue;
      qReset();
      nHeld = 0;
      released = lanes[li].hold ? 0 : 1;
      nQuiesced = 0;
      nLive = 0;
      for (p = 0; p < N; ++p) {
        obsInit(&obs[p]);
        bracha87RetryInit(&cursors[p]);
        quiesced[p] = 0;
        if (lanes[li].silent != (int)p)
          ++nLive;
      }
      for (p = 0; p < N; ++p) {
        if (lanes[li].silent == (int)p)
          continue;
        acasts[p] = (unsigned char)(0xB0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, N, acastOut, n, 1,
                         lanes[li].drop, lanes[li].silent);
      }
      for (iter = 0; iter < lanes[li].iters && nQuiesced < nLive; ++iter) {
        while (qSize() > 0) {
          qPopHead(&w);
          if (lanes[li].silent == (int)w.to)
            continue;
          /* Hold every row of X's A-Cast bound for R until X has sent
           * READY on it; then release them behind that READY. */
          if (!released && w.cls == BKR94ACS_CLS_ACAST && w.process == X
           && w.to == R) {
            CHECK(nHeld < sizeof (held) / sizeof (held[0]),
                  "Q1: the held rows fit");
            if (nHeld < sizeof (held) / sizeof (held[0]))
              held[nHeld++] = w;
            continue;
          }
          if (w.cls == BKR94ACS_CLS_ACAST)
            n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                   wireAnnot(&w), w.from, w.value, out);
          else
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                w.initiator, w.type, wireAnnot(&w), w.from,
                                w.baValue, out);
          if (w.type == BRACHA87_READY && !w.received && quiesced[w.to]) {
            quiesced[w.to] = 0;
            --nQuiesced;
          }
          observeAndOutput(&obs[w.to], w.to, N, out, n, 1,
                           lanes[li].drop, lanes[li].silent);
          if (!released
           && (bkr94acsAcastFig1(processes[X], (unsigned char)X)->flags
               & BRACHA87_F1_RDSENT)) {
            released = 1;
            for (q = 0; q < nHeld; ++q)
              qPush(&held[q]);
          }
        }
        for (p = 0; p < N; ++p) {
          if (lanes[li].silent == (int)p)
            continue;
          if (!quiesced[p]) {
            n = bkr94acsRetryStep(processes[p], &cursors[p], out);
            if (!n && bkr94acsFig1SentCount(processes[p])) {
              quiesced[p] = 1;
              ++nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, N, out, n, 1,
                             lanes[li].drop, lanes[li].silent);
          }
          for (b = 0; b < N; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b,
                                     out)) > 0 && turnDrained()) {
              if (quiesced[p]) {
                quiesced[p] = 0;
                --nQuiesced;
              }
              observeAndOutput(&obs[p], (unsigned char)p, N, out, n, 1,
                               lanes[li].drop, lanes[li].silent);
            }
          n = bkr94acsFanout(processes[p], out);
          if (n) {
            if (quiesced[p]) {
              quiesced[p] = 0;
              --nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, N, out, n, 1,
                             lanes[li].drop, lanes[li].silent);
          }
        }
      }
      if (lanes[li].hold)
        CHECK(released, "Q1: X sent READY, so the held rows were released");
      for (p = 0; p < N; ++p) {
        if (lanes[li].silent == (int)p)
          continue;
        CHECK(processes[p]->complete, "Q1: every live process completes");
      }
      if (lanes[li].silent < 0) {
        CHECK(nQuiesced == nLive, "Q1: every process reached the Retry 0 return");
        /* The witness of the schedule: R's own instance of X's A-Cast
         * readied and accepted -- R took part -- so R is a correct
         * process that holds X's payload by every reading. */
        CHECK(bkr94acsAcastFig1(processes[R], (unsigned char)X)->flags
              & BRACHA87_F1_ACCEPTED,
              "Q1: R accepted X's A-Cast");
      }
      for (b = 0; b < N; ++b) {
        const unsigned char *set;

        if (lanes[li].silent == (int)b)
          continue;
        set = bkr94acsAcastReadied(processes[b], (unsigned char)b);
        CHECK(set != 0, "Q1: the retire set is readable at the initiator");
        if (!set)
          continue;
        for (q = 0; q < N; ++q) {
          if (lanes[li].silent == (int)q) {
            CHECK(!BRACHA87_SKIP_TST(set, q),
                  "Q1: the silent process is not in the retire set");
            continue;
          }
          CHECK(BRACHA87_SKIP_TST(set, q),
                "Q1: every correct process is in the retire set at its initiator");
        }
        CHECK(bkr94acsAcastAllReadied(processes[b], (unsigned char)b)
              == (lanes[li].silent < 0),
              lanes[li].silent < 0
              ? "Q1: the all-or-nothing stop reads 1 at quiescence"
              : "Q1: the all-or-nothing stop stays 0 under a silent process");
      }
      printf("      Q1 lane %u (n=%u t=%u drop=%u%% silent=%d): %u iterations,"
             " %u quiesced of %u\n",
             li, N, lanes[li].t, lanes[li].drop, lanes[li].silent, iter,
             nQuiesced, nLive);
      freeCluster(processes, N);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Q2: the hold is at Input");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Per bkr94acs.h at bkr94acsAcastAllReadied: "THE HOLD IS AT
     * INPUT: a receiver feeds NO row of process's A-Cast ... until it
     * holds the payload ... A readied process therefore holds the
     * payload, and an ACCEPT at a correct process witnesses t+1
     * correct holders.  Withholding only the ECHO's wire copies does
     * NOT do this ... at n=4 t=1 a Byzantine initiator that hands its
     * payload to one correct process is accepted everywhere."  And at
     * bkr94acsAcastValue: under the hold "a process that never
     * receives an initiator's payload never accepts that A-Cast, so
     * this stays 0 there while the BA can still decide 1 ... and every
     * other process's READY toward it never retires".
     *
     * Process 0 is the initiator that withholds its payload; it runs
     * the protocol honestly otherwise.  A payload is modeled as a
     * held flag per (receiver, initiator); every honest initiator's is
     * held everywhere from the start, process 0's only where the lane
     * says.  Lane 0: the hold at Input, one holder -- no correct
     * process accepts, BA_0 decides 0, the run completes and agrees
     * (a demonstration of the hold's outcome: the machine is fed no
     * row it could accept on, so no library defect reaches it).
     * Lane 1: the ECHO wire copies withheld instead, the self
     * hand-back kept, one holder -- accepted everywhere, the header's
     * negative sentence.  Lane 2: the hold at Input, t+1 holders --
     * BA_0 decides 1 everywhere, the non-holder never accepts and
     * reads no value, and the others' READY toward it never retires.
     */
    struct bkr94acs *processes[MAX_PROCESSES];
    struct processObs obs[MAX_PROCESSES];
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3)];
    struct bkr94acsAct acastOut[1];
    unsigned char acasts[4];
    unsigned char held[4][4];
    struct wire w;
    unsigned int iter;
    unsigned int p;
    unsigned int q;
    unsigned int b;
    unsigned int n;
    unsigned int li;
    unsigned int allComplete;
    unsigned int completeAt;
    static const struct {
      unsigned int holders;   /* bitmap of processes handed 0's payload */
      unsigned int wireHold;  /* 1: withhold ECHO wire copies, keep self */
    } lanes[] = {
      { 0x3, 0 },   /* 0 (itself) and 1 */
      { 0x3, 1 },
      { 0x7, 0 },   /* 0, 1 and 2: t+1 correct holders */
    };
    const unsigned int X = 0;

    for (li = 0; li < sizeof (lanes) / sizeof (lanes[0]); ++li) {
      rngSeed(0x0A20u + li);
      if (allocCluster(processes, 4, 1, 0, 4) != 0)
        continue;
      qReset();
      for (p = 0; p < 4; ++p) {
        obsInit(&obs[p]);
        bracha87RetryInit(&cursors[p]);
        for (q = 0; q < 4; ++q)
          held[p][q] = (q != X) || ((lanes[li].holders >> p) & 1);
      }
      for (p = 0; p < 4; ++p) {
        acasts[p] = (unsigned char)(0xC0 + p);
        n = bkr94acsAcast(processes[p], &acasts[p], acastOut);
        observeAndOutput(&obs[p], (unsigned char)p, 4, acastOut, n, 1, 0, -1);
      }
      allComplete = 0;
      completeAt = 0;
      for (iter = 0; iter < 4000; ++iter) {
        while (qSize() > 0) {
          qPopHead(&w);
          if (w.cls == BKR94ACS_CLS_ACAST) {
            if (!held[w.to][w.process]) {
              if (!lanes[li].wireHold)
                continue;                     /* the hold at Input */
            }
            /* The wire-side hold: a non-holder's ECHO reaches nobody
             * but itself.  Modeled at delivery, which is where the
             * withheld copy would otherwise arrive. */
            if (lanes[li].wireHold && w.type == BRACHA87_ECHO
             && !held[w.from][w.process] && w.to != w.from)
              continue;
            n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                   wireAnnot(&w), w.from, w.value, out);
          } else
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                w.initiator, w.type, wireAnnot(&w), w.from,
                                w.baValue, out);
          observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
        }
        for (p = 0; p < 4; ++p) {
          n = bkr94acsRetryStep(processes[p], &cursors[p], out);
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          for (b = 0; b < 4; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b, out)) > 0 && turnDrained())
              observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          n = bkr94acsFanout(processes[p], out);
          observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
        }
        allComplete = 1;
        for (p = 0; p < 4; ++p)
          if (!processes[p]->complete)
            allComplete = 0;
        /* Past completion, a few more ticks so the residue can show. */
        if (allComplete && !completeAt)
          completeAt = iter + 1;
        if (completeAt && iter >= completeAt + 50)
          break;
      }
      CHECK(allComplete, "Q2: every process completes");
      if (allComplete && li < 2)
        assertLemma2(processes, obs, 4, 1);
      else if (allComplete) {
        /* Lane 2 asserts Lemma 2 by hand: Part D reads Q(j) = 1 at a
         * single honest player (bkr94acs.h at bkr94acsBaEntered), and
         * under the hold the non-holder is not that player. */
        unsigned char s0[4];
        unsigned char sp[4];
        unsigned int sz0;
        unsigned int szp;

        sz0 = bkr94acsSubset(processes[0], s0);
        CHECK(sz0 >= 3, "Q2: Lemma 2 Part A: |SubSet| >= n-t");
        for (p = 1; p < 4; ++p) {
          szp = bkr94acsSubset(processes[p], sp);
          CHECK(szp == sz0 && memcmp(s0, sp, sz0) == 0,
                "Q2: Lemma 2 Part C: SubSets agree");
        }
        for (q = 0; q < sz0; ++q)
          CHECK(bkr94acsAcastValue(processes[1], s0[q]) != 0,
                "Q2: Lemma 2 Part D: Q(j) = 1 at an honest holder");
      }
      switch (li) {
      case 0:
        for (p = 1; p < 4; ++p) {
          CHECK(bkr94acsAcastValue(processes[p], (unsigned char)X) == 0,
                "Q2: under the hold at Input no correct process accepts"
                " a one-holder A-Cast");
          CHECK(obs[p].selfInputValue[X] == 0,
                "Q2: and enters 0 in its BA");
          CHECK(bkr94acsBaDecision(processes[p], (unsigned char)X) == 0,
                "Q2: BA_0 decides 0");
        }
        break;
      case 1:
        {
          const unsigned char *ec;

          /* The witness that the wire-side hold took effect: the
           * non-holders' echoes never reached the initiator. */
          ec = bracha87Fig1Skip(bkr94acsAcastFig1(processes[0],
                                                  (unsigned char)X),
                                BRACHA87_INITIAL_ALL);
          CHECK(ec && !BRACHA87_SKIP_TST(ec, 2) && !BRACHA87_SKIP_TST(ec, 3),
                "Q2: the withheld echoes did not reach the initiator");
        }
        for (p = 2; p < 4; ++p) {
          CHECK(bkr94acsAcastValue(processes[p], (unsigned char)X) != 0,
                "Q2: with only the ECHO wire copies withheld a non-holder"
                " accepts the one-holder A-Cast");
          CHECK(bkr94acsBaDecision(processes[p], (unsigned char)X) == 1,
                "Q2: and BA_0 decides 1 with one correct holder");
        }
        break;
      default:
        for (p = 0; p < 4; ++p)
          CHECK(bkr94acsBaDecision(processes[p], (unsigned char)X) == 1,
                "Q2: with t+1 correct holders BA_0 decides 1 everywhere");
        CHECK(bkr94acsAcastValue(processes[3], (unsigned char)X) == 0,
              "Q2: the non-holder never accepts and reads no value");
        CHECK(bkr94acsBaDecision(processes[3], (unsigned char)X) == 1
              && bkr94acsAcastValue(processes[3], (unsigned char)X) == 0,
              "Q2: a BA decided 1 for an A-Cast whose value is absent"
              " (the O1 shape, produced by the hold)");
        for (p = 0; p < 3; ++p) {
          const unsigned char *sk;
          unsigned int sweeps;
          unsigned int carried;
          unsigned int calls;

          sk = bracha87Fig1Skip(bkr94acsAcastFig1(processes[p],
                                                  (unsigned char)X),
                                BRACHA87_READY_ALL);
          CHECK(sk && !BRACHA87_SKIP_TST(sk, 3),
                "Q2: a holder's READY toward the non-holder is still owed"
                " past COMPLETE");
          /* And the next sweep carries it: the walk runs out the
           * current pass and crosses the wrap, where X's A-Cast -- X
           * is 0, cursor position 0 -- is output with the non-holder
           * unsuppressed.  The acts are observed, not sent -- the wire
           * stays as the lane left it.  The walk is bounded by the pass's own
           * ceiling (bkr94acs.h at bkr94acsFig1SentCount, plus the
           * wrap-crossing call): a machine whose sweep counter does
           * not advance reds here instead of hanging the suite. */
          sweeps = cursors[p].sweeps;
          carried = 0;
          calls = 0;
          while (cursors[p].sweeps == sweeps
              && calls <= bkr94acsFig1SentCount(processes[p])) {
            n = bkr94acsRetryStep(processes[p], &cursors[p], out);
            ++calls;
            if (!n)
              break;
            for (q = 0; q < n; ++q)
              if (out[q].act == BKR94ACS_ACT_ACAST_SEND
               && out[q].process == X && out[q].type == BRACHA87_READY
               && !(out[q].skip && BRACHA87_SKIP_TST(out[q].skip, 3)))
                ++carried;
          }
          CHECK(cursors[p].sweeps != sweeps || !n,
                "Q2: the pass closed within its own call bound");
          CHECK(carried > 0,
                "Q2: and the next sweep carries that READY to the non-holder");
        }
        /* The payload arrives late.  Per bkr94acs.h the dropped rows are
         * re-bootstrapped by "the READY re-send and the t+1-readys rule":
         * with the hold lifted, the holders' still-owed READYs carry
         * the non-holder to ACCEPT, its own READY closes the readied set
         * at X, and every process quiesces. */
        held[3][X] = 1;
        {
          unsigned int quiesced[4];
          unsigned int nQuiesced;

          for (p = 0; p < 4; ++p)
            quiesced[p] = 0;
          nQuiesced = 0;
          for (iter = 0; iter < 4000 && nQuiesced < 4; ++iter) {
            while (qSize() > 0) {
              qPopHead(&w);
              if (w.cls == BKR94ACS_CLS_ACAST) {
                if (!held[w.to][w.process])
                  continue;                   /* the hold, still in force */
                n = bkr94acsAcastInput(processes[w.to], w.process, w.type,
                                       wireAnnot(&w), w.from, w.value, out);
              } else
                n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                    w.initiator, w.type, wireAnnot(&w),
                                    w.from, w.baValue, out);
              if (w.type == BRACHA87_READY && !w.received && quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
              observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
            }
            for (p = 0; p < 4; ++p) {
              if (quiesced[p])
                continue;
              n = bkr94acsRetryStep(processes[p], &cursors[p], out);
              if (!n) {
                quiesced[p] = 1;
                ++nQuiesced;
              }
              observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
            }
          }
          CHECK(bkr94acsAcastValue(processes[3], (unsigned char)X) != 0,
                "Q2: the late holder accepts on the re-sent READYs alone");
          CHECK(bkr94acsAcastAllReadied(processes[X], (unsigned char)X) == 1,
                "Q2: and closes the readied set at the initiator");
          CHECK(nQuiesced == 4, "Q2: every process then quiesces");
        }
        break;
      }
      printf("      Q2 lane %u (%s, holders 0x%x): complete at %u%s\n", li,
             lanes[li].wireHold ? "ECHO wire copies withheld"
                                : "hold at Input",
             lanes[li].holders, completeAt,
             li == 2 ? ", then the late payload quiesced all four" : "");
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section R -- a decision in two waves through the composition.   */
  /* ---------------------------------------------------------------- */

  /* Theorem 2's Agreement: a correct decision at phase d leaves      */
  /* every correct process deciding at d or d+1, and the bound        */
  /* (bracha87.h, at bracha87Fig4Round) stops each decider's own      */
  /* rounds after the phase past its decision.  A second wave --      */
  /* deciding at d+1 -- needs the first wave's phase-(d+1) rounds and */
  /* nothing after them.  The split drive above, searched for a seed  */
  /* whose BA 3 decides in two waves, read off the rounds each        */
  /* process initiated (bkr94acsBaFig1, INITIATOR) beside the round   */
  /* its deciding turn opened (RDecideRound, 3d+3) and its turns that */
  /* fired and wrote nothing (RZeroTurns).  Required of every         */
  /* process: completion and agreement; a first-wave decider's last   */
  /* initiation is two rounds past the one its decision opened -- the */
  /* (d, v) of phase d+1 -- exactly one of its turns fired and wrote  */
  /* nothing (the bound turn), and its next round is spent            */
  /* (bkr94acsBaGetValid answers 0: the pin at bkr94acsTurn, which a  */
  /* HELD reading alone does not witness, since an unpinned decider   */
  /* sits on an incomplete round and reads HELD too); a second-wave   */
  /* decider initiates nothing past the round its decision opened and */
  /* has no such turn -- unless, n-t strong, it completes phase d+2   */
  /* among itself and stops at its own bound turn, as R2's second     */
  /* wave does (the header's "may turn r+2's rounds first").  So per  */
  /* process: at most one such turn, and the last initiation is two   */
  /* rounds past the decision's exactly when it fired.  A             */
  /* continuation cut at the decision leaves the                      */
  /* first wave short of the second wave's needs, so no seed          */
  /* completes in two waves; one left unbounded runs every process to */
  /* the ceiling alike, and no seed shows two waves; one that plays a */
  /* phase too many moves the first wave's last initiation off        */
  /* 3d+5.  Fig 1 duties after the bound are not this arm's witness:  */
  /* lossless, the first wave has echoed and readied everything the   */
  /* second wave needs before its bound turn, so a machine that       */
  /* stopped them at the pin would pass here; the loss arms (F1b, H1) */
  /* are where a retry withheld after a decision reds.                */
  /*                                                                  */
  /* R1 wants a first wave of two or more, R2 a lone first-wave       */
  /* decider: at n = 4 the second wave is then n-t, completes the     */
  /* lone decider's phase-(d+2) rounds among itself, and the pin is   */
  /* what keeps its turn from firing there (three refused turns, each */
  /* writing nothing, without it).                                    */
  for (i = 1; i <= 2; ++i) {
    const struct bracha87Fig1 *f1;
    struct rCoin coins[4];
    unsigned char sub[4][MAX_PROCESSES];
    unsigned char gs[MAX_PROCESSES], gv[MAX_PROCESSES];
    unsigned int cnt[4];
    unsigned int last[4];
    unsigned int seed, seedFound, at, p, j, r, lo, hi, first;
    int agree;

    if (i == 1) {
      BANNER("R1: a decision in two waves, a first wave of two, the continuation bounded");
    } else {
      BANNER("R2: a lone first-wave decider, its next rounds completed by the second wave, pinned");
    }
    seedFound = 0;
    at = R_NEVER;
    lo = hi = 0;
    for (seed = 1; seed <= 256 && !seedFound; ++seed) {
      unsigned long sz;

      sz = bkr94acsSz(3, 0, 12);
      for (p = 0; p < 4; ++p) {
        processes[p] = calloc(1, sz);
        obsInit(&obs[p]);
      }
      if (!processes[0] || !processes[1] || !processes[2] || !processes[3])
        break;
      for (p = 0; p < 4; ++p)
        bkr94acsInit(processes[p], 3, 1, 0, 12, (unsigned char)p, rCoinFn, &coins[p]);
      at = rDrive(processes, obs, coins, seed, 400);
      if (at != R_NEVER) {
        lo = 3 * 12;
        hi = 0;
        for (p = 0; p < 4; ++p) {
          last[p] = 0;
          for (r = 0; r < 3 * 12; ++r)
            if ((f1 = bkr94acsBaFig1(processes[p], 3, (unsigned char)r, (unsigned char)p))
             && (f1->flags & BRACHA87_F1_INITIATOR))
              last[p] = r;
          if (last[p] < lo)
            lo = last[p];
          if (last[p] > hi)
            hi = last[p];
        }
        first = 0;
        for (p = 0; p < 4; ++p)
          if (last[p] == lo)
            ++first;
        if (lo != hi && (i == 1 ? first >= 2 : first == 1)) {
          seedFound = seed;
          break;
        }
      }
      freeCluster(processes, 4);
    }
    CHECK(seedFound != 0, i == 1
          ? "R1: a seed decides BA 3 in two waves and completes"
          : "R2: a seed decides BA 3 in two waves, the first wave alone, and completes");
    if (seedFound) {
      agree = 1;
      for (p = 0; p < 4; ++p) {
        cnt[p] = bkr94acsSubset(processes[p], sub[p]);
        if (cnt[p] != cnt[0])
          agree = 0;
        else
          for (j = 0; j < cnt[p]; ++j)
            if (sub[p][j] != sub[0][j])
              agree = 0;
        if (bkr94acsBaDecision(processes[p], 3) != bkr94acsBaDecision(processes[0], 3)
         || bkr94acsBaDecision(processes[p], 3) > 1)
          agree = 0;
      }
      CHECK(agree, i == 1 ? "R1: BA 3 and the subsets agree" : "R2: BA 3 and the subsets agree");
      CHECK(lo % BRACHA87_ROUNDS_PER_PHASE == 2 && hi <= lo + BRACHA87_ROUNDS_PER_PHASE,
            i == 1 ? "R1: the waves are one phase apart" : "R2: the waves are one phase apart");
      for (p = 0; p < 4; ++p) {
        CHECK(RDecideRound[p] != R_NEVER && RDecideRound[p] % BRACHA87_ROUNDS_PER_PHASE == 0,
              "R: every process's deciding turn opened a phase");
        CHECK(RZeroTurns[p] <= 1,
              "R: at most one turn of a process fired and wrote nothing -- its bound turn");
        CHECK(last[p] == RDecideRound[p] + (RZeroTurns[p] ? 2 : 0),
              "R: a process's last initiation is the (d, v) of the phase after its decision when its bound turn fired, else the round its decision opened");
        if (RZeroTurns[p])
          CHECK(bkr94acsBaGetValid(processes[p], 3, gs, gv) == 0,
                "R: past its bound turn a process's round space is spent");
        if (last[p] == lo) {
          CHECK(RZeroTurns[p] == 1,
                i == 1 ? "R1: exactly one turn of a first-wave decider fired and wrote nothing"
                       : "R2: exactly one turn of the lone decider fired and wrote nothing");
          CHECK(bkr94acsBaGetValid(processes[p], 3, gs, gv) == 0,
                i == 1 ? "R1: the first wave's next round is spent"
                       : "R2: the lone decider's next round is spent");
        }
        CHECK(bkr94acsTurnDuty(processes[p], 3) == BKR94ACS_DUTY_HELD,
              i == 1 ? "R1: every BA 3 turn is HELD at the end" : "R2: every BA 3 turn is HELD at the end");
      }
      printf("      R%u: seed %u, complete at tick %u; last round initiated in BA 3:"
             " %u %u %u %u; decisions opened %u %u %u %u; tosses %u/%u/%u/%u\n", i, seedFound, at,
             last[0], last[1], last[2], last[3],
             RDecideRound[0], RDecideRound[1], RDecideRound[2], RDecideRound[3],
             coins[0].calls, coins[1].calls, coins[2].calls, coins[3].calls);
      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Summary                                                         */
  /* ---------------------------------------------------------------- */

  printf("\n=================================\n");
  printf("test_bkr94acs_blackbox: %d checks, %d failures\n",
         Checks, Failures);
  if (Failures) {
    printf("FAILED\n");
    return (1);
  }
  printf("PASSED\n");
  return (0);
}
