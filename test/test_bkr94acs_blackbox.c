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
 * Sections (this file currently covers A and B; C/D/E to follow):
 *
 *   A. API edges
 *      - Sz / Init contract on a freshly allocated peer
 *      - Propose contract: act shape, ProposalValue round-trip,
 *        idempotency on repeated call
 *      - Defensive nulls and out-of-range origin
 *
 *   B. Lemma 2 Parts A/B/C/D + paper-direct invariants
 *      - All-honest convergence at (n=4, t=1) ordered + shuffled
 *      - All-honest convergence at (n=7, t=2) shuffled
 *      - Identical proposals (degenerate value distribution)
 *      - Multi-byte values
 *      - Lemma 2 Part D anchor: every j in SubSet has an accepted
 *        proposal value visible via bkr94acsProposalValue
 *      - Step-2 trigger uses BA-decision count, not Fig1-ACCEPT count
 *        (regression for the HoneyBadger-style optimization that
 *        diverges from BKR94 — see bkr94acs.h commentary)
 *      - Single input per BA per peer (paper Implementer remark:
 *        "step 1 and step 2 stop touching it" once an input is entered)
 *      - Single COMPLETE per peer; single BA_DECIDED per (peer, origin)
 *      - Honest exclusion is allowed (positive non-test from BKR94ACS.txt:
 *        "SubSet need not contain every honest player")
 *
 * Header encoding convention (CRITICAL):
 *   n parameter is encoded; actual peer count = n + 1
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

#define MAX_PEERS  16
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
/*  Wire queue — carries both proposal-class and consensus-class      */
/*  Fig1 messages between peers.  cls discriminates which API the     */
/*  receiver dispatches to.                                           */
/* ------------------------------------------------------------------ */

