/*
 * asynchronousByzantineAgreementProtocols - Example Bracha87 Fig 3 program
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
 * bracha87Fig3.c — Standalone demonstration of Bracha 1987 Figure 3:
 * VALID-set framework parameterized by a caller-supplied N.
 *
 * What Figure 3 brings to the table: the model reduction from
 * Byzantine to crash.
 *
 * Validate accepts a message only if it could have been sent by a
 * correct process — i.e., its value is consistent with N applied to
 * some n-t subset of the prior round's VALID set.  A Byzantine
 * peer's options collapse to:
 *
 *   - send a value that conforms to N: indistinguishable from honest
 *   - send anything else:               never validates, never seen
 *   - stay silent:                      looks like crash
 *
 * A round-based protocol designed under crash-only assumptions
 * therefore runs correctly atop Fig 3 even in a Byzantine world.
 * Lemmas 5/6/7: validations agree across correct peers, and any
 * message broadcast by a correct peer is eventually validated.
 *
 * This demo wires a toy N (binary majority with permissive ties per
 * Implementation Note 6) and runs four rounds of a binary-majority
 * round protocol.  Peer 0 is in one of three modes:
 *
 *   honest:  participates correctly
 *   bogus:   round 0 sends an out-of-range value (rejected at the
 *            VALID^0 base case after Fig 1 cascade); subsequent
 *            rounds send a binary value the recursive VALID check
 *            cannot derive from any n-t subset of the prior round's
 *            VALID set
 *   silent:  sends nothing; tolerated as crash, n-t still reached
 *
 * Caller-driven entry points:
 *
 *   bracha87Fig3Origin   self-initiate a round-(round) broadcast
 *   bracha87Fig3Input    one inbound Fig 1 message → Fig 1 ladder
 *                        + Fig 3 validation cascade + ROUND_COMPLETE
 *                        on n-t-validated rounds
 *   bracha87Fig3Pump     BPR replay of any committed Fig 1 actions
 *                        (idle here because delivery is perfect; the
 *                        call is shown because a real deployment
 *                        needs it)
 *
 * Scope: synchronous deterministic in-memory queue, every input
 * delivered, no loss, no reordering.  See README.md for the
 * deployment story (silence-quorum + K-sweep gate).
 *
 * Usage:
 *   ./example_bracha87Fig3 [-v] [-s seed] [-byz mode] n t [init_values...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87.h"

/*------------------------------------------------------------------------*/
/*  Constants                                                             */
/*------------------------------------------------------------------------*/

#define MAX_PEERS 16
#define ROUNDS    4

/*------------------------------------------------------------------------*/
/*  Toy N: binary majority with permissive subset reachability.          */
/*                                                                        */
/*  Implementation Note 6 (see README.md):                                */
/*    value 0 reachable in some n-t subset iff cnt[0] >= (nt+1)/2         */
/*    value 1 reachable in some n-t subset iff cnt[1] >= nt/2 + 1         */
/*    permissive iff both reachable; tie-break to 0                       */
/*                                                                        */
/*  Returns -1 if any value is out of {0, 1} — catches the round-0       */
/*  Byzantine out-of-range injection at the VALID^0 base case in Fig 3.  */
/*------------------------------------------------------------------------*/

struct toyNctx {
  unsigned int n;
  unsigned int t;
};

static int
toyN(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  struct toyNctx *ctx;
  unsigned int cnt0;
  unsigned int cnt1;
  unsigned int nt;
  unsigned int i;

  (void)k;
  (void)senders;

  ctx = (struct toyNctx *)closure;
  nt = ctx->n - ctx->t;

  cnt0 = cnt1 = 0;
  for (i = 0; i < n_msgs; ++i) {
    if (values[i] == 0) ++cnt0;
    else if (values[i] == 1) ++cnt1;
    else return (-1);
  }

  if (cnt0 >= (nt + 1) / 2 && cnt1 >= nt / 2 + 1) {
    *result = 0;
    return (1);
  }
  if (cnt1 >= nt / 2 + 1) {
    *result = 1;
    return (0);
  }
  *result = 0;
  return (0);
}

/*------------------------------------------------------------------------*/
/*  Per-peer state                                                        */
/*------------------------------------------------------------------------*/

