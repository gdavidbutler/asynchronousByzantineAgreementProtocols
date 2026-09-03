/*
 * asynchronousByzantineAgreementProtocols - Example Bracha87 Fig 1 program
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
 * bracha87Fig1.c -- Standalone demonstration of Bracha 1987 Figure 1:
 * Reliable Broadcast (Theorem 1).
 *
 * What Figure 1 brings to the table:
 *
 *   Lemma 2: If two correct processes accept u and v, then u = v.
 *   Lemma 3: If a correct process accepts v, every correct process
 *            eventually accepts v.
 *   Lemma 4: If a correct process p broadcasts v, all correct
 *            processes accept v.
 *
 * Together: a Byzantine initiator cannot show different values to
 * different correct processes and have any of them accept different
 * things.  Either all correct processes converge on one v, or none
 * accept anything.  This is the all-or-nothing property -- what TCP
 * and UDP cannot do, and what authenticated point-to-point alone
 * cannot do under a Byzantine sender.
 *
 * This demo runs ONE Fig 1 instance per process per initiator, with one
 * designated initiator.  The value carried is multi-byte (vLen+1 bytes,
 * supplied on the command line).
 *
 * Optional Byzantine initiator (`-b split`) -- initiator sends the supplied
 * value to processes [0..split-1] and that value with the first byte
 * inverted (XOR 0xFF) to processes [split..n-1].  Verifies Lemma 2:
 * whichever value any correct process accepts (if any), every correct
 * process that accepts agrees with it.
 *
 * Scope: synchronous in-memory queue, every message delivered, no
 * loss; delivery order is deterministic unless -s is given.
 * Exercises the Fig 1 state machine and the BPR sweep that carries
 * it to quiescence; does NOT exercise BPR retry under loss.  See
 * README.md for the full deployment story.
 *
 * How a run ends -- the taxonomy, and which half this demo shows:
 *
 *   QUIESCENT   the owing ending (BPR.md Quiescence), demonstrated
 *               here -- no exit decision at all.  Every process
 *               announces its own accept on the READY it is already
 *               retrying (the ACCEPTED wire bit below) and confirms
 *               the ones it has recorded (the RECEIVED bit), so each
 *               instance's suppress mask reaches all n, READY retires
 *               with it, and a full bracha87Fig1RetryStep pass owes
 *               nothing -- the 0 return.  The process leaves the
 *               sweep rotation and the wire falls silent.  Both bits
 *               are load-bearing: without the second, a process that
 *               records an accept before announcing its own suppresses
 *               the very (ready, v) the announcement rides on, and the
 *               other end waits forever.
 *   ABANDONED   the policy backstop (README.md "Abandonment").  The
 *               honest runs never take it.  The `-b split` runs do:
 *               a Byzantine initiator holds no Fig 1 state, so it
 *               never announces an accept, the gate can never
 *               close, and the correct processes retry a READY no
 *               correct process consumes -- README.md's
 *               Byzantine-silent scenario.  The sweep cap stands in
 *               for the policy here.
 *   sweep cap   a harness guard, not protocol: a bound on the
 *               rotation, carrying no evidence of anything.
 *
 * Deliberately not exercised: loss itself.  A lost announcement no
 * longer strands the process that missed it -- its next (ready, v)
 * arrives here unmarked, un-suppressing it for the next tick's
 * marked re-send -- but every marked re-send in this demo is
 * delivered, so nothing here shows the retry that fair loss needs.
 *
 * Usage:
 *   ./example_bracha87Fig1 [-v] [-s seed] [-o initiator] [-b split] n t value
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87.h"

/*--------------------------------------------------------------------------*/
/*  Constants                                                               */
/*--------------------------------------------------------------------------*/

#define MAX_PROCESSES 16
#define MAX_VLEN  64

/*
 * Harness guard on the sweep rotation.  A run whose instances can
 * quiesce needs a handful of sweeps; this only bounds one that
 * cannot (a `-b split` that leaves the echo count below threshold,
 * so no correct process ever accepts).  It is not a protocol
 * quantity.
 */
#define SWEEP_CAP 1000

/*
 * This demo's wire ACCEPTED bit.  bracha87.h fixes the Fig 1 type in
 * bits 0-1 (BRACHA87_TYPE_MASK) and leaves the higher bits to the
 * framer; bit 4 is where the ACS packer reserves the same annotation,
 * so a bare-layer framer that may later carry ACS traffic picks it up
 * unchanged.  Set on a retried READY whose instance has ACCEPTED
 * (struct bracha87Fig1Act.accepted), read back into
 * bracha87Fig1ProcessAccepted on ingress.
 */
