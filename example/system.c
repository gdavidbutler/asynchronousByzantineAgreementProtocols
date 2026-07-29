/*
 * asynchronousByzantineAgreementProtocols - Example system layer program
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
 * system.c -- Standalone demonstration of the system obligation layer
 * (system.md) composed with BKR94 ACS.
 *
 * What the system layer brings to the table:
 *
 *   One ACS instance agrees on ONE subset.  An application has a
 *   STREAM.  Turning a series of one-shot agreements into a single
 *   ordered sequence is not free: ACS legitimately excludes up to t
 *   honest contributions per round, so an excluded value must ride
 *   the next round -- without appearing twice if it did land, and
 *   without the sequence forking when a process falls behind.
 *
 * Three printed verdicts, in the order a reviewer should read them:
 *
 *   sequence identity  every process's agreed sequence is
 *                      byte-identical at every position both hold
 *                      (system.md L6).
 *   exactly-once       every presented value appears in the agreed
 *                      sequence exactly once -- never zero, never
 *                      twice (system.md R1).  R1 is a CONJUNCTION:
 *                      the machine half (only ADMIT consumes a value,
 *                      MAINTAIN outranks and never consumes, one
 *                      launch act per opportunity) is system.[hc]'s;
 *                      the caller half (stage the bytes, re-present
 *                      them byte-identically on every launch, retire
 *                      only on witnessing the value in an agreed
 *                      subset) is THIS FILE's, and is the half the
 *                      layer proves nothing about.
 *   laggard heals      a process cut off for one round does not fork
 *                      and is not stranded: the cohort advances under
 *                      the R4 tolerance, the cut process adopts from
 *                      served evidence, and its sequence still
 *                      matches.
 *   containment        with -B, one Byzantine process lying about
 *                      possession, want, and the served composition
 *                      changes nothing the correct processes agree on.
 *                      Every verdict quantifies over CORRECT processes
 *                      only; a Byzantine process's own state proves
 *                      nothing and is excluded.
 *
 * The Byzantine arm attacks the SYSTEM layer's own containment claims,
 * not Bracha's: an equivocating initiator is Lemma 2's business and is
 * demonstrated by example/bracha87Fig1.c.  The three lies and what each
 * tests --
 *
 *   forge possession  claim to hold every round.  The machine marks
 *                     only the authenticated sender, so this retires
 *                     only the serves owed to the liar; release still
 *                     needs all n bits, so every correct process's TRUE
 *                     indication is still required.  No count-threshold
 *                     shortcut exists for exactly this reason.
 *   forge want        never admit holding anything, so every round
 *                     reads as wanted and soaks serve slots.  The cap
 *                     is t with a floor of one and grants rotate, so a
 *                     correct wanting process is still served.
 *   fake candidate    serve a fabricated composition on recovery legs,
 *                     for rounds this process need not even hold.  The
 *                     witness record marks only its sender, so t
 *                     forgers reach at most t -- one short of the t+1
 *                     the adopt gate needs.
 *
 * The three carrier geometries (system.md "Relation to a deployment")
 * are the reason this file has two message classes rather than one.
 * Every carrier is an instance of a layer-below primitive, so
 * transport reliability is INHERITED rather than re-implemented:
 *
 *   round instance  ACS at (n, t) -- the agreed composition rides its
 *                   value plane.
 *   recovery        a Bracha87 Fig 1 instance at TWO processes and
 *                   t = 0 -- the primitive degenerated to an
 *                   acknowledged, retried pair channel.  This is a
 *                   SERVE act's discharge, and it is why a serve here
 *                   is a real Fig 1 leg and not a bare message: BPR
 *                   comes free with the instance, and the leg retires
 *                   on the other end's accept, never on local send.
 *
 * The third geometry (exchange -- a Fig 1 at (n, t) distributing one
 * initiator's content) is the deployment's; see Scope.
 *
 * SCOPE -- what this demonstration deliberately does NOT carry:
 *
 *   No crypto.  O1's chain fold is the demo mixer below, adequate for
 *   demonstration only, exactly as this repo's example coin is; O3's
 *   verification seam is a byte comparison; O5's authorship budget is
 *   a round counter.  A9 (sender-authenticated ingress) is supplied by
 *   fiat -- the wire's 'from' field IS the attribution.
 *   No O2 two-grain split.  Composition (membership + anchor) and
 *   content (the contributed bytes) are separate here, but content
 *   travels on the round's own instance rather than a dedicated
 *   exchange carrier.  A member whose content never arrives before its
 *   round is released is an out-of-band hole, printed as such -- the
 *   per-member cost O2 prices, never the round's failure.
 *   No threads, no transport, no wall clock.  The tick is a loop
 *   iteration.  A4's eventual delivery is supplied by the in-memory
 *   queue rather than inherited from BPR, and L1's "keeps taking
 *   steps" is likewise supplied by the loop.
 *
 * This process keeps its own content record -- it must, since the
 * agreed sequence outlives the rounds that carried it -- and ALSO feeds
 * systemAssembled so the machine's have grain stays current while a
 * round is retained.  That is the fully-fed shape; a caller serving
 * from its own record may decline to feed it and leave the grain at its
 * close-time value, and a caller with no record of its own must feed it
 * (system.h, systemAssembled).
 *
 * It is a GROUPING caller, and therefore never a switching one
 * (system.md, C6): served assertions are grouped by their BYTES in
 * this file's own book, and the machine hears nothing until one
 * group reaches t+1 distinct servers.  Under t Byzantine any t+1
 * group contains an honest server asserting the agreed composition,
 * so at most one group can ever reach t+1 and a candidate switch
 * cannot arise -- systemWitnessReset stays uncalled here.  A caller
 * that cannot group must latch and re-arm on a switch instead; what
 * fails is first-latch WITHOUT the reset -- a Byzantine first server
 * latches a fabrication no honest assertion then byte-matches, and
 * adoption stalls forever (measured at the witness site below:
 * weakening the group test to one assertion strands the laggard at
 * zero adoptions while containment still reads clean).
 *
 * Build:
 *   (from project root) make example_system
 *
 * Usage:
 *   ./example_system [-v] [-s seed] [-l loss] [-L proc:round]
 *                    [-B proc:mode] [-m every] [-T Tp] [-S sweeps]
 *                    n t w msgs
 *
 * Examples -- the notable runs from building this file.  Add -v to any
 * of them to trace launches, closes and adoptions, and -s to move the
 * delivery order and loss draw:
 *
 *   ./example_system 4 1 3 3
 *     Baseline: every correct process prints the same agreed sequence,
 *     each staged value appearing in it exactly once.
 *
 *   ./example_system -v -m 2 4 1 3 3
 *     O5: a maintenance round rides "~maintenance" while the staged
 *     value stays staged and wins a later round -- MAINTAIN outranks
 *     ADMIT and never consumes it.
 *
 *   ./example_system -L 3:1 4 1 3 3
 *     R4 and the heal: process 3 is cut off from round 1, the cohort
 *     advances under the bounded tolerance rather than stalling, and
 *     process 3 adopts from evidence served over a real 2/0 Fig 1 leg.
 *
 *   ./example_system -L 3:2 4 1 3 1
 *     The vacuity guard: with one message each the cut round is never
 *     reached, and the heal verdict reports that instead of passing.
 *
 *   ./example_system -l 50 4 1 3 3
 *     Half of every inter-process wire dropped; BPR comes free with
 *     each carrier and still carries the run to agreement.
 *
 *   ./example_system 2 0 3 2
 *     n = 2, t = 0: the serve cap's floor of one IS the whole serving
 *     capacity here, and without it SERVE would retire by silence.
 *
 *   ./example_system 7 2 4 3
 *     n = 7, t = 2: a subset of n-t = 5 legitimately leaves two
 *     contributions out of every round, so they ride the next one.
 *
 *   ./example_system -B 1:1 4 1 3 3
 *     Forged possession, and its containment: the liar's own forged bit
 *     completes the all-n record, the round releases, and the carrier
 *     the liar still needed dies.  It strands only itself -- the layer
 *     owes nothing to a process that says it is satisfied.
 *
 *   ./example_system -B 1:2 4 1 3 3
 *     Forged want: soaks serve slots without ever displacing a correct
 *     wanting process, because grants rotate.
 *
 *   ./example_system -B 1:8 -L 5:1 7 2 4 3
 *     The fold ground earning its keep: a fabrication that does not
 *     chain to the asserted anchor is refused on one look, no book.
 *
 *   ./example_system -B 1:5 -L 5:1 7 2 4 3
 *     A re-folded fabrication instead, which the fold ground CANNOT
 *     refuse -- the t+1 grouping is what discriminates it.  Needs
 *     t >= 2: a Byzantine process and a partitioned one are two faults.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "bkr94acs.h"

/*--------------------------------------------------------------------------*/
/*  Constants                                                               */
/*--------------------------------------------------------------------------*/

#define MAX_PROCESSES 8
#define MAX_ROUNDS   16   /* demo round cap; the machine's space is the
                           * wrapping byte -- this file simply stops */
#define MAX_MSGS      6   /* application messages staged per process */
#define VLEN         16   /* contributed value bytes (vLen encoding VLEN-1) */
#define MAX_PHASES    8

/*
 * The composition: the chain anchor (O1) followed by one membership
 * byte per process.  Byte-identical at every correct process, which is
 * what makes it comparable at a witness and foldable at an adopter.
 */
#define ANCHOR   4
#define COMPLEN  (ANCHOR + MAX_PROCESSES)

#define LEGCAP      64
#define HOLDCAP   2048
#define QCAP     65536
#define DRAIN     1024
#define MAX_TICKS 400000
#define IDLE_STOP   64

#define WK_ACS   0  /* traffic of a round's ACS instance */
#define WK_SERVE 1  /* traffic of a recovery leg (Fig 1 at 2 processes, t=0) */

#define NO_MSG 0xFF  /* rode[] entry: this round carried no application value */

