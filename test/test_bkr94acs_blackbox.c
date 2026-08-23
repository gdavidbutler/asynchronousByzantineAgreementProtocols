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
 *      patience values: eager excludes the delayed honest process (F1),
 *      patience includes it (F2); a dead slot holds TOLERANCE forever
 *      and finite patience completes past it (F3).  Duty
 *      trichotomy monotone (MET absorbing, TOLERANCE never back to
 *      HELD) at every fDrive sweep.
 *   G. Round-turn pacing -- deliveries bank and decide nothing (G1),
 *      TOLERANCE needs the caller's elapsed signal (G2), MET fires
 *      without it (G3), a drained instance is turn-quiescent (G4).
 *   H. Quiescence is REACHABLE at the ACS surface (H1), and the want
 *      ingress entries' contracts (H2).
 *   I. Partition heal -- READY re-offers alone carry a returner that
 *      holds zero evidence of an instance (I1); a 2/2 cut leaves
 *      neither side n-t and heals (I2).
 *   J. Asymmetric flow -- the receive-only half completes and agrees
 *      (J1); the send-only half feeds everyone and sees pure
 *      barrenness (J2).
 *   K. Byzantine trickle -- a bounded stretch of the barren gate, the
 *      exhaustion of the trickler's supply, value-blind dedup, sweep
 *      inflation, and the derived ceiling (K1).
 *   L. Staggered start -- the pre-accept lane bootstraps on live
 *      INITIAL re-offers (L1); the post-fanout lane still completes
 *      and agrees (L2).
 *   M. After COMPLETE -- a never-announcing leaver holds every
 *      survivor's READY gate open, and the barren backstop is what
 *      ends the drive (M1).
 *   N. The sustained-rate skew lane -- the fairness non-invariant and
 *      its cost scaling (N1); duty verdicts are pure functions of
 *      state (N2).
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
 * (bkr94acsTurn) fire from the caller's sweep, so every driver here
 * bridges at ZERO patience -- the eager schedule -- except
 * where a section makes one of the two the isolated variable.
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
 ,unsigned char phase
){
  (void)closure;
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
  unsigned char answered;     /* READY only: BKR94ACS_ANSWERED wire bit --
                               * decided per recipient from the act's
                               * .answer mask; its ABSENCE is the want */
  unsigned char value[MAX_VLEN]; /* ACAST only (vLen bytes) */
};

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
        w.answered = acts[i].answer
                  && BRACHA87_SKIP_TST(acts[i].answer, j);
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
        w.answered = acts[i].answer
                  && BRACHA87_SKIP_TST(acts[i].answer, j);
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

