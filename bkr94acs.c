/*
 * asynchronousByzantineAgreementProtocols - BKR94 Agreement on a Common Subset
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

#include <string.h>
#include <limits.h>
#include "bracha87Fig1.h"
#include "bkr94acs.h"

/*
 * One BA at this seat: Figure 4's state and the round it is at.
 */
struct bkr94acsBa {
  unsigned int roundSweeps;   /* the seat's sweeps since this round began */
  unsigned char round;        /* current round, 1..maxRounds; 0 = not entered */
  unsigned char decidedRound; /* the round it decided at; 0 before */
  unsigned char valueP;       /* BKR94ACS_V0 .. BKR94ACS_D1 */
  unsigned char decided;      /* 0, 1, or 2 for none */
  unsigned char input;        /* non-zero once this seat entered its input */
  unsigned char qKnown;       /* Q(j) = 1 is known: j's value is accepted here */
};

/*
 * The seat.  d holds, in order and each region 8-aligned: the n BAs;
 * per (ba, k) five counts, validated and per value; per (ba, k) a
 * bitmap over q of the instances that exist; per (ba, k) a bitmap over
 * q of the validated ones; the subset; a bitmap over j of the value
 * broadcasts that exist; then n x maxRounds x n Figure 1 instances of
 * one-byte values, at an 8-aligned stride; then the n value broadcasts,
 * Figure 1 instances of vLen bytes, at their own 8-aligned stride.
 */
struct bkr94acs {
  unsigned char (*coin)(void *);
  void *ctx;
  unsigned long offVInst;     /* past the BAs' instances: the one offset beyond 32 bits */
  unsigned int offCnt;        /* region offsets into d, in bytes */
  unsigned int offExists;
  unsigned int offValid;
  unsigned int offSubSet;
  unsigned int offVEx;
  unsigned int offInst;
  unsigned int sz1;           /* one Figure 1 instance, 8-aligned */
  unsigned int szV;           /* one value broadcast's, 8-aligned */
  unsigned int ones;          /* BAs decided 1 */
  unsigned int completed;     /* BAs decided */
  unsigned int curBa;         /* the sweep cursor */
  unsigned int curK;
  unsigned int curQ;
  unsigned int sweeps;        /* the seat's, since Init */
  unsigned int patience;
  unsigned int abandon;
  unsigned int onesAt;        /* sweeps at the n-t'th decision of 1 */
  unsigned char n1;
  unsigned char t;
  unsigned char vLen1;
  unsigned char self;
  unsigned char maxRounds;
  unsigned char beside;
  unsigned char flags;
  unsigned long d[1];
};

#define F_ABANDONED 0x01
#define F_SUBSET    0x02

#define ALIGN8(x)    (((x) + 7) / 8 * 8)
#define N(a)         ((a)->n1 + 1)
#define BITS(n)      (((n) + 7) / 8)
#define D(a)         ((unsigned char *)(a)->d)
#define BA(a, i)     ((struct bkr94acsBa *)D(a) + (i))
#define RK(a, i, k)  ((i) * (a)->maxRounds + (k) - 1)
#define CNT(a, i, k) ((unsigned short *)(D(a) + (a)->offCnt) + RK(a, i, k) * 5)
#define EXISTS(a, i, k) (D(a) + (a)->offExists + RK(a, i, k) * BITS(N(a)))
#define VALID(a, i, k)  (D(a) + (a)->offValid + RK(a, i, k) * BITS(N(a)))
#define SUBSET(a)    (D(a) + (a)->offSubSet)
#define INST(a, i, k, q) \
  ((struct bracha87Fig1 *)(D(a) + (a)->offInst + (unsigned long)(RK(a, i, k) * N(a) + (q)) * (a)->sz1))
#define VEX(a)       (D(a) + (a)->offVEx)
#define VINST(a, j)  ((struct bracha87Fig1 *)(D(a) + (a)->offVInst + (j) * (a)->szV))
#define BIT_SET(m, p) ((m)[(p) >> 3] |= 1 << ((p) & 7))
#define MIN(x, y)    ((x) < (y) ? (x) : (y))

