/*
 * asynchronousByzantineAgreementProtocols - system obligation layer
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
#include "system.h"

/*
 * data[] layout: the frontier round's witness book, then (w + 1)
 * retained-round entries:
 *
 *   offset 0            wit bitmap, bs bytes (O3: distinct servers
 *                       whose served assertion matched the caller's
 *                       candidate; cleared by complete and reset)
 *   offset bs           the entries, each:
 *     offset 0            round (the round byte this entry represents)
 *     offset 1            in use (0 = free)
 *     offset 2            possess bitmap, bs bytes
 *     offset 2 + bs       want bitmap, bs bytes (still-owed = want
 *                         bits; possession recording clears them)
 *     offset 2 + 2*bs     have bitmap, bs bytes (O2: members whose
 *                         content this process holds and can serve)
 *
 * where bs = (n + 8) / 8 holds one bit per process (actual count
 * n + 1).  Entries are unordered; lookups scan (bounded 256 entries,
 * at per-event rates).  A stored retained count, oldest cursor, or
 * witness count would be a denormalization -- all are derived.
 */

/* Round-class constants for the systemRules.c dispatch (see
 * systemToC.dtc; values are file-local, compared only by the
 * generated tests). */
#define SYSTEM_ROUND_RETAINED 0
#define SYSTEM_ROUND_RELEASED 1
#define SYSTEM_ROUND_LIVE     2
#define SYSTEM_ROUND_AHEAD    3

#define SYSTEM_SET(map, p) ((map)[(p) >> 3] |= (unsigned char)(1 << ((p) & 7)))
#define SYSTEM_CLR(map, p) ((map)[(p) >> 3] &= (unsigned char)~(1 << ((p) & 7)))

static unsigned int
sysBs(
  const struct system *s
){
  return (((unsigned int)s->n + 8) / 8);
}

static unsigned char *
sysEnt(
  struct system *s
 ,unsigned int i
){
  return (s->data + sysBs(s) + i * (2 + 3 * sysBs(s)));
}

/* Slot holding 'round', or w + 1 when none does. */
static unsigned int
sysFind(
  const struct system *s
 ,unsigned char round
){
  unsigned int W;
  unsigned int i;
  const unsigned char *e;

  W = (unsigned int)s->w + 1;
  for (i = 0; i < W; ++i) {
    e = s->data + sysBs(s) + i * (2 + 3 * sysBs(s));
    if (e[1] && e[0] == round)
      return (i);
  }
  return (W);
}

/* Count of set bits over the n + 1 processes of a bitmap. */
static unsigned int
sysPop(
  const struct system *s
 ,const unsigned char *m
){
  unsigned int p;
  unsigned int c;

  c = 0;
  for (p = 0; p <= (unsigned int)s->n; ++p)
    if (SYSTEM_TST(m, p))
      ++c;
  return (c);
}

/* Count of processes in entry i's possession bitmap. */
static unsigned int
sysCnt(
  const struct system *s
 ,unsigned int i
){
  return (sysPop(s, s->data + sysBs(s) + i * (2 + 3 * sysBs(s)) + 2));
}

/* 1 iff entry i's possession bitmap covers all n + 1 processes. */
static unsigned int
sysAll(
  const struct system *s
 ,unsigned int i
){
  return (sysCnt(s, i) == (unsigned int)s->n + 1);
}

/*
 * The R4 advance signal: the possession class of the round below
 * the frontier (SYSTEM_DUTY_*).  A round no longer retained reads
 * as met -- duty is bounded by retention.
 */
static unsigned int
sysPrior(
  const struct system *s
){
  unsigned int i;
  unsigned int c;

  if ((i = sysFind(s, (unsigned char)(s->frontier - 1)))
   >= (unsigned int)s->w + 1)
    return (SYSTEM_DUTY_MET);
  c = sysCnt(s, i);
  if (c >= (unsigned int)s->n + 1)
    return (SYSTEM_DUTY_MET);
  if (c >= (unsigned int)s->n + 1 - s->t)
    return (SYSTEM_DUTY_TOLERANCE);
  return (SYSTEM_DUTY_HELD);
}

