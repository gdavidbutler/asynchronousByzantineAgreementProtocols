/*
 * Tests for bkr94acs.[hc] — BKR94 Asynchronous Common Subset.
 *
 * Simulates all-to-all message passing for BKR94 ACS:
 *   N A-Cast broadcasts (Bracha87 Fig1 with arbitrary values)
 *   N binary BAes (Bracha87 Fig4 per process)
 *
 * Verifies:
 *   Agreement  — all honest processes decide the same subset
 *   Validity   — subset contains at least n-t processes
 *   Totality   — all BAs decide (BKR94ACS_F_COMPLETE flag set)
 *   Values     — accepted A-Cast values match what was A-Cast
 *   Ordering   — deterministic sort produces identical order at each process
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bkr94acs.h"

static int Fail;

static void
check(
  const char *name
 ,int cond
){
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", name);
    ++Fail;
  }
}

/*------------------------------------------------------------------------*/
/*  Coin — deterministic alternating. Adequate for tests; adversarial     */
/*  deployments should pass a local random source.                        */
/*------------------------------------------------------------------------*/

static unsigned char
testCoin(
  void *closure
 ,unsigned char phase
){
  (void)closure;
  return (phase % 2);
}

/*------------------------------------------------------------------------*/
/*  Message queue — same pattern as example/bkr94acs.c                    */
/*------------------------------------------------------------------------*/

#define MAX_PROCESSES  16
#define MAX_PHASES 10
#define MAX_VLEN   32
#define MAX_MSGS   (1024u * 1024u)

struct msg {
  unsigned char cls;
  unsigned char process;
  unsigned char round;
  unsigned char initiator;
  unsigned char type;
  unsigned char from;
  unsigned char to;
  unsigned char value[MAX_VLEN];
};

static struct msg *MsgQ;
static unsigned int Qhead;
static unsigned int Qtail;

static void
qInit(
  void
){
  Qhead = Qtail = 0;
}

static void
qPush(
  unsigned char cls
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char type
 ,unsigned char from
 ,unsigned char to
 ,const unsigned char *value
 ,unsigned int valueLen
){
  if (Qtail >= MAX_MSGS)
    return;
  MsgQ[Qtail].cls = cls;
  MsgQ[Qtail].process = process;
  MsgQ[Qtail].round = round;
  MsgQ[Qtail].initiator = initiator;
  MsgQ[Qtail].type = type;
  MsgQ[Qtail].from = from;
  MsgQ[Qtail].to = to;
  memcpy(MsgQ[Qtail].value, value, valueLen);
  ++Qtail;
}

static void
qShuffle(
  unsigned int *seed
){
  unsigned int n;
  unsigned int i;

  n = Qtail - Qhead;
  if (n < 2)
    return;
  for (i = n - 1; i > 0; --i) {
    unsigned int j;
    struct msg tmp;

    *seed = *seed * 1103515245u + 12345u;
    j = ((*seed >> 16) & 0x7FFF) % (i + 1);
    tmp = MsgQ[Qhead + i];
    MsgQ[Qhead + i] = MsgQ[Qhead + j];
    MsgQ[Qhead + j] = tmp;
  }
}

/*------------------------------------------------------------------------*/
/*  BKR94 ACS simulation engine                                           */
/*------------------------------------------------------------------------*/

struct acsResult {
  int complete;
  unsigned int subsetCnt;
  unsigned char subset[MAX_PROCESSES];
};

/*
 * Run one BKR94 ACS simulation.
 * acasts[i] is a NUL-terminated string for process i.
 * vLen is the padded value length (>= longest string + 1).
 * Returns 0 on success, -1 on allocation failure.
 */
static int
runAcs(
  unsigned int n
 ,unsigned int t
 ,const char acasts[][MAX_VLEN]
 ,unsigned int vLen
 ,unsigned int shuffleSeed
 ,struct acsResult results[]
){
  struct bkr94acs *processes[MAX_PROCESSES];
  unsigned long sz;
  unsigned int i;
  unsigned int j;

  sz = bkr94acsSz(n - 1, vLen - 1, MAX_PHASES);
  memset(processes, 0, sizeof (processes));
  for (i = 0; i < n; ++i) {
    processes[i] = (struct bkr94acs *)calloc(1, sz);
    if (!processes[i]) {
      for (j = 0; j < i; ++j)
        free(processes[j]);
      return (-1);
    }
    bkr94acsInit(processes[i], (unsigned char)(n - 1), (unsigned char)t,
                 (unsigned char)(vLen - 1), MAX_PHASES, (unsigned char)i,
                 testCoin, 0);
  }

  qInit();

  /* Bootstrap: each process broadcasts INITIAL of their A-Cast */
  for (i = 0; i < n; ++i)
    for (j = 0; j < n; ++j)
      qPush(BKR94ACS_CLS_ACAST, (unsigned char)i, 0, 0,
            BRACHA87_INITIAL, (unsigned char)i, (unsigned char)j,
            (const unsigned char *)acasts[i], vLen);

  if (shuffleSeed)
    qShuffle(&shuffleSeed);

  /* Process message queue */
  while (Qhead < Qtail) {
    struct msg *m;
    struct bkr94acs *st;
    struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
    unsigned int nacts;
    unsigned int k;
    unsigned int oldTail;

    m = &MsgQ[Qhead++];
    st = processes[m->to];

    /*
     * Do NOT skip messages addressed to locally-complete processes.
     * A process that has decided all N BAs must keep processing
     * incoming messages so its Fig1 echoes/readys continue to
     * reach processes still working on some BAs.  Skipping replicates
     * the post-decide stall the library itself was fixed to avoid
     * (see bkr94acs.c bkr94acsBaInput comment on
     * BKR94ACS_F_COMPLETE).
     */
    oldTail = Qtail;

    if (m->cls == BKR94ACS_CLS_ACAST) {
      nacts = bkr94acsAcastInput(st, m->process, m->type, m->from,
                                    m->value, acts);
    } else {
      nacts = bkr94acsBaInput(st, m->process, m->round,
                                     m->initiator, m->type,
                                     m->from, m->value[0], acts);
    }

    for (k = 0; k < nacts; ++k) {
      unsigned int p;

      switch (acts[k].act) {
      case BKR94ACS_ACT_ACAST_SEND:
        if (!acts[k].value)
          break;
        for (p = 0; p < n; ++p)
          qPush(BKR94ACS_CLS_ACAST, acts[k].process, 0, 0,
                acts[k].type, m->to, (unsigned char)p,
                acts[k].value, vLen);
        break;
      case BKR94ACS_ACT_BA_SEND:
        for (p = 0; p < n; ++p)
          qPush(BKR94ACS_CLS_BA, acts[k].process, acts[k].round,
                acts[k].initiator, acts[k].type,
                m->to, (unsigned char)p,
                &acts[k].baValue, 1);
        break;
      default:
        break;
      }
    }

    if (shuffleSeed && Qtail > oldTail)
      qShuffle(&shuffleSeed);
  }

  /* Collect results */
  for (i = 0; i < n; ++i) {
    results[i].complete = (processes[i]->flags & BKR94ACS_F_COMPLETE) ? 1 : 0;
    results[i].subsetCnt = bkr94acsSubset(processes[i], results[i].subset);
  }

  for (i = 0; i < n; ++i)
    free(processes[i]);

  return (0);
}

/*------------------------------------------------------------------------*/
/*  Verification helpers                                                  */
/*------------------------------------------------------------------------*/

/* Check: all processes completed */
static int
allComplete(
  const struct acsResult results[]
 ,unsigned int n
){
  unsigned int i;

  for (i = 0; i < n; ++i)
    if (!results[i].complete)
      return (0);
  return (1);
}

/* Check: all processes agree on the same subset */
static int
allAgree(
  const struct acsResult results[]
 ,unsigned int n
){
  unsigned int i;

  for (i = 1; i < n; ++i) {
    if (results[i].subsetCnt != results[0].subsetCnt)
      return (0);
    if (memcmp(results[i].subset, results[0].subset,
               results[0].subsetCnt))
      return (0);
  }
  return (1);
}

/* Check: subset size >= n-t */
static int
subsetValid(
  const struct acsResult results[]
 ,unsigned int n
 ,unsigned int t
){
  unsigned int i;

  for (i = 0; i < n; ++i)
    if (results[i].subsetCnt < n - t)
      return (0);
  return (1);
}

/*------------------------------------------------------------------------*/
/*  Test cases                                                            */
/*------------------------------------------------------------------------*/

static void
testBasic(
  void
){
  char acasts[MAX_PROCESSES][MAX_VLEN];
  struct acsResult results[MAX_PROCESSES];
  unsigned int vLen;
  char label[128];
  unsigned int n;
  unsigned int t;

  printf("BKR94 ACS — Basic honest tests\n");

  /* n=1, t=0 */
  memset(acasts, 0, sizeof (acasts));
  strcpy(acasts[0], "hello");
  vLen = 6;
  check("n=1 t=0 run", runAcs(1, 0, acasts, vLen, 0, results) == 0);
  check("n=1 t=0 complete", allComplete(results, 1));
  check("n=1 t=0 subset==1", results[0].subsetCnt == 1);
  printf("  n=1  t=0: subset %u/%u\n", results[0].subsetCnt, 1);

  /* n=4, t=0: all A-Casts must be included */
  memset(acasts, 0, sizeof (acasts));
  strcpy(acasts[0], "joe");
  strcpy(acasts[1], "sam");
  strcpy(acasts[2], "sally");
  strcpy(acasts[3], "tim");
  vLen = 6;
  check("n=4 t=0 run", runAcs(4, 0, acasts, vLen, 0, results) == 0);
  check("n=4 t=0 complete", allComplete(results, 4));
  check("n=4 t=0 agree", allAgree(results, 4));
  check("n=4 t=0 subset==4", results[0].subsetCnt == 4);
  printf("  n=4  t=0: subset %u/%u\n", results[0].subsetCnt, 4);

  /* n=4, t=1 */
  check("n=4 t=1 run", runAcs(4, 1, acasts, vLen, 0, results) == 0);
  check("n=4 t=1 complete", allComplete(results, 4));
  check("n=4 t=1 agree", allAgree(results, 4));
  check("n=4 t=1 valid", subsetValid(results, 4, 1));
  printf("  n=4  t=1: subset %u/%u\n", results[0].subsetCnt, 4);

  /* Sweep: various n/t combinations */
  {
    static const unsigned int cases[][2] = {
      {4, 1}, {5, 1}, {7, 2}, {8, 2}, {10, 3}, {13, 4}
    };
    unsigned int c;
    unsigned int i;

    for (c = 0; c < sizeof (cases) / sizeof (cases[0]); ++c) {
      n = cases[c][0];
      t = cases[c][1];
      memset(acasts, 0, sizeof (acasts));
      for (i = 0; i < n; ++i) {
        acasts[i][0] = 'A' + (char)i;
        acasts[i][1] = '\0';
      }
      vLen = 2;
      sprintf(label, "n=%u t=%u run", n, t);
      check(label, runAcs(n, t, acasts, vLen, 0, results) == 0);
      sprintf(label, "n=%u t=%u complete", n, t);
      check(label, allComplete(results, n));
      sprintf(label, "n=%u t=%u agree", n, t);
      check(label, allAgree(results, n));
      sprintf(label, "n=%u t=%u valid", n, t);
      check(label, subsetValid(results, n, t));
      printf("  n=%-2u t=%u: subset %u/%u\n",
             n, t, results[0].subsetCnt, n);
    }
  }
}