unsigned long
bkr94acsSz(
  unsigned char n1
 ,unsigned char vLen1
 ,unsigned char maxRounds
){
  unsigned long fixed;      /* everything but the BAs' instances */
  unsigned long count;      /* the BAs' instances */
  unsigned int n;
  unsigned int rk;
  unsigned int sz1;

  if (!maxRounds)
    return (0);
  maxRounds = (maxRounds + 2) / 3 * 3;
  n = n1 + 1U;
  rk = n * maxRounds;
  fixed = sizeof (struct bkr94acs) - sizeof (unsigned long)
        + ALIGN8(n * sizeof (struct bkr94acsBa))
        + ALIGN8(rk * 5 * sizeof (unsigned short))
        + ALIGN8(rk * BITS(n))
        + ALIGN8(rk * BITS(n))
        + ALIGN8(BITS(n))
        + ALIGN8(BITS(n))
        + n * ALIGN8(bracha87Fig1Sz(n1, vLen1, n1 / 3 + 1));
  count = (unsigned long)rk * n;
  sz1 = ALIGN8(bracha87Fig1Sz(n1, 0, MIN(4, n1 / 3 + 1)));
  /* the one product that can pass 32 bits: none where it does not fit */
  if (count > (ULONG_MAX - fixed) / sz1)
    return (0);
  return (fixed + count * sz1);
}

int
bkr94acsInit(
  struct bkr94acs *a
 ,unsigned char n1
 ,unsigned char t
 ,unsigned char vLen1
 ,unsigned char self
 ,unsigned char maxRounds
 ,unsigned int patience
 ,unsigned int abandon
 ,unsigned char (*coin)(void *)
 ,void *ctx
 ,unsigned char beside
){
  unsigned int n;
  unsigned int rk;
  unsigned int off;
  unsigned int i;

  if (!a || t > n1 / 3 || self > n1 || (abandon && abandon <= patience) || !coin
   || !bkr94acsSz(n1, vLen1, maxRounds))
    return (-1);
  /* a BA decides only at a phase's third round: whole phases */
  maxRounds = (maxRounds + 2) / 3 * 3;
  n = n1 + 1U;
  rk = n * maxRounds;
  a->coin = coin;
  a->ctx = ctx;
  a->sweeps = 0;
  a->patience = patience;
  a->abandon = abandon;
  a->onesAt = 0;
  off = ALIGN8(n * sizeof (struct bkr94acsBa));
  a->offCnt = off;
  off += ALIGN8(rk * 5 * sizeof (unsigned short));
  a->offExists = off;
  off += ALIGN8(rk * BITS(n));
  a->offValid = off;
  off += ALIGN8(rk * BITS(n));
  a->offSubSet = off;
  off += ALIGN8(BITS(n));
  a->offVEx = off;
  off += ALIGN8(BITS(n));
  a->offInst = off;
  /* a BA's broadcast has four values and the entry refuses the rest, so
   * four kept is every one; a value broadcast keeps t + 1, the most the
   * figure can use */
  a->sz1 = ALIGN8(bracha87Fig1Sz(n1, 0, MIN(4, t + 1)));
  a->offVInst = off + (unsigned long)rk * n * a->sz1;
  a->szV = ALIGN8(bracha87Fig1Sz(n1, vLen1, t + 1));
  a->ones = a->completed = 0;
  a->curBa = 0;
  a->curK = 0;
  a->curQ = 0;
  a->n1 = n1;
  a->t = t;
  a->vLen1 = vLen1;
  a->self = self;
  a->maxRounds = maxRounds;
  a->beside = beside;
  a->flags = 0;
  memset(D(a), 0, a->offInst);
  for (i = 0; i < n; ++i)
    BA(a, i)->decided = 2;
  return (0);
}

unsigned int
bkr94acsOut(
  const struct bkr94acs *a
){
  if (!a)
    return (0);
  /* one call can broadcast at every round of every BA: an entry's
   * turn chains through the rounds already stored when patience is 0,
   * and step 2 enters every BA it can in one call; plus a decision per
   * BA, the instance's own three sends, the subset and abandon */
  return (N(a) * (a->maxRounds + 1U) + 8);
}

