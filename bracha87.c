/*
 * BrachaAsynchronousByzantineAgreementProtocols - Gabriel Bracha Asynchronous Byzantine Agreement Protocols
 * Copyright (C) 2026 G. David Butler <gdb@dbSystems.com>
 *
 * This file is part of BrachaAsynchronousByzantineAgreementProtocols
 *
 * BrachaAsynchronousByzantineAgreementProtocols is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * BrachaAsynchronousByzantineAgreementProtocols is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * INFORMATION AND COMPUTATION 75, 130-143 (1987)
 * Gabriel Bracha - Asynchronous Byzantine Agreement Protocols
 *
 * Pure state machines for Figures 1, 2, 3, and 4.
 */

#include <string.h>
#include "bracha87.h"

/*************************************************************************/
/*  Figure 1 — Reliable broadcast primitive                              */
/*************************************************************************/

/*
 * B_N: actual process count (n + 1, range 1..256).
 * Works for any struct with an unsigned char n field.
 *
 * F1_VLEN: actual value length (vLen + 1, range 1..256).
 *
 * Layout (N = B_N, L = F1_VLEN):
 *   value[L]             committed value
 *   ecFrom[N]            echo-sent flag per peer
 *   ecVal[N * L]         echo value per peer
 *   rdFrom[N]            ready-sent flag per peer
 *   rdVal[N * L]         ready value per peer
 */

#define B_N(b)       ((unsigned int)(b)->n + 1)
#define F1_VLEN(b)   ((unsigned int)(b)->vLen + 1)
#define F1_VALUE(b)  ((b)->data)
#define F1_ECFROM(b) ((b)->data + F1_VLEN(b))
#define F1_ECVAL(b)  ((b)->data + F1_VLEN(b) + B_N(b))
#define F1_RDFROM(b) ((b)->data + F1_VLEN(b) + B_N(b) + (unsigned long)B_N(b) * F1_VLEN(b))
#define F1_RDVAL(b)  ((b)->data + F1_VLEN(b) + B_N(b) + (unsigned long)B_N(b) * F1_VLEN(b) + B_N(b))

unsigned long
bracha87Fig1Sz(
  unsigned int n
 ,unsigned int vLen
){
  unsigned int N;
  unsigned int L;

  /* actual counts: n/vLen encode 0..255 as 1..256 */
  N = n + 1;
  L = vLen + 1;
  /* struct up to data[1] minus the 1, plus layout */
  return (sizeof (struct bracha87Fig1) - 1
    + L                       /* value */
    + N                       /* ecFrom */
    + (unsigned long)N * L    /* ecVal */
    + N                       /* rdFrom */
    + (unsigned long)N * L);  /* rdVal */
}

void
bracha87Fig1Init(
  struct bracha87Fig1 *b
 ,unsigned char n
 ,unsigned char t
 ,unsigned char vLen
){
  memset(b, 0, bracha87Fig1Sz(n, vLen));
  b->n = n;
  b->t = t;
  b->vLen = vLen;
}

/*
 * Count echoes for value v by scanning per-peer records.
 * This faithfully implements echo_count[v] from the paper.
 */
static unsigned int
fig1EcCnt(
  const struct bracha87Fig1 *b
 ,const unsigned char *v
){
  const unsigned char *ef;
  const unsigned char *ev;
  unsigned int cnt;
  unsigned int j;

  ef = F1_ECFROM(b);
  ev = F1_ECVAL(b);
  cnt = 0;
  for (j = 0; j < B_N(b); ++j)
    if (ef[j] && !memcmp(ev + (unsigned long)j * F1_VLEN(b), v, F1_VLEN(b)))
      ++cnt;
  return (cnt);
}

/*
 * Count readys for value v by scanning per-peer records.
 * This faithfully implements ready_count[v] from the paper.
 */
