/*
 * asynchronousByzantineAgreementProtocols - Bracha 1987 Figure 1
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

#include <stddef.h>
#include <string.h>
#include "bracha87Fig1.h"

/*
 * The instance.  cnt holds, in order: one count per distinct echo value
 * kept, one per distinct ready value kept (values of each, the caller's
 * K); then bytes: the initiator's v, the value echoed, the value
 * readied, the value accepted (vLen each); the K echo values and the K
 * ready values kept (vLen each); then four bitmaps of (n + 7) / 8
 * bytes: the senders of (echo, v), the senders of (ready, v), the
 * senders recorded as holding the initial, and the senders recorded as
 * having accepted.
 *
 * A value is kept the first time a sender sends it, until K are kept;
 * a sender of any other value is recorded as a sender and counts
 * toward no value.  What K = t + 1 keeps and what it can lose is in
 * the header.
 */
struct bracha87Fig1 {
  unsigned int sweeps;    /* this process's, since it started, up to abandon */
  unsigned int abandon;   /* 0: none */
  unsigned char n1;       /* n - 1 */
  unsigned char t;
  unsigned char vLen1;    /* vLen - 1 */
  unsigned char initiator;
  unsigned char self;
  unsigned char flags;    /* BRACHA87_F1_* */
  unsigned char values;   /* K: distinct values kept per kind */
  unsigned char echoes;   /* echo values kept so far */
  unsigned char readies;  /* ready values kept so far */
  unsigned char beside;   /* the caller's flag: something rides beside (initial, v) */
  unsigned short cnt[1];
};

#define N(f)         ((f)->n1 + 1)
#define VLEN(f)      ((f)->vLen1 + 1)
#define K(f)         ((f)->values)
#define BITS(n)      (((n) + 7) / 8)
#define ECNT(f)      ((f)->cnt)
#define RCNT(f)      ((f)->cnt + K(f))
#define BYTES(f)     ((unsigned char *)((f)->cnt + 2 * K(f)))
#define INIT(f)      (BYTES(f))
#define ECHOED(f)    (BYTES(f) + VLEN(f))
#define READIED(f)   (BYTES(f) + 2 * VLEN(f))
#define ACCEPTED(f)  (BYTES(f) + 3 * VLEN(f))
#define EVAL(f)      (BYTES(f) + 4 * VLEN(f))
#define RVAL(f)      (EVAL(f) + K(f) * VLEN(f))
#define ECHOSET(f)   (RVAL(f) + K(f) * VLEN(f))
#define READYSET(f)  (ECHOSET(f) + BITS(N(f)))
#define HOLDSET(f)   (READYSET(f) + BITS(N(f)))
#define ACCSET(f)    (HOLDSET(f) + BITS(N(f)))
#define BIT_SET(m, p) ((m)[(p) >> 3] |= 1 << ((p) & 7))
/* the bytes from cnt on, for an instance of n, vLen, K */
#define REST(n1, vLen1, K) \
  (2U * (K) * sizeof (unsigned short) + (4U + 2U * (K)) * ((vLen1) + 1) + 4U * BITS((n1) + 1))

unsigned int
bracha87Fig1Sz(
  unsigned char n1
 ,unsigned char vLen1
 ,unsigned char values
){
  if (!values)
    return (0);
  return (offsetof(struct bracha87Fig1, cnt) + REST(n1, vLen1, values));
}

int
bracha87Fig1Init(
  struct bracha87Fig1 *f
 ,unsigned char n1
 ,unsigned char t
 ,unsigned char vLen1
 ,unsigned char values
 ,unsigned char initiator
 ,unsigned char self
 ,unsigned int abandon
 ,unsigned char beside
){
  if (!f || t > n1 / 3 || !values || values > t + 1 || initiator > n1 || self > n1)
    return (-1);
  f->sweeps = 0;
  f->abandon = abandon;
  f->n1 = n1;
  f->t = t;
  f->vLen1 = vLen1;
  f->initiator = initiator;
  f->self = self;
  /* with nothing beside, the caller's something is held from the start */
  f->flags = beside ? 0 : BRACHA87_F1_BESIDE;
  f->values = values;
  f->echoes = f->readies = 0;
  f->beside = beside;
  memset(f->cnt, 0, REST(n1, vLen1, values));
  return (0);
}

