/*
 * measure_drift.c -- does a release discipline hold the spread?
 *
 * The hold (bkr94acsInit's hold, released by bkr94acsBaReveal)
 * protects a local coin only while no correct step-3 turn of the
 * phase is still pending when any coin is revealed.
 * A release discipline must therefore bound the SPREAD -- the interval
 * between the first and last correct step-3 turn of a phase -- and a
 * bound on one hop does not, on its own, bound it (README, Delivery
 * time).  This program is a timing model of that claim and of the two
 * candidate disciplines, with nothing of the protocol in it: times only.
 *
 * The model.  n processes, t faulty, m = n-t correct, one hop the
 * unit, a hop's delay uniform on [0, 1] -- or, in the "tail" rows, one
 * hop in a hundred stretched uniformly to [1, 5], since the library's
 * own premise is delay bounded by nothing.  A correct broadcast is Fig
 * 1: INITIAL to all, echo on it, ready on (n+t)/2+1 echoes, accept on
 * 2t+1 readies -- three hops, and at the echo and ready stages the
 * receiver waits for an ORDER STATISTIC of the correct arrivals.
 * Helped (the faulty echo and ready it too), (n+t)/2+1-t correct
 * echoes and t+1 correct readies suffice; unhelped, (n+t)/2+1 correct
 * echoes and 2t+1 correct readies are needed, each capped at m.  So a
 * broadcast sent at s validates at a receiver at s + the INITIAL's
 * hop to the echoers (the echo-count-th arrival) + the echoes' hop
 * (the same order statistic) + the readies' hop (the ready-count-th);
 * the program prints both means.  This is a chosen model of Fig 1's
 * latency, not a measurement of it: what it keeps is the asymmetry
 * the thresholds impose, which is the lever.  The faulty deliver their
 * own round messages at once to the processes they help ("leaders")
 * and never to the L they do not ("laggards", L <= t).  A round is
 * ENABLED at r when n-t messages have validated: at a leader the
 * (n-2t)th correct validation (the faulty's t are in), at a laggard the
 * m-th.  A process's own message validates at its send time.
 *
 * Two disciplines, each with a wait W and a hold H in hops, swept:
 *   enable     fire the turn at enable + W; send the phase-opening
 *              INITIAL at the step-3 fire + H.
 *   metronome  fire the turn at max(enable, previous fire + W); send the
 *              phase-opening INITIAL at the step-3 fire + H.
 * What the sweep is for: a laggard's phase-opening round costs the
 * hold H plus an unhelped validation (the three-maxima chain), so a
 * metronome whose period W is shorter than that path lets the laggards
 * fall behind by the difference every phase, and the entry spread S0
 * is never recovered by either rule, so H must cover it.
 * Round 0 is sent at the process's entry time, uniform on [0, S0]: the
 * entry spread the fanout leaves (README, Delivery time).
 *
 * Measured per phase, over trials: the spread of the correct step-3
 * fires, the laggards' mean lag behind the leaders' mean fire, and the
 * EXPOSURE -- the fraction of correct processes whose step-3 turn fires
 * after some other correct process has revealed its coin, which is the
 * window an adversary reading coins needs.  Zero exposure is what a
 * discipline must show before the hold buys anything.
 *
 * Usage: measure_drift [trials]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* arc4random_buf is not C89; -std=c89 hides its stdlib.h prototype */
void arc4random_buf(void *, size_t);

#define MAX_N   64
#define PHASES  8
#define STEPS   (PHASES * 3)

static unsigned char Rbuf[8192];
static unsigned long Rpos = sizeof (Rbuf);
static unsigned int Tail;          /* 1: one hop in 100 stretched to [1, 5] */

static double
rUnit(
  void
){
  unsigned int v;

  if (Rpos + 4 > sizeof (Rbuf)) {
    arc4random_buf(Rbuf, sizeof (Rbuf));
    Rpos = 0;
  }
  v = Rbuf[Rpos] | Rbuf[Rpos + 1] << 8 | Rbuf[Rpos + 2] << 16 | (unsigned int)Rbuf[Rpos + 3] << 24;
  Rpos += 4;
  return ((double)v / 4294967296.0);
}

/* one hop's delay */
static double
hop(
  void
){
  double u;

  u = rUnit();
  if (Tail && u < 0.01)
    return (1.0 + 4.0 * rUnit());
  return (u);
}

static int
cmpDouble(
  const void *a
 ,const void *b
){
  double x = *(const double *)a;
  double y = *(const double *)b;

  return (x < y ? -1 : x > y ? 1 : 0);
}