static unsigned int
fig1RdCnt(
  const struct bracha87Fig1 *b
 ,const unsigned char *v
){
  const unsigned char *rf;
  const unsigned char *rv;
  unsigned int cnt;
  unsigned int j;

  rf = F1_RDFROM(b);
  rv = F1_RDVAL(b);
  cnt = 0;
  for (j = 0; j < B_N(b); ++j)
    if (rf[j] && !memcmp(rv + (unsigned long)j * F1_VLEN(b), v, F1_VLEN(b)))
      ++cnt;
  return (cnt);
}

/*
 * Record that peer 'from' sent echo with value v.
 */
static void
fig1SetEc(
  struct bracha87Fig1 *b
 ,unsigned char from
 ,const unsigned char *v
){
  F1_ECFROM(b)[from] = 1;
  memcpy(F1_ECVAL(b) + (unsigned long)from * F1_VLEN(b), v, F1_VLEN(b));
}

/*
 * Record that peer 'from' sent ready with value v.
 */
static void
fig1SetRd(
  struct bracha87Fig1 *b
 ,unsigned char from
 ,const unsigned char *v
){
  F1_RDFROM(b)[from] = 1;
  memcpy(F1_RDVAL(b) + (unsigned long)from * F1_VLEN(b), v, F1_VLEN(b));
}

/*
 * Commit to value v (set echoed, store committed value).
 */
static void
fig1Commit(
  struct bracha87Fig1 *b
 ,const unsigned char *v
){
  memcpy(F1_VALUE(b), v, F1_VLEN(b));
  b->echoed = 1;
}


unsigned int
bracha87Fig1Input(
  struct bracha87Fig1 *b
 ,unsigned char type
 ,unsigned char from
 ,const unsigned char *value
 ,unsigned char *out
){
  unsigned int nout;
  unsigned int ec;
  unsigned int rd;

  if (!b || !value || !out || from > b->n || b->accepted)
    return (0);

  nout = 0;

  switch (type) {

  case BRACHA87_INITIAL:
    /*
     * Rule 1: in(initial, v) from p, !echoed -> echo(v) to all
     *
     * Initial comes from the broadcast origin. If we haven't
     * echoed yet, commit to this value and echo it.
     */
    if (b->echoed)
      return (0);
    fig1Commit(b, value);
    out[nout++] = BRACHA87_ECHO_ALL;
    /*
     * No cascade needed: Rules 4/5 require echo/ready counts that
     * can't have reached their thresholds while !echoed was true
     * (otherwise Rule 2 or 3 would have already set echoed, and
     * we wouldn't be here). The initial changes no counts.
     */
    break;

  case BRACHA87_ECHO:
    /*
     * Deduplicate: one echo per sender.
     */
    if (F1_ECFROM(b)[from])
      return (0);
    fig1SetEc(b, from, value);
    ec = fig1EcCnt(b, value);

    /*
     * Rule 2: !echoed && echo_count[v] > (n+t)/2 -> echo(v) to all
     */
    if (!b->echoed
     && ec >= (B_N(b) + b->t) / 2 + 1) {
      fig1Commit(b, value);
      out[nout++] = BRACHA87_ECHO_ALL;
    }

    /*
     * Rule 4: echoed && !rdSent && echo_count[v] > (n+t)/2 -> ready(v)
     *
     * Fires for any value v meeting the threshold (paper's rule).
     * Updates committed value so caller broadcasts ready(v).
     */
    if (b->echoed && !b->rdSent
     && ec >= (B_N(b) + b->t) / 2 + 1) {
      memcpy(F1_VALUE(b), value, F1_VLEN(b));
      b->rdSent = 1;
      out[nout++] = BRACHA87_READY_ALL;
    }

    /* Rule 6 cascade: rdSent may now be true */
    if (b->rdSent && !b->accepted
     && fig1RdCnt(b, value) >= 2u * b->t + 1) {
      memcpy(F1_VALUE(b), value, F1_VLEN(b));
      b->accepted = 1;
      out[nout++] = BRACHA87_ACCEPT;
    }
    break;

  case BRACHA87_READY:
    /*
     * Deduplicate: one ready per sender.
     */
    if (F1_RDFROM(b)[from])
      return (0);
    fig1SetRd(b, from, value);
    rd = fig1RdCnt(b, value);

    /*
     * Rule 3: !echoed && ready_count[v] >= t+1 -> echo(v) to all
     */
    if (!b->echoed
     && rd >= (unsigned int)b->t + 1) {
      fig1Commit(b, value);
      out[nout++] = BRACHA87_ECHO_ALL;
    }

    /*
     * Rule 5: echoed && !rdSent && ready_count[v] >= t+1 -> ready(v)
     *
     * Fires for any value v meeting the threshold (paper's rule).
     * Updates committed value so caller broadcasts ready(v).
     */
    if (b->echoed && !b->rdSent
     && rd >= (unsigned int)b->t + 1) {
      memcpy(F1_VALUE(b), value, F1_VLEN(b));
      b->rdSent = 1;
      out[nout++] = BRACHA87_READY_ALL;
    }

    /*
     * Rule 6: rdSent && ready_count[v] >= 2t+1 -> accept(v)
     *
     * Fires for any value v with sufficient ready count.
     */
    if (b->rdSent && !b->accepted
     && rd >= 2u * b->t + 1) {
      memcpy(F1_VALUE(b), value, F1_VLEN(b));
      b->accepted = 1;
      out[nout++] = BRACHA87_ACCEPT;
    }
    break;

  default:
    return (0);
  }

  return (nout);
}