struct wire {
  unsigned char cls;          /* BKR94ACS_CLS_PROPOSAL | _CONSENSUS */
  unsigned char origin;
  unsigned char round;        /* CONSENSUS only */
  unsigned char broadcaster;  /* CONSENSUS only */
  unsigned char type;         /* BRACHA87_INITIAL/ECHO/READY */
  unsigned char from;         /* wire sender */
  unsigned char to;           /* recipient */
  unsigned char conValue;     /* CONSENSUS only (binary) */
  unsigned char value[MAX_VLEN]; /* PROPOSAL only (vLen bytes) */
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
/*  Per-peer black-box observations.  Updated as acts are returned    */
/*  from any API call.  Every assertion in section B reads from here  */
/*  or from the public accessors — never from the bkr94acs struct's   */
/*  data[] tail.                                                      */
/* ------------------------------------------------------------------ */

struct peerObs {
  unsigned int completeCount;                /* BKR94ACS_ACT_COMPLETE */
  unsigned int baDecidedCount[MAX_PEERS];    /* BKR94ACS_ACT_BA_DECIDED per origin */
  unsigned int baDecidedValue[MAX_PEERS];    /* last conValue seen on BA_DECIDED */
  unsigned char selfInputValue[MAX_PEERS];   /* 0xFF = not yet observed; else recorded conValue
                                                of first round-0 self-INITIAL emission */
  unsigned int selfInputDisagree[MAX_PEERS]; /* > 0 iff a later self-INITIAL value disagrees */
  unsigned int selfInputAny[MAX_PEERS];      /* set iff any self-INITIAL emitted (1/0) */
  unsigned int exhaustedCount[MAX_PEERS];    /* BKR94ACS_ACT_BA_EXHAUSTED per origin */
};

static void
obsInit(
  struct peerObs *o
){
  unsigned int j;
  memset(o, 0, sizeof (*o));
  for (j = 0; j < MAX_PEERS; ++j)
    o->selfInputValue[j] = 0xFF;
}

/* Observe acts emitted BY peer 'self' (regardless of which API
 * call produced them).  Updates obs counters; emits wire messages
 * to all 'nAct' peers (including 'self' — loopback through queue
 * per the project's "feed self through the network" rule).
 *
 * dropPercent  0..99 — per-recipient probability the wire is dropped
 *                       at emission rather than queued.  Models a
 *                       lossy network for BPR-replay convergence
 *                       tests.  0 = no drops.
 * silentPeer   -1 = none; otherwise wires destined to this peer are
 *                       not queued (the silent peer never receives,
 *                       its Propose/Pump are never called, so it
 *                       never emits — modeling a Byzantine-silent
 *                       crash from the rest of the cluster's POV). */
static void
observeAndEmit(
  struct peerObs *obs
 ,unsigned char self
 ,unsigned int nAct
 ,const struct bkr94acsAct *acts
 ,unsigned int n
 ,unsigned int vBytes
 ,unsigned int dropPercent
 ,int silentPeer
){
  unsigned int i, j;
  struct wire w;

  for (i = 0; i < n; ++i) {
    switch (acts[i].act) {
    case BKR94ACS_ACT_PROP_SEND:
      for (j = 0; j < nAct; ++j) {
        if (silentPeer >= 0 && (int)j == silentPeer)
          continue;
        if (dropPercent > 0 && (rngNext() % 100) < dropPercent)
          continue;
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_PROPOSAL;
        w.origin = acts[i].origin;
        w.type = acts[i].type;
        w.from = self;
        w.to = (unsigned char)j;
        if (acts[i].value && vBytes <= sizeof (w.value))
          memcpy(w.value, acts[i].value, vBytes);
        qPush(&w);
      }
      break;
    case BKR94ACS_ACT_CON_SEND:
      /* "Self input" to BA_origin is the local peer broadcasting
       * its own input value (1 from step 1, 0 from step 2 fanout).
       * Surfaces as the round-0 CON_SEND with broadcaster == self &&
       * type == INITIAL.  Subsequent rounds also emit CON_SEND with
       * broadcaster == self / type == INITIAL but those are Fig4
       * round-r values, not BKR94-layer inputs — filter them out. */
      if (acts[i].broadcaster == self
       && acts[i].type == BRACHA87_INITIAL
       && acts[i].round == 0) {
        unsigned int oj = acts[i].origin;
        obs->selfInputAny[oj] = 1;
        if (obs->selfInputValue[oj] == 0xFF)
          obs->selfInputValue[oj] = acts[i].conValue;
        else if (obs->selfInputValue[oj] != acts[i].conValue)
          ++obs->selfInputDisagree[oj];
      }
      for (j = 0; j < nAct; ++j) {
        if (silentPeer >= 0 && (int)j == silentPeer)
          continue;
        if (dropPercent > 0 && (rngNext() % 100) < dropPercent)
          continue;
        memset(&w, 0, sizeof (w));
        w.cls = BKR94ACS_CLS_CONSENSUS;
        w.origin = acts[i].origin;
        w.round = acts[i].round;
        w.broadcaster = acts[i].broadcaster;
        w.type = acts[i].type;
        w.from = self;
        w.to = (unsigned char)j;
        w.conValue = acts[i].conValue;
        qPush(&w);
      }
      break;
    case BKR94ACS_ACT_BA_DECIDED:
      ++obs->baDecidedCount[acts[i].origin];
      obs->baDecidedValue[acts[i].origin] = acts[i].conValue;
      break;
    case BKR94ACS_ACT_COMPLETE:
      ++obs->completeCount;
      break;
    case BKR94ACS_ACT_BA_EXHAUSTED:
      ++obs->exhaustedCount[acts[i].origin];
      break;
    default:
      break;
    }
  }
}

/* Deliver one wire message to its recipient peer; observe the
 * resulting acts. */
static void
deliverWire(
  struct bkr94acs *peer
 ,struct peerObs *obs
 ,const struct wire *w
 ,unsigned int nAct
 ,unsigned int vBytes
 ,struct bkr94acsAct *out
 ,unsigned int outCap
){
  unsigned int n;

  if (w->cls == BKR94ACS_CLS_PROPOSAL)
    n = bkr94acsProposalInput(peer, w->origin, w->type, w->from,
                              w->value, out);
  else
    n = bkr94acsConsensusInput(peer, w->origin, w->round, w->broadcaster,
                               w->type, w->from, w->conValue, out);
  CHECK(n <= outCap, "act count within MAX_ACTS bound");
  observeAndEmit(obs, w->to, nAct, out, n, vBytes, 0, -1);
}

/* ------------------------------------------------------------------ */
/*  Honest-run simulator: every peer proposes, all messages are       */
/*  delivered (no drops), drive until the queue is empty.             */
/*  Pump is not invoked — under no-loss the protocol converges        */
/*  organically.  Section C/D will add fault injection + Pump.        */
/* ------------------------------------------------------------------ */

static int
runHonest(
  unsigned int nAct
 ,unsigned int vLen
 ,unsigned int maxPhases
 ,const unsigned char *proposals  /* nAct * vLen bytes */
 ,int shuffled
 ,struct bkr94acs **peers          /* allocated and Init'd by caller */
 ,struct peerObs *obs              /* zeroed by caller */
){
  unsigned long actsCap;
  struct bkr94acsAct *out;
  struct bkr94acsAct propOut[1];
  unsigned int i, n;
  struct wire w;

  qReset();

  actsCap = BKR94ACS_MAX_ACTS(nAct - 1, maxPhases);
  out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
  if (!out)
    return (-1);

  /* Each peer proposes its value; broadcast PROP_SEND/INITIAL to all. */
  for (i = 0; i < nAct; ++i) {
    n = bkr94acsPropose(peers[i], proposals + i * vLen, propOut);
    CHECK(n == 1, "Propose returns 1 act");
    if (n == 1) {
      CHECK(propOut[0].act == BKR94ACS_ACT_PROP_SEND, "Propose emits PROP_SEND");
      CHECK(propOut[0].origin == (unsigned char)i, "Propose origin == self");
      CHECK(propOut[0].type == BRACHA87_INITIAL, "Propose type == INITIAL");
    }
    observeAndEmit(&obs[i], (unsigned char)i, nAct, propOut, n, vLen, 0, -1);
  }

  /* Drain. */
  while (qSize() > 0) {
    int got;
    got = shuffled ? qPopRandom(&w) : qPopHead(&w);
    if (!got)
      break;
    deliverWire(peers[w.to], &obs[w.to], &w, nAct, vLen, out, actsCap);
  }

  free(out);
  return (0);
}

/* Shared assertion helper: Lemma 2 Parts A/B/C/D plus the paper-direct
 * invariants.  Operates entirely through public accessors and obs[]. */
static void
assertLemma2(
  struct bkr94acs **peers
 ,struct peerObs *obs
 ,unsigned int nAct
 ,unsigned int t
){
  unsigned char subset0[MAX_PEERS];
  unsigned char subsetI[MAX_PEERS];
  unsigned int sz0, szI;
  unsigned int i, j;

  /* Part B: all peers complete (a->complete is the public field). */
  for (i = 0; i < nAct; ++i)
    CHECK(peers[i]->complete != 0, "Lemma 2 Part B: peer completed");

  /* Part A: |SubSet| >= n - t for every peer. */
  sz0 = bkr94acsSubset(peers[0], subset0);
  CHECK(sz0 >= nAct - t, "Lemma 2 Part A: |SubSet| >= n-t");

  /* Part C: every peer's SubSet equals peer 0's. */
  for (i = 1; i < nAct; ++i) {
    szI = bkr94acsSubset(peers[i], subsetI);
    CHECK(szI == sz0, "Lemma 2 Part C: SubSet sizes agree");
    if (szI == sz0)
      CHECK(memcmp(subset0, subsetI, sz0) == 0,
            "Lemma 2 Part C: SubSet contents agree");
  }

  /* Part D: Q(j)=1 for every j in SubSet.  In this deployment Q(j) is
   * "Fig1 reliable broadcast for origin j has ACCEPTED", surfaced as
   * bkr94acsProposalValue(j) returning a non-null pointer. */
  for (j = 0; j < sz0; ++j) {
    unsigned int oj = subset0[j];
    for (i = 0; i < nAct; ++i)
      CHECK(bkr94acsProposalValue(peers[i], (unsigned char)oj) != 0,
            "Lemma 2 Part D: ProposalValue(j) != NULL for j in SubSet");
  }

  /* Single COMPLETE per peer. */
  for (i = 0; i < nAct; ++i)
    CHECK(obs[i].completeCount == 1, "single COMPLETE per peer");

  /* Single BA_DECIDED per (peer, origin); decided values agree across peers. */
  for (j = 0; j < nAct; ++j) {
    unsigned int v0 = obs[0].baDecidedValue[j];
    for (i = 0; i < nAct; ++i) {
      CHECK(obs[i].baDecidedCount[j] == 1,
            "single BA_DECIDED per (peer, origin)");
      CHECK(obs[i].baDecidedValue[j] == v0,
            "BA_j decisions agree across peers");
      CHECK(bkr94acsBaDecision(peers[i], (unsigned char)j) == v0,
            "BaDecision accessor matches BA_DECIDED act");
    }
  }

  /* Single input per BA per peer (paper Implementer remark): every
   * honest peer enters exactly one VALUE into every BA — 1 from
   * step 1 once Q(j)=1 is learned, or 0 from step 2's vote-0 fanout.
   * "Step 1 and step 2 stop touching it" once the input is entered.
   *
   * Under loss, BPR replays the round-0 INITIAL many times for
   * delivery, but always with the same value — replays do not
   * constitute "entering" a new input.  Verify by witnessing that
   * the value stayed consistent across all observed self-INITIAL
   * round-0 emissions (no BKR94 step-1/step-2 disagreement), and
   * that every BA received some input (under all-honest no-loss
   * runs every peer enters every BA by completion). */
  for (i = 0; i < nAct; ++i)
    for (j = 0; j < nAct; ++j) {
      CHECK(obs[i].selfInputAny[j],
            "every BA received an input from every honest peer");
      CHECK(obs[i].selfInputDisagree[j] == 0,
            "single input value per BA per peer (no step-1/step-2 disagreement)");
    }

  /* No EXHAUSTED in honest runs. */
  for (i = 0; i < nAct; ++i)
    for (j = 0; j < nAct; ++j)
      CHECK(obs[i].exhaustedCount[j] == 0, "no EXHAUSTED in honest runs");
}

/* ------------------------------------------------------------------ */
/*  Allocate and initialise a peer cluster of size nAct (encoded as   */
/*  nEnc = nAct - 1) at given t / vLen / maxPhases.                   */
/* ------------------------------------------------------------------ */

static int
allocCluster(
  struct bkr94acs **peers
 ,unsigned int nAct
 ,unsigned int t
 ,unsigned int vLenEnc
 ,unsigned int maxPhases
){
  unsigned long sz;
  unsigned int i;

  sz = bkr94acsSz(nAct - 1, vLenEnc, maxPhases);
  for (i = 0; i < nAct; ++i) {
    peers[i] = (struct bkr94acs *)calloc(1, sz);
    if (!peers[i])
      return (-1);
    bkr94acsInit(peers[i],
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
  struct bkr94acs **peers
 ,unsigned int nAct
){
  unsigned int i;
  for (i = 0; i < nAct; ++i) {
    free(peers[i]);
    peers[i] = 0;
  }
}

/* ------------------------------------------------------------------ */
/*  Pump-driven driver with optional drops + silent peer.             */
/*                                                                    */
/*  Used by Section C/D/E.  Each iteration:                           */
/*    1. Drain wire queue, calling deliverWire for every popped wire  */
/*       (silent peer's wires are skipped at emission, not delivery). */
/*    2. Call bkr94acsPump once on every non-silent peer; emit acts.  */
/*    3. Verify peer-level invariants (Pump act count <= MAX,         */
/*       CommittedFig1Count monotone non-decreasing).                 */
/*    4. Exit when all non-silent peers' a->complete is non-zero.     */
/*                                                                    */
/*  Silent peer (-1 = none): never receives wires, never has its      */
/*  Propose/Pump called, never appears in completion check.           */
/*                                                                    */
/*  Returns 0 on convergence, -1 on iter cap or alloc failure.        */
/*  Witness counters: maxPumpActs, monotoneViolations.                */
/* ------------------------------------------------------------------ */

static int
runWithPump(
  unsigned int nAct
 ,unsigned int vLen
 ,unsigned int maxPhases
 ,const unsigned char *proposals  /* nAct * vLen bytes; entry for silent peer ignored */
 ,unsigned int dropPercent        /* 0..99 */
 ,int silentPeer                  /* -1 = none */
 ,unsigned int maxIters           /* outer loop safety cap */
 ,struct bkr94acs **peers
 ,struct peerObs *obs
 ,unsigned int *maxPumpActsOut    /* witness: max acts ever emitted by Pump */
 ,unsigned int *monotoneViolationsOut /* witness: CommittedFig1Count regressions */
){
  struct bracha87Pump cursors[MAX_PEERS];
  unsigned long actsCap;
  struct bkr94acsAct *out;
  struct bkr94acsAct propOut[1];
  struct bkr94acsAct pumpOut[BKR94ACS_PUMP_MAX_ACTS];
  unsigned int prevCommitted[MAX_PEERS];
  unsigned int i, n, iter;
  unsigned int maxPumpActs = 0;
  unsigned int monotoneViolations = 0;
  struct wire w;
  int allComplete;

  qReset();
  for (i = 0; i < nAct; ++i) {
    bracha87PumpInit(&cursors[i]);
    prevCommitted[i] = 0;
  }

  actsCap = BKR94ACS_MAX_ACTS(nAct - 1, maxPhases);
  out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
  if (!out)
    return (-1);

  /* Each non-silent peer Proposes; PROP_SEND/INITIAL emits to wire. */
  for (i = 0; i < nAct; ++i) {
    if (silentPeer >= 0 && (int)i == silentPeer)
      continue;
    n = bkr94acsPropose(peers[i], proposals + i * vLen, propOut);
    observeAndEmit(&obs[i], (unsigned char)i, nAct, propOut, n, vLen,
                   dropPercent, silentPeer);
  }

  for (iter = 0; iter < maxIters; ++iter) {
    /* Drain queue. */
    while (qSize() > 0) {
      qPopHead(&w);
      /* Silent peer never receives — defensive (emission already
       * skipped them). */
      if (silentPeer >= 0 && (int)w.to == silentPeer)
        continue;
      if (w.cls == BKR94ACS_CLS_PROPOSAL)
        n = bkr94acsProposalInput(peers[w.to], w.origin, w.type, w.from,
                                  w.value, out);
      else
        n = bkr94acsConsensusInput(peers[w.to], w.origin, w.round,
                                   w.broadcaster, w.type, w.from,
                                   w.conValue, out);
      observeAndEmit(&obs[w.to], w.to, nAct, out, n, vLen,
                     dropPercent, silentPeer);
    }

    /* Pump every non-silent peer once.  Per .h:
     *   - returns at most BKR94ACS_PUMP_MAX_ACTS
     *   - returns 0 only when full sweep finds no committed instance
     *     (pre-broadcast / shutdown — never expected mid-run after
     *     Propose has set ORIGIN). */
    for (i = 0; i < nAct; ++i) {
      if (silentPeer >= 0 && (int)i == silentPeer)
        continue;
      n = bkr94acsPump(peers[i], &cursors[i], pumpOut);
      if (n > maxPumpActs)
        maxPumpActs = n;
      observeAndEmit(&obs[i], (unsigned char)i, nAct, pumpOut, n, vLen,
                     dropPercent, silentPeer);
    }

    /* Monotone CommittedFig1Count check. */
    for (i = 0; i < nAct; ++i) {
      unsigned int cur;
      if (silentPeer >= 0 && (int)i == silentPeer)
        continue;
      cur = bkr94acsCommittedFig1Count(peers[i]);
      if (cur < prevCommitted[i])
        ++monotoneViolations;
      prevCommitted[i] = cur;
    }

    /* Exit when all non-silent peers have completed. */
    allComplete = 1;
    for (i = 0; i < nAct; ++i) {
      if (silentPeer >= 0 && (int)i == silentPeer)
        continue;
      if (peers[i]->complete == 0) {
        allComplete = 0;
        break;
      }
    }
    if (allComplete)
      break;
  }

  free(out);
  if (maxPumpActsOut)
    *maxPumpActsOut = maxPumpActs;
  if (monotoneViolationsOut)
    *monotoneViolationsOut = monotoneViolations;
  return (allComplete ? 0 : -1);
}

/* ------------------------------------------------------------------ */
/*  Synthetic Fig1 ACCEPT helper for Section D's EXHAUSTED setup.     */
/*  Drives a single (origin, round, broadcaster) consensus Fig1 to    */
/*  ACCEPT at peer 'a' with the given binary value, by feeding 1      */
/*  INITIAL + 3 distinct READYs (Bracha87 Rule 5 then Rule 6 fires).  */
/*  Used by D1/D2 to construct an EXHAUSTED scenario without driving  */
/*  a multi-peer simulation.  Same approach as test_bkr94acs.c's      */
/*  feedFig1Accept; entirely public-API.                              */
/* ------------------------------------------------------------------ */

static unsigned int
feedConsensusAccept(
  struct bkr94acs *a
 ,unsigned char origin
 ,unsigned char round
 ,unsigned char broadcaster
 ,unsigned char value
 ,struct bkr94acsAct *out
 ,unsigned int *exhaustedSeen
){
  unsigned int total = 0;
  unsigned int n, k;
  unsigned char sender;

  n = bkr94acsConsensusInput(a, origin, round, broadcaster,
                             BRACHA87_INITIAL, broadcaster, value, out);
  for (k = 0; k < n; ++k)
    if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED && out[k].origin == origin)
      ++*exhaustedSeen;
  total += n;

  /* Three distinct READYs trip Rule 5 (rd>=t+1) then Rule 6 (rd>=2t+1)
   * → ACCEPT.  Senders 1, 2, 3 (broadcaster's own READY isn't needed
   * since echoed is set after INITIAL). */
  for (sender = 1; sender <= 3; ++sender) {
    n = bkr94acsConsensusInput(a, origin, round, broadcaster,
                               BRACHA87_READY, sender, value, out);
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED && out[k].origin == origin)
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
  struct bkr94acs *peers[MAX_PEERS];
  struct peerObs obs[MAX_PEERS];
  unsigned char props[MAX_PEERS * MAX_VLEN];
  unsigned int i;

  (void)argc;
  (void)argv;

  rngSeed(0xC0FFEE);

  /* ---------------------------------------------------------------- */
  /*  Section A — API edges                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("A1: Sz/Init contract on a fresh peer");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    unsigned char buf[MAX_PEERS];
    unsigned int j;

    sz = bkr94acsSz(3, 0, 10);
    CHECK(sz > 0, "Sz returns nonzero");

    a = (struct bkr94acs *)calloc(1, sz);
    CHECK(a != 0, "alloc cluster");
    if (!a) goto a1_done;

    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK(a->complete == 0, "fresh: complete == 0");
    CHECK(bkr94acsCommittedFig1Count(a) == 0,
          "fresh: CommittedFig1Count == 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsBaDecision(a, (unsigned char)j) == 0xFF,
            "fresh: BaDecision == 0xFF (undecided)");
    CHECK(bkr94acsSubset(a, buf) == 0, "fresh: Subset returns 0");
    for (j = 0; j < 4; ++j)
      CHECK(bkr94acsProposalValue(a, (unsigned char)j) == 0,
            "fresh: ProposalValue == 0");

    free(a);
  }
  a1_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("A2: Propose contract and ProposalValue round-trip");
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
    n = bkr94acsPropose(a, v1, out);
    CHECK(n == 1, "Propose: 1 act");
    if (n == 1) {
      CHECK(out[0].act == BKR94ACS_ACT_PROP_SEND, "Propose: PROP_SEND");
      CHECK(out[0].origin == 0, "Propose: origin == self (0)");
      CHECK(out[0].type == BRACHA87_INITIAL, "Propose: type == INITIAL");
      CHECK(out[0].value != 0, "Propose: value pointer non-null");
      if (out[0].value)
        CHECK(out[0].value[0] == 0xAB, "Propose: value bytes match");
    }

    pv = bkr94acsProposalValue(a, 0);
    CHECK(pv != 0, "ProposalValue(self) != 0 after Propose");
    if (pv)
      CHECK(pv[0] == 0xAB, "ProposalValue(self) bytes round-trip");

    /* Idempotency: re-Propose overwrites stored value, still emits 1 act. */
    v2[0] = 0xCD;
    n = bkr94acsPropose(a, v2, out);
    CHECK(n == 1, "Re-Propose: 1 act");
    pv = bkr94acsProposalValue(a, 0);
    if (pv)
      CHECK(pv[0] == 0xCD, "Re-Propose: ProposalValue updated");

    free(a);
  }
  a2_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("A4: Defensive nulls and out-of-range origin");
  /* ---------------------------------------------------------------- */
  {
    unsigned long sz;
    struct bkr94acs *a;
    unsigned char dv[1];
    struct bkr94acsAct dout[1];

    dv[0] = 0;

    CHECK(bkr94acsBaDecision(0, 0) == 0xFF, "BaDecision(NULL): 0xFF");
    CHECK(bkr94acsCommittedFig1Count(0) == 0,
          "CommittedFig1Count(NULL): 0");
    CHECK(bkr94acsPropose(0, dv, dout) == 0, "Propose(NULL a): 0");

    sz = bkr94acsSz(3, 0, 10);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto a4_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    CHECK(bkr94acsBaDecision(a, 4) == 0xFF,
          "BaDecision(origin == n): 0xFF");
    CHECK(bkr94acsBaDecision(a, 255) == 0xFF,
          "BaDecision(origin 255): 0xFF");

    free(a);
  }
  a4_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section B — Lemma 2 Parts A/B/C/D + paper-direct invariants     */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("B1: Lemma 2 Parts A/B/C/D — n=4 t=1, ordered delivery");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)('A' + i);