/*--------------------------------------------------------------------------*/
/*  Wire                                                                    */
/*--------------------------------------------------------------------------*/

struct wire {
  unsigned char kind;      /* WK_ACS / WK_SERVE */
  unsigned char sysRound;  /* the system round this traffic belongs to */
  unsigned char from;      /* authenticated sender (A9, by fiat) */
  unsigned char to;
  unsigned char possesses; /* possession indication riding this wire */
  unsigned char cls;       /* WK_ACS: BKR94ACS_CLS_ACAST / _BA */
  unsigned char type;      /* BRACHA87_INITIAL / ECHO / READY */
  unsigned char accepted;  /* BKR94ACS_ACCEPTED annotation */
  unsigned char process;   /* WK_ACS: whose A-Cast / which BA */
  unsigned char baRound;
  unsigned char initiator;
  unsigned char baValue;
  unsigned char legServer; /* WK_SERVE: the serving process */
  unsigned char legServed; /* WK_SERVE: the wanting process */
  unsigned char value[VLEN];
  unsigned char comp[COMPLEN];
};

/*--------------------------------------------------------------------------*/
/*  Recovery leg -- one Bracha87 Fig 1 at two processes, t = 0              */
/*                                                                          */
/*  Within the leg the server is index 0 (its designated initiator) and     */
/*  the served process is index 1.  The leg retires only when both ends     */
/*  have accepted -- remote evidence, never local send.                     */
/*--------------------------------------------------------------------------*/

struct leg {
  struct bracha87Fig1 *f1;
  unsigned char inUse;
  unsigned char server;
  unsigned char served;
  unsigned char round;
  unsigned char retired;
  unsigned char selfAcc;
  unsigned char otherAcc;
  unsigned char pad;
};

/*--------------------------------------------------------------------------*/
/*  One process's seat                                                      */
/*--------------------------------------------------------------------------*/

struct proc {
  struct system *sys;
  struct bkr94acs *acs[MAX_ROUNDS];
  struct wire *hold;                  /* beyond-reach traffic, re-fed later */
  struct bracha87Retry cur[MAX_ROUNDS];
  struct leg leg[LEGCAP];
  unsigned int holdCnt;
  unsigned int tolCount;              /* T_p: own sweeps under TOLERANCE */
  unsigned int barren;                /* S: own sweeps without progress */
  unsigned int adopts;
  unsigned int seqCnt;
  unsigned int maxRetained;
  unsigned char self;
  unsigned char msgHead;              /* PRESENT: the staged value */
  unsigned char msgCnt;
  unsigned char serveCursor;
  unsigned char retryCursor;
  unsigned char adoptPending;
  unsigned char candValid;
  unsigned char tolElapsed;
  unsigned char active;               /* progress observed this tick */
  unsigned char partitioned;
  unsigned char pad[2];
  unsigned char msg[MAX_MSGS][VLEN];
  unsigned char rode[MAX_ROUNDS];
  unsigned char pend[MAX_ROUNDS][MAX_PROCESSES];
  unsigned char closed[MAX_ROUNDS];
  unsigned char emitted[MAX_ROUNDS];
  unsigned char comp[MAX_ROUNDS][COMPLEN];
  unsigned char told[MAX_ROUNDS][MAX_PROCESSES];
  unsigned char has[MAX_ROUNDS][MAX_PROCESSES];
  unsigned char content[MAX_ROUNDS][MAX_PROCESSES][VLEN];
  unsigned char cand[COMPLEN];
  /*
   * O3's verification seam, caller-side: assertions grouped by their
   * BYTES before the machine is told anything.  See legAccept.
   */
  unsigned char candCnt;
  unsigned char candRound;
  unsigned char candSrv[MAX_PROCESSES][(MAX_PROCESSES + 7) / 8];
  unsigned char candBuf[MAX_PROCESSES][COMPLEN];
  unsigned char seqHole[MAX_ROUNDS * MAX_PROCESSES];
  unsigned char seq[MAX_ROUNDS * MAX_PROCESSES][VLEN];
};

/*--------------------------------------------------------------------------*/
/*  Globals                                                                 */
/*--------------------------------------------------------------------------*/

static struct proc Proc[MAX_PROCESSES];
static struct wire *Queue;
static unsigned int Qcount;
static unsigned int Rng;
static unsigned int Pushed;      /* wires pushed this tick */
static unsigned int N;
static unsigned int T;
static unsigned int Wr;          /* retained window, actual rounds */
static unsigned int Verbose;
static unsigned int DropPct;
static unsigned int Tp;
static unsigned int Sp;
static unsigned int MaintEvery;
static int LagProc = -1;
static int LagRound = -1;
static int ByzProc = -1;
static unsigned int ByzMode;
static unsigned int ByzAsserts;   /* fabrications actually served */
static unsigned int CandConflicts; /* times a correct process's book held
                                    * two distinct compositions at once */
static unsigned int CandRejected;  /* assertions the fold ground refused
                                    * on their own, no book consulted */

/*
 * Byzantine behaviors (-B proc:mode).  Each attacks one of the system
 * layer's OWN containment claims; equivocating an A-Cast is Bracha
 * Lemma 2's business and is demonstrated by example/bracha87Fig1.c.
 */
#define BYZ_POSSESS 1  /* claim possession of every round, held or not */
#define BYZ_WANT    2  /* never claim possession, so every round reads
                        * as wanted -- soaks serve slots */
#define BYZ_CAND    4  /* serve a fabricated composition on recovery legs,
                        * re-folded so it is internally consistent */
#define BYZ_LINK    8  /* fabricate WITHOUT re-folding: the membership no
                        * longer chains to the asserted anchor, which is
                        * what O1's linkage check catches on one look */
static unsigned long AcsSz;
static unsigned long LegSz;

/* O1's chain base (A11: the genesis bundle).  Demo bytes. */
static unsigned char Genesis[] = {0x5b, 0xa5, 0x11, 0x00};

/* The deployment's maintenance form (O5) and its empty-join form. */
static unsigned char Maint[] = "~maintenance";
static unsigned char Empty[] = "~empty";

/*--------------------------------------------------------------------------*/
/*  Deterministic scheduling                                                */
/*--------------------------------------------------------------------------*/

static unsigned int
rngNext(
  void
){
  Rng = Rng * 1103515245u + 12345u;
  return ((Rng >> 16) & 0x7FFFu);
}

/*
 * The coin -- deterministic alternating, adequate for demonstration
 * only, exactly as example/bkr94acs.c's.
 */
static unsigned char
demoCoin(
  void *closure
 ,unsigned char phase
){
  (void)closure;
  return (phase % 2);
}

/*
 * O1's chain fold, demo grade: anchor_R = mix(anchor_{R-1}, membership).
 * A10 asks for collision resistance so that a candidate folding to a
 * verified anchor IS the agreed result; this mixer supplies the SHAPE
 * of that argument and none of its strength.  A deployment folds with
 * a real hash.
 */
static void
foldAnchor(
  unsigned char *out            /* ANCHOR bytes */
 ,const unsigned char *prev     /* ANCHOR bytes */
 ,const unsigned char *mem      /* MAX_PROCESSES membership bytes */
){
  unsigned int i;
  unsigned int acc;

  acc = 0;
  for (i = 0; i < ANCHOR; ++i)
    acc = acc * 131u + prev[i];
  for (i = 0; i < MAX_PROCESSES; ++i)
    acc = acc * 131u + mem[i];
  for (i = 0; i < ANCHOR; ++i) {
    out[i] = (unsigned char)(acc & 0xFF);
    acc >>= 5;
    acc = acc * 31u + 7u;
  }
}

/*
 * Wrapping distance from 'base' to 'round' in the unsigned char round
 * space: 0 is 'base' itself, 1..127 ahead of it, 128..255 behind it.
 * Chain reach (system.h) is at-or-behind the frontier.
 */
static unsigned char
aheadBy(
  unsigned char round
 ,unsigned char base
){
  return ((unsigned char)(round - base));
}

/*--------------------------------------------------------------------------*/
/*  Queue                                                                   */
/*--------------------------------------------------------------------------*/

static void
qPush(
  const struct wire *w
){
  if (Qcount >= QCAP)
    return;
  Queue[Qcount++] = *w;
  ++Pushed;
}

/* Pop a uniformly random pending wire (swap-with-last). */
static int
qPopRandom(
  struct wire *w
){
  unsigned int i;

  if (!Qcount)
    return (0);
  i = rngNext() % Qcount;
  *w = Queue[i];
  Queue[i] = Queue[--Qcount];
  return (1);
}

/*
 * Push onto the network.  Self-addressed traffic is not a network path
 * and is never dropped; everything else draws against the loss rate,
 * then against the laggard cut.
 */
static void
pushWire(
  const struct wire *w
){
  if (w->to != w->from) {
    if (DropPct && (rngNext() % 100u) < DropPct)
      return;
    if (w->kind == WK_ACS
     && LagProc >= 0
     && (int)w->to == LagProc
     && (int)w->sysRound == LagRound)
      return;
  }
  qPush(w);
}

static void
holdWire(
  struct proc *p
 ,const struct wire *w
){
  if (p->holdCnt >= HOLDCAP)
    return;
  p->hold[p->holdCnt++] = *w;
}

/*--------------------------------------------------------------------------*/
/*  Forward declarations -- the glue is mutually recursive: a received      */
/*  wire produces machine acts, and applying a machine act (DELIVER,        */
/*  SERVE, a launch) produces more wires.                                   */
/*--------------------------------------------------------------------------*/

static void applySysActs(struct proc *, struct systemAct *, unsigned int,
                         const struct wire *);
static void sysClose(struct proc *, unsigned char, const unsigned char *);
static void deliverWire(const struct wire *);

/*--------------------------------------------------------------------------*/
/*  ACS egress                                                              */
/*                                                                          */
/*  Every wire of round R carries the possession indication for R           */
/*  (system.h systemReceived, 'possesses').  THIS IS LOAD-BEARING and is    */
/*  not obvious from the header: the indication rides a CLOSED round's own  */
/*  tails.  Without it, a set of processes that all close R and wait learn  */
/*  nothing about each other, the advance gate never opens, and the run     */
/*  deadlocks rather than merely slowing.                                   */
/*--------------------------------------------------------------------------*/

