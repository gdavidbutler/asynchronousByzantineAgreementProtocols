/*
 * asynchronousByzantineAgreementProtocols - BKR94 ACS seat shadow test
 * Copyright (C) 2026 G. David Butler <gdb@dbSystems.com>
 *
 * This file is part of asynchronousByzantineAgreementProtocols
 *
 * asynchronousByzantineAgreementProtocols is free software: you can
 * redistribute it and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * asynchronousByzantineAgreementProtocols is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * One seat, fed whatever an adversary can feed in whatever order: the
 * value broadcast of any j, fed to its accept, which is Q(j); INITIAL,
 * ECHO and READY for any (ba, q, k) from any sender with any of the
 * four values, duplicates, sweep steps, and refused arguments.  A shadow records what was fed and holds what the rows
 * say the seat holds:
 *
 *   Figure 1, per instance: a mirror instance of the proven library
 *     fed the same messages, which says what the seat's instance
 *     accepted and what it sends;
 *   Figure 3, per (ba, k): the validated set, closed under the
 *     validate row -- accepted, and at round 1 a value of 0 or 1, above
 *     round 1 the witness over the shadow's counts of round k - 1 and,
 *     where N keeps value_p, the sender's own validated message there;
 *   Figure 4, per BA: the round the seat is at, read off its own
 *     broadcasts, value_p by N over the shadow's counts, the decision,
 *     and whether the decider has broadcast its last phase;
 *   Agreement[Q]: inputs entered, decisions of 1, the subset, and
 *     the values accepted, which the subset's output waits for.
 *
 * In the random seeds every READY is fed with the two annotations the
 * adversary chooses, to the seat and to its mirror alike; the scenes
 * feed none.  The seat's instance must say what the mirror says: its
 * Received, and after a sweep step that re-sent READY, its Recipients.
 *
 * Every return is checked against the rows read as statements about
 * that record: the Figure 1 sends equal the mirror's; a broadcast at
 * the next round comes exactly when the turn row is due and the
 * continue row allows, carrying N; DECIDED comes exactly at more than
 * 2t of (d, v) on a 3i+3 turn, once; entries and the subset come
 * exactly when Agreement[Q]'s rows say; ABANDON comes exactly on the
 * pass at the abandon count or an undecided BA's turn past the last
 * round, last, with silence after; the accessors say what the record
 * says; a refused argument changes nothing.
 *
 * Two checks are weaker than the rows, stated here: a turn's N and a
 * decision are judged over the counts after the call, so a value that
 * N yields over the counts before the call is also allowed, since the
 * turn fires inside the call at the first count that satisfies it;
 * and a validation is seen only through the turn it enables.
 *
 * Sixteen seeds are scripted scenes before the random ones, built to
 * reach the states a random feed seldom does.  Two stand alone: a
 * (d, v) whose witness is short of more than n/2 by one, which must not
 * be validated; and, at n = 5, t = 1, where n - t is not 2t + 1, three
 * decisions of 1 that must enter no 0 and a fourth that must.  Nine
 * more are at n = 4, t = 1 and again at n = 7, t = 2: a 3i+3 turn with more than t and not more
 * than 2t of (d, 1) validated, which adopts and must not decide, and
 * whose round after validates on that count; a 3i+1 round whose
 * witness is reachable by the coin alone, plain values short of n - t
 * with a (d, x) below t filling in; n - t BAs decided 1 with the
 * rest's Q unknown, so step 2 enters 0 only after the patience; three
 * BAs' rounds stored whole before the seat learns their Q, so each
 * entry's turn chains through them at once and the third decision
 * enters 0 to the fourth BA in the same call, which then decides 1 on
 * the others' rounds, so the subset waits for a value this seat does
 * not hold and is output on its accept; a BA that reaches
 * phase 2 undecided, rounds 7 and 8, on coins and ties; and a ceiling
 * abandon inside the acs rows with an entry due after it, which must
 * not be made.  Three more at n = 4, t = 1: a 3i+2 turn on a sample
 * of n - t whose two 0s are more than half the sample and not more
 * than n/2, which keeps value_p; rounds 3 and 2 stored whole before
 * round 1, so the walk must carry round 1's validations through to
 * the decision at round 3; and a decided BA's extra phase under
 * patience, which must turn on the pass that fills it.  Two more for
 * the witness where N keeps value_p: at n = 4, t = 1, a plain 1 at
 * round 3 from a sender whose round 2 carried 0, which must not
 * validate, and a plain 0 from a sender whose round 2 arrives after
 * it, which the walk validates on that arrival, turning the round; and
 * at n = 5, t = 1, a round-1 tie, where a plain 1 at round 2 from a
 * sender whose round 1 carried 0 must not validate until a late
 * round-1 message makes 1 a majority, when the walk validates it and
 * the round turns.
 *
 * Every fourth random seed runs the seat with its beside flag set: a
 * value's step 1 on its (initial, v) then waits for the caller's
 * BESIDE.  The BESIDE is fed after the (initial, v) for the even
 * players and never for the odd, which echo nothing on the initial and
 * accept on the others' readies, as the figure prints.  Every fourth
 * player is two-faced: its (initial, v) here carries another value
 * than the one every ready carries, with the BESIDE for it, and the
 * seat accepts the readied value on the others' word, as Theorem 1
 * says.  A BESIDE ahead of the (initial, v) is
 * refused under the flag, and any BESIDE with it clear, as an
 * (initial, v) of a value from another sender is under either, and
 * none makes the instance.
 *
 * usage: test_acsShadow seeds [first]    (seeds first..seeds; the
 *        scenes are seeds 1..16)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87Fig1.h"
#include "bkr94acs.h"

#define MAXN 7
#define MAXR 9
#define VLEN 2

/* the shadow's own event kinds, beside the arrival kinds 0..2 */
#define EVQ     4   /* make Q(ba) = 1 known: feed ba's value broadcast to its accept */
#define EVSWEEP 5   /* a sweep step */

static unsigned long CoinCalls;
static unsigned char CoinValue;

static unsigned char
coin(
  void *ctx
){
  ++CoinCalls;
  return (*(unsigned char *)ctx);
}

/* the shadow's per-BA state */
struct sev {
  unsigned char kind;   /* an arrival kind, EVQ, or EVSWEEP for a sweep step */
  unsigned char ba;
  unsigned char q;
  unsigned char k;
  unsigned char from;
  unsigned char val;
  unsigned char ann;    /* the annotations on a READY */
};

struct sba {
  unsigned long roundSweeps;
  unsigned int round;        /* read off the seat's own broadcasts; 0 = none */
  unsigned int decidedRound;
  unsigned int valueP;
  unsigned int decided;      /* 0, 1, 2 none */
  unsigned int input;
  unsigned int qKnown;
};

