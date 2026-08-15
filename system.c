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
 * data[] layout: three rs-byte round names and the frontier
 * round's witness book.  The retained rounds are NOT here -- they
 * are the caller's, reached through the four retention operations
 * (system.h):
 *
 *   offset 0            the frontier's name (rs bytes)
 *   offset rs           the last-closed round's name (rs bytes; the
 *                       round below the frontier, held as history
 *                       because predecessor is not an operation on
 *                       names.  No validity marker: before the
 *                       first close nothing is retained, so the
 *                       lookup on these zeroed bytes misses and
 *                       duty reads MET -- the genesis answer)
 *   offset 2rs          the morgue (rs + 1 bytes: a name and an
 *                       in-use byte): a round the machine directs
 *                       the store to drop moves its name here
 *                       first, because the RELEASE act naming it
 *                       is returned after the store no longer
 *                       holds it.  Every entry that writes machine
 *                       or record state zeroes it on the way past
 *                       its refusal guards -- borrows last until
 *                       the next call (system.h), and the zeroing
 *                       keeps the resting state canonical.
 *                       systemServe writes none of the machine's
 *                       state (the induction's SERVE case) and
 *                       leaves it, so a RELEASE borrow outlives a
 *                       serve call -- still within the contract,
 *                       which has the caller consume acts before
 *                       its next call.  The in-use byte
 *                       is what keeps the empty morgue and a
 *                       borrowed all-zero name DISTINCT states --
 *                       names are opaque, so no name value can be
 *                       the empty sentinel
 *   offset 3rs + 1      wit bitmap, bs bytes (O3: distinct servers
 *                       whose served assertion matched the caller's
 *                       candidate; cleared by complete and reset)
 *
 * A retained round's RECORDS, which the store holds and hands
 * back, are three bitmaps laid end to end by this machine:
 *
 *     offset 0            possess bitmap, bs bytes
 *     offset bs           want bitmap, bs bytes (still-owed = want
 *                         bits; possession recording clears them)
 *     offset 2bs          have bitmap, bs bytes (O2: members whose
 *                         content this process holds and can serve)
 *
 * where bs = (n + 8) / 8 holds one bit per process (actual count
 * n + 1) and rs is the caller's round-name size.  Names are opaque
 * bytes: every identity and order question is the caller's one
 * comparator's (equal and order from one authority); the machine
 * performs no arithmetic on a name.  A stored retained count,
 * oldest cursor, or witness count would be a denormalization --
 * all are derived, the first two by asking the store.
 */

/* Round-class constants for the systemRules.c dispatch (see
 * systemToC.dtc; values are file-local, compared only by the
 * generated tests).  AHEAD is entered only by the frontier round
 * awaiting its join -- the dtc's "R not yet reached"; traffic
 * genuinely ahead of the frontier is refused BEFORE the dispatch
 * (beyond chain reach). */
#define SYSTEM_ROUND_RETAINED 0
#define SYSTEM_ROUND_RELEASED 1
#define SYSTEM_ROUND_LIVE     2
#define SYSTEM_ROUND_AHEAD    3

#define SYSTEM_SET(map, p) ((map)[(p) >> 3] |= 1 << ((p) & 7))
#define SYSTEM_CLR(map, p) ((map)[(p) >> 3] &= ~(1 << ((p) & 7)))

static unsigned int
sysBs(
  const struct system *s
){
  return (((unsigned int)s->n + 8) / 8);
}

/* The one comparator: 0 same place, negative a precedes b,
 * positive a follows b. */
static int
sysCmp(
  const struct system *s
 ,const unsigned char *a
 ,const unsigned char *b
){
  return ((*s->cmp)(s->ctx, a, b));
}

static unsigned char *
sysFro(
  struct system *s
){
  return (s->data);
}

static unsigned char *
sysPri(
  struct system *s
){
  return (s->data + s->rs);
}

static unsigned char *
sysScr(
  struct system *s
){
  return (s->data + 2 * (unsigned long)s->rs);
}

static unsigned char *
sysWit(
  struct system *s
){
  return (s->data + 3 * (unsigned long)s->rs + 1);
}

/* The records the caller holds for 'round', or 0 if it retains no
 * such round. */
static unsigned char *
sysRec(
  const struct system *s
 ,const unsigned char *round
){
  return ((*s->records)(s->ctx, round));
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

/* 1 iff a round's possession bitmap covers all n + 1 processes. */
static unsigned int
sysAll(
  const struct system *s
 ,const unsigned char *r
){
  return (sysPop(s, r) == (unsigned int)s->n + 1);
}

/*
 * The R4 advance signal: the possession class of the round below
 * the frontier (SYSTEM_DUTY_*).  The round below the frontier is
 * the last-closed round -- history the machine holds, never a
 * computed predecessor.  A round no longer retained (or no round
 * closed yet) reads as met -- duty is bounded by retention.
 */
static unsigned int
sysPrior(
  const struct system *s
){
  const unsigned char *r;
  unsigned int c;

  if (!(r = sysRec(s, s->data + s->rs)))
    return (SYSTEM_DUTY_MET);
  c = sysPop(s, r);
  if (c >= (unsigned int)s->n + 1)
    return (SYSTEM_DUTY_MET);
  if (c >= (unsigned int)s->n + 1 - s->t)
    return (SYSTEM_DUTY_TOLERANCE);
  return (SYSTEM_DUTY_HELD);
}

unsigned long
systemSz(
  unsigned int n
 ,unsigned int rs
){
  return (sizeof (struct system) - 1 + 3 * (unsigned long)rs + 1
   + (((unsigned long)n + 8) / 8));
}

unsigned long
systemRecordsSz(
  unsigned int n
){
  return (3 * (((unsigned long)n + 8) / 8));
}

unsigned long
systemCursorSz(
  unsigned int rs
){
  return ((unsigned long)rs + 1);
}

void
systemInit(
  struct system *s
 ,unsigned char n
 ,unsigned char t
 ,unsigned char self
 ,unsigned int rs
 ,const unsigned char *genesis
 ,int (*cmp)(void *, const unsigned char *, const unsigned char *)
 ,unsigned char *(*records)(void *, const unsigned char *)
 ,unsigned char *(*retain)(void *, const unsigned char *)
 ,void (*release)(void *, const unsigned char *)
 ,unsigned char *(*after)(void *, const unsigned char *
                         ,const unsigned char **)
 ,void *ctx
){
  if (!s)
    return;
  memset(s, 0, systemSz(n, rs));
  if (self > n || !n || (unsigned int)n + 1 < 3u * t + 1
   || !rs || !genesis || !cmp
   || !records || !retain || !release || !after) {
    s->self = 1; /* rejected: self > n (== 0) marks it; every entry is inert */
    return;
  }
  s->cmp = cmp;
  s->records = records;
  s->retain = retain;
  s->release = release;
  s->after = after;
  s->ctx = ctx;
  s->n = n;
  s->t = t;
  s->rs = rs;
  s->self = self;
  memcpy(sysFro(s), genesis, rs);
}

/*
 * Direct the store to drop 'round', keeping the name for the
 * RELEASE act that announces it: the store no longer holds the
 * round when the act is returned, so the act cannot borrow from
 * it (I9 -- every departure is announced).  The caller's own
 * release runs while the records still exist, which is where a
 * withdrawal names the processes it leaves unserved (system.md
 * R4, absence).
 */
static const unsigned char *
sysDrop(
  struct system *s
 ,const unsigned char *round
){
  memcpy(sysScr(s), round, s->rs);
  *(sysScr(s) + s->rs) = 1;
  (*s->release)(s->ctx, sysScr(s));
  return (sysScr(s));
}

unsigned int
systemReceived(
  struct system *s
 ,const unsigned char *round
 ,unsigned char from
 ,unsigned char possesses
 ,unsigned char cursor
 ,struct systemAct *out
){
  unsigned int bs;
  unsigned int nact;
  int c;
  unsigned char *rec;
  unsigned char *r2;
  const unsigned char *nm;
  const unsigned char *nx;
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

  if (!s || !round || !out || s->self > s->n || from > s->n)
    return (0);
  bs = sysBs(s);
  memset(sysScr(s), 0, s->rs + 1u);

  /*
   * Possession record first, dispatch second (reads following
   * writes): a process announcing it holds the round must not be
   * misread as wanting it.  The record marks only 'from' itself --
   * the Byzantine containment argument in system.h.
   */
  rec = sysRec(s, round);
  if (possesses && rec) {
    SYSTEM_SET(rec, from);
    SYSTEM_CLR(rec + bs, from);
  }

  senderPossesses = 0;
  allPossess = 0;
  if (rec) {
    roundClass = SYSTEM_ROUND_RETAINED;
    if (SYSTEM_TST(rec, from))
      senderPossesses = 1;
    if (sysAll(s, rec))
      allPossess = 1;
  } else if (!(c = sysCmp(s, round, sysFro(s)))) {
    roundClass = (s->flags & SYSTEM_F_LIVE)
                 ? SYSTEM_ROUND_LIVE : SYSTEM_ROUND_AHEAD;
  } else if (c > 0) {
    /*
     * Beyond chain reach: rounds after the frontier cannot have
     * been verified by the caller, so they create no obligation
     * (system.md, existence evidence).  Inert.
     */
    return (0);
  } else {
    roundClass = SYSTEM_ROUND_RELEASED;
  }

  /*
   * The cursor birth (system.md Model, want): a high-water act
   * locates the sender's cursor at 'round', so every retained round
   * after it that the sender has not evidenced is owed to that
   * sender.  Same containment as the direct birth -- only the given
   * sender's still-owed bits, only where its possession bit is
   * clear (I2; self is gated by I3) -- and discharged by the serve
   * walk, not by an act here.  An independent rule set kept out of
   * the merged dispatch (its inputs appear in no other rule -- the
   * adopt-rule precedent; system.dtc, cursor-event section).
   */
  if (cursor) {
    nm = round;
    while ((r2 = (*s->after)(s->ctx, nm, &nx))) {
      nm = nx;
      if (!SYSTEM_TST(r2, from))
        SYSTEM_SET(r2 + bs, from);
    }
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
    out[nact].round = sysFro(s);
    ++nact;
  }
  if (doServeOwed) {
    SYSTEM_SET(rec + bs, from);
    out[nact].want = rec + bs;
    out[nact].have = rec + 2 * bs;
    out[nact].act = SYSTEM_ACT_SERVE;
    out[nact].round = round;
    ++nact;
  }
  if (doParticipationOwed)
    s->flags |= SYSTEM_F_OWED;
  if (doRelease) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = sysDrop(s, round);
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
  memset(sysScr(s), 0, s->rs + 1u);

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
    out[nact].round = sysFro(s);
    ++nact;
    s->flags |= SYSTEM_F_LIVE;
    s->flags &= ~SYSTEM_F_OWED;
  }
  if (doMaintain) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_MAINTAIN;
    out[nact].round = sysFro(s);
    ++nact;
    s->flags |= SYSTEM_F_LIVE;
  }
  if (doAdmit) {
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_ADMIT;
    out[nact].round = sysFro(s);
    ++nact;
    s->flags |= SYSTEM_F_LIVE;
  }
  return (nact);
}

