/*
 * test_ceiling.c -- the 256-process ceiling.
 *
 * Process indices are a byte, so the largest cohort is 256, and every
 * count in this library that can reach the process count must hold
 * 256 -- one past a byte.  A count narrowed to a byte wraps to 0 on
 * the 256th arrival, so whatever reads it reads 0 at this size and
 * the true count at every smaller one; the rest of the battery runs
 * at 37 processes and below, where such a narrowing is invisible.
 * This suite runs every such count to 256 and requires what reads it
 * to hold there; a gate that closes at exactly n must be open at 255
 * and closed at 256.
 *
 * n = 256, t = 85 (the largest t with n > 3t), binary values.  The
 * counts, and where each is read:
 *
 *   Fig 1  echo senders == n        INITIAL retire, bracha87Fig1AllEchoed
 *          accepted senders == n    READY retire (bracha87Fig1Bpr)
 *   Fig 2  received count           bracha87Fig2RecvCount
 *   Fig 3  VALID^k count            bracha87Fig3ValidCount, the round
 *                                   k+1 gate that reads it, at both the
 *                                   direct and the cascade increment
 *   Fig 4  the per-value tallies    bracha87Fig4Round, steps 1-3, and
 *                                   the N that validates the next round
 *   ACS    unentered, BA-output-1   bkr94acsFanoutDuty
 *          validated == n           bkr94acsTurnDuty MET
 *          decided == n             BKR94ACS_ACT_COMPLETE
 *          the act bound            bkr94acsFanout entering all n
 *
 * The ACS half drives one instance's 256 BAs to decision through
 * bkr94acsBaInput and bkr94acsTurn -- 256 BAs, 3 rounds, 256
 * initiators, 2t+1 READYs each -- so the composition's counts are
 * reached through the library's own path, never written into the
 * state.
 *
 * Value 1 throughout: a wrapped step-1 tally reads as a tie, and the
 * tie-break is 0, so 1 is the value a wrapped count cannot reproduce.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bkr94acs.h"

/* The one turn drain here is `while (bkr94acsTurn(...) > 0 &&
 * turnDrained())`: a drain ends because the turn advances or refuses,
 * and a machine that emitted acts without advancing would spin it.
 * Counted against a ceiling no correct run approaches; abort past it,
 * announced, never a silent hang. */
#define TURN_CALL_CAP (1u << 24)
static unsigned long TurnCalls = 0;

static int
turnDrained(
  void
){
  if (++TurnCalls > TURN_CALL_CAP) {
    fprintf(stderr, "FATAL: turn drain runaway -- bkr94acsTurn returned"
            " acts %lu times\n", TurnCalls);
    abort();
  }
  return (1);
}

#define N_ENC 255   /* actual process count = 256 */
#define N_ACT 256
#define T     85    /* 256 > 3 * 85 */

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

static unsigned char
coin(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
  return (phase % 2);
}

/* Fig 3's N for the bare Fig 3 arm: value 1 is exact at every round. */
static int
nOne(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  (void)closure;
  (void)k;
  (void)n_msgs;
  (void)senders;
  (void)values;
  *result = 1;
  return (0);
}