/*
 * Every entry point runs the same body: an arrival feeds its instance
 * and evaluates it; a sweep step feeds one instance a sweep, and the
 * completed pass advances the counters and evaluates what they enable;
 * the seat's own value takes step 0 of its broadcast.  Round 0 of
 * (j, j) is j's value broadcast, and its accept is Q(j) = 1.
 * The regions below are entered by goto with a site to return to.
 * A sweep step fires no step of Figure 1, so it returns re-sends only.
 * Abandon, from the sweep count or from a round past maxRounds, is
 * recorded when it happens and returned last, after Agreement[Q]'s
 * rows have run on the event that caused it.
 */
static unsigned int
bkr94acsRun(
  struct bkr94acs *a
 ,unsigned char ev        /* 0 an arrival, 1 a sweep step, 2 this seat's value */
 ,unsigned char kind      /* an arrival's kind */
 ,unsigned char ba
 ,unsigned char q
 ,unsigned char k
 ,unsigned char from
 ,const unsigned char *vp /* the value: vLen bytes at round 0, one byte above */
 ,unsigned char annot
 ,struct bkr94acsAct *out
 ,int *passDone
){
  struct bracha87Fig1 *inst;
  struct bkr94acsBa *b;
  unsigned short *c;
  unsigned short *pc;
  const unsigned char *cv;
  unsigned int n;
  unsigned int t;
  unsigned int nout;
  unsigned int i;           /* the BA in hand */
  unsigned int kk;          /* the round in hand; 0 for the BA's turn alone */
  unsigned int qq;          /* the broadcaster in hand */
  unsigned int r;           /* the BA's current round */
  unsigned int j;
  unsigned int w;
  unsigned int ek;          /* the round and broadcaster a new broadcast is at */
  unsigned int eq;
  unsigned int n1out;
  unsigned int site;        /* where eval, walk and acs return to */
  unsigned int fig3site;
  unsigned int fig4site;
  unsigned int enterSite;
  unsigned int walkK;       /* the walk: round whose validations may give witnesses */
  unsigned int walkQ;
  unsigned int walkAny;
  unsigned int validatedHere;
  unsigned int st;
  unsigned int acsNeeded;
  unsigned int abandonHere;
  unsigned char v;
  unsigned char o1[BRACHA87_FIG1_OUT];
  /* the dispatches' boundary locals, as the bridges name them */
  int accepted, haveValidated, roundOne, valueBinary, witness, validatedBand, patienceBand;
  int validate, turn;
  int round, haveDecided, sinceDecisionBand, sampleOnes, sampleZerosGtHalfN, sampleOnesGtHalfN, sampleD0, sampleD1;
  int msgRound, msgValue, prevHasNT, prevSame, prevSameGtHalfN, prevQuestion, prevDSameGtT, prevCoin, prevOwn;
  int valueP, decide, cont;
  int qKnown, myInput, onesBand, completedBand, acsPatienceBand, haveOutput;
  int valuesHeld;
  int enterOne, enterZero, outputSubSet, takeSubSet;

  n = N(a);
  t = a->t;
  inst = 0;
  nout = 0;
  site = 0;
  *passDone = 0;
  validatedHere = 0;
  acsNeeded = 0;
  abandonHere = 0;
  i = ba;
  kk = k;
  qq = q;

  if (ev == 2)
    goto broadcast;
  if (ev == 1)
    goto sweepStep;
  if (!kk)
    goto valueArrival;

  /* an arrival for (ba, q, k): the instance exists from its first
   * message -- except this seat's own broadcast, which exists from its
   * step 0: a message about it before then is not this broadcast's */
  inst = INST(a, i, kk, qq);
  if (qq == a->self && !BRACHA87_BIT_TST(EXISTS(a, i, kk), qq))
    return (0);
  if (!BRACHA87_BIT_TST(EXISTS(a, i, kk), qq)) {
    /* nothing of the caller's rides beside a BA's broadcast */
    bracha87Fig1Init(inst, a->n1, a->t, 0, MIN(4, a->t + 1), qq, a->self, 0, 0);
    BIT_SET(EXISTS(a, i, kk), qq);
  }
  n1out = bracha87Fig1Input(inst, kind, from, vp, annot, o1);
  for (j = 0; j < n1out; ++j) {
    if (o1[j] == BRACHA87_ECHO_ALL || o1[j] == BRACHA87_READY_ALL) {
      cv = bracha87Fig1Value(inst, o1[j]);
      out[nout].act = o1[j] == BRACHA87_ECHO_ALL ? BKR94ACS_SEND_ECHO : BKR94ACS_SEND_READY;
      out[nout].ba = i;
      out[nout].q = qq;
      out[nout].k = kk;
      out[nout].value = cv ? *cv : 0;
      ++nout;
    }
  }
  site = 0;
  goto eval;
evalDone0:
  if (validatedHere)
    goto walk;
walkDone0:
  if (acsNeeded)
    goto acs;
acsDone0:
  goto finish;

  /* an arrival for j's value broadcast: it exists from its first
   * message, this seat's own from its step 0 */
valueArrival:
  inst = VINST(a, i);
  /* a BESIDE makes no instance: ahead of i's (initial, v) it is one the
   * caller cannot have validated, and the instance refuses the rest */
  if (kind == BKR94ACS_BESIDE && !BRACHA87_BIT_TST(VEX(a), i))
    return (0);
  if (!BRACHA87_BIT_TST(VEX(a), i)) {
    if (i == a->self)
      return (0);
    bracha87Fig1Init(inst, a->n1, a->t, a->vLen1, a->t + 1, i, a->self, 0, a->beside);
    BIT_SET(VEX(a), i);
  }
  n1out = bracha87Fig1Input(inst, kind, from, vp, annot, o1);
  site = 0;
  /* the value broadcast's sends, and its accept: Q(i) = 1 is known */
valueOut:
  for (j = 0; j < n1out; ++j) {
    if (o1[j] == BRACHA87_ACCEPT) {
      BA(a, i)->qKnown = 1;
      acsNeeded = 1;
      continue;
    }
    out[nout].act = o1[j] == BRACHA87_INITIAL_ALL ? BKR94ACS_SEND_INITIAL
                  : o1[j] == BRACHA87_ECHO_ALL ? BKR94ACS_SEND_ECHO : BKR94ACS_SEND_READY;
    out[nout].ba = out[nout].q = i;
    out[nout].k = out[nout].value = 0;
    ++nout;
  }
  if (site)
    return (nout);
  goto walkDone0;

  /* step 0 of this seat's value broadcast, once */
broadcast:
  i = a->self;
  if (BRACHA87_BIT_TST(VEX(a), i))
    return (0);
  inst = VINST(a, i);
  bracha87Fig1Init(inst, a->n1, a->t, a->vLen1, a->t + 1, i, a->self, 0, a->beside);
  BIT_SET(VEX(a), i);
  bracha87Fig1Initiator(inst, vp);
  out[0].act = BKR94ACS_SEND_INITIAL;
  out[0].ba = out[0].q = i;
  out[0].k = out[0].value = 0;
  return (1);

sweepStep:
  /* the next existing instance in the rotation, or the pass completes;
   * a player's value broadcast comes before its BA's rounds */
  for (;;) {
    if (a->curBa >= n) {
      a->curBa = 0;
      a->curK = 0;
      a->curQ = 0;
      goto passComplete;
    }
    if (!a->curK) {
      if (!a->curQ && BRACHA87_BIT_TST(VEX(a), a->curBa))
        break;
      a->curK = 1;
      a->curQ = 0;
      continue;
    }
    if (a->curK > a->maxRounds) {
      ++a->curBa;
      a->curK = 0;
      a->curQ = 0;
      continue;
    }
    if (a->curQ >= n) {
      ++a->curK;
      a->curQ = 0;
      continue;
    }
    if (BRACHA87_BIT_TST(EXISTS(a, a->curBa, a->curK), a->curQ))
      break;
    ++a->curQ;
  }
  i = a->curBa;
  kk = a->curK;
  qq = a->curQ;
  ++a->curQ;
  if (!kk) {
    inst = VINST(a, i);
    n1out = bracha87Fig1Input(inst, BRACHA87_SWEEP, 0, 0, 0, o1);
    site = 1;
    goto valueOut;
  }
  inst = INST(a, i, kk, qq);
  n1out = bracha87Fig1Input(inst, BRACHA87_SWEEP, 0, 0, 0, o1);
  /* a sweep fires no step: what comes back is the re-sends owed */
  for (j = 0; j < n1out; ++j) {
    cv = bracha87Fig1Value(inst, o1[j]);
    out[nout].act = o1[j] == BRACHA87_INITIAL_ALL ? BKR94ACS_SEND_INITIAL
                  : o1[j] == BRACHA87_ECHO_ALL ? BKR94ACS_SEND_ECHO : BKR94ACS_SEND_READY;
    out[nout].ba = i;
    out[nout].q = qq;
    out[nout].k = kk;
    out[nout].value = cv ? *cv : 0;
    ++nout;
  }
  return (nout);

passComplete:
  *passDone = 1;
  site = 2;
  ++a->sweeps;
  for (i = 0; i < n; ++i)
    if (BA(a, i)->round)
      ++BA(a, i)->roundSweeps;
  /* the turns the round patience now allows, BA by BA */
  for (i = 0; i < n; ++i) {
    if (!BA(a, i)->round)
      continue;
    kk = 0;
    qq = 0;
    validatedHere = 0;
    site = 2;
    goto eval;
evalDone2:
    ;
  }
  goto acs;
acsDone2:
  if (a->abandon && a->sweeps >= a->abandon)
    abandonHere = 1;
  goto finish;

finish:
  if (abandonHere) {
    a->flags |= F_ABANDONED;
    out[nout].act = BKR94ACS_ABANDON;
    out[nout].ba = out[nout].q = out[nout].k = out[nout].value = 0;
    ++nout;
  }
  return (nout);

  /*
   * Evaluate: the instance (i, kk, qq), or with kk == 0 no instance and
   * only BA i's turn.  Figure 4 first for the witness, Figure 3 for
   * validate and the turn; after a validation Figure 3 again, since the
   * turn reads the round's count as state; Figure 4 again for N on a
   * turn; and after the turn's broadcast the new round's turn, since
   * stored messages may already satisfy it.
   */
eval:
  b = BA(a, i);
  r = b->round;
  if (kk) {
    inst = INST(a, i, kk, qq);
    st = bracha87Fig1State(inst);
    accepted = (st & BRACHA87_F1_ACCEPTED) != 0;
    haveValidated = BRACHA87_BIT_TST(VALID(a, i, kk), qq) != 0;
    cv = accepted ? bracha87Fig1Value(inst, BRACHA87_ACCEPT) : 0;
    msgValue = cv ? *cv : 0;
    roundOne = kk == 1;
    msgRound = (kk - 1) % 3 + 1;
  } else {
    accepted = 0;
    haveValidated = 0;
    msgValue = 0;
    roundOne = 0;
    msgRound = 1;
  }
  valueBinary = msgValue <= BKR94ACS_V1;
  /* the witness: VALID^(k-1)'s counts against the figure's thresholds,
   * and where N kept value_p the sender's own (k-1)-message, validated
   * here, since value_p is what it broadcast there */
  if (kk > 1) {
    pc = CNT(a, i, kk - 1);
    prevHasNT = pc[0] + t >= n;
    w = msgValue <= BKR94ACS_V1 ? msgValue : msgValue - BKR94ACS_D0;
    prevSame = 2 * pc[1 + w] > n - t ? 2 : 2 * pc[1 + w] == n - t ? 1 : 0;
    prevSameGtHalfN = 2 * pc[1 + w] > n;
    prevQuestion = MIN(pc[1], n / 2) + MIN(pc[2], n / 2) >= n - t;
    prevDSameGtT = pc[1 + BKR94ACS_D0 + w] > t;
    prevCoin = MIN(pc[1 + BKR94ACS_D0], t) + MIN(pc[1 + BKR94ACS_D1], t) + pc[1] + pc[2] >= n - t;
    prevOwn = BRACHA87_BIT_TST(VALID(a, i, kk - 1), qq)
           && *bracha87Fig1Value(INST(a, i, kk - 1, qq), BRACHA87_ACCEPT) == msgValue;
  } else {
    prevHasNT = 0;
    prevSame = 0;
    prevSameGtHalfN = 0;
    prevQuestion = 0;
    prevDSameGtT = 0;
    prevCoin = 0;
    prevOwn = 0;
  }
  turn = 0;
  fig4site = 0;
  goto fig4;
fig4Done0:
  fig3site = 0;
  goto fig3;
fig3Done0:
  if (validate) {
    BIT_SET(VALID(a, i, kk), qq);
    c = CNT(a, i, kk);
    ++c[0];
    ++c[1 + msgValue];
    validatedHere = 1;
    walkK = kk;
    /* the turn reads the count as state: read it again now */
    haveValidated = 1;
    fig3site = 1;
    goto fig3;
fig3Done1:
    ;
  }
turnCheck:
  if (!turn || !r || (a->flags & F_ABANDONED))
    goto evalReturn;
  /* the turn: N over the current round's validated set */
  fig4site = 1;
  goto fig4;
fig4Done1:
  if (valueP == 5)
    b->valueP = a->coin(a->ctx) & 1;
  else if (valueP != 4)
    b->valueP = valueP;
  if (decide != 2 && b->decided == 2) {
    b->decided = decide;
    b->decidedRound = r;
    ++a->completed;
    if (decide == 1 && ++a->ones + t == n)
      a->onesAt = a->sweeps;
    out[nout].act = BKR94ACS_DECIDED;
    out[nout].ba = i;
    out[nout].q = out[nout].k = 0;
    out[nout].value = decide;
    ++nout;
    acsNeeded = 1;
  }
  if (!cont)
    goto evalReturn;    /* the phase after its decision is done: it stops */
  if (r + 1 > a->maxRounds) {
    /* a round past the byte: a decided BA stops short of its last
     * phase; an undecided one needs what the seat cannot hold, and the
     * seat is done, after this event */
    if (b->decided != 2)
      goto evalReturn;
    a->flags |= F_ABANDONED;
    abandonHere = 1;
    goto evalReturn;
  }
  b->round = r + 1;
  b->roundSweeps = 0;
  v = b->valueP;
  enterSite = 0;
  goto enter;
enterDoneTurn:
  /* the new round may already meet its wait: evaluate its turn */
  r = b->round;
  fig3site = 2;
  goto fig3;
fig3Done2:
  goto turnCheck;
evalReturn:
  switch (site) {
  case 0: goto evalDone0;
  case 2: goto evalDone2;
  case 3: goto evalDone3;
  default: goto evalDone4;
  }

  /*
   * Figure 3's dispatch: validate for the instance in hand, and the
   * turn for BA i's current round r.
   */
fig3:
  if (r) {
    c = CNT(a, i, r);
    validatedBand = c[0] == n ? 2 : c[0] + t >= n ? 1 : 0;
    patienceBand = b->roundSweeps >= a->patience;
  } else {
    validatedBand = 0;
    patienceBand = 0;
  }
#include "bracha87Fig3Rules.c"
  switch (fig3site) {
  case 0: goto fig3Done0;
  case 1: goto fig3Done1;
  default: goto fig3Done2;
  }

  /*
   * Figure 4's dispatch, entered for the witness with turn 0 and for N
   * with the turn: the two chains are independent in its DAG.  The
   * sample N reads is the current round's whole validated set, at
   * least n - t and up to n, which is what the turn's patience buys
   * (bracha87Fig4.dtc).
   */
fig4:
  round = r ? (r - 1) % 3 + 1 : 1;
  haveDecided = b->decided != 2;
  sinceDecisionBand = haveDecided && r - b->decidedRound >= 3;
  if (r) {
    c = CNT(a, i, r);
    sampleOnes = 2 * c[1 + BKR94ACS_V1] > c[0] ? 2 : 2 * c[1 + BKR94ACS_V1] == c[0] ? 1 : 0;
    sampleZerosGtHalfN = 2 * c[1 + BKR94ACS_V0] > n;
    sampleOnesGtHalfN = 2 * c[1 + BKR94ACS_V1] > n;
    sampleD0 = c[1 + BKR94ACS_D0] > 2 * t ? 2 : c[1 + BKR94ACS_D0] > t ? 1 : 0;
    sampleD1 = c[1 + BKR94ACS_D1] > 2 * t ? 2 : c[1 + BKR94ACS_D1] > t ? 1 : 0;
  } else {
    sampleOnes = 0;
    sampleZerosGtHalfN = sampleOnesGtHalfN = 0;
    sampleD0 = sampleD1 = 0;
  }
#include "bracha87Fig4Rules.c"
  if (fig4site == 0)
    goto fig4Done0;
  goto fig4Done1;

  /*
   * This seat broadcasts v at round b->round of BA i: step 0 of a new
   * Figure 1 instance (i, self, round).  The hand-back is the caller's.
   */
enter:
  ek = b->round;
  eq = a->self;
  inst = INST(a, i, ek, eq);
  if (!BRACHA87_BIT_TST(EXISTS(a, i, ek), eq)) {
    bracha87Fig1Init(inst, a->n1, a->t, 0, MIN(4, a->t + 1), eq, a->self, 0, 0);
    BIT_SET(EXISTS(a, i, ek), eq);
  }
  bracha87Fig1Initiator(inst, &v);
  out[nout].act = BKR94ACS_SEND_INITIAL;
  out[nout].ba = i;
  out[nout].q = eq;
  out[nout].k = ek;
  out[nout].value = v;
  ++nout;
  if (enterSite)
    goto enterDoneAcs;
  goto enterDoneTurn;

  /*
   * The walk: a validation at round walkK of BA i may give a stored
   * message at a later round its witness.  Revisit each later round's
   * accepted, unvalidated instances, stopping at the first round that
   * validates nothing new.  Each visit is the validate row; bounded by
   * maxRounds and by the stored messages themselves.
   */
walk:
  for (;;) {
    ++walkK;
    if (walkK > a->maxRounds)
      break;
    walkAny = 0;
    for (walkQ = 0; walkQ < n; ++walkQ) {
      if (!BRACHA87_BIT_TST(EXISTS(a, i, walkK), walkQ)
       || BRACHA87_BIT_TST(VALID(a, i, walkK), walkQ))
        continue;
      if (!(bracha87Fig1State(INST(a, i, walkK, walkQ)) & BRACHA87_F1_ACCEPTED))
        continue;
      kk = walkK;
      qq = walkQ;
      validatedHere = 0;
      {
        unsigned int saveSite;
        unsigned int saveK;

        saveSite = site;
        saveK = walkK;
        site = 3;
        goto eval;
evalDone3:
        site = saveSite;
        walkK = saveK;
      }
      if (validatedHere)
        walkAny = 1;
    }
    if (!walkAny)
      break;
  }
  validatedHere = 0;
  goto walkDone0;

  /*
   * Agreement[Q], every BA_j: enter 1 on Q(j), enter 0 past n - t
   * decisions of 1 and the patience, output the subset at n once the
   * value of every j in it is accepted here.  An entry is followed by
   * that BA's turn, since the round it enters may already meet its
   * wait.
   */
acs:
  acsNeeded = 0;
  onesBand = a->ones + t >= n;
  completedBand = a->completed == n;
  acsPatienceBand = onesBand && a->sweeps - a->onesAt >= a->patience;
  haveOutput = (a->flags & F_SUBSET) != 0;
  valuesHeld = 0;
  if (completedBand && !haveOutput) {
    for (w = 0; w < n && (BA(a, w)->decided != 1 || BA(a, w)->qKnown); ++w)
      ;
    valuesHeld = w == n;
  }
  for (j = 0; j < n; ++j) {
    b = BA(a, j);
    qKnown = b->qKnown;
    myInput = b->input != 0;
#include "bkr94acsRules.c"
    (void) outputSubSet;
    if ((enterOne || enterZero) && !(a->flags & F_ABANDONED)) {
      b->input = 1;
      b->round = 1;
      b->roundSweeps = 0;
      b->valueP = enterOne ? BKR94ACS_V1 : BKR94ACS_V0;
      i = j;
      v = b->valueP;
      enterSite = 1;
      goto enter;
enterDoneAcs:
      {
        unsigned int saveSite;

        saveSite = site;
        kk = 0;
        qq = 0;
        validatedHere = 0;
        site = 4;
        goto eval;
evalDone4:
        site = saveSite;
      }
    }
    if (takeSubSet) {
      a->flags |= F_SUBSET;
      haveOutput = 1;
      for (w = 0; w < n; ++w)
        if (BA(a, w)->decided == 1)
          BIT_SET(SUBSET(a), w);
      out[nout].act = BKR94ACS_SUBSET;
      out[nout].ba = out[nout].q = out[nout].k = out[nout].value = 0;
      ++nout;
    }
  }
  /* an entry's own turn decided under this loop: the counts moved, so
   * the rows run once more; entries do not repeat, so this ends */
  if (acsNeeded)
    goto acs;
  if (!site)
    goto acsDone0;
  goto acsDone2;
}