int
main(
  int argc
 ,char *argv[]
){
  struct bkr94acs *a;
  struct bkr94acsAct *out;
  struct bracha87Fig1 *mir;   /* MAXN * MAXR * MAXN mirror instances */
  struct bracha87Fig1 *mirV;  /* MAXN mirrors of the value broadcasts */
  unsigned long szV;
  unsigned char vex[MAXN];    /* the value broadcasts that exist */
  unsigned int held;          /* the value of every j that decided 1 is accepted */
  unsigned char *exists;      /* per (ba, k, q) */
  unsigned char *valid;       /* per (ba, k, q) */
  struct sba ba[MAXN];
  unsigned long sz1;
  unsigned long seeds;
  unsigned long first;
  unsigned long seed;
  unsigned long s;
  unsigned long calls;
  unsigned long refused;
  unsigned long turns;
  unsigned long decisions;
  unsigned long subsets;
  unsigned long abandons;
  unsigned long coins;
  unsigned long coinsBefore;
  unsigned long witQ;        /* validations by the "?" path */
  unsigned long witCoin;     /* by the coin path */
  unsigned long witTie;      /* by a tie */
  unsigned long sweeps;
  unsigned long onesAt;
  unsigned long patience;
  unsigned long abandon;
  unsigned int n;
  unsigned int t;
  unsigned int maxRounds;
  unsigned int initRounds;   /* as given to Init, which takes it up to a multiple of 3 */
  unsigned int self;
  unsigned int ones;
  unsigned int completed;
  unsigned int subsetOut;
  unsigned int abandoned;
  unsigned int outMax;
  unsigned int nout;
  unsigned int ev;
  unsigned int kind;
  unsigned int i;
  unsigned int j;
  unsigned int k;
  unsigned int q;
  unsigned int from;
  unsigned int val;
  unsigned int ann;
  unsigned int w;
  unsigned int x;
  unsigned int sawAbandon;
  unsigned int sawSubset;
  unsigned int passDone;
  unsigned int passExp;
  unsigned int cb;           /* the shadow's cursor, walked as the seat walks its own */
  unsigned int ck;
  unsigned int cq;
  unsigned int vb;           /* the instance this sweep step visited */
  unsigned int vk;
  unsigned int vq;
  unsigned int visited;
  unsigned int cnt[MAXN][MAXR + 1][5];     /* validated, then per value */
  unsigned int pcnt[MAXN][MAXR + 1][5];    /* the same before the call */
  unsigned int sawInitial[MAXN][MAXR + 2]; /* value + 1 of a broadcast seen this call */
  unsigned int sawDecided[MAXN];
  unsigned int sawEcho;
  unsigned int sawReady;
  unsigned int sawInitRe;
  unsigned int rank;         /* the header's order of actions in one return */
  unsigned int changed;      /* the entries-and-chain pass placed something */
  unsigned int ceiling;      /* an undecided BA's turn is due past the last round */
  unsigned int ceilingBa;    /* the first such BA */
  unsigned char consumed[MAXN];  /* an entry seen this call has been placed */
  unsigned int beside;       /* the seat's beside flag this seed */
  unsigned char sendVal[3];  /* the value each of this call's sends carried */
  unsigned char mirVal[3];
  unsigned char mirGot[3];
  unsigned int mirEcho;
  unsigned int mirReady;
  unsigned int mirInitRe;
  unsigned char o1[BRACHA87_FIG1_OUT];
  unsigned char rcvBefore[(MAXN + 7) / 8];  /* Received before a refused input */
  unsigned char v8;
  unsigned char firstVal[MAXN][MAXR + 1][MAXN];  /* value + 1 first fed per instance */
  struct sev script[3200];
  unsigned int scriptLen;
  unsigned int scripted;
  unsigned int evCount;
  unsigned char qPending[MAXN];  /* Q(j) to feed first, for a seat that knows every Q */
  int rc;
  int more;

#define RND (seed = seed * 6364136223846793005UL + 1442695040888963407UL, (unsigned int)(seed >> 33))
#define MIR(b, kk, qq) ((struct bracha87Fig1 *)((unsigned char *)mir + (((b) * MAXR + (kk) - 1) * MAXN + (qq)) * sz1))
#define MIRV(b) ((struct bracha87Fig1 *)((unsigned char *)mirV + (b) * szV))
#define MIRX(b, kk, qq) ((kk) ? MIR(b, kk, qq) : MIRV(b))
#define EX(b, kk, qq) exists[((b) * MAXR + (kk) - 1) * MAXN + (qq)]
#define VA(b, kk, qq) valid[((b) * MAXR + (kk) - 1) * MAXN + (qq)]
#define MINU(p, r) ((p) < (r) ? (p) : (r))
/* the sender's own validated message of (b, kk) carries v */
#define OWN(b, kk, qq, v) (VA(b, kk, qq) && *bracha87Fig1Value(MIR(b, kk, qq), BRACHA87_ACCEPT) == (v))
#define FAIL(msg) do { printf("seed %lu ev %u: %s\n", s, ev, msg); goto done; } while (0)

  if (argc != 2 && argc != 3) {
    fprintf(stderr, "usage: %s seeds [first]\n", argv[0]);
    return (1);
  }
  seeds = atol(argv[1]);
  first = argc == 3 ? atol(argv[2]) : 1;
  rc = 1;
  calls = refused = turns = decisions = subsets = abandons = 0;
  coins = witQ = witCoin = witTie = 0;
  sz1 = (bracha87Fig1Sz(MAXN - 1, 0, 3) + 7) & ~7UL;
  szV = (bracha87Fig1Sz(MAXN - 1, VLEN - 1, (MAXN - 1) / 3 + 1) + 7) & ~7UL;
  mirV = calloc(MAXN, szV);
  a = calloc(1, bkr94acsSz(MAXN - 1, VLEN - 1, MAXR));
  out = calloc(MAXN * (MAXR + 1) + 8, sizeof (*out));
  mir = calloc(MAXN * MAXR * MAXN, sz1);
  exists = calloc(MAXN * MAXR * MAXN, 1);
  valid = calloc(MAXN * MAXR * MAXN, 1);
  if (!a || !out || !mir || !mirV || !exists || !valid) {
    printf("allocation failed\n");
    goto done;
  }

  for (s = first; s <= seeds; ++s) {
    seed = s * 2654435761UL + 99991;
    n = RND & 1 ? 4 + RND % (MAXN - 3) : RND % MAXN + 1;
    t = n > 3 ? RND % ((n - 1) / 3 + 1) : 0;
    maxRounds = 3 + RND % (MAXR - 2);
    self = RND % n;
    patience = RND % 3;
    abandon = patience + 1 + RND % 12;
    CoinValue = RND & 1;
    CoinCalls = 0;
    scripted = s <= 16;
    beside = !scripted && s % 4 == 1;
    scriptLen = 0;
#define PUSH(K, B, Q, KK, F, V) do { if (scriptLen < sizeof (script) / sizeof (script[0])) { \
    script[scriptLen].kind = (K); script[scriptLen].ba = (B); script[scriptLen].q = (Q); \
    script[scriptLen].k = (KK); script[scriptLen].from = (F); script[scriptLen].val = (V); \
    script[scriptLen].ann = 0; ++scriptLen; } } while (0)
    /* one broadcaster's message at a round, as the figure delivers it:
     * its INITIAL, then everyone's echo and ready of it */