static void
emitAcs(
  struct proc *p
 ,unsigned char round
 ,struct bkr94acsAct *acts
 ,unsigned int nacts
){
  struct wire w;
  unsigned int i;
  unsigned int j;

  for (i = 0; i < nacts; ++i) {
    switch (acts[i].act) {

    case BKR94ACS_ACT_ACAST_SEND:
    case BKR94ACS_ACT_BA_SEND:
      if (acts[i].act == BKR94ACS_ACT_ACAST_SEND && !acts[i].value)
        break;
      memset(&w, 0, sizeof (w));
      w.kind = WK_ACS;
      w.sysRound = round;
      w.from = p->self;
      w.possesses = (unsigned char)(round < MAX_ROUNDS && p->closed[round]);
      /*
       * The two possession lies.  Both are contained by the machine
       * marking only the AUTHENTICATED SENDER: a forged claim retires
       * only the serves owed to the forger, and release still needs all
       * n bits, so with at most t liars every correct process's TRUE
       * indication is still required.  A forged WANT is the mirror --
       * it can soak a serve slot but cannot displace a correct wanting
       * process, because the cap rotates.
       */
      if ((int)p->self == ByzProc) {
        if (ByzMode & BYZ_POSSESS)
          w.possesses = 1;
        else if (ByzMode & BYZ_WANT)
          w.possesses = 0;
      }
      w.type = acts[i].type;
      w.accepted = acts[i].accepted;
      w.process = acts[i].process;
      if (acts[i].act == BKR94ACS_ACT_ACAST_SEND) {
        w.cls = BKR94ACS_CLS_ACAST;
        memcpy(w.value, acts[i].value, VLEN);
      } else {
        w.cls = BKR94ACS_CLS_BA;
        w.baRound = acts[i].round;
        w.initiator = acts[i].initiator;
        w.baValue = acts[i].baValue;
      }
      for (j = 0; j < N; ++j) {
        if (acts[i].skip && BRACHA87_SKIP_TST(acts[i].skip, j))
          continue;
        w.to = (unsigned char)j;
        pushWire(&w);
      }
      break;

    case BKR94ACS_ACT_COMPLETE: {
      unsigned char comp[COMPLEN];
      unsigned char members[MAX_PROCESSES];
      unsigned int cnt;
      unsigned int k;

      if (round >= MAX_ROUNDS || p->closed[round])
        break;                      /* adoption already consumed the round */
      memset(comp, 0, sizeof (comp));
      cnt = bkr94acsSubset(p->acs[round], members);
      for (k = 0; k < cnt; ++k)
        if (members[k] < MAX_PROCESSES)
          comp[ANCHOR + members[k]] = 1;
      foldAnchor(comp, round ? p->comp[round - 1] : Genesis, comp + ANCHOR);
      sysClose(p, round, comp);
      break;
    }

    case BKR94ACS_ACT_BA_EXHAUSTED:
      /*
       * The library's one stop, and a failure stop: this BA can issue
       * no further phase, so COMPLETE is unreachable for the round.
       * Nothing here adds a stop -- the process is simply behind, and
       * the wanting side heals it (system.md, Relation to a deployment).
       */
      printf("process %u: BA[%u] EXHAUSTED at round %u -- healing by adoption\n",
             (unsigned)p->self, (unsigned)acts[i].process, (unsigned)round);
      break;

    default:
      break;
    }
  }
}

/*--------------------------------------------------------------------------*/
/*  Recovery legs                                                           */
/*--------------------------------------------------------------------------*/

static struct leg *
legFind(
  struct proc *p
 ,unsigned char server
 ,unsigned char served
 ,unsigned char round
){
  unsigned int i;

  for (i = 0; i < LEGCAP; ++i)
    if (p->leg[i].inUse
     && p->leg[i].server == server
     && p->leg[i].served == served
     && p->leg[i].round == round)
      return (&p->leg[i]);
  return (0);
}

static struct leg *
legAlloc(
  struct proc *p
 ,unsigned char server
 ,unsigned char served
 ,unsigned char round
){
  struct leg *lg;
  unsigned int i;

  for (i = 0; i < LEGCAP; ++i)
    if (!p->leg[i].inUse)
      break;
  if (i == LEGCAP)
    return (0);
  lg = &p->leg[i];
  if (!(lg->f1 = calloc(1, LegSz)))
    return (0);
  bracha87Fig1Init(lg->f1, 1, 0, COMPLEN - 1);
  lg->inUse = 1;
  lg->server = server;
  lg->served = served;
  lg->round = round;
  lg->retired = 0;
  lg->selfAcc = 0;
  lg->otherAcc = 0;
  return (lg);
}

static void
legFree(
  struct leg *lg
){
  free(lg->f1);
  lg->f1 = 0;
  lg->inUse = 0;
}

/*
 * Emit one leg action to BOTH ends of the pair.  A Bracha Fig 1 is a
 * broadcast primitive and its sender is one of the recipients: at two
 * processes the echo threshold is (n+t)/2 + 1 = 2, so an end that does
 * not feed its own action to itself can never reach it.  Self-addressed
 * wires ride the same delivery path (pushWire exempts them from loss),
 * which keeps one ingress path rather than a recursive local feed.
 */
static void
legEmit(
  struct proc *p
 ,struct leg *lg
 ,unsigned char act
){
  struct wire w;
  const unsigned char *v;
  const unsigned char *skip;
  unsigned int e;

  if (!(v = bracha87Fig1Value(lg->f1)))
    return;
  skip = bracha87Fig1Skip(lg->f1, act);
  memset(&w, 0, sizeof (w));
  w.kind = WK_SERVE;
  w.sysRound = lg->round;
  w.from = p->self;
  w.legServer = lg->server;
  w.legServed = lg->served;
  w.type = (unsigned char)(act == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                         : act == BRACHA87_ECHO_ALL    ? BRACHA87_ECHO
                         :                               BRACHA87_READY);
  w.accepted = (unsigned char)(act == BRACHA87_READY_ALL && lg->selfAcc);
  /*
   * A leg wire is traffic of its round, so it carries the same
   * possession indication an ACS wire of that round would: the server
   * holds the round by construction, and the served process starts
   * carrying it the moment it adopts -- which is what retires the
   * serve per-process at the server end.
   */
  w.possesses = (unsigned char)(lg->round < MAX_ROUNDS
                             && p->closed[lg->round]);
  if ((int)p->self == ByzProc) {
    if (ByzMode & BYZ_POSSESS)
      w.possesses = 1;
    else if (ByzMode & BYZ_WANT)
      w.possesses = 0;
  }
  memcpy(w.comp, v, COMPLEN);
  for (e = 0; e < 2; ++e) {
    if (skip && BRACHA87_SKIP_TST(skip, e))
      continue;
    w.to = (unsigned char)(e ? lg->served : lg->server);
    pushWire(&w);
  }
}

/*
 * A leg reached ACCEPT at this end.  The served end has now obtained a
 * byte-identical assertion of the round's composition from one server:
 * feed it to the machine's witness book (O3) if it speaks the frontier,
 * and evidence the server's own possession either way.
 *
 * The two calls are ordered deliberately: systemPossessed comes LAST
 * because a release it provokes must not free state this function is
 * still walking.
 */