static void
testShuffled(
  void
){
  char acasts[MAX_PROCESSES][MAX_VLEN];
  struct acsResult results[MAX_PROCESSES];
  char label[128];
  unsigned int seed;
  unsigned int n;
  unsigned int t;

  /*
   * With shuffled delivery, different processes accept different A-Casts
   * first, causing enter splits in some BA instances. The deterministic
   * alternating coin can resolve these splits against inclusion,
   * making the subset smaller than n-t. With a random coin, BA
   * terminates w.h.p. and BKR94 Lemma 2 gives |SubSet| >= 2t+1 = n-t.
   *
   * Here we verify the hard guarantees: totality and agreement.
   */

  printf("\nBKR94 ACS — Shuffled delivery tests\n");

  /* n=4, t=1 with 20 different seeds */
  n = 4;
  t = 1;
  memset(acasts, 0, sizeof (acasts));
  strcpy(acasts[0], "alpha");
  strcpy(acasts[1], "bravo");
  strcpy(acasts[2], "charlie");
  strcpy(acasts[3], "delta");

  printf("  n=%u t=%u seeds 1-20: ", n, t);
  for (seed = 1; seed <= 20; ++seed) {
    sprintf(label, "n=4 t=1 seed=%u run", seed);
    check(label, runAcs(n, t, acasts, 8, seed, results) == 0);
    sprintf(label, "n=4 t=1 seed=%u complete", seed);
    check(label, allComplete(results, n));
    sprintf(label, "n=4 t=1 seed=%u agree", seed);
    check(label, allAgree(results, n));
    sprintf(label, "n=4 t=1 seed=%u nonempty", seed);
    check(label, results[0].subsetCnt > 0);
  }
  printf("all agreed\n");

  /* n=7, t=2 with 10 seeds */
  n = 7;
  t = 2;
  memset(acasts, 0, sizeof (acasts));
  {
    unsigned int i;

    for (i = 0; i < n; ++i) {
      acasts[i][0] = 'a' + (char)i;
      acasts[i][1] = '\0';
    }
  }
  printf("  n=%u t=%u seeds 1-10: ", n, t);
  for (seed = 1; seed <= 10; ++seed) {
    sprintf(label, "n=7 t=2 seed=%u run", seed);
    check(label, runAcs(n, t, acasts, 2, seed, results) == 0);
    sprintf(label, "n=7 t=2 seed=%u complete", seed);
    check(label, allComplete(results, n));
    sprintf(label, "n=7 t=2 seed=%u agree", seed);
    check(label, allAgree(results, n));
    sprintf(label, "n=7 t=2 seed=%u nonempty", seed);
    check(label, results[0].subsetCnt > 0);
  }
  printf("all agreed\n");

  /* n=10, t=3 with 5 seeds */
  n = 10;
  t = 3;
  memset(acasts, 0, sizeof (acasts));
  {
    unsigned int i;

    for (i = 0; i < n; ++i) {
      acasts[i][0] = 'A' + (char)i;
      acasts[i][1] = '\0';
    }
  }
  printf("  n=%u t=%u seeds 1-5: ", n, t);
  for (seed = 1; seed <= 5; ++seed) {
    sprintf(label, "n=10 t=3 seed=%u run", seed);
    check(label, runAcs(n, t, acasts, 2, seed, results) == 0);
    sprintf(label, "n=10 t=3 seed=%u complete", seed);
    check(label, allComplete(results, n));
    sprintf(label, "n=10 t=3 seed=%u agree", seed);
    check(label, allAgree(results, n));
    sprintf(label, "n=10 t=3 seed=%u nonempty", seed);
    check(label, results[0].subsetCnt > 0);
  }
  printf("all agreed\n");
}

static void
testValues(
  void
){
  char acasts[MAX_PROCESSES][MAX_VLEN];
  struct bkr94acs *processes[MAX_PROCESSES];
  unsigned long sz;
  unsigned int n;
  unsigned int t;
  unsigned int vLen;
  unsigned int i;
  unsigned int j;
  int valuesOk;

  printf("\nBKR94 ACS — A-Cast value integrity tests\n");

  /*
   * Verify that accepted A-Cast values match what was A-Cast.
   * Run a simulation, then check bkr94acsAcastValue for each
   * included process.
   */
  n = 4;
  t = 1;
  memset(acasts, 0, sizeof (acasts));
  strcpy(acasts[0], "joe");
  strcpy(acasts[1], "sam");
  strcpy(acasts[2], "sally");
  strcpy(acasts[3], "tim");
  vLen = 6;

  sz = bkr94acsSz(n - 1, vLen - 1, MAX_PHASES);
  for (i = 0; i < n; ++i) {
    processes[i] = (struct bkr94acs *)calloc(1, sz);
    bkr94acsInit(processes[i], (unsigned char)(n - 1), (unsigned char)t,
                 (unsigned char)(vLen - 1), MAX_PHASES, (unsigned char)i,
                 testCoin, 0);
  }

  qInit();
  for (i = 0; i < n; ++i)
    for (j = 0; j < n; ++j)
      qPush(BKR94ACS_CLS_ACAST, (unsigned char)i, 0, 0,
            BRACHA87_INITIAL, (unsigned char)i, (unsigned char)j,
            (const unsigned char *)acasts[i], vLen);

  while (Qhead < Qtail) {
    struct msg *m;
    struct bkr94acs *st;
    struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
    unsigned int nacts;
    unsigned int k;

    m = &MsgQ[Qhead++];
    st = processes[m->to];
    if (st->flags & BKR94ACS_F_COMPLETE)
      continue;

    if (m->cls == BKR94ACS_CLS_ACAST) {
      nacts = bkr94acsAcastInput(st, m->process, m->type, m->from,
                                    m->value, acts);
    } else {
      nacts = bkr94acsBaInput(st, m->process, m->round,
                                     m->initiator, m->type,
                                     m->from, m->value[0], acts);
    }

    for (k = 0; k < nacts; ++k) {
      unsigned int p;

      switch (acts[k].act) {
      case BKR94ACS_ACT_ACAST_SEND:
        if (!acts[k].value)
          break;
        for (p = 0; p < n; ++p)
          qPush(BKR94ACS_CLS_ACAST, acts[k].process, 0, 0,
                acts[k].type, m->to, (unsigned char)p,
                acts[k].value, vLen);
        break;
      case BKR94ACS_ACT_BA_SEND:
        for (p = 0; p < n; ++p)
          qPush(BKR94ACS_CLS_BA, acts[k].process, acts[k].round,
                acts[k].initiator, acts[k].type,
                m->to, (unsigned char)p,
                &acts[k].baValue, 1);
        break;
      default:
        break;
      }
    }
  }

  /* Check A-Cast values at each process for each included process */
  valuesOk = 1;
  for (i = 0; i < n; ++i) {
    unsigned char subset[MAX_PROCESSES];
    unsigned int cnt;

    check("values: complete", processes[i]->flags & BKR94ACS_F_COMPLETE);
    cnt = bkr94acsSubset(processes[i], subset);
    for (j = 0; j < cnt; ++j) {
      const unsigned char *pv;

      pv = bkr94acsAcastValue(processes[i], subset[j]);
      if (!pv || memcmp(pv, acasts[subset[j]], vLen))
        valuesOk = 0;
    }
  }
  check("A-Cast values match", valuesOk);
  printf("  n=4  t=1: A-Cast values verified at all processes\n");

  for (i = 0; i < n; ++i)
    free(processes[i]);
}

static void
testMultiByteValues(
  void
){
  char acasts[MAX_PROCESSES][MAX_VLEN];
  struct acsResult results[MAX_PROCESSES];
  unsigned int n;
  unsigned int t;
  unsigned int vLen;
  unsigned int i;

  printf("\nBKR94 ACS — Multi-byte value tests\n");

  /* Long strings: test vLen > 1 */
  n = 4;
  t = 1;
  memset(acasts, 0, sizeof (acasts));
  strcpy(acasts[0], "the quick brown fox");
  strcpy(acasts[1], "jumps over the lazy");
  strcpy(acasts[2], "dog and then some..");
  strcpy(acasts[3], "extra long string!!");
  vLen = 20;

  check("long strings run",
        runAcs(n, t, acasts, vLen, 0, results) == 0);
  check("long strings complete", allComplete(results, n));
  check("long strings agree", allAgree(results, n));
  check("long strings valid", subsetValid(results, n, t));
  printf("  n=4  t=1 vLen=20: subset %u/%u\n",
         results[0].subsetCnt, n);

  /* Single-byte values (vLen=1, like binary but with arbitrary bytes) */
  n = 7;
  t = 2;
  memset(acasts, 0, sizeof (acasts));
  for (i = 0; i < n; ++i)
    acasts[i][0] = (char)(10 + i);
  vLen = 1;

  check("single-byte run",
        runAcs(n, t, acasts, vLen, 0, results) == 0);
  check("single-byte complete", allComplete(results, n));
  check("single-byte agree", allAgree(results, n));
  check("single-byte valid", subsetValid(results, n, t));
  printf("  n=7  t=2 vLen=1: subset %u/%u\n",
         results[0].subsetCnt, n);
}

static void
testIdenticalAcasts(
  void
){
  char acasts[MAX_PROCESSES][MAX_VLEN];
  struct acsResult results[MAX_PROCESSES];

  printf("\nBKR94 ACS — Identical A-Cast tests\n");

  /* All processes A-Cast the same value */
  memset(acasts, 0, sizeof (acasts));
  strcpy(acasts[0], "same");
  strcpy(acasts[1], "same");
  strcpy(acasts[2], "same");
  strcpy(acasts[3], "same");

  check("identical run", runAcs(4, 1, acasts, 5, 0, results) == 0);
  check("identical complete", allComplete(results, 4));
  check("identical agree", allAgree(results, 4));
  check("identical valid", subsetValid(results, 4, 1));
  printf("  n=4  t=1 all-same: subset %u/%u\n",
         results[0].subsetCnt, 4);
}