unsigned int
bkr94acsInput(
  struct bkr94acs *a
 ,unsigned char kind
 ,unsigned char ba
 ,unsigned char q
 ,unsigned char k
 ,unsigned char from
 ,const unsigned char *value
 ,unsigned char annot
 ,struct bkr94acsAct *out
){
  int passDone;

  if (!a || !out || kind > BKR94ACS_BESIDE || ba > a->n1 || q > a->n1 || from > a->n1
   || k > a->maxRounds)
    return (0);
  if (a->flags & F_ABANDONED)
    return (0);
  if (k) {
    if (kind == BKR94ACS_BESIDE || !value || *value > BKR94ACS_D1)
      return (0);
  } else if (q != ba || (kind != BKR94ACS_BESIDE && !value))
    return (0);
  /* an (initial, v) or BESIDE is q's alone: from anyone else it is not
   * this broadcast, and makes no instance */
  if ((kind == BKR94ACS_INITIAL || kind == BKR94ACS_BESIDE) && from != q)
    return (0);
  return (bkr94acsRun(a, 0, kind, ba, q, k, from, value, annot, out, &passDone));
}

unsigned int
bkr94acsBroadcast(
  struct bkr94acs *a
 ,const unsigned char *value
 ,struct bkr94acsAct *out
){
  int passDone;

  if (!a || !value || !out)
    return (0);
  if (a->flags & F_ABANDONED)
    return (0);
  return (bkr94acsRun(a, 2, 0, 0, 0, 0, 0, value, 0, out, &passDone));
}