const unsigned char *
bracha87Fig1Value(
  const struct bracha87Fig1 *b
){
  if (!b || !b->echoed)
    return (0);
  return (F1_VALUE(b));
}

/*************************************************************************/
/*  Figure 2 — Abstract protocol round (0-based)                         */
/*************************************************************************/

/*
 * Per-round record: recvCount + complete + rcvs[n].
 */
#define F2_ROUND_SZ(n) (2 + (unsigned long)((n) + 1) * sizeof (struct bracha87Fig2Rcv))

#define F2_ROUND(b, k) ((b)->data + (k) * F2_ROUND_SZ((b)->n))
#define F2_RCNT(b, k)  (*(F2_ROUND((b), (k))))
#define F2_COMP(b, k)  (*(F2_ROUND((b), (k)) + 1))
#define F2_RCVS(b, k)  ((struct bracha87Fig2Rcv *)(F2_ROUND((b), (k)) + 2))

unsigned long
bracha87Fig2Sz(
  unsigned int n
 ,unsigned int maxRounds
){
  return (sizeof (struct bracha87Fig2) - 1
    + maxRounds * F2_ROUND_SZ(n));
}

void
bracha87Fig2Init(
  struct bracha87Fig2 *b
 ,unsigned char n
 ,unsigned char t
 ,unsigned char maxRounds
){
  memset(b, 0, bracha87Fig2Sz(n, maxRounds));
  b->n = n;
  b->t = t;
  b->maxRounds = maxRounds;
}

unsigned int
bracha87Fig2Receive(
  struct bracha87Fig2 *b
 ,unsigned char k
 ,unsigned char sender
 ,unsigned char value
){
  struct bracha87Fig2Rcv *rcvs;

  if (!b || k >= b->maxRounds || sender > b->n)
    return (0);

  rcvs = F2_RCVS(b, k);

  /* Deduplicate: one message per sender per round */
  if (rcvs[sender].received)
    return (0);

  rcvs[sender].sender = sender;
  rcvs[sender].value = value;
  rcvs[sender].received = 1;
  ++F2_RCNT(b, k);

  /* Check n-t threshold */
  if (!F2_COMP(b, k)
   && F2_RCNT(b, k) >= B_N(b) - b->t) {
    F2_COMP(b, k) = 1;
    return (BRACHA87_ROUND_COMPLETE);
  }

  return (0);
}

unsigned int
bracha87Fig2RecvCount(
  const struct bracha87Fig2 *b
 ,unsigned char k
){
  if (!b || k >= b->maxRounds)
    return (0);
  return (F2_RCNT(b, k));
}