static void
testLargerN(
  void
){
  char acasts[MAX_PROCESSES][MAX_VLEN];
  struct acsResult results[MAX_PROCESSES];
  unsigned int n;
  unsigned int t;
  unsigned int i;
  unsigned int seed;
  char label[128];

  printf("\nBKR94 ACS — Larger N tests\n");

  /* n=13, t=4 */
  n = 13;
  t = 4;
  memset(acasts, 0, sizeof (acasts));
  for (i = 0; i < n; ++i) {
    acasts[i][0] = 'A' + (char)i;
    acasts[i][1] = '\0';
  }

  check("n=13 t=4 run", runAcs(n, t, acasts, 2, 0, results) == 0);
  check("n=13 t=4 complete", allComplete(results, n));
  check("n=13 t=4 agree", allAgree(results, n));
  check("n=13 t=4 valid", subsetValid(results, n, t));
  printf("  n=13 t=4: subset %u/%u\n", results[0].subsetCnt, n);

  /* n=16, t=5 */
  n = 16;
  t = 5;
  memset(acasts, 0, sizeof (acasts));
  for (i = 0; i < n; ++i) {
    acasts[i][0] = 'a' + (char)i;
    acasts[i][1] = '\0';
  }

  check("n=16 t=5 run", runAcs(n, t, acasts, 2, 0, results) == 0);
  check("n=16 t=5 complete", allComplete(results, n));
  check("n=16 t=5 agree", allAgree(results, n));
  check("n=16 t=5 valid", subsetValid(results, n, t));
  printf("  n=16 t=5: subset %u/%u\n", results[0].subsetCnt, n);

  /* n=13 t=4 shuffled */
  n = 13;
  t = 4;
  memset(acasts, 0, sizeof (acasts));
  for (i = 0; i < n; ++i) {
    acasts[i][0] = 'A' + (char)i;
    acasts[i][1] = '\0';
  }
  printf("  n=%u t=%u shuffled seeds 1-5: ", n, t);
  for (seed = 1; seed <= 5; ++seed) {
    sprintf(label, "n=13 t=4 seed=%u run", seed);
    check(label, runAcs(n, t, acasts, 2, seed, results) == 0);
    sprintf(label, "n=13 t=4 seed=%u complete", seed);
    check(label, allComplete(results, n));
    sprintf(label, "n=13 t=4 seed=%u agree", seed);
    check(label, allAgree(results, n));
    sprintf(label, "n=13 t=4 seed=%u valid", seed);
    check(label, subsetValid(results, n, t));
  }
  printf("all agreed\n");
}

/*------------------------------------------------------------------------*/
/*  Post-decide continuation regression test                              */
/*                                                                        */
/*  Bracha Fig4 requires a decided process to keep broadcasting so other  */
/*  processes can decide.  An earlier version of bkr94acs.c short-circuited   */
/*  bkr94acsBaInput when bkr94acsDecision[process] != 0xFF,         */
/*  silently dropping all post-decide messages.  This test pokes the BA   */
/*  decision marker directly, then verifies that a fresh BA        */
/*  INITIAL for that process still drives Fig1 (ECHO output) rather than */
/*  returning zero.                                                       */
/*  With the bug:  nacts == 0.                                            */
/*  With the fix:  nacts >= 1 and includes a BA_SEND ECHO.               */
/*------------------------------------------------------------------------*/

static void
testPostDecideContinuation(
  void
){
  struct bkr94acs *a;
  unsigned long sz;
  struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
  unsigned int nacts;
  unsigned int N;
  unsigned int k;
  unsigned int nEcho;
  unsigned char value;
  unsigned char encN;
  unsigned char t;

  printf("\nBKR94 ACS — Post-decide continuation regression\n");

  encN = 3;  /* actual N = 4 */
  t = 1;
  sz = bkr94acsSz(encN, 0, MAX_PHASES);
  a = (struct bkr94acs *)calloc(1, sz);
  if (!a) {
    check("alloc bkr94acs instance", 0);
    return;
  }
  bkr94acsInit(a, encN, t, 0, MAX_PHASES, 0, testCoin, 0);

  N = (unsigned int)encN + 1;

  /*
   * Simulate prior decide of BA_0: baDecision[0] is the (N+0)th byte
   * of a->data (entered[N] precedes baDecision[N]).  The decided
   * counts are derived by scanning baDecision[] at each decision
   * event, so this single write is the whole simulation -- no
   * parallel bookkeeping to keep consistent.
   */
  a->data[N + 0] = 1;

  /*
   * Feed a round-0 BA INITIAL for process 0 from process 1.
   * Fig1 Rule 1 must fire and output an ECHO action.  Pre-fix this
   * returned zero because of the "already decided" short-circuit.
   */
  value = 1;
  nacts = bkr94acsBaInput(a, 0, 0, 1, BRACHA87_INITIAL, 1, value, acts);
  check("post-decide input produces output", nacts > 0);

  nEcho = 0;
  for (k = 0; k < nacts; ++k) {
    if (acts[k].act == BKR94ACS_ACT_BA_SEND
     && acts[k].process == 0
     && acts[k].type == BRACHA87_ECHO)
      ++nEcho;
  }
  check("post-decide input outputs BA_SEND ECHO", nEcho >= 1);

  /*
   * Feed more messages to drive Fig3 round 0 to n-t validated, so
   * Fig4Round round 0 fires in the already-decided branch and
   * outputs a BROADCAST action for round 1 without DECIDE.  Deliver
   * INITIALs for enough initiators for Fig1 to accept via echoes
   * between them.  Easiest: INITIAL from every process for every
   * initiator — the simple all-to-all simulation pattern.
   */
  {
    unsigned char initiator;
    unsigned char from;
    unsigned char type;
    int hasPostDecideBroadcast;

    hasPostDecideBroadcast = 0;
    for (type = BRACHA87_INITIAL; type <= BRACHA87_READY; ++type) {
      for (initiator = 0; initiator < N; ++initiator) {
        for (from = 0; from < N; ++from) {
          unsigned int kk;

          nacts = bkr94acsBaInput(a, 0, 0, initiator,
                                         type, from, value, acts);
          for (kk = 0; kk < nacts; ++kk) {
            /*
             * A post-decide BA_SEND with round > 0 proves Fig4Round
             * fired in the already-decided else branch and output
             * a continuation broadcast.
             */
            if (acts[kk].act == BKR94ACS_ACT_BA_SEND
             && acts[kk].round > 0)
              hasPostDecideBroadcast = 1;
            /* BA_DECIDED must not re-fire for process 0 */
            if (acts[kk].act == BKR94ACS_ACT_BA_DECIDED
             && acts[kk].process == 0)
              check("no duplicate BA_DECIDED post-decide", 0);
          }
        }
      }
    }
    check("post-decide continuation outputs next-round BA_SEND",
          hasPostDecideBroadcast);
  }

  free(a);
  printf("  n=4 t=1 BA_0 pre-decided: continuation ok\n");
}

/*------------------------------------------------------------------------*/
/*  BKR94 Step 2 trigger regression test                                  */
/*                                                                        */
/*  Pre-fix, bkr94acsAcastInput counted Fig1 ACCEPTs and fired the     */
/*  enter-0 fanout when nAccepted reached n-t.  BKR94 Lemma 2 Part A       */
/*  case (i) requires the step-2 trigger to be "2t+1 BAs terminated with  */
/*  output 1", not "2t+1 Fig1 ACCEPTs" — these coincide only in benign    */
/*  runs and diverge under asynchrony or Byzantine scheduling.            */
/*                                                                        */
/*  This test pins the corrected semantics by driving all N Fig1          */
/*  instances to ACCEPT on a single process via bkr94acsAcastInput and    */
/*  asserting:                                                            */
/*    - BKR94ACS_F_THRESHOLD stays clear after each accept (step 2 not    */
/*      fired),                                                           */
/*    - no BKR94ACS_ACT_BA_SEND with baValue=0 comes out of the         */
/*      A-Cast path (no enter-0 fanout),                                 */
/*    - entered[j] == BKR94ACS_ENTER_ONE for every j (step 1 fired per       */
/*      accept).                                                          */
/*                                                                        */
/*  With the pre-fix code the (n-t)th accept would flip threshold to 1    */
/*  and output a burst of enter-0 BA_SEND actions.                          */
/*------------------------------------------------------------------------*/

static void
testStepTwoTrigger(
  void
){
  struct bkr94acs *a;
  unsigned long sz;
  struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
  unsigned int nacts;
  unsigned int k;
  unsigned int N;
  unsigned int process;
  unsigned int enterZeroSeen;
  unsigned int enterOneSeen;
  unsigned char encN;
  unsigned char t;
  unsigned char val;
  unsigned char from;
  const unsigned char *entered;

  printf("\nBKR94 ACS — Step 2 trigger regression\n");

  encN = 3;  /* actual N = 4, n-t threshold = 3 */
  t = 1;
  N = (unsigned int)encN + 1;
  val = 'x';

  sz = bkr94acsSz(encN, 0, MAX_PHASES);
  a = (struct bkr94acs *)calloc(1, sz);
  if (!a) {
    check("alloc bkr94acs instance", 0);
    return;
  }
  bkr94acsInit(a, encN, t, 0, MAX_PHASES, 0, testCoin, 0);

  enterZeroSeen = 0;
  enterOneSeen = 0;

  /*
   * Drive each Fig1 instance to ACCEPT on this process:
   *   INITIAL from process + ECHO from every process + READY from every
   *   process.  The READY cascade crosses 2t+1 inside
   *   bkr94acsAcastInput and fires BKR94ACS_ACT_BA_SEND
   *   (enter-1, step 1).
   */
  for (process = 0; process < N; ++process) {
    nacts = bkr94acsAcastInput(a, (unsigned char)process, BRACHA87_INITIAL,
                                  (unsigned char)process, &val, acts);
    for (k = 0; k < nacts; ++k)
      if (acts[k].act == BKR94ACS_ACT_BA_SEND) {
        if (acts[k].baValue == 0) ++enterZeroSeen;
        else                       ++enterOneSeen;
      }

    for (from = 0; from < N; ++from) {
      nacts = bkr94acsAcastInput(a, (unsigned char)process, BRACHA87_ECHO,
                                    from, &val, acts);
      for (k = 0; k < nacts; ++k)
        if (acts[k].act == BKR94ACS_ACT_BA_SEND) {
          if (acts[k].baValue == 0) ++enterZeroSeen;
          else                       ++enterOneSeen;
        }
    }

    for (from = 0; from < N; ++from) {
      nacts = bkr94acsAcastInput(a, (unsigned char)process, BRACHA87_READY,
                                    from, &val, acts);
      for (k = 0; k < nacts; ++k)
        if (acts[k].act == BKR94ACS_ACT_BA_SEND) {
          if (acts[k].baValue == 0) ++enterZeroSeen;
          else                       ++enterOneSeen;
        }
    }

    /*
     * Pre-fix, threshold flipped to 1 on the (n-t)th accept
     * (process == 2 here) and the enter-0 fanout fired for the one
     * still-un-entered process.
     */
    check("A-Cast accepts don't fire step 2",
          (a->flags & BKR94ACS_F_THRESHOLD) == 0);
  }

  check("no enter-0 output from A-Cast path", enterZeroSeen == 0);
  check("enter-1 output for every accepted process", enterOneSeen == N);

  /*
   * entered[] layout: first N bytes of a->data (see bkr94acsEnterd in
   * bkr94acs.c).  BKR94ACS_ENTER_ONE is the internal sentinel value 1.
   */
  entered = a->data;
  for (process = 0; process < N; ++process)
    check("entered[j] == ENTER_ONE after accept", entered[process] == 1);

  free(a);
  printf("  n=4 t=1: step 2 stays in BA path, step 1 fires per accept\n");
}

