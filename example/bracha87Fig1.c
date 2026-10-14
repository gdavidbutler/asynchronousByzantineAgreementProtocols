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
 * Bracha 1987 Figure 1: one seat shares a message with the others,
 * over an in-memory queue that loses and reorders by seed.  The
 * message is v, and the report is the message each seat holds at the
 * end.
 *
 * Beside the message rides the caller's own byte to each seat, re-sent
 * with it by BPR until that seat is heard from.  Each seat's caller
 * records the sweep it first held the message and the byte from p.
 * The message is what every seat accepts alike, and no seat echoes
 * it before it holds the byte, however many others have: the others'
 * echoes and readies take a seat through steps 1 to 3 only once the
 * byte is in hand, and p's re-send is what brings the byte.  A seat
 * that never held the byte never accepted; the report says so for
 * that seat.
 *
 * Every process here is one caller with one instance and no knowledge
 * of the others: it feeds what arrives, sends what its actions name,
 * and hands its own messages straight back to itself -- the hand-back
 * is local, never on the queue, and the actions it returns are run
 * like any other.
 *
 * How it ends: by abandon at each process, the abandon count of sweeps
 * after it started.  Before that the re-sends fall silent: a (ready, v)
 * carries two bits of this caller's, that its sender has accepted and
 * that it holds the recipient's accept, and a seat re-sends its ready
 * only to the seats not yet recorded as having accepted, and once more
 * to a seat whose ready arrived without the second bit.  The report
 * shows the sweep of each seat's last re-send of any kind against its
 * abandon.
 *
 * usage: example_bracha87Fig1 [-v] [-s seed] [-l loss] [-o initiator]
 *                             [-a abandon] n t value
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87Fig1.h"

#define HOLDS(st) (((st) & (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE)) == (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE))

struct msg {
  unsigned char kind;
  unsigned char from;
  unsigned char to;
  unsigned char beside;   /* the byte beside (initial, v), on BESIDE */
  unsigned char annot;    /* the annotations, on ECHO and READY */
  unsigned char value[1]; /* vLen bytes */
};