      runHonest(n, vLen, mp, props, 0 /*ordered*/, peers, obs);
      assertLemma2(peers, obs, n, t);

      /* Lemma 2 Part D — explicit value-match check (the implementation
       * of Q(j) = "Fig1 ACCEPTED" also implies the accepted bytes
       * equal what j proposed). */
      {
        unsigned char subset[MAX_PEERS];
        unsigned int sz, j, p;
        sz = bkr94acsSubset(peers[0], subset);
        for (j = 0; j < sz; ++j) {
          unsigned int oj = subset[j];
          for (p = 0; p < n; ++p) {
            const unsigned char *v = bkr94acsProposalValue(peers[p],
                                       (unsigned char)oj);
            CHECK(v != 0 && v[0] == (unsigned char)('A' + oj),
                  "Part D: accepted value matches proposal");
          }
        }
      }

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B2: Lemma 2 — n=4 t=1, shuffled delivery");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)('a' + i);

      runHonest(n, vLen, mp, props, 1 /*shuffled*/, peers, obs);
      assertLemma2(peers, obs, n, t);

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B3: Lemma 2 — n=7 t=2, shuffled delivery");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 7, t = 2, vLen = 1, mp = 10;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)(0x10 + i);

      runHonest(n, vLen, mp, props, 1 /*shuffled*/, peers, obs);
      assertLemma2(peers, obs, n, t);

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B4: Lemma 2 — identical proposals (degenerate values)");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = 0x42;  /* every peer proposes the same byte */