#define FILL(B, KK, Q, V) do { unsigned int ff_; PUSH(BKR94ACS_INITIAL, B, Q, KK, Q, V); \
    for (ff_ = 0; ff_ < n; ++ff_) PUSH(BKR94ACS_ECHO, B, Q, KK, ff_, V); \
    for (ff_ = 0; ff_ < n; ++ff_) PUSH(BKR94ACS_READY, B, Q, KK, ff_, V); } while (0)
    if (scripted) {
      n = s == 11 || s == 16 ? 5 : s <= 3 || s == 7 || s == 8 || s == 10 || s >= 12 ? 4 : 7;
      t = s <= 3 || s == 7 || s == 8 || s >= 10 ? 1 : 2;
      self = 0;
      maxRounds = s == 14 ? 9 : s >= 15 ? 6 : s % 3 == 0 ? 3 : 6;
      patience = s == 3 ? 2 : s == 6 ? 3 : s == 14 ? 1 : 0;
      abandon = 20;
      CoinValue = 1;
      if (s == 12) {
        /* a 3i+2 turn on a sample of n - t with two 0s: two is more than
         * half the sample and not more than n/2, so N keeps value_p and
         * does not go to (d, 0) */
        FILL(0, 1, 1, 0);
        FILL(0, 1, 2, 1);
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);        /* the turn at three: majority 1 */
        FILL(0, 1, 3, 0);
        FILL(0, 2, 1, 0);
        FILL(0, 2, 2, 0);
        FILL(0, 2, 3, 1);        /* the turn at three: two 0s, keep 1 */
      } else if (s == 13) {
        /* rounds 3 and 2 stored whole before round 1: the walk from
         * round 1's validations must reach round 3, where the (d, 1)s
         * decide */
        for (x = 1; x < n; ++x)
          FILL(0, 3, x, 3);
        for (x = 1; x < n; ++x)
          FILL(0, 2, x, 1);
        PUSH(EVQ, 0, 0, 0, 0, 0);
        for (x = 1; x < n; ++x)
          FILL(0, 1, x, 1);
      } else if (s == 14) {
        /* a decided BA's extra phase under patience: round 4 reaches
         * n - t and must turn on the pass that fills the patience, as
         * an undecided round would */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        for (k = 1; k <= 3; ++k)
          for (x = 0; x < n; ++x)
            FILL(0, k, x, k == 3 ? 3 : 1);
        for (x = 0; x < n - t; ++x)
          FILL(0, 4, x, 1);
        for (x = 0; x < 100; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 15) {
        /* the witness where N keeps value_p, at 3i+3: a plain value
         * validates only as the value its sender broadcast at 3i+2.
         * Round 2 keeps 1 at the turn.  At round 3 this seat's plain 1
         * and sender 1's plain 1 validate, sender 2's plain 1 must not,
         * since its round 2 carried 0, and sender 3's plain 0 is stored
         * unvalidated until its round 2 arrives carrying 0, when the
         * walk validates it, the round turns at three and the coin
         * gives the round-4 broadcast */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 0);        /* the turn at three: majority 1 */
        FILL(0, 1, 3, 0);
        FILL(0, 2, 0, 1);
        FILL(0, 2, 1, 1);
        FILL(0, 2, 2, 0);        /* the turn at three: no value above n/2, keep 1 */
        FILL(0, 3, 0, 1);        /* this seat's plain 1: its round 2 carried 1 */
        FILL(0, 3, 1, 1);        /* sender 1's plain 1: its round 2 carried 1 */
        FILL(0, 3, 2, 1);        /* sender 2's plain 1: its round 2 carried 0, not validated */
        FILL(0, 3, 3, 0);        /* sender 3's plain 0: no round 2 from it yet, stored */
        FILL(0, 2, 3, 0);        /* sender 3's round 2 carrying 0: the walk validates
                                  * its round 3, the turn at three, no (d, x): coin */
        for (x = 0; x < 60; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 16) {
        /* the witness where N keeps value_p, at 3i+2 on a tie: round 1
         * turns at four split 2-2 and keeps 1.  At round 2 this seat's
         * plain 1, sender 1's plain 1 and sender 3's plain 0 validate,
         * each the value its sender carried at round 1; sender 2's
         * plain 1 must not, since its round 1 carried 0 and 1 is only
         * half of any four.  Round 2 holds three, no turn, until
         * sender 4's round 1 arrives carrying 1: 1 is then a majority
         * of VALID^1, the walk validates sender 2's stored message, the
         * round turns at four with three 1s above n/2 and this seat
         * broadcasts (d, 1) */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 0);
        FILL(0, 1, 3, 0);        /* the turn at four: a tie, keep 1 */
        FILL(0, 2, 0, 1);        /* this seat's plain 1: its round 1 carried 1 */
        FILL(0, 2, 1, 1);        /* sender 1's plain 1: its round 1 carried 1 */
        FILL(0, 2, 3, 0);        /* sender 3's plain 0: its round 1 carried 0 */
        FILL(0, 2, 2, 1);        /* sender 2's plain 1: its round 1 carried 0, not validated */
        FILL(0, 1, 4, 1);        /* sender 4's round 1: 1 is a majority of VALID^1, the
                                  * walk validates sender 2's round 2, the turn at four: (d, 1) */
        for (x = 0; x < 60; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 10) {
        /* a (d, 1) no correct process could have sent: round 2 holds
         * two 1s and a 0, not more than n/2 of 1, so the (d, 1) at
         * round 3 is not validated and round 3 stays at two, short of
         * the turn.  A witness read against n - t would validate it
         * and turn */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 0);        /* the turn at three: majority 1 */
        FILL(0, 1, 3, 0);
        FILL(0, 2, 0, 1);
        FILL(0, 2, 1, 1);
        FILL(0, 2, 2, 0);        /* the turn at three: no value above n/2, keep 1 */
        FILL(0, 3, 0, 1);        /* this seat's plain 1, "?" reachable */
        FILL(0, 3, 1, 3);        /* (d, 1): two 1s in round 2 are not more than n/2 */
        FILL(0, 3, 2, 0);        /* plain: two validated, no turn */
        for (x = 0; x < 60; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 11) {
        /* above n = 3t + 1, where n - t is not 2t + 1: at n = 5, t = 1
         * three BAs decide 1 and the passes go by with no 0 entered,
         * since three is 2t + 1 and short of n - t; the fourth decides
         * and the 0 goes to the fifth */
        for (j = 0; j < 4; ++j) {
          for (x = 1; x < n; ++x)
            FILL(j, 1, x, 1);
          for (x = 1; x < n; ++x)
            FILL(j, 2, x, 1);
          for (x = 1; x < n; ++x)
            FILL(j, 3, x, 3);
        }
        PUSH(EVQ, 0, 0, 0, 0, 0);
        PUSH(EVQ, 1, 0, 0, 0, 0);
        PUSH(EVQ, 2, 0, 0, 0, 0);
        for (x = 0; x < 150; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
        PUSH(EVQ, 3, 0, 0, 0, 0);
        for (x = 0; x < 150; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 4) {
        /* adopt, not decide, at t = 2: three (d, 1) at the 3i+3 turn */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 1);
        FILL(0, 1, 3, 0);
        FILL(0, 1, 4, 0);        /* the turn at five: majority 1 */
        FILL(0, 1, 5, 0);
        FILL(0, 1, 6, 0);        /* four zeros: a zero validates at round 2 */
        FILL(0, 2, 0, 1);
        FILL(0, 2, 1, 1);
        FILL(0, 2, 2, 1);
        FILL(0, 2, 3, 1);
        FILL(0, 2, 4, 0);        /* the turn at five: four ones above n/2 -> (d, 1) */
        FILL(0, 2, 5, 0);
        FILL(0, 2, 6, 0);        /* round 2: four 1s, three 0s; "?" reachable */
        FILL(0, 3, 4, 0);        /* plain, "?" */
        FILL(0, 3, 5, 0);
        FILL(0, 3, 0, 3);        /* this seat's (d, 1) */
        FILL(0, 3, 1, 3);
        FILL(0, 3, 2, 3);        /* the turn at five: three (d, 1), adopt 1, no decision */
        for (x = 0; x < n; ++x)
          FILL(0, 4, x, 1);      /* witness: three (d, 1), more than t and not more than 2t */
      } else if (s == 5) {
        /* the coin alone reaches the witness, at t = 2 */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 1);
        FILL(0, 1, 3, 0);
        FILL(0, 1, 4, 0);        /* the turn at five: majority 1 */
        FILL(0, 1, 5, 0);
        FILL(0, 1, 6, 0);
        FILL(0, 2, 1, 0);
        FILL(0, 2, 2, 0);
        FILL(0, 2, 3, 0);
        FILL(0, 2, 4, 0);
        FILL(0, 2, 0, 1);        /* the turn at five: four zeros above n/2 -> (d, 0) */
        FILL(0, 2, 5, 1);
        FILL(0, 2, 6, 1);        /* round 2: four 0s, three 1s */
        FILL(0, 3, 3, 0);        /* plain, "?" */
        FILL(0, 3, 4, 0);
        FILL(0, 3, 5, 1);
        FILL(0, 3, 6, 1);
        FILL(0, 3, 0, 2);        /* this seat's (d, 0): one, not more than t -> coin */
        for (x = 0; x < n; ++x)
          FILL(0, 4, x, x ? x & 1 : CoinValue);  /* witness only by the coin */
      } else if (s == 6 || s == 3) {
        /* n - t BAs decide 1, the rest's Q unknown: 0 entered only past
         * the patience.  n - t broadcasters per round suffice, and this
         * seat's own later rounds are fed after the passes that turn
         * it, since a message about its broadcast before its step 0 is
         * dropped */
        for (j = 1; j <= n - t; ++j)
          PUSH(EVQ, j, 0, 0, 0, 0);
        if (s == 6)
          PUSH(EVQ, 6, 0, 0, 0, 0);   /* a sixth, deciding late */
        for (j = 1; j <= n - t; ++j) {
          for (x = 0; x < n - t; ++x)
            FILL(j, 1, x, 1);
          for (x = 1; x < n - t; ++x)
            FILL(j, 2, x, 1);
          for (x = 1; x < n - t; ++x)
            FILL(j, 3, x, 3);
        }
        /* three passes: a pass is one step per existing instance and one more */
        /* passes enough for the patience: a pass is one step per existing
         * instance and one more */
        for (x = 0; x < (patience + 1) * ((n - t) * (3 * (n - t) - 2) + (n - t) + (s == 6) + 1); ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
        for (j = 1; j <= n - t; ++j)
          FILL(j, 2, 0, 1);      /* this seat's round 2, after its turn */
        for (x = 0; x < (patience + 1) * ((n - t) * (3 * (n - t) - 1) + (n - t) + (s == 6) + 1); ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
        for (j = 1; j <= n - t; ++j)
          FILL(j, 3, 0, 3);      /* this seat's round 3 */
        if (s == 6) {
          /* the five decide at the pass that fills their patience; one
           * pass later the sixth BA arrives whole, every broadcaster, so
           * its turns are immediate and its 1 lands after the n - t'th
           * and before step 2's patience: that must not move it */
          for (x = 0; x < (patience + 1) * ((n - t) * (3 * (n - t)) + (n - t) + (s == 6) + 1); ++x)
            PUSH(EVSWEEP, 0, 0, 0, 0, 0);
          for (x = 0; x < n; ++x)
            FILL(6, 1, x, 1);
          for (x = 0; x < n; ++x)
            FILL(6, 2, x, 1);
          for (x = 0; x < n; ++x)
            FILL(6, 3, x, 3);
        }
        for (x = 0; x < 1200; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 9) {
        /* a ceiling abandon inside the acs rows, with an entry due after
         * it in the same pass: five BAs decide at once on stored rounds,
         * the sixth's stored rounds never decide and reach the ceiling,
         * the seventh's 0 then comes due and must not be entered */
        maxRounds = 3;
        patience = 0;
        for (j = 0; j < 5; ++j) {
          for (x = 1; x < n; ++x)
            FILL(j, 1, x, 1);
          for (x = 1; x < n; ++x)
            FILL(j, 2, x, 1);
          for (x = 1; x < n; ++x)
            FILL(j, 3, x, 3);
        }
        FILL(5, 1, 1, 1);
        FILL(5, 1, 2, 1);
        FILL(5, 1, 3, 1);
        FILL(5, 1, 4, 0);
        FILL(5, 1, 5, 0);
        FILL(5, 1, 6, 0);        /* this seat entered 0 and its own is not fed:
                                  * three 1s, three 0s, a tie, keep 0 */
        FILL(5, 2, 1, 1);
        FILL(5, 2, 2, 1);
        FILL(5, 2, 3, 0);
        FILL(5, 2, 4, 0);
        FILL(5, 2, 5, 0);
        FILL(5, 2, 6, 0);        /* four 0s above n/2: (d, 0) */
        FILL(5, 3, 1, 1);
        FILL(5, 3, 2, 1);
        FILL(5, 3, 3, 0);
        FILL(5, 3, 4, 0);
        FILL(5, 3, 5, 0);
        FILL(5, 3, 6, 0);        /* no (d, x): coin, and round 4 is past the ceiling */
        for (j = 0; j < 5; ++j)
          PUSH(EVQ, j, 0, 0, 0, 0);
        for (x = 0; x < 100; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 7) {
        /* three BAs' rounds stored whole before their Q is known: each
         * entry's turn chains through them in one call, and the third
         * decision enters 0 to the fourth BA in that same call */
        patience = 0;
        maxRounds = 6;
        for (j = 0; j < 3; ++j) {
          for (x = 1; x < n; ++x)
            FILL(j, 1, x, 1);
          for (x = 1; x < n; ++x)
            FILL(j, 2, x, 1);
          for (x = 1; x < n; ++x)
            FILL(j, 3, x, 3);
        }
        PUSH(EVQ, 0, 0, 0, 0, 0);
        PUSH(EVQ, 1, 0, 0, 0, 0);
        PUSH(EVQ, 2, 0, 0, 0, 0);
        /* the fourth BA then decides 1 on the others' rounds, this seat
         * having entered 0 without its value: every BA is decided and
         * the subset waits for that value, through passes, and is
         * output on its accept */
        for (x = 1; x < n; ++x)
          FILL(3, 1, x, 1);
        for (x = 1; x < n; ++x)
          FILL(3, 2, x, 1);
        for (x = 1; x < n; ++x)
          FILL(3, 3, x, 3);
        for (x = 0; x < 60; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
        PUSH(EVQ, 3, 0, 0, 0, 0);
        for (x = 0; x < 200; ++x)
          PUSH(EVSWEEP, 0, 0, 0, 0, 0);
      } else if (s == 8) {
        /* phase 2 reached undecided: rounds of ties and coins */
        patience = 0;
        maxRounds = 9;
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 0);        /* the turn at three: majority 1 */
        FILL(0, 1, 3, 0);
        FILL(0, 2, 0, 1);
        FILL(0, 2, 1, 1);
        FILL(0, 2, 2, 0);        /* no value above n/2: keep */
        FILL(0, 2, 3, 0);
        FILL(0, 3, 0, 1);        /* plain, "?" reachable */
        FILL(0, 3, 1, 1);
        FILL(0, 3, 2, 0);        /* no (d, x): coin at the turn */
        FILL(0, 3, 3, 0);
        FILL(0, 4, 0, CoinValue);
        FILL(0, 4, 1, 1);
        FILL(0, 4, 2, 0);        /* phase 1, 3i+1; coin reachable */
        FILL(0, 4, 3, 0);
        FILL(0, 5, 0, 1);
        FILL(0, 5, 1, 0);
        FILL(0, 5, 2, 0);
        FILL(0, 5, 3, 0);
        FILL(0, 6, 1, 0);
        FILL(0, 6, 2, 0);
        FILL(0, 6, 3, 0);        /* coin again: three zeros, no (d, x) */
        FILL(0, 6, 0, 1);        /* this seat's 1; round 6 is three 0s, one 1 */
        FILL(0, 7, 0, CoinValue);
        FILL(0, 7, 1, 1);
        FILL(0, 7, 2, 1);        /* phase 2, 3i+1: a 1 validates only by the coin
                                  * path, since round 6 has one 1; majority 1 */
        FILL(0, 7, 3, 0);
        FILL(0, 8, 0, 1);
        FILL(0, 8, 1, 1);
        FILL(0, 8, 2, 1);
        FILL(0, 8, 3, 0);
      } else if (s == 1) {
        /* adopt, not decide: two (d, 1) at the 3i+3 turn.  Round 1 has
         * two zeros so a zero can validate at round 2, since a value
         * validates only if a correct process could have sent it */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 0);        /* the turn at three: majority 1 */
        FILL(0, 1, 3, 0);
        FILL(0, 2, 0, 1);
        FILL(0, 2, 1, 1);
        FILL(0, 2, 2, 1);        /* the turn: three ones above n/2 -> (d, 1) */
        FILL(0, 2, 3, 0);        /* validates: two zeros in round 1 */
        FILL(0, 3, 3, 0);        /* plain 0: "?" reachable in round 2, one 0 and three 1s */
        FILL(0, 3, 0, 3);        /* this seat's (d, 1) */
        FILL(0, 3, 1, 3);        /* the turn at three: two (d, 1), adopt 1, no decision */
        for (x = 0; x < n; ++x)
          FILL(0, 4, x, 1);      /* witness: more than t of (d, 1), not more than 2t */
      } else if (s == 2) {
        /* the coin alone reaches the witness of the round after */
        PUSH(EVQ, 0, 0, 0, 0, 0);
        FILL(0, 1, 0, 1);
        FILL(0, 1, 1, 1);
        FILL(0, 1, 2, 0);        /* the turn at three: majority 1 */
        FILL(0, 1, 3, 0);
        FILL(0, 2, 1, 0);
        FILL(0, 2, 2, 0);
        FILL(0, 2, 3, 0);        /* the turn: three zeros -> (d, 0) */
        FILL(0, 2, 0, 1);        /* this seat's 1: round 2 is three 0s and one 1 */
        FILL(0, 3, 2, 0);        /* plain, "?" reachable */
        FILL(0, 3, 3, 0);
        FILL(0, 3, 0, 2);        /* this seat's (d, 0): one, not more than t -> coin */
        for (x = 0; x < n; ++x)
          FILL(0, 4, x, x ? 1 : CoinValue);  /* witness only by the coin: two plain, one (d, 0) */
      }
    }
    /* Init takes the rounds up to whole phases; the shadow holds what it
     * was taken to and gives Init what the seed chose */
    initRounds = maxRounds;
    maxRounds = (maxRounds + 2) / 3 * 3;
    if (!scripted && maxRounds - initRounds && s % 2)
      initRounds = maxRounds - 2;
    if (bkr94acsInit(a, n - 1, t, VLEN - 1, self, initRounds, patience, abandon, coin, &CoinValue, beside)) {
      printf("seed %lu: Init refused\n", s);
      goto done;
    }
    outMax = bkr94acsOut(a);
    memset(exists, 0, MAXN * MAXR * MAXN);
    memset(vex, 0, sizeof (vex));
    memset(valid, 0, MAXN * MAXR * MAXN);
    memset(cnt, 0, sizeof (cnt));
    memset(ba, 0, sizeof (ba));
    memset(firstVal, 0, sizeof (firstVal));
    for (i = 0; i < n; ++i)
      ba[i].decided = 2;
    memset(qPending, 0, sizeof (qPending));
    if (RND & 1)
      for (j = 0; j < n; ++j)
        qPending[j] = 1;
    sweeps = 0;
    cb = 0;
    ck = 0;
    cq = 0;
    onesAt = (unsigned long)-1;
    ones = completed = subsetOut = abandoned = 0;

    evCount = scripted ? scriptLen : 300 + 100 * n;
    for (ev = 0; ev < evCount; ++ev) {
      /* choose the event: an arrival kind, Q, a sweep step, or a refusal */
      kind = RND % 100;
      i = RND % n;
      for (j = 0; j < n && !qPending[j]; ++j)
        ;
      if (j < n) {
        qPending[j] = 0;
        kind = 0;
        i = j;
      }
      q = RND % n;
      k = 1 + RND % maxRounds;
      from = RND % n;
      val = RND % 4;
      ann = RND % 8;
      if (RND % 3)
        val = RND & 1;
      if (RND % 3 == 0)
        q = self;
      if (kind >= 10 && kind < 75 && RND % 4) {
        /* coherent: continue an instance at the BA's round, with its value */
        if (ba[i].round)
          k = RND % 2 ? ba[i].round : (ba[i].round < maxRounds ? ba[i].round + 1 : ba[i].round);
        else
          k = 1;
        if (firstVal[i][k][q])
          val = firstVal[i][k][q] - 1;
        else if ((k - 1) % 3 == 2)
          val = RND % 4 ? 3 : RND % 4;
        else
          val = RND % 4 ? 1 : RND & 1;
        if (!EX(i, k, q))
          kind = 10;   /* INITIAL first */
      }
      if (kind < 10)
        kind = EVQ;
      else if (kind < 25)
        kind = BKR94ACS_INITIAL;
      else if (kind < 50)
        kind = BKR94ACS_ECHO;
      else if (kind < 75)
        kind = BKR94ACS_READY;
      else if (kind < 96)
        kind = EVSWEEP;   /* a sweep step */
      else
        kind = 7;               /* refused: a kind outside the five */
      if (kind == BKR94ACS_INITIAL)
        from = q;
      if (scripted) {
        kind = script[ev].kind;
        i = script[ev].ba;
        q = script[ev].q;
        k = script[ev].k;
        from = script[ev].from;
        val = script[ev].val;
        ann = script[ev].ann;
      }
      if (kind <= BKR94ACS_READY && val < 4 && !firstVal[i][k][q])
        firstVal[i][k][q] = val + 1;
      memcpy(pcnt, cnt, sizeof (cnt));
      coinsBefore = CoinCalls;
      ++calls;

      /* the refusals: a bad argument, or anything after abandon */
      x = kind == 7
       || (!scripted && kind <= BKR94ACS_READY && RND % 20 == 0 && (val = 4, 1))
       || (!scripted && kind <= BKR94ACS_READY && n < MAXN && RND % 20 == 0 && (from = n, 1))
       || (!scripted && kind <= BKR94ACS_READY && n < MAXN && RND % 20 == 0 && (q = n, 1))
       || (!scripted && kind <= EVQ && n < MAXN && RND % 20 == 0 && (i = n, 1))
       || (!scripted && kind <= BKR94ACS_READY && RND % 20 == 0 && (k = maxRounds + 1, 1))
       || (!scripted && kind == BKR94ACS_INITIAL && n > 1 && RND % 20 == 0 && (from = (q + 1) % n, 1))
       || abandoned;
      if (x) {
        unsigned long sw;
        unsigned int made;    /* the instance existed before */
        int d0;

        ++refused;
        sw = bkr94acsSweeps(a);
        d0 = bkr94acsDecision(a, 0);
        made = i < n && q < n && k >= 1 && k <= maxRounds && EX(i, k, q);
        if (kind <= BKR94ACS_READY && i < n && q < n && k >= 1 && k <= maxRounds && EX(i, k, q))
          memcpy(rcvBefore, bracha87Fig1Received(MIR(i, k, q)), (n + 7) / 8);
        if (kind == EVSWEEP) {
          more = bkr94acsSweep(a, out, &nout);
          if (more || nout)
            FAIL("a sweep after abandon returned something");
        } else if (bkr94acsInput(a, kind, i, q, k, from, (v8 = val, &v8), ann, out))
          FAIL("a refused input returned actions");
        if (bkr94acsSweeps(a) != sw || bkr94acsDecision(a, 0) != d0)
          FAIL("a refused input changed state");
        if (kind <= BKR94ACS_READY && i < n && q < n && k >= 1 && k <= maxRounds && EX(i, k, q)
         && memcmp(rcvBefore, bracha87Fig1Received(bkr94acsInstance(a, i, q, k)), (n + 7) / 8))
          FAIL("a refused input changed Received");
        if (!made && i < n && q < n && k >= 1 && k <= maxRounds && bkr94acsInstance(a, i, q, k))
          FAIL("a refused input made an instance");
        continue;
      }

      /* the call */
      memset(sawInitial, 0, sizeof (sawInitial));
      memset(sawDecided, 0, sizeof (sawDecided));
      sawEcho = sawReady = sawInitRe = 0;
      visited = 0;
      rank = 0;
      memset(sendVal, 0, sizeof (sendVal));
      memset(mirVal, 0, sizeof (mirVal));
      memset(mirGot, 0, sizeof (mirGot));
      sawAbandon = sawSubset = 0;
      passDone = 0;
      mirEcho = mirReady = mirInitRe = 0;
      if (kind == EVQ) {
        /* Q(i) = 1 becomes known by i's value broadcast being accepted
         * here.  Feed it, to the seat and to its mirror: this seat's own
         * step 0 if i is self, i's (initial, v), the BESIDE where this
         * seed feeds one, then a (ready, v) from each sender in turn.
         * The call the mirror accepts on is the call checked below;
         * the calls before it return that broadcast's own sends and
         * nothing else.  Once it is known, one duplicate (ready, v) is
         * the call. */
        unsigned char vv[VLEN];
        unsigned char vj[VLEN];     /* a faulty sender's value, readied first */
        const unsigned char *fv;
        unsigned int step;
        unsigned int fk;
        unsigned int fr;
        unsigned int ff;
        unsigned int acc;
        unsigned int dup;
        unsigned int twoFaced;      /* under the flag, i sent this seat vj and the others vv */

        vv[0] = i;
        vv[VLEN - 1] = i ^ 0xA5;
        vj[0] = i ^ 0x3C;
        vj[VLEN - 1] = i;
        dup = ba[i].qKnown;
        twoFaced = beside && i != self && i % 4 == 3;
        step = dup ? 4 : i == self ? 0 : 1;
        ff = 0;
        fv = vv;
        if (i != self && !vex[i] && !dup) {
          /* before i's value exists here: an (initial, v) from another
           * sender is not i's broadcast, under the flag a BESIDE ahead
           * of the (initial, v) is one the caller cannot have validated,
           * and with it clear nothing rides beside; all are refused and
           * make no instance */
          if (n > 1 && bkr94acsInput(a, BKR94ACS_INITIAL, i, i, 0, (i + 1) % n, vv, 0, out))
            FAIL("an (initial, v) of i's value from another sender returned actions");
          if ((!beside || (i & 2)) && bkr94acsInput(a, BKR94ACS_BESIDE, i, i, 0, i, 0, 0, out))
            FAIL("a BESIDE ahead of the (initial, v), or under a clear flag, returned actions");
          if (bkr94acsInstance(a, i, i, 0))
            FAIL("a refused message made i's value broadcast");
        }
        for (;;) {
          if (step == 0) {
            bracha87Fig1Init(MIRV(i), n - 1, t, VLEN - 1, t + 1, i, self, 0, beside);
            bracha87Fig1Initiator(MIRV(i), vv);
            vex[i] = 1;
            if (bkr94acsInput(a, BKR94ACS_READY, i, i, 0, 0, vv, 0, out))
              FAIL("a message about this seat's value before its step 0 returned actions");
            nout = bkr94acsBroadcast(a, vv, out);
            if (nout != 1 || out[0].act != BKR94ACS_SEND_INITIAL || out[0].ba != i || out[0].q != i || out[0].k)
              FAIL("Broadcast did not return this seat's value's INITIAL");
            if (bkr94acsBroadcast(a, vv, out))
              FAIL("a second Broadcast was taken");
            step = 1;
            continue;
          }
          if (step == 1) {
            fk = BKR94ACS_INITIAL;
            fr = i;
            if (twoFaced)
              fv = vj;              /* what i sent this seat; the readies below are vv */
            step = beside && (!(i & 1) || twoFaced) ? 2 : 3;
          } else if (step == 2) {
            fk = BKR94ACS_BESIDE;
            fr = i;
            fv = vv;
            step = 3;
          } else if (step == 3 && t && !dup && n > 1) {
            /* above t = 0 a faulty sender readies another value first:
             * the kept values must still have room for the correct one */
            fk = BKR94ACS_READY;
            fr = n - 1;
            fv = vj;
            step = 4;
          } else {
            fk = BKR94ACS_READY;
            fr = ff < n ? ff : 0;
            fv = vv;
            ++ff;
          }
          if (!vex[i]) {
            bracha87Fig1Init(MIRV(i), n - 1, t, VLEN - 1, t + 1, i, self, 0, beside);
            vex[i] = 1;
          }
          acc = 0;
          mirEcho = mirReady = 0;
          w = bracha87Fig1Input(MIRV(i), fk, fr, fv, 0, o1);
          for (x = 0; x < w; ++x) {
            if (o1[x] == BRACHA87_ECHO_ALL)
              mirEcho = 1;
            if (o1[x] == BRACHA87_READY_ALL)
              mirReady = 1;
            if (o1[x] == BRACHA87_ACCEPT)
              acc = 1;
          }
          nout = bkr94acsInput(a, fk, i, i, 0, fr, fv, 0, out);
          if (acc && twoFaced && memcmp(bkr94acsValue(a, i), vv, VLEN))
            FAIL("a two-faced player accepted under other than the readied value");
          if (acc)
            ba[i].qKnown = 1;
          if (acc || dup)
            break;
          if (nout != mirEcho + mirReady)
            FAIL("a value broadcast's arrival returned other than its own sends");
          for (x = 0; x < nout; ++x)
            if (out[x].k || out[x].ba != i || out[x].q != i
             || out[x].act != (x || !mirEcho ? BKR94ACS_SEND_READY : BKR94ACS_SEND_ECHO))
              FAIL("a value broadcast's arrival returned a send that is not its own");
          if (ff > n)
            FAIL("a value broadcast fed whole was not accepted");
        }
        /* nothing rides beside under a clear flag: a BESIDE is refused
         * by the seat and by the instance, the value held */
        if (!beside
         && (bkr94acsInput(a, BKR94ACS_BESIDE, i, i, 0, i, 0, 0, out)
          || bracha87Fig1Input(MIRV(i), BRACHA87_BESIDE, i, 0, 0, o1)))
          FAIL("a BESIDE under a clear flag was taken");
      } else if (kind <= BKR94ACS_READY) {
        if (q == self && !EX(i, k, q)) {
          /* a message about this seat's broadcast before its step 0 */
          if (bkr94acsInput(a, kind, i, q, k, from, (v8 = val, &v8), ann, out))
            FAIL("a message about an unmade broadcast returned actions");
          continue;
        }
        if (!EX(i, k, q)) {
          bracha87Fig1Init(MIR(i, k, q), n - 1, t, 0, (t < 3 ? t + 1 : 4), q, self, 0, 0);
          EX(i, k, q) = 1;
        }
        v8 = val;
        w = bracha87Fig1Input(MIR(i, k, q), kind, from, &v8, ann, o1);
        for (x = 0; x < w; ++x) {
          if (o1[x] == BRACHA87_ECHO_ALL) {
            mirEcho = 1;
            mirVal[1] = *bracha87Fig1Value(MIR(i, k, q), BRACHA87_ECHO_ALL);
          }
          if (o1[x] == BRACHA87_READY_ALL) {
            mirReady = 1;
            mirVal[2] = *bracha87Fig1Value(MIR(i, k, q), BRACHA87_READY_ALL);
          }
        }
        nout = bkr94acsInput(a, kind, i, q, k, from, (v8 = val, &v8), ann, out);
      } else {
        /* a sweep step: the cursor's next existing instance in (ba, k, q)
         * order gets its sweep, in the mirror as in the seat; no next,
         * and the pass completes */
        passExp = 0;
        visited = 0;
        for (;;) {
          if (cb >= n) {
            cb = 0;
            ck = 0;
            cq = 0;
            passExp = 1;
            break;
          }
          if (!ck) {
            if (!cq && vex[cb])
              break;
            ck = 1;
            cq = 0;
            continue;
          }
          if (ck > maxRounds) {
            ++cb;
            ck = 0;
            cq = 0;
            continue;
          }
          if (cq >= n) {
            ++ck;
            cq = 0;
            continue;
          }
          if (EX(cb, ck, cq))
            break;
          ++cq;
        }
        if (!passExp) {
          vb = cb;
          vk = ck;
          vq = vk ? cq : cb;
          visited = 1;
          ++cq;
          w = bracha87Fig1Input(MIRX(vb, vk, vq), BRACHA87_SWEEP, 0, 0, 0, o1);
          for (x = 0; x < w; ++x) {
            if (!vk && o1[x] == BRACHA87_ACCEPT)
              ba[vb].qKnown = 1;
            if (o1[x] == BRACHA87_ECHO_ALL) {
              mirEcho = 1;
              mirVal[1] = vk ? *bracha87Fig1Value(MIR(vb, vk, vq), BRACHA87_ECHO_ALL) : 0;
            }
            if (o1[x] == BRACHA87_READY_ALL) {
              mirReady = 1;
              mirVal[2] = vk ? *bracha87Fig1Value(MIR(vb, vk, vq), BRACHA87_READY_ALL) : 0;
            }
            if (o1[x] == BRACHA87_INITIAL_ALL) {
              mirInitRe = 1;
              mirVal[0] = vk ? *bracha87Fig1Value(MIR(vb, vk, vq), BRACHA87_INITIAL_ALL) : 0;
            }
          }
        }
        more = bkr94acsSweep(a, out, &nout);
        passDone = !more;
        if (passDone != passExp)
          FAIL("the pass completes at a different step than the cursor says");
        if (passDone) {
          ++sweeps;
          for (j = 0; j < n; ++j)
            if (ba[j].round)
              ++ba[j].roundSweeps;
        }
      }
      if (nout > outMax)
        FAIL("more actions than bkr94acsOut");

      /* read the return */
      for (x = 0; x < nout; ++x) {
        struct bkr94acsAct *act;

        act = out + x;
        if (sawAbandon)
          FAIL("an action after ABANDON in one return");
        /* the header's order: the instance's sends (a re-send of its
         * INITIAL among them, Figure 1's first), then the sends and
         * decisions the chain and the turns fire, then SUBSET, then
         * ABANDON */
        w = act->act == BKR94ACS_SEND_ECHO || act->act == BKR94ACS_SEND_READY ? 1
          : act->act == BKR94ACS_SEND_INITIAL
            && kind == EVSWEEP && visited && act->ba == vb && act->k == vk && vq == self
            && act->k <= ba[act->ba].round ? 1
          : act->act == BKR94ACS_SEND_INITIAL || act->act == BKR94ACS_DECIDED ? 2
          : act->act == BKR94ACS_SUBSET ? 3 : 4;
        if (w < rank)
          FAIL("actions out of the header's order");
        rank = w;
        switch (act->act) {
        case BKR94ACS_SEND_ECHO:
        case BKR94ACS_SEND_READY:
          if (kind == EVSWEEP) {
            /* a re-send by the cursor, of the instance it visited */
            if (!visited || act->ba != vb || act->q != vq || act->k != vk)
              FAIL("a re-send on a sweep for an instance the cursor is not at");
          } else if (kind == EVQ) {
            if (act->k || act->ba != i || act->q != i)
              FAIL("an echo or ready off the value broadcast fed");
          } else if (kind > BKR94ACS_READY || act->ba != i || act->q != q || act->k != k)
            FAIL("an echo or ready for another instance");
          if (act->act == BKR94ACS_SEND_ECHO) {
            if (sawEcho)
              FAIL("two echoes in one return");
            sawEcho = 1;
            sendVal[1] = act->value;
          } else {
            if (sawReady)
              FAIL("two readies in one return");
            sawReady = 1;
            sendVal[2] = act->value;
          }
          break;
        case BKR94ACS_SEND_INITIAL:
          if (act->q != self || act->ba >= n || act->k > maxRounds)
            FAIL("SEND_INITIAL not this seat's, or out of the space");
          if (kind == EVSWEEP && visited && act->ba == vb && act->k == vk && vq == self
           && act->k <= ba[act->ba].round) {
            /* a re-send of the broadcast the cursor visited */
            if (sawInitRe)
              FAIL("two INITIAL re-sends in one return");
            sawInitRe = 1;
            sendVal[0] = act->value;
            break;
          }
          if (!act->k)
            FAIL("a value broadcast's INITIAL off its own sweep step");
          if (sawInitial[act->ba][act->k])
            FAIL("the same broadcast twice in one return");
          sawInitial[act->ba][act->k] = act->value + 1;
          break;
        case BKR94ACS_DECIDED:
          if (act->ba >= n || act->value > 1 || sawDecided[act->ba])
            FAIL("DECIDED malformed or twice");
          sawDecided[act->ba] = act->value + 1;
          break;
        case BKR94ACS_SUBSET:
          if (sawSubset)
            FAIL("SUBSET twice in one return");
          sawSubset = 1;
          break;
        case BKR94ACS_ABANDON:
          sawAbandon = 1;
          break;
        default:
          FAIL("unknown action");
        }
      }

      /* Figure 1: the instance's sends are the mirror's, on an arrival and
       * on the cursor's visit alike */
      if ((kind <= EVQ || visited)
       && (sawEcho != mirEcho || sawReady != mirReady || sawInitRe != mirInitRe))
        FAIL("the instance's sends differ from the mirror's");
      /* a value broadcast's send carries what the mirror's does */
      if (kind == EVQ || (visited && !vk)) {
        const struct bracha87Fig1 *si;
        const unsigned char *p1;
        const unsigned char *p2;

        j = kind == EVQ ? i : vb;
        si = bkr94acsInstance(a, j, j, 0);
        for (x = 0; x < 3; ++x) {
          if (!(x == 0 ? sawInitRe : x == 1 ? sawEcho : sawReady))
            continue;
          w = x == 0 ? BRACHA87_INITIAL_ALL : x == 1 ? BRACHA87_ECHO_ALL : BRACHA87_READY_ALL;
          p1 = bracha87Fig1Value(si, w);
          p2 = bracha87Fig1Value(MIRV(j), w);
          if (!p1 || !p2 || memcmp(p1, p2, VLEN))
            FAIL("a value broadcast's send carries other than the mirror's value");
        }
      }
      if ((sawEcho && sendVal[1] != mirVal[1]) || (sawReady && sendVal[2] != mirVal[2])
       || (sawInitRe && sendVal[0] != mirVal[0]))
        FAIL("a send carries a value the mirror did not");

      /* Figure 3: close the validated sets under the validate row */
      for (;;) {
        unsigned int any;

        any = 0;
        for (j = 0; j < n; ++j)
          for (w = 1; w <= maxRounds; ++w)
            for (x = 0; x < n; ++x) {
              const unsigned char *cv;
              unsigned int yes;
              unsigned int mv;
              unsigned int pos;
              unsigned int *pc;

              if (!EX(j, w, x) || VA(j, w, x))
                continue;
              if (!(bracha87Fig1State(MIR(j, w, x)) & BRACHA87_F1_ACCEPTED))
                continue;
              cv = bracha87Fig1Value(MIR(j, w, x), BRACHA87_ACCEPT);
              mv = *cv;
              if (w == 1)
                yes = mv <= 1;
              else {
                pc = cnt[j][w - 1];
                pos = (w - 1) % 3 + 1;
                if (pc[0] + t < n)
                  yes = 0;
                else if (pos == 2) {     /* 3i+2: majority of some n - t of VALID^(3i+1),
                                          * or half of them and the sender's own */
                  yes = mv <= 1 && (2 * pc[1 + mv] > n - t
                      || (2 * pc[1 + mv] == n - t && OWN(j, w - 1, x, mv)));
                  if (yes && 2 * pc[1 + mv] == n - t)
                    ++witTie;
                } else if (pos == 3) {   /* 3i+3: (d, v) above n/2; a plain v is "?"
                                          * and the sender's own */
                  yes = mv >= 2 ? 2 * pc[1 + mv - 2] > n
                      : MINU(pc[1], n / 2) + MINU(pc[2], n / 2) >= n - t && OWN(j, w - 1, x, mv);
                  if (yes && mv <= 1)
                    ++witQ;
                } else {                 /* 3i+1: more than t of (d, v), or the coin */
                  yes = mv <= 1 && (pc[3 + mv] > t
                      || MINU(pc[3], t) + MINU(pc[4], t) + pc[1] + pc[2] >= n - t);
                  if (yes && pc[3 + mv] <= t)
                    ++witCoin;
                }
              }
              if (!yes)
                continue;
              VA(j, w, x) = 1;
              ++cnt[j][w][0];
              ++cnt[j][w][1 + mv];
              any = 1;
            }
        if (!any)
          break;
      }

      /* Agreement[Q] entries and the turn chain, as one pass repeated
       * until nothing new is placed: a decision inside the call can
       * make an entry due inside the same call, and an entry's turn
       * can chain through rounds already stored */
      memset(consumed, 0, sizeof (consumed));
    again:
      changed = 0;
      /* entries: enter 1 on Q known, enter 0 past n - t decisions of 1
       * and the patience, each once; a broadcast at round 1 is exactly
       * such an entry */
      for (j = 0; j < n; ++j) {
        unsigned int due1;
        unsigned int due0;

        due1 = ba[j].qKnown && !ba[j].input;
        due0 = !ba[j].qKnown && !ba[j].input && ones + t >= n
            && onesAt != (unsigned long)-1 && sweeps - onesAt >= patience;
        if (sawInitial[j][1] && !consumed[j]) {
          if (!due1 && !due0)
            continue;   /* judged again after the chain, or not due at all */
          if ((due1 && sawInitial[j][1] != 2) || (due0 && sawInitial[j][1] != 1))
            FAIL("an entry with the wrong value");
          consumed[j] = 1;
          changed = 1;
          ba[j].input = 1;
          ba[j].round = 1;
          ba[j].roundSweeps = 0;
          ba[j].valueP = sawInitial[j][1] - 1;
          EX(j, 1, self) = 1;
          bracha87Fig1Init(MIR(j, 1, self), n - 1, t, 0, (t < 3 ? t + 1 : 4), self, self, 0, 0);
          v8 = ba[j].valueP;
          bracha87Fig1Initiator(MIR(j, 1, self), &v8);
        }
      }

      /* Figure 3's turn and Figure 4's N, BA by BA, as many rounds as
       * the chain can advance in one call */
      for (j = 0; j < n; ++j) {
        for (;;) {
          unsigned int r;
          unsigned int pos;
          unsigned int *c;
          unsigned int *p;
          unsigned int due;
          unsigned int cont;
          unsigned int nv;
          unsigned int nvPre;
          unsigned int dec;
          unsigned int decPre;

          r = ba[j].round;
          if (!r)
            break;
          c = cnt[j][r];
          p = pcnt[j][r];
          due = c[0] == n || (c[0] + t >= n && ba[j].roundSweeps >= patience);
          pos = (r - 1) % 3 + 1;
          /* N over the counts after the call, and before it */
          dec = 2;
          decPre = 2;
          if (pos == 1) {
            nv = 2 * c[2] > c[0] ? 1 : 2 * c[2] == c[0] ? 4 : 0;
            nvPre = 2 * p[2] > p[0] ? 1 : 2 * p[2] == p[0] ? 4 : 0;
          } else if (pos == 2) {
            nv = 2 * c[1] > n ? 2 : 2 * c[2] > n ? 3 : 4;
            nvPre = 2 * p[1] > n ? 2 : 2 * p[2] > n ? 3 : 4;
          } else {
            nv = c[3] > 2 * t ? 0 : c[4] > 2 * t ? 1 : c[3] > t ? 0 : c[4] > t ? 1 : 5;
            dec = c[3] > 2 * t ? 0 : c[4] > 2 * t ? 1 : 2;
            nvPre = p[3] > 2 * t ? 0 : p[4] > 2 * t ? 1 : p[3] > t ? 0 : p[4] > t ? 1 : 5;
            decPre = p[3] > 2 * t ? 0 : p[4] > 2 * t ? 1 : 2;
          }
          cont = ba[j].decided == 2 || r - ba[j].decidedRound < 3;
          if (sawDecided[j] && pos == 3) {
            /* the decision belongs to this round's turn */
            if (ba[j].decided != 2)
              FAIL("DECIDED twice");
            if (sawDecided[j] - 1 != dec && sawDecided[j] - 1 != decPre)
              FAIL("DECIDED with a value the counts do not give");
            ba[j].decided = sawDecided[j] - 1;
            ba[j].decidedRound = r;
            ++completed;
            if (ba[j].decided == 1 && ++ones + t >= n && onesAt == (unsigned long)-1)
              onesAt = sweeps;
            ++decisions;
            sawDecided[j] = 0;
            changed = 1;
            cont = 1;
          } else if (due && pos == 3 && dec != 2 && decPre != 2 && ba[j].decided == 2
                  && (sawInitial[j][r + 1] || r == maxRounds))
            FAIL("a deciding turn without DECIDED");
          if (sawInitial[j][r + 1]) {
            unsigned int got;

            if (!due)
              FAIL("a broadcast at the next round with no turn due");
            if (!cont)
              FAIL("a broadcast by a decider past its last phase");
            got = sawInitial[j][r + 1] - 1;
            if (got == nv || (nv == 4 && got == ba[j].valueP)
             || got == nvPre || (nvPre == 4 && got == ba[j].valueP))
              ;
            else if ((nv == 5 || nvPre == 5) && got == CoinValue && CoinCalls > coinsBefore)
              ++coins;
            else
              FAIL("a broadcast whose value is not N");
            ba[j].valueP = got;
            ba[j].round = r + 1;
            ba[j].roundSweeps = 0;
            EX(j, r + 1, self) = 1;
            bracha87Fig1Init(MIR(j, r + 1, self), n - 1, t, 0, (t < 3 ? t + 1 : 4), self, self, 0, 0);
            v8 = got;
            bracha87Fig1Initiator(MIR(j, r + 1, self), &v8);
            ++turns;
            continue;
          }
          if (due && cont && r < maxRounds && !sawAbandon)
            FAIL("a turn due and no broadcast at the next round");
          if (due && cont && r == maxRounds && ba[j].decided == 2 && !sawAbandon)
            FAIL("an undecided BA past the last round and no ABANDON");
          break;
        }
      }
      if (changed)
        goto again;
      /* a ceiling abandon happens before the acs rows run, a count
       * abandon after them: entries are due in the second, not the first */
      ceiling = 0;
      ceilingBa = n;
      for (j = 0; j < n; ++j)
        if (ba[j].round == maxRounds && ba[j].decided == 2) {
          unsigned int *c;

          c = cnt[j][maxRounds];
          if (c[0] == n || (c[0] + t >= n && ba[j].roundSweeps >= patience)) {
            ceiling = 1;
            if (ceilingBa == n)
              ceilingBa = j;
          }
        }
      for (j = 0; j < n; ++j) {
        unsigned int due1;
        unsigned int due0;

        if (sawDecided[j])
          FAIL("DECIDED off a 3i+3 turn");
        if (sawInitial[j][1] && !consumed[j])
          FAIL("an entry not due");
        /* the acs rows run BA by BA, and at a pass completion after
         * every turn: an entry past the ceiling BA on a Q call, or any
         * entry on the completing pass, follows the abandon */
        if (sawInitial[j][1] && sawAbandon && ceiling && (passDone || j > ceilingBa))
          FAIL("an entry after the seat abandoned at the ceiling");
        due1 = ba[j].qKnown && !ba[j].input;
        due0 = !ba[j].qKnown && !ba[j].input && ones + t >= n
            && onesAt != (unsigned long)-1 && sweeps - onesAt >= patience;
        if ((due1 || due0) && !abandoned && !(sawAbandon && ceiling))
          FAIL("an entry due and not made");
      }

      /* the subset: at n decided, once the value of every j in it is
       * accepted here */
      for (held = 1, j = 0; j < n; ++j)
        if (ba[j].decided == 1 && !ba[j].qKnown)
          held = 0;
      if (sawSubset) {
        const unsigned char *sb;

        if (subsetOut || completed != n || !held)
          FAIL("SUBSET not at n decided with every value accepted, or twice");
        subsetOut = 1;
        ++subsets;
        if (!(sb = bkr94acsSubSet(a)))
          FAIL("SUBSET returned and the accessor gives none");
        for (j = 0; j < n; ++j)
          if ((BKR94ACS_BIT_TST(sb, j) != 0) != (ba[j].decided == 1))
            FAIL("the subset's bits disagree with the decisions");
      } else if (completed == n && held && !subsetOut)
        FAIL("the subset due and not output");

      /* abandon: the pass at the count, or an undecided BA past the last round */
      if (sawAbandon) {
        if (!(passDone && sweeps >= abandon) && !ceiling)
          FAIL("ABANDON with no cause");
        abandoned = 1;
        ++abandons;
      } else if (passDone && sweeps >= abandon)
        FAIL("the pass at the abandon count and no ABANDON");

      /* the accessors */
      if (bkr94acsSweeps(a) != sweeps)
        FAIL("Sweeps disagrees");
      if (bkr94acsAbandoned(a) != (int)abandoned)
        FAIL("Abandoned disagrees");
      for (j = 0; j < n; ++j)
        if (bkr94acsDecision(a, j) != (ba[j].decided == 2 ? -1 : (int)ba[j].decided))
          FAIL("Decision disagrees");
      for (j = 0; j < n; ++j) {
        const unsigned char *p1;

        p1 = bkr94acsValue(a, j);
        if ((p1 != 0) != (ba[j].qKnown != 0)
         || (p1 && memcmp(p1, bracha87Fig1Value(MIRV(j), BRACHA87_ACCEPT), VLEN)))
          FAIL("Value disagrees with the mirror's accept");
        if ((bkr94acsInstance(a, j, j, 0) != 0) != (vex[j] != 0))
          FAIL("Instance disagrees with the record of which value broadcasts exist");
      }
      if (kind <= BKR94ACS_READY) {
        const struct bracha87Fig1 *si;

        for (x = 0; x < n; ++x)
          if ((bkr94acsHolder(a, i, q, k, x) != 0) != (bracha87Fig1Holder(MIR(i, k, q), x) != 0))
            FAIL("Holder disagrees with the mirror");
        if (!(si = bkr94acsInstance(a, i, q, k))
         || memcmp(bracha87Fig1Received(si), bracha87Fig1Received(MIR(i, k, q)), (n + 7) / 8))
          FAIL("the instance's Received disagrees with the mirror's");
        if (bracha87Fig1State(si) != bracha87Fig1State(MIR(i, k, q)))
          FAIL("the instance's State disagrees with the mirror's");
      }
      if (visited && sawReady) {
        const struct bracha87Fig1 *si;

        if (!(si = bkr94acsInstance(a, vb, vq, vk)))
          FAIL("the re-sent READY has no instance");
        for (x = 0; x < n; ++x)
          if ((bracha87Fig1Recipient(si, x) != 0) != (bracha87Fig1Recipient(MIRX(vb, vk, vq), x) != 0))
            FAIL("the re-sent READY's Recipients disagree with the mirror's");
      }
      if (bkr94acsInstance(a, n, 0, 1) || bkr94acsInstance(a, 0, n, 1) || (n > 1 && bkr94acsInstance(a, 0, 1, 0))
       || bkr94acsInstance(a, 0, 0, maxRounds + 1) || bkr94acsInstance(0, 0, 0, 1))
        FAIL("Instance accepted a bad argument");
      for (w = 0; w < n; ++w)
        for (x = 1; x <= maxRounds; ++x)
          if ((bkr94acsInstance(a, w, x % n, x) != 0) != (EX(w, x, x % n) != 0))
            FAIL("Instance disagrees with the record of which instances exist");
    }
  }
  printf("acs shadow: %lu seeds, %lu calls, %lu refused, %lu turns, %lu decisions, %lu subsets, %lu abandons, %lu coins; witness by ?, coin, tie: %lu %lu %lu\n",
         seeds, calls, refused, turns, decisions, subsets, abandons, coins, witQ, witCoin, witTie);
  rc = 0;
done:
  free(a);
  free(out);
  free(mir);
  free(mirV);
  free(exists);
  free(valid);
  return (rc);
}
