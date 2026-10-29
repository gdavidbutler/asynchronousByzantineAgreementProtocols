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
 * n seats, each with a message, run Agreement[Q] over an in-memory
 * queue that loses and reorders by seed; the seats that output agree
 * on a subset of the seats and on the message of each seat in it.  The
 * report is the messages of the subset, as each seat's library hands
 * them back, and at a seat that abandoned without one, the messages it
 * accepted.
 *
 * The message is the seat's value: seat j broadcasts it, padded to a
 * fixed length, and a seat's subset is output with the value of every
 * seat in it accepted there.  Nothing here is sent beside a broadcast.
 *
 * Every seat is one bkr94acs seat: it feeds what arrives, sends what
 * its actions name, hands its own messages straight back to itself,
 * never on the queue, and sweeps once per pass of this loop.  That
 * every seat runs Agreement[Q], with the same patience and abandon,
 * swept in step, is this harness's choice; nothing in a seat relies on
 * it.  With -b one seat is Byzantine and holds nothing: it sends the
 * (initial, v) of its message to every seat, and in every BA its
 * round-1 (initial, v) with value 1 to the seats below the split and 0
 * to the rest, and says nothing more.
 *
 * How a run ends: by abandon at each honest seat, at its abandon count
 * or at the round ceiling of an agreement still undecided.
 *
 * usage: example_bkr94acs [-v] [-s seed] [-l loss] [-b seat] [-x split]
 *                         [-p patience] [-a abandon] [-r rounds]
 *                         n t message...     (n messages, up to 63 bytes)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87Fig1.h"
#include "bkr94acs.h"

#define HOLDS(st) (((st) & (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE)) == (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE))

#define MSGLEN 64

struct msg {
  unsigned char kind;
  unsigned char ba;
  unsigned char q;
  unsigned char k;
  unsigned char from;
  unsigned char value;
  unsigned char annot;    /* on an ECHO or a READY */
  unsigned char to;
  unsigned char v[MSGLEN];  /* at round 0: the message */
};

static unsigned long CoinCalls;

static unsigned char
coin(
  void *ctx
){
  unsigned int *seed;

  ++CoinCalls;
  seed = ctx;
  *seed = *seed * 1103515245u + 12345u;
  return ((*seed >> 16) & 1);
}