      runHonest(n, vLen, mp, props, 1, peers, obs);
      assertLemma2(peers, obs, n, t);

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B5: Lemma 2 — multi-byte values (vLen=8)");
  /* ---------------------------------------------------------------- */
  {
    unsigned int n = 4, t = 1, vLen = 8, mp = 10;
    unsigned int j;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        for (j = 0; j < vLen; ++j)
          props[i * vLen + j] = (unsigned char)((i << 4) | (j & 0x0F));

      runHonest(n, vLen, mp, props, 1, peers, obs);
      assertLemma2(peers, obs, n, t);

      /* Multi-byte value-match check. */
      {
        unsigned char subset[MAX_PEERS];
        unsigned int sz, p, q;
        sz = bkr94acsSubset(peers[0], subset);
        for (j = 0; j < sz; ++j) {
          unsigned int oj = subset[j];
          for (p = 0; p < n; ++p) {
            const unsigned char *v = bkr94acsProposalValue(peers[p],
                                       (unsigned char)oj);
            CHECK(v != 0, "multi-byte: ProposalValue non-null");
            if (v) {
              for (q = 0; q < vLen; ++q)
                CHECK(v[q] == (unsigned char)((oj << 4) | (q & 0x0F)),
                      "multi-byte: ProposalValue bytes round-trip");
            }
          }
        }
      }

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("B6: Step-2 trigger uses BA-decision count, not Fig1-ACCEPT");
  /* ---------------------------------------------------------------- */
  {
    /*
     * Paper Part A Case (i): step 2 fires iff "2t+1 BAs have already
     * terminated with output 1".  In the n=3t+1 regime that's n-t.
     * The HoneyBadger optimization uses Fig1-ACCEPT count instead;
     * BKR94ACS.txt and bkr94acs.h's own commentary flag this as a
     * deviation (only the decide-1 trigger satisfies Part A case (i)
     * of the BKR94 Lemma 2 proof").
     *
     * Construction (n=4 t=1, single peer P0): deliver a complete
     * proposal-message cascade for origins 0/1/2 (PROP_SEND traffic
     * from peer 0 is the cascade roots; ECHO/READY for those Fig1s
     * is delivered to peer 0 from itself + peers 1/2/3 by direct
     * ProposalInput synthesis).  Deliver NOTHING for origin 3's Fig1
     * and NO consensus-class messages at all.
     *
     * After P0 ACCEPTs Fig1 for 0, 1, 2:
     *   P0 has emitted CON_SEND/INITIAL/conValue=1/origin={0,1,2}
     *     (step-1 inputs, expected).
     *   P0 has decided ZERO BAs (no consensus traffic delivered).
     *   Step-2 trigger condition is therefore unmet.
     *
     * Black-box assertion: P0 has NOT emitted any
     *   CON_SEND/INITIAL/conValue=0/origin=3
     * (the vote-0 fanout that step 2 would produce).  A buggy
     * implementation that triggered on n-t Fig1-ACCEPTs would have.
     */
    unsigned int nAct = 4, t = 1, vLen = 1, mp = 10;
    unsigned long sz;
    struct bkr94acs *p0;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(3, 10)];
    unsigned char val0;
    unsigned int o, src, k;
    unsigned int countOrigin0 = 0;
    unsigned int countOrigin1 = 0;
    unsigned int countOrigin2 = 0;
    unsigned int prematureFanout = 0;

    (void)t;
    sz = bkr94acsSz(nAct - 1, vLen - 1, mp);
    p0 = (struct bkr94acs *)calloc(1, sz);
    if (!p0) goto b6_done;
    bkr94acsInit(p0, (unsigned char)(nAct - 1), 1, (unsigned char)(vLen - 1),
                 (unsigned char)mp, 0, testCoin, 0);

    /* Origin 0 — peer 0 proposes, then synthesises the all-honest
     * cascade locally (INITIAL from peer 0; ECHO from 0/1/2/3;
     * READY from 0/1/2/3 once each peer's threshold trips).  Since
     * we're driving only P0, we synthesise these as direct
     * ProposalInput calls with the relevant 'from' field.  No wire
     * queue used in this banner. */
    val0 = 0x33;
    {
      struct bkr94acsAct propOut[1];
      unsigned int n;
      n = bkr94acsPropose(p0, &val0, propOut);
      CHECK(n == 1, "B6: Propose origin 0 emits 1 act");
    }

    /* For each of origins 0, 1, 2: deliver INITIAL from origin's
     * proposer, then ECHO from all four senders, then READY from all
     * four senders.  This drives Fig1 at P0 to ACCEPT for those
     * origins.  Track CON_SEND emissions per origin to confirm the
     * step-1 input, and to confirm no premature step-2 fanout to
     * origin 3. */
    for (o = 0; o < 3; ++o) {
      unsigned char ov = (unsigned char)(0x30 + o);
      unsigned int n;

      /* INITIAL from the origin itself (loopback for o=0; "remote"
       * for o=1, 2). */
      n = bkr94acsProposalInput(p0, (unsigned char)o, BRACHA87_INITIAL,
                                (unsigned char)o, &ov, out);
      for (k = 0; k < n; ++k) {
        if (out[k].act == BKR94ACS_ACT_CON_SEND
         && out[k].broadcaster == 0
         && out[k].type == BRACHA87_INITIAL) {
          if (out[k].origin == 0) ++countOrigin0;
          else if (out[k].origin == 1) ++countOrigin1;
          else if (out[k].origin == 2) ++countOrigin2;
          else if (out[k].origin == 3 && out[k].conValue == 0)
            ++prematureFanout;
        }
      }

      /* ECHO from each of 0..3. */
      for (src = 0; src < nAct; ++src) {
        n = bkr94acsProposalInput(p0, (unsigned char)o, BRACHA87_ECHO,
                                  (unsigned char)src, &ov, out);
        for (k = 0; k < n; ++k) {
          if (out[k].act == BKR94ACS_ACT_CON_SEND
           && out[k].broadcaster == 0
           && out[k].type == BRACHA87_INITIAL) {
            if (out[k].origin == 0) ++countOrigin0;
            else if (out[k].origin == 1) ++countOrigin1;
            else if (out[k].origin == 2) ++countOrigin2;
            else if (out[k].origin == 3 && out[k].conValue == 0)
              ++prematureFanout;
          }
        }
      }

      /* READY from each of 0..3. */
      for (src = 0; src < nAct; ++src) {
        n = bkr94acsProposalInput(p0, (unsigned char)o, BRACHA87_READY,
                                  (unsigned char)src, &ov, out);
        for (k = 0; k < n; ++k) {
          if (out[k].act == BKR94ACS_ACT_CON_SEND
           && out[k].broadcaster == 0
           && out[k].type == BRACHA87_INITIAL) {
            if (out[k].origin == 0) ++countOrigin0;
            else if (out[k].origin == 1) ++countOrigin1;
            else if (out[k].origin == 2) ++countOrigin2;
            else if (out[k].origin == 3 && out[k].conValue == 0)
              ++prematureFanout;
          }
        }
      }
    }

    /* Step-1 inputs for origins 0/1/2 must have fired exactly once each. */
    CHECK(countOrigin0 == 1, "B6: step-1 input for origin 0 fired exactly once");
    CHECK(countOrigin1 == 1, "B6: step-1 input for origin 1 fired exactly once");
    CHECK(countOrigin2 == 1, "B6: step-1 input for origin 2 fired exactly once");

    /* No BA has decided yet — no consensus traffic delivered. */
    CHECK(bkr94acsBaDecision(p0, 0) == 0xFF,
          "B6: BA_0 still undecided (no consensus delivered)");
    CHECK(bkr94acsBaDecision(p0, 1) == 0xFF, "B6: BA_1 undecided");
    CHECK(bkr94acsBaDecision(p0, 2) == 0xFF, "B6: BA_2 undecided");
    CHECK(bkr94acsBaDecision(p0, 3) == 0xFF, "B6: BA_3 undecided");

    /* Step-2 trigger MUST NOT have fired — Fig1-ACCEPT count is now
     * 3 (= n-t) but BA-decision-with-output-1 count is 0. */
    CHECK(prematureFanout == 0,
          "B6: NO premature step-2 fanout on Fig1-ACCEPT count "
          "(BKR94 Part A Case (i) regression)");

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

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      unsigned char subset[MAX_PEERS];
      unsigned int sz;

      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)i;

