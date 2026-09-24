/*
 * measure_phases.c -- phase-depth measurement for Bracha 1987 Figure 4.
 *
 * Drives real bracha87Fig4 instances (no model of the figure) with a
 * genuine local fair coin and asks, per trial, the phase in which the
 * LAST correct process decided.  Reports the histogram, the per-phase
 * hazard, and the phase budget the hazard implies.
 *
 * Ten arms, differing only in WHO sends, WHAT the faulty send, and
 * WHICH messages each correct process has validated when its turn
 * fires:
 *
 *   A  silent   t faulty send nothing; the n-t correct all send, so
 *               every correct process is FORCED to validate the same
 *               n-t messages -- there is no subset to choose.
 *   B0 byzFix   t faulty send plain 0 every round; each correct
 *               process validates an independent uniform random
 *               (n-t)-subset of the n senders.
 *   B1 byzMin   as B0, but the faulty value each round is the base
 *               value currently in the MINORITY among the correct
 *               senders (ties broken to 1, against the library's
 *               majority tie-break to 0).
 *   C  async    no faulty processes at all; each correct process
 *               still validates only an independent uniform random
 *               (n-t)-subset of the n senders.  Isolates schedule
 *               diversity from Byzantine content.
 *   D  adv      the papers' adversary: t faulty whose values it picks,
 *               and a scheduler that chooses EVERY receiver's n-t
 *               sample knowing the coin outcomes.  Its play, per
 *               phase, over m = n-t correct values with X ones:
 *                 step 1  a receiver can be handed majority 1 iff
 *                         X + f1 >= floor(m/2)+1 and majority 0 iff
 *                         m-X + f0 >= ceil(m/2) (tie is 0), f0+f1 = t
 *                         the faulty split.  Both hold iff
 *                         floor(m/2)-t+1 <= X <= floor(m/2)+t: the
 *                         BAND.  Inside it every receiver is handed a
 *                         majority of the adversary's choosing,
 *                         alternating so the step-1 outcomes split
 *                         evenly; outside it no sample can produce the
 *                         minority, every correct process computes
 *                         the same majority and Lemma 9 decides.
 *                 step 2  faulty values balance the full set to within
 *                         one of n/2 each, and every receiver's sample
 *                         holds neither value above n/2, so no (d, v)
 *                         is emitted.  (The alternation at step 1 is
 *                         a convenience: step 2 rebalances from the
 *                         split it actually finds.)
 *                 step 3  no (d, v) anywhere: every correct process
 *                         tosses.  The next phase's X is Bin(m, 1/2).
 *               Per-phase convergence under this play is
 *                 rhoD(m, t) = P(X <= floor(m/2)-t) + P(X >= floor(m/2)+t+1)
 *               and that is what the arm must measure.
 *   E  adv1     arm D, plus at even m one forced process: one correct
 *               receiver is handed > n/2 ones at step 2 and emits
 *               (d, 1); the faulty send (d, 1) too; one receiver is
 *               handed t+1 of them (adopts 1, case (ii)) and every
 *               other at most t (tosses).  The next X is 1 + Bin(m-1),
 *               and for even m that is strictly harder to push out of
 *               the band than Bin(m): the tie-to-0 rule makes the
 *               lower tail one step wider than the upper, and the
 *               shift removes it.  So this arm's rho is arm D's
 *               formula at m' = m if m odd, m-1 if even.  At odd m
 *               arm E is arm D.  It is the best play with the forced
 *               count fixed BEFORE the phase's coins -- and that is
 *               not the adversary's best.
 *   F  adapt    the same emitter and faulty (d, 1) as arm E, at every
 *               m, but the step-3 samples are handed out ONE PROCESS
 *               AT A TIME, each decided after the previous process's
 *               coin has been read off the broadcast it produced (the
 *               paper's scheduler decides "in each round for each
 *               process", on the whole past).  A force can only raise
 *               X, so the adversary tosses every process it safely
 *               can and forces the remainder exactly when
 *               ones + remaining would otherwise fall to the band's
 *               lower edge: the lower tail is never reached, and the
 *               upper tail is untouched (a force happens only on a
 *               path whose free tosses would have ended below the
 *               band).  Per-phase convergence is therefore the upper
 *               tail alone,
 *                 rho(m, t) = P(X >= floor(m/2)+t+1)
 *                           = 2^-m * sum_{k=0}^{ceil(m/2)-t-1} C(m, k)
 *               which at n = 3t+1 is 2^-(n-t): Bracha's Theorem 2
 *               figure, met exactly.  No play does better: v must be
 *               committed before the first coin is read (a tosser's
 *               step-3 sample holds n-t round-(3i+3) messages, so
 *               every faulty (d, v) in it is already sent, and without
 *               t+1 of them committed nobody can be forced later), a
 *               force never lowers X, and holding a process a phase
 *               behind only spends an exclusion slot.  This is the
 *               figure the range table at the end is built on.
 *               A plan the library did not honor (a sample that fired
 *               off the chosen set, a quota short of senders) counts
 *               in PlanMiss and voids the cell.  Arms H-K set no
 *               quota -- a turn there fires over the in set, or the
 *               first n-t validated when it is short -- so nothing in
 *               them counts in PlanMiss.
 *   H  faulty   agreementChain's transport, not the papers': a correct
 *               message reaches every receiver before its next turn
 *               unless dropped or straddling the tick (probability q,
 *               the run's second argument, the deployment's declared
 *               loss), and a turn fires over everything validated by
 *               then -- n-t up to n -- never on a chosen subset, since
 *               nothing in the transport lets the t faulty delay
 *               correct-to-correct traffic.  The faulty's whole lever
 *               is their OWN t votes: per receiver, delivered before
 *               its turn (in) or after (out, still landing in VALID^k
 *               so N stays permissive), and they read every coin as
 *               recipients.  With X ones among m correct and the
 *               faulty voting the minority v (tie is 0, so a tie's
 *               minority is 1): a receiver with them in reads v iff
 *               c_v + t beats m - c_v (strictly for v = 1, at a tie
 *               for v = 0), one with them out reads the population
 *               majority.  The band is floor((m-t)/2)+1 <= X <=
 *               floor((m+t)/2), width t against the scheduler's 2t;
 *               inside it in/out alternates so the step-1 majorities
 *               split evenly, step 2 is balanced and the faulty stay
 *               out, and every correct process tosses.  At q = 0,
 *                 rhoH(m, t) = P(X <= floor((m-t)/2)) + P(X >= floor((m+t)/2)+1)
 *               Under q the arrivals add noise of their own, so the
 *               printed analytic is the q = 0 figure only.
 *   I  fadapt   arm H plus the force of arm F, by the faulty's own
 *               lever: one correct receiver gets their votes in at
 *               step 2 and emits (d, 1), their step-3 (d, 1)s go in at
 *               a receiver to force it and out elsewhere, decided per
 *               receiver after the earlier receivers' coins are read
 *               -- turns spread across a tick, the worst case.  The low
 *               tail is killed as in arm F:
 *                 rhoI(m, t) = P(X >= floor((m+t)/2)+1)
 *               Whether the faulty can react inside one tick is the
 *               deployment's to rule; H and I bracket it.
 *   K  expo     arm I with the adaptive force reaching only the last
 *               Expo correct processes -- the ones whose step-3 turn
 *               fires after some coin is revealed (test/measure_drift.c
 *               measures which, and how many, under a release
 *               discipline); the rest toss before any coin is out and
 *               cannot be forced.  Expo = 0 is arm H, Expo = n-t is
 *               arm I; the sweep prints the rungs between, which is
 *               how an exposure the drift model reports becomes a
 *               per-phase figure.  The forceable processes are the
 *               LAST in the receiver order, so the adaptive rule reads
 *               every free toss before it decides them: the worst
 *               case for the correct side, and the drift model's own
 *               order, where the exposed are the laggards, whose
 *               turns fire last.
 *   J  leak     between I and F.  The faulty DO have a lever on which
 *               correct messages a receiver has validated when its turn
 *               fires, one layer above the transport: at n = 3t+1 a
 *               broadcast accepts on 2t+1 readies, so one the faulty
 *               echo and ready accepts on t+1 correct readies (t
 *               spares) while one they ignore needs every correct ready
 *               to land -- and a lost one is re-sent a sweep later,
 *               after the turn.  So per receiver they name up to t
 *               correct step-1 senders to leave slow, the ones carrying
 *               the value that receiver is meant not to read, and each
 *               is missing at the turn with a probability s the
 *               deployment sets (loss, tick straddling, sweep length;
 *               swept here).  The split is attempted across the
 *               scheduler's 2t band and the leaks decide where it
 *               lands; the force aims at that band's edge.  Otherwise
 *               arm I.  At s = 1 the figure is arm F's; as s falls it
 *               rises, and below about s = 0.25 the attempted splits
 *               fail often enough that arm I's plan does better -- the
 *               adversary's figure at a given s is the smaller of the
 *               two cells.  s is the deployment's: the chance that a
 *               correct broadcast the faulty leave unhelped is not yet
 *               validated when the turn fires.  At n = 3t+1 an unhelped
 *               broadcast needs every one of the 2t+1 correct readies,
 *               so under a loss rate q it is roughly 1 - (1-q)^(2t+1)
 *               plus tick straddling; above 3t+1 the accept threshold
 *               leaves spares (2t+1 of n-t) and loss alone buys the
 *               faulty little -- sizing N above the floor closes this
 *               lever as it closes the vote lever.  s is readable in
 *               the deployment from turns fired below n.
 *
 * What none of H, I, J can say, with Fig 1 bypassed: whether per-
 * receiver in/out of the faulty's OWN broadcast is a free switch.  Once
 * t+1 correct processes have readied it, every receiver accepts it
 * within a hop whatever the faulty send there, so lossless it is a
 * window against unsynchronized tick boundaries, not a choice.  The
 * three arms price the lever as if fully available.
 *
 * Arms A-D never set BRACHA87_D_FLAG on a faulty message.  Fig 3,
 * which this harness drives (bracha87Fig3Accept / GetValid),
 * validates a D_FLAG only when the sender could have earned it, and an
 * earned D_FLAG only ever helps convergence; withholding it is
 * therefore both within what the real composition would deliver and
 * the convergence-fighting play.  Arms E and F set it deliberately, on
 * a (d, 1) the receiver's own step-2 set makes legitimate, because
 * there it is what forces the processes the strategy wants forced.
 *
 * Fig 1 is bypassed, and its one constraint on a faulty process
 * is kept: every faulty sender's value for a round is one value,
 * delivered to every receiver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bracha87.h"

/* arc4random_buf is not C89; -std=c89 hides its stdlib.h prototype */
void arc4random_buf(void *, size_t);