struct peerState {
  struct bracha87Fig3 *fig3;
  struct bracha87Fig1 **fig1;          /* ROUNDS * n instances */
  unsigned char roundValue[ROUNDS + 1];
  unsigned char nextOriginated;        /* highest round originated locally + 1 */
};

/*------------------------------------------------------------------------*/
/*  Message queue                                                         */
/*------------------------------------------------------------------------*/

struct msg {
  unsigned char round;
  unsigned char type;     /* INITIAL / ECHO / READY */
  unsigned char from;
  unsigned char to;
  unsigned char origin;
  unsigned char value;
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
  unsigned char round
 ,unsigned char type
 ,unsigned char from
 ,unsigned char to
 ,unsigned char origin
 ,unsigned char value
){
  if (Qtail >= Qcap)
    return;
  MsgQ[Qtail].round = round;
  MsgQ[Qtail].type = type;
  MsgQ[Qtail].from = from;
  MsgQ[Qtail].to = to;
  MsgQ[Qtail].origin = origin;
  MsgQ[Qtail].value = value;
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
/*  Byzantine modes                                                       */
/*------------------------------------------------------------------------*/

#define BYZ_HONEST 0
#define BYZ_BOGUS  1
#define BYZ_SILENT 2

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

/*------------------------------------------------------------------------*/
/*  Originate the next round for peer self.  Reads VALID^prevRound,      */
/*  applies toyN, stores the next-round value, calls Fig3Origin, and     */
/*  pushes the resulting INITIAL_ALL act to the queue.                   */
/*------------------------------------------------------------------------*/

static void
originateNext(
  struct peerState *st
 ,unsigned char self
 ,unsigned int n
 ,unsigned char prevRound
 ,struct toyNctx *Nctx
){
  unsigned char snd[MAX_PEERS];
  unsigned char val[MAX_PEERS];
  unsigned int rcnt;
  unsigned char nextV;
  struct bracha87Fig3Act act;
  unsigned int j;
  unsigned char nextRound;

  nextRound = (unsigned char)(prevRound + 1);
  if (nextRound >= ROUNDS)
    return;
  if (st->nextOriginated > nextRound)
    return;  /* already originated */

  rcnt = bracha87Fig3GetValid(st->fig3, prevRound, snd, val);
  toyN(Nctx, prevRound, rcnt, snd, val, &nextV);
  st->roundValue[nextRound] = nextV;

  if (bracha87Fig3Origin(st->fig3, st->fig1, nextRound, self, &nextV,
                         &act, 1) == 1) {
    for (j = 0; j < n; ++j)
      qPush(act.round, act.type, self, (unsigned char)j,
            act.origin, act.value ? act.value[0] : 0);
  }
  st->nextOriginated = (unsigned char)(nextRound + 1);
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
  unsigned int byzMode;
  unsigned int verbose;
  unsigned int shuffleSeed;
  unsigned int origSeed;

  unsigned char initVals[MAX_PEERS];
  struct peerState peers[MAX_PEERS];
  struct toyNctx Nctx;

  unsigned long f1sz;
  unsigned int i;
  unsigned int j;
  unsigned int k;
  int arg;
  int exitCode;

  int lemma5ok;

  /*----------------------------------------------------------------------*/
  /*  Parse command line                                                  */
  /*----------------------------------------------------------------------*/

  verbose = 0;
  shuffleSeed = 0;
  byzMode = BYZ_HONEST;
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
    } else if (strcmp(argv[arg], "-byz") == 0) {
      ++arg;
      if (arg >= argc) goto usage;
      if (strcmp(argv[arg], "honest") == 0) byzMode = BYZ_HONEST;
      else if (strcmp(argv[arg], "bogus") == 0) byzMode = BYZ_BOGUS;
      else if (strcmp(argv[arg], "silent") == 0) byzMode = BYZ_SILENT;
      else goto usage;
      ++arg;
    } else {
      goto usage;
    }
  }

  if (argc - arg < 2) goto usage;
  n = (unsigned int)atoi(argv[arg++]);
  t = (unsigned int)atoi(argv[arg++]);

  if (n < 4 || n > MAX_PEERS) {
    fprintf(stderr, "n must be 4..%d\n", MAX_PEERS);
    return (1);
  }
  if (n <= 3 * t) {
    fprintf(stderr, "need n > 3t (n=%u, t=%u)\n", n, t);
    return (1);
  }
  if (t < 1) {
    fprintf(stderr, "need t >= 1 to have a Byzantine peer to study\n");
    return (1);
  }

  origSeed = shuffleSeed;

  for (i = 0; i < n; ++i)
    initVals[i] = 1;
  for (i = 0; arg < argc && i < n; ++i, ++arg)
    initVals[i] = (unsigned char)(atoi(argv[arg]) ? 1 : 0);

  /*----------------------------------------------------------------------*/
  /*  Allocate per-peer state                                             */
  /*----------------------------------------------------------------------*/

  Nctx.n = n;
  Nctx.t = t;

  memset(peers, 0, sizeof (peers));

  f1sz = bracha87Fig1Sz(n - 1, 0);

  for (i = 0; i < n; ++i) {
    if (i == 0 && byzMode != BYZ_HONEST)
      continue;
    peers[i].fig3 = (struct bracha87Fig3 *)calloc(
      1, bracha87Fig3Sz(n - 1, ROUNDS));
    peers[i].fig1 = (struct bracha87Fig1 **)calloc(
      ROUNDS * n, sizeof (struct bracha87Fig1 *));
    if (!peers[i].fig3 || !peers[i].fig1) {
      fprintf(stderr, "allocation failed\n");
      exitCode = 1;
      goto cleanup;
    }
    bracha87Fig3Init(peers[i].fig3, (unsigned char)(n - 1),
                     (unsigned char)t, ROUNDS, toyN, &Nctx);
    for (j = 0; j < ROUNDS * n; ++j) {
      peers[i].fig1[j] = (struct bracha87Fig1 *)calloc(1, f1sz);
      if (!peers[i].fig1[j]) {
        fprintf(stderr, "allocation failed\n");
        exitCode = 1;
        goto cleanup;
      }
      bracha87Fig1Init(peers[i].fig1[j], (unsigned char)(n - 1),
                       (unsigned char)t, 0);
    }
    peers[i].roundValue[0] = initVals[i];
    peers[i].nextOriginated = 1;  /* round 0 will be originated below */
  }

  /*----------------------------------------------------------------------*/
  /*  Allocate message queue                                              */
  /*----------------------------------------------------------------------*/

  if (qAlloc(32u * n * n * ROUNDS)) {
    fprintf(stderr, "queue allocation failed\n");
    exitCode = 1;
    goto cleanup;
  }

  /*----------------------------------------------------------------------*/
  /*  Bootstrap: each non-Byzantine peer originates round 0.              */
  /*  Byzantine bogus peer 0 pre-pushes its bogus messages for every      */
  /*  round (it has no Fig 3 / Fig 1 state to drive autonomously).        */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < n; ++i) {
    struct bracha87Fig3Act act;

    if (i == 0 && byzMode != BYZ_HONEST) {
      if (byzMode == BYZ_BOGUS) {
        /*
         * Round 0: out-of-range value (rejected at VALID^0 base case
         *   even after Fig 1 accepts it via the echo cascade — base
         *   case requires value <= 1).
         * Round k>0: binary value the honest peers' N would not
         *   produce from any n-t subset of round-(k-1) VALID
         *   (rejected at VALID^k recursive case).  Honest peers
         *   produce 1 in this configuration; Byzantine sends 0.
         */
        for (k = 0; k < ROUNDS; ++k) {
          unsigned char v;
          v = (k == 0) ? 7 : 0;
          for (j = 0; j < n; ++j)
            qPush((unsigned char)k, BRACHA87_INITIAL, 0,
                  (unsigned char)j, 0, v);
        }
      }
      continue;
    }

    if (bracha87Fig3Origin(peers[i].fig3, peers[i].fig1, 0,
                           (unsigned char)i, &initVals[i],
                           &act, 1) == 1) {
      for (j = 0; j < n; ++j)
        qPush(act.round, act.type, (unsigned char)i,
              (unsigned char)j, act.origin,
              act.value ? act.value[0] : 0);
    }
    peers[i].nextOriginated = 1;
  }

  if (shuffleSeed)
    qShuffle(&shuffleSeed);

  /*----------------------------------------------------------------------*/
  /*  Process message queue                                               */
  /*                                                                      */
  /*  bracha87Fig3Input drives Fig 1 ladder + Fig 3 cascade in a single   */
  /*  call.  ROUND_COMPLETE acts trigger this peer's next-round           */
  /*  origination via toyN.                                               */
  /*----------------------------------------------------------------------*/

  while (Qhead < Qtail) {
    struct msg *m;
    struct bracha87Fig3Act acts[BRACHA87_FIG3_MAX_ACTS];
    unsigned int nacts;
    unsigned int oldTail;

    m = &MsgQ[Qhead++];

    if (m->to == 0 && byzMode != BYZ_HONEST)
      continue;
    if (m->round >= ROUNDS || m->origin >= n || m->from >= n)
      continue;

    oldTail = Qtail;

    if (verbose)
      printf("peer %u: recv %s(round=%u, origin=%u, value=%u) from %u\n",
             (unsigned)m->to, typeName(m->type),
             (unsigned)m->round, (unsigned)m->origin,
             (unsigned)m->value, (unsigned)m->from);

    nacts = bracha87Fig3Input(peers[m->to].fig3, peers[m->to].fig1,
                              m->round, m->origin, m->type, m->from,
                              &m->value, acts, BRACHA87_FIG3_MAX_ACTS);

    for (k = 0; k < nacts; ++k) {
      switch (acts[k].act) {
      case BRACHA87_ECHO_ALL:
      case BRACHA87_READY_ALL:
        if (verbose) {
          printf("peer %u: -> %s(round=%u, origin=%u, value=%u)\n",
                 (unsigned)m->to,
                 (acts[k].act == BRACHA87_ECHO_ALL) ? "ECHO_ALL"
                                                    : "READY_ALL",
                 (unsigned)acts[k].round, (unsigned)acts[k].origin,
                 (unsigned)(acts[k].value ? acts[k].value[0] : 0));
        }
        for (j = 0; j < n; ++j)
          qPush(acts[k].round, acts[k].type, m->to,
                (unsigned char)j, acts[k].origin,
                acts[k].value ? acts[k].value[0] : 0);
        break;

      case BRACHA87_FIG3_ROUND_COMPLETE:
        if (verbose)
          printf("peer %u: ROUND_COMPLETE round=%u (|VALID^%u|=%u)\n",
                 (unsigned)m->to, (unsigned)acts[k].round,
                 (unsigned)acts[k].round,
                 bracha87Fig3ValidCount(peers[m->to].fig3,
                                        acts[k].round));
        originateNext(&peers[m->to], (unsigned char)m->to, n,
                      acts[k].round, &Nctx);
        break;
      }
    }

    if (shuffleSeed && Qtail > oldTail)
      qShuffle(&shuffleSeed);
  }

  /*----------------------------------------------------------------------*/
  /*  Pump tick                                                           */
  /*                                                                      */
  /*  In a real deployment, the BPR pump is called once per tick.         */
  /*  Looping until idle would flood the network — see bracha87.h's       */
  /*  flood warning.  The call is shown here as a representative tick.    */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < n; ++i) {
    struct bracha87Pump pump;
    struct bracha87Fig3Act pacts[BRACHA87_FIG3_MAX_ACTS];
    unsigned int n_pacts;

    if (i == 0 && byzMode != BYZ_HONEST)
      continue;
    bracha87PumpInit(&pump);
    n_pacts = bracha87Fig3Pump(peers[i].fig3, peers[i].fig1,
                               &pump, pacts, BRACHA87_FIG3_MAX_ACTS);
    for (k = 0; k < n_pacts; ++k)
      for (j = 0; j < n; ++j)
        qPush(pacts[k].round, pacts[k].type, (unsigned char)i,
              (unsigned char)j, pacts[k].origin,
              pacts[k].value ? pacts[k].value[0] : 0);
  }

  /*----------------------------------------------------------------------*/
  /*  Drain the post-pump replay queue.  Receivers dedup at Fig1Input.    */
  /*----------------------------------------------------------------------*/

  while (Qhead < Qtail) {
    struct msg *m;
    struct bracha87Fig3Act dacts[BRACHA87_FIG3_MAX_ACTS];

    m = &MsgQ[Qhead++];
    if (m->to == 0 && byzMode != BYZ_HONEST) continue;
    if (m->round >= ROUNDS || m->origin >= n || m->from >= n) continue;
    bracha87Fig3Input(peers[m->to].fig3, peers[m->to].fig1,
                      m->round, m->origin, m->type, m->from,
                      &m->value, dacts, BRACHA87_FIG3_MAX_ACTS);
    /* Replay-induced acts are duplicates; discarded. */
  }

  /*----------------------------------------------------------------------*/
  /*  Verify Lemma 5: VALID sets agree across honest peers.               */
  /*----------------------------------------------------------------------*/

  lemma5ok = 1;
  for (k = 0; k < ROUNDS; ++k) {
    unsigned char refSnd[MAX_PEERS];
    unsigned char refVal[MAX_PEERS];
    unsigned int refCnt;
    int haveRef;

    haveRef = 0;
    refCnt = 0;
    memset(refSnd, 0, sizeof (refSnd));
    memset(refVal, 0, sizeof (refVal));

    for (j = 0; j < n; ++j) {
      unsigned char snd[MAX_PEERS];
      unsigned char val[MAX_PEERS];
      unsigned int cnt;

      if (j == 0 && byzMode != BYZ_HONEST)
        continue;
      cnt = bracha87Fig3GetValid(peers[j].fig3, (unsigned char)k,
                                 snd, val);
      if (!haveRef) {
        memcpy(refSnd, snd, cnt);
        memcpy(refVal, val, cnt);
        refCnt = cnt;
        haveRef = 1;
      } else {
        unsigned int x;
        if (cnt != refCnt) lemma5ok = 0;
        else for (x = 0; x < cnt; ++x)
          if (snd[x] != refSnd[x] || val[x] != refVal[x])
            lemma5ok = 0;
      }
    }
  }

  /*----------------------------------------------------------------------*/
  /*  Output summary                                                      */
  /*----------------------------------------------------------------------*/

  printf("\n--- Results (n=%u, t=%u, byz=%s, seed=%u) ---\n",
         n, t,
         (byzMode == BYZ_HONEST) ? "honest"
       : (byzMode == BYZ_BOGUS)  ? "bogus"
       :                           "silent",
         origSeed);

  for (j = 0; j < n; ++j) {
    if (j == 0 && byzMode != BYZ_HONEST) {
      printf("Peer %u: Byzantine (%s)\n", j,
             (byzMode == BYZ_BOGUS) ? "injecting bogus values"
                                    : "silent / crash-equivalent");
      continue;
    }
    printf("Peer %u: |VALID^k| =", j);
    for (k = 0; k < ROUNDS; ++k)
      printf(" %u", bracha87Fig3ValidCount(peers[j].fig3,
                                           (unsigned char)k));
    printf("  (broadcast values:");
    for (k = 0; k < ROUNDS; ++k)
      printf(" %u", (unsigned)peers[j].roundValue[k]);
    printf(")\n");
  }
  printf("Lemma 5 (validations agree across honest peers): %s\n",
         lemma5ok ? "ok" : "FAIL");

  if (!lemma5ok)
    exitCode = 1;

  /*----------------------------------------------------------------------*/
  /*  Cleanup                                                             */
  /*----------------------------------------------------------------------*/

cleanup:
  for (i = 0; i < n; ++i) {
    if (peers[i].fig1) {
      for (j = 0; j < ROUNDS * n; ++j)
        free(peers[i].fig1[j]);
      free(peers[i].fig1);
    }
    free(peers[i].fig3);
  }
  qFree();

  return (exitCode);

usage:
  fprintf(stderr,
    "usage: example_bracha87Fig3 [-v] [-s seed] [-byz mode]"
    " n t [init_values...]\n");
  fprintf(stderr,
    "  n          total peers (4-%d)\n"
    "  t          max Byzantine faults (>= 1)\n"
    "  init_values  per-peer initial values (0 or 1), default all 1\n",
    MAX_PEERS);
  fprintf(stderr,
    "  -v         verbose: trace every message\n"
    "  -s seed    shuffle seed (0 = ordered delivery)\n"
    "  -byz mode  Byzantine mode for peer 0:\n"
    "             honest  participates correctly (default)\n"
    "             bogus   sends out-of-range / non-conforming values\n"
    "             silent  sends nothing (crash-equivalent)\n");
  return (1);
}