static void
legAccept(
  struct proc *p
 ,struct leg *lg
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  const unsigned char *v;
  unsigned char f;
  unsigned int n;

  lg->selfAcc = 1;
  bracha87Fig1ProcessAccepted(lg->f1, (unsigned char)(p->self == lg->server ? 0 : 1));
  if (!(v = bracha87Fig1Value(lg->f1)))
    return;
  p->active = 1;

  if (p->self == lg->served) {
    f = systemFrontier(p->sys);
    if (lg->round == f) {
      unsigned char expect[ANCHOR];
      unsigned int i;
      unsigned int cnt;
      unsigned int bad;

      /*
       * O3's FOLD GROUND, caller-side (system.md: "the fold ground is
       * realized wholly caller-side").  Two checks, each decided on ONE
       * assertion with no book consulted:
       *
       *  (1) LINKAGE.  The asserted anchor must be exactly what folding
       *      our own previous anchor over the asserted membership
       *      produces (O1: chain_R = H(chain_{R-1} || the members)).
       *      An assertion that does not chain from the history WE hold
       *      cannot be this round's result.
       *  (2) OUR OWN DECISIONS.  Any BA our own round-R instance has
       *      already decided pins that member: BKR94 agreement says
       *      every correct process decides a BA the same way, so an
       *      assertion disagreeing with a decision we hold is provably
       *      false.  No crypto and no counting.
       *
       * BOTH ARE REJECTION ONLY, and that limit is the interesting
       * part.  The spec's fold ground also lets one assertion CLOSE a
       * round -- and that half is exactly the half that consumes A10.
       * There, folding proves something because the anchor is verified
       * independently (traffic is keyed on chain anchors, so authoring
       * verifiable round-R acts requires chain_{R-1}, and collision
       * resistance makes at most one result derive a given anchor).
       * Here the fold is public arithmetic: anyone can compute a
       * perfectly consistent anchor for a fabricated membership, so
       * passing (1) proves nothing.  With A10 excluded by SCOPE the
       * acceptance half is unreachable, and the t+1 witness ground
       * below remains the only way IN.
       */
      bad = 0;
      foldAnchor(expect, f ? p->comp[f - 1] : Genesis, v + ANCHOR);
      if (memcmp(expect, v, ANCHOR))
        bad = 1;
      if (!bad && p->acs[f])
        for (i = 0; i < N; ++i) {
          unsigned char d;

          d = bkr94acsBaDecision(p->acs[f], (unsigned char)i);
          if (d > 1)
            continue;              /* undecided (0xFF) or exhausted (0xFE) */
          if ((unsigned int)(d ? 1 : 0)
           != (unsigned int)(v[ANCHOR + i] ? 1 : 0)) {
            bad = 1;
            break;
          }
        }
      if (bad) {
        ++CandRejected;
        goto possession;           /* never enters the book */
      }

      /*
       * O3's verification seam, caller-side -- the NEVER-SWITCH
       * discipline (system.md, C6's seam pin): group assertions by their
       * BYTES in our own book and tell the machine nothing until one
       * group reaches t+1 distinct servers.
       *
       * Note WHICH property this protects.  SAFETY -- a fabrication is
       * never adopted -- is the MACHINE's: its witness record marks
       * only the authenticated sender, so t forgers reach at most t,
       * one short of the adopt gate.  What the grouping protects is
       * LIVENESS.  Latch the FIRST assertion instead and a Byzantine
       * first server latches a fabrication that no honest assertion
       * ever byte-matches, so adoption stalls forever and a correct
       * process that needed the heal is stranded -- confirmed by
       * mutating this test to `cnt >= 1`, which strands the laggard at
       * zero adoptions in every seed tried while containment still
       * reads ok.
       *
       * That stall is precisely what systemWitnessReset exists to
       * break, and grouping is the stronger answer: under t Byzantine
       * any t+1 group contains an honest server asserting the agreed
       * composition, so at most one group can ever reach t+1 and a
       * switch cannot arise.  Hence the reset stays uncalled HERE; a
       * caller that cannot group must call it.
       */
      if (p->candRound != f || (!p->candValid && !p->candCnt)) {
        p->candRound = f;
        p->candCnt = 0;
        memset(p->candSrv, 0, sizeof (p->candSrv));
      }
      for (i = 0; i < p->candCnt; ++i)
        if (!memcmp(p->candBuf[i], v, COMPLEN))
          break;
      if (i == p->candCnt && p->candCnt < MAX_PROCESSES) {
        memcpy(p->candBuf[i], v, COMPLEN);
        ++p->candCnt;
        if (p->candCnt == 2 && (int)p->self != ByzProc)
          ++CandConflicts;         /* the grouping had real work to do */
      }
      if (i < p->candCnt) {
        p->candSrv[i][lg->server >> 3] |=
          (unsigned char)(1 << (lg->server & 7));
        cnt = 0;
        for (n = 0; n < N; ++n)
          if (SYSTEM_TST(p->candSrv[i], n))
            ++cnt;
        if (cnt >= T + 1 && !p->candValid) {
          memcpy(p->cand, p->candBuf[i], COMPLEN);
          p->candValid = 1;
          for (n = 0; n < N; ++n) {
            unsigned int wn;

            if (!SYSTEM_TST(p->candSrv[i], n))
              continue;
            wn = systemWitness(p->sys, f, (unsigned char)n, sa);
            applySysActs(p, sa, wn, 0);
          }
        }
      }
    }
  }

possession:
  n = systemPossessed(p->sys, lg->round, lg->server, sa);
  applySysActs(p, sa, n, 0);
}

/*
 * Birth a leg as the SERVER of 'round' to wanting process 'to'.  The
 * composition is the leg's broadcast value; the leg is a Fig 1, so BPR
 * carries it from here and it retires only on the other end's accept.
 */
static void
legBirthServer(
  struct proc *p
 ,unsigned char to
 ,unsigned char round
){
  struct leg *lg;
  unsigned char fake[COMPLEN];
  const unsigned char *assertion;

  if (round >= MAX_ROUNDS)
    return;
  /*
   * A Byzantine server asserts a round it need not hold -- the honest
   * gate is possession, and it is exactly the gate an attacker skips.
   */
  if (!p->closed[round] && (int)p->self != ByzProc)
    return;
  if ((lg = legFind(p, p->self, to, round))) {
    if (lg->retired)
      return;
  } else if (!(lg = legAlloc(p, p->self, to, round)))
    return;
  assertion = p->comp[round];
  if ((int)p->self == ByzProc && (ByzMode & (BYZ_CAND | BYZ_LINK))) {
    /*
     * The fabricated composition: a plausible one, internally
     * consistent (membership flipped, then re-folded), so nothing but
     * the t+1 grouping can tell it from the agreed one.  Containment is
     * the machine's: the witness record marks only this sender, so t
     * forgers reach at most t, one short of the t+1 the adopt gate
     * needs.
     */
    memcpy(fake, p->comp[round], COMPLEN);
    fake[ANCHOR + ((p->self + 1) % N)] ^= 1;
    if (ByzMode & BYZ_CAND)
      foldAnchor(fake, round ? p->comp[round - 1] : Genesis, fake + ANCHOR);
    assertion = fake;
    if (!bracha87Fig1Value(lg->f1))
      ++ByzAsserts;
  }
  if (!bracha87Fig1Value(lg->f1)) {
    bracha87Fig1Initiator(lg->f1, assertion);
    legEmit(p, lg, BRACHA87_INITIAL_ALL);
  }
}

/*--------------------------------------------------------------------------*/
/*  Applying machine actions                                                */
/*--------------------------------------------------------------------------*/

static void
applySysActs(
  struct proc *p
 ,struct systemAct *sa
 ,unsigned int nacts
 ,const struct wire *w     /* the provoking wire, for DELIVER; else 0 */
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
  unsigned int i;
  unsigned int j;
  unsigned int n;
  unsigned char rd;

  for (i = 0; i < nacts; ++i) {
    rd = sa[i].round;

    switch (sa[i].act) {

    case SYSTEM_ACT_DELIVER:
      /*
       * The machine is this caller's router (system.h: routing is the
       * caller's, and DELIVER is the offered surface for a caller whose
       * router IS the machine).  A deployment that routes by its own
       * demux discharges delivery there instead and ignores this act.
       */
      if (!w || w->kind != WK_ACS || rd >= MAX_ROUNDS || !p->acs[rd])
        break;
      if (w->cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(p->acs[rd], w->process, w->type, w->from,
                               w->value, out);
        if (w->type == BRACHA87_READY && w->accepted)
          bkr94acsAcastAccepted(p->acs[rd], w->process, w->from);
      } else {
        n = bkr94acsBaInput(p->acs[rd], w->process, w->baRound, w->initiator,
                            w->type, w->from, w->baValue, out);
        if (w->type == BRACHA87_READY && w->accepted)
          bkr94acsBaAccepted(p->acs[rd], w->process, w->baRound, w->initiator,
                             w->from);
      }
      if (n)
        p->active = 1;              /* a fresh cascade; dedup returns 0 */
      emitAcs(p, rd, out, n);
      break;

    case SYSTEM_ACT_SERVE:
      /*
       * SERVE's pacing is entirely the caller's (system.md SERVE
       * bounds): concurrency capped at t in-flight wanting processes
       * AND NEVER FEWER THAN ONE -- at t = 0 the cap is 1, not 0, or
       * SERVE would be retired by silence, which M1 forbids.  Grants
       * beyond the cap queue for a later tick.
       *
       * The spec's rotation is oldest-WANT-first; this walk approximates
       * it with the serve cursor's own cyclic order, which is over SLOTS
       * and diverges from round age after an out-of-order release (a
       * younger round takes the freed older slot).  Eventual service
       * survives -- the rotation is cyclic, so every wanting process is
       * reached -- but a caller that owes strict oldest-first must sort
       * by round age itself.  Likewise 'live' counts legs rather than
       * distinct wanting processes, so a leg to a dead process holds a
       * slot until its round is released.
       */
      if (rd >= MAX_ROUNDS || !p->closed[rd])
        break;
      {
        unsigned int cap;
        unsigned int live;

        cap = T ? T : 1;
        live = 0;
        for (j = 0; j < LEGCAP; ++j)
          if (p->leg[j].inUse
           && p->leg[j].server == p->self
           && !p->leg[j].retired)
            ++live;
        for (j = 0; j < N; ++j) {
          if (j == p->self || !SYSTEM_TST(sa[i].want, j))
            continue;
          if (legFind(p, p->self, (unsigned char)j, rd))
            continue;              /* already in flight: not a new grant */
          if (live >= cap)
            break;                 /* the rest queue for a later tick */
          legBirthServer(p, (unsigned char)j, rd);
          ++live;
        }
      }
      break;

    case SYSTEM_ACT_RELEASE:
      if (rd >= MAX_ROUNDS)
        break;
      /*
       * Harvest before freeing: the composition is released at all-n
       * possession, but content may still be in the instance.  What is
       * not harvested by now is a per-member out-of-band hole (O2).
       */
      if (p->acs[rd]) {
        for (j = 0; j < N; ++j) {
          const unsigned char *cv;

          if (p->has[rd][j])
            continue;
          if ((cv = bkr94acsAcastValue(p->acs[rd], (unsigned char)j))) {
            memcpy(p->content[rd][j], cv, VLEN);
            p->has[rd][j] = 1;
          }
        }
        free(p->acs[rd]);
        p->acs[rd] = 0;
      }
      for (j = 0; j < LEGCAP; ++j)
        if (p->leg[j].inUse && p->leg[j].round == rd)
          legFree(&p->leg[j]);
      p->active = 1;
      break;

    case SYSTEM_ACT_ADOPT:
      /*
       * ADOPT does NOT close here.  system.h requires an instance to be
       * LIVE for the round before systemComplete does anything, and the
       * recovery traffic that provoked this only recorded participation
       * OWED -- the JOIN that discharges it fires at the next
       * systemLaunch.  Closing from here would be silently inert, and
       * ADOPT is single-fire, so the adoption would be lost.  Carry it
       * as a debt instead; the tick discharges it after its launch.
       */
      if (!p->candValid)
        break;
      p->adoptPending = 1;
      p->active = 1;
      break;

    case SYSTEM_ACT_JOIN:
    case SYSTEM_ACT_ADMIT:
    case SYSTEM_ACT_MAINTAIN: {
      struct bkr94acsAct one;
      unsigned char val[VLEN];

      if (rd >= MAX_ROUNDS)
        break;
      if (p->acs[rd])
        break;                     /* R2b: a resumed round keeps its
                                    * instance state; never re-execute */
      if (!(p->acs[rd] = calloc(1, AcsSz)))
        break;
      bkr94acsInit(p->acs[rd], (unsigned char)(N - 1), (unsigned char)T,
                   VLEN - 1, MAX_PHASES, p->self, demoCoin, 0);
      bracha87RetryInit(&p->cur[rd]);

      memset(val, 0, sizeof (val));
      if (sa[i].act == SYSTEM_ACT_MAINTAIN) {
        /*
         * O5: the contribution is the maintenance form, NEVER the
         * pending value.  A maintenance win is not the value's win, so
         * PRESENT stays outstanding and the value rides a later round.
         */
        memcpy(val, Maint, sizeof (Maint));
        p->rode[rd] = NO_MSG;
      } else if (p->msgHead < p->msgCnt) {
        /*
         * PRESENT, caller half: re-present the staged bytes
         * byte-identically.  A JOIN carries a pending value exactly as
         * an ADMIT does -- participation is contribution.
         */
        memcpy(val, p->msg[p->msgHead], VLEN);
        p->rode[rd] = p->msgHead;
      } else {
        memcpy(val, Empty, sizeof (Empty));
        p->rode[rd] = NO_MSG;
      }
      n = bkr94acsAcast(p->acs[rd], val, &one);
      p->active = 1;
      if (Verbose)
        printf("process %u: %s round %u with \"%s\"\n",
               (unsigned)p->self,
               sa[i].act == SYSTEM_ACT_JOIN ? "JOIN"
             : sa[i].act == SYSTEM_ACT_ADMIT ? "ADMIT" : "MAINTAIN",
               (unsigned)rd, (const char *)val);
      emitAcs(p, rd, &one, n);
      break;
    }

    default:
      break;
    }
  }
}