#define WIRE_ACCEPTED 0x10

/*
 * This demo's wire RECEIVED bit, bit 5 where the ACS packer reserves
 * BKR94ACS_RECEIVED.  Decided per RECIPIENT, not per sender: set on a
 * (ready, v) to process j iff bit j is in the instance's RECEIVED mask
 * (bracha87Fig1Received / struct bracha87Fig1Act.received), meaning "I have
 * recorded YOUR accept."  Its ABSENCE is what the receiver acts on --
 * bracha87Fig1ProcessResend on ingress -- so a framer that drops this bit
 * makes every READY unmarked, every receiver re-arms, and the honest
 * run never falls silent.
 */
#define WIRE_RECEIVED 0x20

/*--------------------------------------------------------------------------*/
/*  Message queue                                                           */
/*--------------------------------------------------------------------------*/

struct msg {
  unsigned char type;     /* wire byte: BRACHA87_INITIAL / ECHO / READY
                           * in bits 0-1, + WIRE_ACCEPTED / WIRE_RECEIVED
                           * on a READY */
  unsigned char from;
  unsigned char to;
  unsigned char value[MAX_VLEN];
};

static struct msg *MsgQ;
static unsigned int Qcap;
static unsigned int Qhead;
static unsigned int Qtail;
static unsigned int Vlen;  /* actual payload length: vLen + 1 */

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
  unsigned char type
 ,unsigned char accepted  /* READY only: act's .accepted -> WIRE_ACCEPTED */
 ,unsigned char received  /* READY only: RECEIVED mask bit 'to' -> WIRE_RECEIVED */
 ,unsigned char from
 ,unsigned char to
 ,const unsigned char *value
){
  if (Qtail >= Qcap)
    return;
  MsgQ[Qtail].type = (unsigned char)(type | (accepted ? WIRE_ACCEPTED : 0)
                                          | (received ? WIRE_RECEIVED : 0));
  MsgQ[Qtail].from = from;
  MsgQ[Qtail].to = to;
  memcpy(MsgQ[Qtail].value, value, Vlen);
  ++Qtail;
}

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

/*
 * Print a value as printable string if all bytes are printable,
 * else as hex.  No trailing newline.
 */