unsigned int
bracha87Fig2GetReceived(
  const struct bracha87Fig2 *b
 ,unsigned char k
 ,unsigned char *senders
 ,unsigned char *values
){
  const struct bracha87Fig2Rcv *rcvs;
  unsigned int cnt;
  unsigned int i;

  if (!b || k >= b->maxRounds)
    return (0);

  rcvs = F2_RCVS(b, k);
  cnt = 0;
  for (i = 0; i < B_N(b); ++i) {
    if (rcvs[i].received) {
      if (senders)
        senders[cnt] = rcvs[i].sender;
      if (values)
        values[cnt] = rcvs[i].value;
      ++cnt;
    }
  }
  return (cnt);
}

/*************************************************************************/
/*  Figure 3 — Correctness enforcement (VALID sets, 0-based)             */
/*************************************************************************/

/*
 * Per-round record: validCount + complete + msgs[n].
 */
#define F3_ROUND_SZ(n) (2 + (unsigned long)((n) + 1) * sizeof (struct bracha87Msg))

#define F3_ROUND(b, k) ((b)->data + (k) * F3_ROUND_SZ((b)->n))
#define F3_VCNT(b, k)  (*(F3_ROUND((b), (k))))
#define F3_COMP(b, k)  (*(F3_ROUND((b), (k)) + 1))
#define F3_MSGS(b, k)  ((struct bracha87Msg *)(F3_ROUND((b), (k)) + 2))

unsigned long
bracha87Fig3Sz(
  unsigned int n
 ,unsigned int maxRounds
){
  return (sizeof (struct bracha87Fig3) - 1
    + maxRounds * F3_ROUND_SZ(n));
}

void
bracha87Fig3Init(
  struct bracha87Fig3 *b
 ,unsigned char n
 ,unsigned char t
 ,unsigned char maxRounds
 ,bracha87Nfn N
 ,void *Nclosure
){
  memset(b, 0, bracha87Fig3Sz(n, maxRounds));
  b->n = n;
  b->t = t;
  b->maxRounds = maxRounds;
  b->N = N;
  b->Nclosure = Nclosure;
}

/*
 * Check VALID^k predicate for a message (sender, k, value).
 *
 * VALID^0: accepted && value in {0, 1}
 * VALID^k (k>0): accepted && exists n-t in VALID^{k-1}
 *                s.t. value = N(k-1, set)
 */
static int
fig3IsValid(
  struct bracha87Fig3 *b
 ,unsigned char k
 ,unsigned char value
){
  unsigned int nt;
  unsigned int vc;
  unsigned char senders[256];
  unsigned char values[256];
  unsigned int i;
  unsigned int j;
  struct bracha87Msg *msgs;
  unsigned char result;

  if (k >= b->maxRounds)
    return (0);

  /* VALID^0: value in {0, 1} */
  if (k == 0)
    return (value <= 1);

  /* VALID^k, k > 0: need n-t validated messages from round k-1 */
  nt = B_N(b) - b->t;
  vc = F3_VCNT(b, k - 1);
  if (vc < nt)
    return (0);

  /*
   * Collect ALL validated messages from round k-1.
   * N receives the full set so it can determine whether different
   * n-t subsets could produce different results (existential check).
   */
  msgs = F3_MSGS(b, k - 1);
  j = 0;
  for (i = 0; i < B_N(b); ++i) {
    if (msgs[i].valid) {
      senders[j] = msgs[i].sender;
      values[j] = msgs[i].value;
      ++j;
    }
  }

  /* Check: value = N(k-1, set) */
  {
    int rc;

    rc = b->N(b->Nclosure, k - 1, j, senders, values, &result);
    if (rc < 0)
      return (0);
    if (rc > 0)
      return ((value & (unsigned char)~BRACHA87_D_FLAG) <= 1);
    return (result == value);
  }
}