unsigned int
systemComplete(
  struct system *s
 ,const unsigned char *round
 ,const unsigned char *next
 ,const unsigned char *have
 ,struct systemAct *out
){
  unsigned int bs;
  unsigned int nact;
  unsigned char *rec;
  const unsigned char *nm;
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

  if (!s || !round || !next || !out || s->self > s->n
   || !(s->flags & SYSTEM_F_LIVE) || sysCmp(s, round, sysFro(s)))
    return (0);
  bs = sysBs(s);
  nact = 0;
  memset(sysScr(s), 0, s->rs + 1u);

  /*
   * Ask the store to hold the closing round.  A refusal is the
   * reach binding (system.md O6) and has changed nothing, so the
   * release rule's eviction arm reads it as a boundary input the
   * same way every other post-write input is read.
   */
  rec = (*s->retain)(s->ctx, sysFro(s));

  roundClass = SYSTEM_ROUND_RELEASED; /* inert for received-event rules */
  senderPossesses = 1;
  instanceLive = 1;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = 0;
  backlogDrained = 0;
  allPossess = 0;
  budgetExceeded = rec ? 0 : 1;
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
    /* evict the oldest retained round -- the earliest in the
     * order, which the store answers, then ask again for the room
     * it freed */
    if ((*s->after)(s->ctx, 0, &nm)) {
      out[nact].want = 0;
      out[nact].have = 0;
      out[nact].act = SYSTEM_ACT_RELEASE;
      out[nact].round = sysDrop(s, nm);
      ++nact;
    }
    rec = (*s->retain)(s->ctx, sysFro(s));
  }

  if (rec) {
    memset(rec, 0, 3 * bs);
    SYSTEM_SET(rec, s->self);
    if (have)
      memcpy(rec + 2 * bs, have, bs);
  }
  /*
   * The one consume region: a live COMPLETE and an adoption close
   * both land here, so completion clears the witness book either
   * way -- a racing own COMPLETE supersedes the accumulation
   * structurally.
   */
  memset(sysWit(s), 0, bs);
  /*
   * Advance: the closed round becomes the prior (history, not a
   * computed predecessor), and the close's minted name becomes the
   * frontier.
   */
  memcpy(sysPri(s), sysFro(s), s->rs);
  memcpy(sysFro(s), next, s->rs);
  s->flags &= ~(SYSTEM_F_LIVE | SYSTEM_F_OWED | SYSTEM_F_ADOPT);
  return (nact);
}

