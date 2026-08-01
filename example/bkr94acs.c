/*
 * asynchronousByzantineAgreementProtocols - Example BKR94 ACS program
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
 * bkr94acs.c -- Standalone demonstration of the BKR94 Asynchronous
 * Common Subset protocol (Ben-Or/Kelmer/Rabin 1994 Section 4
 * Figure 3, composing Bracha87 Figures 1 and 4).
 *
 * Each of N processes A-Casts a string value. The BKR94 ACS protocol
 * ensures all honest processes agree on the same common subset of
 * A-Casts (at least n-t). The subset is then sorted
 * deterministically so every process outputs the same ordering -- the
 * deterministic ordering the caller chooses over SubSet.
 *
 * Scope: this demo runs in a single process with a synchronous
 * in-memory queue -- every message is delivered, no loss, no
 * asynchrony; delivery order is deterministic unless -s is given.
 * It exercises the protocol state machines and the BPR retry but
 * does NOT exercise the deployment-time abandonment policy (the
 * barren-sweep gate) needed under real asynchronous transport;
 * that is inherently coupled to message loss, partial ordering,
 * and the failure modes those introduce. See README.md
 * "Abandonment" for the design.
 *
 * Build:
 *   (from project root) make example_bkr94acs
 *
 * Usage:
 *   ./example_bkr94acs [-v] [-s seed] [-d process] [-g budget] n t acast0 ...
 *
 * Example:
 *   ./example_bkr94acs 4 1 joe sam sally tim
 *
 * The sweep-side pacing pair (-d names the WAN laggard, -g the grace):
 *   ./example_bkr94acs -d 3 4 1 joe sam sally tim
 *     the eager schedule (budget 0) excludes the delayed honest
 *     process: SubSet = 3 of 4, its value accepted everywhere but
 *     excluded -- participation loss, not value loss
 *   ./example_bkr94acs -d 3 -g 8 4 1 joe sam sally tim
 *     the identical schedule under an 8-sweep grace includes it:
 *     SubSet = 4 of 4, step 2 never fires
 * What the pair deliberately shows the eager run giving up is the
 * whole demonstration; a run without -d never even enables step 2
 * (every A-Cast enters 1 before three BAs decide).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bkr94acs.h"

/*--------------------------------------------------------------------------*/
/*  Constants                                                               */
/*--------------------------------------------------------------------------*/

#define MAX_PROCESSES  16
#define MAX_PHASES 10
#define MAX_VLEN   256  /* max A-Cast bytes (including \0); bracha87 vLen encoding 255 */

/*--------------------------------------------------------------------------*/
/*  Message queue -- simulated network                                      */
/*                                                                          */
/*  Two message classes share one queue:                                    */
/*    BKR94ACS_CLS_ACAST -- Fig1 messages carrying A-Cast values            */
/*    BKR94ACS_CLS_BA    -- Fig1 messages for per-process binary BA         */
/*                                                                          */
/*  Class + Bracha87 type (and, for BA, the binary value + D_FLAG)          */
/*  ride in ONE clsType byte per the canonical packed-byte layout in        */
/*  bkr94acs.h.  qPush composes it; the process loop recovers it.  An       */
/*  ACAST carries its value in value[]; BA folds its payload into           */
/*  clsType and leaves value[] unused.  A READY also carries the            */
/*  BKR94ACS_ACCEPTED bit (bit 4, class-independent): egress sets it from   */
/*  the act's .accepted flag, ingress routes it to bkr94acs*Accepted --     */
/*  the BPR per-process READY retire and the all-n quiescence gate.         */
/*--------------------------------------------------------------------------*/

struct msg {
  unsigned char clsType;     /* class|type; +ACCEPTED on a READY;
                              * +cv,D_FLAG for BA (see above) */
  unsigned char process;      /* which process */
  unsigned char round;       /* BA round (class=BA only) */
  unsigned char initiator; /* who initiated this Fig1 broadcast (BA) */
  unsigned char from;        /* sender */
  unsigned char to;          /* recipient */
  unsigned char value[MAX_VLEN];  /* ACAST value (vLen+1 bytes); unused for BA */
};

static struct msg *MsgQ;
static unsigned int Qcap;
static unsigned int Qhead;
static unsigned int Qtail;

