/*
 * asynchronousByzantineAgreementProtocols - Bracha87 Fig 1 test
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
 * n instances over an in-memory queue that loses each delivery with the
 * given probability, at the given t, driven drain-then-sweep until
 * every instance has abandoned.  Each process hands its own messages
 * straight back to itself, off the queue.  Asserts:
 *
 *   the refusals of Init, Initiator and Input;
 *   the initiator's INITIAL re-send comes only on a sweep, and after
 *     its accept never once every process is recorded as holding the
 *     initial and the message beside it;
 *   every process accepts the initiator's value, and at the end holds
 *     the (initial, v) and the message beside it, both from p;
 *   a READY is re-sent on a sweep only while the instance names some
 *     process as still owed it;
 *   a reply comes only on an arrival, never on a sweep, an echo's only
 *     on an arrival from the initiator; the answer at the accept comes
 *     only on an arrival too, never before the accept;
 *   without loss no READY is re-sent at all, at any t: a ready and the
 *     reply it asks for cross before the first sweep, a ready that came
 *     before the accept answered at the accept, toward a slow process
 *     too, since its replies and answer come on the arrivals and not on
 *     its sweeps; nor is anything else, an INITIAL or an ECHO: every
 *     process said it holds both on the arrivals, and a sweep returns
 *     nothing but the abandon; and the run still ends by abandon;
 *   every process abandons, so the run ends by abandon and nothing else;
 *   an instance fed nothing, or only a BESIDE ahead of the initial,
 *     which is refused, returns nothing until the sweep that makes its
 *     abandon count, abandons on it all the same, and returns nothing
 *     after.
 *
 * With slow = k, process n - 1 takes one sweep in k, as a process whose
 * caller is slow to sweep does; the asserts are the same.
 *
 * usage: test_bracha87Fig1 n t loss [slow]    (loss in percent; every
 *        process is honest at any t; slow defaults to 1, every sweep)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87Fig1.h"

#define VLEN 3
#define HOLDS(st) (((st) & (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE)) == (BRACHA87_F1_INITIAL | BRACHA87_F1_BESIDE))
#define ABANDON 64

struct msg {
  unsigned char kind;
  unsigned char from;
  unsigned char to;
  unsigned char annot;      /* on an ECHO or a READY */
  unsigned char value[VLEN];
};

