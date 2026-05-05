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
 * bracha87Fig1.c — Standalone demonstration of Bracha 1987 Figure 1:
 * Reliable Broadcast (Theorem 1).
 *
 * What Figure 1 brings to the table:
 *
 *   Lemma 3: If a correct process accepts v, every correct process
 *            eventually accepts v.
 *   Lemma 4: If a correct process p broadcasts v, all correct
 *            processes accept v.
 *
 * Together: a Byzantine origin cannot show different values to
 * different correct peers and have any of them accept different
 * things.  Either all correct peers converge on one v, or none
 * accept anything.  This is the all-or-nothing property — what TCP
 * and UDP cannot do, and what authenticated point-to-point alone
 * cannot do under a Byzantine sender.
 *
 * This demo runs ONE Fig 1 instance per peer per origin, with one
 * designated origin.  The value carried is multi-byte (vLen+1 bytes,
 * supplied on the command line).
 *
 * Optional Byzantine origin (`-b split`) — origin sends the supplied
 * value to peers [0..split-1] and that value with the first byte
 * inverted (XOR 0xFF) to peers [split..n-1].  Verifies Lemma 3:
 * whichever value any correct peer accepts (if any), every correct
 * peer that accepts agrees with it.
 *
 * Scope: synchronous deterministic in-memory queue, every input
 * delivered, no loss.  Exercises only the Fig 1 state machine; does
 * NOT exercise BPR replay under loss.  See README.md for the full
 * deployment story.
 *
 * Usage:
 *   ./example_bracha87Fig1 [-v] [-s seed] [-o origin] [-b split] n t value
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87.h"

/*------------------------------------------------------------------------*/
/*  Constants                                                             */
/*------------------------------------------------------------------------*/

#define MAX_PEERS 16
#define MAX_VLEN  64

/*------------------------------------------------------------------------*/
/*  Message queue                                                         */
/*------------------------------------------------------------------------*/