/*------------------------------------------------------------------------*/
/*  Main                                                                  */
/*------------------------------------------------------------------------*/

/*------------------------------------------------------------------------*/
/*  BPR retry tests                                                        */
/*                                                                        */
/*  White-box tests of bkr94acsAcast and bkr94acsRetry:                  */
/*    - A-Cast marks the local A-Cast Fig1 as process and outputs         */
/*      ACAST_SEND/INITIAL.                                                */
/*    - Retry keeps outputting ACAST_SEND/INITIAL on every sweep while        */
/*      F1_INITIATOR is set (Implementation Note 11); ACAST_SEND/ECHO and    */
/*      ACAST_SEND/READY join independently once F1_ECHOED / F1_RDSENT    */
/*      get set.                                                          */
/*    - Retry returns 0 on idle (no sent state anywhere).             */
/*    - Retry's per-process gate skips A-Cast Fig1s for BAs decided 0     */
/*      (post-Step 2 fanout, j excluded from SubSet).                     */
/*    - End-to-end: ACS converges under retry-only drive (no application   */
/*      bookkeeping, no per-record destination tracking) given fair-loss       */
/*      message delivery.                                                 */
/*------------------------------------------------------------------------*/

static void
testBpr(
  void
){
  struct bkr94acs *a;
  unsigned long sz;
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  unsigned char val[1];
  unsigned int n;
  unsigned int i;
  struct bracha87Retry retry;

  bracha87RetryInit(&retry);

  printf("\n  BPR retry tests:\n");

  /* n=4, t=1, vLen=1, maxPhases=4, self=0 */
  sz = bkr94acsSz(3, 0, 4);
  a = (struct bkr94acs *)calloc(1, sz);
  bkr94acsInit(a, 3, 1, 0, 4, 0, testCoin, 0);

  /* Retry on a virgin instance: no sent state -> idle */
  n = bkr94acsRetry(a, &retry, out);
  check("BPR retry virgin: 0 actions", n == 0);

  /* A-Cast: marks process, outputs ACAST_SEND/INITIAL once */
  val[0] = 1;
  n = bkr94acsAcast(a, val, out);
  check("BPR A-Cast: 1 action", n == 1);
  check("BPR A-Cast: ACAST_SEND/INITIAL", n >= 1
        && out[0].act == BKR94ACS_ACT_ACAST_SEND
        && out[0].type == BRACHA87_INITIAL);
  check("BPR A-Cast: process = self", n >= 1
        && out[0].process == 0);
  check("BPR A-Cast: act value pointer matches stored value",
        n >= 1 && out[0].value
        && out[0].value == bkr94acsAcastValue(a, 0)
        && out[0].value[0] == 1);

  /* Retry pre-loopback: retries ACAST_SEND/INITIAL every tick.
   * The cursor must visit the self A-Cast in finite calls; since
   * other processes' A-Cast Fig1s are unsent, the cursor walks
   * past them returning 0 -- but our walker keeps walking until it
   * finds retries or wraps, so one Retry call must surface our
   * ACAST_SEND/INITIAL. */
  for (i = 0; i < 5; ++i) {
    n = bkr94acsRetry(a, &retry, out);
    check("BPR retry pre-loopback: outputs", n >= 1);
    check("BPR retry pre-loopback: ACAST_SEND/INITIAL",
          n >= 1 && out[0].act == BKR94ACS_ACT_ACAST_SEND
          && out[0].type == BRACHA87_INITIAL);
    check("BPR retry pre-loopback: process = self",
          n >= 1 && out[0].process == 0);
  }

  /* Loopback our own INITIAL through AcastInput -> Rule 1 fires,
   * ECHOED is set on acastF1(0).  INITIAL retry does NOT stop here:
   * retiring at local ECHOED would strand a process that missed the
   * bootstrap (pitfall 11). */
  {
    struct bkr94acsAct iout[BKR94ACS_MAX_ACTS(4, 4)];
    bkr94acsAcastInput(a, 0, BRACHA87_INITIAL, 0, val, iout);
  }

  /* Post-loopback, the A-Cast Fig1 is INITIATOR+ECHOED but not yet
   * accepted and echoSenders < n, so INITIAL retry continues
   * alongside ECHO retry -- honest processes that missed the bootstrap
   * can still catch up.  The retry outputs both; verify the cursor
   * surfaces both for process = self.  (Both retire once self
   * accepts; this instance has not.) */
  {
    int seenInitial;
    int seenEcho;
    unsigned int sweep;
    unsigned int j;

    seenInitial = 0;
    seenEcho = 0;
    /* One full sweep of the cursor space; bound generously. */
    for (sweep = 0; sweep < 4 * (4 + 4 * 12 * 4); ++sweep) {
      n = bkr94acsRetry(a, &retry, out);
      if (!n)
        continue;
      for (j = 0; j < n; ++j) {
        if (out[j].act == BKR94ACS_ACT_ACAST_SEND && out[j].process == 0
         && out[j].type == BRACHA87_INITIAL)
          seenInitial = 1;
        if (out[j].act == BKR94ACS_ACT_ACAST_SEND && out[j].process == 0
         && out[j].type == BRACHA87_ECHO)
          seenEcho = 1;
      }
      if (seenInitial && seenEcho)
        break;
    }
    check("BPR retry post-loopback: INITIAL still retries",
          seenInitial);
    check("BPR retry post-loopback: ECHO retry also fires",
          seenEcho);
  }

  free(a);

  /*
   * Per-process retry gate: BA decided 0 -> skip A-Cast retry.
   * Synthesise the state by setting bkr94acsDecision[1] = 0
   * directly via the public-ish header layout.  We can't reach
   * the helper from outside; use a small driven path: drive ACS
   * forward to where BA_1 decides 0, then verify retry never
   * returns a PROP_* action for process 1.
   *
   * Cheaper synthetic: forge bkr94acsDecision[1] via observation
   * after a real run of testBasic-like setup with 3 processes
   * A-Casting and one not.  The detailed synthesis is deferred to
   * the end-to-end test below.
   */

  /*
   * End-to-end: retry-only drive with no application bookkeeping.  Construct a
   * 4-process simulation that bootstraps ONLY by A-Cast and then
   * relies on Retry to reach all processes under heavy A-Cast-
   * INITIAL drop (50% loss on the bootstrap broadcast; retry
   * makes up the difference).  Verify all four processes reach
   * complete with the same SubSet.
   *
   * This regression covers gap 4 (initiator INITIAL retry)
   * for the bkr94acs composition: without process INITIAL retry,
   * a 50%-drop bootstrap leaves at least one process permanently
   * unable to ACCEPT some process's A-Cast, and ACS stalls.
   */
  {
    struct bkr94acs *processes[4];
    struct bracha87Retry processRetry[4];
    struct acsResult results[4];
    const char *acasts[4] = {"a", "b", "c", "d"};
    unsigned int dropSeed;
    unsigned int totalSwept;
    unsigned int p;
    unsigned int q;

    sz = bkr94acsSz(3, 0, MAX_PHASES);
    for (p = 0; p < 4; ++p) {
      processes[p] = (struct bkr94acs *)calloc(1, sz);
      bkr94acsInit(processes[p], 3, 1, 0, MAX_PHASES, (unsigned char)p,
                   testCoin, 0);
      bracha87RetryInit(&processRetry[p]);
    }

    qInit();
    dropSeed = 0xDEADBEEFu;

    /* Bootstrap: each process A-Casts their value (binary 0 or 1
     * for this test; acasts[] strings reduce to first byte). */
    for (p = 0; p < 4; ++p) {
      struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
      unsigned int nAct;
      unsigned char acastVal;

      acastVal = (unsigned char)acasts[p][0];
      nAct = bkr94acsAcast(processes[p], &acastVal, iact);
      check("BPR e2e: A-Cast returned 1 action", nAct == 1);
      /* Push the A-Cast result to a random subset (50% loss) */
      for (q = 0; q < 4; ++q) {
        dropSeed = dropSeed * 1103515245u + 12345u;
        if (((dropSeed >> 16) & 1) == 0)
          continue;  /* dropped */
        qPush(BKR94ACS_CLS_ACAST, iact[0].process, 0, 0,
              BRACHA87_INITIAL, (unsigned char)p, (unsigned char)q,
              &acastVal, 1);
      }
    }

    /*
     * Drive the simulation: drain the queue, then retry every
     * process once and re-drain.  Loop until either all-complete
     * or a sweep produces no new traffic (idle threshold).
     */
    totalSwept = 0;
    for (;;) {
      int progress;

      /* Drain network */
      while (Qhead < Qtail) {
        struct msg *m;
        struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
        unsigned int nacts;
        unsigned int k;

        m = &MsgQ[Qhead++];
        if (m->cls == BKR94ACS_CLS_ACAST)
          nacts = bkr94acsAcastInput(processes[m->to], m->process,
                    m->type, m->from, m->value, acts);
        else
          nacts = bkr94acsBaInput(processes[m->to], m->process,
                    m->round, m->initiator, m->type, m->from,
                    m->value[0], acts);

        for (k = 0; k < nacts; ++k) {
          struct bkr94acsAct *act = &acts[k];

          switch (act->act) {
          case BKR94ACS_ACT_ACAST_SEND:
            if (!act->value) break;
            for (q = 0; q < 4; ++q)
              qPush(BKR94ACS_CLS_ACAST, act->process, 0, 0,
                    act->type, m->to, (unsigned char)q, act->value, 1);
            break;
          case BKR94ACS_ACT_BA_SEND:
            for (q = 0; q < 4; ++q)
              qPush(BKR94ACS_CLS_BA, act->process, act->round,
                    act->initiator, act->type, m->to,
                    (unsigned char)q, &act->baValue, 1);
            break;
          default:
            break;
          }
        }
      }

      /* Idle threshold check */
      progress = 0;
      for (p = 0; p < 4; ++p)
        if (!(processes[p]->flags & BKR94ACS_F_COMPLETE)) {
          progress = 1;
          break;
        }
      if (!progress)
        break;

      /* Retry every process */
      progress = 0;
      for (p = 0; p < 4; ++p) {
        struct bkr94acsAct pact[BKR94ACS_RETRY_MAX_ACTS];
        unsigned int npact;
        unsigned int k;

        npact = bkr94acsRetry(processes[p], &processRetry[p], pact);
        if (npact)
          progress = 1;
        for (k = 0; k < npact; ++k) {
          switch (pact[k].act) {
          case BKR94ACS_ACT_ACAST_SEND:
            if (!pact[k].value) break;
            for (q = 0; q < 4; ++q)
              qPush(BKR94ACS_CLS_ACAST, pact[k].process, 0, 0,
                    pact[k].type, (unsigned char)p, (unsigned char)q,
                    pact[k].value, 1);
            break;
          case BKR94ACS_ACT_BA_SEND:
            for (q = 0; q < 4; ++q)
              qPush(BKR94ACS_CLS_BA, pact[k].process, pact[k].round,
                    pact[k].initiator, pact[k].type,
                    (unsigned char)p, (unsigned char)q,
                    &pact[k].baValue, 1);
            break;
          default:
            break;
          }
        }
      }

      ++totalSwept;
      if (totalSwept > 10000) {
        check("BPR e2e: converged within budget", 0);
        break;
      }
      /* If queue is empty AND no process retryed, we're idle: terminate. */
      if (!progress && Qhead == Qtail)
        break;
    }

    for (p = 0; p < 4; ++p) {
      results[p].complete = (processes[p]->flags & BKR94ACS_F_COMPLETE) ? 1 : 0;
      results[p].subsetCnt = bkr94acsSubset(processes[p], results[p].subset);
    }

    check("BPR e2e: all processes complete", allComplete(results, 4));
    check("BPR e2e: all processes agree on subset", allAgree(results, 4));
    check("BPR e2e: subset size >= n-t", subsetValid(results, 4, 1));
    printf("    e2e retry-only drive  : 4 processes, 50%% INITIAL drop, |SubSet|=%u\n",
           results[0].subsetCnt);

    for (p = 0; p < 4; ++p)
      free(processes[p]);
  }
}

