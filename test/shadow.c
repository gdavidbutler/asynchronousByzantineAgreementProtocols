/*
 * asynchronousByzantineAgreementProtocols - Bracha87 Fig 1 shadow test
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
 * One instance, from one process's seat, fed whatever an adversary can
 * feed in whatever order: INITIAL from anyone with any of three values,
 * ECHO and READY from anyone with any value, BESIDE, SWEEP, duplicates,
 * and refused arguments.  A shadow records what was fed -- the first
 * INITIAL from p, each sender's first echo and first ready, the steps
 * taken, the sweeps -- counts echoes and readies per value as the
 * instance keeps them, the first K distinct values of a kind and no
 * other (K is the seed's, 1..t + 1, so with three values in play a
 * value is often not kept), and every return is checked against the
 * rows of bracha87Fig1.dtc read as statements about that record:
 *
 *   a step taken is sound for the value it carries, and no step that
 *     the rows say is due is left untaken (the paper's rows, and the
 *     caller's wait for the BESIDE at step 1's initial enabling under
 *     the seed's beside flag; the other two enablings, steps 2 and 3,
 *     and the flag clear, the figure as printed);
 *   a BESIDE is refused with the flag clear, and with it set ahead of
 *     p's (initial, v);
 *   abandon comes once, on a sweep, at the abandon count;
 *   a re-send comes only on a sweep, only of a sent message, only while
 *     live, INITIAL only from the initiator while some process is not
 *     recorded as holding it,
 *     ECHO until this process accepts and, past that, once to the
 *     initiator for each of its INITIALs or BESIDEs that arrived while
 *     this process held both, READY only while some
 *     process is owed it -- not recorded as having accepted, or armed
 *     -- and then to exactly those, the arms served cleared; never
 *     beside its own step;
 *   an ECHO or READY carrying HOLDING records its sender as holding
 *     the initial, a READY carrying ACCEPTED as having accepted, on a
 *     duplicate too, and nothing else does; one arriving without
 *     RECEIVED arms its sender once this process has accepted, the
 *     READY that fires the accept included, and a marked one never;
 *     an annotation on any other kind records nothing;
 *   the actions are returned in the documented order, at most three;
 *   State, Holder, Received, Sweeps and Value say what the shadow
 *     says, a value
 *     is written once and its pointer stays put;
 *   a refused argument returns 0 and changes nothing, and so does any
 *     input after the seat has abandoned.
 *
 * It proves the implementation of the figure, not the figure.
 *
 * usage: test_shadow seeds
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bracha87Fig1.h"

/* the caller's something for p's (initial, v) is held: under the flag
 * the BESIDE, with it clear from the start */
#define HOLDI (!besideFlag || beside)

#define VALUES 3   /* distinct values in play, the initiator's is 0 */