static int
qAlloc(
  unsigned int cap
){
  MsgQ = (struct msg *)calloc(cap, sizeof (struct msg));
  if (!MsgQ)
    return (-1);
  Qcap = cap;
  Qhead = Qtail = 0;
  return (0);
}

static void
qFree(
  void
){
  free(MsgQ);
  MsgQ = 0;
}

static void
qPush(
  unsigned char cls
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char type
 ,unsigned char accepted /* READY only: act's .accepted -> wire bit 4 */
 ,unsigned char from
 ,unsigned char to
 ,const unsigned char *value
 ,unsigned int valueLen
){
  if (Qtail >= Qcap)
    return;
  MsgQ[Qtail].process = process;
  MsgQ[Qtail].round = round;
  MsgQ[Qtail].initiator = initiator;
  MsgQ[Qtail].from = from;
  MsgQ[Qtail].to = to;
  if (cls == BKR94ACS_CLS_BA)
    /*
     * BA payload is two live bits: the binary value (placed at
     * bit 3) and BRACHA87_D_FLAG (kept at its native bit 7).  Fold both
     * into the wire byte alongside class, type, and the ACCEPTED
     * annotation; no value byte is sent.
     */
    MsgQ[Qtail].clsType = (unsigned char)
      (cls | type
       | (accepted ? BKR94ACS_ACCEPTED : 0)
       | ((value[0] & 1) << 3)
       | (value[0] & BRACHA87_D_FLAG));
  else {
    /* A-Cast payload is real bytes; pack class, type, ACCEPTED. */
    MsgQ[Qtail].clsType = (unsigned char)
      (cls | type
       | (accepted ? BKR94ACS_ACCEPTED : 0));
    memcpy(MsgQ[Qtail].value, value, valueLen);
  }
  ++Qtail;
}

/*
 * Fisher-Yates shuffle of the unprocessed portion of the queue.
 */
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

/*--------------------------------------------------------------------------*/
/*  Coin -- deterministic alternating, adequate for demonstration only.     */
/*--------------------------------------------------------------------------*/

static unsigned char
demoCoin(
  void *closure
 ,unsigned char phase
){
  (void)closure;
  return (phase % 2);
}

/*--------------------------------------------------------------------------*/
/*  Verbose helpers                                                         */
/*--------------------------------------------------------------------------*/

static const char *
typeName(
  unsigned char type
){
  switch (type) {
  case BRACHA87_INITIAL: return ("INITIAL");
  case BRACHA87_ECHO:    return ("ECHO");
  case BRACHA87_READY:   return ("READY");
  default:               return ("???");
  }
}

/*--------------------------------------------------------------------------*/
/*  Act framing -- the canonical packed-byte template                       */
/*                                                                          */
/*  Three sources hand the pump acts of the same kinds: a delivery          */
/*  (bkr94acs{Acast,Ba}Input), the step-2 fanout (bkr94acsFanout), and a     */
/*  BA round turn (bkr94acsTurn).  They differ only in what enabled them,    */
/*  never in how their acts are framed, so all three route here: the wire    */
/*  byte composed per bkr94acs.h, the BPR per-process suppress mask          */
/*  honored, and the three terminal acts reported.  'tag' names the source   */
/*  in the verbose trace.                                                   */
/*--------------------------------------------------------------------------*/