/*------------------------------------------------------------------------*/
/*  Retry cursor coverage white-box                                        */
/*                                                                        */
/*  Verifies the cursor visits every owned Fig1 instance with retry      */
/*  potential within a bounded number of calls.  Force several A-Cast   */
/*  Fig1s into INITIATOR+!ECHOED state via A-Cast-from-other-processes          */
/*  (pretend each process is the initiator); call Retry until either we've  */
/*  seen all expected processes or hit a generous budget.                  */
/*------------------------------------------------------------------------*/

static void
testBprCursorCoverage(
  void
){
  struct bkr94acs *processes[4];
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  struct bracha87Retry retry;
  unsigned char val[1];
  unsigned long sz;
  unsigned int p;
  unsigned int seen[4];
  unsigned int allSeen;
  unsigned int call;
  unsigned int budget;

  bracha87RetryInit(&retry);

  printf("\n  BPR retry cursor coverage:\n");

  sz = bkr94acsSz(3, 0, 4);
  for (p = 0; p < 4; ++p) {
    processes[p] = (struct bkr94acs *)calloc(1, sz);
    bkr94acsInit(processes[p], 3, 1, 0, 4, (unsigned char)p, testCoin, 0);
  }

  /* Each process A-Casts; their A-Cast Fig1 (process = self) becomes
   * INITIATOR+!ECHOED.  We sweep process 0's retry and verify it eventually
   * surfaces ACAST_SEND/INITIAL for process 0's own process.  Then we feed
   * process 0 INITIALs from processes 1, 2, 3 (driving Rule 1 on their
   * A-Cast Fig1s on process 0's instance), so process 0's view of those
   * A-Cast Fig1s becomes ECHOED (sent to ECHO retry).
   * After that, process 0's retry should cycle through all 4 processes'
   * A-Cast Fig1s on subsequent calls. */
  val[0] = 1;
  for (p = 0; p < 4; ++p) {
    struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, 4)];
    bkr94acsAcast(processes[p], val, iact);
  }

  /* Feed process 0 INITIALs from processes 1, 2, 3 so process 0's
   * acastF1(j) for j=1..3 fires Rule 1 (ECHOED).  Process 0's
   * acastF1(0) is already INITIATOR (via A-Cast). */
  for (p = 1; p < 4; ++p) {
    struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, 4)];
    bkr94acsAcastInput(processes[0], (unsigned char)p, BRACHA87_INITIAL,
                          (unsigned char)p, val, iact);
  }

  /* Now process 0 has 4 A-Cast Fig1s with retry potential:
   *   acastF1(0): INITIATOR+!ECHOED -> INITIAL_ALL retry
   *   acastF1(1..3): ECHOED -> ECHO_ALL retry
   * Sweep retry and assert all 4 processes surface in PROP_*. */
  memset(seen, 0, sizeof (seen));
  budget = 4 + 4 * 12 * 4 + 100;  /* one full cursor cycle + slack */
  for (call = 0; call < budget; ++call) {
    unsigned int n;

    n = bkr94acsRetry(processes[0], &retry, out);
    if (!n)
      continue;
    if (out[0].act == BKR94ACS_ACT_ACAST_SEND)
      seen[out[0].process] = 1;

    allSeen = 1;
    for (p = 0; p < 4; ++p)
      if (!seen[p]) {
        allSeen = 0;
        break;
      }
    if (allSeen)
      break;
  }
  for (p = 0; p < 4; ++p)
    check("BPR cursor coverage: process visited", seen[p]);
  printf("    cursor visited all 4 A-Cast Fig1s in %u retry calls\n",
         call + 1);

  for (p = 0; p < 4; ++p)
    free(processes[p]);
}

/*------------------------------------------------------------------------*/
/*  bkr94acsAcastAllEchoed white-box                                   */
/*                                                                        */
/*  Drives one process's view of process 0's A-Cast Fig1 with ECHO from     */
/*  every process BEFORE any READY, so the echo bitmap reaches n before any  */
/*  ACCEPT could freeze it.  The accessor must read 0 until the n-th      */
/*  distinct echo sender, 1 thereafter, and survive the post-accept       */
/*  freeze (it latched at n).  This is the application's retirement gate   */
/*  for an INITIAL-paired side channel (PSK / signature) -- it must NOT    */
/*  retire at the A-Cast's ACCEPTED.                                     */
/*------------------------------------------------------------------------*/

static void
testAcastAllEchoed(
  void
){
  struct bkr94acs *a;
  struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(4, 4)];
  unsigned char val[1];
  unsigned long sz;
  unsigned int from;

  printf("\n  bkr94acsAcastAllEchoed:\n");

  sz = bkr94acsSz(3, 0, 4);
  a = (struct bkr94acs *)calloc(1, sz);
  if (!a) {
    check("alloc bkr94acs instance", 0);
    return;
  }
  bkr94acsInit(a, 3, 1, 0, 4, 0, testCoin, 0);
  val[0] = 1;

  check("AllEchoed: NULL state -> 0", bkr94acsAcastAllEchoed(0, 0) == 0);
  check("AllEchoed: out-of-range process -> 0",
        bkr94acsAcastAllEchoed(a, 200) == 0);
  check("AllEchoed: fresh A-Cast -> 0", bkr94acsAcastAllEchoed(a, 0) == 0);

  bkr94acsAcastInput(a, 0, BRACHA87_INITIAL, 0, val, acts);
  /* ECHO from every process, no readys: echoSenders climbs to n with no
   * ACCEPT (which needs 2t+1 readys) to freeze it short. */
  for (from = 0; from < 4; ++from) {
    bkr94acsAcastInput(a, 0, BRACHA87_ECHO, (unsigned char)from, val, acts);
    check("AllEchoed: 1 exactly when echoSenders == n",
          bkr94acsAcastAllEchoed(a, 0) == (from == 3));
  }

  /* Drive to ACCEPT via readys; the latched all-echoed bit holds. */
  for (from = 0; from < 4; ++from)
    bkr94acsAcastInput(a, 0, BRACHA87_READY, (unsigned char)from, val, acts);
  check("AllEchoed: survives post-accept freeze",
        bkr94acsAcastAllEchoed(a, 0) == 1);
  printf("    0 until echoSenders==n, latched 1 across accept\n");

  free(a);
}

/*------------------------------------------------------------------------*/
/*  Retry-process gate white-box                                            */
/*                                                                        */
/*  The 3-rule decision in bkr94acs.dtc's BPR section: BA decided 0       */
/*  -> skip A-Cast Fig1 retry; BA undecided / decided 1 -> retry.       */
/*  Force decision[1] = 0 directly and verify Retry never returns          */
/*  PROP_* for process 1 across a full cursor sweep, while still           */
/*  surfacing retries for process 0 (undecided).                           */
/*------------------------------------------------------------------------*/

/* Internal layout helpers from bkr94acs.c needed for the white-box poke.
 * Walking past the data[] header to reach the per-process decision
 * byte: entered[N] precedes baDecision[N] (offsets 0 and N
 * respectively from a->data). */
static unsigned char *
testWriteDecision(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char value
){
  unsigned int N;
  unsigned char *baDec;

  N = (unsigned int)a->n + 1;
  baDec = a->data + N;
  baDec[process] = value;
  return (baDec);
}