int
bracha87Fig1Initiator(
  struct bracha87Fig1 *f
 ,const unsigned char *value
){
  if (!f || !value || f->self != f->initiator
   || (f->flags & (BRACHA87_F1_INITIATOR | BRACHA87_F1_INITIAL)))
    return (-1);
  memcpy(INIT(f), value, VLEN(f));
  f->flags |= BRACHA87_F1_INITIATOR;
  return (0);
}

unsigned int
bracha87Fig1Input(
  struct bracha87Fig1 *f
 ,unsigned char kind
 ,unsigned char from
 ,const unsigned char *value
 ,unsigned char annot
 ,unsigned char *out
){
  const unsigned char *v;
  unsigned int n;
  unsigned int t;
  unsigned int vLen;
  unsigned int u;          /* senders of echo or ready, at entry */
  unsigned int cand;       /* candidate cursor: INIT, then echoes, then readies */
  unsigned int c;
  unsigned int i;
  unsigned int ec;
  unsigned int rd;
  unsigned int nout;
  unsigned int steps;      /* steps taken this call, as action bits */
  int evaluated;
  int retryI;
  int retryE;
  int retryR;
  /* the dispatch's boundary locals, named as bracha87Fig1ToC.dtc names them */
  int sweep, receivedInitial, ecGtHalfNT, rdGeTPlus1, rdGe2TPlus1;
  int haveEchoed, haveSentReady, haveAccepted, amInitiator, holdSomething;
  int holdingBand, abandonBand, abandoned, owedBand;
  int sendEcho, sendReady, acceptV, takeStep1, takeStep2, takeStep3;
  int abandon, retryInitial, retryEcho, retryReady;
  /* bracha87Fig1ReadyToC.dtc's, run once after the main dispatch */
  int readyArrival, carriesReceived, acceptedBefore, replyReady;
  int acceptedNow, readiedBefore, answerReady;
  int initialArrival, holdsBoth, echoedBefore, replyEcho;

  if (!f || !out || kind > BRACHA87_SWEEP)
    return (0);
  /* an abandoned seat is done with this broadcast: it records nothing
   * and takes nothing */
  if (f->flags & BRACHA87_F1_ABANDONED)
    return (0);
  n = N(f);
  t = f->t;
  vLen = VLEN(f);

  /* record the arrival */
  switch (kind) {
  case BRACHA87_INITIAL:
    if (from >= n || !value)
      return (0);
    if (from != f->initiator)
      return (0);
    if (!(f->flags & BRACHA87_F1_INITIAL)) {
      if (!(f->flags & BRACHA87_F1_INITIATOR))
        memcpy(INIT(f), value, vLen);
      f->flags |= BRACHA87_F1_INITIAL;
    }
    break;
  case BRACHA87_ECHO:
    if (from >= n || !value)
      return (0);
    if (!BRACHA87_BIT_TST(ECHOSET(f), from)) {
      BIT_SET(ECHOSET(f), from);
      /* count it under its value: kept already, kept now, or not kept */
      for (i = 0; i < f->echoes && memcmp(EVAL(f) + i * vLen, value, vLen); ++i)
        ;
      if (i < f->echoes)
        ++ECNT(f)[i];
      else if (i < K(f)) {
        memcpy(EVAL(f) + i * vLen, value, vLen);
        ECNT(f)[i] = 1;
        ++f->echoes;
      }
    }
    /* carrying HOLDING, the sender holds the initial: recorded on
     * every arrival, a re-send's included */
    if (annot & BRACHA87_ANN_HOLDING)
      BIT_SET(HOLDSET(f), from);
    break;
  case BRACHA87_READY:
    if (from >= n || !value)
      return (0);
    if (!BRACHA87_BIT_TST(READYSET(f), from)) {
      BIT_SET(READYSET(f), from);
      for (i = 0; i < f->readies && memcmp(RVAL(f) + i * vLen, value, vLen); ++i)
        ;
      if (i < f->readies)
        ++RCNT(f)[i];
      else if (i < K(f)) {
        memcpy(RVAL(f) + i * vLen, value, vLen);
        RCNT(f)[i] = 1;
        ++f->readies;
      }
    }
    /* carrying HOLDING or ACCEPTED, the sender holds the initial or
     * has accepted: recorded on every arrival, a re-send's included,
     * beside the ready itself */
    if (annot & BRACHA87_ANN_HOLDING)
      BIT_SET(HOLDSET(f), from);
    if (annot & BRACHA87_ANN_ACCEPTED)
      BIT_SET(ACCSET(f), from);
    break;
  case BRACHA87_BESIDE:
    if (from >= n)
      return (0);
    if (from != f->initiator)
      return (0);
    /* nothing rides beside under a clear flag; under a set one the
     * caller validates what rode beside against the v the instance
     * holds, so ahead of the (initial, v) it cannot have: both refused */
    if (!f->beside || !(f->flags & (BRACHA87_F1_INITIATOR | BRACHA87_F1_INITIAL)))
      return (0);
    f->flags |= BRACHA87_F1_BESIDE;
    break;
  default:
    break;
  }
  /* this sweep counts, up to the abandon count */
  if (kind == BRACHA87_SWEEP && f->sweeps < f->abandon)
    ++f->sweeps;
  /* the replies and the answer read what this process had done before
   * this event */
  echoedBefore = (f->flags & BRACHA87_F1_ECHOED) != 0;
  readiedBefore = (f->flags & BRACHA87_F1_RDSENT) != 0;
  acceptedBefore = (f->flags & BRACHA87_F1_ACCEPTED) != 0;

  /* what does not vary by candidate value */
  for (u = 0, i = 0; i < n; ++i)
    if (BRACHA87_BIT_TST(ECHOSET(f), i) || BRACHA87_BIT_TST(READYSET(f), i))
      ++u;
  for (holdingBand = 1, i = 0; i < n && holdingBand; ++i)
    if (!BRACHA87_BIT_TST(HOLDSET(f), i))
      holdingBand = 0;
  /* (ready, v) is owed to a process until it is recorded as having
   * accepted */
  for (owedBand = 0, i = 0; i < n && !owedBand; ++i)
    if (!BRACHA87_BIT_TST(ACCSET(f), i))
      owedBand = 1;
  abandonBand = f->abandon && f->sweeps >= f->abandon;
  sweep = kind == BRACHA87_SWEEP;
  amInitiator = (f->flags & BRACHA87_F1_INITIATOR) != 0;
  nout = 0;
  steps = 0;
  evaluated = 0;
  retryI = retryE = retryR = 0;

  /*
   * "For some v": the dispatch runs once per distinct value in hand --
   * the initiator's v, then each echo value kept, then each ready value
   * kept -- with the steps applied between, so a later candidate reads
   * the state an earlier one left.  An instance with no value in hand
   * runs it once over the zeroed initial slot, every count at 0: no
   * step can fire, and the rows that do not read a value, abandon
   * among them, still do.
   */
  cand = 0;
next:
  if (cand == 0) {
    if (!(f->flags & (BRACHA87_F1_INITIATOR | BRACHA87_F1_INITIAL)) && u)
      goto skip;
    v = INIT(f);
  } else if (cand <= f->echoes)
    v = EVAL(f) + (cand - 1) * vLen;
  else if (cand <= K(f))
    goto skip;
  else if (cand <= K(f) + f->readies)
    v = RVAL(f) + (cand - K(f) - 1) * vLen;
  else
    goto done;
  /* seen already as an earlier candidate? */
  for (c = 0; c < cand; ++c) {
    if (c == 0) {
      if (!(f->flags & (BRACHA87_F1_INITIATOR | BRACHA87_F1_INITIAL)) && u)
        continue;
      if (!memcmp(INIT(f), v, vLen))
        goto skip;
    } else if (c <= f->echoes) {
      if (!memcmp(EVAL(f) + (c - 1) * vLen, v, vLen))
        goto skip;
    } else if (c > K(f) && !memcmp(RVAL(f) + (c - K(f) - 1) * vLen, v, vLen))
      goto skip;
  }
  /* the figure's counts, for this v */
  for (ec = 0, i = 0; i < f->echoes; ++i)
    if (!memcmp(EVAL(f) + i * vLen, v, vLen))
      ec = ECNT(f)[i];
  for (rd = 0, i = 0; i < f->readies; ++i)
    if (!memcmp(RVAL(f) + i * vLen, v, vLen))
      rd = RCNT(f)[i];
  receivedInitial = (f->flags & BRACHA87_F1_INITIAL) != 0
                 && !memcmp(INIT(f), v, vLen);
  /* the caller's validation of p's (initial, v), read where that
   * initial is what enables step 1; under a clear flag it is held from
   * Init */
  holdSomething = (f->flags & BRACHA87_F1_BESIDE) != 0;
  ecGtHalfNT = ec > (n + t) / 2;
  rdGeTPlus1 = rd >= t + 1;
  rdGe2TPlus1 = rd >= 2 * t + 1;
  haveEchoed = (f->flags & BRACHA87_F1_ECHOED) != 0;
  haveSentReady = (f->flags & BRACHA87_F1_RDSENT) != 0;
  haveAccepted = (f->flags & BRACHA87_F1_ACCEPTED) != 0;
  abandoned = (f->flags & BRACHA87_F1_ABANDONED) != 0;
#include "bracha87Fig1Rules.c"
  (void) sendEcho;
  (void) sendReady;
  (void) acceptV;
  evaluated = 1;
  if (takeStep1) {
    f->flags |= BRACHA87_F1_ECHOED;
    memcpy(ECHOED(f), v, vLen);
    steps |= 1 << BRACHA87_ECHO_ALL;
  }
  if (takeStep2) {
    f->flags |= BRACHA87_F1_RDSENT;
    memcpy(READIED(f), v, vLen);
    steps |= 1 << BRACHA87_READY_ALL;
  }
  if (takeStep3) {
    f->flags |= BRACHA87_F1_ACCEPTED;
    memcpy(ACCEPTED(f), v, vLen);
    steps |= 1 << BRACHA87_ACCEPT;
  }
  if (abandon) {
    f->flags |= BRACHA87_F1_ABANDONED;
    steps |= 1 << BRACHA87_ABANDON;
  }
  /* the re-sends are the last evaluation's: it saw every step applied */
  retryI = retryInitial;
  retryE = retryEcho;
  retryR = retryReady;
skip:
  ++cand;
  goto next;
done:
  /* the ready's reply: a (ready, v) arriving without RECEIVED, once
   * this process has an accept to carry; and at the accept, the answer
   * to the readies held */
  readyArrival = kind == BRACHA87_READY;
  carriesReceived = readyArrival && (annot & BRACHA87_ANN_RECEIVED) != 0;
  acceptedNow = (steps & (1 << BRACHA87_ACCEPT)) != 0;
  /* and the echo's: p's (initial, v) or BESIDE, at a process holding
   * both that had echoed before it came */
  initialArrival = kind == BRACHA87_INITIAL || kind == BRACHA87_BESIDE;
  holdsBoth = (f->flags & (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE))
           == (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE);
#include "bracha87Fig1ReadyRules.c"
  if (steps & (1 << BRACHA87_ECHO_ALL))
    out[nout++] = BRACHA87_ECHO_ALL;
  if (steps & (1 << BRACHA87_READY_ALL))
    out[nout++] = BRACHA87_READY_ALL;
  if (steps & (1 << BRACHA87_ACCEPT))
    out[nout++] = BRACHA87_ACCEPT;
  if (steps & (1 << BRACHA87_ABANDON))
    out[nout++] = BRACHA87_ABANDON;
  /* the replies, to the sender of this arrival, and the answer to the
   * readies held; each reply row reads a step taken before the event,
   * the answer's row one taken before it and one taken on it */
  if (replyEcho)
    out[nout++] = BRACHA87_ECHO_REPLY;
  if (replyReady)
    out[nout++] = BRACHA87_READY_REPLY;
  if (answerReady)
    out[nout++] = BRACHA87_READY_ANSWER;
  if (evaluated) {
    if (retryI)
      out[nout++] = BRACHA87_INITIAL_ALL;
    if (retryE && !(steps & (1 << BRACHA87_ECHO_ALL)))
      out[nout++] = BRACHA87_ECHO_ALL;
    if (retryR && !(steps & (1 << BRACHA87_READY_ALL)))
      out[nout++] = BRACHA87_READY_ALL;
  }
  return (nout);
}