#define MAX_N      256
#define MAX_PHASES BRACHA87_MAX_PHASES
#define ARM_SILENT 0
#define ARM_BYZFIX 1
#define ARM_BYZMIN 2
#define ARM_ASYNC  3
#define ARM_ADV    4
#define ARM_ADV1   5
#define ARM_ADAPT  6
#define ARM_HOLD   7
#define ARM_HADAPT 8
#define ARM_LEAK   9
#define ARM_EXPO   10
#define ARMS       11

static unsigned long Stalls;
static unsigned long CoinCalls;   /* every step 3 case (iii) in the library */
static unsigned long CaseMismatch;/* harness classification vs CoinCalls */
static unsigned long PlanMiss;    /* adversary arms: a plan the run did not realize */
static double        DropQ;       /* arms H, I: a correct message's chance of missing a turn */
static double        LeakS;       /* arm J: a scheduler exclusion's chance of holding */
static unsigned int  Expo;        /* arm K: correct processes whose step-3 turn can be forced */

/*
 * Per-cell step 3 census.  Indexed by k, the number of correct
 * processes that took case (iii) in one phase -- the quantity the
 * budget rests on.  PhaseK counts phases, PhaseKConv the subset after
 * which every correct process held the same value, so Lemma 9 decides
 * the next phase.
 */
static unsigned long PhaseK[MAX_N + 1];
static unsigned long PhaseKConv[MAX_N + 1];
static unsigned long PhasesSeen;
static unsigned long CaseCnt[3];  /* process-phases in case (i)/(ii)/(iii) */

static unsigned char Rbuf[8192];
static unsigned long Rpos = sizeof (Rbuf);

static unsigned char
rByte(
  void
){
  if (Rpos >= sizeof (Rbuf)) {
    arc4random_buf(Rbuf, sizeof (Rbuf));
    Rpos = 0;
  }
  return (Rbuf[Rpos++]);
}

/* Uniform on 0..m-1 by rejection; m <= 256 */
static unsigned int
rRange(
  unsigned int m
){
  unsigned int lim;
  unsigned int v;

  if (m < 2)
    return (0);
  lim = 256 - (256 % m);
  do
    v = rByte();
  while (v >= lim);
  return (v % m);
}

/*
 * The local coin of step 3 case (iii): an independent fair bit per
 * call, so per process per phase.  instance and phase are ignored --
 * a local coin needs entropy, not a name.
 */
static unsigned char
localCoin(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
  (void)phase;
  ++CoinCalls;
  return ((unsigned char)(rByte() & 1));
}

/*
 * One trial.  Returns 1 if every correct process decided within
 * maxPhases, 0 if the phase space ran out first.
 *
 * On a 1 return firstPh/lastPh hold the phase of the first and last
 * correct decision.  agreeOk is cleared if two correct processes
 * decided different values (Theorem 2 violation) -- a harness bug or
 * a library defect, either way the run is void.
 */