/*--------------------------------------------------------------------------*/
/*  The one consume region                                                  */
/*                                                                          */
/*  A live COMPLETE and an adoption both close HERE (system.md, the         */
/*  wanting side: one region -- two copies drift).  A racing own COMPLETE   */
/*  therefore structurally supersedes the witness book.                     */
/*--------------------------------------------------------------------------*/

static void
sysClose(
  struct proc *p
 ,unsigned char round
 ,const unsigned char *comp
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  unsigned char have[(MAX_PROCESSES + 7) / 8];
  unsigned char before;
  unsigned int n;
  unsigned int j;

  if (round >= MAX_ROUNDS)
    return;
  before = systemFrontier(p->sys);

  /*
   * The have grain at close (O2): members whose content this process
   * already holds.  Assembly is a separate gather and may lag, so this
   * is a subset of the membership; what lands later is reported by
   * systemAssembled.
   */
  memset(have, 0, sizeof (have));
  for (j = 0; j < N; ++j) {
    const unsigned char *cv;

    if (!comp[ANCHOR + j])
      continue;
    if (p->has[round][j]
     || (p->acs[round]
      && (cv = bkr94acsAcastValue(p->acs[round], (unsigned char)j)))) {
      if (!p->has[round][j] && p->acs[round]
       && (cv = bkr94acsAcastValue(p->acs[round], (unsigned char)j))) {
        memcpy(p->content[round][j], cv, VLEN);
        p->has[round][j] = 1;
      }
      have[j >> 3] |= (unsigned char)(1 << (j & 7));
      p->told[round][j] = 1;
    }
  }

  n = systemComplete(p->sys, round, have, sa);
  if (systemFrontier(p->sys) == before)
    return;                        /* refused: not the frontier, or no
                                    * live instance -- the machine's own
                                    * supersession guard */

  memcpy(p->comp[round], comp, COMPLEN);
  p->closed[round] = 1;
  p->adoptPending = 0;
  p->candValid = 0;
  p->tolCount = 0;
  p->active = 1;

  /*
   * PRESENT retirement, caller half: the staged value retires ONLY on
   * being witnessed as a member of an agreed subset.  Exclusion does
   * not retire it (R2d) and a maintenance win does not retire it (O5) --
   * both of those leave it staged to ride the next round, which is the
   * at-least-once half of R1.  Retiring here, exactly once, is the
   * at-most-once half.
   */
  if (comp[ANCHOR + p->self] && p->rode[round] != NO_MSG
   && p->rode[round] == p->msgHead)
    ++p->msgHead;

  if (Verbose) {
    printf("process %u: closed round %u, subset {", (unsigned)p->self,
           (unsigned)round);
    for (j = 0; j < N; ++j)
      if (comp[ANCHOR + j])
        printf(" %u", j);
    printf(" }\n");
  }

  applySysActs(p, sa, n, 0);

  /* Re-present the indications that arrived while this round was live. */
  for (j = 0; j < N; ++j) {
    struct systemAct pa[SYSTEM_MAX_ACTS];
    unsigned int pn;

    if (j == p->self || !p->pend[round][j])
      continue;
    p->pend[round][j] = 0;
    if (!systemRetained(p->sys, round))
      break;                       /* released underneath us */
    pn = systemPossessed(p->sys, round, (unsigned char)j, pa);
    applySysActs(p, pa, pn, 0);
  }
}

/*--------------------------------------------------------------------------*/
/*  Ingress                                                                 */
/*--------------------------------------------------------------------------*/

static void
deliverWire(
  const struct wire *w
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  struct proc *p;
  unsigned char f;
  unsigned char ab;
  unsigned int n;
  unsigned int i;

  p = &Proc[w->to];
  f = systemFrontier(p->sys);
  ab = aheadBy(w->sysRound, f);

  if (w->kind == WK_SERVE) {
    struct leg *lg;
    struct systemAct sa[SYSTEM_MAX_ACTS];
    unsigned char out[3];
    unsigned char legFrom;
    unsigned int k;

    if (ab && ab < 128)
      return;                      /* beyond chain reach: unverifiable */

    /*
     * Recovery traffic is TRAFFIC OF ITS ROUND (system.md, carrier
     * geometries): behind a receiver's frontier it classifies as
     * retained-round traffic and its tails carry possession evidence
     * like any other traffic of the round.  So it goes through the
     * machine's evidence surfaces exactly as ACS traffic does -- a
     * server's leg wires evidence its possession, a served process's
     * leg wires are want evidence, and the same O1 inference applies.
     *
     * Done BEFORE the leg is touched: a release this provokes frees
     * every leg of the round, and taking the pointer first would leave
     * us walking freed storage.  DELIVER cannot misfire here because
     * its handler routes only WK_ACS wires.
     */
    n = systemReceived(p->sys, w->sysRound, w->from, w->possesses, sa);
    applySysActs(p, sa, n, w);
    for (i = 0; i < MAX_ROUNDS; ++i) {
      if (aheadBy((unsigned char)i, w->sysRound) < 128)
        continue;
      if (!systemRetained(p->sys, (unsigned char)i))
        continue;
      n = systemPossessed(p->sys, (unsigned char)i, w->from, sa);
      applySysActs(p, sa, n, 0);
    }
    if (w->possesses && w->sysRound < MAX_ROUNDS && w->from < MAX_PROCESSES) {
      if (systemRetained(p->sys, w->sysRound)) {
        n = systemPossessed(p->sys, w->sysRound, w->from, sa);
        applySysActs(p, sa, n, 0);
      } else if (w->sysRound == systemFrontier(p->sys))
        p->pend[w->sysRound][w->from] = 1;
    }

    if (!(lg = legFind(p, w->legServer, w->legServed, w->sysRound))) {
      /*
       * Only the served end births its own leg on arrival, and only for
       * a round within reach; a server's leg is born by a SERVE act.
       */
      if (w->to != w->legServed)
        return;
      if (!(lg = legAlloc(p, w->legServer, w->legServed, w->sysRound)))
        return;
    }
    if (lg->retired)
      return;
    legFrom = (unsigned char)(w->from == lg->server ? 0 : 1);
    /*
     * Pitfall 17, at the bare Fig 1 layer: only the designated
     * initiator may send INITIAL.  The leg's initiator is its server.
     */
    if (w->type == BRACHA87_INITIAL && legFrom != 0)
      return;
    n = bracha87Fig1Input(lg->f1, w->type, legFrom, w->comp, out);
    for (k = 0; k < n; ++k) {
      if (out[k] == BRACHA87_ACCEPT) {
        legAccept(p, lg);
        /*
         * legAccept banks possession, which can complete an all-n
         * record and drive a RELEASE whose handler frees every leg of
         * this round -- lg included.  Re-find before touching it again.
         */
        if (!(lg = legFind(p, w->legServer, w->legServed, w->sysRound)))
          return;
      } else
        legEmit(p, lg, out[k]);
    }
    if (w->type == BRACHA87_READY && w->accepted) {
      bracha87Fig1ProcessAccepted(lg->f1, legFrom);
      lg->otherAcc = 1;
    }
    if (lg->selfAcc && lg->otherAcc)
      lg->retired = 1;             /* remote evidence, never local send */
    return;
  }

  /* WK_ACS */
  if (ab && ab < 128) {
    holdWire(p, w);                /* beyond reach: unverifiable, held */
    return;
  }

  n = systemReceived(p->sys, w->sysRound, w->from, w->possesses, sa);
  applySysActs(p, sa, n, w);

  /*
   * An indication riding LIVE-round traffic is dropped by the machine,
   * correctly: only retained rounds have a record.  system.h calls that
   * conservative because the O1 inference re-derives it from the
   * sender's later-round traffic once the round is retained -- but that
   * carrier only exists if a later round is ever launched.  When the
   * cohort runs out of application values, no later round is, and a
   * process that closes late never learns what its own live-round
   * traffic already told it.  The evidence is authenticated and true
   * when it arrives; it is only PREMATURE.  Hold it and re-present it
   * at the close.  Sound by the same containment as the indication
   * itself: it marks only its own sender.
   *
   * Held ONLY for the frontier round, which is the only round that can
   * become retained at the next close.  Keying the hold on "not
   * retained" instead would also catch RELEASED rounds, whose entries
   * nothing ever clears -- and in the wrapping round space a stale
   * entry would then be re-presented at the next incarnation of that
   * round byte, resurrecting evidence for a different round.
   */
  if (w->possesses && w->sysRound < MAX_ROUNDS && w->from < MAX_PROCESSES) {
    if (systemRetained(p->sys, w->sysRound)) {
      /* the round became retained while these acts were applied */
      n = systemPossessed(p->sys, w->sysRound, w->from, sa);
      applySysActs(p, sa, n, 0);
    } else if (w->sysRound == systemFrontier(p->sys))
      p->pend[w->sysRound][w->from] = 1;
  }

  /*
   * O1's linkage inference: an authenticated act of round R+1 OR LATER
   * evidences its sender's possession of R's composition.  "R+1 alone"
   * is the reading that strands any round whose immediate successor
   * never arrives -- walk EVERY retained round behind this wire's.
   */
  for (i = 0; i < MAX_ROUNDS; ++i) {
    if (aheadBy((unsigned char)i, w->sysRound) < 128)
      continue;                    /* not strictly behind the wire */
    if (!systemRetained(p->sys, (unsigned char)i))
      continue;
    n = systemPossessed(p->sys, (unsigned char)i, w->from, sa);
    applySysActs(p, sa, n, 0);
  }

  /*
   * Frontier traffic that produced no act found no live instance: it
   * recorded participation owed.  Hold it and re-present it after the
   * join, so the join's instance sees it without waiting a retry cycle.
   */
  if (!ab && !systemLive(p->sys) && w->sysRound < MAX_ROUNDS
   && !p->closed[w->sysRound])
    holdWire(p, w);
}