const unsigned char *
bracha87Fig1Value(
  const struct bracha87Fig1 *f
 ,unsigned char act
){
  if (!f)
    return (0);
  switch (act) {
  case BRACHA87_INITIAL_ALL:
    return (f->flags & (BRACHA87_F1_INITIATOR | BRACHA87_F1_INITIAL) ? INIT(f) : 0);
  case BRACHA87_ECHO_ALL:
  case BRACHA87_ECHO_REPLY:
    return (f->flags & BRACHA87_F1_ECHOED ? ECHOED(f) : 0);
  case BRACHA87_READY_ALL:
  case BRACHA87_READY_REPLY:
  case BRACHA87_READY_ANSWER:
    return (f->flags & BRACHA87_F1_RDSENT ? READIED(f) : 0);
  case BRACHA87_ACCEPT:
    return (f->flags & BRACHA87_F1_ACCEPTED ? ACCEPTED(f) : 0);
  default:
    return (0);
  }
}

int
bracha87Fig1Holder(
  const struct bracha87Fig1 *f
 ,unsigned char p
){
  if (!f || p > f->n1)
    return (0);
  return (BRACHA87_BIT_TST(HOLDSET(f), p) != 0);
}

int
bracha87Fig1EchoRecipient(
  const struct bracha87Fig1 *f
 ,unsigned char p
){
  if (!f || p > f->n1)
    return (0);
  return (!(f->flags & BRACHA87_F1_ACCEPTED));
}

int
bracha87Fig1Recipient(
  const struct bracha87Fig1 *f
 ,unsigned char p
){
  if (!f || p > f->n1)
    return (0);
  return (!BRACHA87_BIT_TST(ACCSET(f), p));
}

int
bracha87Fig1ReadySender(
  const struct bracha87Fig1 *f
 ,unsigned char p
){
  if (!f || p > f->n1)
    return (0);
  return (BRACHA87_BIT_TST(READYSET(f), p) != 0);
}

const unsigned char *
bracha87Fig1Received(
  const struct bracha87Fig1 *f
){
  if (!f)
    return (0);
  return (ACCSET(f));
}

unsigned int
bracha87Fig1State(
  const struct bracha87Fig1 *f
){
  if (!f)
    return (0);
  return (f->flags);
}

unsigned int
bracha87Fig1Sweeps(
  const struct bracha87Fig1 *f
){
  if (!f)
    return (0);
  return (f->sweeps);
}