static void
qActs(
  const struct bkr94acsAct *acts
 ,unsigned int nacts
 ,unsigned char self
 ,unsigned int n
 ,unsigned int vLen
 ,unsigned int verbose
 ,const char *tag
){
  unsigned int k;
  unsigned int p;

  for (k = 0; k < nacts; ++k) {
    switch (acts[k].act) {

    case BKR94ACS_ACT_ACAST_SEND:
      if (!acts[k].value)
        break;
      if (verbose)
        printf("process %u: -> ACAST %s(process=%u)%s\n",
               (unsigned)self, typeName(acts[k].type),
               (unsigned)acts[k].process, tag);
      for (p = 0; p < n; ++p) {
        /* BPR per-process suppression: a transport skips recipients the
         * action is provably no longer owed to (echoed -> INITIAL,
         * readied -> ECHO, accepted -> READY).  Sound under loss; here
         * (no loss) it merely trims redundant retries.  The READY
         * suppress mask is fed by the ACCEPTED wire bit this loop
         * round-trips (egress .accepted -> bit 4 -> ingress
         * bkr94acs*Accepted); the same round-trip is exercised under
         * loss in test_bkr94acs_blackbox.c. */
        if (acts[k].skip && BRACHA87_SKIP_TST(acts[k].skip, p))
          continue;
        qPush(BKR94ACS_CLS_ACAST, acts[k].process, 0, 0,
              acts[k].type, acts[k].accepted, self, (unsigned char)p,
              acts[k].value, vLen);
      }
      break;

    case BKR94ACS_ACT_BA_SEND:
      if (verbose)
        printf("process %u: -> BA %s(process=%u, round=%u, initiator=%u, val=%u)%s\n",
               (unsigned)self, typeName(acts[k].type),
               (unsigned)acts[k].process, (unsigned)acts[k].round,
               (unsigned)acts[k].initiator,
               (unsigned)acts[k].baValue, tag);
      for (p = 0; p < n; ++p) {
        if (acts[k].skip && BRACHA87_SKIP_TST(acts[k].skip, p))
          continue;
        qPush(BKR94ACS_CLS_BA, acts[k].process, acts[k].round,
              acts[k].initiator, acts[k].type, acts[k].accepted,
              self, (unsigned char)p,
              &acts[k].baValue, 1);
      }
      break;

    case BKR94ACS_ACT_BA_DECIDED:
      if (verbose)
        printf("process %u: BA[%u] decided %u\n",
               (unsigned)self, (unsigned)acts[k].process,
               (unsigned)acts[k].baValue);
      break;

    case BKR94ACS_ACT_COMPLETE:
      if (verbose)
        printf("process %u: BKR94 ACS COMPLETE\n", (unsigned)self);
      break;

    case BKR94ACS_ACT_BA_EXHAUSTED:
      /*
       * Not verbose-gated: BA_EXHAUSTED is a fatal protocol-level
       * event (BKR94 Lemma 2 Part B's BA-termination assumption
       * was violated for this instance), not informational like
       * BA_DECIDED / COMPLETE.  An application's silence on this
       * action would teach the wrong pattern.
       */
      printf("process %u: BA[%u] EXHAUSTED -- ACS cannot complete "
             "(no decision in %u phases)\n",
             (unsigned)self, (unsigned)acts[k].process,
             (unsigned)MAX_PHASES);
      break;
    }
  }
}

/*--------------------------------------------------------------------------*/
/*  String comparison for qsort                                             */
/*--------------------------------------------------------------------------*/

static int
strPtrCmp(
  const void *a
 ,const void *b
){
  return (strcmp(*(const char *const *)a, *(const char *const *)b));
}

/*--------------------------------------------------------------------------*/
/*  Main simulation                                                         */
/*--------------------------------------------------------------------------*/