static void
refeedHeld(
  struct proc *p
){
  struct wire *snap;
  unsigned int cnt;
  unsigned int i;

  if (!(cnt = p->holdCnt))
    return;
  if (!(snap = calloc(cnt, sizeof (*snap))))
    return;
  memcpy(snap, p->hold, cnt * sizeof (*snap));
  p->holdCnt = 0;
  for (i = 0; i < cnt; ++i) {
    unsigned char ab;

    ab = aheadBy(snap[i].sysRound, systemFrontier(p->sys));
    if ((ab && ab < 128) || (!ab && !systemLive(p->sys)))
      holdWire(p, &snap[i]);       /* still unreachable / still no instance */
    else
      deliverWire(&snap[i]);
  }
  free(snap);
}

/*
 * Append this process's agreed-sequence positions for every closed
 * round whose content is in hand, in round order.  A round holds its
 * position until its content arrives or until it is released -- content
 * may assemble past the composition (O2), so an unreleased round with a
 * gap is still waiting, not yet a hole.  'final' is the teardown pass:
 * the run is over, so what is still missing is missing for good and the
 * position is recorded as an out-of-band hole.
 */
static void
seqEmit(
  struct proc *p
 ,unsigned int final
){
  unsigned int j;
  unsigned int k;

  for (j = 0; j < MAX_ROUNDS; ++j) {
    unsigned int ready;

    if (!p->closed[j])
      break;                       /* the sequence is ordered: stop here */
    if (p->emitted[j])
      continue;
    ready = 1;
    if (!final)
      for (k = 0; k < N; ++k)
        if (p->comp[j][ANCHOR + k] && !p->has[j][k]
         && systemRetained(p->sys, (unsigned char)j))
          ready = 0;
    if (!ready)
      break;
    for (k = 0; k < N; ++k) {
      if (!p->comp[j][ANCHOR + k])
        continue;
      if (p->seqCnt >= sizeof (p->seq) / sizeof (p->seq[0]))
        break;
      if (p->has[j][k])
        memcpy(p->seq[p->seqCnt], p->content[j][k], VLEN);
      else
        p->seqHole[p->seqCnt] = 1;
      ++p->seqCnt;
    }
    p->emitted[j] = 1;
  }
}

/*--------------------------------------------------------------------------*/
/*  Main                                                                    */
/*--------------------------------------------------------------------------*/