int
main(
  int argc
 ,char *argv[]
){
  struct bracha87Fig1 **f;
  struct bracha87Fig1 *big;  /* an n = 256 instance, for the bound checks */
  struct msg *q;
  unsigned char *accepted;  /* per process: accepted the initiator's value */
  unsigned char *abandoned;
  unsigned int *lastReady;  /* per process: the sweep of its last READY re-send */
  unsigned int lastAccept;  /* the sweep the last process accepted at */
  unsigned int readyResends;
  unsigned int sweepSends;  /* what sweeps returned to send, of any kind */
  unsigned int replies;
  unsigned int answers;     /* READY_ANSWERs */
  unsigned int queued;      /* messages put on the queue */
  unsigned int lastInitial;       /* the sweep of the last INITIAL re-send */
  const unsigned char *cv;
  int resend;               /* a READY re-sent on a sweep */
  int reply;                /* an ECHO_REPLY or READY_REPLY */
  int answer;               /* a READY_ANSWER */
  unsigned char besideHeld[256];  /* per process: a beside message kept ahead of its initial */
  unsigned long sz;
  unsigned int n;
  unsigned int t;
  unsigned int loss;
  unsigned int slow;        /* process n - 1 sweeps one sweep in slow */
  unsigned int seed;
  unsigned int qcap;
  unsigned int qhead;
  unsigned int qtail;
  unsigned int sweeps;
  unsigned int dropped;
  unsigned int p;
  unsigned int site;
  unsigned int i;
  unsigned int j;
  unsigned int k;
  unsigned int nout;
  unsigned int nwork;
  unsigned int a;
  unsigned int to;          /* who a reply goes to: the arrival's sender */
  unsigned char annot;
  unsigned char out[BRACHA87_FIG1_OUT];
  unsigned char work[64];
  unsigned char workTo[64]; /* per action: the sender of the arrival that returned it */
  unsigned char v[VLEN];
  int rc;

  if (argc != 4 && argc != 5) {
    fprintf(stderr, "usage: %s n t loss [slow]\n", argv[0]);
    return (1);
  }
  n = atoi(argv[1]);
  t = atoi(argv[2]);
  loss = atoi(argv[3]);
  slow = argc == 5 ? atoi(argv[4]) : 1;
  if (n < 1 || n > 256 || t > 85 || slow < 1 || slow > 64) {
    fprintf(stderr, "n is 1..256, t is 0..85, slow is 1..64\n");
    return (1);
  }
  seed = 12345 + n * 7 + loss;
  rc = 1;
  f = 0;
  big = 0;
  q = 0;
  accepted = abandoned = 0;
  lastReady = 0;
  lastAccept = 0;
  readyResends = 0;
  sweepSends = 0;
  replies = 0;
  answers = 0;
  queued = 0;
  lastInitial = 0;

  /* refusals */
  /* four values, K echo and K ready values and their counts, four bitmaps */
  if (bracha87Fig1Sz(255, 255, 86) < (4U + 2U * 86) * 256 + 2U * 86 * 2 + 4U * 32
   || bracha87Fig1Sz(255, 255, 0)) {
    printf("Sz too small at n=256, vLen=256\n");
    goto done;
  }
  sz = bracha87Fig1Sz(n - 1, VLEN - 1, t + 1);
  if (!(big = calloc(1, bracha87Fig1Sz(255, VLEN - 1, 86)))
   || !(f = calloc(n, sizeof (*f)))
   || !(accepted = calloc(n, 1)) || !(abandoned = calloc(n, 1))
   || !(lastReady = calloc(n, sizeof (*lastReady)))) {
    printf("allocation failed\n");
    goto done;
  }
  memset(besideHeld, 0, sizeof (besideHeld));
  for (i = 0; i < n; ++i)
    if (!(f[i] = calloc(1, sz))) {
      printf("allocation failed\n");
      goto done;
    }
  if (!bracha87Fig1Init(0, n - 1, t, VLEN - 1, t + 1, 0, 0, ABANDON, 1)
   || !bracha87Fig1Init(f[0], 2, 1, VLEN - 1, 2, 0, 0, ABANDON, 1)
   || !bracha87Fig1Init(big, 255, 86, VLEN - 1, 86, 0, 0, ABANDON, 1)
   || !bracha87Fig1Init(big, 255, 85, VLEN - 1, 0, 0, 0, ABANDON, 1)
   || !bracha87Fig1Init(big, 255, 85, VLEN - 1, 87, 0, 0, ABANDON, 1)
   || bracha87Fig1Init(big, 255, 85, VLEN - 1, 86, 0, 0, ABANDON, 1)
   || (n < 256 && !bracha87Fig1Init(f[0], n - 1, t, VLEN - 1, t + 1, n, 0, ABANDON, 1))
   || (n < 256 && !bracha87Fig1Init(f[0], n - 1, t, VLEN - 1, t + 1, 0, n, ABANDON, 1))) {
    printf("Init accepted a bad argument\n");
    goto done;
  }
  for (i = 0; i < n; ++i)
    if (bracha87Fig1Init(f[i], n - 1, t, VLEN - 1, t + 1, 0, i, ABANDON, 1)) {
      printf("Init refused process %u\n", i);
      goto done;
    }
  v[0] = 'a';
  v[1] = 'b';
  v[2] = 'c';
  if (!bracha87Fig1Initiator(f[0], 0)
   || (n > 1 && !bracha87Fig1Initiator(f[1], v))
   || bracha87Fig1Initiator(f[0], v)
   || !bracha87Fig1Initiator(f[0], v)) {
    printf("Initiator refusals wrong\n");
    goto done;
  }
  if (bracha87Fig1Input(0, BRACHA87_ECHO, 0, v, 0, out)
   || bracha87Fig1Input(f[0], BRACHA87_ECHO, 0, v, 0, 0)
   || bracha87Fig1Input(f[0], BRACHA87_SWEEP + 1, 0, v, 0, out)
   || (n < 256 && bracha87Fig1Input(f[0], BRACHA87_ECHO, n, v, 0, out))
   || bracha87Fig1Input(f[0], BRACHA87_ECHO, 0, 0, 0, out)
   || (n > 1 && bracha87Fig1Input(f[0], BRACHA87_INITIAL, 1, v, 0, out))
   || (n > 1 && bracha87Fig1Input(f[0], BRACHA87_BESIDE, 1, 0, 0, out))) {
    printf("Input accepted a bad argument\n");
    goto done;
  }
  if (bracha87Fig1Value(0, BRACHA87_ACCEPT)
   || bracha87Fig1Value(f[0], BRACHA87_ACCEPT)
   || !bracha87Fig1Value(f[0], BRACHA87_INITIAL_ALL)
   || bracha87Fig1Holder(0, 0) || bracha87Fig1Recipient(0, 0) || bracha87Fig1Received(0)
   || bracha87Fig1ReadySender(0, 0)
   || (n < 256 && (bracha87Fig1Holder(f[0], n) || bracha87Fig1Recipient(f[0], n)
    || bracha87Fig1ReadySender(f[0], n)))
   || bracha87Fig1State(0) || bracha87Fig1Sweeps(0)) {
    printf("accessor refusals wrong\n");
    goto done;
  }

  /* an empty-handed instance abandons on schedule: nothing in hand,
   * then a BESIDE ahead of the initial, which is refused */
  for (j = 0; j < 2; ++j) {
    if (bracha87Fig1Init(big, 2, 0, VLEN - 1, 1, 0, 1, ABANDON, 1)) {
      printf("Init refused the empty instance\n");
      goto done;
    }
    if (j && bracha87Fig1Input(big, BRACHA87_BESIDE, 0, 0, 0, out)) {
      printf("BESIDE alone fired an action\n");
      goto done;
    }
    for (i = 0; i + 1 < ABANDON; ++i)
      if (bracha87Fig1Input(big, BRACHA87_SWEEP, 0, 0, 0, out)) {
        printf("empty instance fired an action on sweep %u\n", i + 1);
        goto done;
      }
    if (bracha87Fig1Input(big, BRACHA87_SWEEP, 0, 0, 0, out) != 1 || out[0] != BRACHA87_ABANDON
     || !(bracha87Fig1State(big) & BRACHA87_F1_ABANDONED)) {
      printf("empty instance did not abandon on sweep %u\n", ABANDON);
      goto done;
    }
    if (bracha87Fig1Input(big, BRACHA87_SWEEP, 0, 0, 0, out)) {
      printf("empty instance fired after abandon\n");
      goto done;
    }
  }

  /* one sweep's traffic at most: n re-sends of three kinds to n, plus
   * the beside message, plus the cascade an arrival can start */
  qcap = 8 * n * n + 8;
  if (!(q = calloc(qcap, sizeof (*q)))) {
    printf("allocation failed\n");
    goto done;
  }
  qhead = qtail = 0;
  sweeps = 0;
  dropped = 0;

  /* step 0: the initiator sends (initial, v) and its message beside it,
   * self by hand-back */
  p = 0;
  nwork = 0;
  for (j = 0; j < n; ++j) {
    if (j == p) {
      nout = bracha87Fig1Input(f[p], BRACHA87_INITIAL, p, v, 0, out);
      for (k = 0; k < nout; ++k) {
        workTo[nwork] = p;
        work[nwork++] = out[k];
      }
      nout = bracha87Fig1Input(f[p], BRACHA87_BESIDE, p, 0, 0, out);
      for (k = 0; k < nout; ++k) {
        workTo[nwork] = p;
        work[nwork++] = out[k];
      }
      continue;
    }
    q[qtail].kind = BRACHA87_INITIAL;
    q[qtail].from = p;
    q[qtail].to = j;
    memcpy(q[qtail].value, v, VLEN);
    ++qtail;
    q[qtail].kind = BRACHA87_BESIDE;
    q[qtail].from = p;
    q[qtail].to = j;
    q[qtail].value[0] = j ^ 0x5A;
    ++qtail;
  }
  site = 0;
  goto run;
step0done:

  for (;;) {
    while (qhead < qtail) {
      struct msg *m;

      ++queued;
      m = &q[qhead++];
      seed = seed * 1103515245u + 12345u;
      if (((seed >> 16) & 0x7FFF) % 100 < loss) {
        ++dropped;
        continue;
      }
      if (m->to == m->from) {
        printf("a message to self was on the queue\n");
        goto done;
      }
      if (m->kind == BRACHA87_BESIDE && m->value[0] != (m->to ^ 0x5A)) {
        printf("beside message to %u carries the wrong byte\n", m->to);
        goto done;
      }
      p = m->to;
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
      memset(workTo, m->from, nwork);
      site = 1;
      goto run;
arrivalDone:
      ;
    }
    qhead = qtail = 0;

    ++sweeps;
    for (i = 0; i < n; ++i) {
      if (abandoned[i])
        continue;
      if (i == n - 1 && sweeps % slow)
        continue;
      p = i;
      nwork = bracha87Fig1Input(f[p], BRACHA87_SWEEP, 0, 0, 0, work);
      for (k = 0; k < nwork; ++k)
        if (work[k] == BRACHA87_ECHO_REPLY || work[k] == BRACHA87_READY_REPLY
         || work[k] == BRACHA87_READY_ANSWER) {
          printf("a sweep returned a reply at %u\n", p);
          goto done;
        }
      memset(workTo, p, nwork);
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
    /* a harness guard on the loop, not a protocol quantity */
    if (sweeps > 64 * ABANDON) {
      printf("harness guard: %u sweeps without every process abandoning\n", sweeps);
      goto done;
    }
  }

  for (i = 0; i < n; ++i)
    if (!accepted[i]) {
      printf("process %u did not accept\n", i);
      goto done;
    }
  if (loss && !dropped) {
    printf("loss given and nothing dropped\n");
    goto done;
  }
  /* without loss no READY is re-sent: every process is recorded by
   * every other on the arrivals, before the first sweep */
  if (!loss && readyResends) {
    printf("%u READY re-sends without loss\n", readyResends);
    goto done;
  }
  /* nor anything else: without loss a sweep returns the abandon alone */
  if (!loss && sweepSends) {
    printf("%u re-sends without loss\n", sweepSends);
    goto done;
  }
  for (k = 0, i = 0; i < n; ++i)
    if (lastReady[i] > k)
      k = lastReady[i];
  /* every process holds p's (initial, v) and the message beside it */
  for (i = 0; i < n; ++i)
    if (!HOLDS(bracha87Fig1State(f[i]))) {
      printf("process %u accepted and never held the initial and the message beside it\n", i);
      goto done;
    }
  printf("n=%u t=%u loss=%u%% slow=%u: all accepted, all abandoned, %u sweeps, %u queued, %u dropped, %u READY re-sends, the last at sweep %u after the last accept at %u, %u replies, %u answers, the last INITIAL re-send at sweep %u\n",
         n, t, loss, slow, sweeps, queued, dropped, readyResends, k, lastAccept, replies, answers, lastInitial);
  rc = 0;
done:
  if (f)
    for (i = 0; i < n; ++i)
      free(f[i]);
  free(f);
  free(big);
  free(q);
  free(accepted);
  free(abandoned);
  free(lastReady);
  return (rc);

  /* run the actions in work[] at p, self by hand-back, and return to site */
run:
  while (nwork) {
    a = work[--nwork];
    to = workTo[nwork];
    reply = a == BRACHA87_ECHO_REPLY || a == BRACHA87_READY_REPLY;
    answer = a == BRACHA87_READY_ANSWER;
    if (a == BRACHA87_ACCEPT) {
      if (!(cv = bracha87Fig1Value(f[p], BRACHA87_ACCEPT))) {
        printf("ACCEPT without a value at %u\n", p);
        goto done;
      }
      if (memcmp(cv, v, VLEN)) {
        printf("process %u accepted another value\n", p);
        goto done;
      }
      accepted[p] = 1;
      lastAccept = sweeps;
      continue;
    }
    if (a == BRACHA87_ABANDON) {
      if (site != 2) {
        printf("abandon on an arrival at %u\n", p);
        goto done;
      }
      abandoned[p] = 1;
      continue;
    }
    if (site == 2)
      ++sweepSends;
    if (a == BRACHA87_INITIAL_ALL && site != 2) {
      printf("(initial, v) re-send on an arrival at %u\n", p);
      goto done;
    }
    if (a == BRACHA87_INITIAL_ALL)
      lastInitial = sweeps;
    if (a == BRACHA87_INITIAL_ALL && accepted[p]) {
      for (j = 0; j < n; ++j)
        if (!bracha87Fig1Holder(f[p], j))
          break;
      if (j == n) {
        printf("(initial, v) re-send with every process recorded as holding it at %u\n", p);
        goto done;
      }
    }
    if (a != BRACHA87_INITIAL_ALL && a != BRACHA87_ECHO_ALL && a != BRACHA87_READY_ALL && !reply && !answer) {
      printf("unexpected action %u at %u\n", a, p);
      goto done;
    }
    if (reply) {
      if (a == BRACHA87_ECHO_REPLY && to != 0) {
        printf("an echo's reply to other than the initiator at %u\n", p);
        goto done;
      }
      ++replies;
    }
    if (answer) {
      if (!(bracha87Fig1State(f[p]) & BRACHA87_F1_ACCEPTED)) {
        printf("an answer before the accept at %u\n", p);
        goto done;
      }
      ++answers;
    }
    if (!(cv = bracha87Fig1Value(f[p], a))) {
      printf("action without a value at %u\n", p);
      goto done;
    }
    /* INITIAL to the processes not recorded as holding it; a READY
     * re-sent on a sweep to the processes still owed it, which must be
     * some */
    resend = a == BRACHA87_READY_ALL && site == 2;
    if (resend) {
      ++readyResends;
      lastReady[p] = sweeps;
      for (j = 0; j < n && !bracha87Fig1Recipient(f[p], j); ++j)
        ;
      if (j == n) {
        printf("READY re-sent to no one at %u\n", p);
        goto done;
      }
    }
    for (j = 0; j < n; ++j) {
      if (a == BRACHA87_INITIAL_ALL && bracha87Fig1Holder(f[p], j))
        continue;
      if (resend && !bracha87Fig1Recipient(f[p], j))
        continue;
      if (a == BRACHA87_ECHO_ALL && site == 2 && !bracha87Fig1EchoRecipient(f[p], j))
        continue;
      /* a reply to the sender of the arrival that returned it alone */
      if (reply && j != to)
        continue;
      /* the answer to each process whose ready is held */
      if (answer && !bracha87Fig1ReadySender(f[p], j))
        continue;
      /* every echo and ready says whether this process holds the
       * initial and the message beside it; a ready says the rest */
      annot = 0;
      if (a != BRACHA87_INITIAL_ALL && HOLDS(bracha87Fig1State(f[p])))
        annot |= BRACHA87_ANN_HOLDING;
      if (a == BRACHA87_READY_ALL || a == BRACHA87_READY_REPLY || answer) {
        if (bracha87Fig1State(f[p]) & BRACHA87_F1_ACCEPTED)
          annot |= BRACHA87_ANN_ACCEPTED;
        if (BRACHA87_BIT_TST(bracha87Fig1Received(f[p]), j))
          annot |= BRACHA87_ANN_RECEIVED;
      }
      if (j == p) {
        /* the hand-back is an input like any other; it moves only what
         * the instance records of self, so its place in this loop does
         * not matter.  What it returns goes back to self. */
        if (nwork + 2 * BRACHA87_FIG1_OUT > sizeof (work) / sizeof (work[0])) {
          printf("hand-back cascade too deep at %u\n", p);
          goto done;
        }
        nout = bracha87Fig1Input(f[p],
                                 a == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                               : a == BRACHA87_ECHO_ALL || a == BRACHA87_ECHO_REPLY ? BRACHA87_ECHO : BRACHA87_READY,
                                 p, cv, annot, out);
        for (k = 0; k < nout; ++k) {
          workTo[nwork] = p;
          work[nwork++] = out[k];
        }
        if (a == BRACHA87_INITIAL_ALL) {
          nout = bracha87Fig1Input(f[p], BRACHA87_BESIDE, p, 0, 0, out);
          for (k = 0; k < nout; ++k) {
            workTo[nwork] = p;
            work[nwork++] = out[k];
          }
        }
        continue;
      }
      if (qtail + 2 > qcap) {
        printf("queue overflow\n");
        goto done;
      }
      q[qtail].kind = a == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                    : a == BRACHA87_ECHO_ALL || a == BRACHA87_ECHO_REPLY ? BRACHA87_ECHO : BRACHA87_READY;
      q[qtail].from = p;
      q[qtail].to = j;
      q[qtail].annot = annot;
      memcpy(q[qtail].value, cv, VLEN);
      ++qtail;
      if (a == BRACHA87_INITIAL_ALL) {
        q[qtail].kind = BRACHA87_BESIDE;
        q[qtail].from = p;
        q[qtail].to = j;
        q[qtail].value[0] = j ^ 0x5A;
        ++qtail;
      }
    }
  }
  switch (site) {
  case 0: goto step0done;
  case 1: goto arrivalDone;
  default: goto sweepDone;
  }
}
