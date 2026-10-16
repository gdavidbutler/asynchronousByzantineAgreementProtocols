/*
 * asynchronousByzantineAgreementProtocols - BKR94 ACS test
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
 * n seats over an in-memory queue that loses each delivery with the
 * given probability, every seat honest, every seat broadcasting its
 * value at the start, driven drain-then-sweep until every seat has
 * abandoned.  Each seat hands its own messages straight back to
 * itself, off the queue.  Asserts, at t = 0 with no seat starved:
 *
 *   every seat decides 1 in every BA, and outputs the subset of all n;
 *   every seat holds, for every j, the value j broadcast, and held it
 *     for every j in the subset when the subset was output; and at the
 *     end holds every j's (initial, v) and what rode beside it;
 *   without loss the INITIAL re-sends stop: every seat says it holds a
 *     broadcast within two sweeps of it;
 *   no seat ever enters 0 -- n - t is n, so step 2 never fires;
 *   the coin is never called -- every input is 1;
 *   every seat abandons, and nothing comes after.
 *
 * Above t = 0 with every seat honest the same holds, since the
 * thresholds are met by honest counts alone.
 *
 * With starve = s, seats 1..s see no READY of seat n - 1's value until
 * each has entered its input to that seat's BA; the INITIAL and the
 * echoes reach them, so they echo and ready, the other seats accept on
 * everyone's readies and enter 1, and a starved seat holds one ready,
 * its own, never accepts, and enters 0 at step 2.  Honest inputs to
 * that one BA are then split, which no all-ones run reaches.  At n
 * even, t above 0 and s = n/2, without loss (run at n = 4 and n = 6,
 * t = 1, the only lossless shape accepted): the unstarved seats turn
 * round 1 at n - t on a majority of 1, the starved turn at n on a tie
 * and keep 0, no round-2 sample has a value above n/2 so every seat
 * keeps, round 3 is plain at every seat and every seat tosses the coin,
 * and the phase after decides the coin's value at round 6.  The turns
 * are the schedule's and no assert pins them; the coin count does.  The plain
 * round-3 values are validated by the witness row that reads the
 * sender's own round-2 message, which is what this arm reaches across
 * seats; the tie row at 3i+2 needs n - t even and is the shadow's.
 * Asserts, without loss: every seat decides every BA, the same value
 * at every seat, 1 for every BA but the last and the coin's value for
 * the last; every starved seat entered 0 holding nothing of the
 * starved value, every other seat entered 1; the coin was called once
 * per seat; the subset is the BAs decided 1, with their values held.
 * With loss the schedule is the seed's and a fixed coin can repeat a
 * phase past the ceiling, so of the last BA only agreement is asserted:
 * every seat that decided it decided the same value as every other
 * that did, every starved seat that entered it entered 0, every seat
 * that output a subset output the BAs decided 1 and the same subset as
 * every other that did; every other BA decides 1 at every seat, as in
 * every lossy run, and the initials, the values and the silence after
 * abandon are asserted as in every lossy run.
 *
 * usage: test_bkr94acs n t loss [maxRounds [starve [coin]]]
 *        (loss in percent; maxRounds defaults to 6, two phases; 3
 *        makes every decision the last round, so the subset must still
 *        come before abandon; starve defaults to 0, else 1..n-2 with t
 *        above 0, and without loss n/2 at n even with maxRounds at
 *        least 4; coin, 0 or 1, is what every seat's coin answers)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87Fig1.h"
#include "bkr94acs.h"

#define HOLDS(st) (((st) & (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE)) == (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE))

#define VLEN 3
#define PATIENCE 1
#define ABANDON 400

struct msg {
  unsigned char kind;
  unsigned char ba;
  unsigned char q;
  unsigned char k;
  unsigned char from;
  unsigned char value;
  unsigned char annot;    /* on an ECHO or a READY */
  unsigned char to;
  unsigned char v[VLEN];  /* at round 0: the value */
};