unsigned long
systemSz(
  unsigned int n
 ,unsigned int w
){
  return (sizeof (struct system) - 1 + (((unsigned long)n + 8) / 8)
   + ((unsigned long)w + 1) * (2 + 3 * (((unsigned long)n + 8) / 8)));
}

void
systemInit(
  struct system *s
 ,unsigned char n
 ,unsigned char t
 ,unsigned char w
 ,unsigned char self
){
  if (!s)
    return;
  memset(s, 0, systemSz(n, w));
  if (self > n || !n || (unsigned int)n + 1 < 3u * t + 1) {
    s->self = 1; /* rejected: self > n (== 0) marks it; entries are inert */
    return;
  }
  s->n = n;
  s->t = t;
  s->w = w;
  s->self = self;
}

unsigned int
systemReceived(
  struct system *s
 ,unsigned char round
 ,unsigned char from
 ,unsigned char possesses
 ,struct systemAct *out
){
  unsigned int W;
  unsigned int i;
  unsigned int nact;
  unsigned char *e;
  unsigned char roundClass;
  unsigned char senderPossesses;
  unsigned char instanceLive;
  unsigned char participationOwed;
  unsigned char valuePending;
  unsigned char backlogDrained;
  unsigned char allPossess;
  unsigned char budgetExceeded;
  unsigned char oldestRetained;
  unsigned char priorPossession;
  unsigned char toleranceElapsed;
  unsigned char doDeliver;
  unsigned char doServeOwed;
  unsigned char doParticipationOwed;
  unsigned char maintenanceDue;
  unsigned char doJoin;
  unsigned char doMaintain;
  unsigned char doAdmit;
  unsigned char doRelease;

  if (!s || !out || s->self > s->n || from > s->n)
    return (0);
  W = (unsigned int)s->w + 1;

  /*
   * Possession record first, dispatch second (reads following
   * writes): a process announcing it holds the round must not be
   * misread as wanting it.  The record marks only 'from' itself --
   * the Byzantine containment argument in system.h.
   */
  i = sysFind(s, round);
  if (possesses && i < W) {
    e = sysEnt(s, i);
    SYSTEM_SET(e + 2, from);
    SYSTEM_CLR(e + 2 + sysBs(s), from);
  }

  senderPossesses = 0;
  allPossess = 0;
  if (i < W) {
    roundClass = SYSTEM_ROUND_RETAINED;
    e = sysEnt(s, i);
    if (SYSTEM_TST(e + 2, from))
      senderPossesses = 1;
    if (sysAll(s, i))
      allPossess = 1;
  } else if (round == s->frontier) {
    roundClass = (s->flags & SYSTEM_F_LIVE)
                 ? SYSTEM_ROUND_LIVE : SYSTEM_ROUND_AHEAD;
  } else if ((unsigned char)(round - s->frontier) < 128) {
    /*
     * Beyond chain reach: rounds ahead of the frontier cannot have
     * been verified by the caller, so they create no obligation
     * (system.md, existence evidence).  Inert, like the released
     * class -- which also absorbs the wrap ambiguity (a round more
     * than 127 behind an empty window is indistinguishable from
     * one ahead; both are inert, so the ambiguity is harmless).
     */
    return (0);
  } else {
    roundClass = SYSTEM_ROUND_RELEASED;
  }

  instanceLive = (s->flags & SYSTEM_F_LIVE) ? 1 : 0;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = 0;
  backlogDrained = 0;
  budgetExceeded = 0;
  oldestRetained = 0;
  priorPossession = SYSTEM_DUTY_MET;
  toleranceElapsed = 0;
  doDeliver = 0;
  doServeOwed = 0;
  doParticipationOwed = 0;
  maintenanceDue = 0;
  doJoin = 0;
  doMaintain = 0;
  doAdmit = 0;
  doRelease = 0;
#include "systemRules.c"
  /* launch outputs are not applied at a received event (the
   * dispatch may resolve them under this site's inert launch
   * inputs; the discriminator discards them) */
  (void)doJoin;
  (void)doMaintain;
  (void)doAdmit;

  nact = 0;
  if (doDeliver) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_DELIVER;
    out[nact].round = round;
    ++nact;
  }
  if (doServeOwed) {
    e = sysEnt(s, i);
    SYSTEM_SET(e + 2 + sysBs(s), from);
    out[nact].want = e + 2 + sysBs(s);
    out[nact].have = e + 2 + 2 * sysBs(s);
    out[nact].act = SYSTEM_ACT_SERVE;
    out[nact].round = round;
    ++nact;
  }
  if (doParticipationOwed)
    s->flags |= SYSTEM_F_OWED;
  if (doRelease) {
    e = sysEnt(s, i);
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = round;
    ++nact;
  }
  return (nact);
}

