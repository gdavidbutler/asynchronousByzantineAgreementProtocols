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
 * does NOT exercise the deployment-time abandonment policy needed
 * under real asynchronous transport; that is inherently coupled to
 * message loss, partial ordering, and the failure modes those
 * introduce. See README.md "Abandonment" for the design.
 *
 * How a run ends -- the taxonomy, and which half this demo shows:
 *
 *   QUIESCENT   the owing ending (BPR.md Quiescence -- no exit
 *               decision at all), demonstrated here, per process on
 *               its OWN evidence: a bkr94acsRetryStep pass past its
 *               A-Cast owes nothing, so it leaves the retry
 *               rotation.  Completion is then an assertion the
 *               results section checks, not the gate.  Reaching it
 *               needs both READY annotations round-tripped below --
 *               ACCEPTED for "I have accepted", RECEIVED for "I have
 *               yours" -- since suppressing on the first alone
 *               silences the announcement the other end waits for.
 *   ABANDONED   the policy backstop (README.md "Abandonment"), which
 *               reads the ABSENCE of progress and therefore cannot
 *               tell a laggard from a dead process.  Under loss it
 *               is the only exit a process whose owed evidence
 *               never arrives has.
 *               This demo loses nothing, so it never takes it.
 *   tick cap    a harness guard, not protocol: a bound on the
 *               rotation, carrying no evidence of anything.
 *
 * Build:
 *   (from project root) make example_bkr94acs
 *
 * Usage:
 *   ./example_bkr94acs [-v] [-s seed] [-d process] [-g patience]
 *                      [-b mode] n t acast0 ...
 *
 * Example:
 *   ./example_bkr94acs 4 1 joe sam sally tim
 *
 * The Byzantine mode (-b) designates PROCESS 0 as the one faulty
 * process -- the same slot example/bracha87Fig1.c's -b uses, and the
 * one the equivocation splits are written around -- and gives it one
 * of three behaviors.  It composes with -s and -v and REFUSES to
 * compose with -d / -g: those demonstrate sweep-counted patience, and
 * a patience demonstration collapses when the run cannot end by
 * quiescence.  Under -b the tick cap IS the expected ending, and the
 * exit code reports whether the honest assertions held:
 *   ./example_bkr94acs -b silent 4 1 joe sam sally tim
 *     process 0 runs no state machine and sends nothing.  The honest
 *     processes complete with its A-Cast excluded and agree; every
 *     gate that needs all n stands one bit short forever.
 *   ./example_bkr94acs -b equiv2 4 1 joe sam sally tim
 *     process 0 submits INITIAL with its value to processes [0..2)
 *     and with the first byte's case bit flipped to [2..n), then runs
 *     no state machine.  The honest camps are 1 and 2, both below the echo
 *     threshold of 3: nobody accepts, and Lemma 2's conditional is
 *     vacuous.
 *   ./example_bkr94acs -b equiv1 4 1 joe sam sally tim
 *     the same behavior split 1/3, so all three honest processes echo
 *     the same value, cross the threshold, and accept it -- the
 *     equivocator's A-Cast is INCLUDED.
 *   ./example_bkr94acs -b poke 4 1 joe sam sally tim
 *     process 0 participates honestly through COMPLETE, then forever
 *     re-sends READYs stripped of the RECEIVED annotation.  Each poke
 *     re-arms one marked re-send aimed only at the poker, and is a
 *     duplicate READY that Input returns 0 acts for: it defeats the
 *     quiescence ending without touching the progress evidence a
 *     barren-sweep policy reads.
 *
 * The sweep-side pacing pair (-d names the WAN laggard, -g the patience,
 * in COMPLETED BPR SWEEPS -- full passes of the Retry cursor, the
 * unit bkr94acs.h ratifies):
 *   ./example_bkr94acs -d 3 4 1 joe sam sally tim
 *     the eager schedule (patience 0) excludes the delayed honest
 *     process: SubSet = 3 of 4, its value accepted everywhere but
 *     excluded -- participation loss, not value loss
 *   ./example_bkr94acs -d 3 -g 1 4 1 joe sam sally tim
 *     the identical schedule under ONE sweep of patience includes it:
 *     SubSet = 4 of 4, step 2 never fires.  One sweep is what
 *     patience is worth by construction: a pass re-sends every sent
 *     instance once, so the released INITIAL reaches everyone
 *     within it.
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

/*
 * The -b behaviors.  BYZ_PROCESS is the designated faulty slot: process
 * 0, matching example/bracha87Fig1.c's default initiator, so the
 * equivocation splits below read as the paper's do -- [0..split) gets
 * one value, [split..n) the other, and the honest camps are what is
 * left after the equivocator's own slot is taken out.
 */