/*
 * One sender's Fig 1 cascade this round, helped and unhelped: three
 * stages, the order statistics the thresholds name at each, over one
 * sorted draw of m hops per stage.
 */
static void
cascade(
  unsigned int n
 ,unsigned int t
 ,unsigned int m
 ,double *helped
 ,double *unhelped
){
  double draw[MAX_N];
  unsigned int d;
  unsigned int keH;
  unsigned int keU;
  unsigned int krH;
  unsigned int krU;

  keH = (n + t) / 2 + 1 - t;         /* correct echoes needed, helped */
  keU = (n + t) / 2 + 1;             /* and unhelped */
  krH = t + 1;                       /* correct readies needed, helped */
  krU = 2 * t + 1;                   /* and unhelped */
  if (keU > m)
    keU = m;
  if (krU > m)
    krU = m;
  *helped = *unhelped = 0.0;
  for (d = 0; d < m; ++d)
    draw[d] = hop();
  qsort(draw, m, sizeof (double), cmpDouble);
  *helped += draw[keH - 1];
  *unhelped += draw[keU - 1];
  for (d = 0; d < m; ++d)
    draw[d] = hop();
  qsort(draw, m, sizeof (double), cmpDouble);
  *helped += draw[keH - 1];
  *unhelped += draw[keU - 1];
  for (d = 0; d < m; ++d)
    draw[d] = hop();
  qsort(draw, m, sizeof (double), cmpDouble);
  *helped += draw[krH - 1];
  *unhelped += draw[krU - 1];
}

/* the k-th smallest of a[0..n) (0-based k), by selection */
static double
kth(
  const double *a
 ,unsigned int n
 ,unsigned int k
){
  double b[MAX_N];
  unsigned int i;
  unsigned int j;
  double x;

  memcpy(b, a, n * sizeof (double));
  for (i = 0; i <= k; ++i) {
    for (j = i + 1; j < n; ++j)
      if (b[j] < b[i]) {
        x = b[i];
        b[i] = b[j];
        b[j] = x;
      }
  }
  return (b[k]);
}