unsigned int
systemLaunch(
  struct system *s
 ,unsigned char vp
 ,unsigned char md
 ,unsigned char bd
 ,unsigned char te
 ,struct systemAct *out
){
  unsigned int nact;
  unsigned char roundClass;
  unsigned char senderPossesses;
  unsigned char instanceLive;
  unsigned char participationOwed;
  unsigned char valuePending;
  unsigned char backlogDrained;
  unsigned char allPossess;
  unsigned char budgetExceeded;
  unsigned char oldestRetained;
  unsigned char priorPossession;
  unsigned char toleranceElapsed;
  unsigned char doDeliver;
  unsigned char doServeOwed;
  unsigned char doParticipationOwed;
  unsigned char maintenanceDue;
  unsigned char doJoin;
  unsigned char doMaintain;
  unsigned char doAdmit;
  unsigned char doRelease;

  if (!s || !out || s->self > s->n)
    return (0);

  roundClass = SYSTEM_ROUND_RELEASED; /* inert for received-event rules */
  senderPossesses = 1;
  instanceLive = (s->flags & SYSTEM_F_LIVE) ? 1 : 0;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = vp ? 1 : 0;
  backlogDrained = bd ? 1 : 0;
  allPossess = 0;
  budgetExceeded = 0;
  oldestRetained = 0;
  priorPossession = sysPrior(s);
  toleranceElapsed = te ? 1 : 0;
  doDeliver = 0;
  doServeOwed = 0;
  doParticipationOwed = 0;
  maintenanceDue = md ? 1 : 0;
  doJoin = 0;
  doMaintain = 0;
  doAdmit = 0;
  doRelease = 0;
#include "systemRules.c"
  /* received/retention outputs are unreachable on a launch event */
  (void)doDeliver;
  (void)doServeOwed;
  (void)doParticipationOwed;
  (void)doRelease;

  nact = 0;
  if (doJoin) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_JOIN;
    out[nact].round = s->frontier;
    ++nact;
    s->flags |= SYSTEM_F_LIVE;
    s->flags &= (unsigned char)~SYSTEM_F_OWED;
  }
  if (doMaintain) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_MAINTAIN;
    out[nact].round = s->frontier;
    ++nact;
    s->flags |= SYSTEM_F_LIVE;
  }
  if (doAdmit) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_ADMIT;
    out[nact].round = s->frontier;
    ++nact;
    s->flags |= SYSTEM_F_LIVE;
  }
  return (nact);
}