unsigned int
bracha87Fig3Accept(
  struct bracha87Fig3 *b
 ,unsigned char k
 ,unsigned char sender
 ,unsigned char value
 ,unsigned int *validCount
){
  struct bracha87Msg *msgs;

  if (!b || k >= b->maxRounds || sender > b->n)
    return (0);

  msgs = F3_MSGS(b, k);

  /* Deduplicate: one accepted message per sender per round */
  if (msgs[sender].arrived)
    return (0);

  /* Record the accepted message (stored for re-evaluation) */
  msgs[sender].sender = sender;
  msgs[sender].value = value;
  msgs[sender].arrived = 1;

  /* Check VALID^k */
  if (!fig3IsValid(b, k, value)) {
    if (validCount)
      *validCount = F3_VCNT(b, k);
    return (0);
  }

  /* Mark valid */
  msgs[sender].valid = 1;
  ++F3_VCNT(b, k);

  /*
   * Fig 2 round coordination: when n-t validated, re-evaluate
   * stored messages from subsequent rounds.  VALID^{r} depends
   * on n-t in VALID^{r-1}, so completing round k may unblock
   * round k+1, which may unblock k+2, etc.
   */
  if (!F3_COMP(b, k)
   && F3_VCNT(b, k) >= B_N(b) - b->t) {
    unsigned int r;

    F3_COMP(b, k) = 1;
    for (r = (unsigned int)k + 1; r < b->maxRounds; ++r) {
      struct bracha87Msg *rm;
      unsigned int i;

      if (F3_COMP(b, r))
        break;
      rm = F3_MSGS(b, r);
      for (i = 0; i < B_N(b); ++i) {
        if (rm[i].arrived && !rm[i].valid
         && fig3IsValid(b, r, rm[i].value)) {
          rm[i].valid = 1;
          ++F3_VCNT(b, r);
        }
      }
      if (F3_VCNT(b, r) >= B_N(b) - b->t)
        F3_COMP(b, r) = 1;
      else
        break;
    }
  }

  if (validCount)
    *validCount = F3_VCNT(b, k);
  return (BRACHA87_VALIDATED);
}

unsigned int
bracha87Fig3ValidCount(
  const struct bracha87Fig3 *b
 ,unsigned char k
){
  if (!b || k >= b->maxRounds)
    return (0);
  return (F3_VCNT(b, k));
}

unsigned int
bracha87Fig3GetValid(
  const struct bracha87Fig3 *b
 ,unsigned char k
 ,unsigned char *senders
 ,unsigned char *values
){
  const struct bracha87Msg *msgs;
  unsigned int cnt;
  unsigned int i;

  if (!b || k >= b->maxRounds)
    return (0);

  msgs = F3_MSGS(b, k);
  cnt = 0;
  for (i = 0; i < B_N(b); ++i) {
    if (msgs[i].valid) {
      if (senders)
        senders[cnt] = msgs[i].sender;
      if (values)
        values[cnt] = msgs[i].value;
      ++cnt;
    }
  }
  return (cnt);
}

int
bracha87Fig3RoundComplete(
  const struct bracha87Fig3 *b
 ,unsigned char k
){
  if (!b || k >= b->maxRounds)
    return (0);
  return (F3_COMP(b, k) != 0);
}

/*************************************************************************/
/*  Figure 4 — Consensus protocol (0-based)                              */
/*************************************************************************/

/*
 * Figure 4 embeds a Figure 3 instance.
 * The N function implements the three round-specific functions:
 *   Round 3i   (sub 0): majority
 *   Round 3i+1 (sub 1): decision check
 *   Round 3i+2 (sub 2): (handled directly in fig4, N not needed)
 */

/*
 * The N function for Figure 4.
 * Implements the protocol function parameterizing Figure 3.
 *
 * Receives all validated messages for the round (may exceed n-t).
 * When n_msgs > n-t, checks whether different n-t subsets could
 * produce different results. If so, returns permissive (>0) to
 * implement the paper's existential quantifier in VALID^k.
 *
 * Round 3i   (sub 0): v = majority of the n-t values
 * Round 3i+1 (sub 1): v = (d, v') if >n/2 agree, else v unchanged
 * Round 3i+2 (sub 2): always valid (decision logic in Fig4)
 */