int
main(
  int argc
 ,char *argv[]
){
  unsigned int msgs;
  unsigned int seed;
  unsigned int origSeed;
  unsigned int tick;
  unsigned int idle;
  unsigned int i;
  unsigned int j;
  unsigned int k;
  int arg;
  int exitCode;
  unsigned long sysSz;

  unsigned int seqOk;
  unsigned int onceOk;
  unsigned int holes;

  Verbose = 0;
  DropPct = 4;
  Tp = 30;
  Sp = 0;
  MaintEvery = 0;
  seed = 1;
  exitCode = 0;

  arg = 1;
  while (arg < argc && argv[arg][0] == '-') {
    if (argv[arg][1] == 'v' && argv[arg][2] == '\0') {
      ++Verbose;
      ++arg;
    } else if (argv[arg][1] == 's' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      seed = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'l' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      DropPct = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'm' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      MaintEvery = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'T' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      Tp = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'S' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      Sp = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'L' && argv[arg][2] == '\0') {
      char *c;

      if (++arg >= argc) goto usage;
      LagProc = atoi(argv[arg]);
      if (!(c = strchr(argv[arg], ':')))
        goto usage;
      LagRound = atoi(c + 1);
      ++arg;
    } else if (argv[arg][1] == 'B' && argv[arg][2] == '\0') {
      char *c;

      if (++arg >= argc) goto usage;
      ByzProc = atoi(argv[arg]);
      ByzMode = (c = strchr(argv[arg], ':'))
              ? (unsigned int)atoi(c + 1)
              : (BYZ_POSSESS | BYZ_CAND);
      ++arg;
    } else
      goto usage;
  }

  if (argc - arg < 4) goto usage;
  N = (unsigned int)atoi(argv[arg++]);
  T = (unsigned int)atoi(argv[arg++]);
  Wr = (unsigned int)atoi(argv[arg++]);
  msgs = (unsigned int)atoi(argv[arg++]);

  if (N < 2 || N > MAX_PROCESSES) {
    fprintf(stderr, "n must be 2..%d\n", MAX_PROCESSES);
    return (1);
  }
  if (N < 3 * T + 1) {
    fprintf(stderr, "need n >= 3t + 1 (n=%u, t=%u)\n", N, T);
    return (1);
  }
  if (Wr < 1 || Wr > MAX_ROUNDS) {
    fprintf(stderr, "w must be 1..%d\n", MAX_ROUNDS);
    return (1);
  }
  if (msgs < 1 || msgs > MAX_MSGS) {
    fprintf(stderr, "msgs must be 1..%d\n", MAX_MSGS);
    return (1);
  }
  if (DropPct > 90) {
    fprintf(stderr, "loss must be 0..90\n");
    return (1);
  }
  /*
   * S > T_p is required, not tuned (system.md R4): the progress budget
   * must outlast the duty budget, or a process would abandon a round it
   * is still legitimately holding for.
   */
  if (!Sp)
    Sp = 2 * Tp;
  if (Sp <= Tp) {
    fprintf(stderr, "need S > Tp (S=%u, Tp=%u)\n", Sp, Tp);
    return (1);
  }
  if (LagProc >= 0 && ((unsigned int)LagProc >= N || LagRound < 0)) {
    fprintf(stderr, "-L proc:round out of range\n");
    return (1);
  }
  if (ByzProc >= 0) {
    if ((unsigned int)ByzProc >= N) {
      fprintf(stderr, "-B proc out of range\n");
      return (1);
    }
    if (!T) {
      fprintf(stderr, "-B needs t >= 1 (one Byzantine process must fit"
                      " inside the fault budget)\n");
      return (1);
    }
    if (!ByzMode || (ByzMode & ~(unsigned int)(BYZ_POSSESS | BYZ_WANT
                                             | BYZ_CAND | BYZ_LINK))) {
      fprintf(stderr, "-B mode is a mask of 1 (forge possession),"
                      " 2 (forge want), 4 (fake candidate),"
                      " 8 (unchained candidate)\n");
      return (1);
    }
    if ((ByzMode & BYZ_POSSESS) && (ByzMode & BYZ_WANT)) {
      fprintf(stderr, "-B modes 1 and 2 are contradictory lies about the"
                      " same bit\n");
      return (1);
    }
    if (ByzProc == LagProc) {
      fprintf(stderr, "-B and -L must name different processes\n");
      return (1);
    }
    /*
     * A partitioned process is not distinguishable from a faulty one
     * under unbounded latency, so R4 charges it to the same budget the
     * Byzantine process is charged to.  Running both at t = 1 asks a
     * system to tolerate two faults; it will correctly refuse to
     * advance, which is the model, not a defect.
     */
    if (LagProc >= 0 && T < 2) {
      fprintf(stderr, "-B with -L is two faults: needs t >= 2"
                      " (a partitioned process spends the same budget)\n");
      return (1);
    }
  }

  origSeed = seed;
  Rng = seed ? seed : 1;
  AcsSz = bkr94acsSz(N - 1, VLEN - 1, MAX_PHASES);
  LegSz = bracha87Fig1Sz(1, COMPLEN - 1);
  sysSz = systemSz(N - 1, Wr - 1);

  if (!(Queue = calloc(QCAP, sizeof (*Queue)))) {
    fprintf(stderr, "queue allocation failed\n");
    return (1);
  }

  for (i = 0; i < N; ++i) {
    struct proc *p;

    p = &Proc[i];
    p->self = (unsigned char)i;
    if (!(p->sys = calloc(1, sysSz))
     || !(p->hold = calloc(HOLDCAP, sizeof (*p->hold)))) {
      fprintf(stderr, "allocation failed\n");
      exitCode = 1;
      goto cleanup;
    }
    systemInit(p->sys, (unsigned char)(N - 1), (unsigned char)T,
               (unsigned char)(Wr - 1), (unsigned char)i);
    memset(p->rode, NO_MSG, sizeof (p->rode));
    /*
     * Stage this process's application messages: the layer has ACCEPTED
     * them, so from here each is an obligation, not a preference (R1).
     * Both indices are single digits by MAX_PROCESSES / MAX_MSGS.
     */
    p->msgCnt = (unsigned char)msgs;
    for (j = 0; j < msgs; ++j) {
      p->msg[j][0] = 'p';
      p->msg[j][1] = (unsigned char)('0' + i);
      p->msg[j][2] = 'm';
      p->msg[j][3] = (unsigned char)('0' + j);
    }
  }

  printf("--- system layer over BKR94 ACS "
         "(n=%u, t=%u, w=%u, msgs/process=%u, loss=%u%%, seed=%u"
         ", Tp=%u, S=%u", N, T, Wr, msgs, DropPct, origSeed, Tp, Sp);
  if (MaintEvery)
    printf(", maintenance every %u", MaintEvery);
  if (LagProc >= 0)
    printf(", cut process %d at round %d", LagProc, LagRound);
  printf(") ---\n\n");

  /*----------------------------------------------------------------------*/
  /*  The tick loop                                                        */
  /*                                                                      */
  /*  The tick is a wire rate limit, never a correctness clock: nothing    */
  /*  below reads elapsed time, and every budget is counted in this        */
  /*  process's OWN sweeps.                                                */
  /*----------------------------------------------------------------------*/

  idle = 0;
  for (tick = 0; tick < MAX_TICKS; ++tick) {
    struct wire w;
    unsigned int done;

    Pushed = 0;

    for (k = 0; k < DRAIN && qPopRandom(&w); ++k)
      deliverWire(&w);

    for (i = 0; i < N; ++i) {
      struct proc *p;
      struct systemAct sa[SYSTEM_MAX_ACTS];
      struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
      unsigned char f;
      unsigned int duty;
      unsigned int n;

      p = &Proc[i];
      p->active = 0;

      refeedHeld(p);

      f = systemFrontier(p->sys);
      duty = systemDuty(p->sys);

      /*
       * R4, the caller's half: T_p is a count of THIS process's own
       * sweeps since the class first read TOLERANCE, reset when the
       * frontier advances.  It is self-local by construction -- no
       * adversary can hold it shut or force it open (axiom A6).
       */
      if (duty == SYSTEM_DUTY_TOLERANCE)
        ++p->tolCount;
      else
        p->tolCount = 0;
      p->tolElapsed = (unsigned char)(duty == SYSTEM_DUTY_TOLERANCE
                                   && p->tolCount >= Tp);

      /*
       * backlogDrained is literal 1 here and that is honest, not a stub:
       * only the frontier round has a live instance in this composition,
       * so there is no multi-round fleet whose depth a capacity gate
       * could measure.  A deployment that runs many rounds' carriers
       * concurrently derives it from that depth.
       */
      n = systemLaunch(p->sys,
                       (unsigned char)(p->msgHead < p->msgCnt),
                       (unsigned char)(MaintEvery && f
                                    && (f % MaintEvery) == 0),
                       1,
                       p->tolElapsed,
                       sa);
      applySysActs(p, sa, n, 0);

      /*
       * Discharge an adoption debt AFTER the tick's own launch: the
       * close needs a live instance (systemComplete is inert without
       * one), so JOIN first, then re-read the frontier and close
       * through the one consume region.
       *
       * In THIS glue the instance is always already live: want evidence
       * is born from acts of round R, so a process being served R has
       * launched R.  The JOIN below is the path for a caller whose
       * recovery traffic reaches systemReceived and records
       * participation owed -- system.h's stated ADOPT shape, kept here
       * because a deployment copying this file needs it.
       */
      if (p->adoptPending && p->candValid) {
        if (!systemLive(p->sys)) {
          n = systemLaunch(p->sys, 0, 0, 1, p->tolElapsed, sa);
          applySysActs(p, sa, n, 0);
        }
        if (systemLive(p->sys)) {
          unsigned char cand[COMPLEN];
          unsigned char wit[(MAX_PROCESSES + 7) / 8];
          const unsigned char *wp;
          unsigned char adRound;

          adRound = systemFrontier(p->sys);
          memcpy(cand, p->cand, COMPLEN);
          /*
           * Snapshot the witness book BEFORE the close -- the close is
           * the one consume region and clears it.  Every witness SERVED
           * this composition, and a server has by construction closed
           * and retained the round, so each is the strongest possession
           * evidence there is.  Banking it is not optional bookkeeping:
           * an adopted round is born possessed by self alone, and until
           * the record reaches n-t the NEXT frontier reads HELD.  With
           * the cohort quiesced there is no later-round traffic for the
           * O1 inference to ride, so a healed process would stall one
           * round after healing.  t+1 witnesses plus self is at least
           * n-t at n = 3t+1, which is exactly the TOLERANCE escape.
           */
          memset(wit, 0, sizeof (wit));
          if ((wp = systemWitnesses(p->sys)))
            memcpy(wit, wp, sizeof (wit));
          p->adoptPending = 0;
          ++p->adopts;
          if (Verbose)
            printf("process %u: ADOPT round %u from served evidence\n",
                   (unsigned)p->self, (unsigned)adRound);
          sysClose(p, adRound, cand);
          if (systemRetained(p->sys, adRound))
            for (j = 0; j < N; ++j) {
              if (j == p->self || !SYSTEM_TST(wit, j))
                continue;
              n = systemPossessed(p->sys, adRound, (unsigned char)j, sa);
              applySysActs(p, sa, n, 0);
            }
        }
      }

      /* ACS tails: one instance per tick, round-robin (the flood rule). */
      for (j = 0; j < MAX_ROUNDS; ++j) {
        unsigned char rd;

        rd = (unsigned char)((p->retryCursor + j) % MAX_ROUNDS);
        if (!p->acs[rd])
          continue;
        n = bkr94acsRetry(p->acs[rd], &p->cur[rd], out);
        emitAcs(p, rd, out, n);
        p->retryCursor = (unsigned char)((rd + 1) % MAX_ROUNDS);
        break;
      }

      /* Leg tails: BPR on every live leg. */
      for (j = 0; j < LEGCAP; ++j) {
        struct leg *lg;
        unsigned char legOut[BRACHA87_FIG1_RETRY_MAX_ACTS];
        unsigned int m;

        lg = &p->leg[j];
        if (!lg->inUse || lg->retired || !lg->f1)
          continue;
        n = bracha87Fig1Bpr(lg->f1, legOut);
        for (m = 0; m < n; ++m)
          legEmit(p, lg, legOut[m]);
      }

      /* The serve walk -- one owed round per tick, cursor is ours. */
      n = systemServe(p->sys, &p->serveCursor, sa);
      applySysActs(p, sa, n, 0);

      /*
       * Late assembly (O2).  This caller is BOOKLESS -- it has no
       * content record the machine could not tell it about -- so it
       * reports every member's content as it lands and reads .have as
       * its own record.
       */
      for (j = 0; j < MAX_ROUNDS; ++j) {
        if (!p->closed[j] || !p->acs[j])
          continue;
        for (k = 0; k < N; ++k) {
          const unsigned char *cv;

          if (!p->comp[j][ANCHOR + k] || p->has[j][k])
            continue;
          if (!(cv = bkr94acsAcastValue(p->acs[j], (unsigned char)k)))
            continue;
          memcpy(p->content[j][k], cv, VLEN);
          p->has[j][k] = 1;
          p->active = 1;
          if (!p->told[j][k] && systemRetained(p->sys, (unsigned char)j)) {
            systemAssembled(p->sys, (unsigned char)j, (unsigned char)k);
            p->told[j][k] = 1;
          }
        }
      }

      /* Retention never exceeds the window -- bounded memory, one column. */
      {
        unsigned int ret;

        ret = 0;
        for (j = 0; j < MAX_ROUNDS; ++j)
          if (systemRetained(p->sys, (unsigned char)j))
            ++ret;
        if (ret > p->maxRetained)
          p->maxRetained = ret;
      }

      /*
       * The barren-sweep meter: the S budget, counted in this process's
       * own sweeps.  A process is PARTITIONED by DEFAULT -- it is the
       * state it cannot disprove -- and the lapse of this budget is the
       * proof of participation expiring, not a network condition being
       * read.  No process ever classifies another, and the classified
       * process keeps stepping: its blind re-offers at the stale cursor
       * ARE the want evidence servers observe.
       */
      if (p->active
       || duty == SYSTEM_DUTY_TOLERANCE
       || !(p->msgHead < p->msgCnt || systemLive(p->sys)
         || systemOwed(p->sys)))
        p->barren = 0;             /* Neither of these is barren.  A
                                    * process with nothing to send is a
                                    * participant, and so is one holding
                                    * under TOLERANCE -- that hold is
                                    * funded catch-up time for the tail,
                                    * not idleness (R4).  A HELD strand
                                    * is the case that does accrue: it
                                    * reads no progress evidence and
                                    * proves no participation. */
      else if (++p->barren >= Sp) {
        if (!p->partitioned && Verbose)
          printf("process %u: self-classified PARTITIONED at round %u"
                 " (barren sweeps exhausted)\n",
                 (unsigned)p->self, (unsigned)systemFrontier(p->sys));
        p->partitioned = 1;
        p->barren = 0;             /* re-arm: the budget meters each
                                    * lapse, it does not latch */
        /*
         * The lapse ENDS at the classification.  It is tempting to act
         * on a HELD frontier by evicting the round below -- duty is
         * bounded by retention, so a released round reads MET -- and
         * that is wrong twice over.  R4 gives HELD no budget escape by
         * design (only TOLERANCE has one, and T_p is it), and
         * systemEvict is the artifact BYTE budget's actuator, not a
         * lever for shedding duty.  Doing it anyway drops rounds that
         * correct processes are still being served: it frees the
         * retained record a heal is accumulating witnesses against, and
         * releasing below t+1 surviving retainers makes the witness
         * ground unreachable forever.  The classified process keeps
         * stepping instead -- its blind re-offers at the stale cursor
         * ARE the want evidence servers observe.
         */
      }
    }

    for (i = 0; i < N; ++i)
      seqEmit(&Proc[i], 0);

    done = 1;
    for (i = 0; i < N; ++i)
      if (Proc[i].msgHead < Proc[i].msgCnt)
        done = 0;
    if (done && !Qcount && !Pushed) {
      if (++idle >= IDLE_STOP)
        break;
    } else
      idle = 0;
  }

  /*----------------------------------------------------------------------*/
  /*  Results                                                             */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < N; ++i)
    seqEmit(&Proc[i], 1);

  printf("\n--- Agreed sequences ---\n");
  for (i = 0; i < N; ++i) {
    struct proc *p;

    p = &Proc[i];
    printf("process %u%s (frontier %u, duty %s, staged %u/%u,"
           " adoptions %u, max retained %u):\n",
           i, (int)i == ByzProc ? " [BYZANTINE]" : "",
           (unsigned)systemFrontier(p->sys),
           systemDuty(p->sys) == SYSTEM_DUTY_MET ? "MET"
         : systemDuty(p->sys) == SYSTEM_DUTY_TOLERANCE ? "TOLERANCE" : "HELD",
           (unsigned)p->msgHead, (unsigned)p->msgCnt,
           p->adopts, p->maxRetained);
    printf("  ");
    for (j = 0; j < p->seqCnt; ++j)
      printf("%s%s", j ? " " : "",
             p->seqHole[j] ? "<out-of-band>" : (const char *)p->seq[j]);
    printf("\n");
  }

  /*
   * Verdict 1 -- sequence identity at every position both hold (L6).
   * Compared PAIRWISE, not against one baseline: with a baseline, two
   * processes could disagree at a position the baseline happens to hold
   * as a hole and nothing would notice.
   */
  seqOk = 1;
  for (i = 0; i < N; ++i) {
    if ((int)i == ByzProc)
      continue;                    /* every guarantee is over CORRECT
                                    * processes; a Byzantine process's
                                    * own state proves nothing */
    for (j = i + 1; j < N; ++j) {
      unsigned int m;

      if ((int)j == ByzProc)
        continue;
      for (m = 0; m < Proc[i].seqCnt && m < Proc[j].seqCnt; ++m) {
        if (Proc[i].seqHole[m] || Proc[j].seqHole[m])
          continue;                /* a position one of them does not hold */
        if (memcmp(Proc[i].seq[m], Proc[j].seq[m], VLEN))
          seqOk = 0;
      }
    }
  }

  /* Verdict 2 -- exactly-once (R1), over correct processes. */
  onceOk = 1;
  for (i = 0; i < N; ++i) {
    struct proc *p;

    if ((int)i == ByzProc)
      continue;
    p = &Proc[i];
    for (j = 0; j < p->msgCnt; ++j) {
      unsigned int seen;

      seen = 0;
      for (k = 0; k < p->seqCnt; ++k)
        if (!p->seqHole[k] && !memcmp(p->seq[k], p->msg[j], VLEN))
          ++seen;
      if (seen != 1) {
        onceOk = 0;
        printf("process %u: \"%s\" appears %u times (want exactly 1)\n",
               i, (const char *)p->msg[j], seen);
      }
    }
  }

  holes = 0;
  for (i = 0; i < N; ++i)
    for (j = 0; j < Proc[i].seqCnt && (int)i != ByzProc; ++j)
      if (Proc[i].seqHole[j])
        ++holes;

  printf("\n--- Verdicts ---\n");
  printf("sequence identity (L6, positions both hold): %s\n",
         seqOk ? "ok" : "FAIL");
  printf("exactly-once presentation (R1):              %s\n",
         onceOk ? "ok" : "FAIL");
  if (ByzProc >= 0) {
    unsigned int compOk;

    /*
     * Byzantine containment.  The sharpest single check the system
     * layer's own notes license: no correct process ever HOLDS a
     * composition another correct process disagrees with at the same
     * round.  A fabricated assertion that reached adoption anywhere
     * would show up here as two correct processes holding different
     * bytes for one round, and it would then also fork the sequence.
     */
    compOk = 1;
    for (i = 0; i < N; ++i) {
      if ((int)i == ByzProc)
        continue;
      for (j = i + 1; j < N; ++j) {
        if ((int)j == ByzProc)
          continue;
        for (k = 0; k < MAX_ROUNDS; ++k)
          if (Proc[i].closed[k] && Proc[j].closed[k]
           && memcmp(Proc[i].comp[k], Proc[j].comp[k], COMPLEN))
            compOk = 0;
      }
    }
    printf("Byzantine containment (process %d, mode %u):   %s\n",
           ByzProc, ByzMode, compOk ? "ok" : "FAIL");
    if (!compOk)
      exitCode = 1;
    /*
     * Say whether the arm was EXERCISED.  A containment verdict that
     * passes because the attack never happened proves nothing, and
     * silence about that is how a demonstration comes to lie.
     */
    if (ByzMode & (BYZ_CAND | BYZ_LINK))
      printf("  fabricated compositions served: %u; refused outright by the"
             " fold ground: %u;\n  reaching the book, so needing the t+1"
             " grouping to discriminate: %u%s\n",
             ByzAsserts, CandRejected, CandConflicts,
             ByzAsserts ? "" : "  <- attack never reached a served leg");
  }
  if (LagProc >= 0) {
    unsigned int healed;
    unsigned int maxF;

    /*
     * "Rejoined" must be TESTED, not asserted.  Retiring the staged
     * values alone passes vacuously in any config whose values all land
     * before the cut round -- the cut is then never exercised and the
     * process was never behind.  Require that the cut round was
     * actually reached, that an adoption happened (the cut is total for
     * that round, so it can close no other way), and that the frontier
     * caught up with the cohort.
     */
    maxF = 0;
    for (i = 0; i < N; ++i)
      if ((unsigned int)systemFrontier(Proc[i].sys) > maxF)
        maxF = systemFrontier(Proc[i].sys);
    if ((unsigned int)LagRound >= maxF) {
      printf("cut process %d healed and rejoined:           "
             "not exercised (round %d never reached)\n", LagProc, LagRound);
    } else {
      healed = (unsigned int)(Proc[LagProc].msgHead == Proc[LagProc].msgCnt
                           && Proc[LagProc].adopts >= 1
                           && (unsigned int)systemFrontier(Proc[LagProc].sys)
                              == maxF);
      printf("cut process %d healed and rejoined:           %s"
             " (%u adoption%s, frontier %u of %u)\n",
             LagProc, healed ? "ok" : "FAIL", Proc[LagProc].adopts,
             Proc[LagProc].adopts == 1 ? "" : "s",
             (unsigned)systemFrontier(Proc[LagProc].sys), maxF);
      if (!healed)
        exitCode = 1;
    }
  }
  {
    unsigned int boundOk;

    boundOk = 1;
    for (i = 0; i < N; ++i)
      if (Proc[i].maxRetained > Wr)
        boundOk = 0;
    printf("retention bounded by w (%u):                  %s\n",
           Wr, boundOk ? "ok" : "FAIL");
    if (!boundOk)
      exitCode = 1;
  }
  if (holes)
    printf("out-of-band content holes: %u"
           " (per-member cost O2 prices, never a round's failure)\n", holes);
  /*
   * Soundness of the fold ground, reported unconditionally: it must
   * never refuse a TRUE assertion, so with no Byzantine process present
   * this count must be zero.  A non-zero value here without -B is a
   * defect in the checks, not a defence working.
   */
  if (ByzProc < 0 && (CandRejected || CandConflicts))
    printf("fold ground refused %u assertion(s) and saw %u candidate"
           " conflict(s) with NO Byzantine process -- this is a defect\n",
           CandRejected, CandConflicts);
  for (i = 0; i < N; ++i)
    if (Proc[i].partitioned && (int)i != ByzProc)
      printf("process %u self-classified PARTITIONED\n", i);

  if (!seqOk || !onceOk)
    exitCode = 1;

  /*----------------------------------------------------------------------*/
  /*  Cleanup                                                             */
  /*----------------------------------------------------------------------*/