struct msg {
  unsigned char type;     /* BRACHA87_INITIAL / ECHO / READY */
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
 ,unsigned char from
 ,unsigned char to
 ,const unsigned char *value
){
  if (Qtail >= Qcap)
    return;
  MsgQ[Qtail].type = type;
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

/*------------------------------------------------------------------------*/
/*  Verbose helpers                                                       */
/*------------------------------------------------------------------------*/

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

/*------------------------------------------------------------------------*/
/*  Main                                                                  */
/*------------------------------------------------------------------------*/

int
main(
  int argc
 ,char *argv[]
){
  unsigned int n;
  unsigned int t;
  unsigned int origin;
  unsigned int byzSplit;
  unsigned int verbose;
  unsigned int shuffleSeed;
  unsigned int origSeed;

  unsigned char honestVal[MAX_VLEN];
  unsigned char byzVal[MAX_VLEN];

  struct bracha87Fig1 *fig1[MAX_PEERS]; /* one instance per peer */
  unsigned char accepted[MAX_PEERS];    /* 1 if peer accepted */
  unsigned char acceptVal[MAX_PEERS][MAX_VLEN];

  unsigned long f1sz;
  unsigned int i;
  unsigned int j;
  int arg;
  int exitCode;

  unsigned int acceptCount;
  int lemma3ok;
  int lemma4ok;
  unsigned char firstAccVal[MAX_VLEN];
  int haveFirstAcc;

  /*----------------------------------------------------------------------*/
  /*  Parse command line                                                  */
  /*----------------------------------------------------------------------*/

  verbose = 0;
  shuffleSeed = 0;
  origin = 0;
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
      origin = (unsigned int)atoi(argv[arg]);
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

  if (n < 1 || n > MAX_PEERS) {
    fprintf(stderr, "n must be 1..%d\n", MAX_PEERS);
    return (1);
  }
  if (n <= 3 * t) {
    fprintf(stderr, "need n > 3t (n=%u, t=%u)\n", n, t);
    return (1);
  }
  if (origin >= n) {
    fprintf(stderr, "origin must be < n\n");
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
  /*  Allocate per-peer Fig 1 instance                                    */
  /*----------------------------------------------------------------------*/

  /* vLen encoding: actual = vLen + 1; we have Vlen bytes. */
  f1sz = bracha87Fig1Sz(n - 1, Vlen - 1);

  memset(fig1, 0, sizeof (fig1));
  memset(accepted, 0, sizeof (accepted));
  memset(acceptVal, 0, sizeof (acceptVal));

  for (i = 0; i < n; ++i) {
    /* Byzantine origin holds no Fig 1 state — it sends arbitrary
     * messages and never runs the protocol. */
    if (byzSplit && i == origin)
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
  /*  Up to 3 * n^2 messages: INITIAL/ECHO/READY from n peers to n peers. */
  /*----------------------------------------------------------------------*/

  if (qAlloc(16u * n * n)) {
    fprintf(stderr, "queue allocation failed\n");
    exitCode = 1;
    goto cleanup;
  }

  /*----------------------------------------------------------------------*/
  /*  Bootstrap: origin broadcasts INITIAL                                */
  /*----------------------------------------------------------------------*/

  if (byzSplit) {
    /* Byzantine origin equivocates: honestVal to first split peers,
     * byzVal to the rest. */
    for (j = 0; j < n; ++j)
      qPush(BRACHA87_INITIAL, (unsigned char)origin, (unsigned char)j,
            (j < byzSplit) ? honestVal : byzVal);
    if (verbose) {
      printf("origin %u (BYZANTINE): sending ", origin);
      printValue(honestVal);
      printf(" to peers [0..%u), ", byzSplit);
      printValue(byzVal);
      printf(" to peers [%u..%u)\n", byzSplit, n);
    }
  } else {
    /* Honest origin: same value to all. */
    bracha87Fig1Origin(fig1[origin], honestVal);
    for (j = 0; j < n; ++j)
      qPush(BRACHA87_INITIAL, (unsigned char)origin,
            (unsigned char)j, honestVal);
    if (verbose) {
      printf("origin %u (honest): broadcasting ", origin);
      printValue(honestVal);
      printf("\n");
    }
  }

  if (shuffleSeed)
    qShuffle(&shuffleSeed);

  /*----------------------------------------------------------------------*/
  /*  Process message queue                                               */
  /*----------------------------------------------------------------------*/

  while (Qhead < Qtail) {
    struct msg *m;
    struct bracha87Fig1 *f1;
    unsigned char out[3];
    unsigned int nout;
    unsigned int k;
    unsigned int oldTail;
    const unsigned char *cv;

    m = &MsgQ[Qhead++];

    /* Skip messages to the Byzantine origin — it has no state. */
    if (byzSplit && m->to == origin)
      continue;
    if (m->to >= n || m->from >= n)
      continue;

    f1 = fig1[m->to];
    oldTail = Qtail;

    if (verbose) {
      printf("peer %u: recv %s value=", (unsigned)m->to, typeName(m->type));
      printValue(m->value);
      printf(" from %u\n", (unsigned)m->from);
    }

    nout = bracha87Fig1Input(f1, m->type, m->from, m->value, out);

    for (k = 0; k < nout; ++k) {
      cv = bracha87Fig1Value(f1);
      if (!cv)
        continue;

      if (out[k] == BRACHA87_ACCEPT) {
        accepted[m->to] = 1;
        memcpy(acceptVal[m->to], cv, Vlen);
        if (verbose) {
          printf("peer %u: ACCEPT value=", (unsigned)m->to);
          printValue(cv);
          printf("\n");
        }
        continue;
      }

      /* ECHO_ALL or READY_ALL: relay the committed value to all peers. */
      if (verbose) {
        printf("peer %u: -> %s value=", (unsigned)m->to,
               (out[k] == BRACHA87_ECHO_ALL) ? "ECHO_ALL" : "READY_ALL");
        printValue(cv);
        printf("\n");
      }
      for (j = 0; j < n; ++j)
        qPush((out[k] == BRACHA87_ECHO_ALL)
                ? BRACHA87_ECHO : BRACHA87_READY,
              m->to, (unsigned char)j, cv);
    }

    if (shuffleSeed && Qtail > oldTail)
      qShuffle(&shuffleSeed);
  }

  /*----------------------------------------------------------------------*/
  /*  Pump tick                                                           */
  /*                                                                      */
  /*  In a real deployment, the BPR pump is called once per tick,         */
  /*  paced by the application's sleep(tickMs).  Looping until idle       */
  /*  would flood the network — Bracha BPR replays are persistent, so     */
  /*  every committed Fig 1 always has actions; a tight loop empties      */
  /*  the cursor as fast as the CPU runs and overruns kernel UDP          */
  /*  buffers.  The call is shown here as a representative single tick.   */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < n; ++i) {
    struct bracha87Fig1 *peerArr[1];
    struct bracha87Pump pump;
    struct bracha87Fig1Act pacts[BRACHA87_FIG1_PUMP_MAX_ACTS];
    unsigned int n_pacts;
    unsigned int p;

    if (byzSplit && i == origin) continue;
    if (!fig1[i]) continue;

    peerArr[0] = fig1[i];
    bracha87PumpInit(&pump);
    n_pacts = bracha87Fig1PumpStep(peerArr, 1, &pump, pacts,
                                   BRACHA87_FIG1_PUMP_MAX_ACTS);
    for (p = 0; p < n_pacts; ++p)
      for (j = 0; j < n; ++j)
        qPush(pacts[p].act == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
            : pacts[p].act == BRACHA87_ECHO_ALL    ? BRACHA87_ECHO
            :                                        BRACHA87_READY,
              (unsigned char)i, (unsigned char)j, pacts[p].value);
  }

  /*----------------------------------------------------------------------*/
  /*  Drain the post-pump replay queue.  Receivers dedup at Fig1Input,    */
  /*  so under perfect delivery these replays produce no new state — the  */
  /*  drain mirrors what a deployment loop does on every tick.            */
  /*----------------------------------------------------------------------*/

  while (Qhead < Qtail) {
    struct msg *m;
    struct bracha87Fig1 *f1;
    unsigned char out[3];
    unsigned int nout;
    unsigned int k;
    const unsigned char *cv;

    m = &MsgQ[Qhead++];
    if (byzSplit && m->to == origin) continue;
    if (m->to >= n || m->from >= n) continue;
    f1 = fig1[m->to];
    nout = bracha87Fig1Input(f1, m->type, m->from, m->value, out);
    for (k = 0; k < nout; ++k) {
      cv = bracha87Fig1Value(f1);
      if (!cv) continue;
      if (out[k] == BRACHA87_ACCEPT) {
        /* already accepted; dedup at Fig1Input prevents re-trigger */
        continue;
      }
      /* ECHO_ALL / READY_ALL replays from this receiver do not propagate
       * further in the demo — they are equivalent to the ones already in
       * the system.  Under loss this is where new echoes/readys would
       * help peers below threshold. */
    }
  }

  /*----------------------------------------------------------------------*/
  /*  Verify Lemma 3 and Lemma 4                                          */
  /*----------------------------------------------------------------------*/

  /* Lemma 3: all correct peers that accept agree on the value. */
  acceptCount = 0;
  haveFirstAcc = 0;
  lemma3ok = 1;
  memset(firstAccVal, 0, sizeof (firstAccVal));
  for (i = 0; i < n; ++i) {
    if (byzSplit && i == origin)
      continue;
    if (accepted[i]) {
      ++acceptCount;
      if (!haveFirstAcc) {
        memcpy(firstAccVal, acceptVal[i], Vlen);
        haveFirstAcc = 1;
      } else if (memcmp(firstAccVal, acceptVal[i], Vlen) != 0) {
        lemma3ok = 0;
      }
    }
  }

  /* Lemma 4: if origin is correct, all correct peers accept (and the
   * value they accept is honestVal — a stronger property than Lemma 4
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

  printf("\n--- Results (n=%u, t=%u, origin=%u, seed=%u) ---\n",
         n, t, origin, origSeed);
  for (i = 0; i < n; ++i) {
    if (byzSplit && i == origin) {
      printf("Peer %u: Byzantine origin (equivocating, split=%u)\n",
             i, byzSplit);
    } else if (accepted[i]) {
      printf("Peer %u: accepted ", i);
      printValue(acceptVal[i]);
      printf("\n");
    } else {
      printf("Peer %u: did not accept\n", i);
    }
  }
  printf("Accept count: %u of %u correct peers\n",
         acceptCount, byzSplit ? (n - 1) : n);
  printf("Lemma 3 (accepts agree): %s\n",
         (acceptCount > 1) ? (lemma3ok ? "ok" : "FAIL")
                           : "n/a (fewer than 2 accepts)");
  if (!byzSplit)
    printf("Lemma 4 (correct origin -> all accept): %s\n",
           lemma4ok ? "ok" : "FAIL");

  if (!lemma3ok || !lemma4ok)
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
    "usage: example_bracha87Fig1 [-v] [-s seed] [-o origin] [-b split]"
    " n t value\n"
    "  n        total peers (1-%d)\n"
    "  t        max Byzantine faults\n"
    "  value    multi-byte payload to broadcast (1-%d bytes, string)\n"
    "  -v       verbose: trace every message\n"
    "  -s seed  shuffle seed (0 = ordered delivery)\n"
    "  -o orig  designated origin peer (default 0)\n"
    "  -b split origin is Byzantine: sends value to peers [0..split-1],\n"
    "           value with first byte XOR 0xFF to [split..n-1]\n",
    MAX_PEERS, MAX_VLEN);
  return (1);
}