static void
testBprProcessGate(
  void
){
  struct bkr94acs *a;
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  struct bracha87Retry retry;
  unsigned char val[1];
  unsigned long sz;
  unsigned int call;
  unsigned int budget;
  unsigned int process0Seen;
  unsigned int process1Seen;

  printf("\n  BPR retry-process gate:\n");

  sz = bkr94acsSz(3, 0, 4);
  a = (struct bkr94acs *)calloc(1, sz);
  bkr94acsInit(a, 3, 1, 0, 4, 0, testCoin, 0);
  bracha87RetryInit(&retry);

  /* Both A-Cast Fig1s for processes 0 and 1 in ECHOED state.
   * Process 0: A-Cast (INITIATOR+!ECHOED, then loopback -> ECHOED).
   * Process 1: feed INITIAL from process 1 (ECHOED via Rule 1). */
  val[0] = 1;
  {
    struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, 4)];
    bkr94acsAcast(a, val, iact);
    bkr94acsAcastInput(a, 0, BRACHA87_INITIAL, 0, val, iact);
    bkr94acsAcastInput(a, 1, BRACHA87_INITIAL, 1, val, iact);
  }

  /* Force BA decision: process 1 decided 0 (excluded), process 0
   * undecided.  Retry should walk past process 1 silently and surface
   * ACAST_SEND/ECHO for process 0. */
  testWriteDecision(a, 1, 0);
  /* Process 0: leave as undecided (0xFF, default from Init) */

  process0Seen = 0;
  process1Seen = 0;
  budget = 4 + 4 * 12 * 4 + 100;
  for (call = 0; call < budget; ++call) {
    unsigned int n;

    n = bkr94acsRetry(a, &retry, out);
    if (!n)
      continue;
    if (out[0].act == BKR94ACS_ACT_ACAST_SEND) {
      if (out[0].process == 0)
        ++process0Seen;
      if (out[0].process == 1)
        ++process1Seen;
    }
    /* Stop after enough process-0 surfaces (cursor cycled) */
    if (process0Seen >= 3)
      break;
  }
  check("BPR gate: process 0 (undecided) IS retryed", process0Seen >= 1);
  check("BPR gate: process 1 (decided 0) NOT retryed", process1Seen == 0);
  printf("    decided 0 process skipped (process0=%u, process1=%u over %u calls)\n",
         process0Seen, process1Seen, call + 1);

  /* Now flip process 1 to decided 1 (post-decide continuation).
   * Retry should retry process 1 (Bracha pitfall #1). */
  testWriteDecision(a, 1, 1);

  /* Reset cursor by calling Init?  No -- we want to test that the
   * gate change takes effect mid-flight.  Just sweep and watch. */
  process1Seen = 0;
  for (call = 0; call < budget; ++call) {
    unsigned int n;

    n = bkr94acsRetry(a, &retry, out);
    if (!n)
      continue;
    if (out[0].act == BKR94ACS_ACT_ACAST_SEND
     && out[0].process == 1)
      ++process1Seen;
    if (process1Seen >= 1)
      break;
  }
  check("BPR gate: process 1 (decided 1) IS retryed (post-decide)",
        process1Seen >= 1);
  printf("    decided 1 process retryed (post-decide continuation, pitfall #1)\n");

  free(a);
}

/*------------------------------------------------------------------------*/
/*  Byzantine retry test                                                   */
/*                                                                        */
/*  n=4 t=1.  Process 3 is byzantine: never sends its own messages           */
/*  (silent withhold).  The 3 honest processes (0, 1, 2) bootstrap with       */
/*  A-Cast and rely on each other's BPR + a small amount of input        */
/*  message processing to converge.  Drop rate moderate (25%).            */
/*  Verifies BPR retry + post-Step-2-fanout under t-byzantine.           */
/*------------------------------------------------------------------------*/

static void
testBprByzantineSilent(
  void
){
  struct bkr94acs *processes[4];
  struct bracha87Retry processRetry[4];
  struct acsResult results[4];
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  unsigned char val;
  unsigned long sz;
  unsigned int dropSeed;
  unsigned int totalSwept;
  unsigned int p;
  unsigned int q;

  printf("\n  BPR byzantine silent process:\n");

  sz = bkr94acsSz(3, 0, MAX_PHASES);
  /* Allocate honest processes + a byzantine slot for index symmetry */
  for (p = 0; p < 4; ++p) {
    processes[p] = (struct bkr94acs *)calloc(1, sz);
    bkr94acsInit(processes[p], 3, 1, 0, MAX_PHASES, (unsigned char)p,
                 testCoin, 0);
    bracha87RetryInit(&processRetry[p]);
  }

  qInit();
  dropSeed = 0xCAFEBABEu;

  /* Honest processes 0, 1, 2 A-Cast; process 3 (byzantine) never A-Casts
   * and never sends anything. */
  val = 1;
  for (p = 0; p < 3; ++p) {
    struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
    bkr94acsAcast(processes[p], &val, iact);
    for (q = 0; q < 4; ++q) {
      dropSeed = dropSeed * 1103515245u + 12345u;
      /* 12.5% drop (1/8): models lossy network on top of byzantine
       * silence.  At higher drop rates the simulator's bounded
       * MAX_MSGS queue fills with BPR retries faster than
       * actions are consumed, causing silent enqueue drops that
       * mask convergence -- a property of this test harness, not
       * BPR.  testBprHighDrop drives convergence under heavier
       * loss with no byzantine process so the volume stays bounded. */
      if (((dropSeed >> 16) & 7) == 0)
        continue;
      qPush(BKR94ACS_CLS_ACAST, (unsigned char)p, 0, 0,
            BRACHA87_INITIAL, (unsigned char)p, (unsigned char)q,
            &val, 1);
    }
  }

  /* Drive: drain queue (skipping any messages addressed to process 3
   * to model the byzantine process ignoring inputs), then retry
   * honest processes, repeat. */
  totalSwept = 0;
  for (;;) {
    int progress;

    /* Drain network */
    while (Qhead < Qtail) {
      struct msg *m;
      struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
      unsigned int nacts;
      unsigned int k;

      m = &MsgQ[Qhead++];
      /* Byzantine process 3 silently drops all inputs and produces
       * no outputs.  Processes 0, 1, 2 process normally. */
      if (m->to == 3)
        continue;

      if (m->cls == BKR94ACS_CLS_ACAST)
        nacts = bkr94acsAcastInput(processes[m->to], m->process,
                  m->type, m->from, m->value, acts);
      else
        nacts = bkr94acsBaInput(processes[m->to], m->process,
                  m->round, m->initiator, m->type, m->from,
                  m->value[0], acts);

      for (k = 0; k < nacts; ++k) {
        struct bkr94acsAct *act = &acts[k];

        switch (act->act) {
        case BKR94ACS_ACT_ACAST_SEND:
          if (!act->value) break;
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_ACAST, act->process, 0, 0,
                  act->type, m->to, (unsigned char)q, act->value, 1);
          break;
        case BKR94ACS_ACT_BA_SEND:
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_BA, act->process, act->round,
                  act->initiator, act->type, m->to,
                  (unsigned char)q, &act->baValue, 1);
          break;
        default:
          break;
        }
      }
    }

    /* Idle threshold on honest processes */
    progress = 0;
    for (p = 0; p < 3; ++p)
      if (!(processes[p]->flags & BKR94ACS_F_COMPLETE)) {
        progress = 1;
        break;
      }
    if (!progress)
      break;

    /* Retry honest processes */
    progress = 0;
    for (p = 0; p < 3; ++p) {
      unsigned int npact;
      unsigned int k;

      npact = bkr94acsRetry(processes[p], &processRetry[p], out);
      if (npact)
        progress = 1;
      for (k = 0; k < npact; ++k) {
        switch (out[k].act) {
        case BKR94ACS_ACT_ACAST_SEND:
          if (!out[k].value) break;
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_ACAST, out[k].process, 0, 0,
                  out[k].type, (unsigned char)p, (unsigned char)q,
                  out[k].value, 1);
          break;
        case BKR94ACS_ACT_BA_SEND:
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_BA, out[k].process, out[k].round,
                  out[k].initiator, out[k].type,
                  (unsigned char)p, (unsigned char)q,
                  &out[k].baValue, 1);
          break;
        default:
          break;
        }
      }
    }

    ++totalSwept;
    if (totalSwept > 500000) {
      check("BPR byzantine: converged within budget", 0);
      break;
    }
    if (!progress && Qhead == Qtail)
      break;
  }

  for (p = 0; p < 3; ++p) {
    results[p].complete = (processes[p]->flags & BKR94ACS_F_COMPLETE) ? 1 : 0;
    results[p].subsetCnt = bkr94acsSubset(processes[p], results[p].subset);
  }

  check("BPR byzantine: 3 honest processes complete",
        results[0].complete && results[1].complete && results[2].complete);
  check("BPR byzantine: honest processes agree on subset",
        results[0].subsetCnt == results[1].subsetCnt
        && results[1].subsetCnt == results[2].subsetCnt
        && memcmp(results[0].subset, results[1].subset,
                  results[0].subsetCnt) == 0
        && memcmp(results[1].subset, results[2].subset,
                  results[1].subsetCnt) == 0);
  /* Subset must contain all 3 honest processes (they all A-Cast
   * and reached Fig1 ACCEPT among the honest 3 -- 3 = n-t = 3
   * which satisfies BKR94 Lemma 2 Part A). */
  check("BPR byzantine: |subset| >= n-t = 3",
        results[0].subsetCnt >= 3);
  printf("    n=4 t=1 silent byzantine: 3 honest converged, |SubSet|=%u in %u sweeps\n",
         results[0].subsetCnt, totalSwept);

  for (p = 0; p < 4; ++p)
    free(processes[p]);
}

/*------------------------------------------------------------------------*/
/*  High-drop e2e                                                         */
/*                                                                        */
/*  Same harness as testBpr's e2e, drop rate parameterised.  At 90%       */
/*  drop the bootstrap broadcast nearly always loses; convergence         */
/*  must come from BPR retries.  Confirms BPR is sufficient under         */
/*  pathological loss.                                                    */
/*------------------------------------------------------------------------*/