static int
fig4Nfn(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  struct bracha87Fig4 *f4 = (struct bracha87Fig4 *)closure;
  unsigned int nt;
  unsigned int cnt[2];
  unsigned int i;
  unsigned int sub;
  unsigned char v;

  (void)senders;

  if (!n_msgs)
    return (-1);

  nt = B_N(f4) - f4->t;
  sub = (unsigned int)(k % 3);
  cnt[0] = cnt[1] = 0;

  for (i = 0; i < n_msgs; ++i) {
    v = values[i] & (unsigned char)~BRACHA87_D_FLAG;
    if (v <= 1)
      ++cnt[v];
  }

  switch (sub) {

  case 0:
    /*
     * Round 3i (step 1): majority value.
     *
     * With exactly n-t messages (odd for binary), majority is unique.
     * With >n-t, different n-t subsets could have different majorities.
     * Value v has majority in some n-t subset iff cnt[v] >= nt/2+1.
     * If both values meet this threshold, return permissive.
     */
    *result = (cnt[1] > cnt[0]) ? 1 : 0;
    if (n_msgs > nt
     && cnt[0] >= nt / 2 + 1
     && cnt[1] >= nt / 2 + 1)
      return (1); /* permissive: subsets disagree on majority */
    break;

  case 1:
    /*
     * Round 3i+1 (step 2): if >n/2 agree on v, result = (d, v) exactly.
     * Otherwise any binary value is valid (process keeps own value,
     * which may differ across correct processes).
     *
     * With >n-t messages, a subset might see >n/2 when the full set
     * doesn't, or vice versa. Permissive if borderline:
     * the worst-case n-t subset for value v removes (n_msgs - nt)
     * copies of v. If that could drop below n/2, subsets disagree.
     */
    if (cnt[0] * 2 > B_N(f4)) {
      *result = 0 | BRACHA87_D_FLAG;
      if (n_msgs > nt
       && (cnt[0] - (n_msgs - nt)) * 2 <= B_N(f4))
        return (1); /* permissive: some subsets lack >n/2 */
      return (0);
    }
    if (cnt[1] * 2 > B_N(f4)) {
      *result = 1 | BRACHA87_D_FLAG;
      if (n_msgs > nt
       && (cnt[1] - (n_msgs - nt)) * 2 <= B_N(f4))
        return (1); /* permissive: some subsets lack >n/2 */
      return (0);
    }
    /*
     * No majority in full set. But some n-t subset might have >n/2
     * if cnt[v] > n/2. Always permissive here since the process
     * keeps its own value (non-deterministic across processes).
     */
    return (1);

  case 2:
    /*
     * Round 3i+2 (step 3): output depends on d-message counts.
     * If >2t d-messages for v in the full set, even the worst-case
     * n-t subset has >2t - (n_msgs - nt) = >2t - excess d-messages.
     * Exact only if all subsets agree; otherwise permissive.
     */
    {
      unsigned int dc2[2];
      unsigned char dm;
      unsigned int excess;

      dc2[0] = dc2[1] = 0;
      for (i = 0; i < n_msgs; ++i) {
        v = values[i] & (unsigned char)~BRACHA87_D_FLAG;
        if (v <= 1 && (values[i] & BRACHA87_D_FLAG))
          ++dc2[v];
      }
      dm = (dc2[1] > dc2[0]) ? 1 : 0;
      excess = (n_msgs > nt) ? n_msgs - nt : 0;
      if (dc2[dm] > 2u * f4->t) {
        /* Exact if worst-case subset still has >2t */
        if (dc2[dm] - excess > 2u * f4->t) {
          *result = dm;
          return (0);
        }
      }
      return (1); /* permissive: adopt or coin */
    }
  }

  return (0);
}

unsigned long
bracha87Fig4Sz(
  unsigned int n
 ,unsigned int maxPhases
){
  return (sizeof (struct bracha87Fig4) - 1
    + bracha87Fig3Sz(n, maxPhases * 3));
}

void
bracha87Fig4Init(
  struct bracha87Fig4 *b
 ,unsigned char n
 ,unsigned char t
 ,unsigned char maxPhases
 ,unsigned char initialValue
 ,bracha87CoinFn coin
 ,void *coinClosure
){
  memset(b, 0, bracha87Fig4Sz(n, maxPhases));
  b->n = n;
  b->t = t;
  b->maxPhases = maxPhases;
  b->phase = 0;
  b->subRound = 0;
  b->value = initialValue;
  b->decided = 0;
  b->decision = 0;
  b->coin = coin;
  b->coinClosure = coinClosure;
  bracha87Fig3Init(
    (struct bracha87Fig3 *)b->data
   ,n, t, (unsigned char)(maxPhases * 3), fig4Nfn, b);
}