      runHonest(n, vLen, mp, props, 1, peers, obs);
      sz = bkr94acsSubset(peers[0], subset);
      CHECK(sz >= n - t, "B7: |SubSet| >= n-t (lower bound is contractual)");
      CHECK(sz <= n, "B7: |SubSet| <= n (upper bound is structural)");
      /* No assertion that sz == n — that would over-specify. */

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section C — BPR / Pump                                          */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("C1: Pump idle on fresh peer (no Propose)");
  /* ---------------------------------------------------------------- */
  {
    /* Per .h: "Returns 0 only when a full sweep finds no committed
     * instance — pre-broadcast / shutdown state".  A freshly-Init'd
     * peer that has not Proposed and received no inputs has no
     * committed Fig1 instances; every Pump call must return 0,
     * regardless of cursor position. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Pump cursor;
    struct bkr94acsAct out[BKR94ACS_PUMP_MAX_ACTS];
    unsigned int j, n;

    sz = bkr94acsSz(3, 0, 10);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto c1_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bracha87PumpInit(&cursor);
    /* Walk well past the cursor space (proposal Fig1s + every owned
     * consensus Fig1 slot).  All return 0. */
    for (j = 0; j < 1024; ++j) {
      n = bkr94acsPump(a, &cursor, out);
      CHECK(n == 0, "C1: fresh peer Pump returns 0 every call");
      if (n != 0) break;
    }
    free(a);
  }
  c1_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("C2: Pump after Propose emits self proposal INITIAL");
  /* ---------------------------------------------------------------- */
  {
    /* Propose sets the ORIGIN bit on self's proposal Fig1.  Per .h
     * BPR rules: ORIGIN → emit INITIAL_ALL on every Bpr call (forever).
     * The cursor must visit self's proposal Fig1 in finite calls and
     * surface the replay. */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Pump cursor;
    struct bkr94acsAct out[BKR94ACS_PUMP_MAX_ACTS];
    struct bkr94acsAct propOut[1];
    unsigned char val = 0xC2;
    unsigned int j, k, n;
    int sawSelfInitial = 0;

    sz = bkr94acsSz(3, 0, 10);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto c2_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bkr94acsPropose(a, &val, propOut);
    bracha87PumpInit(&cursor);

    /* 32 calls is plenty: cursor starts at 0 = proposal Fig1 origin 0
     * (= self), so the first call should already emit. */
    for (j = 0; j < 32; ++j) {
      n = bkr94acsPump(a, &cursor, out);
      CHECK(n <= BKR94ACS_PUMP_MAX_ACTS, "C2: Pump within MAX_ACTS bound");
      for (k = 0; k < n; ++k) {
        if (out[k].act == BKR94ACS_ACT_PROP_SEND
         && out[k].origin == 0
         && out[k].type == BRACHA87_INITIAL) {
          sawSelfInitial = 1;
          /* Borrowed pointer matches stored value. */
          CHECK(out[k].value != 0
             && out[k].value == bkr94acsProposalValue(a, 0)
             && out[k].value[0] == val,
                "C2: Pump emission carries proposal value");
        }
      }
    }
    CHECK(sawSelfInitial, "C2: Pump traversal surfaces self proposal INITIAL");

    free(a);
  }
  c2_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("C3+C4: Pump-driven all-honest run, MAX_ACTS + monotone witness");
  /* ---------------------------------------------------------------- */
  {
    /* Drive an all-honest n=4 t=1 run with Pump in the loop (no
     * drops, no silent peer).  Verify witnesses:
     *   C3: max acts emitted by any Pump call <= BKR94ACS_PUMP_MAX_ACTS
     *   C4: CommittedFig1Count is monotone non-decreasing per peer
     * Plus the standard Lemma 2 properties for sanity. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned int maxPumpActs = 999;
    unsigned int monotoneViolations = 999;
    int rc;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)('p' + i);

      rc = runWithPump(n, vLen, mp, props, 0, -1, 1000, peers, obs,
                       &maxPumpActs, &monotoneViolations);
      CHECK(rc == 0, "C3+C4: all-honest Pump run converges");
      CHECK(maxPumpActs <= BKR94ACS_PUMP_MAX_ACTS,
            "C3: Pump never exceeds BKR94ACS_PUMP_MAX_ACTS");
      CHECK(monotoneViolations == 0,
            "C4: CommittedFig1Count monotone non-decreasing");

      assertLemma2(peers, obs, n, t);

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("C5: Pump full-sweep idle return = 0 (silence-quorum signal)");
  /* ---------------------------------------------------------------- */
  {
    /* The .h documents Pump returning 0 only on full-sweep idle —
     * the only contractual case is "pre-broadcast / shutdown".  This
     * banner re-anchors that on a fresh peer (same as C1, formalised
     * as the silence-quorum-exit signal a deployment uses). */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Pump cursor;
    struct bkr94acsAct out[BKR94ACS_PUMP_MAX_ACTS];
    unsigned int j, n;
    unsigned int zeros = 0;

    sz = bkr94acsSz(3, 0, 10);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto c5_done;
    bkr94acsInit(a, 3, 1, 0, 10, 0, testCoin, 0);

    bracha87PumpInit(&cursor);
    for (j = 0; j < 256; ++j) {
      n = bkr94acsPump(a, &cursor, out);
      if (n == 0) ++zeros;
    }
    CHECK(zeros == 256,
          "C5: pre-Propose Pump returns 0 every call (silence signal)");

    free(a);
  }
  c5_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("C6: Pump-driven convergence at 50% drop");
  /* ---------------------------------------------------------------- */
  {
    /* High-loss network: 50% of every emitted wire is dropped at
     * source.  The protocol's only mechanism for recovering is BPR
     * replay via Pump.  Convergence under loss exercises the replay
     * rules (ORIGIN → INITIAL forever, ECHOED → ECHO forever,
     * RDSENT → READY forever) end-to-end. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned int maxPumpActs;
    unsigned int monotoneViolations;
    int rc;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)(i + 1);

      rc = runWithPump(n, vLen, mp, props, 50, -1, 5000, peers, obs,
                       &maxPumpActs, &monotoneViolations);
      CHECK(rc == 0, "C6: 50% drop run converges");
      CHECK(maxPumpActs <= BKR94ACS_PUMP_MAX_ACTS,
            "C6: Pump within MAX_ACTS bound under loss");
      CHECK(monotoneViolations == 0,
            "C6: CommittedFig1Count monotone under loss");
      if (rc == 0)
        assertLemma2(peers, obs, n, t);

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("C7: Silent Byzantine peer canary (pitfall 11 regression)");
  /* ---------------------------------------------------------------- */
  {
    /* n=4 t=1, peer 3 is Byzantine-silent: never proposes, never
     * receives, never emits.  Honest peers 0/1/2 must converge —
     * SubSet excludes peer 3 via step-2 vote-0 fanout for origin 3.
     *
     * This is the regression for pitfall 11: the originator INITIAL
     * replay must NOT short-circuit on local ECHOED.  Each honest
     * peer is an originator of its own proposal; their Pumps must
     * keep replaying INITIAL forever.  At the n=3t+1 boundary,
     * Bracha's echo threshold ((n+t)/2+1) equals the honest count,
     * so any peer that missed the bootstrap depends on the
     * originator's continued INITIAL replay to complete its echo
     * count.  The original gap-4 design (`ORIGIN && !ECHOED → emit`)
     * stalled at |SubSet|=1 in this setup. */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned int maxPumpActs;
    unsigned int monotoneViolations;
    int rc;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      unsigned char subset[MAX_PEERS];
      unsigned int sz, p, j;

      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      memset(props, 0, sizeof (props));
      for (i = 0; i < n; ++i)
        props[i * vLen] = (unsigned char)(0xA0 + i);

      /* 12.5% drop on top of the silent peer, matching the
       * white-box testBprByzantineSilent setup. */
      rc = runWithPump(n, vLen, mp, props, 12, 3 /* silentPeer */,
                       5000, peers, obs,
                       &maxPumpActs, &monotoneViolations);
      CHECK(rc == 0, "C7: silent Byzantine peer — honest peers converge");
      CHECK(monotoneViolations == 0,
            "C7: CommittedFig1Count monotone with silent peer");

      /* Honest peers (0/1/2) agree on a SubSet, of size >= n-t=3.
       * Origin 3 must be excluded (its Fig1 never accepts at any
       * honest peer because peer 3 never broadcasts its INITIAL). */
      sz = bkr94acsSubset(peers[0], subset);
      CHECK(sz >= n - t, "C7: |SubSet| >= n-t");
      for (p = 1; p < 3; ++p) {
        unsigned char other[MAX_PEERS];
        unsigned int szOther = bkr94acsSubset(peers[p], other);
        CHECK(szOther == sz, "C7: honest peers agree on SubSet size");
        if (szOther == sz)
          CHECK(memcmp(subset, other, sz) == 0,
                "C7: honest peers agree on SubSet contents");
      }
      for (j = 0; j < sz; ++j)
        CHECK(subset[j] != 3, "C7: SubSet excludes silent peer");

      freeCluster(peers, n);
    }
  }

  /* ---------------------------------------------------------------- */
  /*  Section D — EXHAUSTED                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("D1: BA_EXHAUSTED single emission, 0xFE sentinel, !complete");
  /* ---------------------------------------------------------------- */
  {
    /* maxPhases=1 → BA has only 1 phase (3 sub-rounds) to terminate.
     * Drive split values across all 3 sub-rounds at every broadcaster
     * so neither the >2t case (i) nor the >t case (ii) of Fig4
     * step 3 fires.  Fig4 returns BRACHA87_EXHAUSTED.  BKR94 surfaces
     * BKR94ACS_ACT_BA_EXHAUSTED exactly once, sets baDecision[0]=0xFE,
     * and never sets a->complete (no unilateral substitute is safe —
     * Part C of Lemma 2 agreement would break). */
    unsigned long sz;
    struct bkr94acs *a;
    struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PEERS, 1)];
    unsigned int round, b, n, k;
    unsigned int exhaustedSeen = 0;