cleanup:
  for (i = 0; i < N; ++i) {
    for (j = 0; j < MAX_ROUNDS; ++j)
      free(Proc[i].acs[j]);
    for (j = 0; j < LEGCAP; ++j)
      free(Proc[i].leg[j].f1);
    free(Proc[i].hold);
    free(Proc[i].sys);
  }
  free(Queue);

  return (exitCode);

usage:
  fprintf(stderr,
    "usage: example_system [-v] [-s seed] [-l loss] [-L proc:round]\n"
    "                      [-B proc:mode] [-m every] [-T Tp] [-S sweeps]\n"
    "                      n t w msgs\n"
    "  n            total processes (2-%d)\n"
    "  t            max Byzantine faults (n >= 3t + 1)\n"
    "  w            retained window in rounds (1-%d)\n"
    "  msgs         application messages staged per process (1-%d)\n",
    MAX_PROCESSES, MAX_ROUNDS, MAX_MSGS);
  fprintf(stderr,
    "  -v           verbose: trace launches, closes, adoptions\n"
    "  -s seed      delivery-order / loss seed\n"
    "  -l loss      percent of inter-process wires dropped (default 4)\n"
    "  -L proc:rnd  cut proc off from all other processes' round-rnd\n"
    "               ACS traffic -- it must heal by adoption\n");
  fprintf(stderr,
    "  -B proc:mode Byzantine process (needs t >= 1); mode is a mask of\n"
    "               1 forge possession, 2 forge want, 4 fake candidate,\n"
    "               8 unchained candidate (default 5); 1 and 2 are\n"
    "               contradictory\n"
    "  -m every     identity-maintenance round every 'every' rounds (O5)\n"
    "  -T Tp        duty budget: own sweeps under TOLERANCE (default 30)\n"
    "  -S sweeps    progress budget, must exceed Tp (default 2*Tp)\n");
  return (1);
}