unsigned int
systemComplete(
  struct system *s
 ,unsigned char round
 ,const unsigned char *have
 ,struct systemAct *out
){
  unsigned int W;
  unsigned int i;
  unsigned int free_;
  unsigned int old;
  unsigned int nact;
  unsigned char d;
  unsigned char best;
  unsigned char *e;
  unsigned char roundClass;
  unsigned char senderPossesses;
  unsigned char instanceLive;
  unsigned char participationOwed;
  unsigned char valuePending;
  unsigned char backlogDrained;
  unsigned char allPossess;
  unsigned char budgetExceeded;
  unsigned char oldestRetained;
  unsigned char priorPossession;
  unsigned char toleranceElapsed;
  unsigned char doDeliver;
  unsigned char doServeOwed;
  unsigned char doParticipationOwed;
  unsigned char maintenanceDue;
  unsigned char doJoin;
  unsigned char doMaintain;
  unsigned char doAdmit;
  unsigned char doRelease;

  if (!s || !out || s->self > s->n || !(s->flags & SYSTEM_F_LIVE)
   || round != s->frontier)
    return (0);
  W = (unsigned int)s->w + 1;
  nact = 0;

  /*
   * Round-space wrap boundary (encoding-derived, like the 254-round
   * cap -- a structural guard, not an obligation rule): an entry 256
   * rounds behind the frontier-to-be carries the same round byte the
   * stream is about to reuse, so it must release before its identity
   * recurs.  Releasing it also frees a slot, so the budget rule
   * below cannot fire on the same call (SYSTEM_MAX_ACTS holds).
   */
  if ((i = sysFind(s, (unsigned char)(s->frontier + 1))) < W) {
    e = sysEnt(s, i);
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = (unsigned char)(s->frontier + 1);
    ++nact;
  }

  free_ = W;
  for (i = 0; i < W; ++i)
    if (!*(sysEnt(s, i) + 1)) {
      free_ = i;
      break;
    }

  roundClass = SYSTEM_ROUND_RELEASED; /* inert for received-event rules */
  senderPossesses = 1;
  instanceLive = 1;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = 0;
  backlogDrained = 0;
  allPossess = 0;
  budgetExceeded = (free_ == W) ? 1 : 0;
  oldestRetained = 1;
  priorPossession = SYSTEM_DUTY_MET;
  toleranceElapsed = 0;
  doDeliver = 0;
  doServeOwed = 0;
  doParticipationOwed = 0;
  maintenanceDue = 0;
  doJoin = 0;
  doMaintain = 0;
  doAdmit = 0;
  doRelease = 0;
#include "systemRules.c"
  (void)doDeliver;
  (void)doServeOwed;
  (void)doParticipationOwed;
  (void)doJoin;
  (void)doMaintain;
  (void)doAdmit;

  if (doRelease) {
    /* evict the oldest retained round (largest distance behind the
     * frontier); its slot receives the new entry */
    old = 0;
    best = 0;
    for (i = 0; i < W; ++i) {
      e = sysEnt(s, i);
      if (e[1] && (d = (unsigned char)(s->frontier - e[0])) >= best) {
        best = d;
        old = i;
      }
    }
    e = sysEnt(s, old);
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = e[0];
    ++nact;
    free_ = old;
  }

  e = sysEnt(s, free_);
  e[0] = s->frontier;
  e[1] = 1;
  memset(e + 2, 0, 3 * sysBs(s));
  SYSTEM_SET(e + 2, s->self);
  if (have)
    memcpy(e + 2 + 2 * sysBs(s), have, sysBs(s));
  /*
   * The one consume region: a live COMPLETE and an adoption close
   * both land here, so completion clears the witness book either
   * way -- a racing own COMPLETE supersedes the accumulation
   * structurally.
   */
  memset(s->data, 0, sysBs(s));
  ++s->frontier;
  s->flags &= (unsigned char)~(SYSTEM_F_LIVE | SYSTEM_F_OWED | SYSTEM_F_ADOPT);
  return (nact);
}