static int
runTrial(
  unsigned int n
 ,unsigned int t
 ,int arm
 ,int allSame
 ,unsigned int maxPhases
 ,unsigned int *firstPh
 ,unsigned int *lastPh
 ,int *agreeOk
){
  struct bracha87Fig4 *inst[MAX_N];
  unsigned char msg[MAX_N];
  unsigned char nxt[MAX_N];
  unsigned char svals[MAX_N];
  unsigned char ssend[MAX_N];
  unsigned char idx[MAX_N];
  unsigned int decPh[MAX_N];
  unsigned long sz;
  unsigned int nf;
  unsigned int c0;
  unsigned int nc;
  unsigned int nt;
  unsigned int nsend;
  unsigned int ndec;
  unsigned int k;
  unsigned int i;
  unsigned int j;
  unsigned int r;
  unsigned int vc;
  unsigned int vs;
  unsigned int act;
  unsigned int kCoin;
  unsigned long coin0;
  int fired;
  int preDec;
  unsigned int cnt0;
  unsigned int cnt1;
  unsigned char bv;
  unsigned char dv;
  unsigned char tmp;
  int ok;
  int plan;            /* adversary arms: this phase is inside the band */
  int force;           /* arm E at even m: force one process this phase */
  unsigned int advV;   /* adversary arms: the value every majority takes outside the band */
  unsigned int advB;   /* arm E: correct receivers handed majority 1 at step 1 */
  unsigned int sub;
  unsigned int X;
  unsigned int Z;
  unsigned int O;
  unsigned int fz;
  unsigned int quota[3];
  unsigned int key[MAX_N];
  unsigned char ord[MAX_N];
  unsigned char inAt[MAX_N];   /* arms H, I: faulty votes in before receiver's turn */
  unsigned int no;
  unsigned int q;
  unsigned int fireAt;         /* arms H, I: the turn fires once this many are in */

  nf = (arm == ARM_ASYNC) ? 0 : t;
  c0 = nf;
  nc = n - nf;
  nt = n - t;
  nsend = (arm == ARM_SILENT) ? nc : n;
  plan = 0;
  force = 0;
  advV = 0;
  advB = 0;
  memset(inAt, 0, sizeof (inAt));

  sz = bracha87Fig4Sz(n - 1, maxPhases);
  if (!sz) {
    fprintf(stderr, "Sz refused n=%u maxPhases=%u\n", n, maxPhases);
    exit(1);
  }
  for (i = 0; i < n; ++i)
    inst[i] = 0;
  for (i = c0; i < n; ++i) {
    if (!(inst[i] = calloc(1, sz))) {
      fprintf(stderr, "OoR\n");
      exit(1);
    }
    /* worst-case split among the correct: floor(nc/2) zeros, rest ones */
    dv = allSame ? 0 : (unsigned char)(((i - c0) < nc / 2) ? 0 : 1);
    if (!bracha87Fig4Init(inst[i], (unsigned char)(n - 1), (unsigned char)t,
                          (unsigned char)maxPhases, dv, 0, localCoin, 0)) {
      fprintf(stderr, "Init refused\n");
      exit(1);
    }
    msg[i] = dv;
    decPh[i] = maxPhases;
  }

  ndec = 0;
  kCoin = 0;
  preDec = 1;
  coin0 = CoinCalls;
  *agreeOk = 1;
  dv = 0xFF;

  for (k = 0; k < maxPhases * BRACHA87_ROUNDS_PER_PHASE && ndec < nc; ++k) {
    if (!(k % BRACHA87_ROUNDS_PER_PHASE))
      preDec = !ndec;

    /*
     * Faulty senders' round-k message.  Plain binary in arms B0, B1
     * and D; see the D_FLAG note at the head of this file.
     */
    sub = k % BRACHA87_ROUNDS_PER_PHASE;
    if (nf && (arm == ARM_HOLD || arm == ARM_HADAPT || arm == ARM_LEAK || arm == ARM_EXPO)) {
      Z = O = 0;
      for (i = c0; i < n; ++i) {
        if ((msg[i] & ~BRACHA87_D_FLAG) == 1)
          ++O;
        else
          ++Z;
      }
      X = O;
      if (sub == 0) {
        /* w = the population majority (tie 0), v the minority; the
         * band is where the faulty's t votes flip an in-receiver to v */
        advV = (X > nc - X) ? 1 : 0;
        if (arm == ARM_LEAK)
          plan = (X + t >= nc / 2 + 1 && X <= nc / 2 + t);  /* the 2t band: the split is attempted and the leaks decide */
        else if (advV)
          plan = (2 * X <= nc + t);
        else
          plan = (2 * X + t > nc);
        force = plan && (arm == ARM_HADAPT || arm == ARM_LEAK || (arm == ARM_EXPO && Expo));
        advB = force ? n / 2 + 1 - t : 0;
        for (i = 0; i < nf; ++i)
          msg[i] = plan ? !advV : advV;
        for (i = c0; i < n; ++i) {
          if (!plan)
            inAt[i] = 1;
          else if (force)
            inAt[i] = ((i - c0 < advB) ? 1 : 0) == !advV;
          else
            inAt[i] = ((i - c0) & 1) == !advV;
        }
      } else if (sub == 1) {
        if (!plan) {
          for (i = 0; i < nf; ++i)
            msg[i] = advV;
        } else if (force) {
          for (i = 0; i < nf; ++i)
            msg[i] = 1;
        } else {
          fz = (n / 2 > Z) ? n / 2 - Z : 0;
          if (fz > t)
            fz = t;
          for (i = 0; i < nf; ++i)
            msg[i] = (i < fz) ? 0 : 1;
        }
        for (i = c0; i < n; ++i)
          inAt[i] = !plan || (force && i == c0);
      } else {
        if (!plan)
          for (i = 0; i < nf; ++i)
            msg[i] = advV | BRACHA87_D_FLAG;
        else if (force)
          for (i = 0; i < nf; ++i)
            msg[i] = 1 | BRACHA87_D_FLAG;
        for (i = c0; i < n; ++i)
          inAt[i] = !plan;         /* under force, decided per receiver below */
      }
    } else if (nf && (arm == ARM_ADV || arm == ARM_ADV1 || arm == ARM_ADAPT)) {
      /*
       * The adversary reads the correct processes' round-k values
       * (msg[c0..n), what they are about to broadcast) and sets the
       * faulty values for the step; the receiver loop below then
       * hands each receiver the sample the plan names.
       */
      Z = O = 0;
      for (i = c0; i < n; ++i) {
        if ((msg[i] & ~BRACHA87_D_FLAG) == 1)
          ++O;
        else
          ++Z;
      }
      X = O;
      if (sub == 0) {
        /* the band: floor(m/2)-t+1 <= X <= floor(m/2)+t */
        plan = (X + t >= nc / 2 + 1 && X <= nc / 2 + t);
        force = plan && (arm == ARM_ADAPT || (arm == ARM_ADV1 && !(nc & 1)));
        if (plan) {
          /* f0 = max(0, X - floor(m/2)): the smallest faulty-zero count
           * that leaves ceil(m/2) zeros available; the rest send 1 */
          fz = (X > nc / 2) ? X - nc / 2 : 0;
          advB = force ? n / 2 + 1 - t : 0;
        } else {
          advV = (X > nc / 2) ? 1 : 0;
          fz = advV ? 0 : t;
        }
        for (i = 0; i < nf; ++i)
          msg[i] = (i < fz) ? 0 : 1;
      } else if (sub == 1) {
        if (!plan)
          fz = advV ? 0 : t;
        else if (force)
          fz = 0;           /* every faulty sends 1: ones = advB + t = n/2 + 1 */
        else {
          /* balance the full step-2 set: zeros to n/2, the rest ones */
          fz = (n / 2 > Z) ? n / 2 - Z : 0;
          if (fz > t)
            fz = t;
        }
        for (i = 0; i < nf; ++i)
          msg[i] = (i < fz) ? 0 : 1;
      } else {
        if (!plan)
          for (i = 0; i < nf; ++i)
            msg[i] = advV | BRACHA87_D_FLAG;
        else if (force)
          for (i = 0; i < nf; ++i)
            msg[i] = 1 | BRACHA87_D_FLAG;
        /* else: arm D keeps the plain step-2 value; no (d, v) is
         * legitimate against a balanced set and none is wanted */
      }
    } else if (nf) {
      if (arm == ARM_BYZFIX)
        bv = 0;
      else {
        cnt0 = cnt1 = 0;
        for (i = c0; i < n; ++i) {
          if ((msg[i] & ~BRACHA87_D_FLAG) == 1)
            ++cnt1;
          else
            ++cnt0;
        }
        bv = (unsigned char)((cnt1 <= cnt0) ? 1 : 0);
      }
      for (i = 0; i < nf; ++i)
        msg[i] = bv;
    }

    for (i = c0; i < n; ++i) {
      /*
       * Terminal instances (decided on the last phase, or a
       * post-decide continuation out of phase space) no longer name a
       * next round; re-calling them would recompute a spent round.
       */
      if (k != (unsigned int)inst[i]->phase * BRACHA87_ROUNDS_PER_PHASE
              + inst[i]->subRound) {
        nxt[i] = inst[i]->value;
        continue;
      }

      /*
       * Deliver round-k messages to this process in an independent
       * random order through its own Fig 3, and fire the round on the
       * n-t'th VALIDATION -- the smallest legal sample, which is what
       * a caller firing at enabling takes.  Fig 3 is what makes a
       * faulty message cost the adversary something: a value outside
       * VALID^k for THIS receiver is dropped and the wait goes on, so
       * the faulty processes get exactly the freedom the paper's
       * existential quantifier leaves them and no more.
       */
      for (j = 0; j < nsend; ++j)
        idx[j] = (unsigned char)((arm == ARM_SILENT) ? c0 + j : j);
      for (j = 0; j < nsend; ++j) {
        r = j + rRange(nsend - j);
        tmp = idx[j];
        idx[j] = idx[r];
        idx[r] = tmp;
      }
      /*
       * The adversary's sample: quotas per sender class, pulled to
       * the front of the (otherwise random) order so that the n-t'th
       * validation IS the chosen set.  Classes are 0 and 1 by base
       * value at steps 1 and 2; at step 3 under a force, 0 = plain,
       * 1 = faulty (d, 1), 2 = the correct (d, 1) sender.  A quota a
       * class cannot fill is a plan miss.
       */
      quota[0] = quota[1] = quota[2] = 0;
      q = 0;
      fireAt = 0;
      if (arm == ARM_HOLD || arm == ARM_HADAPT || arm == ARM_LEAK || arm == ARM_EXPO) {
        /*
         * The deployment's arrival: every correct message is in before
         * the turn unless it missed (DropQ); the faulty are in or out
         * as planned, and under a force the step-3 in/out is decided
         * here from the coins already read.  Order: the in set (random
         * within), then the late correct, then the faulty if out.  The
         * turn fires over the in set, or over the first n-t validated
         * when the in set is short (HELD waits for the retry).
         */
        if (sub == 2 && force) {
          O = 0;
          for (j = c0; j < i; ++j)
            if (nxt[j] == 1)
              ++O;
          inAt[i] = (O + n - i <= ((arm == ARM_LEAK) ? nc / 2 - t + 1 : (nc - t) / 2 + 1)) ? 1 : 0;
          if (arm == ARM_EXPO && i + Expo < n)
            inAt[i] = 0;                 /* fired before any coin was out */
        }
        no = 0;
        for (j = 0; j < nsend; ++j)
          if (idx[j] >= c0
           && (unsigned int)(rByte() << 8 | rByte()) >= (unsigned int)(DropQ * 65536.0))
            ord[no++] = idx[j];
        if (arm == ARM_LEAK && plan && sub == 0) {
          /*
           * The faulty's Fig 1 lever: per receiver, up to t correct
           * senders of the value this receiver is meant NOT to read
           * are left slow, and each is missing at the turn with
           * probability s.  An in-receiver is meant to read the
           * minority, so its slow ones carry the majority; an
           * out-receiver the reverse.
           */
          O = inAt[i] ? advV : !advV;    /* the value to thin */
          Z = 0;                         /* thinned so far */
          for (j = 0; j < no && Z < t; ++j)
            if ((msg[ord[j]] & ~BRACHA87_D_FLAG) == O
             && (unsigned int)(rByte() << 8 | rByte()) < (unsigned int)(LeakS * 65536.0)) {
              tmp = ord[j];
              ord[j] = ord[no - 1];
              ord[no - 1] = tmp;
              --no;
              --j;
              ++Z;
            }
        }
        if (inAt[i])
          for (j = 0; j < nf; ++j)
            ord[no++] = (unsigned char)j;
        fireAt = no;
        for (j = 0; j < nsend; ++j) {
          for (r = 0; r < fireAt && ord[r] != idx[j]; ++r)
            ;
          if (r == fireAt && idx[j] >= c0)
            ord[no++] = idx[j];
        }
        if (!inAt[i])
          for (j = 0; j < nf; ++j)
            ord[no++] = (unsigned char)j;
        memcpy(idx, ord, nsend);
        if (fireAt < nt)
          fireAt = nt;
      }
      if ((arm == ARM_ADV || arm == ARM_ADV1 || arm == ARM_ADAPT) && plan) {
        if (sub == 0) {
          if (force ? (i - c0 < advB) : ((i - c0) & 1)) {
            quota[1] = nc / 2 + 1;       /* majority 1: floor(m/2)+1 ones */
            quota[0] = nt - quota[1];
          } else {
            quota[0] = (nc + 1) / 2;     /* majority 0: ceil(m/2) zeros */
            quota[1] = nt - quota[0];
          }
          q = 2;
        } else if (sub == 1) {
          if (force && i == c0)
            quota[1] = n / 2 + 1;        /* the one (d, 1) emitter */
          else {
            Z = O = 0;
            for (j = 0; j < n; ++j) {
              if ((msg[j] & ~BRACHA87_D_FLAG) == 1)
                ++O;
              else
                ++Z;
            }
            quota[1] = (O < n / 2) ? O : n / 2;
          }
          quota[0] = nt - quota[1];
          q = 2;
        } else if (force) {
          quota[1] = t;                  /* the faulty (d, 1)s */
          if (arm == ARM_ADAPT) {
            /*
             * Adaptive: the processes before i have fired this round
             * and nxt[] holds what each will broadcast -- its coin, or
             * the 1 it was forced to.  Force i iff tossing the rest
             * could still land below the band.
             */
            O = 0;
            for (j = c0; j < i; ++j)
              if (nxt[j] == 1)
                ++O;
            quota[2] = (O + n - i + t <= nc / 2 + 1) ? 1 : 0;
          } else
            quota[2] = (i == c0 + 1) ? 1 : 0; /* the forced one also takes c0's */
          quota[0] = nt - quota[1] - quota[2];
          q = 3;
        }
        if (q) {
          for (j = 0; j < nsend; ++j) {
            if (q == 3)
              key[j] = !(msg[idx[j]] & BRACHA87_D_FLAG) ? 0
                     : (idx[j] < nf) ? 1 : 2;
            else
              key[j] = (msg[idx[j]] & ~BRACHA87_D_FLAG) == 1;
          }
          no = 0;
          for (r = 0; r < q; ++r)
            for (j = 0; j < nsend && quota[r]; ++j)
              if (key[j] == r) {
                ord[no++] = idx[j];
                key[j] = 3;              /* taken */
                --quota[r];
              }
          if (quota[0] || quota[1] || quota[2] || no != nt)
            ++PlanMiss;
          for (j = 0; j < nsend; ++j)
            if (key[j] != 3)
              ord[no++] = idx[j];
          memcpy(idx, ord, nsend);
        }
      }
      vc = 0;
      fired = 0;
      for (j = 0; j < nsend; ++j) {
        bracha87Fig3Accept(&inst[i]->fig3, (unsigned char)k, idx[j],
                           msg[idx[j]], &vc);
        if (fired || vc < nt || j + 1 < fireAt)
          continue;
        if (q && !fireAt && j != nt - 1)
          ++PlanMiss;                    /* fired off the chosen set */
        /*
         * The n-t'th validation.  Take the sample HERE, not after the
         * rest of the round's traffic lands: the smallest legal
         * sample is both what a caller firing at enabling consumes
         * and the one that leaves correct processes most free to
         * disagree, so it is the worst case for convergence.
         */
        fired = 1;
        vs = bracha87Fig3GetValid(&inst[i]->fig3, (unsigned char)k,
                                  ssend, svals);
        if (k % BRACHA87_ROUNDS_PER_PHASE == 2 && preDec) {
          cnt0 = cnt1 = 0;
          for (r = 0; r < vs; ++r) {
            if (!(svals[r] & BRACHA87_D_FLAG))
              continue;
            if ((svals[r] & ~BRACHA87_D_FLAG) == 1)
              ++cnt1;
            else if (!(svals[r] & ~BRACHA87_D_FLAG))
              ++cnt0;
          }
          r = (cnt1 > cnt0) ? cnt1 : cnt0;
          if (r > 2 * t)
            ++CaseCnt[0];
          else if (r > t)
            ++CaseCnt[1];
          else {
            ++CaseCnt[2];
            ++kCoin;
          }
        }
        act = bracha87Fig4Round(inst[i], (unsigned char)k, vs, svals);
        if ((act & BRACHA87_DECIDE)) {
          decPh[i] = k / BRACHA87_ROUNDS_PER_PHASE;
          ++ndec;
          if (dv == 0xFF)
            dv = inst[i]->decision;
          else if (dv != inst[i]->decision)
            *agreeOk = 0;
        }
      }
      /*
       * Delivery does NOT stop at the firing point: the rest of the
       * round's traffic still lands, and it must, because VALID^k is
       * the evidence against which round k+1 is validated.  A VALID^k
       * frozen at n-t would reject the round-(k+1) value of every
       * correct process whose own sample differed -- the harness's
       * own truncation, read back as a stalled protocol.
       */
      if (!fired)
        ++Stalls;
      nxt[i] = inst[i]->value;
    }

    /*
     * Step 3 census, taken on the phase's last round.  The case is
     * read off the SAME sample the library consumed, by the paper's
     * own arithmetic: >2t d-messages is case (i), >t is case (ii),
     * neither is case (iii).  CoinCalls is the library's own count of
     * case (iii); the two disagreeing means the harness has
     * misclassified and the census is void.
     */
    if (k % BRACHA87_ROUNDS_PER_PHASE == 2 && preDec) {
      if (kCoin != CoinCalls - coin0)
        ++CaseMismatch;
      ++PhasesSeen;
      ++PhaseK[kCoin];
      r = 1;
      for (i = c0 + 1; i < n; ++i)
        if (nxt[i] != nxt[c0])
          r = 0;
      PhaseKConv[kCoin] += r;
    }
    if (k % BRACHA87_ROUNDS_PER_PHASE == 2) {
      kCoin = 0;
      coin0 = CoinCalls;
    }

    for (i = c0; i < n; ++i)
      msg[i] = nxt[i];
  }

  ok = (ndec == nc);
  *firstPh = maxPhases;
  *lastPh = 0;
  if (ok)
    for (i = c0; i < n; ++i) {
      if (decPh[i] < *firstPh)
        *firstPh = decPh[i];
      if (decPh[i] > *lastPh)
        *lastPh = decPh[i];
    }

  for (i = c0; i < n; ++i)
    free(inst[i]);
  return (ok);
}