static int
runRetryOnlyE2e(
  unsigned int dropMask
 ,unsigned int *sweepsOut
){
  struct bkr94acs *processes[4];
  struct bracha87Retry processRetry[4];
  struct acsResult results[4];
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  unsigned char val;
  unsigned long sz;
  unsigned int dropSeed;
  unsigned int totalSwept;
  unsigned int p;
  unsigned int q;
  int allOk;

  sz = bkr94acsSz(3, 0, MAX_PHASES);
  for (p = 0; p < 4; ++p) {
    processes[p] = (struct bkr94acs *)calloc(1, sz);
    bkr94acsInit(processes[p], 3, 1, 0, MAX_PHASES, (unsigned char)p,
                 testCoin, 0);
    bracha87RetryInit(&processRetry[p]);
  }

  qInit();
  dropSeed = 0x13371337u;

  val = 1;
  for (p = 0; p < 4; ++p) {
    struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
    bkr94acsAcast(processes[p], &val, iact);
    for (q = 0; q < 4; ++q) {
      dropSeed = dropSeed * 1103515245u + 12345u;
      /* dropMask is in 1/16ths: 14/16 = ~87.5%, 15/16 = ~93.75% */
      if (((dropSeed >> 16) & 0xF) < dropMask)
        continue;
      qPush(BKR94ACS_CLS_ACAST, (unsigned char)p, 0, 0,
            BRACHA87_INITIAL, (unsigned char)p, (unsigned char)q,
            &val, 1);
    }
  }

  totalSwept = 0;
  for (;;) {
    int progress;

    while (Qhead < Qtail) {
      struct msg *m;
      struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
      unsigned int nacts;
      unsigned int k;

      m = &MsgQ[Qhead++];
      if (m->cls == BKR94ACS_CLS_ACAST)
        nacts = bkr94acsAcastInput(processes[m->to], m->process,
                  m->type, m->from, m->value, acts);
      else
        nacts = bkr94acsBaInput(processes[m->to], m->process,
                  m->round, m->initiator, m->type, m->from,
                  m->value[0], acts);
      for (k = 0; k < nacts; ++k) {
        struct bkr94acsAct *act = &acts[k];
        switch (act->act) {
        case BKR94ACS_ACT_ACAST_SEND:
          if (!act->value) break;
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_ACAST, act->process, 0, 0,
                  act->type, m->to, (unsigned char)q, act->value, 1);
          break;
        case BKR94ACS_ACT_BA_SEND:
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_BA, act->process, act->round,
                  act->initiator, act->type, m->to,
                  (unsigned char)q, &act->baValue, 1);
          break;
        default:
          break;
        }
      }
    }

    progress = 0;
    for (p = 0; p < 4; ++p)
      if (!(processes[p]->flags & BKR94ACS_F_COMPLETE)) {
        progress = 1;
        break;
      }
    if (!progress)
      break;

    progress = 0;
    for (p = 0; p < 4; ++p) {
      unsigned int npact;
      unsigned int k;

      npact = bkr94acsRetry(processes[p], &processRetry[p], out);
      if (npact)
        progress = 1;
      for (k = 0; k < npact; ++k) {
        switch (out[k].act) {
        case BKR94ACS_ACT_ACAST_SEND:
          if (!out[k].value) break;
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_ACAST, out[k].process, 0, 0,
                  out[k].type, (unsigned char)p, (unsigned char)q,
                  out[k].value, 1);
          break;
        case BKR94ACS_ACT_BA_SEND:
          for (q = 0; q < 4; ++q)
            qPush(BKR94ACS_CLS_BA, out[k].process, out[k].round,
                  out[k].initiator, out[k].type,
                  (unsigned char)p, (unsigned char)q,
                  &out[k].baValue, 1);
          break;
        default:
          break;
        }
      }
    }

    ++totalSwept;
    if (totalSwept > 100000) {
      *sweepsOut = totalSwept;
      for (p = 0; p < 4; ++p)
        free(processes[p]);
      return (-1);
    }
    if (!progress && Qhead == Qtail)
      break;
  }

  for (p = 0; p < 4; ++p) {
    results[p].complete = (processes[p]->flags & BKR94ACS_F_COMPLETE) ? 1 : 0;
    results[p].subsetCnt = bkr94acsSubset(processes[p], results[p].subset);
  }

  allOk = allComplete(results, 4) && allAgree(results, 4)
       && subsetValid(results, 4, 1);
  *sweepsOut = totalSwept;

  for (p = 0; p < 4; ++p)
    free(processes[p]);
  return (allOk ? 0 : -1);
}

static void
testBprHighDrop(
  void
){
  unsigned int sweeps;
  int rc;

  printf("\n  BPR high-drop e2e:\n");

  /* 75% drop (12/16) */
  rc = runRetryOnlyE2e(12, &sweeps);
  check("BPR high-drop 75%: converged", rc == 0);
  printf("    n=4 t=1 75%% INITIAL drop: %s in %u sweeps\n",
         rc == 0 ? "converged" : "FAILED", sweeps);

  /* 87.5% drop (14/16) */
  rc = runRetryOnlyE2e(14, &sweeps);
  check("BPR high-drop 87.5%: converged", rc == 0);
  printf("    n=4 t=1 87.5%% INITIAL drop: %s in %u sweeps\n",
         rc == 0 ? "converged" : "FAILED", sweeps);
}

/*------------------------------------------------------------------------*/
/*  EXHAUSTED handling: drive one BA's Fig4 to BRACHA87_EXHAUSTED and     */
/*  verify the BKR94 layer surfaces it correctly.                         */
/*                                                                        */
/*  Setup: n=4, t=1, maxPhases=1, vLen=0, self=0.  We drive only one BA  */
/*  (process=0) directly via bkr94acsBaInput, bypassing the        */
/*  A-Cast layer.                                                       */
/*                                                                        */
/*  Per round, each of the 4 initiators' Fig1 is driven to ACCEPT at   */
/*  process 0 by feeding INITIAL + 3 distinct READYs (>= 2t+1=3 readys =>   */
/*  Bracha Rule 6 fires).  After the 3rd Fig1 ACCEPTs in a round,        */
/*  Fig3RoundComplete fires and Fig4Round runs.  The 4th ACCEPT adds a   */
/*  4th validation so that the next round's fig3IsValid call sees N(k-1) */
/*  permissive (cnt[0]=cnt[1]=2 in a 4-element set, both reachable in    */
/*  some n-t=3 subset), letting the next round's split values validate.  */
/*                                                                        */
/*  Values per round: (0, 0, 1, 1) across initiators (0,1,2,3).         */
/*    sub=0 (k=0): N case 0 with n_msgs=3 (cnt 0,0,1) => exact 0.        */
/*                 b->value := majority = 0.                             */
/*    sub=1 (k=1): N case 1 with n_msgs=3 (cnt 0,0,1) => no strict       */
/*                 majority => no D_FLAG, *result=0 permissive.          */
/*                 b->value unchanged (still 0).                         */
/*    sub=2 (k=2): n_msgs=3, dc[0]=dc[1]=0 (no D_FLAG flagged messages   */
/*                 because no process set d in sub=1) => gt2T=gtT=0,        */
/*                 n2Half=0 => coin path.  b->value := coin(0).          */
/*                 !decideV && !haveDecided && ph+1=1 >= maxPhases=1     */
/*                 => return BRACHA87_EXHAUSTED.                         */
/*                                                                        */
/*  Expectation: BKR94ACS_ACT_BA_EXHAUSTED for process=0 fires exactly    */
/*  once; baDecision[0] becomes 0xFE; complete stays 0; subsequent       */
/*  inputs do not retry EXHAUSTED.                                     */
/*------------------------------------------------------------------------*/

static unsigned int
feedFig1Accept(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char value
 ,struct bkr94acsAct *out
 ,unsigned int *exhaustedSeen
){
  unsigned int total;
  unsigned int n;
  unsigned int k;
  unsigned char sender;

  total = 0;
  /* INITIAL from initiator: process 0 echoes (Rule 1) */
  n = bkr94acsBaInput(a, process, round, initiator,
                             BRACHA87_INITIAL, initiator, value, out);
  for (k = 0; k < n; ++k)
    if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED && out[k].process == process)
      ++*exhaustedSeen;
  total += n;

  /*
   * Three distinct READYs.  rdCnt sequence: 1, 2, 3.
   *   sender=1: rdCnt=1, no rule fires.
   *   sender=2: rdCnt=2, Rule 5 fires (echoed && !rdsent && rd>=t+1=2).
   *   sender=3: rdCnt=3, Rule 6 fires (rd>=2t+1=3) => ACCEPT, cascade.
   */
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

static void
testExhausted(
  void
){
  unsigned long sz;
  struct bkr94acs *a;
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PROCESSES, 1)];
  unsigned int round;
  unsigned int b;
  unsigned int exhaustedSeen;
  unsigned int n;
  unsigned int k;

  printf("\n  EXHAUSTED handling:\n");

  sz = bkr94acsSz(3, 0, 1);  /* n=4 (encoded 3), vLen=1 (encoded 0), maxPhases=1 */
  a = (struct bkr94acs *)calloc(1, sz);
  if (!a) {
    check("testExhausted alloc", 0);
    return;
  }
  bkr94acsInit(a, 3, 1, 0, 1, 0, testCoin, 0);

  exhaustedSeen = 0;
  for (round = 0; round < 3; ++round)
    for (b = 0; b < 4; ++b)
      feedFig1Accept(a, 0, (unsigned char)round, (unsigned char)b,
                     (b < 2) ? 0 : 1, out, &exhaustedSeen);

  check("EXHAUSTED action output exactly once",
        exhaustedSeen == 1);
  check("baDecision[0] == 0xFE (exhausted sentinel)",
        bkr94acsBaDecision(a, 0) == 0xFE);
  check("BKR94ACS_F_COMPLETE remains clear after EXHAUSTED",
        (a->flags & BKR94ACS_F_COMPLETE) == 0);

  /*
   * After EXHAUSTED, the per-process retry gate keeps retrying
   * (0xFE != 0), and additional BA inputs for this process
   * must not retry EXHAUSTED.  Drive any further input and check.
   */
  n = bkr94acsBaInput(a, 0, 0, 0, BRACHA87_READY, 0, 0, out);
  for (k = 0; k < n; ++k)
    if (out[k].act == BKR94ACS_ACT_BA_EXHAUSTED)
      ++exhaustedSeen;
  check("no duplicate EXHAUSTED on subsequent input",
        exhaustedSeen == 1);

  printf("    EXHAUSTED at single-phase BA: output %u time(s); "
         "baDecision=0x%02X; complete=%u\n",
         exhaustedSeen,
         (unsigned)bkr94acsBaDecision(a, 0),
         (unsigned)((a->flags & BKR94ACS_F_COMPLETE) ? 1 : 0));

  free(a);
}

/*
 * White-box: bkr94acsAcastValue ACCEPT-gate transition for a
 * non-self process.
 *
 * Pre-Rule-1: returns NULL (no flags set).
 * Post-Rule-1 (ECHOED only, no ACCEPT yet): returns NULL.  This is
 *   the regression for the recent .c tightening — pre-change the
 *   function returned the ECHOED-stored value, exposing potentially
 *   Byzantine-equivocated bytes that Bracha Lemma 2 doesn't protect.
 * Post-Rule-6 (ACCEPT): returns the accepted value.
 *
 * For self-process: INITIATOR bit (set by A-Cast) gates non-null even
 * before ACCEPT, per the header's "or, for self-process, not yet
 * A-Cast" carve-out.
 */