static unsigned long CoinCalls;

static unsigned char
coin(
  void *ctx
){
  ++CoinCalls;
  return (*(unsigned char *)ctx);
}

int
main(
  int argc
 ,char *argv[]
){
  struct bkr94acs **a;
  struct bkr94acsAct *out;
  struct bkr94acsAct *work;
  struct msg *q;
  unsigned char *abandoned;
  unsigned char *subsetSeen;
  unsigned char *entered;     /* per seat: 1 + the input it entered to the last BA, 0 before */
  unsigned long *decided;
  unsigned long sz;
  unsigned int n;
  unsigned int t;
  unsigned int loss;
  unsigned int seed;
  unsigned int starve;        /* seats 1..starve see no READY of the last seat's value until they enter its BA */
  unsigned int qcap;
  unsigned int qhead;
  unsigned int qtail;
  unsigned int sweeps;
  unsigned int dropped;
  unsigned int zeros;
  unsigned int initials;      /* this seat's broadcasts: first sends of INITIAL */
  unsigned int initialRe;     /* and their re-sends on sweeps */
  unsigned int outMax;
  unsigned int workCap;
  unsigned int nwork;
  unsigned int nout;
  unsigned int p;
  unsigned int site;
  unsigned int i;
  unsigned int j;
  unsigned int w;
  unsigned int to;
  unsigned char coinValue;
  unsigned char maxRounds;
  unsigned char v[VLEN];
  const unsigned char *cv;
  const struct bracha87Fig1 *inst;
  int recip;                  /* a READY re-sent on a sweep: to its recipients only */
  int rc;
  int more;
  int dl;                     /* the last BA's decision at the seats that decided it, -1 until one has */
  int d;

  if (argc < 4 || argc > 7) {
    fprintf(stderr, "usage: %s n t loss [maxRounds [starve [coin]]]\n", argv[0]);
    return (1);
  }
  n = atoi(argv[1]);
  t = atoi(argv[2]);
  loss = atoi(argv[3]);
  maxRounds = argc >= 5 ? atoi(argv[4]) : 6;
  starve = argc >= 6 ? atoi(argv[5]) : 0;
  w = argc == 7 ? atoi(argv[6]) : 0;
  if (n < 1 || n > 256 || t > 85 || n <= 3 * t || maxRounds < 1
   || (starve && (!t || starve > n - 2)) || w > 1
   || (starve && !loss && (n % 2 || starve != n / 2 || maxRounds < 4))) {
    fprintf(stderr, "n is 1..256 and above 3t, maxRounds at least 1, starve 0 or 1..n-2 with t above 0 (without loss n/2 at n even, maxRounds at least 4), coin 0 or 1\n");
    return (1);
  }
  coinValue = w;
  seed = 777 + n * 13 + loss;
  rc = 1;
  a = 0;
  out = work = 0;
  q = 0;
  abandoned = subsetSeen = entered = 0;
  decided = 0;
  CoinCalls = 0;

  sz = bkr94acsSz(n - 1, VLEN - 1, maxRounds);
  if (!(a = calloc(n, sizeof (*a))) || !(abandoned = calloc(n, 1))
   || !(subsetSeen = calloc(n, 1)) || !(entered = calloc(n, 1))
   || !(decided = calloc(n, sizeof (*decided)))) {
    printf("allocation failed\n");
    goto done;
  }
  for (i = 0; i < n; ++i) {
    if (!(a[i] = calloc(1, sz))) {
      printf("allocation failed\n");
      goto done;
    }
    if (bkr94acsInit(a[i], n - 1, t, VLEN - 1, i, maxRounds, PATIENCE, ABANDON, coin, &coinValue, 0)) {
      printf("Init refused seat %u\n", i);
      goto done;
    }
  }
  if (!bkr94acsInit(0, n - 1, t, VLEN - 1, 0, maxRounds, PATIENCE, ABANDON, coin, &coinValue, 0)
   || !bkr94acsInit(a[0], n - 1, t, VLEN - 1, 0, 0, PATIENCE, ABANDON, coin, &coinValue, 0)
   || !bkr94acsInit(a[0], n - 1, t, VLEN - 1, 0, maxRounds, ABANDON, ABANDON, coin, &coinValue, 0)
   || !bkr94acsInit(a[0], n - 1, t, VLEN - 1, 0, maxRounds, PATIENCE, ABANDON, 0, &coinValue, 0)
   || !bkr94acsInit(a[0], 2, 1, VLEN - 1, 0, maxRounds, PATIENCE, ABANDON, coin, &coinValue, 0)) {
    printf("Init accepted a bad argument\n");
    goto done;
  }
  /* an abandon of 0 is none, above any patience */
  if (bkr94acsInit(a[0], n - 1, t, VLEN - 1, 0, maxRounds, PATIENCE, 0, coin, &coinValue, 0)
   || bkr94acsInit(a[0], n - 1, t, VLEN - 1, 0, maxRounds, PATIENCE, ABANDON, coin, &coinValue, 0)) {
    printf("Init refused abandon 0, none\n");
    goto done;
  }
  outMax = bkr94acsOut(a[0]);
  workCap = 16 * outMax;
  if (!(out = calloc(outMax, sizeof (*out))) || !(work = calloc(workCap, sizeof (*work)))) {
    printf("allocation failed\n");
    goto done;
  }
  qcap = 64 * n * n * n + 64;
  if (!(q = calloc(qcap, sizeof (*q)))) {
    printf("allocation failed\n");
    goto done;
  }
  qhead = qtail = 0;
  sweeps = dropped = zeros = initials = initialRe = 0;

  /* every seat broadcasts its value, once: seat p's is p, 'v', p ^ 0x5A */
  for (p = 0; p < n; ++p) {
    v[0] = p;
    v[1] = 'v';
    v[2] = p ^ 0x5A;
    if (bkr94acsInput(a[p], BKR94ACS_READY, p, p, 0, p, v, 0, work)) {
      printf("seat %u took a message about its value before broadcasting it\n", p);
      goto done;
    }
    nwork = bkr94acsBroadcast(a[p], v, work);
    if (nwork != 1 || bkr94acsBroadcast(a[p], v, out)) {
      printf("seat %u: Broadcast returned %u, or a second one was taken\n", p, nwork);
      goto done;
    }
    site = 0;
    goto run;
qDone:
    ;
  }

  for (;;) {
    while (qhead < qtail) {
      struct msg *m;

      m = &q[qhead++];
      seed = seed * 1103515245u + 12345u;
      if (((seed >> 16) & 0x7FFF) % 100 < loss) {
        ++dropped;
        continue;
      }
      if (m->to == m->from) {
        printf("a message to self was on the queue\n");
        goto done;
      }
      /* the starvation is decided at delivery, not at the send: a seat's
       * entry can come later in the sweep than the re-sends queued to it */
      if (starve && m->kind == BKR94ACS_READY && m->ba == n - 1 && !m->k
       && m->to >= 1 && m->to <= starve && !entered[m->to])
        continue;
      p = m->to;
      if (abandoned[p])
        continue;
      nwork = bkr94acsInput(a[p], m->kind, m->ba, m->q, m->k, m->from, m->k ? &m->value : m->v, m->annot, work);
      site = 1;
      goto run;
arrivalDone:
      ;
    }
    qhead = qtail = 0;

    ++sweeps;
    for (p = 0; p < n; ++p) {
      if (abandoned[p])
        continue;
      do {
        more = bkr94acsSweep(a[p], out, &nout);
        if (nout > outMax) {
          printf("seat %u returned %u actions, room %u\n", p, nout, outMax);
          goto done;
        }
        memcpy(work, out, nout * sizeof (*out));
        nwork = nout;
        site = 2;
        goto run;
sweepDone:
        ;
      } while (more);
    }
    for (p = 0; p < n; ++p)
      if (!abandoned[p])
        break;
    if (p == n)
      break;
    if (sweeps > 4 * ABANDON) {
      printf("harness guard: %u sweeps without every seat abandoning\n", sweeps);
      goto done;
    }
  }

  dl = -1;
  for (p = 0; p < n; ++p) {
    if (starve && loss) {
      /* every BA but the last decides 1 everywhere, as in every lossy
       * run; of the last, agreement alone: among the seats that decided
       * it, and among the seats that output */
      for (j = 0; j + 1 < n; ++j)
        if (bkr94acsDecision(a[p], j) != 1) {
          printf("seat %u: BA %u decided %d\n", p, j, bkr94acsDecision(a[p], j));
          goto done;
        }
      if ((d = bkr94acsDecision(a[p], n - 1)) != -1) {
        for (w = 0; w < p; ++w)
          if (bkr94acsDecision(a[w], n - 1) != -1 && bkr94acsDecision(a[w], n - 1) != d) {
            printf("seat %u: BA %u decided %d, seat %u %d\n", p, n - 1, d, w, bkr94acsDecision(a[w], n - 1));
            goto done;
          }
        dl = d;
      }
      if (p >= 1 && p <= starve && entered[p] == 1 + BKR94ACS_V1) {
        printf("seat %u entered BA %u with 1 while starved\n", p, n - 1);
        goto done;
      }
      if (subsetSeen[p] != (bkr94acsSubSet(a[p]) != 0)) {
        printf("seat %u: SUBSET and the accessor disagree\n", p);
        goto done;
      }
      for (j = 0; subsetSeen[p] && j < n; ++j) {
        if ((bkr94acsDecision(a[p], j) == 1) != (BKR94ACS_BIT_TST(bkr94acsSubSet(a[p]), j) != 0)) {
          printf("seat %u: subset bit %u is not BA %u's decision\n", p, j, j);
          goto done;
        }
        for (w = 0; w < p; ++w)
          if (subsetSeen[w]
           && (BKR94ACS_BIT_TST(bkr94acsSubSet(a[p]), j) != 0) != (BKR94ACS_BIT_TST(bkr94acsSubSet(a[w]), j) != 0)) {
            printf("seat %u and seat %u output different subsets at %u\n", p, w, j);
            goto done;
          }
      }
      goto held;
    }
    if (decided[p] != n) {
      printf("seat %u decided %lu of %u BAs\n", p, decided[p], n);
      goto done;
    }
    for (j = 0; j < n; ++j)
      if (bkr94acsDecision(a[p], j) != (starve && j == n - 1 ? coinValue : 1)) {
        printf("seat %u: BA %u decided %d\n", p, j, bkr94acsDecision(a[p], j));
        goto done;
      }
    if (!subsetSeen[p] || !bkr94acsSubSet(a[p])) {
      printf("seat %u did not output the subset\n", p);
      goto done;
    }
    for (j = 0; j < n; ++j)
      if ((BKR94ACS_BIT_TST(bkr94acsSubSet(a[p]), j) != 0) != (bkr94acsDecision(a[p], j) == 1)) {
        printf("seat %u: subset bit %u is not BA %u's decision\n", p, j, j);
        goto done;
      }
    if (starve && entered[p] != (p >= 1 && p <= starve ? 1 + BKR94ACS_V0 : 1 + BKR94ACS_V1)) {
      printf("seat %u entered BA %u with %d\n", p, n - 1, entered[p] - 1);
      goto done;
    }
held:
    for (j = 0; j < n; ++j)
      if (!HOLDS(bracha87Fig1State(bkr94acsInstance(a[p], j, j, 0)))) {
        printf("seat %u never held %u's (initial, v) and what rode beside it\n", p, j);
        goto done;
      }
    for (j = 0; j < n; ++j)
      if (!(cv = bkr94acsValue(a[p], j)) || cv[0] != j || cv[1] != 'v' || cv[2] != (j ^ 0x5A)) {
        printf("seat %u: the value of %u is not what %u broadcast\n", p, j, j);
        goto done;
      }
    v[0] = v[1] = v[2] = 0;
    if (bkr94acsInput(a[p], BKR94ACS_READY, 0, 0, 0, 0, v, 0, work)
     || bkr94acsSweep(a[p], out, &nout) || nout) {
      printf("seat %u took input after abandon\n", p);
      goto done;
    }
  }
  if (zeros) {
    printf("%u entries of 0 at t=%u\n", zeros, t);
    goto done;
  }
  if (!starve && CoinCalls) {
    printf("the coin was called %lu times\n", CoinCalls);
    goto done;
  }
  if (starve && !loss && CoinCalls != n) {
    printf("the coin was called %lu times for %u seats\n", CoinCalls, n);
    goto done;
  }
  if (loss && !dropped) {
    printf("loss given and nothing dropped\n");
    goto done;
  }
  /* without loss every seat says it holds a broadcast within two
   * sweeps of it, so the INITIAL re-sends stop: at most two a broadcast */
  if (!loss && initialRe > 2 * initials) {
    printf("%u INITIAL re-sends for %u broadcasts without loss\n", initialRe, initials);
    goto done;
  }
  if (starve && loss)
    printf("n=%u t=%u loss=%u%% starve=%u coin=%u: the seats that decided agree, the seats that output agree, all abandoned, %u sweeps, %u dropped, %lu coin calls, the last BA %s\n",
           n, t, loss, starve, coinValue, sweeps, dropped, CoinCalls,
           dl == -1 ? "undecided everywhere" : dl ? "decided 1" : "decided 0");
  else if (starve)
    printf("n=%u t=%u loss=%u%% starve=%u coin=%u: every seat decided 1 in every BA but the last and the coin in the last, subset with its values, all abandoned, %u sweeps, %lu coin calls\n",
           n, t, loss, starve, coinValue, sweeps, CoinCalls);
  else
    printf("n=%u t=%u loss=%u%%: every seat decided 1 in every BA, subset all with its values, all abandoned, %u sweeps, %u dropped\n",
           n, t, loss, sweeps, dropped);
  rc = 0;
done:
  if (a)
    for (i = 0; i < n; ++i)
      free(a[i]);
  free(a);
  free(out);
  free(work);
  free(q);
  free(abandoned);
  free(subsetSeen);
  free(entered);
  free(decided);
  return (rc);

  /* run the actions in work[] at seat p, self by hand-back, return to site */
run:
  while (nwork) {
    struct bkr94acsAct act;

    act = work[--nwork];
    switch (act.act) {
    case BKR94ACS_DECIDED:
      ++decided[p];
      continue;
    case BKR94ACS_SUBSET:
      subsetSeen[p] = 1;
      for (j = 0; j < n; ++j)
        if (BKR94ACS_BIT_TST(bkr94acsSubSet(a[p]), j) && !bkr94acsValue(a[p], j)) {
          printf("seat %u: subset output without the value of %u\n", p, j);
          goto done;
        }
      continue;
    case BKR94ACS_ABANDON:
      abandoned[p] = 1;
      continue;
    case BKR94ACS_SEND_INITIAL:
      /* a 0 is entered to the starved BA alone */
      if (act.k == 1 && act.value == BKR94ACS_V0 && !(starve && act.ba == n - 1))
        ++zeros;
      /* the entry to the last BA ends this seat's starvation; a starved
       * seat enters holding nothing of the starved value, or the
       * starvation leaked */
      if (act.k == 1 && act.ba == n - 1 && !entered[p]) {
        if (starve && p >= 1 && p <= starve && bkr94acsValue(a[p], n - 1)) {
          printf("seat %u held the starved value at its entry\n", p);
          goto done;
        }
        entered[p] = 1 + act.value;
      }
      if (site == 2)
        ++initialRe;
      else
        ++initials;
      break;
    case BKR94ACS_SEND_ECHO:
    case BKR94ACS_SEND_READY:
      break;
    default:
      printf("seat %u: unknown action %u\n", p, act.act);
      goto done;
    }
    /* INITIAL to the seats not heard from; a READY re-sent on a sweep to
     * the seats the instance still owes it, carrying what it says */
    inst = act.act != BKR94ACS_SEND_INITIAL ? bkr94acsInstance(a[p], act.ba, act.q, act.k) : 0;
    recip = act.act == BKR94ACS_SEND_READY && site == 2;
    /* a value broadcast's send carries its own vLen bytes */
    cv = act.k ? &act.value
       : bracha87Fig1Value(bkr94acsInstance(a[p], act.ba, act.q, 0),
                           act.act == BKR94ACS_SEND_INITIAL ? BRACHA87_INITIAL_ALL
                         : act.act == BKR94ACS_SEND_ECHO ? BRACHA87_ECHO_ALL : BRACHA87_READY_ALL);
    if (!cv) {
      printf("seat %u: a send of (%u, %u, %u) with no value\n", p, act.ba, act.q, act.k);
      goto done;
    }
    for (to = 0; to < n; ++to) {
      unsigned char kind;
      unsigned char annot;

      kind = act.act == BKR94ACS_SEND_INITIAL ? BKR94ACS_INITIAL
           : act.act == BKR94ACS_SEND_ECHO ? BKR94ACS_ECHO : BKR94ACS_READY;
      if (act.act == BKR94ACS_SEND_INITIAL) {
        if (bkr94acsHolder(a[p], act.ba, act.q, act.k, to))
          continue;
      }
      if (recip && !bracha87Fig1Recipient(inst, to))
        continue;
      if (act.act == BKR94ACS_SEND_ECHO && site == 2 && !bracha87Fig1EchoRecipient(inst, to))
        continue;
      /* every echo and ready says whether this seat holds the initial
       * and what rode beside it; a ready says the rest */
      annot = 0;
      if (inst && HOLDS(bracha87Fig1State(inst)))
        annot |= BRACHA87_ANN_HOLDING;
      if (act.act == BKR94ACS_SEND_READY) {
        if (bracha87Fig1State(inst) & BRACHA87_F1_ACCEPTED)
          annot |= BRACHA87_ANN_ACCEPTED;
        if (BRACHA87_BIT_TST(bracha87Fig1Received(inst), to))
          annot |= BRACHA87_ANN_RECEIVED;
      }
      if (to == p) {
        /* the hand-back is an input like any other; who this sweep's
         * re-sends go to stands until the next sweep, so its place in
         * this loop does not matter */
        if (nwork + outMax > workCap) {
          printf("seat %u: hand-back cascade too deep\n", p);
          goto done;
        }
        nout = bkr94acsInput(a[p], kind, act.ba, act.q, act.k, p, cv, annot, out);
        for (w = 0; w < nout; ++w)
          work[nwork++] = out[w];
        continue;
      }
      if (qtail >= qcap) {
        printf("queue overflow\n");
        goto done;
      }
      q[qtail].kind = kind;
      q[qtail].ba = act.ba;
      q[qtail].q = act.q;
      q[qtail].k = act.k;
      q[qtail].from = p;
      q[qtail].value = act.value;
      q[qtail].annot = annot;
      q[qtail].to = to;
      if (!act.k)
        memcpy(q[qtail].v, cv, VLEN);
      ++qtail;
    }
  }
  switch (site) {
  case 0: goto qDone;
  case 1: goto arrivalDone;
  default: goto sweepDone;
  }
}