static const char *ArmName[ARMS] = {
  "A-silent", "B0-byzFix", "B1-byzMin", "C-async", "D-adv", "E-adv1", "F-adapt",
  "H-faulty", "I-fadapt", "J-leak", "K-expo"
};

/*
 * The adversary's per-phase convergence probability over m tossing
 * correct processes, X ~ Bin(m, 1/2): the upper tail
 * P(X >= floor(m/2)+t+1), plus the lower tail P(X <= floor(m/2)-t)
 * when twoTail is set.  Arm D is the two-tail figure at m = n-t; arm
 * E the two-tail figure at the largest odd m' <= n-t; arm F, the
 * adversary's best, the upper tail at m = n-t.  Terms are built by
 * ratio from 2^-m, so m up to 255 stays in range of a double.
 */
static double
advTail(
  unsigned int m
 ,int lo                     /* low tail: X <= lo (when twoTail) */
 ,unsigned int hi            /* high tail: X >= hi */
 ,int twoTail
){
  double term;
  double sum;
  unsigned int k;

  term = ldexp(1.0, -(int)m);
  sum = 0.0;
  for (k = 0; k <= m; ++k) {
    if ((twoTail && (int)k <= lo) || k >= hi)
      sum += term;
    term = term * (double)(m - k) / (double)(k + 1);
  }
  return (sum);
}