unsigned int
systemPossessed(
  struct system *s
 ,const unsigned char *round
 ,unsigned char from
 ,struct systemAct *out
){
  unsigned int bs;
  unsigned int nact;
  unsigned char *rec;
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

  if (!s || !round || !out || s->self > s->n || from > s->n)
    return (0);
  if (!(rec = sysRec(s, round)))
    return (0);
  bs = sysBs(s);
  memset(sysScr(s), 0, s->rs + 1u);
  SYSTEM_SET(rec, from);
  SYSTEM_CLR(rec + bs, from);

  roundClass = SYSTEM_ROUND_RETAINED;
  senderPossesses = 1;
  instanceLive = (s->flags & SYSTEM_F_LIVE) ? 1 : 0;
  participationOwed = (s->flags & SYSTEM_F_OWED) ? 1 : 0;
  valuePending = 0;
  backlogDrained = 0;
  allPossess = sysAll(s, rec) ? 1 : 0;
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
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = sysDrop(s, round);
    ++nact;
  }
  return (nact);
}

unsigned int
systemEvict(
  struct system *s
 ,struct systemAct *out
){
  unsigned int nact;
  const unsigned char *nm;
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
  /* the oldest retained round -- the earliest in the order, which
   * the store answers (I11: a rung dropped from the middle stalls
   * a climb nothing afterward can observe) */
  if (!(*s->after)(s->ctx, 0, &nm))
    return (0);
  memset(sysScr(s), 0, s->rs + 1u);

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
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = sysDrop(s, nm);
    ++nact;
  }
  return (nact);
}