unsigned int
systemPossessed(
  struct system *s
 ,unsigned char round
 ,unsigned char from
 ,struct systemAct *out
){
  unsigned int W;
  unsigned int i;
  unsigned int nact;
  unsigned char *e;
  unsigned char roundClass;
  unsigned char senderPossesses;
  unsigned char instanceLive;
  unsigned char participationOwed;
  unsigned char valuePending;
  unsigned char backlogDrained;
  unsigned char allPossess;
  unsigned char budgetExceeded;
  unsigned char oldestRetained;
  unsigned char priorPossession;
  unsigned char toleranceElapsed;
  unsigned char doDeliver;
  unsigned char doServeOwed;
  unsigned char doParticipationOwed;
  unsigned char maintenanceDue;
  unsigned char doJoin;
  unsigned char doMaintain;
  unsigned char doAdmit;
  unsigned char doRelease;

  if (!s || !out || s->self > s->n || from > s->n)
    return (0);
  W = (unsigned int)s->w + 1;
  if ((i = sysFind(s, round)) >= W)
    return (0);
  e = sysEnt(s, i);
  SYSTEM_SET(e + 2, from);
  SYSTEM_CLR(e + 2 + sysBs(s), from);

  roundClass = SYSTEM_ROUND_RETAINED;
  senderPossesses = 1;
  instanceLive = (s->flags & SYSTEM_F_LIVE) ? 1 : 0;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = 0;
  backlogDrained = 0;
  allPossess = sysAll(s, i) ? 1 : 0;
  budgetExceeded = 0;
  oldestRetained = 0;
  priorPossession = SYSTEM_DUTY_MET;
  toleranceElapsed = 0;
  doDeliver = 0;
  doServeOwed = 0;
  doParticipationOwed = 0;
  maintenanceDue = 0;
  doJoin = 0;
  doMaintain = 0;
  doAdmit = 0;
  doRelease = 0;
#include "systemRules.c"
  (void)doDeliver;
  (void)doServeOwed;
  (void)doParticipationOwed;
  (void)doJoin;
  (void)doMaintain;
  (void)doAdmit;

  nact = 0;
  if (doRelease) {
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = round;
    ++nact;
  }
  return (nact);
}

unsigned int
systemEvict(
  struct system *s
 ,struct systemAct *out
){
  unsigned int W;
  unsigned int i;
  unsigned int old;
  unsigned int any;
  unsigned int nact;
  unsigned char d;
  unsigned char best;
  unsigned char *e;
  unsigned char roundClass;
  unsigned char senderPossesses;
  unsigned char instanceLive;
  unsigned char participationOwed;
  unsigned char valuePending;
  unsigned char backlogDrained;
  unsigned char allPossess;
  unsigned char budgetExceeded;
  unsigned char oldestRetained;
  unsigned char priorPossession;
  unsigned char toleranceElapsed;
  unsigned char doDeliver;
  unsigned char doServeOwed;
  unsigned char doParticipationOwed;
  unsigned char maintenanceDue;
  unsigned char doJoin;
  unsigned char doMaintain;
  unsigned char doAdmit;
  unsigned char doRelease;

  if (!s || !out || s->self > s->n)
    return (0);
  W = (unsigned int)s->w + 1;
  any = 0;
  old = 0;
  best = 0;
  for (i = 0; i < W; ++i) {
    e = sysEnt(s, i);
    if (e[1] && (d = (unsigned char)(s->frontier - e[0])) >= best) {
      best = d;
      old = i;
      any = 1;
    }
  }
  if (!any)
    return (0);

  roundClass = SYSTEM_ROUND_RETAINED;
  senderPossesses = 1;
  instanceLive = (s->flags & SYSTEM_F_LIVE) ? 1 : 0;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = 0;
  backlogDrained = 0;
  allPossess = 0;
  budgetExceeded = 1;
  oldestRetained = 1;
  priorPossession = SYSTEM_DUTY_MET;
  toleranceElapsed = 0;
  doDeliver = 0;
  doServeOwed = 0;
  doParticipationOwed = 0;
  maintenanceDue = 0;
  doJoin = 0;
  doMaintain = 0;
  doAdmit = 0;
  doRelease = 0;
#include "systemRules.c"
  (void)doDeliver;
  (void)doServeOwed;
  (void)doParticipationOwed;
  (void)doJoin;
  (void)doMaintain;
  (void)doAdmit;

  nact = 0;
  if (doRelease) {
    e = sysEnt(s, old);
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = e[0];
    ++nact;
  }
  return (nact);
}