#define BYZ_NONE    0
#define BYZ_SILENT  1
#define BYZ_EQUIV   2
#define BYZ_POKE    3
#define BYZ_PROCESS 0

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
/*  clsType and leaves value[] unused.  A READY also carries the two        */
/*  class-independent BPR annotations: BKR94ACS_ACCEPTED (bit 4) from the   */
/*  act's .accepted flag, one fact about the sender; and                    */
/*  BKR94ACS_RECEIVED (bit 5) from the act's .received mask, decided per    */
/*  RECIPIENT.  Together they are the READY retire: the first says "I       */
/*  have accepted," the second "I have yours," and quiescence needs both    */
/*  in every direction.  On ingress neither needs a call of its own --      */
/*  the whole clsType byte goes to bkr94acs*Input as annot, which routes    */
/*  both, in order, off the one message that carried them.                  */
/*--------------------------------------------------------------------------*/

struct msg {
  unsigned char clsType;     /* class|type; +ACCEPTED,+RECEIVED on a READY;
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
 ,unsigned char received /* READY only: act's .received bit 'to' -> wire bit 5 */
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
       | (received ? BKR94ACS_RECEIVED : 0)
       | ((value[0] & 1) << 3)
       | (value[0] & BRACHA87_D_FLAG));
  else {
    /* A-Cast payload is real bytes; pack class, type, ACCEPTED. */
    MsgQ[Qtail].clsType = (unsigned char)
      (cls | type
       | (accepted ? BKR94ACS_ACCEPTED : 0)
       | (received ? BKR94ACS_RECEIVED : 0));
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
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
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
/*  Three sources hand the loop acts of the same kinds: a delivery          */
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
         * round-trips (egress .accepted -> bit 4 -> the annot argument
         * of bkr94acs*Input); the same round-trip is exercised under
         * loss in test_bkr94acs_blackbox.c. */
        if (acts[k].skip && BRACHA87_SKIP_TST(acts[k].skip, p))
          continue;
        qPush(BKR94ACS_CLS_ACAST, acts[k].process, 0, 0,
              acts[k].type, acts[k].accepted,
              acts[k].received && BRACHA87_SKIP_TST(acts[k].received, p),
              self, (unsigned char)p,
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
              acts[k].received && BRACHA87_SKIP_TST(acts[k].received, p),
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
  unsigned int patience;    /* -g: patience in sweeps, 0 = eager */
  unsigned int patienceGiven; /* -g appeared on the command line */
  unsigned int byzMode;     /* -b: BYZ_NONE / SILENT / EQUIV / POKE */
  unsigned int byzSplit;    /* -b equiv<S>: recipients [0..S) get the value */

  /* Per-process BKR94 ACS state */
  struct bkr94acs *processes[MAX_PROCESSES];
  unsigned long acsSize;
  struct bracha87Retry retry[MAX_PROCESSES];

  /* Sweep-side pacing state (the header's caller discipline: count
   * COMPLETED SWEEPS while a decision's duty holds TOLERANCE, fire
   * when the count reaches the budget, evaluating the verdict on
   * every tick so a zero budget stays eager; reset whenever duty
   * leaves TOLERANCE.  THE UNIT IS THE FULL SWEEP -- one complete
   * pass of the Retry cursor over every sent Fig 1 instance, read
   * off the cursor's own `sweeps` wrap count, per bkr94acs.h.  One
   * loop pass here is a TICK (one Retry call per process, the
   * header's application-loop template), so a sweep boundary for a
   * process is that count changing, or a quiescent tick (the pass
   * it would have made owes nothing). */
  unsigned int turnSweeps[MAX_PROCESSES][MAX_PROCESSES];
  unsigned int fanoutSweeps[MAX_PROCESSES];
  unsigned int lastSweeps[MAX_PROCESSES];
  unsigned int fanoutFires;
  unsigned int tickCount;
  unsigned int released;

  /* Per-process ending: quiescence is read per process, from its own
   * Retry sweep, so the run reports which processes left and when. */
  unsigned char quiescent[MAX_PROCESSES];
  unsigned int quiesceTick[MAX_PROCESSES];
  unsigned int quiesced;

  /* Byzantine-mode observation.  The POKE assertions are read off the
   * live run: a poke's Input return (the barren evidence stream), and
   * the two READY masks at the one stable point where they diverge --
   * right after a poke's delivery has re-armed its sender and before
   * the marked re-send the next egress aims back consumes the arm. */
  unsigned int pokeArmed;
  unsigned int pokeSent;
  unsigned int pokeActs;
  unsigned int pokeSampled;
  unsigned int pokeInstances;
  unsigned int pokeAimed;

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
  patience = 0;
  patienceGiven = 0;
  byzMode = BYZ_NONE;
  byzSplit = 0;

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
      patience = (unsigned int)atoi(argv[arg]);
      patienceGiven = 1;
      ++arg;
    } else if (argv[arg][1] == 'b' && argv[arg][2] == '\0') {
      ++arg;
      if (arg >= argc) goto usage;
      if (!strcmp(argv[arg], "silent"))
        byzMode = BYZ_SILENT;
      else if (!strcmp(argv[arg], "poke"))
        byzMode = BYZ_POKE;
      else if (!strncmp(argv[arg], "equiv", 5)) {
        byzMode = BYZ_EQUIV;
        byzSplit = (unsigned int)atoi(argv[arg] + 5);
      } else
        goto usage;
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
  if (byzMode != BYZ_NONE) {
    /*
     * The -d / -g pair demonstrates sweep-counted patience, and
     * patience is demonstrated by the ENDING it changes.  With a Byzantine
     * process the run cannot end by quiescence at all, so the pair
     * would be measuring nothing.  Refuse rather than mislead.
     */
    if (dproc >= 0 || patienceGiven) {
      fprintf(stderr, "-b does not compose with -d / -g\n");
      return (1);
    }
    if (t < 1) {
      fprintf(stderr, "-b needs t >= 1 (one faulty process to tolerate)\n");
      return (1);
    }
    if (byzMode == BYZ_EQUIV && byzSplit > n) {
      fprintf(stderr, "-b equiv split must be <= n\n");
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
  /*  bkr94acsAcast marks the local A-Cast Fig1 as the broadcast          */
  /*  initiator and outputs one BKR94ACS_ACT_ACAST_SEND action (.type =   */
  /*  BRACHA87_INITIAL) for the application to broadcast to all           */
  /*  processes.  Retry thereafter is intrinsic to BPR                    */
  /*  (bkr94acsRetryStep) -- no application bookkeeping required.         */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < n; ++i) {
    struct bkr94acsAct acastAct;
    unsigned int nActs;

    /* The -d process is the WAN laggard: only its OUTBOUND A-Cast is
     * delayed.  It runs, receives, retries, and enters like everyone
     * else; its INITIAL releases in the tick loop below. */
    if (dproc >= 0 && i == (unsigned int)dproc)
      continue;

    /*
     * The Byzantine process's own submission.  SILENT sends nothing at
     * all.  EQUIV sends (initial, v) to [0..split) and (initial, v')
     * to [split..n), v' being v with its first byte inverted, and then
     * runs no state machine: an equivocator that went on to echo would
     * change the n=4 outcome, so this one stops at the split.  POKE
     * A-Casts honestly and is handled by the ordinary path below.
     */
    if (byzMode != BYZ_NONE && byzMode != BYZ_POKE
     && i == (unsigned int)BYZ_PROCESS) {
      if (byzMode == BYZ_EQUIV) {
        unsigned char split[MAX_VLEN];

        memcpy(split, acasts[i], vLen);
        /* A different value, and one that stays printable: this demo
         * renders its A-Casts as text, so flipping the case bit shows
         * the equivocation in the output instead of hiding it behind a
         * byte the terminal cannot draw. */
        split[0] = (unsigned char)(split[0] ^ 0x20);
        printf("process %u: Byzantine initiator equivocates -- \"%s\" to"
               " processes [0..%u), \"%s\" to [%u..%u)\n",
               i, acasts[i], byzSplit, split, byzSplit, n);
        for (j = 0; j < n; ++j)
          qPush(BKR94ACS_CLS_ACAST, (unsigned char)i, 0, 0,
                BRACHA87_INITIAL, 0, 0,
                (unsigned char)i, (unsigned char)j,
                (j < byzSplit)
                  ? (const unsigned char *)acasts[i]
                  : (const unsigned char *)split,
                vLen);
      } else
        printf("process %u: Byzantine SILENT -- no state machine, no"
               " messages\n", i);
      continue;
    }
    nActs = bkr94acsAcast(processes[i],
                            (const unsigned char *)acasts[i],
                            &acastAct);
    if (nActs != 1)
      continue;
    for (j = 0; j < n; ++j)
      qPush(BKR94ACS_CLS_ACAST, acastAct.process, 0, 0,
            BRACHA87_INITIAL, acastAct.accepted, 0,
            (unsigned char)i, (unsigned char)j,
            (const unsigned char *)acasts[i], vLen);
  }

  if (shuffleSeed)
    qShuffle(&shuffleSeed);

  /*----------------------------------------------------------------------*/
  /*  Drive to completion: drain ingress, tick, repeat -- the            */
  /*  bkr94acs.h application loop.  A tick is one Retry call per          */
  /*  process plus the two sweep-side protocol decisions, each paced      */
  /*  by -g's patience in COMPLETED SWEEPS (0 = fire whenever             */
  /*  enabled, the eager schedule).                                       */
  /*----------------------------------------------------------------------*/

  released = (dproc < 0) ? 1 : 0;
  tickCount = 0;
  fanoutFires = 0;
  quiesced = 0;
  memset(quiescent, 0, sizeof (quiescent));
  memset(quiesceTick, 0, sizeof (quiesceTick));
  memset(turnSweeps, 0, sizeof (turnSweeps));
  memset(fanoutSweeps, 0, sizeof (fanoutSweeps));
  memset(lastSweeps, 0, sizeof (lastSweeps));
  pokeArmed = 0;
  pokeSent = 0;
  pokeActs = 0;
  pokeSampled = 0;
  pokeInstances = 0;
  pokeAimed = 0;

  for (;;) {
    ++tickCount;

    /*--------------------------------------------------------------------*/
    /*  Drain ingress.  Deliveries BANK evidence and output the protocol's  */
    /*  own echo/ready traffic; no decision fires here.                   */
    /*--------------------------------------------------------------------*/

    while (Qhead < Qtail) {
      struct msg *m;
      struct bkr94acs *st;
      struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES)];
      unsigned int nacts;
      unsigned int oldTail;
      unsigned char cls;
      unsigned char type;
      unsigned char baValue;

      m = &MsgQ[Qhead++];
      /* A process that runs no state machine has nothing to deliver
       * to; the wire is simply lost at its end. */
      if (byzMode != BYZ_NONE && byzMode != BYZ_POKE
       && m->to == (unsigned char)BYZ_PROCESS)
        continue;
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

        /*
         * The whole clsType byte rides in as annot.  The library reads
         * BKR94ACS_ACCEPTED and BKR94ACS_RECEIVED off it, and only on a
         * READY: ACCEPTED says the sender has accepted this Fig1 and
         * consumes no further (ready, v) from us; the ABSENCE of
         * RECEIVED says the sender has not recorded OUR accept -- it
         * would have suppressed us otherwise -- so it is un-suppressed
         * for the next READY egress, which goes out marked.  Ordering
         * the two against the message's own Fig1 input is the library's.
         */
        nacts = bkr94acsAcastInput(st, m->process, type, m->clsType,
                                      m->from, m->value, acts);
        /*
         * The one part that stays here, because it is this program's
         * parking policy and not the protocol's.
         */
        if (type == BRACHA87_READY && !(m->clsType & BKR94ACS_RECEIVED)) {
          /*
           * Leaving the rotation is PROVISIONAL: an unmarked READY is
           * evidence that something IS still owed, and a process that
           * stopped ticking can never re-send marked.  Re-enter --
           * under loss this is what makes quiescence reachable, since
           * a lost marked re-send is drawn again by the next unmarked
           * one, and the unmarked one is worthless if nobody is left to hear it.
           */
          if (quiescent[m->to]) {
            quiescent[m->to] = 0;
            --quiesced;
          }
        }
      } else {
        /* Recover the BA binary value (+D_FLAG) from the wire byte. */
        baValue = (unsigned char)
          (((m->clsType >> 3) & 1) | (m->clsType & BRACHA87_D_FLAG));
        if (verbose)
          printf("process %u: recv BA %s(process=%u, round=%u, val=%u) from %u\n",
                 (unsigned)m->to, typeName(type),
                 (unsigned)m->process, (unsigned)m->round,
                 (unsigned)baValue, (unsigned)m->from);

        /* Same annotation ingress as the A-Cast class: the byte goes
         * in as annot and the library routes both bits. */
        nacts = bkr94acsBaInput(st, m->process, m->round,
                                       m->initiator, type, m->clsType,
                                       m->from, baValue, acts);
        if (type == BRACHA87_READY && !(m->clsType & BKR94ACS_RECEIVED)) {
          if (quiescent[m->to]) {
            quiescent[m->to] = 0;
            --quiesced;
          }
        }
      }

      /* Enqueue output actions as network messages.  A duplicate
       * (BPR-retried) delivery returns 0 acts -- Input dedup, the
       * invariant a barren-sweep policy's progress reading relies
       * on (BPR.md Termination and Abandonment); this demo ends by
       * quiescence and maintains no progress counter. */
      /* A poke is a duplicate READY.  Its Input return is what the
       * barren-sweep policy would read, and it is the whole reason a
       * poke costs the evidence stream nothing. */
      if (pokeArmed && m->from == (unsigned char)BYZ_PROCESS
       && type == BRACHA87_READY)
        pokeActs += nacts;
      qActs(acts, nacts, m->to, n, vLen, verbose, "");

      if (shuffleSeed && Qtail > oldTail)
        qShuffle(&shuffleSeed);
    }

    /* The queue is drained (Qhead == Qtail): reset the indices so the
     * append-only store's capacity bounds one tick's traffic, not the
     * whole run's -- without this a long retirement tail fills the
     * queue and qPush's silent drop would fake quiescence. */
    Qhead = Qtail = 0;

    /*--------------------------------------------------------------------*/
    /*  THE AIMED RE-SEND READING, taken once and only here.  A poke has  */
    /*  just been delivered and its arm is placed; the egress whose       */
    /*  marked re-send consumes it has not run yet.  This is the single   */
    /*  configuration where the two READY masks diverge: the poker        */
    /*  announced its own accept, so its RECEIVED bit is set, while the   */
    /*  arm has taken it back out of SKIP -- and every other process is   */
    /*  still suppressed.  The marked re-send therefore reaches the       */
    /*  poker and nobody else.                                            */
    /*                                                                    */
    /*  The stable point is the poker's OWN quiescence: its masks fill    */
    /*  only once every process has announced its accept and re-sent      */
    /*  marked to the poker, so the honest exchange has settled by then   */
    /*  and the only arm left standing anywhere is the one this tick's    */
    /*  poke just placed.                                                 */
    /*--------------------------------------------------------------------*/

    if (byzMode == BYZ_POKE && pokeArmed && quiescent[BYZ_PROCESS]
     && !pokeSampled) {
      pokeSampled = 1;
      for (i = 0; i < n; ++i) {
        if (i == (unsigned int)BYZ_PROCESS)
          continue;
        for (j = 0; j < n; ++j) {
          const struct bracha87Fig1 *f1;
          const unsigned char *sk;
          const unsigned char *rcv;
          unsigned int q;

          if (!(f1 = bkr94acsAcastFig1(processes[i], (unsigned char)j))
           || !bkr94acsAcastValue(processes[i], (unsigned char)j))
            continue;
          sk = bracha87Fig1Skip(f1, BRACHA87_READY_ALL);
          rcv = bracha87Fig1Received(f1);
          if (!sk || !rcv)
            continue;
          ++pokeInstances;
          if (BRACHA87_SKIP_TST(sk, BYZ_PROCESS)
           || !BRACHA87_SKIP_TST(rcv, BYZ_PROCESS))
            continue;
          for (q = 0; q < n; ++q)
            if (q != (unsigned int)BYZ_PROCESS && !BRACHA87_SKIP_TST(sk, q))
              break;
          if (q == n)
            ++pokeAimed;
        }
      }
    }

    /*--------------------------------------------------------------------*/
    /*  The delayed A-Cast releases at the first tick its own process     */
    /*  observes step 2 leave HELD: the WAN knife edge.  Under patience   */
    /*  at TOLERANCE -- the patience clock is counting completed          */
    /*  sweeps and the next drain delivers the INITIAL, so step 1 wins.   */
    /*  At -g 0 the verdict elapses within the same tick that decides     */
    /*  the n-t'th BA (it is evaluated on every tick), so the first       */
    /*  observable state is MET: the door shut before the laggard's       */
    /*  INITIAL could leave, and the release demonstrates the arrival     */
    /*  that was one tick too late.                                       */
    /*--------------------------------------------------------------------*/

    if (!released
     && bkr94acsFanoutDuty(processes[dproc]) != BKR94ACS_DUTY_HELD) {
      struct bkr94acsAct acastAct;

      released = 1;
      printf("process %d: delayed A-Cast \"%s\" releases (tick %u)\n",
             dproc, acasts[dproc], tickCount);
      if (bkr94acsAcast(processes[dproc],
                          (const unsigned char *)acasts[dproc],
                          &acastAct) == 1)
        for (j = 0; j < n; ++j)
          qPush(BKR94ACS_CLS_ACAST, acastAct.process, 0, 0,
                BRACHA87_INITIAL, acastAct.accepted, 0,
                (unsigned char)dproc, (unsigned char)j,
                (const unsigned char *)acasts[dproc], vLen);
    }

    /*--------------------------------------------------------------------*/
    /*  The tick: one Retry call per process plus the sweep-side          */
    /*  decisions, their tolerance clocks counting completed sweeps.      */
    /*--------------------------------------------------------------------*/

    for (i = 0; i < n; ++i) {
      struct bkr94acsAct acts[BKR94ACS_MAX_ACTS(MAX_PROCESSES)];
      unsigned int nacts;
      unsigned int p;
      unsigned int sweepDone;
      unsigned char duty;

      /* A SILENT or equivocating process runs no state machine: it
       * takes no tick, so nothing here is called on its behalf. */
      if (byzMode != BYZ_NONE && byzMode != BYZ_POKE
       && i == (unsigned int)BYZ_PROCESS)
        continue;

      /* One retry call per process per TICK -- looping until idle
       * would flood the network; see bracha87.h's flood warning.  A
       * full BPR SWEEP -- one complete pass of the cursor over every
       * sent Fig 1 instance -- spans many ticks, and its boundary
       * here is what the tolerance clocks below count: the cursor's
       * `sweeps` wrap count changing, or a quiescent tick (the pass
       * it would have made owes nothing).
       * Receivers dedup at Fig1Input, so under perfect delivery a
       * retry's duplicates produce no new state; here it carries the
       * -d laggard's late INITIAL and re-carries anything a lossy
       * transport would have dropped.
       *
       * A 0 return past this process's own A-Cast is QUIESCENCE on its
       * OWN evidence: a full pass of its cursor found every sent Fig 1
       * retired, which for READY means every process has announced its
       * accept and holds this one's.  It owes nothing to anyone, so it
       * leaves the rotation.  Its ingress stays open -- quiescence
       * bounds what a process OWES, never what it may still be told --
       * and a turn or fanout below re-arms it, since either can seed a
       * fresh Fig 1 for a round nobody has broadcast yet. */
      sweepDone = 0;
      if (!quiescent[i]) {
        nacts = bkr94acsRetryStep(processes[i], &retry[i], acts);
        /* The cursor's own wrap count IS the pass boundary; compare
         * it, never assume it advanced by one (a single call can
         * complete two passes).  Counting calls against
         * bkr94acsFig1SentCount would close late, by the retired
         * count, and that error grows as a run matures. */
        if (retry[i].sweeps != lastSweeps[i]) {
          lastSweeps[i] = retry[i].sweeps;
          sweepDone = 1;
        }
        if (!nacts && bkr94acsFig1SentCount(processes[i])) {
          quiescent[i] = 1;
          quiesceTick[i] = tickCount;
          ++quiesced;
          if (verbose)
            printf("process %u: QUIESCENT (tick %u)\n", i, tickCount);
        }
        qActs(acts, nacts, (unsigned char)i, n, vLen, verbose, " [retry]");
      } else
        /* A quiescent process's pass owes nothing: an idle sweep
         * completes every tick, so its tolerance clocks keep
         * running. */
        sweepDone = 1;

      /*
       * BA round turns, paced per (ACS state, BA): count COMPLETED
       * SWEEPS while bkr94acsTurnDuty holds TOLERANCE, pass the
       * elapsed signal once the count reaches -g -- evaluated on
       * every tick, so zero patience recovers the eager schedule
       * exactly (bkr94acs.h: a clock that only advances at a sweep
       * boundary fires one boundary late even at zero).  MET fires
       * free -- the full sample is in hand and waiting buys
       * nothing.  Patience is
       * scoped to UNDECIDED BAs: once bkr94acsBaDecision reports a
       * decision, post-decide continuation rounds carry the pinned
       * value and their sample no longer chooses anything, so
       * holding them to the patience would only convoy the cohort
       * (each process's round-k INITIAL waits on its own turn of
       * k-1, and one process's stall holds everyone at n-t).  One
       * turn per BA per tick, and the clock re-arms only when duty
       * leaves TOLERANCE: a cascade that holds it continuously
       * spends ONE patience across all of its rounds, one round per
       * tick past the elapse; a later round pays its own patience
       * only after a HELD dip (an evidence gap) resets the count.
       */
      for (p = 0; p < n; ++p) {
        duty = bkr94acsTurnDuty(processes[i], (unsigned char)p);
        if (duty == BKR94ACS_DUTY_TOLERANCE) {
          if (sweepDone)
            ++turnSweeps[i][p];
        } else
          turnSweeps[i][p] = 0;
        nacts = bkr94acsTurn(processes[i], (unsigned char)p,
                             turnSweeps[i][p] >= patience
                             || bkr94acsBaDecision(processes[i],
                                  (unsigned char)p) != 0xFF, acts);
        if (nacts) {
          if (quiescent[i]) {
            quiescent[i] = 0;
            --quiesced;
          }
        }
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
      if (duty == BKR94ACS_DUTY_TOLERANCE) {
        if (sweepDone)
          ++fanoutSweeps[i];
      } else
        fanoutSweeps[i] = 0;
      if (fanoutSweeps[i] >= patience) {
        nacts = bkr94acsFanout(processes[i], 1, acts);
        if (nacts) {
          fanoutFires += nacts;
          if (quiescent[i]) {
            quiescent[i] = 0;
            --quiesced;
          }
          printf("process %u: step 2 fires (tick %u) -- enter 0 in %u unentered BA(s)\n",
                 i, tickCount, nacts);
        }
        qActs(acts, nacts, (unsigned char)i, n, vLen, verbose, " [step 2]");
      }
    }

    /*--------------------------------------------------------------------*/
    /*  Terminate by QUIESCENCE: every process has left the rotation on   */
    /*  its own evidence -- a full Retry pass owing nothing.  That is a   */
    /*  stronger ending than the barren-sweep abandon gate this loop      */
    /*  used to take, and it is available only because both READY         */
    /*  annotations round-trip here: the barren gate reads the ABSENCE    */
    /*  of progress, which a laggard's silence imitates exactly, while    */
    /*  quiescence reads evidence every process supplied.  Completion is  */
    /*  then an assertion rather than the gate -- an un-complete process  */
    /*  is reported below and fails the run.  Abandonment remains the     */
    /*  policy any deployment still needs (README.md "Abandonment"),      */
    /*  since loss can hold the rotation open; the cap stands in for it   */
    /*  here and is a harness guard, not protocol.                        */
    /*--------------------------------------------------------------------*/

    /*--------------------------------------------------------------------*/
    /*  THE POKE.  Once the poker has completed it re-sends, every tick,  */
    /*  a READY for every A-Cast instance it accepted, stripped of the    */
    /*  RECEIVED annotation -- the framer's own bit, cleared.  Each one   */
    /*  is a duplicate the receiver's Input dedups to 0 acts, and each    */
    /*  one re-arms a marked re-send that costs one masked READY per      */
    /*  poked instance per sweep, aimed back at the poker alone.  The     */
    /*  price is bounded by the poked instance count; what it buys is     */
    /*  that no gate needing all n ever closes.                           */
    /*--------------------------------------------------------------------*/

    if (byzMode == BYZ_POKE && processes[BYZ_PROCESS]->complete) {
      pokeArmed = 1;
      for (j = 0; j < n; ++j) {
        const unsigned char *pv;
        unsigned int q;

        if (!(pv = bkr94acsAcastValue(processes[BYZ_PROCESS],
                                      (unsigned char)j)))
          continue;
        for (q = 0; q < n; ++q) {
          if (q == (unsigned int)BYZ_PROCESS)
            continue;
          qPush(BKR94ACS_CLS_ACAST, (unsigned char)j, 0, 0,
                BRACHA87_READY, 1 /* the poker did accept */,
                0 /* RECEIVED stripped -- unmarked, so the receiver re-arms */,
                (unsigned char)BYZ_PROCESS, (unsigned char)q, pv, vLen);
          ++pokeSent;
        }
      }
    }

    if (quiesced == n)
      break;
    if (tickCount > 100000) {
      /*
       * The cap is a harness guard on the rotation, in TICKS -- not the
       * abandonment policy's unit and carrying no evidence of anything.
       * Without -b a run that reaches it failed to converge and says
       * so.  With -b it is the EXPECTED ending: a Byzantine process
       * holds every all-n gate open, so quiescence is unreachable by
       * construction and the exit code reports the honest assertions
       * instead.
       */
      if (!byzMode) {
        fprintf(stderr, "no convergence within the tick cap\n");
        exitCode = 1;
      }
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

      /* The designated Byzantine process is not part of the honest
       * outcome: agreement and completion are claims about the
       * processes the protocol is required to carry. */
      if (byzMode != BYZ_NONE && i == (unsigned int)BYZ_PROCESS)
        continue;

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
  /*  How the run ended, per process.  QUIESCENT is the owing ending:      */
  /*  that process's Retry pass owed nothing.  Reaching the cap instead    */
  /*  is the harness standing in for an abandonment policy, and a failure  */
  /*  here since this demo loses nothing.                                  */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < n; ++i)
    if (quiescent[i])
      printf("Process %u: QUIESCENT at tick %u\n", i, quiesceTick[i]);
    else
      printf("Process %u: not quiescent (tick cap)\n", i);
  printf("Ending: %u of %u processes QUIESCENT in %u ticks\n",
         quiesced, n, tickCount);

  /*----------------------------------------------------------------------*/
  /*  The Byzantine mode's own verdict.  Each behavior states what the     */
  /*  honest processes are owed under it, and the checks below are what    */
  /*  the exit code reports.  The tick cap is the expected ending here --  */
  /*  every gate that needs all n stands one bit short for good -- so a    */
  /*  run that reached it is not a failure and does not say it is.         */
  /*----------------------------------------------------------------------*/

  if (byzMode != BYZ_NONE) {
    unsigned char subset[MAX_PROCESSES];
    unsigned int cnt;
    unsigned int byzInSubset;
    unsigned int accepted;
    unsigned int agreed;
    const unsigned char *ref;

    printf("\n--- Byzantine mode (process %d) ---\n", BYZ_PROCESS);

    cnt = 0;
    byzInSubset = 0;
    if (processes[1]->complete) {
      cnt = bkr94acsSubset(processes[1], subset);
      for (j = 0; j < cnt; ++j)
        if (subset[j] == (unsigned char)BYZ_PROCESS)
          byzInSubset = 1;
    }

    /* Bracha Lemma 2, composed: whichever value any honest process
     * accepts for the faulty A-Cast, every honest process that accepts
     * agrees with it -- or nobody accepts and the conditional is
     * vacuous.  A disjunction, checked as one. */
    accepted = 0;
    agreed = 1;
    ref = 0;
    for (i = 1; i < n; ++i) {
      const unsigned char *pv;

      if (!(pv = bkr94acsAcastValue(processes[i], (unsigned char)BYZ_PROCESS)))
        continue;
      ++accepted;
      if (!ref)
        ref = pv;
      else if (memcmp(ref, pv, vLen))
        agreed = 0;
    }
    printf("Lemma 2 disjunction (all acceptors agree XOR nobody accepts): "
           "%s -- %u of %u honest processes accepted\n",
           agreed ? "ok" : "FAIL", accepted, n - 1);
    if (!agreed)
      exitCode = 1;
    printf("Faulty A-Cast in the agreed subset: %s (%u of %u members)\n",
           byzInSubset ? "INCLUDED" : "excluded", cnt, n);

    if (byzMode == BYZ_POKE) {
      printf("Poker: %u pokes sent, %u acts returned by them; "
             "barren evidence stream %s\n",
             pokeSent, pokeActs,
             pokeActs ? "DISTURBED -- FAIL" : "unaffected");
      if (pokeActs)
        exitCode = 1;
      printf("Aimed re-sends: %u of %u poked instances suppress every "
             "process but the poker: %s\n",
             pokeAimed, pokeInstances,
             (pokeInstances && pokeAimed == pokeInstances) ? "ok" : "FAIL");
      if (!pokeInstances || pokeAimed != pokeInstances)
        exitCode = 1;
    }

    printf("Quiescence: %s (%u of %u) -- an all-n gate one bit short: %s\n",
           quiesced < n ? "DEFEATED" : "reached", quiesced, n,
           quiesced < n ? "ok" : "FAIL");
    if (quiesced >= n)
      exitCode = 1;
    printf("Ending: tick cap at %u ticks -- the expected ending under -b\n",
           tickCount);
  }

  /*----------------------------------------------------------------------*/
  /*  The -d demonstration's verdict: included under patience, excluded      */
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
    printf("\nDelayed process %d (patience %u sweeps, %u ticks): %s\n",
           dproc, patience, tickCount,
           inSubset
             ? "INCLUDED -- patience let step 1 win"
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
    "usage: example_bkr94acs [-v] [-s seed] [-d process] [-g patience]"
    " [-b mode] n t acast0 acast1 ...\n"
    "  n            total processes (1-%d)\n"
    "  t            max Byzantine faults\n"
    "  acast*      per-process A-Cast strings\n"
    "  -v           verbose: trace every message\n",
    MAX_PROCESSES);
  fprintf(stderr,
    "  -s seed      shuffle seed (0 = ordered delivery)\n"
    "  -d process   hold that process's A-Cast until step 2 first\n"
    "               enables (the WAN laggard; needs t >= 1)\n"
    "  -g patience  patience for the sweep-side decisions,\n"
    "               in completed BPR sweeps -- full Retry-cursor\n"
    "               passes (0 = eager; with -d, -g 1 includes the\n"
    "               laggard in SubSet)\n");
  fprintf(stderr,
    "  -b mode      process %d is Byzantine (needs t >= 1; does not\n"
    "               compose with -d / -g).  The tick cap is then the\n"
    "               expected ending and the exit code reports the\n"
    "               honest assertions.  mode is one of:\n",
    BYZ_PROCESS);
  fprintf(stderr,
    "                 silent    runs no state machine, sends nothing\n"
    "                 equiv<S>  sends (initial, v) to processes\n"
    "                           [0..S) and v with its first byte's\n"
    "                           case bit flipped to [S..n), then stops\n"
    "                 poke      participates honestly through\n"
    "                           COMPLETE, then re-sends READYs with\n"
    "                           the RECEIVED annotation stripped\n");
  return (1);
}