    sz = bkr94acsSz(3, 0, 1);   /* n=4, vLen=1, maxPhases=1 */
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto d1_done;
    bkr94acsInit(a, 3, 1, 0, 1, 0, testCoin, 0);

    /* Drive every (round, broadcaster) Fig1 in phase 0 to ACCEPT
     * with a value that splits 2/2 across broadcasters per round. */
    for (round = 0; round < 3; ++round)
      for (b = 0; b < 4; ++b)
        feedConsensusAccept(a, 0, (unsigned char)round, (unsigned char)b,
                            (b < 2) ? 0 : 1, out, &exhaustedSeen);

    CHECK(exhaustedSeen == 1, "D1: BA_EXHAUSTED emitted exactly once");
    CHECK(bkr94acsBaDecision(a, 0) == 0xFE,
          "D1: baDecision[0] == 0xFE (exhausted sentinel)");
    CHECK(a->complete == 0,
          "D1: a->complete remains 0 (no unilateral substitute)");

    /* Subsequent consensus input for the exhausted origin must NOT
     * re-emit BA_EXHAUSTED. */
    n = bkr94acsConsensusInput(a, 0, 0, 0, BRACHA87_READY, 0, 0, out);
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED)
        ++exhaustedSeen;
    CHECK(exhaustedSeen == 1, "D1: no duplicate BA_EXHAUSTED on later input");

    free(a);
  }
  d1_done: ;

  /* ---------------------------------------------------------------- */
  BANNER("D2: Pump continues past EXHAUSTED for that origin");
  /* ---------------------------------------------------------------- */
  {
    /* Per .h: "BPR pump continues for that origin (0xFE != 0 in the
     * pump gate) so other peers may still benefit from earlier-round
     * echoes / readys."  After EXHAUSTED for origin 0, Pump must
     * still emit replays for the consensus Fig1s belonging to
     * origin 0 (the ones that ACCEPTed earlier). */
    unsigned long sz;
    struct bkr94acs *a;
    struct bracha87Pump cursor;
    struct bkr94acsAct out[BKR94ACS_PUMP_MAX_ACTS];
    struct bkr94acsAct synthOut[BKR94ACS_MAX_ACTS(MAX_PEERS, 1)];
    unsigned int round, b, j, k, n;
    unsigned int exhaustedSeen = 0;
    unsigned int origin0Replays = 0;

    sz = bkr94acsSz(3, 0, 1);
    a = (struct bkr94acs *)calloc(1, sz);
    if (!a) goto d2_done;
    bkr94acsInit(a, 3, 1, 0, 1, 0, testCoin, 0);

    /* Set up an EXHAUSTED state same as D1. */
    for (round = 0; round < 3; ++round)
      for (b = 0; b < 4; ++b)
        feedConsensusAccept(a, 0, (unsigned char)round, (unsigned char)b,
                            (b < 2) ? 0 : 1, synthOut, &exhaustedSeen);
    CHECK(exhaustedSeen == 1, "D2: EXHAUSTED setup OK");
    CHECK(bkr94acsCommittedFig1Count(a) > 0,
          "D2: post-EXHAUSTED CommittedFig1Count > 0");

    /* Sweep Pump enough to traverse all Fig1 slots; count CON_SEND
     * replays for origin 0 (the EXHAUSTED origin). */
    bracha87PumpInit(&cursor);
    for (j = 0; j < 2048; ++j) {
      n = bkr94acsPump(a, &cursor, out);
      for (k = 0; k < n; ++k) {
        if (out[k].act == BKR94ACS_ACT_CON_SEND && out[k].origin == 0)
          ++origin0Replays;
      }
    }
    CHECK(origin0Replays > 0,
          "D2: Pump continues to replay consensus Fig1s for EXHAUSTED origin");

    free(a);
  }
  d2_done: ;

  /* ---------------------------------------------------------------- */
  /*  Section E — Byzantine                                           */
  /* ---------------------------------------------------------------- */

  /* ---------------------------------------------------------------- */
  BANNER("E1: Equivocating proposer (Bracha Lemma 2 inheritance)");
  /* ---------------------------------------------------------------- */
  {
    /*
     * n=4 t=1, peer 0 is Byzantine and equivocates its own proposal:
     *   INITIAL/v1 → peers 1, 2
     *   INITIAL/v2 → peer 3
     * Peer 0 sends nothing else (no echoes, no readys, no consensus).
     *
     * Bracha 1987 Lemma 2: "if two correct processes accept u and v,
     * then u = v."  Composed at the BKR94 layer: any honest peer
     * that ACCEPTs origin 0's Fig1 must accept the same value as any
     * other honest peer that ACCEPTs.  In this split it's likely
     * neither v1 nor v2 reaches the (n+t)/2+1=3 echo threshold at
     * any honest peer, so Fig1 origin 0 never accepts → BA_0 decides
     * 0 via step-2 fanout → SubSet excludes peer 0.
     *
     * Black-box assertion: ACS still completes; honest peers agree
     * on SubSet; if any honest peer's bkr94acsProposalValue(0) is
     * non-null, all honest peers see the same bytes there (Lemma 2);
     * |SubSet| >= n-t.  Honest peers 1, 2, 3 propose and run
     * normally; the harness manually injects peer 0's split INITIAL.
     */
    unsigned int n = 4, t = 1, vLen = 1, mp = 10;
    unsigned char v1 = 0xE1;
    unsigned char v2 = 0xE2;

    if (allocCluster(peers, n, t, vLen - 1, mp) == 0) {
      struct bracha87Pump cursors[MAX_PEERS];
      unsigned long actsCap;
      struct bkr94acsAct *out;
      struct bkr94acsAct propOut[1];
      struct bkr94acsAct pumpOut[BKR94ACS_PUMP_MAX_ACTS];
      struct wire w;
      unsigned int iter, j, p, q, sz;
      int allComplete;
      unsigned char subset[MAX_PEERS];

      { unsigned int oi; for (oi = 0; oi < MAX_PEERS; ++oi) obsInit(&obs[oi]); }
      qReset();

      actsCap = BKR94ACS_MAX_ACTS(n - 1, mp);
      out = (struct bkr94acsAct *)malloc(actsCap * sizeof (*out));
      if (!out) { freeCluster(peers, n); goto e1_done; }

      for (i = 0; i < n; ++i)
        bracha87PumpInit(&cursors[i]);

      /* Peer 0's Byzantine equivocation: split-INITIAL emission only.
       * No Propose, no Pump for peer 0 — this attacker only sends
       * the bootstrap INITIAL, then is silent. */
      memset(&w, 0, sizeof (w));
      w.cls = BKR94ACS_CLS_PROPOSAL;
      w.origin = 0;
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

      /* Honest peers 1, 2, 3 Propose. */
      for (p = 1; p < n; ++p) {
        unsigned char val = (unsigned char)(0xB0 + p);
        unsigned int nact = bkr94acsPropose(peers[p], &val, propOut);
        observeAndEmit(&obs[p], (unsigned char)p, n, propOut, nact, vLen,
                       0, -1);
      }

      for (iter = 0; iter < 2000; ++iter) {
        while (qSize() > 0) {
          unsigned int nact;
          qPopHead(&w);
          if (w.to == 0)
            continue;  /* Byzantine peer 0 is also silent on receive */
          if (w.cls == BKR94ACS_CLS_PROPOSAL)
            nact = bkr94acsProposalInput(peers[w.to], w.origin, w.type,
                                         w.from, w.value, out);
          else
            nact = bkr94acsConsensusInput(peers[w.to], w.origin, w.round,
                                          w.broadcaster, w.type, w.from,
                                          w.conValue, out);
          observeAndEmit(&obs[w.to], w.to, n, out, nact, vLen, 0, -1);
        }
        for (p = 1; p < n; ++p) {
          unsigned int nact = bkr94acsPump(peers[p], &cursors[p], pumpOut);
          observeAndEmit(&obs[p], (unsigned char)p, n, pumpOut, nact, vLen,
                         0, -1);
        }
        allComplete = 1;
        for (p = 1; p < n; ++p)
          if (peers[p]->complete == 0) { allComplete = 0; break; }
        if (allComplete) break;
      }
      free(out);

      /* Honest peers (1, 2, 3) all completed. */
      for (p = 1; p < n; ++p)
        CHECK(peers[p]->complete != 0,
              "E1: honest peer completes despite equivocating proposer");

      /* Honest peers agree on SubSet (Lemma 2 Part C). */
      sz = bkr94acsSubset(peers[1], subset);
      CHECK(sz >= n - t, "E1: |SubSet| >= n-t");
      for (p = 2; p < n; ++p) {
        unsigned char other[MAX_PEERS];
        unsigned int szOther = bkr94acsSubset(peers[p], other);
        CHECK(szOther == sz, "E1: honest SubSet sizes agree");
        if (szOther == sz)
          CHECK(memcmp(subset, other, sz) == 0,
                "E1: honest SubSet contents agree");
      }

      /* Bracha Lemma 2 inheritance via the bkr94acs.h contract:
       *
       *   "Returns pointer to the vLen + 1 byte value, or 0 if not
       *    yet accepted (or, for self-origin, not yet proposed)."
       *
       * For a non-self origin, ProposalValue is non-null iff the
       * local Fig1 has ACCEPTED.  Bracha Lemma 2 then guarantees any
       * two honest acceptors agree on the value.  Equivocation by
       * peer 0 must not produce a state where peer A's
       * bkr94acsProposalValue(0) == v1 and peer B's == v2.
       *
       * (BA_0 deciding 0 across all peers — i.e. SubSet excludes
       * peer 0 — is the expected case here, since neither v1 nor v2
       * can reach the (n+t)/2+1 echo threshold under this split.) */
      {
        for (p = 1; p < n; ++p) {
          const unsigned char *v_a = bkr94acsProposalValue(peers[p], 0);
          unsigned int q2;
          for (q2 = p + 1; q2 < n; ++q2) {
            const unsigned char *v_b = bkr94acsProposalValue(peers[q2], 0);
            if (v_a && v_b)
              CHECK(v_a[0] == v_b[0],
                    "E1: Bracha Lemma 2 — accepted values agree across honest peers");
          }
        }
        /* Honest peers' own proposal values must round-trip
         * (orthogonal to peer 0's equivocation). */
        for (p = 1; p < n; ++p) {
          for (q = 1; q < n; ++q) {
            const unsigned char *v = bkr94acsProposalValue(peers[p],
                                       (unsigned char)q);
            CHECK(v != 0 && v[0] == (unsigned char)(0xB0 + q),
                  "E1: honest proposal values preserved");
          }
        }
      }

      /* SubSet contents include only origins for which Q(j)=1, i.e.
       * Fig1 ACCEPTED at the local peer.  This is Lemma 2 Part D
       * inherited from Section B. */
      for (j = 0; j < sz; ++j) {
        unsigned char oj = subset[j];
        for (p = 1; p < n; ++p)
          CHECK(bkr94acsProposalValue(peers[p], oj) != 0,
                "E1: Part D — SubSet members have accepted values");
      }

      freeCluster(peers, n);
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