static void
printValue(
  const unsigned char *v
){
  unsigned int i;
  int printable;

  printable = 1;
  for (i = 0; i < Vlen; ++i) {
    if (v[i] < 0x20 || v[i] >= 0x7F) {
      printable = 0;
      break;
    }
  }
  if (printable) {
    fputc('"', stdout);
    for (i = 0; i < Vlen; ++i)
      fputc(v[i], stdout);
    fputc('"', stdout);
  } else {
    fputs("0x", stdout);
    for (i = 0; i < Vlen; ++i)
      printf("%02x", v[i]);
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
  unsigned int n;
  unsigned int t;
  unsigned int initiator;
  unsigned int byzSplit;
  unsigned int verbose;
  unsigned int shuffleSeed;
  unsigned int origSeed;

  unsigned char honestVal[MAX_VLEN];
  unsigned char byzVal[MAX_VLEN];

  struct bracha87Fig1 *fig1[MAX_PROCESSES]; /* one instance per process */
  unsigned char accepted[MAX_PROCESSES];    /* 1 if process accepted */
  unsigned char acceptVal[MAX_PROCESSES][MAX_VLEN];

  unsigned long f1sz;
  unsigned int i;
  unsigned int j;
  int arg;
  int exitCode;

  unsigned int acceptCount;
  int lemma2ok;
  int lemma4ok;
  unsigned char firstAccVal[MAX_VLEN];
  int haveFirstAcc;

  unsigned char quiescent[MAX_PROCESSES];   /* left the sweep rotation */
  unsigned int quiesceSweep[MAX_PROCESSES]; /* the sweep it left at */
  unsigned int inRotation;
  unsigned int quiesced;
  unsigned int sweepCount;
  int wireSilent;

  /*----------------------------------------------------------------------*/
  /*  Parse command line                                                  */
  /*----------------------------------------------------------------------*/

  verbose = 0;
  shuffleSeed = 0;
  initiator = 0;
  byzSplit = 0;
  exitCode = 0;

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
    } else if (argv[arg][1] == 'o' && argv[arg][2] == '\0') {
      ++arg;
      if (arg >= argc) goto usage;
      initiator = (unsigned int)atoi(argv[arg]);
      ++arg;
    } else if (argv[arg][1] == 'b' && argv[arg][2] == '\0') {
      ++arg;
      if (arg >= argc) goto usage;
      byzSplit = (unsigned int)atoi(argv[arg]);
      ++arg;
    } else {
      goto usage;
    }
  }

  if (argc - arg < 3) goto usage;
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
  if (initiator >= n) {
    fprintf(stderr, "initiator must be < n\n");
    return (1);
  }
  if (byzSplit > n) {
    fprintf(stderr, "split must be <= n\n");
    return (1);
  }

  /* The honest value is the user-supplied string, NUL-padded. */
  memset(honestVal, 0, sizeof (honestVal));
  Vlen = (unsigned int)strlen(argv[arg]);
  if (Vlen == 0 || Vlen > MAX_VLEN) {
    fprintf(stderr, "value length must be 1..%d\n", MAX_VLEN);
    return (1);
  }
  memcpy(honestVal, argv[arg], Vlen);

  /* The Byzantine equivocation value: first byte XOR 0xFF.  Distinct
   * from honestVal under any non-empty value. */
  memcpy(byzVal, honestVal, Vlen);
  byzVal[0] ^= 0xFF;

  origSeed = shuffleSeed;

  /*----------------------------------------------------------------------*/
  /*  Allocate per-process Fig 1 instance                                    */
  /*----------------------------------------------------------------------*/

  /* vLen encoding: actual = vLen + 1; we have Vlen bytes. */
  f1sz = bracha87Fig1Sz(n - 1, Vlen - 1);

  memset(fig1, 0, sizeof (fig1));
  memset(accepted, 0, sizeof (accepted));
  memset(acceptVal, 0, sizeof (acceptVal));

  for (i = 0; i < n; ++i) {
    /* Byzantine initiator holds no Fig 1 state -- it sends arbitrary
     * messages and never runs the protocol. */
    if (byzSplit && i == initiator)
      continue;
    fig1[i] = (struct bracha87Fig1 *)calloc(1, f1sz);
    if (!fig1[i]) {
      fprintf(stderr, "allocation failed\n");
      exitCode = 1;
      goto cleanup;
    }
    bracha87Fig1Init(fig1[i], (unsigned char)(n - 1),
                     (unsigned char)t, (unsigned char)(Vlen - 1));
  }

  /*----------------------------------------------------------------------*/
  /*  Allocate message queue                                              */
  /*                                                                      */
  /*  One broadcast round is 3 * n^2 messages (INITIAL/ECHO/READY from    */
  /*  n processes to n processes); the retry tick re-sends un-retired     */
  /*  actions, so size the queue with headroom for those sweeps.          */
  /*----------------------------------------------------------------------*/

  if (qAlloc(16u * n * n)) {
    fprintf(stderr, "queue allocation failed\n");
    exitCode = 1;
    goto cleanup;
  }

  /*----------------------------------------------------------------------*/
  /*  Bootstrap: initiator broadcasts INITIAL                                */
  /*----------------------------------------------------------------------*/

  if (byzSplit) {
    /* Byzantine initiator equivocates: honestVal to first split processes,
     * byzVal to the rest. */
    for (j = 0; j < n; ++j)
      qPush(BRACHA87_INITIAL, 0, 0, (unsigned char)initiator,
            (unsigned char)j, (j < byzSplit) ? honestVal : byzVal);
    if (verbose) {
      printf("initiator %u (BYZANTINE): sending ", initiator);
      printValue(honestVal);
      printf(" to processes [0..%u), ", byzSplit);
      printValue(byzVal);
      printf(" to processes [%u..%u)\n", byzSplit, n);
    }
  } else {
    /* Honest initiator: same value to all. */
    bracha87Fig1Initiator(fig1[initiator], honestVal);
    for (j = 0; j < n; ++j)
      qPush(BRACHA87_INITIAL, 0, 0, (unsigned char)initiator,
            (unsigned char)j, honestVal);
    if (verbose) {
      printf("initiator %u (honest): broadcasting ", initiator);
      printValue(honestVal);
      printf("\n");
    }
  }

  if (shuffleSeed)
    qShuffle(&shuffleSeed);

  /*----------------------------------------------------------------------*/
  /*  Drive to quiescence: drain ingress, tick the sweep, repeat.         */
  /*                                                                      */
  /*  In a real deployment the BPR retry is called once per tick, paced   */
  /*  by the application's sleep(tickMs).  Looping until idle would       */
  /*  flood the network -- Bracha BPR retries persist, so every sent      */
  /*  Fig 1 has actions until its gates close; a tight loop empties the   */
  /*  cursor as fast as the CPU runs and overruns kernel buffers.  One    */
  /*  tick per process per pass of this loop is that rate.                */
  /*                                                                      */
  /*  Each process's instance space here is a single Fig 1, so ONE        */
  /*  bracha87Fig1RetryStep call IS a full sweep of it: the 0 return is   */
  /*  the completed traversal that found nothing owed, and                */
  /*  bracha87Fig1SentCount separates that from the pre-broadcast idle    */
  /*  the same 0 also reports.  A process meeting both is QUIESCENT and   */
  /*  leaves the rotation.  It owes nothing to anyone -- READY, the one   */
  /*  action with no local retire, retires only once every process has    */
  /*  announced its accept AND holds this one's -- so no process left     */
  /*  behind is waiting on it.                                            */
  /*  Its ingress stays open: quiescence bounds what a process OWES,      */
  /*  never what it may still be told, and every later delivery to an     */
  /*  accepted instance is a dedup no-op.                                 */
  /*----------------------------------------------------------------------*/

  memset(quiescent, 0, sizeof (quiescent));
  memset(quiesceSweep, 0, sizeof (quiesceSweep));
  inRotation = 0;
  for (i = 0; i < n; ++i)
    if (fig1[i])
      ++inRotation;
  quiesced = 0;
  sweepCount = 0;
  wireSilent = 0;

  for (;;) {
    ++sweepCount;

    while (Qhead < Qtail) {
      struct msg *m;
      struct bracha87Fig1 *f1;
      unsigned char out[3];
      unsigned char type;
      unsigned int nout;
      unsigned int k;
      unsigned int oldTail;
      const unsigned char *cv;

      m = &MsgQ[Qhead++];

      /* Skip messages to the Byzantine initiator -- it has no state. */
      if (byzSplit && m->to == initiator)
        continue;
      if (m->to >= n || m->from >= n)
        continue;

      type = (unsigned char)(m->type & BRACHA87_TYPE_MASK);

      /*
       * INITIAL sender obligation (bracha87Fig1Input header, Note
       * 17): the bare Fig 1 entry does not know its designated
       * initiator, so the CALLER must drop any INITIAL whose
       * authenticated sender is not it -- a forged non-initiator INITIAL
       * would ride the echo cascade to a false ACCEPT.  bkr94acs
       * enforces this inside its Input entries; a bare-layer caller
       * filters here.
       */
      if (type == BRACHA87_INITIAL && m->from != initiator)
        continue;

      f1 = fig1[m->to];
      oldTail = Qtail;

      if (verbose) {
        printf("process %u: recv %s value=", (unsigned)m->to, typeName(type));
        printValue(m->value);
        printf(" from %u\n", (unsigned)m->from);
      }

      nout = bracha87Fig1Input(f1, type, m->from, m->value, out);

      /*
       * ACCEPTED-annotation ingress: the bit on a retried READY says
       * its sender has accepted this instance and consumes no further
       * (ready, v) from us.  Route it AFTER the matching Input, so
       * rdFrom is recorded first and acFrom stays a subset of it.
       */
      if (type == BRACHA87_READY && (m->type & WIRE_ACCEPTED))
        bracha87Fig1ProcessAccepted(f1, m->from);

      /*
       * RECEIVED-annotation ingress, the other half of the same retire:
       * a (ready, v) WITHOUT the bit is its sender showing it has not
       * recorded our accept -- it would have suppressed us otherwise --
       * so un-suppress it for the next tick, whose re-send goes out
       * marked.  Never route one that HAS the bit: a marked READY that
       * re-armed its receiver would ping-pong, and the two would never
       * fall silent.
       */
      if (type == BRACHA87_READY && !(m->type & WIRE_RECEIVED)) {
        bracha87Fig1ProcessResend(f1, m->from);
        /*
         * Leaving the rotation is PROVISIONAL, and this is the whole
         * reason ingress stays open: an unmarked READY is evidence that
         * something IS still owed, and a process that has stopped
         * ticking can never re-send marked.  Re-enter.  Under loss this
         * is the path that makes quiescence reachable rather than
         * merely hoped for -- a lost marked re-send is drawn again by
         * the next unmarked one, and the unmarked one is worthless if
         * nobody is left to hear it.
         */
        if (quiescent[m->to]) {
          quiescent[m->to] = 0;
          --quiesced;
        }
      }

      for (k = 0; k < nout; ++k) {
        cv = bracha87Fig1Value(f1);
        if (!cv)
          continue;

        if (out[k] == BRACHA87_ACCEPT) {
          accepted[m->to] = 1;
          memcpy(acceptVal[m->to], cv, Vlen);
          /* The instance is not told its own index, so the caller
           * supplies it for the self-accept the all-n gate counts. */
          bracha87Fig1ProcessAccepted(f1, m->to);
          if (verbose) {
            printf("process %u: ACCEPT value=", (unsigned)m->to);
            printValue(cv);
            printf("\n");
          }
          continue;
        }

        /* ECHO_ALL or READY_ALL: relay the echoed value to all processes,
         * minus the BPR per-process suppress set (processes that have already
         * echoed/readied/accepted no longer consume this action). */
        if (verbose) {
          printf("process %u: -> %s value=", (unsigned)m->to,
                 (out[k] == BRACHA87_ECHO_ALL) ? "ECHO_ALL" : "READY_ALL");
          printValue(cv);
          printf("\n");
        }
        {
          const unsigned char *skip;
          const unsigned char *received;

          skip = bracha87Fig1Skip(f1, out[k]);
          received = (out[k] == BRACHA87_READY_ALL)
                   ? bracha87Fig1Received(f1) : 0;
          for (j = 0; j < n; ++j) {
            if (skip && BRACHA87_SKIP_TST(skip, j))
              continue;
            qPush((out[k] == BRACHA87_ECHO_ALL)
                    ? BRACHA87_ECHO : BRACHA87_READY,
                  0,
                  received && BRACHA87_SKIP_TST(received, j),
                  m->to, (unsigned char)j, cv);
          }
        }
      }

      if (shuffleSeed && Qtail > oldTail)
        qShuffle(&shuffleSeed);
    }

    /* The queue is drained (Qhead == Qtail): reset the indices so the
     * append-only store's capacity bounds one sweep's traffic, not the
     * whole run's -- a silent qPush drop would fake the silence
     * quiescence is read from. */
    Qhead = Qtail = 0;

    for (i = 0; i < n; ++i) {
      struct bracha87Fig1 *processArr[1];
      struct bracha87Retry retry;
      struct bracha87Fig1Act pacts[BRACHA87_FIG1_RETRY_MAX_ACTS];
      unsigned int n_pacts;
      unsigned int p;

      if (!fig1[i] || quiescent[i])
        continue;

      processArr[0] = fig1[i];
      bracha87RetryInit(&retry);
      n_pacts = bracha87Fig1RetryStep(processArr, 1, &retry, pacts);
      if (!n_pacts && bracha87Fig1SentCount(processArr, 1)) {
        quiescent[i] = 1;
        quiesceSweep[i] = sweepCount;
        ++quiesced;
        if (verbose)
          printf("process %u: QUIESCENT (sweep %u)\n", i, sweepCount);
        continue;
      }
      for (p = 0; p < n_pacts; ++p)
        for (j = 0; j < n; ++j) {
          if (pacts[p].skip && BRACHA87_SKIP_TST(pacts[p].skip, j))
            continue;
          qPush(pacts[p].act == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
              : pacts[p].act == BRACHA87_ECHO_ALL    ? BRACHA87_ECHO
              :                                        BRACHA87_READY,
                pacts[p].accepted,
                pacts[p].received
                  && BRACHA87_SKIP_TST(pacts[p].received, j),
                (unsigned char)i, (unsigned char)j,
                pacts[p].value);
        }
    }

    if (quiesced == inRotation) {
      /* Nothing was output by the sweep that emptied the rotation, so
       * the wire is silent at the exit -- the observable half of the
       * claim the 0 returns make. */
      wireSilent = (Qtail == Qhead);
      break;
    }
    if (sweepCount >= SWEEP_CAP) {
      /*
       * The cap stands in for the abandonment policy a deployment
       * would run here.  A Byzantine initiator holds no Fig 1 state,
       * so it never announces an accept and the all-n gate can never
       * close -- README.md's Byzantine-silent scenario, where the
       * retry tail is correct and only abandonment ends it.  With an
       * honest initiator every process announces, so reaching the cap
       * is a real failure of the run.
       */
      fprintf(stderr,
              "sweep cap (%u) reached: %u of %u processes quiescent\n",
              (unsigned)SWEEP_CAP, quiesced, inRotation);
      if (!byzSplit)
        exitCode = 1;
      break;
    }
  }

  /*----------------------------------------------------------------------*/
  /*  Verify Lemma 2 and Lemma 4                                          */
  /*----------------------------------------------------------------------*/

  /* Lemma 2: all correct processes that accept agree on the value. */
  acceptCount = 0;
  haveFirstAcc = 0;
  lemma2ok = 1;
  memset(firstAccVal, 0, sizeof (firstAccVal));
  for (i = 0; i < n; ++i) {
    if (byzSplit && i == initiator)
      continue;
    if (accepted[i]) {
      ++acceptCount;
      if (!haveFirstAcc) {
        memcpy(firstAccVal, acceptVal[i], Vlen);
        haveFirstAcc = 1;
      } else if (memcmp(firstAccVal, acceptVal[i], Vlen) != 0) {
        lemma2ok = 0;
      }
    }
  }

  /* Lemma 4: if initiator is correct, all correct processes accept (and the
   * value they accept is honestVal -- a stronger property than Lemma 4
   * alone, which the demo verifies for completeness). */
  lemma4ok = 1;
  if (!byzSplit) {
    for (i = 0; i < n; ++i)
      if (!accepted[i] || memcmp(acceptVal[i], honestVal, Vlen) != 0)
        lemma4ok = 0;
  }

  /*----------------------------------------------------------------------*/
  /*  Output summary                                                      */
  /*----------------------------------------------------------------------*/

  printf("\n--- Results (n=%u, t=%u, initiator=%u, seed=%u) ---\n",
         n, t, initiator, origSeed);
  for (i = 0; i < n; ++i) {
    if (byzSplit && i == initiator) {
      printf("Process %u: Byzantine initiator (equivocating, split=%u)\n",
             i, byzSplit);
    } else if (accepted[i]) {
      printf("Process %u: accepted ", i);
      printValue(acceptVal[i]);
      if (quiescent[i])
        printf(", quiescent at sweep %u\n", quiesceSweep[i]);
      else
        printf(", not quiescent (sweep cap)\n");
    } else {
      printf("Process %u: did not accept%s\n", i,
             quiescent[i] ? ", quiescent" : ", not quiescent (sweep cap)");
    }
  }
  printf("Accept count: %u of %u correct processes\n",
         acceptCount, byzSplit ? (n - 1) : n);
  printf("Lemma 2 (accepts agree): %s\n",
         (acceptCount > 1) ? (lemma2ok ? "ok" : "FAIL")
                           : "n/a (fewer than 2 accepts)");
  if (!byzSplit)
    printf("Lemma 4 (correct initiator -> all accept): %s\n",
           lemma4ok ? "ok" : "FAIL");
  printf("Ending: %u of %u processes QUIESCENT in %u sweeps%s\n",
         quiesced, inRotation, sweepCount,
         wireSilent ? ", wire silent" : "");

  if (!lemma2ok || !lemma4ok)
    exitCode = 1;

  /*----------------------------------------------------------------------*/
  /*  Cleanup                                                             */
  /*----------------------------------------------------------------------*/

cleanup:
  for (i = 0; i < n; ++i)
    free(fig1[i]);
  qFree();

  return (exitCode);

usage:
  fprintf(stderr,
    "usage: example_bracha87Fig1 [-v] [-s seed] [-o initiator] [-b split]"
    " n t value\n"
    "  n            total processes (1-%d)\n"
    "  t            max Byzantine faults\n"
    "  value        multi-byte payload to broadcast (1-%d bytes, string)\n",
    MAX_PROCESSES, MAX_VLEN);
  fprintf(stderr,
    "  -v           verbose: trace every message\n"
    "  -s seed      shuffle seed (0 = ordered delivery)\n"
    "  -o initiator designated initiator process (default 0)\n"
    "  -b split     initiator is Byzantine: sends value to processes\n"
    "               [0..split-1], value with first byte XOR 0xFF to\n"
    "               [split..n-1]\n");
  return (1);
}