/* BA round turns at ZERO patience -- the bridge bkr94acs.h
 * prescribes for a caller that wants the eager schedule: after any
 * delivery or retry that may have banked evidence, turn every BA that
 * became turnable.  The while() is required (cascaded validation can
 * unlock several successive rounds), and the sweep runs over ALL BAs
 * of the instance because an A-Cast accept enters round 0 of a BA the
 * arrival did not name.  BA_SENDs go to the wire; BA_DECIDED /
 * COMPLETE / BA_EXHAUSTED are observation-only. */
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
    while ((n = bkr94acsTurn(process, (unsigned char)b, 1, out)) > 0) {
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
    n = bkr94acsAcastInput(process, w->process, w->type, w->from,
                              w->value, out);
    CHECK(n <= 3, "A-Cast input act count within its bound (2 + enter-1)");
  } else {
    n = bkr94acsBaInput(process, w->process, w->round, w->initiator,
                               w->type, w->from, w->baValue, out);
    CHECK(n <= 2, "BA input act count within its bound (echo/ready only)");
  }
  observeAndOutput(obs, w->to, nAct, out, n, vBytes, 0, -1);

  /* Sweep-side decisions at zero patience, turns first
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
 ,unsigned int maxPhases
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

  actsCap = BKR94ACS_MAX_ACTS(nAct - 1, maxPhases);
  out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
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
  CHECK(sz0 >= nAct - t, "Lemma 2 Part A: |SubSet| >= n-t");

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
    processes[i] = (struct bkr94acs *)calloc(1, sz);
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
/*    2. Call bkr94acsRetry once on every non-silent process; output acts.  */
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
 ,unsigned int maxPhases
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

  actsCap = BKR94ACS_MAX_ACTS(nAct - 1, maxPhases);
  out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
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
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type, w.from,
                                  w.value, out);
        /* BPR ACCEPTED annotation rides on a READY; feed it AFTER Input
         * (which records rdFrom) so acFrom stays a subset of rdFrom. */
        if (w.accepted)
          bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
        /* The ANSWERED bit's ABSENCE is the want: the sender has not
         * recorded our accept, so un-suppress it for the next egress. */
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsAcastWants(processes[w.to], w.process, w.from);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, w.from,
                                   w.baValue, out);
        if (w.accepted)
          bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                    w.initiator, w.from);
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsBaWants(processes[w.to], w.process, w.round,
                                 w.initiator, w.from);
      }
      observeAndOutput(&obs[w.to], w.to, nAct, out, n, vLen,
                     dropPercent, silentProcess);
      /* Sweep-side decisions at zero patience, turns first (see
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
      n = bkr94acsRetry(processes[i], &cursors[i], retryOut);
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
      cur = bkr94acsSentFig1Count(processes[i]);
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
/*  caller's schedule: nonzero drains turns at zero patience after     */
/*  every input (the eager schedule D1/D2 want, counting EXHAUSTED     */
/*  from the turn's acts), zero banks without turning (Section G,      */
/*  which must read a duty class over a round the caller has not yet   */
/*  consumed).                                                        */
/* ------------------------------------------------------------------ */

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
                             BRACHA87_INITIAL, initiator, value, out);
  CHECK(n <= 2, "feedBAAccept: BA input outputs at most 2 acts");
  total += n;
  if (turned)
    while ((n = bkr94acsTurn(a, process, 1, out)) > 0) {
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
                               BRACHA87_READY, sender, value, out);
    CHECK(n <= 2, "feedBAAccept: BA input outputs at most 2 acts");
    total += n;
    if (turned)
      while ((n = bkr94acsTurn(a, process, 1, out)) > 0) {
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 8)];
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
                                  w.from, w.value, out);
        if (w.accepted)
          bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
        /* The ANSWERED bit's ABSENCE is the want: the sender has not
         * recorded our accept, so un-suppress it for the next egress. */
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsAcastWants(processes[w.to], w.process, w.from);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, w.from,
                                   w.baValue, out);
        if (w.accepted)
          bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                    w.initiator, w.from);
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsBaWants(processes[w.to], w.process, w.round,
                                 w.initiator, w.from);
      }
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, silent);
    }

    for (p = 0; p < 4; ++p) {
      unsigned char duty;

      if (silent >= 0 && (int)p == silent)
        continue;
      n = bkr94acsRetry(processes[p], &cursors[p], out);
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
/*  the bkr94acsRetry egress is not progress (it is the                */
/*  retransmission stream, and its duplicates produce 0 acts at the    */
/*  receiver, the C8 invariant).  The narrowness is load-bearing:      */
/*  post-decide continuation turns produce BA_SENDs every sweep until  */
/*  the round space is spent, so a definition that counted them would  */
/*  make M1's monotone climb false against a CORRECT machine.          */
/*                                                                    */
/*  SWEEP boundary at a process, the application loop's operational    */
/*  rule: a Retry 0 return (an idle pass), the per-pass call count     */
/*  reaching a RECOMPUTED bkr94acsSentFig1Count (the count grows as    */
/*  the BAs advance, so a stored constant mis-counts the unit), or --  */
/*  for a process already marked quiescent and skipping its Retry      */
/*  call -- one idle sweep per tick.                                   */
/*                                                                    */
/*  BARREN = a completed sweep that observed no progress.  The policy  */
/*  fires after S consecutive barren sweeps; budget compares use >=,   */
/*  so a zero budget would be eager.                                   */
/*                                                                    */
/*  The counter is per process and is harness policy, never library    */
/*  state.                                                            */
/* ------------------------------------------------------------------ */

#define BARREN_S 8    /* the harness policy's S */

struct sweepPolicy {
  unsigned int calls;    /* Retry calls made in the pass under way */
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
 ,unsigned int retryActs
 ,unsigned int sentCount
 ,unsigned int skipped
){
  unsigned int done;

  done = 0;
  if (skipped)
    done = 1;
  else {
    ++sp->calls;
    if (!retryActs || sp->calls >= sentCount)
      done = 1;
  }
  if (done) {
    sp->calls = 0;
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
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
                                  w.from, w.value, out);
        if (w.accepted)
          bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsAcastWants(processes[w.to], w.process, w.from);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, w.from,
                                   w.baValue, out);
        if (w.accepted)
          bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                    w.initiator, w.from);
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsBaWants(processes[w.to], w.process, w.round,
                                 w.initiator, w.from);
      }
      if (n)
        ++pol[w.to].progress;   /* PROGRESS: an Input returning acts */
      observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
    }

    for (p = 0; p < 4; ++p) {
      n = bkr94acsRetry(processes[p], &cursors[p], out);
      if (!n && cutTo >= 0 && (int)p == cutTo)
        ++*zeroRetriesOut;
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      spTick(&pol[p], n, bkr94acsSentFig1Count(processes[p]), 0);
      if (pol[p].barren < prevBarren[p]
       && cutTo >= 0 && (int)p == cutTo)
        ++*barrenDropsOut;
      prevBarren[p] = pol[p].barren;

      for (b = 0; b < 4; ++b)
        while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1,
                                 out)) > 0) {
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
  struct wire w;
  unsigned int p, b, n, k;

  while (qSize() > 0) {
    qPopHead(&w);
    if (side[w.from] != side[w.to])
      continue;
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, w.from,
                                w.value, out);
      if (w.accepted)
        bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsAcastWants(processes[w.to], w.process, w.from);
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
                                 w.initiator, w.type, w.from,
                                 w.baValue, out);
      if (w.accepted)
        bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                  w.initiator, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsBaWants(processes[w.to], w.process, w.round,
                               w.initiator, w.from);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, -1);
  }

  for (p = 0; p < 4; ++p) {
    n = bkr94acsRetry(processes[p], &cursors[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    spTick(&pol[p], n, bkr94acsSentFig1Count(processes[p]), 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1, out)) > 0) {
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
  unsigned int n;

  if (w->cls == BKR94ACS_CLS_ACAST)
    n = bkr94acsAcastInput(processes[to], w->process, w->type, w->from,
                              w->value, out);
  else
    n = bkr94acsBaInput(processes[to], w->process, w->round,
                               w->initiator, w->type, w->from,
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
  struct wire w;
  unsigned int p, b, n, k;

  while (qSize() > 0) {
    qPopHead(&w);
    if (w.to >= K_HONEST)
      continue;
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, w.from,
                                w.value, out);
      if (w.accepted)
        bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsAcastWants(processes[w.to], w.process, w.from);
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, w.from,
                                 w.baValue, out);
      if (w.accepted)
        bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                  w.initiator, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsBaWants(processes[w.to], w.process, w.round,
                               w.initiator, w.from);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, 4, out, n, vBytes, 0, -1);
  }

  for (p = 0; p < K_HONEST; ++p) {
    n = bkr94acsRetry(processes[p], &cursors[p], out);
    if (!n && zeroRetriesOut)
      ++*zeroRetriesOut;
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, vBytes, 0, -1);
    spTick(&pol[p], n, bkr94acsSentFig1Count(processes[p]), 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1, out)) > 0) {
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
/*  necessarily a BPR re-offer and nothing else.                       */
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
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
     * so one arriving afterward is necessarily a BPR re-offer.  BA
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
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, w.from,
                                w.value, out);
      if (w.accepted)
        bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsAcastWants(processes[w.to], w.process, w.from);
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, w.from,
                                 w.baValue, out);
      if (w.accepted)
        bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                  w.initiator, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsBaWants(processes[w.to], w.process, w.round,
                               w.initiator, w.from);
    }
    if (n)
      ++pol[w.to].progress;
    observeAndOutput(&obs[w.to], w.to, 4, out, n, 1, 0, silent);
  }

  for (p = 0; p < 4; ++p) {
    if (down[p])
      continue;
    n = bkr94acsRetry(processes[p], &cursors[p], out);
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, silent);
    spTick(&pol[p], n, bkr94acsSentFig1Count(processes[p]), 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1, out)) > 0) {
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
/*  One process is pinned as a NEVER-ANNOUNCER: it runs, echoes and   */
/*  readys like anyone else, and LEAVES at the first egress that      */
/*  would carry its own ACCEPTED annotation -- that batch is dropped  */
/*  and it is never ticked again.  An announced-then-silent leaver is */
/*  a different schedule entirely: there the survivors' Skip masks    */
/*  fill, no want ever arms, and a correct machine quiesces.          */
/* ------------------------------------------------------------------ */

static void
mTick(
  struct bkr94acs **processes
 ,struct processObs *obs
 ,struct bracha87Retry *cursors
 ,struct sweepPolicy *pol
 ,unsigned int leaver
 ,unsigned int *gone
 ,unsigned int *deliveredOut
 ,unsigned int *zeroRetriesOut
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
  struct wire w;
  unsigned int p, b, n, k, announces;

  while (qSize() > 0) {
    qPopHead(&w);
    if (*gone && w.to == leaver)
      continue;
    if (w.cls == BKR94ACS_CLS_ACAST) {
      n = bkr94acsAcastInput(processes[w.to], w.process, w.type, w.from,
                                w.value, out);
      if (w.accepted)
        bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsAcastWants(processes[w.to], w.process, w.from);
    } else {
      n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                 w.initiator, w.type, w.from,
                                 w.baValue, out);
      if (w.accepted)
        bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                  w.initiator, w.from);
      if (w.type == BRACHA87_READY && !w.answered)
        bkr94acsBaWants(processes[w.to], w.process, w.round,
                               w.initiator, w.from);
    }
    if (n)
      ++pol[w.to].progress;
    if (w.to != leaver)
      ++*deliveredOut;
    announces = 0;
    if (w.to == leaver)
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
    n = bkr94acsRetry(processes[p], &cursors[p], out);
    announces = 0;
    if (p == leaver)
      for (k = 0; k < n; ++k)
        if (out[k].accepted)
          announces = 1;
    if (announces) {
      *gone = 1;
      continue;
    }
    if (!n && p != leaver && zeroRetriesOut)
      ++*zeroRetriesOut;
    observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
    spTick(&pol[p], n, bkr94acsSentFig1Count(processes[p]), 0);
    for (b = 0; b < 4; ++b)
      while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1, out)) > 0) {
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
/*  carry it is that process's BPR re-offer -- which arrives at ITS    */
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
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 8)];
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
        n = bkr94acsAcastInput(processes[w.to], w.process, w.type, w.from,
                                  w.value, out);
        if (w.accepted)
          bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsAcastWants(processes[w.to], w.process, w.from);
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, w.from,
                                   w.baValue, out);
        if (w.accepted)
          bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                    w.initiator, w.from);
        if (w.type == BRACHA87_READY && !w.answered)
          bkr94acsBaWants(processes[w.to], w.process, w.round,
                                 w.initiator, w.from);
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
       * release itself is LOST.  From here only its own re-offer can
       * carry it, at its own cursor rate. */
      if (p == delayed)
        ++ownTicks;
      if (p == delayed && !released && ownTicks > releaseTick) {
        released = 1;
        bkr94acsAcast(processes[delayed], &acast[delayed], acastOut);
      }

      n = bkr94acsRetry(processes[p], &cursors[p], out);
      observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
      sweepDone = spTick(&pol[p], n, bkr94acsSentFig1Count(processes[p]), 0);

      for (b = 0; b < 4; ++b)
        while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1, out)) > 0) {
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

/* ================================================================== */
/*  main                                                              */
/* ================================================================== */

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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 10)];
    unsigned int j;

    sz = bkr94acsSz(3, 0, 10);
    CHECK(sz > 0, "Sz returns nonzero");

    a = (struct bkr94acs *)calloc(1, sz);
    CHECK(a != 0, "alloc cluster");
    if (!a) goto a1_done;

    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK(a->complete == 0,
          "fresh: complete clear");
    CHECK(bkr94acsSentFig1Count(a) == 0,
          "fresh: SentFig1Count == 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsBaDecision(a, (unsigned char)j) == 0xFF,
            "fresh: BaDecision == 0xFF (undecided)");
    CHECK(bkr94acsSubset(a, buf) == 0, "fresh: Subset returns 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsAcastValue(a, (unsigned char)j) == 0,
            "fresh: AcastValue == 0");

    /* No evidence banked, so no BA has a complete round: every duty
     * reads HELD and an unconditional turn -- with or without the
     * elapsed signal -- outputs nothing. */
    for (j = 0; j < 4; ++j) {
      CHECK(bkr94acsTurnDuty(a, (unsigned char)j) == BKR94ACS_DUTY_HELD,
            "fresh: TurnDuty == HELD");
      CHECK(bkr94acsTurn(a, (unsigned char)j, 0, out) == 0,
            "fresh: Turn without the elapsed signal outputs nothing");
      CHECK(bkr94acsTurn(a, (unsigned char)j, 1, out) == 0,
            "fresh: Turn with the elapsed signal outputs nothing");
    }
    CHECK(bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_HELD,
          "fresh: FanoutDuty == HELD");

    free(a);
  }
  a1_done: ;

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
    a = (struct bkr94acs *)calloc(1, sz);
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

    dv[0] = 0;

    CHECK(bkr94acsBaDecision(0, 0) == 0xFF, "BaDecision(NULL): 0xFF");
    CHECK(bkr94acsSentFig1Count(0) == 0,
          "SentFig1Count(NULL): 0");
    CHECK(bkr94acsAcast(0, dv, dout) == 0, "A-Cast(NULL a): 0");
    /* Per .h TurnDuty reads HELD on bad args ("no turnable round"),
     * and Turn is safe to call unconditionally. */
    CHECK(bkr94acsTurnDuty(0, 0) == BKR94ACS_DUTY_HELD,
          "TurnDuty(NULL): HELD");
    CHECK(bkr94acsTurn(0, 0, 1, dout) == 0, "Turn(NULL a): 0");

    sz = bkr94acsSz(3, 0, 10);
    a = (struct bkr94acs *)calloc(1, sz);
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
    CHECK(bkr94acsTurn(a, 4, 1, dout) == 0, "Turn(process == n): 0");
    CHECK(bkr94acsTurn(a, 255, 1, dout) == 0, "Turn(process 255): 0");

    free(a);
  }
  a4_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("A5: forged INITIAL rejection (Note 14 / pitfall 17)");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(4, 10)];
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
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto a5_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    v[0] = 0x42;

    /* A-Cast INITIAL with from != process: dropped. */
    n = bkr94acsAcastInput(a, /*process=*/1, BRACHA87_INITIAL,
                              /*from=*/2, v, out);
    CHECK(n == 0, "forged A-Cast INITIAL (from != process): 0 acts");
    CHECK(bkr94acsAcastValue(a, 1) == 0,
          "forged A-Cast INITIAL: process 1 stays unaccepted");

    /* A-Cast INITIAL with from == process: echoes (1 act). */
    n = bkr94acsAcastInput(a, /*process=*/1, BRACHA87_INITIAL,
                              /*from=*/1, v, out);
    CHECK(n == 1 && out[0].act == BKR94ACS_ACT_ACAST_SEND
                 && out[0].type == BRACHA87_ECHO,
          "honest A-Cast INITIAL (from == process): ACAST_SEND/ECHO");

    /* An ECHO from a non-process sender is legitimate (sender-deduped),
     * NOT subject to the INITIAL rule. */
    n = bkr94acsAcastInput(a, /*process=*/1, BRACHA87_ECHO,
                              /*from=*/3, v, out);
    CHECK(n <= 1, "non-process ECHO accepted (not dropped as forged)");

    /* BA INITIAL with from != initiator: dropped. */
    n = bkr94acsBaInput(a, /*process=*/1, /*round=*/0,
                               /*initiator=*/2, BRACHA87_INITIAL,
                               /*from=*/3, /*value=*/1, out);
    CHECK(n == 0, "forged BA INITIAL (from != initiator): 0 acts");

    /* BA INITIAL with from == initiator: echoes. */
    n = bkr94acsBaInput(a, /*process=*/1, /*round=*/0,
                               /*initiator=*/2, BRACHA87_INITIAL,
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

      runHonest(n, vLen, mp, acasts, 0 /*ordered*/, processes, obs);
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

      runHonest(n, vLen, mp, acasts, 1 /*shuffled*/, processes, obs);
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

      runHonest(n, vLen, mp, acasts, 1 /*shuffled*/, processes, obs);
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

      runHonest(n, vLen, mp, acasts, 1, processes, obs);
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

      runHonest(n, vLen, mp, acasts, 1, processes, obs);
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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 10)];
    unsigned char val0;
    unsigned int o, src, k;
    unsigned int countProcess0 = 0;
    unsigned int countProcess1 = 0;
    unsigned int countProcess2 = 0;
    unsigned int prematureFanout = 0;

    (void)t;
    sz = bkr94acsSz(nAct - 1, vLen - 1, mp);
    p0 = (struct bkr94acs *)calloc(1, sz);
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

    /* bkr94acsAcastAllEchoed contract: processes 0/1/2 each received an
     * ECHO from all n processes before any READY, so the bit latched at n
     * before ACCEPT and holds; process 3 received nothing.  Plus the
     * documented null / out-of-range guards. */
    CHECK(bkr94acsAcastAllEchoed(p0, 0) == 1,
          "B6: AllEchoed 1 for fully-echoed process 0 (latched across accept)");
    CHECK(bkr94acsAcastAllEchoed(p0, 1) == 1, "B6: AllEchoed 1 for process 1");
    CHECK(bkr94acsAcastAllEchoed(p0, 2) == 1, "B6: AllEchoed 1 for process 2");
    CHECK(bkr94acsAcastAllEchoed(p0, 3) == 0,
          "B6: AllEchoed 0 for un-echoed process 3");
    CHECK(bkr94acsAcastAllEchoed(0, 0) == 0, "B6: AllEchoed NULL -> 0");
    CHECK(bkr94acsAcastAllEchoed(p0, 200) == 0,
          "B6: AllEchoed out-of-range process -> 0");

    /* bkr94acsAcastSkip is the per-process refinement of the same gate:
     * the A-Cast's echoed-process mask.  Process 0 (fully echoed) -> every
     * bit set (all processes suppressed, == AllEchoed); process 3 (no echoes)
     * -> empty mask (nobody suppressed).  Null / out-of-range -> 0. */
    {
      const unsigned char *sk0;
      const unsigned char *sk3;

      sk0 = bkr94acsAcastSkip(p0, 0);
      sk3 = bkr94acsAcastSkip(p0, 3);
      CHECK(sk0 != 0, "B6: AcastSkip non-null for valid process 0");
      CHECK(sk0 && BRACHA87_SKIP_TST(sk0, 0) && BRACHA87_SKIP_TST(sk0, 1)
            && BRACHA87_SKIP_TST(sk0, 2) && BRACHA87_SKIP_TST(sk0, 3),
            "B6: AcastSkip all bits set for fully-echoed process 0");
      CHECK(sk3 && !BRACHA87_SKIP_TST(sk3, 0) && !BRACHA87_SKIP_TST(sk3, 1),
            "B6: AcastSkip empty for un-echoed process 3");
      CHECK(bkr94acsAcastSkip(0, 0) == 0, "B6: AcastSkip NULL -> 0");
      CHECK(bkr94acsAcastSkip(p0, 200) == 0,
            "B6: AcastSkip out-of-range process -> 0");
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
     * player: an honest P_h whose Q-value propagates slowly ... may
     * be excluded.  Honest exclusion is a feature of the async
     * model, not a bug."
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

      runHonest(n, vLen, mp, acasts, 1, processes, obs);
      sz = bkr94acsSubset(processes[0], subset);
      CHECK(sz >= n - t, "B7: |SubSet| >= n-t (lower bound is contractual)");
      CHECK(sz <= n, "B7: |SubSet| <= n (upper bound is structural)");
      /* No assertion that sz == n -- that would over-specify. */

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B8: A-Cast ACCEPT before the last echo leaves the gates live");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Per .h bkr94acsAcastAllEchoed: "1 iff A-Cast Fig1[process] has
     * recorded an echo from all n processes", and the reason it is
     * exposed at all -- "ACCEPTED can be reached at 2t+1 readys (up to
     * t byzantine, t un-validated above the n=3t+1 boundary) while
     * correct processes still lack the payload.  Pinning the side
     * channel to ACCEPTED would strand them; pinning it here does
     * not."  That contract only holds if an echo arriving AFTER the
     * A-Cast's accept still counts: otherwise the gate a side channel
     * retires on is pinned to ACCEPTED after all, by omission.
     * bkr94acsAcastSkip is the per-process refinement -- it "drops each
     * process from the side channel's recipient set the moment IT
     * echoes" -- so it must gain a late echoer's bit for the same
     * reason.  Per .h bkr94acsAcastInput the return is the number of
     * actions; an accepted A-Cast has no rule left to fire, so a late
     * echo is recorded silently.
     */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(4, 4)];
    unsigned char v[1];
    const unsigned char *m;
    unsigned int nact;
    unsigned int late;

    sz = bkr94acsSz(3, 0, 4);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto b8_done;
    bkr94acsInit(a, 3, 1, 0, 4, 0, testCoin, 0);
    v[0] = 1;

    /* Two echoes only -- short of both n and the echo threshold. */
    bkr94acsAcastInput(a, 0, BRACHA87_INITIAL, 0, v, out);
    bkr94acsAcastInput(a, 0, BRACHA87_ECHO, 0, v, out);
    bkr94acsAcastInput(a, 0, BRACHA87_ECHO, 1, v, out);
    CHECK(bkr94acsAcastAllEchoed(a, 0) == 0, "B8: gate 0 below n echoers");

    /* 2t+1 readys accept ahead of the remaining echoes. */
    for (i = 0; i < 3; ++i)
      bkr94acsAcastInput(a, 0, BRACHA87_READY, (unsigned char)i, v, out);
    CHECK(bkr94acsAcastAllEchoed(a, 0) == 0, "B8: gate still 0 at accept");
    m = bkr94acsAcastSkip(a, 0);
    CHECK(m != 0, "B8: skip mask non-null");
    if (m)
      CHECK(!BRACHA87_SKIP_TST(m, 2) && !BRACHA87_SKIP_TST(m, 3),
            "B8: mask clear for the late echoers at accept");

    /* The remaining echoes arrive after accept. */
    for (late = 2; late < 4; ++late) {
      nact = bkr94acsAcastInput(a, 0, BRACHA87_ECHO, (unsigned char)late,
                                v, out);
      CHECK(nact == 0, "B8: post-accept echo outputs 0 acts");
      CHECK(bkr94acsAcastAllEchoed(a, 0) == (late == 3),
            "B8: gate 1 exactly when echo senders == n");
    }
    m = bkr94acsAcastSkip(a, 0);
    if (m)
      CHECK(BRACHA87_SKIP_TST(m, 2) && BRACHA87_SKIP_TST(m, 3),
            "B8: mask gains the late echoers");

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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 10)];
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
    bkr94acsAcastInput(p0, 1, BRACHA87_INITIAL, 1, &val1, out);
    for (src = 0; src < nAct; ++src)
      bkr94acsAcastInput(p0, 1, BRACHA87_ECHO, src, &val1, out);
    for (src = 0; src < nAct; ++src)
      bkr94acsAcastInput(p0, 1, BRACHA87_READY, src, &val1, out);
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
      bkr94acsAcastInput(p0, 1, BRACHA87_READY, src, &val1, out);
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

      runHonest(nAct, vLen, mp, acasts, 0 /*ordered*/, processes, obs);

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
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto c1_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bracha87RetryInit(&cursor);
    /* Walk well past the cursor space (A-Cast Fig1s + every owned
     * BA Fig1 slot).  All return 0. */
    for (j = 0; j < 1024; ++j) {
      n = bkr94acsRetry(a, &cursor, out);
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
     * until ACCEPTED or all-echoed (pitfall 11).
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
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto c2_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bkr94acsAcast(a, &val, acastOut);
    bracha87RetryInit(&cursor);

    /* 32 calls is plenty: cursor starts at 0 = A-Cast Fig1 initiator 0
     * (= self), so the first call should already output. */
    for (j = 0; j < 32; ++j) {
      n = bkr94acsRetry(a, &cursor, out);
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

      rc = runWithRetry(n, vLen, mp, acasts, 0, -1, 1000, processes, obs,
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
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto c5_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bracha87RetryInit(&cursor);
    for (j = 0; j < 256; ++j) {
      n = bkr94acsRetry(a, &cursor, out);
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

      rc = runWithRetry(n, vLen, mp, acasts, 50, -1, 5000, processes, obs,
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
  BANNER("C7: Silent Byzantine process canary (pitfall 11 regression)");
  /* ---------------------------------------------------------------- */
  {
    /* n=4 t=1, process 3 is Byzantine-silent: never A-Casts, never
     * receives, never outputs.  Honest processes 0/1/2 must converge --
     * SubSet excludes process 3 via step-2 enter-0 fanout for process 3.
     *
     * This is the regression for pitfall 11: the initiator INITIAL
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
      rc = runWithRetry(n, vLen, mp, acasts, 12, 3 /* silentProcess */,
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
     * onto already-delivered wires (pitfalls 10/11); if those
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

      actsCap = BKR94ACS_MAX_ACTS(n - 1, mp);
      out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
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
                                             w.from, w.value, out);
            else
              nDeliv = bkr94acsBaInput(processes[w.to], w.process,
                                              w.round, w.initiator,
                                              w.type, w.from, w.baValue,
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
            nDeliv = bkr94acsRetry(processes[i], &cursors[i], retryOut);
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
                                          acastSample.from, acastSample.value,
                                          out);
          CHECK(nRetry == 0,
                "C8: re-delivered ACAST returns 0 acts (Input dedup)");
        }
        if (haveBaSample) {
          nRetry = bkr94acsBaInput(processes[baSample.to],
                                           baSample.process, baSample.round,
                                           baSample.initiator,
                                           baSample.type, baSample.from,
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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PROCESSES, 1)];
    unsigned int round, b, n, k;
    unsigned int exhaustedSeen = 0;

    sz = bkr94acsSz(3, 0, 1);   /* n=4, vLen=1, maxPhases=1 */
    a = (struct bkr94acs *)calloc(1, sz);
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
    n = bkr94acsBaInput(a, 0, 0, 0, BRACHA87_READY, 0, 0, out);
    CHECK(n <= 2, "D1: later BA input within the 2-act bound");
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED)
        ++exhaustedSeen;
    while ((n = bkr94acsTurn(a, 0, 1, out)) > 0)
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
    struct bkr94acsAct synthOut[BKR94ACS_MAX_ACTS(MAX_PROCESSES, 1)];
    unsigned int round, b, j, k, n;
    unsigned int exhaustedSeen = 0;
    unsigned int process0Retries = 0;

    sz = bkr94acsSz(3, 0, 1);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto d2_done;
    bkr94acsInit(a, 3, 1, 0, 1, 0, testCoin, 0);

    /* Set up an EXHAUSTED state same as D1. */
    for (round = 0; round < 3; ++round)
      for (b = 0; b < 4; ++b)
        feedBAAccept(a, 0, (unsigned char)round, (unsigned char)b,
                            (b < 2) ? 0 : 1, synthOut, 1, &exhaustedSeen);
    CHECK(exhaustedSeen == 1, "D2: EXHAUSTED setup OK");
    CHECK(bkr94acsSentFig1Count(a) > 0,
          "D2: post-EXHAUSTED SentFig1Count > 0");

    /* Sweep Retry enough to traverse all Fig1 slots; count BA_SEND
     * retries for process 0 (the EXHAUSTED process). */
    bracha87RetryInit(&cursor);
    for (j = 0; j < 2048; ++j) {
      n = bkr94acsRetry(a, &cursor, out);
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

      actsCap = BKR94ACS_MAX_ACTS(n - 1, mp);
      out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
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
                                         w.from, w.value, out);
          else
            nact = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                          w.initiator, w.type, w.from,
                                          w.baValue, out);
          observeAndOutput(&obs[w.to], w.to, n, out, nact, vLen, 0, -1);
          drainTurns(processes[w.to], &obs[w.to], w.to, n, vLen, out, 0, -1);
          nact = bkr94acsFanout(processes[w.to], out);
          observeAndOutput(&obs[w.to], w.to, n, out, nact, vLen, 0, -1);
        }
        for (p = 1; p < n; ++p) {
          unsigned int nact = bkr94acsRetry(processes[p], &cursors[p], retryOut);
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
  /*  The same delayed-A-Cast schedule under two patience values: the eager   */
  /*  schedule (F1) excludes the delayed honest process every time    */
  /*  and patience (F2) includes it -- the pair is the WAN            */
  /*  starvation seed and its remedy.  F3 is the liveness half: a     */
  /*  dead slot holds TOLERANCE forever, patience bounds the tax,   */
  /*  and firing after it completes the instance.                     */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("F1: eager schedule excludes a delayed honest A-Cast");
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
       * the paper's per-instance honest-exclusion allowance.  Under a
       * persistent latency spread the SAME process re-suffers this
       * every instance; that compounding is what F2's patience removes. */
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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(4, 2)];
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
                                      w.from, w.value, out);
            if (w.accepted)
              bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
            /* Leaving the rotation is provisional: only a tick can
             * answer a want, so an armed want puts the process back. */
            if (w.type == BRACHA87_READY && !w.answered) {
              bkr94acsAcastWants(processes[w.to], w.process, w.from);
              if (quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
            }
          } else {
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                       w.initiator, w.type, w.from,
                                       w.baValue, out);
            if (w.accepted)
              bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                        w.initiator, w.from);
            if (w.type == BRACHA87_READY && !w.answered) {
              bkr94acsBaWants(processes[w.to], w.process, w.round,
                                     w.initiator, w.from);
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
            n = bkr94acsRetry(processes[p], &cursors[p], out);
            if (!n && bkr94acsSentFig1Count(processes[p])) {
              quiesced[p] = 1;
              ++nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, 0, -1);
          }
          for (b = 0; b < 4; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1,
                                     out)) > 0) {
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
        CHECK(bkr94acsRetry(processes[p], &cursors[p], out) == 0,
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
  /*  Section G -- Round-turn pacing (bkr94acsTurnDuty/bkr94acsTurn)  */
  /*                                                                  */
  /*  Section F isolates the fanout's pacing; this isolates the BA    */
  /*  round turn's.  The arrival path banks evidence and decides      */
  /*  nothing (G1); a round complete at n-t validated waits for the   */
  /*  caller's elapsed signal (G2); a round complete at all n --      */
  /*  the full sample, nothing left to wait for -- fires without it   */
  /*  (G3); and a zero-patience drained instance is turn-quiescent      */
  /*  (G4).                                                           */
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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 8)];
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
                                    w.from, w.value, out);
          CHECK(n <= 3, "G1: A-Cast input within its bound");
          if (w.accepted)
            bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
          if (w.type == BRACHA87_READY && !w.answered)
            bkr94acsAcastWants(processes[w.to], w.process, w.from);
        } else {
          n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                     w.initiator, w.type, w.from,
                                     w.baValue, out);
          CHECK(n <= 2, "G1: BA input within its bound (echo/ready only)");
          if (w.accepted)
            bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                      w.initiator, w.from);
          if (w.type == BRACHA87_READY && !w.answered)
            bkr94acsBaWants(processes[w.to], w.process, w.round,
                                   w.initiator, w.from);
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
  BANNER("G2: TOLERANCE requires the elapsed signal");
  /* ---------------------------------------------------------------- */
  {
    /* Three of BA_0's four round-0 Fig1s accept, all carrying the same
     * value: the round is complete at n-t = 3 validated but the sample
     * can still grow to n, so the turn is enabled and waiting is still
     * worth something -- TOLERANCE.  It fires only on the caller's
     * patience verdict. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 8)];
    unsigned int dummy = 0;
    unsigned int sentBefore;
    unsigned int sawNextRound = 0;
    unsigned int n, k;

    sz = bkr94acsSz(3, 0, 8);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto g2_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (k = 0; k < 3; ++k)
      feedBAAccept(a, 0, 0, (unsigned char)k, 1, out, 0, &dummy);

    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_TOLERANCE,
          "G2: duty TOLERANCE at n-t of n validated");

    sentBefore = bkr94acsSentFig1Count(a);
    CHECK(bkr94acsTurn(a, 0, 0, out) == 0,
          "G2: turn without the elapsed signal does not fire");
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_TOLERANCE,
          "G2: the refused turn left the duty unchanged");
    CHECK(bkr94acsSentFig1Count(a) == sentBefore,
          "G2: the refused turn started no round");
    CHECK(bkr94acsBaDecision(a, 0) == 0xFF, "G2: BA_0 still undecided");
    CHECK(a->complete == 0, "G2: not complete");

    n = bkr94acsTurn(a, 0, 1, out);
    CHECK(n > 0, "G2: turn fires once the caller's patience elapses");
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
     * turn is free -- it fires with patienceElapsed == 0. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 8)];
    unsigned int dummy = 0;
    unsigned int n, k;

    sz = bkr94acsSz(3, 0, 8);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto g3_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (k = 0; k < 4; ++k)
      feedBAAccept(a, 0, 0, (unsigned char)k, 1, out, 0, &dummy);

    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_MET,
          "G3: duty MET with all n validated");
    n = bkr94acsTurn(a, 0, 0, out);
    CHECK(n > 0, "G3: MET turn fires without the elapsed signal");
    CHECK(n <= 3, "G3: turn outputs at most 3 acts");

    /* One turn per call: round 1 has no messages, so the duty drops
     * back to HELD and a second call outputs nothing. */
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_HELD,
          "G3: next round incomplete -- duty back to HELD");
    CHECK(bkr94acsTurn(a, 0, 1, out) == 0,
          "G3: nothing left to turn even with the elapsed signal");

    free(a);
  }
  g3_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("G4: turns are quiescent at completion");
  /* ---------------------------------------------------------------- */
  {
    /* A zero-patience drained convergence (runHonest turns after every
     * delivery).  Post-decide continuation runs the turns past DECIDE
     * to the end of the round space, so at quiescence every BA is
     * either out of rounds or has no complete round left: HELD
     * everywhere, and a further turn outputs nothing. */
    unsigned int nAct = 4, t = 1, vLen = 1, mp = 10;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 10)];
    unsigned int p, b;

    if (allocCluster(processes, nAct, t, vLen - 1, mp) == 0) {
      for (p = 0; p < MAX_PROCESSES; ++p) obsInit(&obs[p]);
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < nAct; ++i)
        acasts[i * vLen] = (unsigned char)(0x60 + i);

      runHonest(nAct, vLen, mp, acasts, 0 /*ordered*/, processes, obs);

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
          CHECK(bkr94acsTurn(processes[p], (unsigned char)b, 1, out) == 0,
                "G4: re-calling Turn outputs nothing");
        }
      }
      freeCluster(processes, nAct);
    }
  }

  /* ================================================================ */
  /*  Section H -- quiescence is REACHABLE at the ACS surface         */
  /* ================================================================ */
  /*  bkr94acs.h, bkr94acsRetry: 0 means "every sent instance has     */
  /*  retired all its retries -- quiescence."  bracha87.h's retry     */
  /*  banner makes that a pair of remote facts: every process has     */
  /*  announced its accept AND holds this one's.  Only the second     */
  /*  needs a wire annotation this layer did not have, so H drives a  */
  /*  cluster past COMPLETE to the 0 return at every process.  The    */
  /*  drive round-trips both READY bits (observeAndOutput /           */
  /*  runWithRetry above); the ANSWERED half is what makes 0          */
  /*  reachable rather than merely hoped for.                         */
  /* ---------------------------------------------------------------- */
  BANNER("H1: every process reaches the Retry 0 return");
  {
    struct bkr94acs *processes[MAX_PROCESSES];
    struct processObs obs[MAX_PROCESSES];
    struct bracha87Retry cursors[MAX_PROCESSES];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(4, 4)];
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
    static const unsigned int drops[] = { 0, 25 };

    /* Lossless first, then a fair-loss drive.  Loss is where the claim
     * bites: an answer can itself be dropped, and the wanter's next
     * poke has to re-arm the one that replaces it. */
    for (di = 0; di < sizeof (drops) / sizeof (drops[0]); ++di) {
    drop = drops[di];
    rngSeed(0x5A5A00u + drop);
    if (allocCluster(processes, 4, 1, 0, 4) == 0) {
      qReset();
      for (p = 0; p < 4; ++p) {
        obsInit(&obs[p]);
        bracha87RetryInit(&cursors[p]);
        quiesced[p] = 0;
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
                                      w.from, w.value, out);
            if (w.accepted)
              bkr94acsAcastAccepted(processes[w.to], w.process, w.from);
            /* Leaving the rotation is PROVISIONAL: a want is exactly
             * the evidence that something is still owed, and a process
             * that has stopped ticking can never answer it.  Re-enter. */
            if (w.type == BRACHA87_READY && !w.answered) {
              bkr94acsAcastWants(processes[w.to], w.process, w.from);
              if (quiesced[w.to]) {
                quiesced[w.to] = 0;
                --nQuiesced;
              }
            }
          } else {
            n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                       w.initiator, w.type, w.from,
                                       w.baValue, out);
            if (w.accepted)
              bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                        w.initiator, w.from);
            if (w.type == BRACHA87_READY && !w.answered) {
              bkr94acsBaWants(processes[w.to], w.process, w.round,
                                     w.initiator, w.from);
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
            n = bkr94acsRetry(processes[p], &cursors[p], out);
            if (!n && bkr94acsSentFig1Count(processes[p])) {
              quiesced[p] = 1;
              ++nQuiesced;
            }
            observeAndOutput(&obs[p], (unsigned char)p, 4, out, n, 1, drop, -1);
          }
          for (b = 0; b < 4; ++b)
            while ((n = bkr94acsTurn(processes[p], (unsigned char)b, 1,
                                     out)) > 0) {
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
      for (p = 0; p < 4; ++p) {
        CHECK(processes[p]->complete, "H1: quiescence past COMPLETE");
        CHECK(bkr94acsRetry(processes[p], &cursors[p], out) == 0,
              "H1: and the 0 return is stable");
      }
      CHECK(qSize() == 0, "H1: the wire is silent at quiescence");
      freeCluster(processes, 4);
    }
    }
  }

  BANNER("H2: the want ingress entries' contracts");
  {
    struct bkr94acs *processes[MAX_PROCESSES];

    if (allocCluster(processes, 4, 1, 0, 4) == 0) {
      /* Defensive: null state and every out-of-range index ignored, no
       * output actions (these entries return void).  Mirrors the
       * bkr94acs*Accepted guards in Section A. */
      bkr94acsAcastWants(0, 0, 0);
      bkr94acsAcastWants(processes[0], 99, 0);
      bkr94acsAcastWants(processes[0], 0, 99);
      bkr94acsBaWants(0, 0, 0, 0, 0);
      bkr94acsBaWants(processes[0], 99, 0, 0, 0);
      bkr94acsBaWants(processes[0], 0, 250, 0, 0);
      bkr94acsBaWants(processes[0], 0, 0, 99, 0);
      bkr94acsBaWants(processes[0], 0, 0, 0, 99);
      CHECK(1, "H2: want ingress guards survive null / out-of-range");
      /* Pre-accept a want records nothing, so an A-Cast that nobody has
       * accepted still suppresses nobody and answers nobody -- there is
       * no state for a forged poke to disturb. */
      bkr94acsAcastWants(processes[0], 1, 2);
      CHECK(bkr94acsAcastSkip(processes[0], 1) != 0,
            "H2: the A-Cast skip accessor is unaffected by a want");
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
  BANNER("I1: READY re-offers alone carry a returner holding zero evidence");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
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
       * state, so the re-offers stand until the survivors' own gates
       * fire -- which is exactly the window this heal lands in. */
      for (tick = 0; tick < 20000; ++tick) {
        iTick(processes, obs, cursors, pol, side, &wit);
        if (pol[0].barren && pol[1].barren && pol[2].barren)
          break;
      }
      CHECK(tick < 20000, "I1: the surviving side comes to rest");

      /* THE CONSTRUCTION GUARD, read at the instant of heal: accepted
       * implies retired (bracha87.h's retire conditions), so a full
       * survivor sweep across the healed link carries READY re-offers
       * and nothing else. */
      side[3] = 0;
      healInitials = 0;
      healEchoes = 0;
      healReadys = 0;
      for (p = 0; p < 3; ++p) {
        pass = bkr94acsSentFig1Count(processes[p]);
        for (tick = 0; tick < pass; ++tick) {
          n = bkr94acsRetry(processes[p], &cursors[p], out);
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
      CHECK(healReadys > 0, "I1: the healed link carries READY re-offers");
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
      CHECK(bkr94acsAcastAllEchoed(processes[3], 2) == 0,
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
  /*  banner; bkr94acs.h's bkr94acsRetry (0 only on an idle sweep),   */
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
      CHECK(bkr94acsSentFig1Count(processes[3]) == 1,
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
        sentBefore[p] = bkr94acsSentFig1Count(processes[p]);

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
        sentNow = bkr94acsSentFig1Count(processes[p]);
        n = kDeliver(processes, obs, &w, (unsigned char)p, vLen);
        CHECK(n == 0,
              "K1: a same-sender echo with a different value returns 0 acts");
        pv = bkr94acsAcastValue(processes[p], 0);
        CHECK(pv != 0 && pv[0] == valBefore[p],
              "K1: and changes nothing observable");
        CHECK(bkr94acsSentFig1Count(processes[p]) == sentNow,
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
        delta = bkr94acsSentFig1Count(processes[p]) - sentBefore[p];
        CHECK(bkr94acsSentFig1Count(processes[p]) >= sentBefore[p],
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
  BANNER("L1: a late starter bootstraps on live INITIAL re-offers");
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Retry cursors[4];
    struct sweepPolicy pol[4];
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
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
        pass = bkr94acsSentFig1Count(processes[p]);
        for (tick = 0; tick < pass; ++tick) {
          n = bkr94acsRetry(processes[p], &cursors[p], out);
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
            "L1: the late starter's bootstrap consumed a re-offered A-Cast"
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
             " %u re-offered A-Cast INITIALs taken in, |SubSet| %u\n",
             guardInitials, guardEchoes, guardReadys, wit.initialsIn, sz0);

      freeCluster(processes, 4);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("L2: a post-fanout late starter still completes and agrees");
  /* ---------------------------------------------------------------- */
  {
    /* K after the survivors' step-2 fanout fired, so their INITIAL and
     * ECHO retries retired long ago and READY re-offers are all that
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
  /*  reaching all n; Skip is the accepted set MINUS the wanters, so   */
  /*  it is a subset of the Answer mask); bkr94acs.h's Retry 0-return  */
  /*  contract.  Cross-reference label: README "Abandonment" / After   */
  /*  COMPLETE.                                                       */
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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 2)];
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
        mTick(processes, obs, cursors, pol, leaver, &gone, &delivered,
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
        mTick(processes, obs, cursors, pol, leaver, &gone, &delivered,
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
          ans = bracha87Fig1Answer(f1);
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
              ans = bracha87Fig1Answer(f1);
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
            "M1: every served instance's ANSWER mask is short by exactly"
            " the leaver's bit");
      CHECK(served == aimed,
            "M1: and its READY re-offers are aimed at the leaver alone");

      /* A bounded further drive: the facts above are STANDING, not a
       * moment.  The barren counters climb monotonically over it under
       * the shared PROGRESS definition -- the state the policy exists
       * to end -- and the harness policy is what ends this drive. */
      for (tick = 0; tick < 30000; ++tick) {
        mTick(processes, obs, cursors, pol, leaver, &gone, &delivered,
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
        CHECK(bkr94acsRetry(processes[p], &cursors[p], out) > 0,
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
              ans = bracha87Fig1Answer(f1);
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

  /* ================================================================ */
  /*  Section N -- the sustained-rate skew lane                       */
  /* ================================================================ */
  /*  Grounding: bkr94acs.h's duty trichotomy block ("derived by       */
  /*  scanning baDecision[] and the entered set; nothing stored"),     */
  /*  the FanoutDuty / TurnDuty semantics, and the sweep-unit block    */
  /*  ("THE UNIT IS THE FULL SWEEP ... a budget denominated in calls   */
  /*  rather than passes re-offers only its own count out of           */
  /*  bkr94acsSentFig1Count and can buy nothing at all").              */
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
     * instance's re-offer -- arrives at the DELAYED process's cursor
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
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 8)];
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
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto n2_done;
    bkr94acsInit(a, 3, 1, 0, 8, 0, testCoin, 0);

    for (b = 0; b < 2; ++b)
      feedBAAccept(a, 0, 0, (unsigned char)b, 1, out, 0, &dummy);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_HELD,
          "N2: TurnDuty HELD below n-t validated");
    feedBAAccept(a, 0, 0, 2, 1, out, 0, &dummy);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_TOLERANCE,
          "N2: the n-t'th validated message moves it HELD -> TOLERANCE");
    feedBAAccept(a, 0, 0, 3, 1, out, 0, &dummy);
    CHECK(bkr94acsTurnDuty(a, 0) == BKR94ACS_DUTY_MET,
          "N2: the n'th moves it TOLERANCE -> MET");
    free(a);

    /* -- FanoutDuty, the same form ---------------------------------- */
    a = (struct bkr94acs *)calloc(1, sz);
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
      a = (struct bkr94acs *)calloc(1, sz);
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