int
main(
  int argc
 ,char *argv[]
){
  struct bracha87Fig1 *f1;
  struct bracha87Fig2 *f2;
  struct bracha87Fig3 *f3;
  struct bracha87Fig4 *f4;
  struct bkr94acs *a;
  struct bkr94acsAct *out;
  struct bkr94acsAct tout[3];
  unsigned char acts[3];
  unsigned char values[N_ACT];
  unsigned char senders[N_ACT];
  unsigned char subset[N_ACT];
  unsigned char value;
  unsigned long sz;
  unsigned int nact;
  unsigned int cnt;
  unsigned int i;
  unsigned int j;
  unsigned int p;
  unsigned int r;
  unsigned int b;
  unsigned int s;
  unsigned int complete;
  unsigned int decided;
  unsigned int short1;
  unsigned int met;
  unsigned int tolerance;
  unsigned int held;
  unsigned int seen;

  (void)argc;
  (void)argv;

  printf("test_ceiling: every count against the process count, at 256\n");

  /*
   * Fig 1.  An initiator whose INITIAL retires on the 256th echo
   * sender and whose READY retires on the 256th accept.
   */
  sz = bracha87Fig1Sz(N_ENC, 0);
  check("Fig1: Sz admits 256", sz != 0);
  if (!(f1 = calloc(1, sz))) {
    fprintf(stderr, "test_ceiling: allocation failed\n");
    return (2);
  }
  check("Fig1: Init admits 256, t=85", bracha87Fig1Init(f1, N_ENC, T, 0));
  value = 1;
  bracha87Fig1Initiator(f1, &value);

  for (s = 0; s < N_ACT; ++s) {
    if (s == N_ACT - 1) {
      nact = bracha87Fig1Bpr(f1, acts);
      seen = 0;
      for (i = 0; i < nact; ++i)
        if (acts[i] == BRACHA87_INITIAL_ALL)
          seen = 1;
      check("Fig1: INITIAL still owed at 255 echo senders", seen);
      check("Fig1: not all echoed at 255", !bracha87Fig1AllEchoed(f1));
    }
    (void)bracha87Fig1Input(f1, BRACHA87_ECHO, s, &value, 0, 1,
             acts);
  }
  check("Fig1: all echoed at 256", bracha87Fig1AllEchoed(f1));
  nact = bracha87Fig1Bpr(f1, acts);
  seen = 0;
  for (i = 0; i < nact; ++i)
    if (acts[i] == BRACHA87_INITIAL_ALL)
      seen = 1;
  check("Fig1: INITIAL retired at 256 echo senders", !seen);

  seen = 0;
  for (s = 0; s < N_ACT; ++s) {
    nact = bracha87Fig1Input(f1, BRACHA87_READY, s, &value, 0, 1,
             acts);
    for (i = 0; i < nact; ++i)
      if (acts[i] == BRACHA87_ACCEPT)
        seen = s + 1;
  }
  check("Fig1: accepted on the 2t+1'th ready", seen == 2 * T + 1);

  for (s = 0; s < N_ACT; ++s) {
    if (s == N_ACT - 1) {
      nact = bracha87Fig1Bpr(f1, acts);
      check("Fig1: READY still owed at 255 accepted",
            nact == 1 && acts[0] == BRACHA87_READY_ALL);
    }
    bracha87Fig1ProcessAccepted(f1, s);
  }
  nact = bracha87Fig1Bpr(f1, acts);
  check("Fig1: quiescent at 256 accepted", nact == 0);
  free(f1);

  /*
   * Fig 2.  One round received from all 256 senders.
   */
  sz = bracha87Fig2Sz(N_ENC, 1);
  check("Fig2: Sz admits 256", sz != 0);
  if (!(f2 = calloc(1, sz))) {
    fprintf(stderr, "test_ceiling: allocation failed\n");
    return (2);
  }
  check("Fig2: Init admits 256, t=85", bracha87Fig2Init(f2, N_ENC, T, 1));
  seen = 0;
  cnt = 0;
  for (s = 0; s < N_ACT; ++s)
    if (bracha87Fig2Receive(f2, 0, s, 1)
     == BRACHA87_ROUND_COMPLETE) {
      seen = s + 1;
      ++cnt;
    }
  check("Fig2: ROUND_COMPLETE on the n-t'th receipt, once",
        seen == N_ACT - T && cnt == 1);
  check("Fig2: received count 256", bracha87Fig2RecvCount(f2, 0) == N_ACT);
  check("Fig2: 256 received messages",
        bracha87Fig2GetReceived(f2, 0, senders, values) == N_ACT);
  free(f2);

  /*
   * Fig 3.  Round 1 stored from all 256 senders before round 0 has
   * validated any, so the cascade validates them -- its own increment
   * -- when round 0 crosses n - t; round 0 is then validated directly
   * from all 256; a round-2 message validates over the cascaded
   * round 1.
   */
  sz = bracha87Fig3Sz(N_ENC, 3);
  check("Fig3: Sz admits 256", sz != 0);
  if (!(f3 = calloc(1, sz))) {
    fprintf(stderr, "test_ceiling: allocation failed\n");
    return (2);
  }
  check("Fig3: Init admits 256, t=85",
        bracha87Fig3Init(f3, N_ENC, T, 3, nOne, 0));
  cnt = 0;
  for (s = 0; s < N_ACT; ++s)
    cnt += bracha87Fig3Accept(f3, 1, s, 1, 0);
  check("Fig3: round 1 stored unvalidated ahead of round 0", cnt == 0);
  cnt = 0;
  for (s = 0; s < N_ACT; ++s)
    if (bracha87Fig3Accept(f3, 0, s, 1, 0) == BRACHA87_VALIDATED)
      ++cnt;
  check("Fig3: 256 validated at round 0", cnt == N_ACT);
  check("Fig3: valid count 256", bracha87Fig3ValidCount(f3, 0) == N_ACT);
  check("Fig3: 256 valid messages",
        bracha87Fig3GetValid(f3, 0, senders, values) == N_ACT);
  check("Fig3: round 0 complete", bracha87Fig3RoundComplete(f3, 0));
  check("Fig3: cascade valid count 256",
        bracha87Fig3ValidCount(f3, 1) == N_ACT);
  check("Fig3: cascaded round 1 complete", bracha87Fig3RoundComplete(f3, 1));
  check("Fig3: round 2 validates over a cascaded round 1",
        bracha87Fig3Accept(f3, 2, 0, 1, 0) == BRACHA87_VALIDATED);
  free(f3);

  /*
   * Fig 4.  Three rounds, each computed over all 256 values.
   */
  sz = bracha87Fig4Sz(N_ENC, 1);
  check("Fig4: Sz admits 256", sz != 0);
  if (!(f4 = calloc(1, sz))) {
    fprintf(stderr, "test_ceiling: allocation failed\n");
    return (2);
  }
  check("Fig4: Init admits 256, t=85",
        bracha87Fig4Init(f4, N_ENC, T, 1, 1, 0, coin, 0));
  memset(values, 1, sizeof (values));
  check("Fig4: step 1 majority over 256",
        (bracha87Fig4Round(f4, 0, N_ACT, values) & BRACHA87_BROADCAST)
     && f4->value == 1);
  check("Fig4: step 2 (d, v) over 256",
        (bracha87Fig4Round(f4, 1, N_ACT, values) & BRACHA87_BROADCAST)
     && f4->value == (1 | BRACHA87_D_FLAG));
  memset(values, 1 | BRACHA87_D_FLAG, sizeof (values));
  check("Fig4: step 3 decides over 256",
        (bracha87Fig4Round(f4, 2, N_ACT, values) & BRACHA87_DECIDE)
     && f4->decision == 1);
  free(f4);

  /*
   * ACS.  One instance, its 256 BAs driven to decision 1.
   */
  sz = bkr94acsSz(N_ENC, 0, 1);
  check("ACS: Sz admits 256", sz != 0);
  if (!(a = calloc(1, sz))
   || !(out = calloc(BKR94ACS_MAX_ACTS(N_ENC), sizeof (*out)))) {
    fprintf(stderr, "test_ceiling: allocation failed\n");
    return (2);
  }
  check("ACS: Init admits 256, t=85",
        bkr94acsInit(a, N_ENC, T, 0, 1, 0, coin, 0));
  check("ACS: fanout HELD with 256 unentered",
        bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_HELD);

  complete = 0;
  decided = 0;
  short1 = 0;
  met = 0;
  tolerance = 0;
  held = 0;
  for (p = 0; p < N_ACT; ++p) {
    for (r = 0; r < 3; ++r) {
      value = r < 2 ? 1 : (1 | BRACHA87_D_FLAG);
      for (b = 0; b < N_ACT; ++b) {
        if (b == N_ACT - 1
         && bkr94acsTurnDuty(a, p) == BKR94ACS_DUTY_TOLERANCE)
          ++short1;
        (void)bkr94acsBaInput(a, p, r, b, BRACHA87_INITIAL,
               BKR94ACS_RECEIVED, b, value, out);
        for (s = 0; s <= 2 * T; ++s)
          (void)bkr94acsBaInput(a, p, r, b, BRACHA87_READY,
                 BKR94ACS_RECEIVED, s, value, out);
      }
      switch (bkr94acsTurnDuty(a, p)) {
      case BKR94ACS_DUTY_MET:
        ++met;
        break;
      case BKR94ACS_DUTY_TOLERANCE:
        ++tolerance;
        break;
      default:
        ++held;
        break;
      }
      if (p == N_ACT - 1 && r == 2)
        check("ACS: not complete at 255 decided", !a->complete);
      while ((nact = bkr94acsTurn(a, p, tout)) > 0 && turnDrained())
        for (j = 0; j < nact; ++j) {
          if (tout[j].act == BKR94ACS_ACT_BA_DECIDED && tout[j].baValue == 1)
            ++decided;
          if (tout[j].act == BKR94ACS_ACT_COMPLETE)
            ++complete;
        }
    }
  }
  check("ACS: every round TOLERANCE at 255 validated", short1 == 3 * N_ACT);
  check("ACS: every turn MET on 256 validated",
        met == 3 * N_ACT && !tolerance && !held);
  check("ACS: 256 BAs decided 1", decided == N_ACT);
  check("ACS: COMPLETE on the 256th decision, once",
        complete == 1 && a->complete);
  check("ACS: subset of 256", bkr94acsSubset(a, subset) == N_ACT);
  check("ACS: fanout TOLERANCE on 256 BA outputs of 1",
        bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_TOLERANCE);
  check("ACS: fanout enters all 256", bkr94acsFanout(a, out) == N_ACT);
  check("ACS: fanout MET with none unentered",
        bkr94acsFanoutDuty(a) == BKR94ACS_DUTY_MET);
  printf("  ACS: rounds TOLERANCE at 255 %u; turns MET %u TOLERANCE %u"
         " HELD %u; decided %u; COMPLETE %u\n",
         short1, met, tolerance, held, decided, complete);
  free(out);
  free(a);

  printf("test_ceiling: %s\n", Fail ? "FAILED" : "PASSED");
  return (Fail ? 1 : 0);
}
