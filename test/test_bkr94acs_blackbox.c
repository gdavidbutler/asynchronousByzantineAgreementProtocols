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
 * Sections (see in-file "Section X — ..." markers in main() for
 * the authoritative list):
 *
 *   A. API edges — Sz/Init, A-Cast round-trip, defensive nulls.
 *   B. Lemma 2 Parts A/B/C/D + paper-direct invariants — honest
 *      convergence at n=4/n=7, identical acasts, multi-byte
 *      values, step-2 BA-decision trigger, single-input-per-BA,
 *      single COMPLETE / BA_DECIDED, honest-exclusion allowance.
 *   C. BPR / Retry — idle-on-fresh, post-A-Cast self-INITIAL,
 *      MAX_ACTS bound, SentFig1Count monotone, silence-threshold
 *      signal, drop convergence, silent-Byzantine canary, Input
 *      dedup (retried wire returns 0 acts).
 *   D. EXHAUSTED — single output + 0xFE sentinel + permanent
 *      !complete; Retry continues post-EXHAUSTED.
 *   E. Byzantine — equivocating A-Caster (Bracha Lemma 2 inheritance).
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
/*  Coin — deterministic alternating. Adequate for tests; adversarial */
/*  deployments should pass a local random source.                    */
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
/*  Wire queue — carries both A-Cast-class and BA-class      */
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
/*  or from the public accessors — never from the bkr94acs struct's   */
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
 * to all 'nAct' processes (including 'self' — loopback through queue
 * per the project's "feed self through the network" rule).
 *
 * dropPercent  0..99 — per-recipient probability the wire is dropped
 *                       at output rather than queued.  Models a
 *                       lossy network for BPR-retry convergence
 *                       tests.  0 = no drops.
 * silentProcess   -1 = none; otherwise wires destined to this process are
 *                       not queued (the silent process never receives,
 *                       its A-Cast/Retry are never called, so it
 *                       never outputs — modeling a Byzantine-silent
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
       * round-r values, not BKR94-layer inputs — filter them out. */
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

  if (w->cls == BKR94ACS_CLS_ACAST)
    n = bkr94acsAcastInput(process, w->process, w->type, w->from,
                              w->value, out);
  else
    n = bkr94acsBaInput(process, w->process, w->round, w->initiator,
                               w->type, w->from, w->baValue, out);
  CHECK(n <= outCap, "act count within MAX_ACTS bound");
  observeAndOutput(obs, w->to, nAct, out, n, vBytes, 0, -1);
}

/* ------------------------------------------------------------------ */
/*  Honest-run simulator: every process acasts, all messages are       */
/*  delivered (no drops), drive until the queue is empty.             */
/*  Retry is not invoked — under no-loss the protocol converges        */
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

  /* Part B: all processes complete (BKR94ACS_F_COMPLETE flag in a->flags). */
  for (i = 0; i < nAct; ++i)
    CHECK(processes[i]->flags & BKR94ACS_F_COMPLETE,
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
   * honest process enters exactly one VALUE into every BA — 1 from
   * step 1 once Q(j)=1 is learned, or 0 from step 2's enter-0 fanout.
   * "Step 1 and step 2 stop touching it" once the input is entered.
   *
   * Under loss, BPR retries the round-0 INITIAL many times for
   * delivery, but always with the same value — retries do not
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
/*  Allocate and initialise a process cluster of size nAct (encoded as   */
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
/*    4. Exit when all non-silent processes carry BKR94ACS_F_COMPLETE.    */
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
      /* Silent process never receives — defensive (output already
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
      } else {
        n = bkr94acsBaInput(processes[w.to], w.process, w.round,
                                   w.initiator, w.type, w.from,
                                   w.baValue, out);
        if (w.accepted)
          bkr94acsBaAccepted(processes[w.to], w.process, w.round,
                                    w.initiator, w.from);
      }
      observeAndOutput(&obs[w.to], w.to, nAct, out, n, vLen,
                     dropPercent, silentProcess);
    }

    /* Retry every non-silent process once.  Per .h:
     *   - returns at most BKR94ACS_RETRY_MAX_ACTS
     *   - returns 0 only when full sweep finds no sent instance
     *     (pre-broadcast / shutdown — never expected mid-run after
     *     A-Cast has set INITIATOR). */
    for (i = 0; i < nAct; ++i) {
      if (silentProcess >= 0 && (int)i == silentProcess)
        continue;
      n = bkr94acsRetry(processes[i], &cursors[i], retryOut);
      if (n > maxRetryActs)
        maxRetryActs = n;
      observeAndOutput(&obs[i], (unsigned char)i, nAct, retryOut, n, vLen,
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
      if (!(processes[i]->flags & BKR94ACS_F_COMPLETE)) {
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
/*  Synthetic Fig1 ACCEPT helper for Section D's EXHAUSTED setup.     */
/*  Drives a single (process, round, initiator) BA Fig1 to    */
/*  ACCEPT at process 'a' with the given binary value, by feeding 1      */
/*  INITIAL + 3 distinct READYs (Bracha87 Rule 5 then Rule 6 fires).  */
/*  Used by D1/D2 to construct an EXHAUSTED scenario without driving  */
/*  a multi-process simulation.  Same approach as test_bkr94acs.c's      */
/*  feedFig1Accept; entirely public-API.                              */
/* ------------------------------------------------------------------ */

static unsigned int
feedBAAccept(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char value
 ,struct bkr94acsAct *out
 ,unsigned int *exhaustedSeen
){
  unsigned int total = 0;
  unsigned int n, k;
  unsigned char sender;

  n = bkr94acsBaInput(a, process, round, initiator,
                             BRACHA87_INITIAL, initiator, value, out);
  for (k = 0; k < n; ++k)
    if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED && out[k].process == process)
      ++*exhaustedSeen;
  total += n;

  /* Three distinct READYs trip Rule 5 (rd>=t+1) then Rule 6 (rd>=2t+1)
   * → ACCEPT.  Senders 1, 2, 3 (initiator's own READY isn't needed
   * since echoed is set after INITIAL). */
  for (sender = 1; sender <= 3; ++sender) {
    n = bkr94acsBaInput(a, process, round, initiator,
                               BRACHA87_READY, sender, value, out);
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED && out[k].process == process)
        ++*exhaustedSeen;
    total += n;
  }
  return (total);
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
  /*  Section A — API edges                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("A1: Sz/Init contract on a fresh process");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    unsigned char buf[MAX_PROCESSES];
    unsigned int j;

    sz = bkr94acsSz(3, 0, 10);
    CHECK(sz > 0, "Sz returns nonzero");

    a = (struct bkr94acs *)calloc(1, sz);
    CHECK(a != 0, "alloc cluster");
    if (!a) goto a1_done;

    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK((a->flags & BKR94ACS_F_COMPLETE) == 0,
          "fresh: BKR94ACS_F_COMPLETE clear");
    CHECK(bkr94acsSentFig1Count(a) == 0,
          "fresh: SentFig1Count == 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsBaDecision(a, (unsigned char)j) == 0xFF,
            "fresh: BaDecision == 0xFF (undecided)");
    CHECK(bkr94acsSubset(a, buf) == 0, "fresh: Subset returns 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsAcastValue(a, (unsigned char)j) == 0,
            "fresh: AcastValue == 0");

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
    struct bkr94acsAct dout[1];

    dv[0] = 0;

    CHECK(bkr94acsBaDecision(0, 0) == 0xFF, "BaDecision(NULL): 0xFF");
    CHECK(bkr94acsSentFig1Count(0) == 0,
          "SentFig1Count(NULL): 0");
    CHECK(bkr94acsAcast(0, dv, dout) == 0, "A-Cast(NULL a): 0");

    sz = bkr94acsSz(3, 0, 10);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto a4_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK(bkr94acsBaDecision(a, 4) == 0xFF,
          "BaDecision(process == n): 0xFF");
    CHECK(bkr94acsBaDecision(a, 255) == 0xFF,
          "BaDecision(process 255): 0xFF");

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
  /*  Section B — Lemma 2 Parts A/B/C/D + paper-direct invariants     */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("B1: Lemma 2 Parts A/B/C/D — n=4 t=1, ordered delivery");
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

      /* Lemma 2 Part D — explicit value-match check (the implementation
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
  BANNER("B2: Lemma 2 — n=4 t=1, shuffled delivery");
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
  BANNER("B3: Lemma 2 — n=7 t=2, shuffled delivery");
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
  BANNER("B4: Lemma 2 — identical A-Casts (degenerate values)");
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
  BANNER("B5: Lemma 2 — multi-byte values (vLen=8)");
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

    /* Process 0 — process 0 acasts, then synthesises the all-honest
     * cascade locally (INITIAL from process 0; ECHO from 0/1/2/3;
     * READY from 0/1/2/3 once each process's threshold trips).  Since
     * we're driving only P0, we synthesise these as direct
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

    /* No BA has decided yet — no BA traffic delivered. */
    CHECK(bkr94acsBaDecision(p0, 0) == 0xFF,
          "B6: BA_0 still undecided (no BA delivered)");
    CHECK(bkr94acsBaDecision(p0, 1) == 0xFF, "B6: BA_1 undecided");
    CHECK(bkr94acsBaDecision(p0, 2) == 0xFF, "B6: BA_2 undecided");
    CHECK(bkr94acsBaDecision(p0, 3) == 0xFF, "B6: BA_3 undecided");

    /* Step-2 trigger MUST NOT have fired — Fig1-ACCEPT count is now
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
      /* No assertion that sz == n — that would over-specify. */

      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section C — BPR / Retry                                          */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("C1: Retry idle on fresh process (no A-Cast)");
  /* ---------------------------------------------------------------- */
  {
    /* Per .h: "Returns 0 only when a full sweep finds no sent
     * instance — pre-broadcast / shutdown state".  A freshly-Init'd
     * process that has not A-Castd and received no inputs has no
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
     * BPR rules: INITIATOR → output INITIAL_ALL on every Bpr call (forever).
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
  BANNER("C5: Retry full-sweep idle return = 0 (silence-threshold signal)");
  /* ---------------------------------------------------------------- */
  {
    /* The .h documents Retry returning 0 only on full-sweep idle —
     * the only contractual case is "pre-broadcast / shutdown".  This
     * banner re-anchors that on a fresh process (same as C1, formalised
     * as the silence-threshold-exit signal a deployment uses). */
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
          "C5: pre-A-Cast Retry returns 0 every call (silence signal)");

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
     * rules (INITIATOR → INITIAL forever, ECHOED → ECHO forever,
     * RDSENT → READY forever) end-to-end. */
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
    /* n=4 t=1, process 3 is Byzantine-silent: never acasts, never
     * receives, never outputs.  Honest processes 0/1/2 must converge —
     * SubSet excludes process 3 via step-2 enter-0 fanout for process 3.
     *
     * This is the regression for pitfall 11: the initiator INITIAL
     * retry must NOT short-circuit on local ECHOED.  Each honest
     * process is an initiator of its own A-Cast; their Retrys must
     * keep retrying INITIAL until that A-Cast is accepted (the
     * sound stop), NOT merely until they echoed locally.  At the
     * n=3t+1 boundary Bracha's echo threshold ((n+t)/2+1) equals the
     * honest count, so any process that missed the bootstrap depends on
     * the initiator's continued INITIAL retry to complete its echo
     * count.  The original gap-4 design (`INITIATOR && !ECHOED → output`)
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
      CHECK(rc == 0, "C7: silent Byzantine process — honest processes converge");
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
  BANNER("C8: Input dedup — retried wire returns 0 acts (silence-threshold invariant)");
  /* ---------------------------------------------------------------- */
  {
    /* Load-bearing invariant for deployment-layer progress-silence threshold
     * exit: the per-process progress clock advances only when AcastInput /
     * BAInput returns nacts > 0.  BPR Retry keeps retrying
     * un-retired actions (READY forever; INITIAL/ECHO until accept)
     * onto already-delivered wires (pitfalls 10/11); if those
     * re-deliveries returned acts > 0, the silence timer would never
     * elapse and the exit could never form.
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
    struct wire conSample;
    int havePropSample;
    int haveConSample;
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
        havePropSample = 0;
        haveConSample = 0;

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
              if (w.cls == BKR94ACS_CLS_ACAST && !havePropSample) {
                acastSample = w;
                havePropSample = 1;
              } else if (w.cls == BKR94ACS_CLS_BA && !haveConSample) {
                conSample = w;
                haveConSample = 1;
              }
            }
            observeAndOutput(&obs[w.to], w.to, n, out, nDeliv, vLen, 0, -1);
          }
          for (i = 0; i < n; ++i) {
            nDeliv = bkr94acsRetry(processes[i], &cursors[i], retryOut);
            observeAndOutput(&obs[i], (unsigned char)i, n, retryOut, nDeliv,
                           vLen, 0, -1);
          }
          allComplete = 1;
          for (i = 0; i < n; ++i)
            if (!(processes[i]->flags & BKR94ACS_F_COMPLETE)) {
              allComplete = 0;
              break;
            }
        }
        CHECK(allComplete, "C8: cluster converged");
        CHECK(havePropSample,
              "C8: captured a ACAST wire whose first delivery output acts");
        CHECK(haveConSample,
              "C8: captured a BA wire whose first delivery output acts");

        /* Retry: identical args, same target process.  The receiver's
         * Bracha state has already consumed this exact (process, type,
         * sender [+ round, initiator, baValue]) tuple; per Fig1
         * Rule 1/2/3 dedup the dispatch must produce zero acts. */
        if (havePropSample) {
          nRetry = bkr94acsAcastInput(processes[acastSample.to],
                                          acastSample.process, acastSample.type,
                                          acastSample.from, acastSample.value,
                                          out);
          CHECK(nRetry == 0,
                "C8: re-delivered ACAST returns 0 acts (Input dedup)");
        }
        if (haveConSample) {
          nRetry = bkr94acsBaInput(processes[conSample.to],
                                           conSample.process, conSample.round,
                                           conSample.initiator,
                                           conSample.type, conSample.from,
                                           conSample.baValue, out);
          CHECK(nRetry == 0,
                "C8: re-delivered BA returns 0 acts (Input dedup)");
        }

        free(out);
      }
      freeCluster(processes, n);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section D — EXHAUSTED                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("D1: BA_EXHAUSTED single output, 0xFE sentinel, !complete");
  /* ---------------------------------------------------------------- */
  {
    /* maxPhases=1 → BA has only 1 phase (3 sub-rounds) to terminate.
     * Drive split values across all 3 sub-rounds at every initiator
     * so neither the >2t case (i) nor the >t case (ii) of Fig4
     * step 3 fires.  Fig4 returns BRACHA87_EXHAUSTED.  BKR94 surfaces
     * BKR94ACS_ACT_BA_EXHAUSTED exactly once, sets baDecision[0]=0xFE,
     * and never sets BKR94ACS_F_COMPLETE (no unilateral substitute is safe —
     * Part C of Lemma 2 agreement would break). */
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
                            (b < 2) ? 0 : 1, out, &exhaustedSeen);

    CHECK(exhaustedSeen == 1, "D1: BA_EXHAUSTED output exactly once");
    CHECK(bkr94acsBaDecision(a, 0) == 0xFE,
          "D1: baDecision[0] == 0xFE (exhausted sentinel)");
    CHECK((a->flags & BKR94ACS_F_COMPLETE) == 0,
          "D1: BKR94ACS_F_COMPLETE remains clear (no unilateral substitute)");

    /* Subsequent BA input for the exhausted process must NOT
     * retry BA_EXHAUSTED. */
    n = bkr94acsBaInput(a, 0, 0, 0, BRACHA87_READY, 0, 0, out);
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
                            (b < 2) ? 0 : 1, synthOut, &exhaustedSeen);
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
  /*  Section E — Byzantine                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("E1: Equivocating A-Caster (Bracha Lemma 2 inheritance)");
  /* ---------------------------------------------------------------- */
  {
    /*
     * n=4 t=1, process 0 is Byzantine and equivocates its own A-Cast:
     *   INITIAL/v1 → processes 1, 2
     *   INITIAL/v2 → process 3
     * Process 0 sends nothing else (no echoes, no readys, no BA).
     *
     * Bracha 1987 Lemma 2: "if two correct processes accept u and v,
     * then u = v."  Composed at the BKR94 layer: any honest process
     * that ACCEPTs process 0's Fig1 must accept the same value as any
     * other honest process that ACCEPTs.  In this split it's likely
     * neither v1 nor v2 reaches the (n+t)/2+1=3 echo threshold at
     * any honest process, so Fig1 initiator 0 never accepts → BA_0 decides
     * 0 via step-2 fanout → SubSet excludes process 0.
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
       * No A-Cast, no Retry for process 0 — this attacker only sends
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
        }
        for (p = 1; p < n; ++p) {
          unsigned int nact = bkr94acsRetry(processes[p], &cursors[p], retryOut);
          observeAndOutput(&obs[p], (unsigned char)p, n, retryOut, nact, vLen,
                         0, -1);
        }
        allComplete = 1;
        for (p = 1; p < n; ++p)
          if (!(processes[p]->flags & BKR94ACS_F_COMPLETE)) { allComplete = 0; break; }
        if (allComplete) break;
      }
      free(out);

      /* Honest processes (1, 2, 3) all completed. */
      for (p = 1; p < n; ++p)
        CHECK(processes[p]->flags & BKR94ACS_F_COMPLETE,
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
       * (BA_0 deciding 0 across all processes — i.e. SubSet excludes
       * process 0 — is the expected case here, since neither v1 nor v2
       * can reach the (n+t)/2+1 echo threshold under this split.) */
      {
        for (p = 1; p < n; ++p) {
          const unsigned char *v_a = bkr94acsAcastValue(processes[p], 0);
          unsigned int q2;
          for (q2 = p + 1; q2 < n; ++q2) {
            const unsigned char *v_b = bkr94acsAcastValue(processes[q2], 0);
            if (v_a && v_b)
              CHECK(v_a[0] == v_b[0],
                    "E1: Bracha Lemma 2 — accepted values agree across honest processes");
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
                "E1: Part D — SubSet members have accepted values");
      }

      freeCluster(processes, n);
    }
  }
  e1_done: ;

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