static double
advRho(
  unsigned int m
 ,unsigned int t
 ,int twoTail
){
  return (advTail(m, (int)(m / 2) - (int)t, m / 2 + t + 1, twoTail));
}

/*
 * Arms H and I: the faulty's own-vote band, floor((m-t)/2)+1 <= X <=
 * floor((m+t)/2); the two tails outside it (H), or the upper alone (I).
 */
static double
depRho(
  unsigned int m
 ,unsigned int t
 ,int twoTail
){
  return (advTail(m, (int)((m - t) / 2), (m + t) / 2 + 1, twoTail));
}

int
main(
  int argc
 ,char **argv
){
  /*
   * The five asked-for configurations, then two probes.  The probes
   * hold t fixed and move n across 4t, the threshold at which a
   * correct process can still reach step 2's >n/2 gate with all t
   * faulty messages occupying its sample (n - 2t > n/2 iff n > 4t).
   * 10/3 -> 13/3 and 16/4 -> 17/4 cross it on a change in n of 3 and
   * 1, which separates a dependence on N from a dependence on n/t.
   */
  static const unsigned int Cfg[11][2] = {
    { 4, 1 }, { 7, 2 }, { 10, 3 }, { 16, 4 }, { 25, 5 }, { 36, 6 }, { 49, 7 },
    { 100, 10 }, { 256, 16 },
    { 13, 3 }, { 17, 4 }
  };
#define NCFG (sizeof (Cfg) / sizeof (Cfg[0]))
#define NPROBE 2
  /* the two large configurations run only where a phase is cheap: the
   * scheduler arms spend tens of phases per trial at every n */
#define BIG(n, arm) ((n) > 64 && (((arm) >= ARM_BYZFIX && (arm) <= ARM_ADAPT) || (arm) == ARM_LEAK || (arm) == ARM_EXPO))
  static const double Qs[5] = { 0.0, 0.01, 0.05, 0.10, 0.25 };
  static const double Ss[4] = { 0.25, 0.5, 0.75, 1.0 };
  unsigned int qi;
  unsigned int nq;
  unsigned long cellTrials;
  int armOnly;
  unsigned int ncfg;
  unsigned long hist[MAX_PHASES + 1];
  unsigned long gapHist[16];
  unsigned long trials;
  unsigned long tr;
  unsigned long ceil_;
  unsigned long bad;
  unsigned long surv;
  unsigned long atK;
  double sum;
  double p;
  unsigned int n;
  unsigned int t;
  unsigned int first;
  unsigned int last;
  unsigned int cfg;
  unsigned int k;
  unsigned int gap;
  unsigned int r;
  int arm;
  int okAgree;
  int ok;

  trials = (argc > 1) ? strtoul(argv[1], 0, 10) : 100000UL;
  armOnly = (argc > 2) ? atoi(argv[2]) : -1;   /* one arm by index, or all */

  printf("Bracha 1987 Fig 4 phase-depth measurement\n");
  printf("trials per cell: %lu   maxPhases: %d   coin: arc4random_buf, "
         "one fair bit per process per phase\n", trials, MAX_PHASES);
  printf("usage: measure_phases [trials [arm]]   arm 0..%d runs that arm alone\n\n", ARMS - 1);

  printf("configurations (all satisfy n > 3t AND n >= t*t)\n");
  printf("  %-5s %-4s %-5s %-6s %-6s %-13s %-8s %-8s %-8s %-22s %-8s %-8s %s\n",
         "n", "t", "n-t", "3t+1", "t*t", "binding", "rho(D)", "rho(E)",
         "rho(F)", "E[coin] mp6 mp9 (F)", "rho(H)", "rho(I)", "E[coin] mp6 mp9 (I)");
  for (cfg = 0; cfg < NCFG; ++cfg) {
    double pi;

    n = Cfg[cfg][0];
    t = Cfg[cfg][1];
    p = advRho(n - t, t, 0);
    pi = depRho(n - t, t, 0);
    printf("  %-5u %-4u %-5u %-6u %-6u %-13s %-8.4f %-8.4f %-8.4f %-7.1f %-6.0f %-7.0f %-8.4f %-8.4f %-7.1f %-6.0f %.0f\n",
           n, t, n - t, 3 * t + 1, t * t,
           (cfg >= NCFG - NPROBE) ? "PROBE"
         : (t * t < 3 * t + 1) ? "n > 3t"
         : (t * t > 3 * t + 1) ? "n >= t*t"
         : "both (equal)",
           advRho(n - t, t, 1),
           advRho((n - t) & 1 ? n - t : n - t - 1, t, 1), p, 1.0 / p,
           ceil(1.0 + log(1e-6) / log(1.0 - p)),
           ceil(1.0 + log(1e-9) / log(1.0 - p)),
           depRho(n - t, t, 1), pi, 1.0 / pi,
           ceil(1.0 + log(1e-6) / log(1.0 - pi)),
           ceil(1.0 + log(1e-9) / log(1.0 - pi)));
  }
  printf("\n");

  /* Lemma 9 control: all correct start with the same value */
  printf("control (all-same initial values; Lemma 9 requires phase 0)\n");
  printf("  %-9s %-6s %-10s %s\n", "arm", "n/t", "maxLastPh", "trials");
  DropQ = 0.0;
  for (arm = 0; arm < ARMS; ++arm)
    for (cfg = 0; cfg < NCFG; ++cfg) {
      n = Cfg[cfg][0];
      t = Cfg[cfg][1];
      if (BIG(n, arm) || (armOnly >= 0 && arm != armOnly))
        continue;
      last = 0;
      bad = 0;
      ceil_ = 0;
      PlanMiss = 0;
      for (tr = 0; tr < (n > 64 ? 200UL : 2000UL); ++tr) {
        ok = runTrial(n, t, arm, 1, MAX_PHASES, &first, &k, &okAgree);
        if (!ok)
          ++ceil_;
        else if (k > last)
          last = k;
        if (!okAgree)
          ++bad;
      }
      printf("  %-9s %u/%-4u %-10u %lu%s%s%s\n", ArmName[arm], n, t, last, tr,
             ceil_ ? "  CEILING HIT" : "", bad ? "  AGREEMENT VIOLATED" : "",
             PlanMiss ? "  PLAN MISSED" : "");
    }
  printf("\n");

  /* Primary: worst-case split initial values */
  for (arm = 0; arm < ARMS; ++arm) {
    if (armOnly >= 0 && arm != armOnly)
      continue;
    ncfg = (arm == ARM_SILENT || arm == ARM_BYZFIX) ? NCFG - NPROBE : NCFG;
    nq = (arm == ARM_HOLD || arm == ARM_HADAPT) ? sizeof (Qs) / sizeof (Qs[0])
       : (arm == ARM_LEAK) ? sizeof (Ss) / sizeof (Ss[0])
       : (arm == ARM_EXPO) ? 4 : 1;
    printf("=== arm %s, worst-case split initial values ===\n", ArmName[arm]);
    printf("  %-8s %-9s %-8s %-6s %-7s %-9s %s\n",
           "n/t", "trials", "mean*", "max", "ceiling", "gap>0/max",
           "histogram ph0..");
    for (qi = 0; qi < nq; ++qi)
    for (cfg = 0; cfg < ncfg; ++cfg) {
      n = Cfg[cfg][0];
      t = Cfg[cfg][1];
      if (BIG(n, arm))
        continue;
      DropQ = (arm == ARM_LEAK || arm == ARM_EXPO) ? 0.0 : Qs[qi];
      LeakS = (arm == ARM_LEAK) ? Ss[qi] : 1.0;
      /* arm K: 1, t, 2t and every correct process forceable; at t = 1
       * the first two rungs are one */
      Expo = (arm != ARM_EXPO) ? 0 : (qi == 0) ? 1 : (qi == 1) ? t : (qi == 2) ? 2 * t : n - t;
      if (arm == ARM_EXPO && qi == 1 && t == 1)
        continue;
      cellTrials = (n > 64) ? (trials / 10 > 100 ? trials / 10 : 100) : trials;
      memset(hist, 0, sizeof (hist));
      memset(gapHist, 0, sizeof (gapHist));
      memset(PhaseK, 0, sizeof (PhaseK));
      memset(PhaseKConv, 0, sizeof (PhaseKConv));
      memset(CaseCnt, 0, sizeof (CaseCnt));
      PhasesSeen = 0;
      CaseMismatch = 0;
      PlanMiss = 0;
      Stalls = 0;
      ceil_ = 0;
      bad = 0;
      sum = 0.0;
      for (tr = 0; tr < cellTrials; ++tr) {
        ok = runTrial(n, t, arm, 0, MAX_PHASES, &first, &last, &okAgree);
        if (!okAgree)
          ++bad;
        if (!ok) {
          ++ceil_;
          sum += MAX_PHASES;
          continue;
        }
        ++hist[last];
        sum += last;
        gap = last - first;
        if (gap > 15)
          gap = 15;
        ++gapHist[gap];
      }
      /* mean* -- a censored trial is counted at the ceiling, so with a
       * nonzero ceiling the figure is a LOWER BOUND, not the mean */
      if (arm == ARM_LEAK)
        printf("  s=%.2f", LeakS);
      else if (arm == ARM_EXPO)
        printf("  expo=%-3u", Expo);
      else if (nq > 1)
        printf("  q=%.2f", DropQ);
      printf("  %s%u/%-6u %-9lu %-8.4f ", (cfg >= NCFG - NPROBE) ? "probe " : "", n, t,
             cellTrials, sum / (double)cellTrials);
      for (k = MAX_PHASES; k > 0 && !hist[k]; --k)
        ;
      for (gap = 15; gap > 0 && !gapHist[gap]; --gap)
        ;
      printf("%-6u %-7lu %lu/max%u%s ", k, ceil_,
             cellTrials - ceil_ - gapHist[0], gap, (gap == 15) ? "+" : "");
      for (k = 0; k <= 9 && k <= MAX_PHASES; ++k)
        printf("%lu ", hist[k]);
      printf("%s\n", bad ? " AGREEMENT VIOLATED" : "");

      /*
       * Per-phase hazard: P(last == k | last >= k).  A ceiling trial
       * has NOT finished, so it stays in the at-risk denominator at
       * every phase -- dropping it reads the hazard off the finishers
       * alone and reports a machine converging faster than it does.
       */
      printf("        hazard:");
      surv = cellTrials;
      for (k = 0; k <= 9; ++k) {
        atK = hist[k];
        if (!surv)
          break;
        printf(" %.4f", (double)atK / (double)surv);
        surv -= atK;
      }
      /*
       * Pooled geometric hazard over phases >= 2 (phase 0 and 1 are
       * structurally different: phase 0 has no prior coin).  Events =
       * trials still running at phase 2; exposure = sum over trials of
       * the phases each spent at risk from 2 onward.  A ceiling trial
       * is censored: it contributes exposure but no event.
       */
      surv = cellTrials - ceil_;
      for (k = 0; k < 2; ++k)
        surv -= hist[k];
      atK = ceil_ * (unsigned long)(MAX_PHASES - 2);
      for (k = 2; k <= MAX_PHASES; ++k)
        atK += hist[k] * (unsigned long)(k - 1);
      p = atK ? (double)surv / (double)atK : 0.0;
      printf("        tail hazard p(ph>=2) = %.4f", p);
      if (arm == ARM_ADV)
        printf(" [analytic %.4f]", advRho(n - t, t, 1));
      else if (arm == ARM_ADV1)
        printf(" [analytic %.4f]",
               advRho((n - t) & 1 ? n - t : n - t - 1, t, 1));
      else if (arm == ARM_ADAPT)
        printf(" [analytic %.4f]", advRho(n - t, t, 0));
      else if (arm == ARM_HOLD)
        printf(" [analytic q=0 %.4f]", depRho(n - t, t, 1));
      else if (arm == ARM_HADAPT)
        printf(" [analytic q=0 %.4f]", depRho(n - t, t, 0));
      else if (arm == ARM_LEAK)
        printf(" [bracket I %.4f .. F %.4f]", depRho(n - t, t, 0), advRho(n - t, t, 0));
      else if (arm == ARM_EXPO)
        printf(" [bracket H %.4f .. I %.4f]", depRho(n - t, t, 1), depRho(n - t, t, 0));
      if (p > 0.0 && p < 1.0) {
        double s2;
        s2 = (double)(surv + ceil_) / (double)cellTrials;
        printf("   S(2)=%.5f   maxPhases@1e-6 = %.0f   @1e-9 = %.0f",
               s2,
               ceil(2.0 + (log(1e-6) - log(s2)) / log(1.0 - p)),
               ceil(2.0 + (log(1e-9) - log(s2)) / log(1.0 - p)));
      } else
        printf("   (no trial reached phase 2; budget = 1 phase)");
      printf("\n");

      /*
       * Step 3 census.  k is the number of correct processes that
       * took case (iii) in a phase; the phase converges (so Lemma 9
       * decides the next one) only if every one of them lands on the
       * value the forced processes already hold, which is 2^-k.
       */
      printf("        step3 process-phases: (i) decide %lu  (ii) forced %lu"
             "  (iii) coin %lu   phases %lu  k=0 %.4f%s%s\n",
             CaseCnt[0], CaseCnt[1], CaseCnt[2], PhasesSeen,
             PhasesSeen ? (double)PhaseK[0] / (double)PhasesSeen : 0.0,
             CaseMismatch ? "  CENSUS VOID" : "",
             Stalls ? "  STALLED" : "");
      if (PlanMiss)
        printf("        PLAN MISSED %lu times: the adversary arm is void here\n",
               PlanMiss);
      printf("        k>0 dist:");
      sum = 0.0;
      atK = 0;
      for (k = 1; k <= n; ++k) {
        atK += PhaseK[k];
        sum += (double)PhaseK[k] * k;
      }
      for (k = 1; k <= n && k <= 8; ++k)
        printf(" k=%u:%lu", k, PhaseK[k]);
      printf("   meanK|k>0 = %.3f   maxK = ", atK ? sum / (double)atK : 0.0);
      for (k = n; k > 0 && !PhaseK[k]; --k)
        ;
      printf("%u\n", k);
      sum = 0.0;
      for (k = 0; k <= n; ++k)
        sum += PhaseKConv[k];
      printf("        P(phase converges) = %.4f  (Lemma 9 then decides the "
             "next phase)\n", PhasesSeen ? sum / (double)PhasesSeen : 0.0);
      printf("        P(converge|k):");
      /*
       * Reference: with at least one case (ii) process the tossers
       * must all land on the value THAT process already holds, so
       * 2^-k.  When every correct process tossed there is no such
       * anchor and either value will do, so 2^-(k-1).  Measured, not
       * assumed -- see the printed pairs.
       */
      r = (arm == ARM_ASYNC) ? n : n - t;
      for (k = 0; k <= n && k <= 6; ++k)
        if (PhaseK[k])
          printf("  k=%u %.4f (ref %.4f, phases %lu)", k,
                 (double)PhaseKConv[k] / (double)PhaseK[k],
                 (!k ? 1.0
                  : 1.0 / (double)(1UL << (k == r ? k - 1 : k))), PhaseK[k]);
      if (PhaseK[r])
        printf("   [k=%u is all-correct: ref is 2^-(k-1)]", r);
      printf("\n");
    }
    printf("\n");
  }

  /*
   * The formula over ABAP's range, evaluated: for each t, the budget
   * the adversary's rho demands at n = 3t+1 (Bracha's floor), at
   * n = t*t (Theorem 3's sizing; below t = 4 that is under the floor
   * and the column repeats the floor), and at n = 256 (the encoding's
   * ceiling), and the smallest n at which the budget fits inside
   * BRACHA87_MAX_PHASES at each target.  "-" is a budget past 85,
   * which the encoding refuses.  maxPhases = 1 + ceil(ln eps /
   * ln(1 - rho)): the first coin is phase 0's, coins 0..P-2 all
   * failing is what leaves phases 0..P-1 undecided, and a phase
   * where some decide and the rest adopt is a coin with no toss --
   * a converged one -- so the last decider needs no phase of its own.
   */
  printf("=== the formula over ABAP's range: rho = 2^-m * sum_{k<=ceil(m/2)-t-1} C(m,k), "
         "m = n-t ===\n");
  printf("  %-3s %-18s %-18s %-18s %-14s %-14s\n", "t",
         "n=3t+1: rho/mp6/mp9", "n=max(t*t,3t+1)", "n=256: rho/mp6/mp9",
         "n: all above fit mp6", "mp9");
  for (t = 1; t <= 16; ++t) {
    static const unsigned int Which[3] = { 0, 1, 2 };
    unsigned int nn;
    unsigned int fit6;
    unsigned int fit9;
    double m6;
    double m9;

    printf("  %-3u", t);
    for (k = 0; k < 3; ++k) {
      nn = Which[k] == 0 ? 3 * t + 1 : Which[k] == 1 ? t * t : 256;
      if (nn < 3 * t + 1)
        nn = 3 * t + 1;
      p = advRho(nn - t, t, 0);
      m6 = ceil(1.0 + log(1e-6) / log(1.0 - p));
      m9 = ceil(1.0 + log(1e-9) / log(1.0 - p));
      if (m6 <= MAX_PHASES && m9 <= MAX_PHASES)
        printf(" %-6.4f %4.0f  %4.0f  ", p, m6, m9);
      else if (m6 <= MAX_PHASES)
        printf(" %-6.4f %4.0f  %4s  ", p, m6, "-");
      else
        printf(" %-6.4f %4s  %4s  ", p, "-", "-");
    }
    /* the smallest n above which EVERY larger n also fits: the budget
     * is not monotone in n (the parity of m and t moves the band's
     * edges), so a first fit could name an n whose neighbor fails */
    fit6 = fit9 = 0;
    for (nn = 256; nn >= 3 * t + 1; --nn) {
      p = advRho(nn - t, t, 0);
      if (ceil(1.0 + log(1e-6) / log(1.0 - p)) <= MAX_PHASES)
        fit6 = nn;
      else
        break;
    }
    for (nn = 256; nn >= 3 * t + 1; --nn) {
      p = advRho(nn - t, t, 0);
      if (ceil(1.0 + log(1e-9) / log(1.0 - p)) <= MAX_PHASES)
        fit9 = nn;
      else
        break;
    }
    if (fit6)
      printf(" %-14u", fit6);
    else
      printf(" %-14s", "-");
    if (fit9)
      printf(" %-14u\n", fit9);
    else
      printf(" %-14s\n", "-");
  }

  /*
   * The same table for the deployment model (arm I, the faulty's
   * own-vote band with the adaptive force; arm H's two-tail figure
   * beside it), at q = 0.
   */
  printf("\n=== the deployment model over ABAP's range: rhoI = 2^-m * sum_{k>=floor((m+t)/2)+1} C(m,k), "
         "m = n-t (rhoH adds the low tail) ===\n");
  printf("  %-3s %-26s %-26s %-26s %-14s %-14s\n", "t",
         "n=3t+1: rhoH rhoI mp6 mp9", "n=max(t*t,3t+1)", "n=256: rhoH rhoI mp6 mp9",
         "n: all above fit mp6", "mp9");
  for (t = 1; t <= 16; ++t) {
    unsigned int nn;
    unsigned int fit6;
    unsigned int fit9;
    double m6;
    double m9;

    printf("  %-3u", t);
    for (k = 0; k < 3; ++k) {
      nn = k == 0 ? 3 * t + 1 : k == 1 ? t * t : 256;
      if (nn < 3 * t + 1)
        nn = 3 * t + 1;
      p = depRho(nn - t, t, 0);
      m6 = ceil(1.0 + log(1e-6) / log(1.0 - p));
      m9 = ceil(1.0 + log(1e-9) / log(1.0 - p));
      printf(" %-6.4f %-6.4f", depRho(nn - t, t, 1), p);
      if (m6 <= MAX_PHASES)
        printf(" %4.0f ", m6);
      else
        printf(" %4s ", "-");
      if (m9 <= MAX_PHASES)
        printf(" %4.0f  ", m9);
      else
        printf(" %4s  ", "-");
    }
    fit6 = fit9 = 0;
    for (nn = 256; nn >= 3 * t + 1; --nn) {
      p = depRho(nn - t, t, 0);
      if (ceil(1.0 + log(1e-6) / log(1.0 - p)) <= MAX_PHASES)
        fit6 = nn;
      else
        break;
    }
    for (nn = 256; nn >= 3 * t + 1; --nn) {
      p = depRho(nn - t, t, 0);
      if (ceil(1.0 + log(1e-9) / log(1.0 - p)) <= MAX_PHASES)
        fit9 = nn;
      else
        break;
    }
    if (fit6)
      printf(" %-14u", fit6);
    else
      printf(" %-14s", "-");
    if (fit9)
      printf(" %-14u\n", fit9);
    else
      printf(" %-14s\n", "-");
  }

  /*
   * The vote band held: what a release discipline with ZERO exposure
   * buys (arm H, both tails, no forcing at all), as a budget.  The
   * per-phase figure is not monotone in N, so each row takes the WORST
   * N from max(t*t, 3t+1) to 256 and the budget that N implies; the
   * last line is the worst row, the one number a deployment sized
   * anywhere in that range can quote.  Arm I's column beside it is
   * what any exposure at all returns the deployment to.
   */
  printf("\n=== the vote band held (arm H): the worst per-phase figure over N in"
         " [max(t*t, 3t+1), 256] and its budget; arm I beside it ===\n");
  printf("  %-3s %-4s %-8s %-5s %-5s | %-8s %-5s %-5s\n", "t", "N0",
         "rhoH", "mp6", "mp9", "rhoI", "mp6", "mp9");
  {
    double worstH;
    double worstI;
    double wh;
    double wi;
    unsigned int nn;
    unsigned int n0;
    unsigned int mh6;
    unsigned int mh9;
    unsigned int mi6;
    unsigned int mi9;
    unsigned int allH6;
    unsigned int allH9;

    allH6 = allH9 = 0;
    for (t = 1; t <= 16; ++t) {
      n0 = (t * t > 3 * t + 1) ? t * t : 3 * t + 1;
      worstH = worstI = 1.0;
      for (nn = n0; nn <= 256; ++nn) {
        wh = depRho(nn - t, t, 1);
        wi = depRho(nn - t, t, 0);
        if (wh < worstH)
          worstH = wh;
        if (wi < worstI)
          worstI = wi;
      }
      mh6 = (unsigned int)ceil(1.0 + log(1e-6) / log(1.0 - worstH));
      mh9 = (unsigned int)ceil(1.0 + log(1e-9) / log(1.0 - worstH));
      mi6 = (unsigned int)ceil(1.0 + log(1e-6) / log(1.0 - worstI));
      mi9 = (unsigned int)ceil(1.0 + log(1e-9) / log(1.0 - worstI));
      if (mh6 > allH6)
        allH6 = mh6;
      if (mh9 > allH9)
        allH9 = mh9;
      printf("  %-3u %-4u %-8.4f %-5u %-5u | %-8.4f %-5u %-5u\n", t, n0, worstH, mh6, mh9,
             worstI, mi6, mi9);
    }
    printf("  worst over t = 1..16 with the vote band held: maxPhases %u at 1e-6, %u at 1e-9\n",
           allH6, allH9);
  }
  return (0);
}