int
main(
  int argc
 ,char *argv[]
){
  /* Configuration */
  unsigned int n;
  unsigned int t;
  unsigned int verbose;
  unsigned int shuffleSeed;
  unsigned int origSeed;
  unsigned int vLen;
  int dproc;                /* -d: the delayed (WAN laggard) process, -1 none */
  unsigned int graceBudget; /* -g: tolerance budget in sweeps, 0 = eager */

  /* Per-process BKR94 ACS state */
  struct bkr94acs *processes[MAX_PROCESSES];
  unsigned long acsSize;
  struct bracha87Retry retry[MAX_PROCESSES];

  /* Sweep-side pacing state (the header's caller discipline: count
   * sweeps while a decision's duty holds TOLERANCE, fire when the
   * count exceeds the budget; reset whenever duty leaves TOLERANCE) */
  unsigned int turnTicks[MAX_PROCESSES][MAX_PROCESSES];
  unsigned int fanoutTicks[MAX_PROCESSES];
  unsigned int fanoutFires;
  unsigned int sweepCount;
  unsigned int released;

  /* A-Cast strings */
  char acasts[MAX_PROCESSES][MAX_VLEN];

  /* Loop / temp */
  unsigned int i;
  unsigned int j;
  int arg;
  int exitCode;

  /*----------------------------------------------------------------------*/
  /*  Parse command line                                                   */
  /*----------------------------------------------------------------------*/

  verbose = 0;
  shuffleSeed = 0;
  exitCode = 0;
  dproc = -1;
  graceBudget = 0;

  arg = 1;
  while (arg < argc && argv[arg][0] == '-') {
    if (argv[arg][1] == 'v' && argv[arg][2] == '\0') {
      verbose = 1;
      ++arg;
    } else if (argv[arg][1] == 's' && argv[arg][2] == '\0') {
      ++arg;
      if (arg >= argc) goto usage;
      shuffleSeed = (unsigned int)atoi(argv[arg]);
      ++arg;
    } else if (argv[arg][1] == 'd' && argv[arg][2] == '\0') {
      ++arg;
      if (arg >= argc) goto usage;
      dproc = atoi(argv[arg]);
      ++arg;
    } else if (argv[arg][1] == 'g' && argv[arg][2] == '\0') {
      ++arg;
      if (arg >= argc) goto usage;
      graceBudget = (unsigned int)atoi(argv[arg]);
      ++arg;
    } else {
      goto usage;
    }
  }

  if (argc - arg < 2) goto usage;
  n = (unsigned int)atoi(argv[arg++]);
  t = (unsigned int)atoi(argv[arg++]);

  if (n < 1 || n > MAX_PROCESSES) {
    fprintf(stderr, "n must be 1..%d\n", MAX_PROCESSES);
    return (1);
  }
  if (n <= 3 * t) {
    fprintf(stderr, "need n > 3t (n=%u, t=%u)\n", n, t);
    return (1);
  }
  if (dproc >= 0) {
    if ((unsigned int)dproc >= n) {
      fprintf(stderr, "-d process must be < n\n");
      return (1);
    }
    /*
     * At t = 0, n-t = n: step 2's count can never reach n while a BA
     * is unentered, so TOLERANCE -- the release trigger below -- is
     * unreachable and the delayed A-Cast would be held forever.
     */
    if (t < 1) {
      fprintf(stderr, "-d needs t >= 1\n");
      return (1);
    }
  }
  if ((unsigned int)(argc - arg) < n) {
    fprintf(stderr, "need %u A-Cast strings\n", n);
    return (1);
  }

  origSeed = shuffleSeed;

  /* Read acasts, find max length for vLen */
  vLen = 1;
  memset(acasts, 0, sizeof (acasts));
  for (i = 0; i < n; ++i) {
    unsigned int len;

    len = (unsigned int)strlen(argv[arg]) + 1; /* include \0 */
    if (len > MAX_VLEN) {
      fprintf(stderr, "A-Cast too long: %s (max %d chars + \\0)\n",
              argv[arg], MAX_VLEN - 1);
      return (1);
    }
    memcpy(acasts[i], argv[arg], len);
    if (len > vLen)
      vLen = len;
    ++arg;
  }

  /*----------------------------------------------------------------------*/
  /*  Allocate per-process BKR94 ACS state                                   */
  /*----------------------------------------------------------------------*/

  /* vLen encoding: actual length = vLen, encoding = vLen - 1 */
  acsSize = bkr94acsSz((unsigned int)(n - 1), (unsigned int)(vLen - 1), MAX_PHASES);

  memset(processes, 0, sizeof (processes));
  for (i = 0; i < n; ++i) {
    processes[i] = (struct bkr94acs *)calloc(1, acsSize);
    if (!processes[i]) {
      fprintf(stderr, "allocation failed\n");
      exitCode = 1;
      goto cleanup;
    }
    bkr94acsInit(processes[i], (unsigned char)(n - 1), (unsigned char)t,
                 (unsigned char)(vLen - 1), MAX_PHASES, (unsigned char)i,
                 demoCoin, 0);
    bracha87RetryInit(&retry[i]);
  }

  /*----------------------------------------------------------------------*/
  /*  Allocate message queue                                              */
  /*  BKR94 ACS generates more messages than plain BA:             */
  /*  N A-Cast broadcasts + N BA pipelines, each with rounds.    */
  /*----------------------------------------------------------------------*/

  if (qAlloc(64u * n * n * (unsigned int)MAX_PHASES * BRACHA87_ROUNDS_PER_PHASE + 1024)) {
    fprintf(stderr, "queue allocation failed\n");
    exitCode = 1;
    goto cleanup;
  }

  /*----------------------------------------------------------------------*/
  /*  Bootstrap: each process A-Casts its value                           */
  /*                                                                      */
  /*  bkr94acsAcast marks the local A-Cast Fig1 as the broadcast      */
  /*  initiator and outputs one BKR94ACS_ACT_ACAST_SEND action (.type =     */
  /*  BRACHA87_INITIAL) for the application to broadcast to all processes.    */
  /*  Retry thereafter is intrinsic to BPR (bkr94acsRetry) -- no          */
  /*  application bookkeeping required.                                        */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < n; ++i) {
    struct bkr94acsAct acastAct;
    unsigned int nActs;

    /* The -d process is the WAN laggard: only its OUTBOUND A-Cast is
     * delayed.  It runs, receives, retries, and enters like everyone
     * else; its INITIAL releases in the sweep loop below. */
    if (dproc >= 0 && i == (unsigned int)dproc)
      continue;
    nActs = bkr94acsAcast(processes[i],
                            (const unsigned char *)acasts[i],
                            &acastAct);
    if (nActs != 1)
      continue;
    for (j = 0; j < n; ++j)
      qPush(BKR94ACS_CLS_ACAST, acastAct.process, 0, 0,
            BRACHA87_INITIAL, acastAct.accepted,
            (unsigned char)i, (unsigned char)j,
            (const unsigned char *)acasts[i], vLen);
  }

  if (shuffleSeed)
    qShuffle(&shuffleSeed);

  /*----------------------------------------------------------------------*/
  /*  Drive to completion: drain ingress, tick the sweep, repeat -- the   */
  /*  bkr94acs.h application loop.  The sweep carries the BPR retry and   */
  /*  the two sweep-side protocol decisions, each paced by -g's           */
  /*  tolerance budget (0 = fire whenever enabled, the eager schedule).   */
  /*----------------------------------------------------------------------*/

  released = (dproc < 0) ? 1 : 0;
  sweepCount = 0;
  fanoutFires = 0;
  memset(turnTicks, 0, sizeof (turnTicks));
  memset(fanoutTicks, 0, sizeof (fanoutTicks));

  for (;;) {
    unsigned int progress;

    ++sweepCount;
    progress = 0;

    /*--------------------------------------------------------------------*/
    /*  Drain ingress.  Deliveries BANK evidence and emit the protocol's  */
    /*  own echo/ready traffic; no decision fires here.                   */
    /*--------------------------------------------------------------------*/

    while (Qhead < Qtail) {
      struct msg *m;
      struct bkr94acs *st;
      struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
      unsigned int nacts;
      unsigned int oldTail;
      unsigned char cls;
      unsigned char type;
      unsigned char baValue;

      m = &MsgQ[Qhead++];
      st = processes[m->to];

      /* Unpack the wire byte back into class and Bracha87 type. */
      cls = m->clsType & BKR94ACS_CLS_MASK;
      type = m->clsType & BRACHA87_TYPE_MASK;

      /*
       * Do NOT skip messages addressed to locally-complete processes.
       * A process that has decided all N BAs must keep processing
       * incoming messages so its Fig1 echoes/readys continue to
       * reach processes still working on some BAs.  Skipping replicates
       * the post-decide stall the library itself was fixed to avoid
       * (see bkr94acs.c bkr94acsBaInput comment on
       * a->complete).  The loop terminates on completion plus
       * quiescence below, not when any one process reaches complete.
       */
      oldTail = Qtail;

      /*
       * Sender (m->from) is the authenticated message sender; for INITIAL
       * messages the library checks it against the designated initiator
       * (process for acasts, initiator for BA) and drops a
       * forged non-initiator INITIAL, so this no-loss honest demo needs
       * no extra filter here.  A deployment with Byzantine processes relies on
       * that library check -- do NOT strip it by pre-validating away the
       * 'from' argument.
       */
      if (cls == BKR94ACS_CLS_ACAST) {
        if (verbose)
          printf("process %u: recv ACAST %s(process=%u) from %u\n",
                 (unsigned)m->to, typeName(type),
                 (unsigned)m->process, (unsigned)m->from);

        nacts = bkr94acsAcastInput(st, m->process, type, m->from,
                                      m->value, acts);
        /*
         * ACCEPTED-annotation ingress: bit 4 on a READY says its sender
         * has accepted this Fig1 and consumes no further (ready, v) from
         * us.  Route it AFTER the matching Input above, so rdFrom is
         * recorded first and acFrom stays a subset of rdFrom.
         */
        if (type == BRACHA87_READY && (m->clsType & BKR94ACS_ACCEPTED))
          bkr94acsAcastAccepted(st, m->process, m->from);
      } else {
        /* Recover the BA binary value (+D_FLAG) from the wire byte. */
        baValue = (unsigned char)
          (((m->clsType >> 3) & 1) | (m->clsType & BRACHA87_D_FLAG));
        if (verbose)
          printf("process %u: recv BA %s(process=%u, round=%u, val=%u) from %u\n",
                 (unsigned)m->to, typeName(type),
                 (unsigned)m->process, (unsigned)m->round,
                 (unsigned)baValue, (unsigned)m->from);

        nacts = bkr94acsBaInput(st, m->process, m->round,
                                       m->initiator, type,
                                       m->from, baValue, acts);
        /* Same ACCEPTED ingress as the A-Cast class (Input first). */
        if (type == BRACHA87_READY && (m->clsType & BKR94ACS_ACCEPTED))
          bkr94acsBaAccepted(st, m->process, m->round,
                                    m->initiator, m->from);
      }

      /* Enqueue output actions as network messages.  A duplicate
       * (BPR-retried) delivery returns 0 acts -- Input dedup, the
       * invariant that makes the progress gate below sound. */
      if (nacts)
        progress = 1;
      qActs(acts, nacts, m->to, n, vLen, verbose, "");

      if (shuffleSeed && Qtail > oldTail)
        qShuffle(&shuffleSeed);
    }

    /* The queue is drained (Qhead == Qtail): reset the indices so the
     * append-only store's capacity bounds one sweep's traffic, not the
     * whole run's -- without this a long retirement tail fills the
     * queue and qPush's silent drop would fake quiescence. */
    Qhead = Qtail = 0;

    /*--------------------------------------------------------------------*/
    /*  The delayed A-Cast releases at the first sweep its own process    */
    /*  observes step 2 leave HELD: the WAN knife edge.  Under a budget   */
    /*  that is TOLERANCE -- the grace clock is running and the next      */
    /*  drain delivers the INITIAL, so step 1 wins.  At -g 0 TOLERANCE    */
    /*  never survives to a sweep boundary (the tick fires the fanout     */
    /*  within the same pass that decides the n-t'th BA), so the first    */
    /*  observable state is MET: the door shut before the laggard's       */
    /*  INITIAL could leave, and the release demonstrates the arrival     */
    /*  that was one sweep too late.                                      */
    /*--------------------------------------------------------------------*/

    if (!released
     && bkr94acsFanoutDuty(processes[dproc]) != BKR94ACS_DUTY_HELD) {
      struct bkr94acsAct acastAct;

      released = 1;
      progress = 1;
      printf("process %d: delayed A-Cast \"%s\" releases (sweep %u)\n",
             dproc, acasts[dproc], sweepCount);
      if (bkr94acsAcast(processes[dproc],
                          (const unsigned char *)acasts[dproc],
                          &acastAct) == 1)
        for (j = 0; j < n; ++j)
          qPush(BKR94ACS_CLS_ACAST, acastAct.process, 0, 0,
                BRACHA87_INITIAL, acastAct.accepted,
                (unsigned char)dproc, (unsigned char)j,
                (const unsigned char *)acasts[dproc], vLen);
    }

    /*--------------------------------------------------------------------*/
    /*  Sweep tick: the BPR retry plus the sweep-side decisions.          */
    /*--------------------------------------------------------------------*/

    for (i = 0; i < n; ++i) {
      struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
      unsigned int nacts;
      unsigned int p;
      unsigned char duty;

      /* One retry call per process per sweep -- looping until idle
       * would flood the network; see bracha87.h's flood warning.
       * Receivers dedup at Fig1Input, so under perfect delivery a
       * retry's duplicates produce no new state; here it carries the
       * -d laggard's late INITIAL and re-carries anything a lossy
       * transport would have dropped. */
      nacts = bkr94acsRetry(processes[i], &retry[i], acts);
      qActs(acts, nacts, (unsigned char)i, n, vLen, verbose, " [retry]");

      /*
       * BA round turns, paced per (instance, BA): count sweeps while
       * bkr94acsTurnDuty holds TOLERANCE, pass the elapsed signal
       * once the count exceeds -g.  MET fires free -- the full
       * sample is in hand and waiting buys nothing.  The grace is
       * scoped to UNDECIDED BAs: once bkr94acsBaDecision reports a
       * decision, post-decide continuation rounds carry the pinned
       * value and their sample no longer chooses anything, so
       * holding them to the budget would only convoy the cohort
       * (each process's round-k INITIAL waits on its own turn of
       * k-1, and one process's stall holds everyone at n-t).  One
       * turn per BA per sweep: an undecided cascade's later rounds
       * each get their own grace.
       */
      for (p = 0; p < n; ++p) {
        duty = bkr94acsTurnDuty(processes[i], (unsigned char)p);
        if (duty == BKR94ACS_DUTY_TOLERANCE)
          ++turnTicks[i][p];
        else
          turnTicks[i][p] = 0;
        nacts = bkr94acsTurn(processes[i], (unsigned char)p,
                             turnTicks[i][p] > graceBudget
                             || bkr94acsBaDecision(processes[i],
                                  (unsigned char)p) != 0xFF, acts);
        if (nacts)
          progress = 1;
        qActs(acts, nacts, (unsigned char)i, n, vLen, verbose, " [turn]");
      }

      /*
       * BKR94 step 2, paced the same way -- after the turns, because
       * only a turn produces the decisions it counts.  While its
       * TOLERANCE holds, the retry above is still carrying the
       * delayed A-Cast: each one that completes enters 1 by step 1
       * and leaves the unentered set before the fanout reads it.
       */
      duty = bkr94acsFanoutDuty(processes[i]);
      if (duty == BKR94ACS_DUTY_TOLERANCE)
        ++fanoutTicks[i];
      else
        fanoutTicks[i] = 0;
      if (fanoutTicks[i] > graceBudget) {
        nacts = bkr94acsFanout(processes[i], acts);
        if (nacts) {
          fanoutFires += nacts;
          progress = 1;
          printf("process %u: step 2 fires (sweep %u) -- enter 0 in %u unentered BA(s)\n",
                 i, sweepCount, nacts);
        }
        qActs(acts, nacts, (unsigned char)i, n, vLen, verbose, " [step 2]");
      }
    }

    /*--------------------------------------------------------------------*/
    /*  Terminate by the documented abandonment pattern: every process    */
    /*  complete AND a barren sweep -- no delivery, turn, or fanout       */
    /*  produced a fresh act.  BPR keeps re-sending (the library          */
    /*  prescribes no stop; see README.md "Abandonment"), but Input       */
    /*  dedup returns 0 acts for a duplicate, so retries never register   */
    /*  as progress -- the invariant test_bkr94acs_blackbox C8 pins.      */
    /*  The cap is a harness guard, not protocol.                         */
    /*--------------------------------------------------------------------*/

    if (!progress) {
      unsigned int allDone;

      allDone = 1;
      for (i = 0; i < n; ++i)
        if (!processes[i]->complete) {
          allDone = 0;
          break;
        }
      if (allDone)
        break;
    }
    if (sweepCount > 100000) {
      fprintf(stderr, "no convergence within the sweep cap\n");
      exitCode = 1;
      break;
    }
  }

  /*----------------------------------------------------------------------*/
  /*  Output: each process's agreed common subset in sorted order            */
  /*----------------------------------------------------------------------*/

  printf("\n--- BKR94 ACS Results (n=%u, t=%u, seed=%u) ---\n", n, t, origSeed);

  {
    /* Verify all honest processes agree on the same subset and ordering */
    int allAgree;
    int haveBaseline;
    unsigned char firstSubset[MAX_PROCESSES];
    unsigned int firstCnt;

    allAgree = 1;
    haveBaseline = 0;
    firstCnt = 0;

    for (i = 0; i < n; ++i) {
      unsigned char subset[MAX_PROCESSES];
      unsigned int cnt;
      const char *sorted[MAX_PROCESSES];

      if (!processes[i]->complete) {
        /*
         * Distinguish exhaustion from other incompleteness causes:
         * the library exposes 0xFE in bkr94acsBaDecision for BAs
         * that hit BRACHA87_EXHAUSTED.  Listing them tells the
         * operator exactly which BA(s) failed; a generic "did not
         * complete" message would conflate this with a queue-drain
         * race or other deployment-level cause.
         */
        unsigned int exhaustedCnt;

        printf("Process %u: BKR94 ACS did not complete", i);
        exhaustedCnt = 0;
        for (j = 0; j < n; ++j) {
          if (bkr94acsBaDecision(processes[i], (unsigned char)j) == 0xFE) {
            printf("%s BA[%u]",
                   exhaustedCnt ? "," : " (exhausted:",
                   j);
            ++exhaustedCnt;
          }
        }
        if (exhaustedCnt)
          printf(")");
        printf("\n");
        exitCode = 1;
        continue;
      }

      cnt = bkr94acsSubset(processes[i], subset);
      printf("Process %u: common subset (%u/%u A-Casts):\n", i, cnt, n);

      /* Collect A-Cast strings for sorted output */
      for (j = 0; j < cnt; ++j) {
        const unsigned char *pv;

        pv = bkr94acsAcastValue(processes[i], subset[j]);
        sorted[j] = pv ? (const char *)pv : "(null)";
      }

      /* Sort lexicographically -- deterministic ordering */
      qsort(sorted, cnt, sizeof (sorted[0]), strPtrCmp);

      for (j = 0; j < cnt; ++j)
        printf("  %s\n", sorted[j]);

      /*
       * Track the baseline by first-completion, not process index.
       * If process 0 fails to complete (e.g. exhausted BA), comparing
       * subsequent processes' subsets against an unset firstCnt would
       * spuriously flag disagreement.
       */
      if (!haveBaseline) {
        firstCnt = cnt;
        memcpy(firstSubset, subset, cnt);
        haveBaseline = 1;
      } else {
        if (cnt != firstCnt
         || memcmp(subset, firstSubset, cnt))
          allAgree = 0;
      }
    }

    printf("\nAll processes agree on subset: %s\n",
           allAgree ? "ok" : "FAIL");
    if (!allAgree)
      exitCode = 1;
  }

  /*----------------------------------------------------------------------*/
  /*  The -d demonstration's verdict: included under grace, excluded      */
  /*  under the eager schedule -- and in the eager case the value still   */
  /*  arrived everywhere, pinning that exclusion is participation loss,   */
  /*  never value loss.                                                   */
  /*----------------------------------------------------------------------*/

  if (dproc >= 0) {
    unsigned char subset[MAX_PROCESSES];
    unsigned int cnt;
    unsigned int inSubset;
    unsigned int acceptedEverywhere;

    cnt = bkr94acsSubset(processes[0], subset);
    inSubset = 0;
    for (j = 0; j < cnt; ++j)
      if (subset[j] == (unsigned char)dproc)
        inSubset = 1;
    acceptedEverywhere = 1;
    for (i = 0; i < n; ++i)
      if (!bkr94acsAcastValue(processes[i], (unsigned char)dproc))
        acceptedEverywhere = 0;
    printf("\nDelayed process %d (grace budget %u, %u sweeps): %s\n",
           dproc, graceBudget, sweepCount,
           inSubset
             ? "INCLUDED -- the grace let step 1 win"
             : "EXCLUDED -- the eager schedule shut the door");
    printf("step 2 fired %u enter-0 act(s); the delayed value was %s"
           "accepted at every process%s\n",
           fanoutFires,
           acceptedEverywhere ? "" : "NOT ",
           inSubset ? "" : " -- participation loss, not value loss");
  }

  /*----------------------------------------------------------------------*/
  /*  Cleanup                                                             */
  /*----------------------------------------------------------------------*/

cleanup:
  for (i = 0; i < n; ++i)
    free(processes[i]);
  qFree();

  return (exitCode);

usage:
  fprintf(stderr,
    "usage: example_bkr94acs [-v] [-s seed] [-d process] [-g budget] n t acast0 acast1 ...\n"
    "  n            total processes (1-%d)\n"
    "  t            max Byzantine faults\n"
    "  acast*      per-process A-Cast strings\n"
    "  -v           verbose: trace every message\n",
    MAX_PROCESSES);
  fprintf(stderr,
    "  -s seed      shuffle seed (0 = ordered delivery)\n"
    "  -d process   hold that process's A-Cast until step 2 first\n"
    "               enables (the WAN laggard; needs t >= 1)\n"
    "  -g budget    tolerance budget in sweeps for the sweep-side\n"
    "               decisions (0 = eager; with -d, a sufficient\n"
    "               budget includes the laggard in SubSet)\n");
  return (1);
}