int
bkr94acsSweep(
  struct bkr94acs *a
 ,struct bkr94acsAct *out
 ,unsigned int *nout
){
  int passDone;

  if (nout)
    *nout = 0;
  if (!a || !out || !nout)
    return (0);
  if (a->flags & F_ABANDONED)
    return (0);
  *nout = bkr94acsRun(a, 1, 0, 0, 0, 0, 0, 0, 0, out, &passDone);
  return (!passDone);
}

int
bkr94acsHolder(
  const struct bkr94acs *a
 ,unsigned char ba
 ,unsigned char q
 ,unsigned char k
 ,unsigned char p
){
  if (!a || ba > a->n1 || q > a->n1 || k > a->maxRounds)
    return (0);
  if (!k)
    return (q == ba && BRACHA87_BIT_TST(VEX(a), ba) ? bracha87Fig1Holder(VINST(a, ba), p) : 0);
  if (!BRACHA87_BIT_TST(EXISTS(a, ba, k), q))
    return (0);
  return (bracha87Fig1Holder(INST(a, ba, k, q), p));
}

const struct bracha87Fig1 *
bkr94acsInstance(
  const struct bkr94acs *a
 ,unsigned char ba
 ,unsigned char q
 ,unsigned char k
){
  if (!a || ba > a->n1 || q > a->n1 || k > a->maxRounds)
    return (0);
  if (!k)
    return (q == ba && BRACHA87_BIT_TST(VEX(a), ba) ? VINST(a, ba) : 0);
  if (!BRACHA87_BIT_TST(EXISTS(a, ba, k), q))
    return (0);
  return (INST(a, ba, k, q));
}

const unsigned char *
bkr94acsSubSet(
  const struct bkr94acs *a
){
  if (!a || !(a->flags & F_SUBSET))
    return (0);
  return (SUBSET(a));
}

const unsigned char *
bkr94acsValue(
  const struct bkr94acs *a
 ,unsigned char j
){
  if (!a || j > a->n1 || !BRACHA87_BIT_TST(VEX(a), j))
    return (0);
  return (bracha87Fig1Value(VINST(a, j), BRACHA87_ACCEPT));
}

int
bkr94acsDecision(
  const struct bkr94acs *a
 ,unsigned char ba
){
  if (!a || ba > a->n1 || BA(a, ba)->decided == 2)
    return (-1);
  return (BA(a, ba)->decided);
}

unsigned int
bkr94acsSweeps(
  const struct bkr94acs *a
){
  if (!a)
    return (0);
  return (a->sweeps);
}

int
bkr94acsAbandoned(
  const struct bkr94acs *a
){
  if (!a)
    return (0);
  return ((a->flags & F_ABANDONED) != 0);
}