static void
testAcastValueGate(
  void
){
  struct bkr94acs *a;
  unsigned long sz;
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
  unsigned char val;
  unsigned char sender;

  printf("\n  bkr94acsAcastValue ACCEPT-gate:\n");

  /* n=4, t=1, vLen=1, self=0 */
  sz = bkr94acsSz(3, 0, MAX_PHASES);
  a = (struct bkr94acs *)calloc(1, sz);
  if (!a) {
    check("AcastValueGate alloc", 0);
    return;
  }
  bkr94acsInit(a, 3, 1, 0, MAX_PHASES, 0, testCoin, 0);

  /* Pre-input: no flags set on any Fig1 → AcastValue returns NULL
   * for both self and non-self processes. */
  check("AcastValueGate: pre-input non-self returns NULL",
        bkr94acsAcastValue(a, 1) == 0);
  check("AcastValueGate: pre-A-Cast self returns NULL",
        bkr94acsAcastValue(a, 0) == 0);

  /* Feed INITIAL from process 1 to process 0 for process=1.  Rule 1 fires:
   * process 0's Fig1 initiator=1 sets ECHOED, outputs ECHO_ALL.  ACCEPTED is
   * NOT yet set (no ready cascade).
   *
   * Pre-tightening (.c bug): AcastValue returned the ECHOED-
   * stored 0xC1 here, exposing pre-Lemma-2 bytes to callers.
   * Post-tightening (current): returns NULL until ACCEPT. */
  val = 0xC1;
  bkr94acsAcastInput(a, /*process=*/1, BRACHA87_INITIAL,
                        /*from=*/1, &val, out);

  check("AcastValueGate: ECHOED-only non-self returns NULL "
        "(post-tightening regression)",
        bkr94acsAcastValue(a, 1) == 0);

  /* Drive Rule 5 then Rule 6 by feeding 3 distinct READY/v messages.
   * Rule 5 (echoed && rd>=t+1=2): sender 2 trips it, READY output.
   * Rule 6 (rdsent && rd>=2t+1=3): sender 3 trips it, ACCEPT. */
  for (sender = 1; sender <= 3; ++sender)
    bkr94acsAcastInput(a, /*process=*/1, BRACHA87_READY,
                          sender, &val, out);

  check("AcastValueGate: post-ACCEPT non-self returns value",
        bkr94acsAcastValue(a, 1) != 0);
  if (bkr94acsAcastValue(a, 1))
    check("AcastValueGate: post-ACCEPT value bytes match",
          bkr94acsAcastValue(a, 1)[0] == 0xC1);

  /* Self-process (INITIATOR-bit carve-out): pre-A-Cast returned NULL
   * above; post-A-Cast returns the value before any ACCEPT. */
  val = 0xA0;
  bkr94acsAcast(a, &val, out);

  check("AcastValueGate: post-A-Cast self returns value (INITIATOR bit)",
        bkr94acsAcastValue(a, 0) != 0);
  if (bkr94acsAcastValue(a, 0))
    check("AcastValueGate: post-A-Cast self value matches",
          bkr94acsAcastValue(a, 0)[0] == 0xA0);

  free(a);
}

/* Bit test on a returned suppress mask (process j skipped iff bit j set). */

/*
 * Sweep the retry up to a full cursor cycle; capture the first ACAST_SEND
 * READY for 'process' (its skip mask + accepted flag).  Returns 1 if seen.
 */
static int
findPropReady(
  struct bkr94acs *a
 ,struct bracha87Retry *retry
 ,unsigned char process
 ,const unsigned char **skipOut
 ,int *acceptedOut
){
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  unsigned int call;
  unsigned int budget;

  budget = 4 + 4 * 12 * 4 + 50;   /* > one full sweep (N + N*mr*N) */
  for (call = 0; call < budget; ++call) {
    unsigned int n;
    unsigned int k;

    n = bkr94acsRetry(a, retry, out);
    for (k = 0; k < n; ++k)
      if (out[k].act == BKR94ACS_ACT_ACAST_SEND
       && out[k].process == process
       && out[k].type == BRACHA87_READY) {
        *skipOut = out[k].skip;
        *acceptedOut = out[k].accepted;
        return (1);
      }
  }
  return (0);
}

/*
 * Per-process BPR suppression at the bkr94acs layer: the ACCEPTED wire bit,
 * the egress .skip / .accepted fields on retry actions, the ingress
 * accept setters, and all-accepted READY quiescence.  n=4 t=1, self=0.
 */
static void
testBprSkipAccept(
  void
){
  struct bkr94acs *a;
  struct bkr94acsAct iact[BKR94ACS_MAX_ACTS(4, 4)];
  struct bracha87Retry retry;
  unsigned char val[1];
  unsigned long sz;
  const unsigned char *skip;
  int accepted;
  int seen;

  printf("\n  BPR per-process skip / ACCEPTED wire bit:\n");
  check("ACCEPTED wire bit value is 0x10", BKR94ACS_ACCEPTED == 0x10);

  sz = bkr94acsSz(3, 0, 4);
  a = (struct bkr94acs *)calloc(1, sz);
  bkr94acsInit(a, 3, 1, 0, 4, 0, testCoin, 0);   /* self = 0 */
  bracha87RetryInit(&retry);

  val[0] = 1;
  /* Process 2's A-Cast to RDSENT (NOT accepted): INITIAL + 2 readys. */
  bkr94acsAcastInput(a, 2, BRACHA87_INITIAL, 2, val, iact);
  bkr94acsAcastInput(a, 2, BRACHA87_READY, 1, val, iact);
  bkr94acsAcastInput(a, 2, BRACHA87_READY, 3, val, iact);

  /* Egress before accept: READY carries a non-null skip mask, accepted=0. */
  skip = 0; accepted = -1;
  seen = findPropReady(a, &retry, 2, &skip, &accepted);
  check("Egress: process-2 A-Cast READY surfaced", seen);
  check("Egress: READY carries non-null skip mask", skip != 0);
  check("Egress: READY accepted=0 before accept", accepted == 0);

  /* Ingress: process 1 announces accept of process-2 A-Cast; process 3 does
   * not.  The next READY skip marks 1 only -- a single process's accept
   * touches only its own bit (byzantine-false-accept is thus contained). */
  bkr94acsAcastAccepted(a, 2, 1);
  skip = 0; accepted = -1;
  seen = findPropReady(a, &retry, 2, &skip, &accepted);
  check("Ingress: READY still output (not all accepted)", seen);
  check("Ingress: skip marks process 1 (announced accept)",
        skip && BRACHA87_SKIP_TST(skip, 1));
  check("Ingress: skip leaves process 3 clear (no accept announced)",
        skip && !BRACHA87_SKIP_TST(skip, 3));

  /* Drive process-2 A-Cast to ACCEPT (3rd ready): self-accept is
   * recorded, so egress .accepted flips to 1 and self's skip bit sets. */
  bkr94acsAcastInput(a, 2, BRACHA87_READY, 0, val, iact);
  skip = 0; accepted = -1;
  seen = findPropReady(a, &retry, 2, &skip, &accepted);
  check("Egress: process-2 READY still surfaced post-accept", seen);
  check("Egress: READY accepted=1 after self-accept", accepted == 1);
  check("Egress: skip marks self (index 0) after self-accept",
        skip && BRACHA87_SKIP_TST(skip, 0));

  /* Quiescence: mark the remaining processes (2, 3) accepted.  With self(0)
   * + 1 + 2 + 3 all accepted, process-2's A-Cast READY retires. */
  bkr94acsAcastAccepted(a, 2, 2);
  bkr94acsAcastAccepted(a, 2, 3);
  skip = 0; accepted = -1;
  seen = findPropReady(a, &retry, 2, &skip, &accepted);
  check("Quiescence: process-2 A-Cast READY retired at all-accepted",
        !seen);
  printf("    egress skip/accepted, ingress setters, all-accepted quiescence\n");
  free(a);
}


/*
 * Implementation Pitfall 17 (Note 14): an INITIAL must come from the
 * instance's designated initiator (process for acasts, initiator
 * for BA).  A non-initiator INITIAL is a forged broadcast and
 * must be dropped — otherwise Rule 1 echoes it and the (n+t)/2+1 echo
 * cascade carries a value the correct process never sent to a false
 * ACCEPT.  Honest generators never produce this (from == process even
 * for an equivocating process), so it needs an explicit injection.
 * n=4, t=1, self=2.
 */
static void
testForgedInitial(
  void
){
  struct bkr94acs *a;
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(4, MAX_PHASES)];
  unsigned long sz;
  unsigned char v;
  unsigned int n;
  unsigned char from;

  printf("\n  Forged INITIAL rejection (pitfall 17):\n");

  sz = bkr94acsSz(3, 0, MAX_PHASES);
  a = (struct bkr94acs *)calloc(1, sz);
  if (!a) {
    check("ForgedInitial alloc", 0);
    return;
  }
  bkr94acsInit(a, 3, 1, 0, MAX_PHASES, /*self=*/2, testCoin, 0);

  /* Forged A-Cast INITIAL for process 0, sent by Byzantine process 1. */
  v = 0x55;
  n = bkr94acsAcastInput(a, /*process=*/0, BRACHA87_INITIAL,
                            /*from=*/1, &v, out);
  check("forged A-Cast INITIAL (from!=process) outputs no action", n == 0);

  /*
   * Drive the forged value at EVERY process index: if the from!=process
   * drop were absent, four forged INITIALs would echo 0x55 and the
   * cascade could ACCEPT it.  With the drop, the BA stays pristine.
   */
  for (from = 0; from <= 3; ++from)
    if (from != 0)
      (void)bkr94acsAcastInput(a, 0, BRACHA87_INITIAL, from, &v, out);
  check("forged INITIAL flood leaves A-Cast Fig1 unsent",
        bkr94acsAcastValue(a, 0) == 0);

  /* The honest process's own INITIAL still works (from == process). */
  v = 0x55;
  n = bkr94acsAcastInput(a, /*process=*/0, BRACHA87_INITIAL,
                            /*from=*/0, &v, out);
  check("honest A-Cast INITIAL (from==process) outputs ECHO", n == 1);
  check("honest A-Cast INITIAL action is ACAST_SEND/ECHO",
        n == 1 && out[0].act == BKR94ACS_ACT_ACAST_SEND
              && out[0].type == BRACHA87_ECHO);

  /* BA path: forged INITIAL with from != initiator is dropped;
   * the legitimate initiator's INITIAL is accepted. */
  n = bkr94acsBaInput(a, /*process=*/0, /*round=*/0, /*initiator=*/1,
                             BRACHA87_INITIAL, /*from=*/3, /*value=*/1, out);
  check("forged BA INITIAL (from!=initiator) outputs no action",
        n == 0);
  n = bkr94acsBaInput(a, /*process=*/0, /*round=*/0, /*initiator=*/1,
                             BRACHA87_INITIAL, /*from=*/1, /*value=*/1, out);
  check("legitimate BA INITIAL (from==initiator) outputs ECHO",
        n == 1 && out[0].act == BKR94ACS_ACT_BA_SEND
              && out[0].type == BRACHA87_ECHO);

  free(a);
}

int
main(
  void
){
  printf("bkr94acs test suite\n");
  printf("===================\n\n");

  MsgQ = (struct msg *)calloc(MAX_MSGS, sizeof (struct msg));
  if (!MsgQ) {
    fprintf(stderr, "message queue allocation failed\n");
    return (1);
  }

  testBasic();
  testShuffled();
  testValues();
  testMultiByteValues();
  testIdenticalAcasts();
  testLargerN();
  testPostDecideContinuation();
  testStepTwoTrigger();
  testBpr();
  testBprCursorCoverage();
  testAcastAllEchoed();
  testBprProcessGate();
  testBprSkipAccept();
  testBprByzantineSilent();
  testBprHighDrop();
  testExhausted();
  testAcastValueGate();
  testForgedInitial();

  free(MsgQ);

  printf("\n===================\n");
  if (Fail) {
    printf("%d FAILED\n", Fail);
    return (1);
  }
  printf("ALL PASSED\n");
  return (0);
}