int
main(
  int argc
 ,char *argv[]
){
  struct bkr94acs **a;
  struct bkr94acsAct *out;
  struct bkr94acsAct *work;
  unsigned char *workTo;      /* per action: the sender of the arrival that returned it */
  struct msg *q;
  unsigned int *subsetAt;
  unsigned int *abandonedAt;
  unsigned long *sent;        /* deliveries by kind: initial, echo, ready */
  unsigned long sz;
  unsigned int patience;
  unsigned int abandon;
  unsigned int n;
  unsigned int t;
  unsigned int maxRounds;
  unsigned int seed;
  unsigned int seed0;
  unsigned int *coinSeed;     /* per seat: the coin is local to its seat */
  unsigned int loss;
  unsigned int verbose;
  unsigned int byz;           /* the Byzantine seat, n if none */
  unsigned int split;
  unsigned int qcap;
  unsigned int qhead;
  unsigned int qtail;
  unsigned int sweep;
  unsigned int dropped;
  unsigned int toByz;
  unsigned int outMax;
  unsigned int workCap;
  unsigned int replyTo;
  unsigned int nwork;
  unsigned int nout;
  unsigned int p;
  unsigned int site;
  unsigned int i;
  unsigned int j;
  unsigned int w;
  unsigned int to;
  int arg;
  const struct bracha87Fig1 *inst;
  int recip;                  /* a READY re-sent on a sweep: to its recipients only */
  const unsigned char *cv;
  unsigned char v[MSGLEN];
  int rc;
  int more;

  verbose = 0;
  seed = 0;
  loss = 0;
  byz = 256;
  split = 0;
  patience = 2;
  abandon = 32;
  maxRounds = 6;
  rc = 1;
  a = 0;
  out = work = 0;
  workTo = 0;
  q = 0;
  subsetAt = abandonedAt = 0;
  sent = 0;
  coinSeed = 0;
  CoinCalls = 0;

  arg = 1;
  while (arg < argc && argv[arg][0] == '-' && argv[arg][1] && argv[arg][2] == '\0') {
    switch (argv[arg][1]) {
    case 'v':
      verbose = 1;
      ++arg;
      continue;
    case 's':
    case 'l':
    case 'b':
    case 'x':
    case 'p':
    case 'a':
    case 'r':
      if (arg + 1 >= argc)
        goto usage;
      switch (argv[arg][1]) {
      case 's': seed = atoi(argv[arg + 1]); break;
      case 'l': loss = atoi(argv[arg + 1]); break;
      case 'b': byz = atoi(argv[arg + 1]); break;
      case 'x': split = atoi(argv[arg + 1]); break;
      case 'p': patience = atoi(argv[arg + 1]); break;
      case 'a': abandon = atoi(argv[arg + 1]); break;
      default:  maxRounds = atoi(argv[arg + 1]); break;
      }
      arg += 2;
      continue;
    default:
      goto usage;
    }
  }
  if (argc - arg < 2)
    goto usage;
  n = atoi(argv[arg]);
  t = atoi(argv[arg + 1]);
  if (n < 1 || n > 64 || t > 21 || n <= 3 * t || loss > 99
   || maxRounds < 1 || maxRounds > 255 || abandon < 1
   || (byz < 256 && (byz >= n || !t))) {
    fprintf(stderr, "n is 1..64 and above 3t, loss below 100, rounds 1..255,"
                    " abandon at least 1 sweep,"
                    " a Byzantine seat below n and only when t > 0\n");
    goto done;
  }
  if (argc - arg != 2 + (int)n)
    goto usage;
  for (j = 0; j < n; ++j)
    if (!argv[arg + 2 + j][0] || strlen(argv[arg + 2 + j]) >= MSGLEN) {
      fprintf(stderr, "each message is 1..%d bytes\n", MSGLEN - 1);
      goto done;
    }
  if (byz < 256 && split > n)
    split = n;
  seed0 = seed;
  if (!(coinSeed = calloc(n, sizeof (*coinSeed)))) {
    fprintf(stderr, "allocation failed\n");
    goto done;
  }
  for (i = 0; i < n; ++i)
    coinSeed[i] = seed + 1 + i;

  sz = bkr94acsSz(n - 1, MSGLEN - 1, maxRounds);
  if (!(a = calloc(n, sizeof (*a)))
   || !(subsetAt = calloc(n, sizeof (*subsetAt))) || !(abandonedAt = calloc(n, sizeof (*abandonedAt)))
   || !(sent = calloc(3, sizeof (*sent)))) {
    fprintf(stderr, "allocation failed\n");
    goto done;
  }
  for (i = 0; i < n; ++i) {
    if (i == byz)
      continue;
    if (!(a[i] = calloc(1, sz))) {
      fprintf(stderr, "allocation failed\n");
      goto done;
    }
    if (bkr94acsInit(a[i], n - 1, t, MSGLEN - 1, i, maxRounds, patience, abandon, coin, coinSeed + i, 0)) {
      fprintf(stderr, "Init refused: abandon must be above patience\n");
      goto done;
    }
  }
  outMax = bkr94acsOut(a[byz == 0 ? 1 : 0]);
  workCap = 16 * outMax;
  if (!(out = calloc(outMax, sizeof (*out))) || !(work = calloc(workCap, sizeof (*work)))
   || !(workTo = calloc(workCap, 1))) {
    fprintf(stderr, "allocation failed\n");
    goto done;
  }
  qcap = 64 * n * n * n + 64;
  if (!(q = calloc(qcap, sizeof (*q)))) {
    fprintf(stderr, "allocation failed\n");
    goto done;
  }
  qhead = qtail = 0;
  sweep = dropped = toByz = 0;
  /* the queue holds one pass's sends and grows to fit them: a seat
   * sends what its actions name, and this harness drops nothing but
   * what -l loses */

  /* every honest seat broadcasts its message */
  for (p = 0; p < n; ++p) {
    if (p == byz)
      continue;
    memset(v, 0, sizeof (v));
    strcpy((char *)v, argv[arg + 2 + p]);
    nwork = bkr94acsBroadcast(a[p], v, work);
    workTo[0] = p;
    site = 0;
    goto run;
qDone:
    ;
  }
  /* the Byzantine seat: the (initial, v) of its message, and its
   * round-1 (initial, v) in every BA, two values */
  if (byz < 256) {
    for (to = 0; to < n; ++to) {
      if (to == byz)
        continue;
      memset(&q[qtail], 0, sizeof (q[qtail]));
      q[qtail].kind = BKR94ACS_INITIAL;
      q[qtail].ba = q[qtail].q = q[qtail].from = byz;
      q[qtail].to = to;
      strcpy((char *)q[qtail].v, argv[arg + 2 + byz]);
      ++qtail;
    }
    for (i = 0; i < n; ++i)
      for (to = 0; to < n; ++to) {
        if (to == byz)
          continue;
        memset(&q[qtail], 0, sizeof (q[qtail]));
        q[qtail].kind = BKR94ACS_INITIAL;
        q[qtail].ba = i;
        q[qtail].q = byz;
        q[qtail].k = 1;
        q[qtail].from = byz;
        q[qtail].value = to < split ? BKR94ACS_V1 : BKR94ACS_V0;
        q[qtail].to = to;
        ++qtail;
      }
  }

  for (;;) {
    while (qhead < qtail) {
      struct msg *m;

      if (seed0) {
        unsigned int r;

        seed = seed * 1103515245u + 12345u;
        r = qhead + ((seed >> 16) & 0x7FFF) % (qtail - qhead);
        if (r != qhead) {
          struct msg tmp;

          tmp = q[qhead];
          q[qhead] = q[r];
          q[r] = tmp;
        }
      }
      if (loss) {
        seed = seed * 1103515245u + 12345u;
        if (((seed >> 16) & 0x7FFF) % 100 < loss) {
          ++dropped;
          ++qhead;
          continue;
        }
      }
      m = &q[qhead++];
      p = m->to;
      if (p == byz) {
        ++toByz;
        continue;
      }
      if (abandonedAt[p])
        continue;
      ++sent[m->kind];
      if (verbose)
        printf("seat %u: %s of (ba %u, q %u, k %u) from %u%s%s\n", p,
               m->kind == BKR94ACS_INITIAL ? "initial" : m->kind == BKR94ACS_ECHO ? "echo" : "ready",
               m->ba, m->q, m->k, m->from, m->k ? "" : " carrying ", m->k ? "" : (char *)m->v);
      nwork = bkr94acsInput(a[p], m->kind, m->ba, m->q, m->k, m->from, m->k ? &m->value : m->v, m->annot, work);
      memset(workTo, m->from, nwork);
      site = 1;
      goto run;
arrivalDone:
      ;
    }
    qhead = qtail = 0;

    ++sweep;
    for (p = 0; p < n; ++p) {
      if (p == byz || abandonedAt[p])
        continue;
      do {
        more = bkr94acsSweep(a[p], out, &nout);
        memcpy(work, out, nout * sizeof (*out));
        nwork = nout;
        memset(workTo, p, nwork);
        site = 2;
        goto run;
sweepDone:
        ;
      } while (more);
    }
    for (p = 0; p < n; ++p)
      if (p != byz && !abandonedAt[p])
        break;
    if (p == n)
      break;
    if (sweep >= 1000 * abandon) {
      /* a guard on this loop, carrying no evidence of anything */
      fprintf(stderr, "harness guard: %u sweeps and not every honest seat abandoned\n", sweep);
      goto done;
    }
  }

  printf("n=%u t=%u seed=%u loss=%u%% patience=%u abandon=%u rounds=%u",
         n, t, seed0, loss, patience, abandon, maxRounds);
  if (byz < 256)
    printf(" Byzantine=%u split=%u", byz, split);
  printf("\n");
  for (p = 0; p < n; ++p) {
    const unsigned char *sb;
    const unsigned char *av;

    if (p == byz) {
      printf("seat %u: Byzantine, sent \"%s\" alike to every seat and two values in each BA's round 1, holds nothing\n", p, argv[arg + 2 + byz]);
      continue;
    }
    if (!(sb = bkr94acsSubSet(a[p]))) {
      /* no subset: the values this seat accepted are still read */
      printf("seat %u: no subset, abandoned at sweep %u, accepted:", p, abandonedAt[p]);
      for (j = 0; j < n; ++j)
        if ((av = bkr94acsValue(a[p], j)))
          printf(" %u:\"%s\"", j, (const char *)av);
      printf("\n");
      rc = 2;
      continue;
    }
    printf("seat %u: subset at sweep %u, abandoned at sweep %u:", p, subsetAt[p], abandonedAt[p]);
    for (j = 0; j < n; ++j) {
      if (!BKR94ACS_BIT_TST(sb, j))
        continue;
      printf(" %u:\"%s\"", j, (const char *)bkr94acsValue(a[p], j));
    }
    printf("\n");
  }
  printf("%u sweeps; delivered %lu initial, %lu echo, %lu ready; %u lost; %u to the Byzantine discarded; %lu coin calls\n",
         sweep, sent[BKR94ACS_INITIAL], sent[BKR94ACS_ECHO], sent[BKR94ACS_READY],
         dropped, toByz, CoinCalls);
  if (rc == 1)
    rc = 0;
done:
  if (a)
    for (i = 0; i < n; ++i)
      free(a[i]);
  free(a);
  free(out);
  free(work);
  free(workTo);
  free(q);
  free(subsetAt);
  free(abandonedAt);
  free(sent);
  free(coinSeed);
  return (rc);

  /* run the actions in work[] at seat p, self by hand-back, return to site */
run:
  while (nwork) {
    struct bkr94acsAct act;

    act = work[--nwork];
    replyTo = workTo[nwork];
    switch (act.act) {
    case BKR94ACS_DECIDED:
      if (verbose)
        printf("seat %u: BA %u decided %u (sweep %u)\n", p, act.ba, act.value, sweep);
      continue;
    case BKR94ACS_SUBSET:
      subsetAt[p] = sweep + (site != 2);
      if (verbose)
        printf("seat %u: subset output (sweep %u)\n", p, subsetAt[p]);
      continue;
    case BKR94ACS_ABANDON:
      abandonedAt[p] = sweep + (site != 2);
      if (verbose)
        printf("seat %u: abandon (sweep %u)\n", p, abandonedAt[p]);
      continue;
    case BKR94ACS_SEND_INITIAL:
    case BKR94ACS_SEND_ECHO:
    case BKR94ACS_SEND_READY:
    case BKR94ACS_REPLY_ECHO:
    case BKR94ACS_REPLY_READY:
    case BKR94ACS_ANSWER_READY:
      break;
    default:
      fprintf(stderr, "seat %u: unknown action %u\n", p, act.act);
      goto done;
    }
    /* a value broadcast's send carries its own bytes, off the instance */
    cv = act.k ? &act.value
       : bracha87Fig1Value(bkr94acsInstance(a[p], act.ba, act.q, 0),
                           act.act == BKR94ACS_SEND_INITIAL ? BRACHA87_INITIAL_ALL
                         : act.act == BKR94ACS_SEND_ECHO || act.act == BKR94ACS_REPLY_ECHO ? BRACHA87_ECHO_ALL
                         : BRACHA87_READY_ALL);
    /* INITIAL to the seats not recorded as holding it; a READY re-sent on
     * a sweep to the seats the instance still owes it, carrying what it
     * says; a reply to the sender of the arrival that returned it; an
     * answer to each seat whose ready the instance holds */
    inst = act.act != BKR94ACS_SEND_INITIAL ? bkr94acsInstance(a[p], act.ba, act.q, act.k) : 0;
    recip = act.act == BKR94ACS_SEND_READY && site == 2;
    for (to = 0; to < n; ++to) {
      unsigned char kind;
      unsigned char annot;

      kind = act.act == BKR94ACS_SEND_INITIAL ? BKR94ACS_INITIAL
           : act.act == BKR94ACS_SEND_ECHO || act.act == BKR94ACS_REPLY_ECHO ? BKR94ACS_ECHO : BKR94ACS_READY;
      if (act.act == BKR94ACS_SEND_INITIAL) {
        if (bkr94acsHolder(a[p], act.ba, act.q, act.k, to))
          continue;
      }
      if (recip && !bracha87Fig1Recipient(inst, to))
        continue;
      if (act.act == BKR94ACS_SEND_ECHO && site == 2 && !bracha87Fig1EchoRecipient(inst, to))
        continue;
      if ((act.act == BKR94ACS_REPLY_ECHO || act.act == BKR94ACS_REPLY_READY) && to != replyTo)
        continue;
      if (act.act == BKR94ACS_ANSWER_READY && !bracha87Fig1ReadySender(inst, to))
        continue;
      /* every echo and ready says whether this seat holds the initial
       * and what rode beside it; a ready says the rest */
      annot = 0;
      if (inst && HOLDS(bracha87Fig1State(inst)))
        annot |= BRACHA87_ANN_HOLDING;
      if (act.act == BKR94ACS_SEND_READY || act.act == BKR94ACS_REPLY_READY
       || act.act == BKR94ACS_ANSWER_READY) {
        if (bracha87Fig1State(inst) & BRACHA87_F1_ACCEPTED)
          annot |= BRACHA87_ANN_ACCEPTED;
        if (BRACHA87_BIT_TST(bracha87Fig1Received(inst), to))
          annot |= BRACHA87_ANN_RECEIVED;
      }
      if (to == p) {
        /* the hand-back is an input like any other; it moves only what
         * the instance records of self, so its place in this loop does
         * not matter.  What it returns goes back to self. */
        if (nwork + outMax > workCap) {
          fprintf(stderr, "harness guard: hand-back cascade too deep\n");
          goto done;
        }
        nout = bkr94acsInput(a[p], kind, act.ba, act.q, act.k, p, cv, annot, out);
        for (w = 0; w < nout; ++w) {
          workTo[nwork] = p;
          work[nwork++] = out[w];
        }
        continue;
      }
      if (qtail + 1 > qcap) {
        struct msg *grown;

        if (!(grown = realloc(q, 2 * qcap * sizeof (*q)))) {
          fprintf(stderr, "queue growth failed at %u entries\n", qcap);
          goto done;
        }
        q = grown;
        qcap *= 2;
      }
      memset(&q[qtail], 0, sizeof (q[qtail]));
      q[qtail].kind = kind;
      q[qtail].ba = act.ba;
      q[qtail].q = act.q;
      q[qtail].k = act.k;
      q[qtail].from = p;
      q[qtail].value = act.value;
      q[qtail].annot = annot;
      q[qtail].to = to;
      if (!act.k)
        memcpy(q[qtail].v, cv, MSGLEN);
      ++qtail;
    }
  }
  switch (site) {
  case 0: goto qDone;
  case 1: goto arrivalDone;
  default: goto sweepDone;
  }

usage:
  fprintf(stderr,
    "usage: example_bkr94acs [-v] [-s seed] [-l loss] [-b seat] [-x split]"
    " [-p patience] [-a abandon] [-r rounds] n t message...\n"
    "  n           seats, 1..64, above 3t; then one message per seat, 1..63 bytes\n"
    "  t           faulty seats the run tolerates\n"
    "  -v          trace every delivery, decision, subset and abandon\n"
    "  -s seed     reorder deliveries by seed (0: in order)\n"
    "  -l loss     lose that percent of deliveries, by seed\n");
  fprintf(stderr,
    "  -b seat     that seat is Byzantine: it sends its message alike to every\n"
    "              seat, and two values of its round-1 broadcast in each BA,\n"
    "              then nothing (needs t > 0)\n"
    "  -x split    the Byzantine sends value 1 to seats below split, 0 to the rest\n"
    "  -p sweeps   patience (default 2)\n"
    "  -a sweeps   abandon, above patience (default 32)\n"
    "  -r rounds   rounds a BA may run, 1..255, taken up to whole phases\n"
    "              of 3 (default 6)\n");
  return (1);
}