int
main(
  int argc
 ,char *argv[]
){
  struct bracha87Fig1 **f;
  unsigned char *q;         /* struct msg array, msz bytes each */
  unsigned int *held;       /* sweep a process first held both, 0 = not yet */
  unsigned int *accepted;   /* sweep a process accepted, 0 = not yet */
  unsigned int *abandoned;  /* sweep a process abandoned, 0 = not yet */
  unsigned int *lastResend; /* sweep of a process's last re-send, 0 = none */
  const unsigned char *value;
  const unsigned char *cv;
  int resend;               /* a READY re-sent on a sweep */
  unsigned char besideHeld[256];  /* per process: a beside message kept ahead of its initial */
  unsigned long sz;
  unsigned long msz;
  unsigned int abandon;
  unsigned int n;
  unsigned int t;
  unsigned int vLen;
  unsigned int initiator;
  unsigned int seed;
  unsigned int seed0;       /* as given, for the report */
  unsigned int loss;
  unsigned int verbose;
  unsigned int qcap;
  unsigned int qhead;
  unsigned int qtail;
  unsigned int sweep;
  unsigned int now;         /* the sweep an event is charged to */
  unsigned int dropped;
  unsigned int p;           /* the process whose actions are being run */
  unsigned int site;        /* where to return to after running them */
  unsigned int i;
  unsigned int j;
  unsigned int k;
  unsigned int nout;
  unsigned int nwork;
  unsigned int a;
  unsigned char annot;
  unsigned char out[BRACHA87_FIG1_OUT];
  unsigned char work[64];   /* actions still to run at p; a hand-back adds */
  int arg;
  int rc;

  verbose = 0;
  seed = 0;
  loss = 0;
  initiator = 0;
  abandon = 32;
  rc = 1;
  f = 0;
  q = 0;
  held = accepted = abandoned = lastResend = 0;

  arg = 1;
  while (arg < argc && argv[arg][0] == '-' && argv[arg][1] && argv[arg][2] == '\0') {
    switch (argv[arg][1]) {
    case 'v':
      verbose = 1;
      ++arg;
      continue;
    case 's':
    case 'l':
    case 'o':
    case 'a':
      if (arg + 1 >= argc)
        goto usage;
      switch (argv[arg][1]) {
      case 's': seed = atoi(argv[arg + 1]); break;
      case 'l': loss = atoi(argv[arg + 1]); break;
      case 'o': initiator = atoi(argv[arg + 1]); break;
      default:  abandon = atoi(argv[arg + 1]); break;
      }
      arg += 2;
      continue;
    default:
      goto usage;
    }
  }
  if (argc - arg != 3)
    goto usage;
  n = atoi(argv[arg]);
  t = atoi(argv[arg + 1]);
  value = (const unsigned char *)argv[arg + 2];
  vLen = strlen(argv[arg + 2]);
  if (!vLen) {
    fprintf(stderr, "value must not be empty\n");
    goto done;
  }
  if (loss > 99) {
    fprintf(stderr, "loss is a percent below 100\n");
    goto done;
  }
  if (n < 1 || n > 256 || vLen > 256 || initiator >= n || n <= 3 * t) {
    fprintf(stderr, "n is 1..256 and above 3t, the message 1..256 bytes, the initiator below n\n");
    goto done;
  }
  sz = bracha87Fig1Sz(n - 1, vLen - 1, t + 1);
  if (!(f = calloc(n, sizeof (*f))) || !(held = calloc(n, sizeof (*held)))
   || !(accepted = calloc(n, sizeof (*accepted)))
   || !(abandoned = calloc(n, sizeof (*abandoned)))
   || !(lastResend = calloc(n, sizeof (*lastResend)))) {
    fprintf(stderr, "allocation failed\n");
    goto done;
  }
  memset(besideHeld, 0, sizeof (besideHeld));
  for (i = 0; i < n; ++i) {
    if (!(f[i] = calloc(1, sz))) {
      fprintf(stderr, "allocation failed\n");
      goto done;
    }
    if (abandon < 1
     || bracha87Fig1Init(f[i], n - 1, t, vLen - 1, t + 1, initiator, i, abandon, 1)) {
      fprintf(stderr, "abandon is at least 1 sweep\n");
      goto done;
    }
  }
  msz = sizeof (struct msg) - 1 + vLen;
  qcap = 8 * n * n + 8;
  if (!(q = calloc(qcap, msz))) {
    fprintf(stderr, "allocation failed\n");
    goto done;
  }
  qhead = qtail = 0;
#define Q(x) ((struct msg *)(q + (x) * msz))
  seed0 = seed;
  sweep = 0;
  dropped = 0;

  /* step 0: (initial, v) and the byte beside it to all, self by hand-back */
  if (bracha87Fig1Initiator(f[initiator], value)) {
    fprintf(stderr, "Initiator refused\n");
    goto done;
  }
  if (verbose)
    printf("initiator %u: (initial, \"%s\") and one byte beside it to all\n",
           initiator, argv[arg + 2]);
  p = initiator;
  now = 1;
  nwork = 0;
  for (j = 0; j < n; ++j) {
    if (j == p) {
      nout = bracha87Fig1Input(f[p], BRACHA87_INITIAL, p, value, 0, out);
      for (k = 0; k < nout; ++k)
        work[nwork++] = out[k];
      nout = bracha87Fig1Input(f[p], BRACHA87_BESIDE, p, 0, 0, out);
      for (k = 0; k < nout; ++k)
        work[nwork++] = out[k];
      continue;
    }
    Q(qtail)->kind = BRACHA87_INITIAL;
    Q(qtail)->from = p;
    Q(qtail)->to = j;
    memcpy(Q(qtail)->value, value, vLen);
    ++qtail;
    Q(qtail)->kind = BRACHA87_BESIDE;
    Q(qtail)->from = p;
    Q(qtail)->to = j;
    Q(qtail)->beside = j ^ 0x5A;
    ++qtail;
  }
  site = 0;
  goto run;
step0done:

  for (;;) {
    /* deliver, in an order the seed decides, losing by the seed */
    while (qhead < qtail) {
      struct msg *m;

      if (seed0) {
        unsigned int r;

        seed = seed * 1103515245u + 12345u;
        r = qhead + ((seed >> 16) & 0x7FFF) % (qtail - qhead);
        if (r != qhead) {
          unsigned char *x;
          unsigned char *y;
          unsigned long b;

          for (x = q + qhead * msz, y = q + r * msz, b = 0; b < msz; ++b, ++x, ++y) {
            unsigned char c;

            c = *x;
            *x = *y;
            *y = c;
          }
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
      m = Q(qhead++);
      if (verbose)
        printf("process %u: %s from %u\n", m->to,
               m->kind == BRACHA87_INITIAL ? "(initial, v)"
             : m->kind == BRACHA87_ECHO ? "(echo, v)"
             : m->kind == BRACHA87_READY ? "(ready, v)" : "beside", m->from);
      p = m->to;
      now = sweep + 1;
      /* what rode beside is kept until the (initial, v) it is validated
       * against is held, and fed as BESIDE then */
      if (m->kind == BRACHA87_BESIDE && !(bracha87Fig1State(f[p]) & BRACHA87_F1_INITIAL)) {
        besideHeld[p] = 1;
        continue;
      }
      nwork = bracha87Fig1Input(f[p], m->kind, m->from, m->value, m->annot, work);
      if (m->kind == BRACHA87_INITIAL && besideHeld[p]) {
        besideHeld[p] = 0;
        nwork += bracha87Fig1Input(f[p], BRACHA87_BESIDE, m->from, 0, 0, work + nwork);
      }
      site = 1;
      goto run;
arrivalDone:
      ;
    }
    qhead = qtail = 0;

    /* sweep */
    ++sweep;
    for (i = 0; i < n; ++i) {
      if (abandoned[i])
        continue;
      p = i;
      now = sweep;
      nwork = bracha87Fig1Input(f[p], BRACHA87_SWEEP, 0, 0, 0, work);
      site = 2;
      goto run;
sweepDone:
      ;
    }
    for (i = 0; i < n; ++i)
      if (!abandoned[i])
        break;
    if (i == n)
      break;
    if (sweep >= 1000 * abandon) {
      /* a guard on this loop, carrying no evidence of anything */
      fprintf(stderr, "harness guard: %u sweeps and not every process abandoned\n", sweep);
      goto done;
    }
  }

  printf("n=%u t=%u initiator=%u seed=%u loss=%u%% abandon=%u\n",
         n, t, initiator, seed0, loss, abandon);
  k = 0;
  for (i = 0; i < n; ++i) {
    cv = bracha87Fig1Value(f[i], BRACHA87_ACCEPT);
    printf("seat %u: holds ", i);
    if (cv) {
      printf("\"");
      for (j = 0; j < vLen; ++j)
        putchar(cv[j] >= 0x20 && cv[j] < 0x7F ? cv[j] : '.');
      printf("\"");
    } else
      printf("nothing");
    if (held[i])
      printf(", held it and the byte from the initiator at sweep %u", held[i]);
    else {
      printf(", never held the byte");
      ++k;
    }
    printf(", accepted at sweep %u, last re-send of any kind at sweep %u, abandoned at sweep %u\n",
           accepted[i], lastResend[i], abandoned[i]);
    if (!cv || memcmp(cv, value, vLen))
      rc = 2;
  }
  printf("%u of %u seats never held the byte; the initiator accepted at sweep %u\n",
         k, n, accepted[initiator]);
  printf("%u sweeps, %u deliveries lost\n", sweep, dropped);
  if (rc == 1)
    rc = 0;
done:
  if (f)
    for (i = 0; i < n; ++i)
      free(f[i]);
  free(f);
  free(q);
  free(held);
  free(accepted);
  free(abandoned);
  free(lastResend);
  return (rc);

  /*
   * Run the actions in work[] at process p: record an accept or an
   * abandon, send the rest to every process it names -- self by
   * hand-back, whose own actions join work[] -- and return to site.
   */
run:
  while (nwork) {
    a = work[--nwork];
    if (a == BRACHA87_ACCEPT) {
      accepted[p] = now;
      if (verbose)
        printf("process %u: accept, sweep %u\n", p, now);
      continue;
    }
    if (a == BRACHA87_ABANDON) {
      abandoned[p] = now;
      if (verbose)
        printf("process %u: abandon, sweep %u\n", p, now);
      continue;
    }
    cv = bracha87Fig1Value(f[p], a);
    /* INITIAL to the processes not heard from; a READY re-sent on a
     * sweep to the processes still owed it */
    resend = a == BRACHA87_READY_ALL && site == 2;
    if (site == 2)
      lastResend[p] = now;
    if (verbose)
      printf("process %u: %s %s\n", p, site == 2 ? "re-send" : "send",
             a == BRACHA87_INITIAL_ALL ? "(initial, v) and the byte beside it"
           : a == BRACHA87_ECHO_ALL ? "(echo, v)" : "(ready, v)");
    for (j = 0; j < n; ++j) {
      if (a == BRACHA87_INITIAL_ALL && bracha87Fig1Holder(f[p], j))
        continue;
      if (resend && !bracha87Fig1Recipient(f[p], j))
        continue;
      if (a == BRACHA87_ECHO_ALL && site == 2 && !bracha87Fig1EchoRecipient(f[p], j))
        continue;
      /* every echo and ready says whether this process holds the
       * initial and the message beside it; a ready says the rest */
      annot = 0;
      if (a != BRACHA87_INITIAL_ALL && HOLDS(bracha87Fig1State(f[p])))
        annot |= BRACHA87_ANN_HOLDING;
      if (a == BRACHA87_READY_ALL) {
        if (bracha87Fig1State(f[p]) & BRACHA87_F1_ACCEPTED)
          annot |= BRACHA87_ANN_ACCEPTED;
        if (BRACHA87_BIT_TST(bracha87Fig1Received(f[p]), j))
          annot |= BRACHA87_ANN_RECEIVED;
      }
      if (j == p) {
        /* the hand-back is an input like any other; who this sweep's
         * re-sends go to stands until the next sweep, so its place in
         * this loop does not matter */
        if (nwork + 2 * BRACHA87_FIG1_OUT > sizeof (work) / sizeof (work[0])) {
          fprintf(stderr, "harness guard: hand-back cascade too deep\n");
          goto done;
        }
        nout = bracha87Fig1Input(f[p],
                                 a == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                               : a == BRACHA87_ECHO_ALL ? BRACHA87_ECHO : BRACHA87_READY,
                                 p, cv, annot, out);
        for (k = 0; k < nout; ++k)
          work[nwork++] = out[k];
        if (a == BRACHA87_INITIAL_ALL) {
          nout = bracha87Fig1Input(f[p], BRACHA87_BESIDE, p, 0, 0, out);
          for (k = 0; k < nout; ++k)
            work[nwork++] = out[k];
        }
        continue;
      }
      if (qtail + 2 > qcap)
        continue;
      Q(qtail)->kind = a == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                     : a == BRACHA87_ECHO_ALL ? BRACHA87_ECHO : BRACHA87_READY;
      Q(qtail)->from = p;
      Q(qtail)->to = j;
      Q(qtail)->annot = annot;
      memcpy(Q(qtail)->value, cv, vLen);
      ++qtail;
      if (a == BRACHA87_INITIAL_ALL) {
        Q(qtail)->kind = BRACHA87_BESIDE;
        Q(qtail)->from = p;
        Q(qtail)->to = j;
        Q(qtail)->beside = j ^ 0x5A;
        ++qtail;
      }
    }
  }
  if (!held[p]) {
    k = bracha87Fig1State(f[p]);
    if ((k & BRACHA87_F1_INITIAL) && (k & BRACHA87_F1_BESIDE))
      held[p] = now;
  }
  switch (site) {
  case 0: goto step0done;
  case 1: goto arrivalDone;
  default: goto sweepDone;
  }

usage:
  fprintf(stderr,
    "usage: example_bracha87Fig1 [-v] [-s seed] [-l loss] [-o initiator]"
    " [-a abandon] n t message\n"
    "  n           seats, 1..256\n"
    "  t           faulty seats the run tolerates, n > 3t; none here is\n"
    "  message     what the initiator shares, 1..256 bytes\n");
  fprintf(stderr,
    "  -v          trace every delivery and action\n"
    "  -s seed     reorder deliveries by seed (0: in order)\n"
    "  -l loss     lose that percent of deliveries, by seed\n"
    "  -o seat     the initiator (default 0)\n"
    "  -a sweeps   abandon, sweeps from a process's Init (default 32)\n");
  return (1);
}