int
main(
  int argc
 ,char **argv
){
  static const unsigned int Cfg[6][2] = {
    { 4, 1 }, { 7, 2 }, { 10, 3 }, { 16, 4 }, { 25, 5 }, { 49, 7 }
  };
  static const char *RuleName[2] = { "enable", "metronome" };
  static const double S0s[3] = { 0.0, 1.0, 4.0 };
  static const double WH[4][2] = { { 2.0, 1.0 }, { 4.0, 1.0 }, { 8.0, 5.0 }, { 12.0, 8.0 } };
  double helped[MAX_N];      /* this round: each sender's helped validation latency */
  double unhelped[MAX_N];    /* and its unhelped one */
  double meanH;
  double meanU;
  unsigned long nLat;
  unsigned int wi;
  unsigned int tail;
  double send[MAX_N];        /* this round's send time per correct process */
  double fire[MAX_N];        /* this round's fire time per correct process */
  double val[MAX_N];         /* validation times at one receiver */
  double reveal[MAX_N];      /* the reveal each process made at the last step 3 */
  double spread[PHASES];
  double lag[PHASES];
  double expo[PHASES];
  unsigned long trials;
  unsigned long tr;
  unsigned int cfg;
  unsigned int rule;
  unsigned int si;
  unsigned int n;
  unsigned int t;
  unsigned int m;
  unsigned int L;
  unsigned int k;
  unsigned int ph;
  unsigned int r;
  unsigned int q;
  unsigned int exposed;
  double W;
  double H;
  double S0;
  double enable;
  double lo;
  double hi;
  double leadMean;
  double lagMean;

  trials = (argc > 1) ? strtoul(argv[1], 0, 10) : 2000UL;
  printf("release-discipline drift model: %lu trials per cell, %d phases, one hop = 1.0\n",
         trials, PHASES);
  printf("a broadcast validates at send + three Fig 1 hops, at the echo and ready stages the"
         " order statistic the threshold names (helped: (n+t)/2+1-t echoes, t+1 readies;"
         " unhelped: (n+t)/2+1 echoes, 2t+1 readies); L = t laggards never helped\n");
  for (tail = 0; tail < 2; ++tail) {
    Tail = tail;
    printf("mean validation latency in hops, helped / unhelped, hops %s:",
           tail ? "1 in 100 stretched to 1..5" : "bounded by 1");
    for (cfg = 0; cfg < sizeof (Cfg) / sizeof (Cfg[0]); ++cfg) {
      n = Cfg[cfg][0];
      t = Cfg[cfg][1];
      m = n - t;
      meanH = meanU = 0.0;
      for (nLat = 0; nLat < 2000; ++nLat) {
        cascade(n, t, m, &lo, &hi);
        meanH += lo;
        meanU += hi;
      }
      printf("  %u/%u %.2f/%.2f", n, t, meanH / 2000.0, meanU / 2000.0);
    }
    printf("\n");
  }
  printf("\n");

  for (tail = 0; tail < 2; ++tail)
  for (rule = 0; rule < 2; ++rule)
  for (wi = 0; wi < sizeof (WH) / sizeof (WH[0]); ++wi) {
    Tail = tail;
    W = WH[wi][0];
    H = WH[wi][1];
    printf("=== rule %s: turn at %s, reveal at step-3 fire + H; W = %.1f, H = %.1f;"
           " hops %s ===\n",
           RuleName[rule], rule ? "max(enable, previous fire + W)" : "enable + W", W, H,
           tail ? "1 in 100 stretched to 1..5" : "bounded by 1");
    printf("  %-6s %-4s | %-7s %s\n", "n/t", "S0", "phase:", "spread (hops) / laggard lag / exposure at phases 1, 4, 8");
    for (cfg = 0; cfg < sizeof (Cfg) / sizeof (Cfg[0]); ++cfg) {
      n = Cfg[cfg][0];
      t = Cfg[cfg][1];
      m = n - t;
      L = t;
      for (si = 0; si < sizeof (S0s) / sizeof (S0s[0]); ++si) {
        S0 = S0s[si];
        memset(spread, 0, sizeof (spread));
        memset(lag, 0, sizeof (lag));
        memset(expo, 0, sizeof (expo));
        for (tr = 0; tr < trials; ++tr) {
          /* entry: round 0 sent uniformly over [0, S0]; nothing revealed yet */
          for (r = 0; r < m; ++r) {
            send[r] = S0 * rUnit();
            fire[r] = send[r];
            reveal[r] = -1.0;
          }
          for (k = 0; k < STEPS; ++k) {
            ph = k / 3;
            /* each correct sender's cascade this round */
            for (q = 0; q < m; ++q)
              cascade(n, t, m, &helped[q], &unhelped[q]);
            /* each correct receiver r: validation times of the m correct
             * sends, then its enabling and fire */
            for (r = 0; r < m; ++r) {
              for (q = 0; q < m; ++q) {
                if (q == r)
                  val[q] = send[q];
                else if (r < L)
                  val[q] = send[q] + unhelped[q];     /* laggard */
                else
                  val[q] = send[q] + helped[q];       /* leader */
              }
              enable = (r < L) ? kth(val, m, m - 1) : kth(val, m, n - 2 * t - 1);
              if (rule == 0)
                fire[r] = enable + W;
              else
                fire[r] = (enable > fire[r] + W) ? enable : fire[r] + W;
            }
            if (k % 3 == 2) {
              /* step 3 fired: the spread, the lag, and the exposure
               * against the reveals of THIS phase's coins */
              lo = hi = fire[0];
              leadMean = lagMean = 0.0;
              for (r = 0; r < m; ++r) {
                if (fire[r] < lo)
                  lo = fire[r];
                if (fire[r] > hi)
                  hi = fire[r];
                if (r < L)
                  lagMean += fire[r];
                else
                  leadMean += fire[r];
                reveal[r] = fire[r] + H;
              }
              spread[ph] += hi - lo;
              lag[ph] += lagMean / (double)L - leadMean / (double)(m - L);
              exposed = 0;
              for (r = 0; r < m; ++r)
                for (q = 0; q < m; ++q)
                  if (q != r && reveal[q] < fire[r]) {
                    ++exposed;
                    break;
                  }
              expo[ph] += (double)exposed / (double)m;
              /* the next round's sends are the reveals */
              for (r = 0; r < m; ++r)
                send[r] = reveal[r];
            } else {
              for (r = 0; r < m; ++r)
                send[r] = fire[r];
            }
          }
        }
        printf("  %2u/%-3u %-4.1f |", n, t, S0);
        for (ph = 0; ph < PHASES; ph += (ph == 0) ? 3 : 4)
          printf("  %5.1f/%5.1f/%.2f", spread[ph] / (double)trials, lag[ph] / (double)trials,
                 expo[ph] / (double)trials);
        printf("\n");
      }
    }
    printf("\n");
  }
  return (0);
}