unsigned int
systemWitness(
  struct system *s
 ,const unsigned char *round
 ,unsigned char from
 ,struct systemAct *out
){
  if (!s || !round || !out || s->self > s->n || from > s->n
   || from == s->self)
    return (0);
  if (sysCmp(s, round, sysFro(s)))
    return (0);
  memset(sysScr(s), 0, s->rs + 1u);
  SYSTEM_SET(sysWit(s), from);
  /*
   * The O3 adopt rule as a C guard -- an independent rule set kept
   * out of the merged dispatch (system.dtc, witness-event section):
   * adopt iff post-record distinct witnesses reach t+1 and adoption
   * is not already signaled.  Single-fire via the latch.
   */
  if (sysPop(s, sysWit(s)) >= (unsigned int)s->t + 1
   && !(s->flags & SYSTEM_F_ADOPT)) {
    s->flags |= SYSTEM_F_ADOPT;
    out->want = 0;
    out->have = 0;
    out->act = SYSTEM_ACT_ADOPT;
    out->round = sysFro(s);
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
  memset(sysScr(s), 0, s->rs + 1u);
  memset(sysWit(s), 0, sysBs(s));
  s->flags &= ~SYSTEM_F_ADOPT;
}

void
systemAssembled(
  struct system *s
 ,const unsigned char *round
 ,unsigned char member
){
  unsigned char *rec;

  if (!s || !round || s->self > s->n || member > s->n)
    return;
  if (!(rec = sysRec(s, round)))
    return;
  memset(sysScr(s), 0, s->rs + 1u);
  SYSTEM_SET(rec + 2 * sysBs(s), member);
}

unsigned int
systemServe(
  struct system *s
 ,unsigned char *cursor
 ,struct systemAct *out
){
  unsigned int bs;
  unsigned int b;
  unsigned int pass;
  unsigned char *rec;
  const unsigned char *nm;
  const unsigned char *nx;

  if (!s || !cursor || !out || s->self > s->n)
    return (0);
  bs = sysBs(s);
  /*
   * The rotation, positioned by NAME: walk forward from the round
   * last served, then wrap to the oldest.  Positioning in the
   * order rather than by a count is what keeps a continuously
   * owed round revisited within one turn however the set grows
   * and shrinks between calls -- and the returner the walk exists
   * for stands at the OLDEST rung, the one a count into a growing
   * set starves last (system.md SERVE bounds, M1).  A cursor
   * naming a released round still positions the walk: 'after'
   * asks an order question, which does not require the round be
   * retained.
   */
  for (pass = 0; pass < 2; ++pass) {
    nm = (!pass && cursor[s->rs]) ? cursor : 0;
    while ((rec = (*s->after)(s->ctx, nm, &nx))) {
      nm = nx;
      for (b = 0; b < bs; ++b)
        if (*(rec + bs + b)) {
          out->want = rec + bs;
          out->have = rec + 2 * bs;
          out->act = SYSTEM_ACT_SERVE;
          out->round = nm;
          memcpy(cursor, nm, s->rs);
          cursor[s->rs] = 1;
          return (1);
        }
    }
    if (!cursor[s->rs]) /* the first pass already started oldest */
      break;
  }
  return (0);
}

const unsigned char *
systemFrontier(
  const struct system *s
){
  return (s && s->self <= s->n ? s->data : 0);
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
 ,const unsigned char *round
){
  return (s && round && s->self <= s->n && sysRec(s, round) ? 1 : 0);
}

const unsigned char *
systemPossess(
  const struct system *s
 ,const unsigned char *round
){
  if (!s || !round || s->self > s->n)
    return (0);
  return (sysRec(s, round));
}

const unsigned char *
systemWant(
  const struct system *s
 ,const unsigned char *round
){
  const unsigned char *rec;

  if (!s || !round || s->self > s->n || !(rec = sysRec(s, round)))
    return (0);
  return (rec + sysBs(s));
}

const unsigned char *
systemHave(
  const struct system *s
 ,const unsigned char *round
){
  const unsigned char *rec;

  if (!s || !round || s->self > s->n || !(rec = sysRec(s, round)))
    return (0);
  return (rec + 2 * sysBs(s));
}

const unsigned char *
systemWitnesses(
  const struct system *s
){
  if (!s || s->self > s->n)
    return (0);
  return (s->data + 3 * (unsigned long)s->rs + 1);
}