unsigned int
bracha87Fig4Round(
  struct bracha87Fig4 *b
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
){
  unsigned int cnt[2];
  unsigned int dc[2];
  unsigned int sub;
  unsigned int ph;
  unsigned int i;
  unsigned char v;
  unsigned char dmax;

  (void)senders;
  if (!b || !n_msgs)
    return (0);

  sub = (unsigned int)(k % 3);
  ph = (unsigned int)(k / 3);

  /* Count base values (strip d flag) and d-flagged values separately */
  cnt[0] = cnt[1] = 0;
  dc[0] = dc[1] = 0;
  for (i = 0; i < n_msgs; ++i) {
    v = values[i] & (unsigned char)~BRACHA87_D_FLAG;
    if (v <= 1) {
      ++cnt[v];
      if (values[i] & BRACHA87_D_FLAG)
        ++dc[v];
    }
  }

  switch (sub) {

  case 0:
    /*
     * Phase step 1 (round 3i):
     *   value_p := majority value of the n-t validated messages.
     */
    b->value = (cnt[1] > cnt[0]) ? 1 : 0;
    b->phase = (unsigned char)ph;
    b->subRound = 1;
    return (BRACHA87_BROADCAST);

  case 1:
    /*
     * Phase step 2 (round 3i+1):
     *   If >n/2 same value v: value_p := (d, v)
     *   Otherwise: value_p unchanged
     *
     * Threshold: more than n/2 of the n-t validated messages.
     * The paper says "more than n/2 of the messages."
     */
    if (cnt[0] * 2 > B_N(b))
      b->value = 0 | BRACHA87_D_FLAG;
    else if (cnt[1] * 2 > B_N(b))
      b->value = 1 | BRACHA87_D_FLAG;
    /* else: value_p unchanged (no d flag) */
    b->phase = (unsigned char)ph;
    b->subRound = 2;
    return (BRACHA87_BROADCAST);

  case 2:
    /*
     * Phase step 3 (round 3i+2):
     *   Count only d-flagged messages (decision candidates).
     *
     *   (i)   If >2t d-messages with (d,v): decide v
     *   (ii)  If >t d-messages with (d,v): value_p := v
     *   (iii) Otherwise: value_p := coin
     *
     *   Paper: all three cases fall through to "Go to round 1
     *   of phase i+1." A decided process continues participating
     *   so that other correct processes can also reach consensus.
     */
    dmax = (dc[1] > dc[0]) ? 1 : 0;

    if (!b->decided) {
      /* (i) >2t d-messages with same value -> decide AND continue */
      if (dc[dmax] > 2u * b->t) {
        b->value = dmax;
        b->decision = dmax;
        b->decided = 1;
        if (ph + 1 >= b->maxPhases) {
          /* Decided at operational limit; cannot continue */
          return (BRACHA87_DECIDE);
        }
        b->phase = (unsigned char)(ph + 1);
        b->subRound = 0;
        return (BRACHA87_DECIDE | BRACHA87_BROADCAST);
      }

      /* Operational limit without decision */
      if (ph + 1 >= b->maxPhases)
        return (BRACHA87_EXHAUSTED);

      /* (ii) >t d-messages with same value -> adopt */
      if (dc[dmax] > (unsigned int)b->t)
        b->value = dmax;
      else
        /* (iii) coin flip */
        b->value = b->coin(b->coinClosure, (unsigned char)ph);
    }
    /* Already decided: value is fixed at decision */
    else {
      b->value = b->decision;
      if (ph + 1 >= b->maxPhases)
        return (0); /* already decided, cannot continue */
    }

    b->phase = (unsigned char)(ph + 1);
    b->subRound = 0;
    return (BRACHA87_BROADCAST);
  }

  return (0);
}