int
main(
  int argc
 ,char *argv[]
){
  struct bracha87Fig1 *f;
  unsigned char *echoVal;   /* per sender: value index + 1, 0 = none */
  unsigned char *readyVal;
  unsigned char *acc;       /* per sender: recorded as having accepted */
  unsigned char *hld;       /* per sender: recorded as holding the initial */
  unsigned char *armed;     /* per sender: armed, until the re-send that serves it */
  unsigned char *served;    /* per sender: served by the last sweep's re-send, until the next sweep */
  unsigned long seeds;
  unsigned long seed;
  unsigned long s;
  unsigned long calls;
  unsigned long refused;
  unsigned long steps[4];
  unsigned long resends[3];
  unsigned long arms;
  unsigned long quiet;      /* sweeps with a ready sent and none owed */
  unsigned long sweeps;     /* shadow count */
  unsigned long abandon;
  unsigned long pre;        /* sweeps before this call */
  unsigned int n;
  unsigned int t;
  unsigned int vLen;
  unsigned int initiator;
  unsigned int self;
  unsigned int u;           /* shadow senders */
  unsigned int i;
  unsigned int k;
  unsigned int ev;
  unsigned int kind;
  unsigned int from;
  unsigned int val;
  unsigned int ann;         /* the annotations fed */
  unsigned int owed;        /* some process owed the ready, before the call */
  unsigned int nout;
  unsigned int ec[VALUES];
  unsigned int rd[VALUES];
  unsigned int eKept[VALUES];  /* the value is among the K echo values kept */
  unsigned int rKept[VALUES];
  unsigned int eUsed;          /* echo values kept so far */
  unsigned int rUsed;
  unsigned int kvals;          /* K */
  unsigned int initVal;     /* value index + 1 held as p's INITIAL, 0 = none */
  unsigned int isInit;      /* this process is the initiator, holding value 0 */
  unsigned int beside;
  unsigned int besideFlag;  /* the seed's: something rides beside */
  unsigned int echoed;      /* value index + 1 of the echo, 0 = not taken */
  unsigned int readied;
  unsigned int accepted;
  unsigned int abandoned;
  unsigned int wasEchoed, wasReadied, wasAbandoned;
  unsigned int armE;        /* one echo owed to the initiator */
  unsigned int servedE;     /* served by the last sweep's echo re-send, until the next sweep */
  unsigned int wasArmE;
  unsigned int tookE, tookR, tookA, tookAb;
  unsigned int sawI, sawRE, sawRR;
  unsigned int exp;
  unsigned int live;
  unsigned int st;
  const unsigned char *valPtr[5];   /* by act */
  const unsigned char *cv;
  unsigned char out[BRACHA87_FIG1_OUT + 1];
  unsigned char value[VALUES][256];
  unsigned char before[80];   /* State and Received before a refused input */
  unsigned char qBefore[256]; /* Holder and Recipient, per process, before it */
  int rc;

  if (argc != 2) {
    fprintf(stderr, "usage: %s seeds\n", argv[0]);
    return (1);
  }
  seeds = atol(argv[1]);
  rc = 1;
  f = 0;
  echoVal = readyVal = 0;
  calls = refused = 0;
  memset(steps, 0, sizeof (steps));
  memset(resends, 0, sizeof (resends));
  arms = quiet = 0;
  acc = armed = served = hld = 0;
  if (!(echoVal = calloc(256, 1)) || !(readyVal = calloc(256, 1))
   || !(acc = calloc(256, 1)) || !(armed = calloc(256, 1)) || !(served = calloc(256, 1))
   || !(hld = calloc(256, 1))) {
    printf("allocation failed\n");
    goto done;
  }
#define RND (seed = seed * 6364136223846793005UL + 1442695040888963407UL, (unsigned int)(seed >> 33))

  for (s = 1; s <= seeds; ++s) {
    seed = s * 2654435761UL + 12345;
    /* the run's shape */
    n = RND % 8 + 1;
    if (RND % 50 == 0)
      n = 256;
    else if (RND % 10 == 0)
      n = 9 + RND % 247;    /* a partial last byte with more than one byte */
    t = n > 3 ? RND % ((n - 1) / 3 + 1) : 0;
    vLen = RND % 3 + 1;
    if (RND % 20 == 0)
      vLen = 256;
    initiator = RND % n;
    self = RND % 4 ? RND % n : initiator;
    abandon = RND % 5 + 1;
    abandon += RND % 20;
    if (RND % 16 == 0)
      abandon = 0;          /* none: the instance never abandons */
    kvals = RND % (t + 1) + 1;
    if (RND % 16 == 0)
      kvals = t + 1;
    besideFlag = RND % 4 != 0;
    for (k = 0; k < VALUES; ++k)
      for (i = 0; i < vLen; ++i)
        value[k][i] = (unsigned char)(i == (k % 2 ? 0 : vLen - 1) ? 'a' + k : 'z');
    /* allocated to its own size, so an overrun is an overrun */
    free(f);
    if (!(f = calloc(1, bracha87Fig1Sz(n - 1, vLen - 1, kvals)))) {
      printf("allocation failed\n");
      goto done;
    }
    if (bracha87Fig1Init(f, n - 1, t, vLen - 1, kvals, initiator, self, abandon, besideFlag)) {
      printf("seed %lu: Init refused n=%u t=%u\n", s, n, t);
      goto done;
    }
    memset(echoVal, 0, 256);
    memset(readyVal, 0, 256);
    memset(acc, 0, 256);
    memset(armed, 0, 256);
    memset(served, 0, 256);
    memset(hld, 0, 256);
    memset(eKept, 0, sizeof (eKept));
    memset(rKept, 0, sizeof (rKept));
    memset(ec, 0, sizeof (ec));
    memset(rd, 0, sizeof (rd));
    eUsed = rUsed = 0;
    sweeps = 0;
    u = 0;
    initVal = 0;
    isInit = 0;
    beside = echoed = readied = accepted = abandoned = 0;
    armE = 0;
    servedE = 0;
    memset(valPtr, 0, sizeof (valPtr));
    if (self == initiator && RND % 8) {
      if (bracha87Fig1Initiator(f, value[0])) {
        printf("seed %lu: Initiator refused\n", s);
        goto done;
      }
      isInit = 1;
      valPtr[BRACHA87_INITIAL_ALL] = bracha87Fig1Value(f, BRACHA87_INITIAL_ALL);
      if (!valPtr[BRACHA87_INITIAL_ALL] || memcmp(valPtr[BRACHA87_INITIAL_ALL], value[0], vLen)) {
        printf("seed %lu: INITIAL_ALL value wrong after Initiator\n", s);
        goto done;
      }
    }

    for (ev = 0; ev < 60 + 2 * n; ++ev) {
      /* choose the event */
      kind = RND % 100;
      from = RND % n;
      val = RND % VALUES;
      ann = RND % 8;            /* on any kind: read on an ECHO or a READY, ignored otherwise */
      if (RND % 4 == 0)
        from = initiator;
      if (RND % 3)
        val = 0;
      if (kind < 15)
        kind = BRACHA87_INITIAL;
      else if (kind < 40)
        kind = BRACHA87_ECHO;
      else if (kind < 65)
        kind = BRACHA87_READY;
      else if (kind < 72)
        kind = BRACHA87_BESIDE;
      else if (kind < 95)
        kind = BRACHA87_SWEEP;
      else
        kind = 5 + RND % 3;   /* refused: a kind outside the five */
      wasEchoed = echoed;
      wasReadied = readied;
      wasAbandoned = abandoned;
      wasArmE = armE;
      pre = sweeps;
      for (owed = 0, i = 0; i < n && !owed; ++i)
        if (!acc[i] || armed[i])
          owed = 1;
      memset(out, 0xEE, sizeof (out));
      ++calls;

      /* refusals: no change, 0 returned; an abandoned seat refuses all */
      i = abandoned || kind > BRACHA87_SWEEP
       || (kind != BRACHA87_SWEEP && n < 256 && RND % 25 == 0 && (from = n, 1))
       || ((kind == BRACHA87_INITIAL || kind == BRACHA87_BESIDE) && from != initiator)
       || (kind == BRACHA87_BESIDE && (!besideFlag || (!initVal && !isInit)))
       || (kind <= BRACHA87_READY && RND % 25 == 0 && (val = VALUES, 1));
      if (i) {
        unsigned char *b;

        ++refused;
        b = before;
        st = bracha87Fig1State(f);
        memcpy(b, &st, sizeof (st));
        b += sizeof (st);
        memcpy(b, bracha87Fig1Received(f), (n + 7) / 8);
        for (k = 0; k < n; ++k)
          qBefore[k] = (bracha87Fig1Holder(f, k) != 0) | (bracha87Fig1Recipient(f, k) != 0) << 1;
        sweeps = bracha87Fig1Sweeps(f);
        nout = bracha87Fig1Input(f, kind, from, val < VALUES ? value[val] : 0, ann, out);
        b = before;
        st = bracha87Fig1State(f);
        for (k = 0; k < n; ++k)
          if (qBefore[k] != ((bracha87Fig1Holder(f, k) != 0) | (bracha87Fig1Recipient(f, k) != 0) << 1))
            break;
        if (nout || k < n || memcmp(b, &st, sizeof (st))
         || memcmp(b + sizeof (st), bracha87Fig1Received(f), (n + 7) / 8)
         || bracha87Fig1Sweeps(f) != sweeps) {
          printf("seed %lu ev %u: refused input kind %u from %u changed state or returned %u\n",
                 s, ev, kind, from, nout);
          goto done;
        }
        sweeps = pre;
        continue;
      }

      /* a sweep answers for its own re-sends: what the last one served
       * is cleared at its start */
      if (kind == BRACHA87_SWEEP) {
        memset(served, 0, 256);
        servedE = 0;
      }

      /* record what is fed, as the rows record it */
      switch (kind) {
      case BRACHA87_INITIAL:
        if (!initVal)
          initVal = isInit ? 1 : val + 1;   /* the initiator keeps its own v */
        break;
      case BRACHA87_ECHO:
        if (!echoVal[from]) {
          echoVal[from] = val + 1;
          if (!readyVal[from])
            ++u;
          /* counted under its value if kept, or kept now while K allows */
          if (eKept[val])
            ++ec[val];
          else if (eUsed < kvals) {
            eKept[val] = 1;
            ec[val] = 1;
            ++eUsed;
          }
        }
        if (ann & BRACHA87_ANN_HOLDING)
          hld[from] = 1;
        break;
      case BRACHA87_READY:
        if (!readyVal[from]) {
          readyVal[from] = val + 1;
          if (!echoVal[from])
            ++u;
          if (rKept[val])
            ++rd[val];
          else if (rUsed < kvals) {
            rKept[val] = 1;
            rd[val] = 1;
            ++rUsed;
          }
        }
        if (ann & BRACHA87_ANN_ACCEPTED)
          acc[from] = 1;
        if (ann & BRACHA87_ANN_HOLDING)
          hld[from] = 1;
        break;
      case BRACHA87_BESIDE:
        beside = 1;
        break;
      default:
        break;
      }
      nout = bracha87Fig1Input(f, kind, from, kind <= BRACHA87_READY ? value[val] : 0, ann, out);
      if (kind == BRACHA87_SWEEP && sweeps < abandon)
        ++sweeps;             /* counted up to the abandon count */
      if (nout > BRACHA87_FIG1_OUT || out[BRACHA87_FIG1_OUT] != 0xEE) {
        printf("seed %lu ev %u: %u actions or overrun\n", s, ev, nout);
        goto done;
      }

      /* read the actions in order, each at most once.  A step and a
       * re-send of the same message are told apart by the flag before
       * the call: a step needs it clear, a re-send needs it set. */
      tookE = tookR = tookA = tookAb = 0;
      sawI = sawRE = sawRR = 0;
      k = 0;
      for (i = 0; i < nout; ++i) {
        unsigned int rank;

        switch (out[i]) {
        case BRACHA87_ECHO_ALL:
          if (!wasEchoed) { rank = 1; tookE = 1; ++steps[0]; }
          else { rank = 6; sawRE = 1; ++resends[1]; }
          break;
        case BRACHA87_READY_ALL:
          if (!wasReadied) { rank = 2; tookR = 1; ++steps[1]; }
          else { rank = 7; sawRR = 1; ++resends[2]; }
          break;
        case BRACHA87_ACCEPT:  rank = 3; tookA = 1; ++steps[2]; break;
        case BRACHA87_ABANDON: rank = 4; tookAb = 1; ++steps[3]; break;
        case BRACHA87_INITIAL_ALL: rank = 5; sawI = 1; ++resends[0]; break;
        default:
          printf("seed %lu ev %u: unknown action %u\n", s, ev, out[i]);
          goto done;
        }
        if (rank <= k) {
          printf("seed %lu ev %u: action order broken at %u (%u)\n", s, ev, i, out[i]);
          goto done;
        }
        k = rank;
      }

      /* the steps taken: sound for their value, applied once */
      if (tookE) {
        if (!(cv = bracha87Fig1Value(f, BRACHA87_ECHO_ALL))) {
          printf("seed %lu ev %u: echo without a value\n", s, ev);
          goto done;
        }
        for (k = 0; k < VALUES && memcmp(cv, value[k], vLen); ++k)
          ;
        /* step 1 is sound for a value one of the figure's three ways
         * enables: p's (initial, v) with the caller's something held,
         * or more than (n+t)/2 echoes or t+1 readies of it, the others'
         * word, as printed */
        if (k == VALUES
         || !((initVal == k + 1 && HOLDI) || ec[k] > (n + t) / 2 || rd[k] >= t + 1)) {
          printf("seed %lu ev %u: step 1 unsound: value %u initVal %u beside %u ec %u rd %u\n",
                 s, ev, k, initVal, beside, k < VALUES ? ec[k] : 0, k < VALUES ? rd[k] : 0);
          goto done;
        }
        echoed = k + 1;
        valPtr[BRACHA87_ECHO_ALL] = cv;
      }
      if (tookR) {
        if (!(cv = bracha87Fig1Value(f, BRACHA87_READY_ALL))) {
          printf("seed %lu ev %u: ready without a value\n", s, ev);
          goto done;
        }
        for (k = 0; k < VALUES && memcmp(cv, value[k], vLen); ++k)
          ;
        if (k == VALUES || !echoed || !(ec[k] > (n + t) / 2 || rd[k] >= t + 1)) {
          printf("seed %lu ev %u: step 2 unsound: value %u ec %u rd %u\n", s, ev, k, k < VALUES ? ec[k] : 0, k < VALUES ? rd[k] : 0);
          goto done;
        }
        readied = k + 1;
        valPtr[BRACHA87_READY_ALL] = cv;
      }
      if (tookA) {
        if (!(cv = bracha87Fig1Value(f, BRACHA87_ACCEPT))) {
          printf("seed %lu ev %u: accept without a value\n", s, ev);
          goto done;
        }
        for (k = 0; k < VALUES && memcmp(cv, value[k], vLen); ++k)
          ;
        if (k == VALUES || !readied || rd[k] < 2 * t + 1) {
          printf("seed %lu ev %u: step 3 unsound: value %u rd %u u %u pre %lu\n", s, ev, k, k < VALUES ? rd[k] : 0, u, pre);
          goto done;
        }
        accepted = k + 1;
        valPtr[BRACHA87_ACCEPT] = cv;
      }
      if (tookAb) {
        if (kind != BRACHA87_SWEEP || !abandon || pre + 1 < abandon || wasAbandoned) {
          printf("seed %lu ev %u: abandon unsound: kind %u pre %lu\n", s, ev, kind, pre);
          goto done;
        }
        abandoned = 1;
      }

      /* the steps due and not taken */
      if (!echoed)
        for (k = 0; k < VALUES; ++k)
          if ((initVal == k + 1 && HOLDI) || ec[k] > (n + t) / 2 || rd[k] >= t + 1) {
            printf("seed %lu ev %u: step 1 due on value %u, not taken\n", s, ev, k);
            goto done;
          }
      if (echoed && !readied)
        for (k = 0; k < VALUES; ++k)
          if (ec[k] > (n + t) / 2 || rd[k] >= t + 1) {
            printf("seed %lu ev %u: step 2 due on value %u, not taken\n", s, ev, k);
            goto done;
          }
      if (readied && !accepted)
        for (k = 0; k < VALUES; ++k)
          if (rd[k] >= 2 * t + 1) {
            printf("seed %lu ev %u: step 3 due on value %u, not taken\n", s, ev, k);
            goto done;
          }
      if (kind == BRACHA87_SWEEP && abandon && pre + 1 >= abandon && !wasAbandoned && !tookAb) {
        printf("seed %lu ev %u: abandon due, not taken\n", s, ev);
        goto done;
      }

      /* the arm: a READY without RECEIVED, once accepted -- read after
       * the steps, so the READY that fires the accept arms its sender */
      if (kind == BRACHA87_READY && !(ann & BRACHA87_ANN_RECEIVED) && accepted) {
        armed[from] = 1;
        ++arms;
      }
      /* the echo's arm: p's INITIAL or BESIDE at a process holding both
       * that had echoed before it came: the arrival that completes the
       * holding, or fires the echo, is not p's word that it has not
       * heard */
      if ((kind == BRACHA87_INITIAL || kind == BRACHA87_BESIDE) && initVal && (beside || !besideFlag) && wasEchoed)
        armE = 1;

      /* the re-sends: on a sweep, of a sent message, while live */
      live = !abandoned;
      for (i = 0; i < n && hld[i]; ++i)
        ;
      exp = kind == BRACHA87_SWEEP && live && isInit && i < n;
      if (sawI != exp) {
        printf("seed %lu ev %u: INITIAL re-send %s\n", s, ev, exp ? "missing" : "unexpected");
        goto done;
      }
      exp = kind == BRACHA87_SWEEP && live && wasEchoed && (!accepted || wasArmE);
      if (sawRE != exp) {
        printf("seed %lu ev %u: ECHO re-send %s\n", s, ev, exp ? "missing" : "unexpected");
        goto done;
      }
      if (sawRE) {
        /* to every process until accepted; past that to the initiator
         * alone, and the arm is served: who, is checked below with the
         * rest */
        if (wasArmE)
          servedE = 1;        /* any re-send of the echo reaches p and serves the arm */
        armE = 0;
      } else if (kind == BRACHA87_SWEEP && live && wasEchoed && !accepted) {
        printf("seed %lu ev %u: echo re-send due before accept, not seen\n", s, ev);
        goto done;
      }
      exp = kind == BRACHA87_SWEEP && live && wasReadied && owed;
      if (sawRR != exp) {
        printf("seed %lu ev %u: READY re-send %s\n", s, ev, exp ? "missing" : "unexpected");
        goto done;
      }
      if (kind == BRACHA87_SWEEP && live && wasReadied && !owed)
        ++quiet;
      if (sawRR) {
        /* to the processes owed it; the arms it serves are served, and
         * stay answered until the next sweep */
        for (i = 0; i < n; ++i) {
          served[i] = armed[i];
          armed[i] = 0;
        }
        if (n < 256 && (bracha87Fig1Recipient(f, n) || bracha87Fig1Holder(f, n))) {
          printf("seed %lu ev %u: a Recipient or Holder past n\n", s, ev);
          goto done;
        }
      }

      /* who the last sweep's re-sends went to, answered until the next
       * sweep whatever arrives between, the hand-back to self included */
      for (i = 0; i < n; ++i) {
        if ((bracha87Fig1Recipient(f, i) != 0) != (!acc[i] || served[i])) {
          printf("seed %lu ev %u: Recipient %u disagrees (acc %u served %u)\n", s, ev, i, acc[i], served[i]);
          goto done;
        }
        if ((bracha87Fig1EchoRecipient(f, i) != 0) != (!accepted || ((armE || servedE) && i == initiator))) {
          printf("seed %lu ev %u: EchoRecipient %u disagrees (accepted %u armE %u servedE %u)\n", s, ev, i, accepted, armE, servedE);
          goto done;
        }
      }

      /* what the instance says about itself */
      st = bracha87Fig1State(f);
      if (((st & BRACHA87_F1_INITIATOR) != 0) != isInit
       || ((st & BRACHA87_F1_INITIAL) != 0) != (initVal != 0)
       || ((st & BRACHA87_F1_BESIDE) != 0) != (besideFlag ? beside : 1)
       || ((st & BRACHA87_F1_ECHOED) != 0) != (echoed != 0)
       || ((st & BRACHA87_F1_RDSENT) != 0) != (readied != 0)
       || ((st & BRACHA87_F1_ACCEPTED) != 0) != (accepted != 0)
       || ((st & BRACHA87_F1_ABANDONED) != 0) != abandoned) {
        printf("seed %lu ev %u: State %02x disagrees with the record\n", s, ev, st);
        goto done;
      }
      if (bracha87Fig1Sweeps(f) != sweeps) {
        printf("seed %lu ev %u: Sweeps %u, record %lu\n", s, ev, bracha87Fig1Sweeps(f), sweeps);
        goto done;
      }
      for (i = 0; i < n; ++i)
        if ((bracha87Fig1Holder(f, i) != 0) != hld[i]) {
          printf("seed %lu ev %u: Holder %u disagrees\n", s, ev, i);
          goto done;
        }
      cv = bracha87Fig1Received(f);
      for (i = 0; i < n; ++i)
        if ((BRACHA87_BIT_TST(cv, i) != 0) != acc[i]) {
          printf("seed %lu ev %u: Received bit %u disagrees\n", s, ev, i);
          goto done;
        }
      for (k = 0; k <= BRACHA87_INITIAL_ALL; ++k) {
        unsigned int want;

        if (k == BRACHA87_ABANDON)
          continue;
        want = k == BRACHA87_INITIAL_ALL ? (isInit ? 1 : initVal)
             : k == BRACHA87_ECHO_ALL ? echoed
             : k == BRACHA87_READY_ALL ? readied : accepted;
        cv = bracha87Fig1Value(f, k);
        if (k == BRACHA87_INITIAL_ALL && cv && !valPtr[k])
          valPtr[k] = cv;     /* held from the first (initial, v), and it stays put */
        if ((cv != 0) != (want != 0) || (cv && (cv != valPtr[k]
         || memcmp(cv, value[want - 1], vLen)))) {
          printf("seed %lu ev %u: Value(%u) disagrees with the record\n", s, ev, k);
          goto done;
        }
      }
      if (bracha87Fig1Value(f, BRACHA87_ABANDON) || bracha87Fig1Value(f, 200)) {
        printf("seed %lu ev %u: Value of a non-act\n", s, ev);
        goto done;
      }
    }
  }
  printf("shadow: %lu seeds, %lu calls, %lu refused, steps %lu %lu %lu abandons %lu, re-sends %lu %lu %lu, arms %lu, quiet sweeps %lu\n",
         seeds, calls, refused, steps[0], steps[1], steps[2], steps[3],
         resends[0], resends[1], resends[2], arms, quiet);
  rc = 0;
done:
  free(f);
  free(echoVal);
  free(readyVal);
  free(acc);
  free(armed);
  free(served);
  free(hld);
  return (rc);
}