unsigned int
systemWitness(
  struct system *s
 ,unsigned char round
 ,unsigned char from
 ,struct systemAct *out
){
  if (!s || !out || s->self > s->n || from > s->n || from == s->self)
    return (0);
  if (round != s->frontier)
    return (0);
  SYSTEM_SET(s->data, from);
  /*
   * The O3 adopt rule as a C guard -- an independent rule set kept
   * out of the merged dispatch (system.dtc, witness-event section):
   * adopt iff post-record distinct witnesses reach t+1 and adoption
   * is not already signaled.  Single-fire via the latch.
   */
  if (sysPop(s, s->data) >= (unsigned int)s->t + 1
   && !(s->flags & SYSTEM_F_ADOPT)) {
    s->flags |= SYSTEM_F_ADOPT;
    out->want = 0;
    out->have = 0;
    out->act = SYSTEM_ACT_ADOPT;
    out->round = s->frontier;
    return (1);
  }
  return (0);
}

void
systemWitnessReset(
  struct system *s
){
  if (!s || s->self > s->n)
    return;
  memset(s->data, 0, sysBs(s));
  s->flags &= (unsigned char)~SYSTEM_F_ADOPT;
}

void
systemAssembled(
  struct system *s
 ,unsigned char round
 ,unsigned char member
){
  unsigned int i;

  if (!s || s->self > s->n || member > s->n)
    return;
  if ((i = sysFind(s, round)) >= (unsigned int)s->w + 1)
    return;
  SYSTEM_SET(sysEnt(s, i) + 2 + 2 * sysBs(s), member);
}

unsigned int
systemServe(
  struct system *s
 ,unsigned char *cursor
 ,struct systemAct *out
){
  unsigned int W;
  unsigned int bs;
  unsigned int i;
  unsigned int k;
  unsigned int b;
  unsigned char *e;

  if (!s || !cursor || !out || s->self > s->n)
    return (0);
  W = (unsigned int)s->w + 1;
  bs = sysBs(s);
  for (k = 0; k < W; ++k) {
    i = ((unsigned int)*cursor + k) % W;
    e = sysEnt(s, i);
    if (!e[1])
      continue;
    for (b = 0; b < bs; ++b)
      if (*(e + 2 + bs + b)) {
        out->want = e + 2 + bs;
        out->have = e + 2 + 2 * bs;
        out->act = SYSTEM_ACT_SERVE;
        out->round = e[0];
        *cursor = (unsigned char)((i + 1) % W);
        return (1);
      }
  }
  return (0);
}

unsigned char
systemFrontier(
  const struct system *s
){
  return (s ? s->frontier : 0);
}

unsigned int
systemLive(
  const struct system *s
){
  return (s && (s->flags & SYSTEM_F_LIVE) ? 1 : 0);
}

unsigned int
systemOwed(
  const struct system *s
){
  return (s && (s->flags & SYSTEM_F_OWED) ? 1 : 0);
}

unsigned int
systemDuty(
  const struct system *s
){
  if (!s || s->self > s->n)
    return (SYSTEM_DUTY_MET);
  return (sysPrior(s));
}

unsigned int
systemRetained(
  const struct system *s
 ,unsigned char round
){
  return (s && sysFind(s, round) <= s->w ? 1 : 0);
}

const unsigned char *
systemPossess(
  const struct system *s
 ,unsigned char round
){
  unsigned int i;

  if (!s || (i = sysFind(s, round)) > s->w)
    return (0);
  return (s->data + sysBs(s) + i * (2 + 3 * sysBs(s)) + 2);
}

const unsigned char *
systemWant(
  const struct system *s
 ,unsigned char round
){
  unsigned int i;

  if (!s || (i = sysFind(s, round)) > s->w)
    return (0);
  return (s->data + sysBs(s)
   + i * (2 + 3 * sysBs(s)) + 2 + sysBs(s));
}

const unsigned char *
systemHave(
  const struct system *s
 ,unsigned char round
){
  unsigned int i;

  if (!s || (i = sysFind(s, round)) > s->w)
    return (0);
  return (s->data + sysBs(s)
   + i * (2 + 3 * sysBs(s)) + 2 + 2 * sysBs(s));
}

const unsigned char *
systemWitnesses(
  const struct system *s
){
  if (!s || s->self > s->n)
    return (0);
  return (s->data);
}
