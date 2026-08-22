/*
 * asynchronousByzantineAgreementProtocols - BKR94 Asynchronous Common Subset
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
 * BKR94 Asynchronous Common Subset -- Protocol Agreement[Q].
 *
 * This file is BKR94 Section 4 Figure 3.  Direct line-by-line port
 * of Ben-Or/Kelmer/Rabin 1994; see BKR94ACS.txt for the paper
 * extract used as the spec.  Composes N Bracha87 Fig1 instances
 * (A-Cast reliable broadcast supplying Q) with N Bracha87 Fig4
 * instances (binary BA on inclusion -- the BKR94 "BA"
 * subprotocol).
 *
 * Step 1 lives in bkr94acsAcastInput (enter 1 on Fig1 ACCEPT).
 * Step 2 lives on the BPR sweep: bkr94acsFanoutDuty classifies
 *   (HELD / TOLERANCE / MET) and bkr94acsFanout enters 0 into every
 *   unentered BA once the 2t+1-BAs-with-output-1 count holds and
 *   the caller's tolerance budget elapses.  The paper's "upon" is
 *   enabling evidence, not a moment; see the bkr94acs.dtc Step 2
 *   section.
 * The BA round turn lives on the same sweep: bkr94acsBaInput only
 *   BANKS evidence (Fig1 traffic, Fig3 store/validate/cascade);
 *   bkr94acsTurnDuty classifies each BA's next round and
 *   bkr94acsTurn computes it -- Bracha Fig4's "wait until validate
 *   n-t k-messages" is the same enabling-evidence reading, and the
 *   sample a turn consumes is still growing when it first suffices.
 * Step 3 lives in bkr94acsTurn (BKR94ACS_ACT_COMPLETE when the
 *   turn decides the last undecided BA) and bkr94acsSubset
 *   (SubSet = { j : BA_j = 1 }).
 */

#include <assert.h>
#include <string.h>
#include "bkr94acs.h"

#define A_N(a) ((unsigned int)(a)->n + 1)

/*
 * Pointer alignment for carving Fig1/Fig4 instances out of a single
 * buffer.  bracha87Fig4 begins with function-pointer fields, and
 * bracha87Fig1 begins with 2-byte shorts; rounding up to pointer
 * alignment subsumes both.  The struct bkr94acs header is 8 bytes
 * so a->data is already pointer-aligned without an explicit pad
 * field; the BKR94ACS_ALIGN_UP applications below pad each carved
 * sub-region up to the same boundary.
 */
#define BKR94ACS_ALIGN_P  ((unsigned long)sizeof (void *))
#define BKR94ACS_ALIGN_UP(x)  (((unsigned long)(x) + BKR94ACS_ALIGN_P - 1) \
                               & ~(BKR94ACS_ALIGN_P - 1))

/*--------------------------------------------------------------------------*/
/*  Internal layout helpers                                                 */
/*                                                                          */
/*  data[] layout (N = n + 1, MR = maxPhases * 3, all sub-sizes rounded     */
/*  up to pointer alignment):                                               */
/*    entered[N]       per-process enter status                             */
/*    baDecision[N]    per-process BA decision                              */
/*    baNextRound[N]   per-process next BA round to check                   */
/*    (alignment pad to pointer alignment)                                  */
/*    acastFig1 area   N * acastF1Sz bytes                                  */
/*    baPipeline area  N * (MR * N * baF1Sz + baF4Sz) bytes                 */
/*--------------------------------------------------------------------------*/

/* Per-process enter status */
#define BKR94ACS_ENTER_NONE 0
#define BKR94ACS_ENTER_ONE  1
#define BKR94ACS_ENTER_ZERO 2

/* ACS-event discriminator for the bkr94acsRules.c dispatch.  See
 * bkr94acsToC.dtc; the values are opaque tokens compared by equality. */
#define BKR94ACS_ACS_EVENT_Q   0
#define BKR94ACS_ACS_EVENT_BA0 1
#define BKR94ACS_ACS_EVENT_BA1 2

/*
 * All layout helpers take const struct bkr94acs * -- they only read
 * the header fields n/vLen/maxPhases to compute offsets.  Callers
 * that need to write through the returned pointer cast away const.
 * This lets the read-only query functions (bkr94acsSubset,
 * bkr94acsAcastValue) stay const-correct.
 */

static unsigned char *
bkr94acsEnterd(
  const struct bkr94acs *a
){
  return ((unsigned char *)a->data);
}

static unsigned char *
bkr94acsDecision(
  const struct bkr94acs *a
){
  return ((unsigned char *)a->data + A_N(a));
}

static unsigned char *
bkr94acsNextRound(
  const struct bkr94acs *a
){
  return ((unsigned char *)a->data + 2 * A_N(a));
}

/*
 * Size of the entered/baDecision/baNextRound header, padded up to
 * pointer alignment so that acastF1Base is aligned.
 */
static unsigned long
headerSz(
  const struct bkr94acs *a
){
  return (BKR94ACS_ALIGN_UP(3UL * A_N(a)));
}

static unsigned long
acastF1Sz(
  const struct bkr94acs *a
){
  return (BKR94ACS_ALIGN_UP(bracha87Fig1Sz(a->n, a->vLen)));
}

static unsigned long
baF1Sz(
  const struct bkr94acs *a
){
  return (BKR94ACS_ALIGN_UP(bracha87Fig1Sz(a->n, 0)));
}

static unsigned long
baF4Sz(
  const struct bkr94acs *a
){
  return (BKR94ACS_ALIGN_UP(bracha87Fig4Sz(a->n, a->maxPhases)));
}

static unsigned int
maxRounds(
  const struct bkr94acs *a
){
  return (a->maxPhases * 3);
}

/* Base of A-Cast Fig1 area */
static unsigned char *
acastF1Base(
  const struct bkr94acs *a
){
  return ((unsigned char *)a->data + headerSz(a));
}

/* A-Cast Fig1 instance for process j */
static struct bracha87Fig1 *
acastF1(
  const struct bkr94acs *a
 ,unsigned int j
){
  return ((struct bracha87Fig1 *)(acastF1Base(a) + j * acastF1Sz(a)));
}

/* Base of BA pipeline area */
static unsigned char *
baBase(
  const struct bkr94acs *a
){
  return (acastF1Base(a) + A_N(a) * acastF1Sz(a));
}

/* Size of one BA pipeline (for process j) */
static unsigned long
baPipelineSz(
  const struct bkr94acs *a
){
  return ((unsigned long)maxRounds(a) * A_N(a) * baF1Sz(a) + baF4Sz(a));
}

/* BA Fig1 instance for process j, round r, initiator k */
static struct bracha87Fig1 *
baF1(
  const struct bkr94acs *a
 ,unsigned int j
 ,unsigned int r
 ,unsigned int k
){
  unsigned char *base;

  base = baBase(a) + j * baPipelineSz(a);
  return ((struct bracha87Fig1 *)(base
    + ((unsigned long)r * A_N(a) + k) * baF1Sz(a)));
}

/* BA Fig4 instance for process j */
static struct bracha87Fig4 *
baF4(
  const struct bkr94acs *a
 ,unsigned int j
){
  unsigned char *base;

  base = baBase(a) + j * baPipelineSz(a);
  return ((struct bracha87Fig4 *)(base
    + (unsigned long)maxRounds(a) * A_N(a) * baF1Sz(a)));
}


/*--------------------------------------------------------------------------*/
/*  Public API                                                              */
/*--------------------------------------------------------------------------*/

unsigned long
bkr94acsSz(
  unsigned int n
 ,unsigned int vLen
 ,unsigned int maxPhases
){
  unsigned int N;
  unsigned long pf1;
  unsigned long cf1;
  unsigned long cf4;
  unsigned long mr;
  unsigned long hdr;
  unsigned long baPipe;

  /*
   * Encoded limits: n and vLen carry actual_count - 1 in an unsigned
   * char, so they must fit 0..255.  maxPhases is bounded by the
   * unsigned-char round counter (3 * maxPhases <= 255).
   */
  if (n > 255 || vLen > 255 || maxPhases == 0
   || maxPhases > BRACHA87_MAX_PHASES)
    return (0);

  N = n + 1;
  pf1 = BKR94ACS_ALIGN_UP(bracha87Fig1Sz(n, vLen));
  cf1 = BKR94ACS_ALIGN_UP(bracha87Fig1Sz(n, 0));
  cf4 = BKR94ACS_ALIGN_UP(bracha87Fig4Sz(n, maxPhases));
  mr = (unsigned long)maxPhases * 3;
  hdr = BKR94ACS_ALIGN_UP(3UL * N);
  baPipe = mr * N * cf1 + cf4;

  return (sizeof (struct bkr94acs) - 1
    + hdr                      /* entered + baDecision + baNextRound + pad */
    + N * pf1                  /* A-Cast Fig1 instances */
    + N * baPipe);            /* BA pipelines */
}

void
bkr94acsInit(
  struct bkr94acs *a
 ,unsigned char n
 ,unsigned char t
 ,unsigned char vLen
 ,unsigned char maxPhases
 ,unsigned char self
 ,bracha87CoinFn coin
 ,void *coinClosure
){
  unsigned int N;
  unsigned int i;
  unsigned int j;
  unsigned int r;

  /*
   * Validate caller contract.  bkr94acsSz rejects the same
   * out-of-range maxPhases by returning 0; if bkr94acsInit proceeded
   * past these checks it would memcpy into fields beyond the
   * allocation and the Fig4 step 3 path would crash on the missing
   * coin.  An out-of-range self would index BA Fig1s past the
   * allocation on the first enter (baF1 initiator = self).  Bracha
   * also requires actual_N > 3t (asserted in each Fig*Init below,
   * but checking here gives a cleaner failure mode).
   */
  if (!coin)
    return;
  if (!maxPhases || maxPhases > BRACHA87_MAX_PHASES)
    return;
  if (self > n)
    return;
  assert((unsigned int)n + 1 > 3u * t);

  memset(a, 0, bkr94acsSz(n, vLen, maxPhases));
  a->n = n;
  a->t = t;
  a->vLen = vLen;
  a->maxPhases = maxPhases;
  a->self = self;

  N = n + 1;

  /* Mark all BA decisions as undecided */
  memset(bkr94acsDecision(a), 0xFF, N);

  /* Initialize A-Cast Fig1 instances */
  for (i = 0; i < N; ++i)
    bracha87Fig1Init(acastF1(a, i), n, t, vLen);

  /* Initialize BA pipelines */
  {
    unsigned int mr2;

    mr2 = maxPhases * 3;
    for (i = 0; i < N; ++i) {
      for (r = 0; r < mr2; ++r)
        for (j = 0; j < N; ++j)
          bracha87Fig1Init(baF1(a, i, r, j), n, t, 0);
      bracha87Fig4Init(baF4(a, i), n, t, maxPhases, 0,
                       coin, coinClosure);
    }
  }
}

/*
 * Internal: enter this process's input value into BA_self.
 *
 * Called from both BKR94 Step 1 (enter=1 on Fig1 ACCEPT) and BKR94
 * Step 2 (enter=0 when the caller fires bkr94acsFanout from the
 * BPR sweep).  The
 * entered[] guard enforces the paper's single-input-per-BA rule:
 * "Once a BA has received an input from Pi (1 from step 1 or 0
 * from step 2), step 1 and step 2 stop touching it -- BA semantics
 * demand a single input per player."  First caller wins.
 *
 * Returns number of BKR94ACS_ACT_BA_SEND actions added (0 if
 * already entered, 1 otherwise).
 */
static unsigned int
bkr94acsEnter(
  struct bkr94acs *a
 ,unsigned int process
 ,unsigned char enter
 ,struct bkr94acsAct *out
){
  unsigned char *entered;

  entered = bkr94acsEnterd(a);
  if (entered[process] != BKR94ACS_ENTER_NONE)
    return (0);

  entered[process] = enter ? BKR94ACS_ENTER_ONE : BKR94ACS_ENTER_ZERO;

  /*
   * Initial broadcast of our BA input.  Mark the corresponding
   * (process, round=0, initiator=self) BA Fig1 as the
   * initiator and store the value, so bkr94acsRetry's BPR walk
   * keeps outputting BKR94ACS_ACT_BA_SEND with .type=INITIAL for
   * as long as F1_INITIATOR is set on that Fig1 (Implementation
   * Note 11); once F1_ECHOED is set, BA_SEND/ECHO joins the
   * stream alongside it, and once F1_RDSENT is set, BA_SEND/
   * READY joins too -- all three streams retry independently
   * while their flags hold.  BA Fig1s are vLen=0 (binary
   * value), so we pass the raw enter byte (0 or 1), not the
   * BKR94ACS_ENTER_* encoding stored in entered[].
   */
  {
    unsigned char binary;

    binary = enter ? 1 : 0;
    bracha87Fig1Initiator(baF1(a, process, 0, a->self), &binary);
  }

  out->value = 0;
  out->skip = 0;            /* fresh initiation: nothing to suppress yet */
  out->act = BKR94ACS_ACT_BA_SEND;
  out->process = process;
  out->round = 0;
  out->type = BRACHA87_INITIAL;
  out->baValue = enter;
  out->initiator = a->self;
  out->accepted = 0;
  return (1);
}

unsigned int
bkr94acsAcastInput(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char type
 ,unsigned char from
 ,const unsigned char *value
 ,struct bkr94acsAct *out
){
  struct bracha87Fig1 *f1;
  unsigned char f1out[3];
  unsigned int nf1;
  unsigned int nact;
  unsigned int k;
  unsigned char acsEvent;
  unsigned char inputToBAj;
  unsigned char postCountAllN;
  unsigned char doInput1;
  unsigned char doOutputSubset;

  /*
   * Encoded: a->n = actual_N - 1, so valid process indices are
   * 0..a->n inclusive.  "> a->n" rejects actual_N and above.
   *
   * Do NOT short-circuit on a->complete.  This process has locally
   * decided all N BAs, but other processes may still be working on
   * some BAs and depend on THIS process's continued Fig1 echoes and
   * readys to reach their own n-t thresholds.  Bracha requires
   * post-decide continuation at the BA level (pitfall #1); the
   * same obligation applies at the BKR94 ACS level -- a
   * locally-complete process must keep participating until the
   * application decides to exit (e.g. the barren-sweep gate).
   * A blanket complete-guard causes classic post-decide stalls
   * where the fastest process strands the slowest.  Step 1's enter
   * below is idempotent (bkr94acsEnter's entered-state dedup), and
   * the terminal actions live in bkr94acsTurn, gated there by
   * Fig4's once-only DECIDE, so continuing after complete cannot
   * output duplicates.
   */
  if (!a || process > a->n || from > a->n || !value || !out)
    return (0);

  /*
   * INITIAL is the designated initiator's message: only the process
   * may send (initial, v) for its own A-Cast.  ECHO/READY arrive
   * legitimately from any process (from != process is normal and is
   * sender-deduped in bracha87Fig1Input), but a non-process INITIAL is
   * a forged A-Cast -- a Byzantine process injecting a value the correct
   * process never broadcast, which the (n+t)/2+1 echo cascade would then
   * carry to a false ACCEPT.  Authenticated channels bind 'from' to the
   * true sender but do NOT bind the message's process field to it (process
   * != from is a valid ECHO/READY), so this is a protocol-semantic check
   * that must live here, not in the transport.  Drop the message.
   */
  if (type == BRACHA87_INITIAL && from != process)
    return (0);

  f1 = acastF1(a, process);
  nf1 = bracha87Fig1Input(f1, type, from, value, f1out);
  nact = 0;

  for (k = 0; k < nf1; ++k) {
    if (f1out[k] == BRACHA87_ECHO_ALL || f1out[k] == BRACHA87_READY_ALL) {
      out[nact].value = bracha87Fig1Value(f1);
      out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
      out[nact].act = BKR94ACS_ACT_ACAST_SEND;
      out[nact].process = process;
      out[nact].round = 0;
      out[nact].type = (f1out[k] == BRACHA87_ECHO_ALL)
                       ? BRACHA87_ECHO : BRACHA87_READY;
      out[nact].baValue = 0;
      out[nact].initiator = 0;
      out[nact].accepted = (f1out[k] == BRACHA87_READY_ALL
                         && (f1->flags & BRACHA87_F1_ACCEPTED)) ? 1 : 0;
      ++nact;
    } else if (f1out[k] == BRACHA87_ACCEPT) {
      /*
       * Self-accept: record our own accept in the A-Cast Fig1's
       * acFrom so the READY quiescence count can reach n (the Fig1
       * does not know self; bkr94acs routes by it).  bkr94acsAcast-
       * Accepted feeds processes' accepts here from the wire ACCEPTED bit.
       */
      bracha87Fig1ProcessAccepted(f1, a->self);
      /*
       * BKR94 Step 1: "For each Pj for whom you (Pi) know Q(j) = 1,
       * participate in BA_j with input 1."  Q(j) = 1 is carried by
       * Fig1 ACCEPT for process j (Bracha87 Lemma 4 gives BKR94's Q
       * assumption (2) for free).  Step 2 is not dispatched from any
       * arrival path -- it fires from the BPR sweep (bkr94acsFanout);
       * only Step 1 is reachable at this dispatch site, but the
       * bridge sees the Step 1 + Step 3 rule set so the unreachable
       * Step 3 output is zeroed by the dispatch and the post-include
       * applies only doInput1.
       */
      acsEvent = BKR94ACS_ACS_EVENT_Q;
      inputToBAj = bkr94acsEnterd(a)[process];
      postCountAllN = 0;
      doInput1 = 0;
      doOutputSubset = 0;
#include "bkr94acsRules.c"
      /* Step 3's output is unreachable on a Q event; the dispatch
       * still resolves it to 0 at every leaf. */
      (void)doOutputSubset;
      if (doInput1)
        nact += bkr94acsEnter(a, process, 1, &out[nact]);
    }
  }

  return (nact);
}

unsigned int
bkr94acsBaInput(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char type
 ,unsigned char from
 ,unsigned char value
 ,struct bkr94acsAct *out
){
  unsigned char *pipe;
  unsigned long bf1sz;
  unsigned long f4off;
  unsigned int N;
  unsigned int mr;
  struct bracha87Fig1 *f1;
  struct bracha87Fig3 *f3;
  struct bracha87Fig4 *f4;
  unsigned char f1out[3];
  unsigned int nf1;
  unsigned int nact;
  unsigned int k;

  if (!a || process > a->n || initiator > a->n || from > a->n || !out)
    return (0);
  if (round >= maxRounds(a))
    return (0);

  /*
   * BA INITIAL is the initiator's message: only 'initiator'
   * may send (initial, v) for the (process, round, initiator) Fig1.
   * Same forged-A-Cast defense as bkr94acsAcastInput -- a non-
   * initiator INITIAL would steer this BA toward a value the correct
   * initiator never A-Cast.  ECHO/READY from any process are normal.
   */
  if (type == BRACHA87_INITIAL && from != initiator)
    return (0);

  /*
   * Two intentional non-short-circuits -- both are Bracha post-decide
   * continuation (pitfall #1) applied at different layers.
   *
   * 1. We do NOT short-circuit on bkr94acsDecision[process] != 0xFF.
   *    Bracha Fig4 requires a decided process to continue
   *    broadcasting so processes lagging in THIS BA can reach n-t
   *    validated and decide.
   *
   * 2. We do NOT short-circuit on a->complete.  A locally-complete
   *    process has decided all N BAs but other processes may still be
   *    working on some BAs.  Their Fig1 instances for (process_X,
   *    round_Y, initiator_THIS) wait on THIS process's continued
   *    echoes and readys to cross n-t thresholds.  Dropping inputs
   *    after local complete strands lagging processes -- a classic
   *    post-decide stall.
   *    Application exit is a separate concern (see the
   *    barren-sweep gate).
   */

  /* Compute pipeline base once -- avoids redundant external calls */
  N = A_N(a);
  mr = maxRounds(a);
  bf1sz = baF1Sz(a);
  f4off = (unsigned long)mr * N * bf1sz;
  pipe = baBase(a) + process * baPipelineSz(a);
  f1 = (struct bracha87Fig1 *)(pipe
    + ((unsigned long)round * N + initiator) * bf1sz);
  f4 = (struct bracha87Fig4 *)(pipe + f4off);
  f3 = &f4->fig3;
  nact = 0;

  nf1 = bracha87Fig1Input(f1, type, from, &value, f1out);

  for (k = 0; k < nf1; ++k) {
    if (f1out[k] == BRACHA87_ACCEPT) {
      const unsigned char *cv;

      cv = bracha87Fig1Value(f1);
      if (!cv)
        continue;

      /*
       * Self-accept: record our own accept in this BA Fig1's
       * acFrom (round, initiator) so its READY quiescence count can
       * reach n.  Processes' accepts arrive via bkr94acsBaAccepted.
       */
      bracha87Fig1ProcessAccepted(f1, a->self);

      /*
       * Fig3 sender is the initiator (whose broadcast was accepted).
       * The arrival path ends here: the accept is BANKED -- stored,
       * validated, cascaded (bracha87Fig3Accept holds every message
       * and re-derives validity as prior rounds grow) -- but no
       * round is turned.  Fig4's "wait until validate n-t
       * k-messages" is enabling evidence over a still-growing
       * sample, so the turn fires from the BPR sweep
       * (bkr94acsTurn), caller-paced; BA_DECIDED / COMPLETE /
       * BA_EXHAUSTED emerge there, never from this entry.
       */
      bracha87Fig3Accept(f3, round, initiator, cv[0], 0);
      continue;
    }

    /*
     * Non-ACCEPT: must be ECHO_ALL or READY_ALL.  bracha87Fig1Input
     * never outputs BRACHA87_INITIAL_ALL (that's a Bpr-only output);
     * the assert documents the contract so the type-mapping ternary
     * below isn't reading a stale assumption.
     */
    assert(f1out[k] == BRACHA87_ECHO_ALL || f1out[k] == BRACHA87_READY_ALL);
    {
      const unsigned char *cv;

      cv = bracha87Fig1Value(f1);
      if (!cv)
        continue;

      out[nact].value = 0;
      out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
      out[nact].act = BKR94ACS_ACT_BA_SEND;
      out[nact].process = process;
      out[nact].round = round;
      out[nact].type = (f1out[k] == BRACHA87_ECHO_ALL)
        ? BRACHA87_ECHO : BRACHA87_READY;
      out[nact].baValue = cv[0];
      out[nact].initiator = initiator;
      out[nact].accepted = (f1out[k] == BRACHA87_READY_ALL
                         && (f1->flags & BRACHA87_F1_ACCEPTED)) ? 1 : 0;
      ++nact;
    }
  }

  return (nact);
}

unsigned int
bkr94acsSubset(
  const struct bkr94acs *a
 ,unsigned char *processes
){
  unsigned int cnt;
  unsigned int i;
  const unsigned char *dec;

  if (!a || !processes)
    return (0);

  /*
   * BKR94 Step 3 read: SubSet_i = { j : BA_j had output 1 }.
   * Lemma 2 Part A gives |SubSet| >= 2t+1 = n-t; Part C gives
   * cross-process agreement on SubSet; Part D gives Q(j)=1 for every
   * j in SubSet.  Caller must gate this on a->complete to
   * observe the final subset; a mid-run read reports the partial
   * set of decided-1 processes.
   */
  dec = bkr94acsDecision(a);
  cnt = 0;
  for (i = 0; i < A_N(a); ++i) {
    if (dec[i] == 1)
      processes[cnt++] = i;
  }
  return (cnt);
}

const unsigned char *
bkr94acsAcastValue(
  const struct bkr94acs *a
 ,unsigned char process
){
  struct bracha87Fig1 *f;
  if (!a || process > a->n)
    return (0);
  f = acastF1(a, process);
  /* Header contract: returns non-null only when ACCEPTED, or for
   * self-process once INITIATOR is set (after A-Cast).  Pre-ACCEPT
   * ECHOED values are intentionally hidden -- under Byzantine
   * equivocation they can disagree across honest processes (Bracha 1987
   * Lemmas 1/2 only constrain READY/ACCEPT), so exposing them
   * would let callers act on values that aren't yet
   * agreement-protected. */
  if ((f->flags & (BRACHA87_F1_INITIATOR | BRACHA87_F1_ACCEPTED)) == 0)
    return (0);
  return (bracha87Fig1Value(f));
}

unsigned int
bkr94acsAcast(
  struct bkr94acs *a
 ,const unsigned char *value
 ,struct bkr94acsAct *out
){
  if (!a || !value || !out)
    return (0);

  /*
   * Mark the local A-Cast Fig1 (process = self) as the
   * broadcast initiator.  bracha87Fig1Initiator sets F1_INITIATOR
   * and copies value into the echoed-value slot so
   * bracha87Fig1Value returns it (without setting ECHOED --
   * Rule 1 still fires from bkr94acsAcastInput receiving
   * (initial, v) via the network loopback).
   *
   * Thereafter bkr94acsRetry keeps outputting BKR94ACS_ACT_
   * ACAST_SEND with .type=INITIAL for as long as F1_INITIATOR
   * is set (Implementation Note 11); once F1_ECHOED is set
   * by loopback or process echoes, ACAST_SEND/ECHO outputs
   * alongside it, and once F1_RDSENT is set, ACAST_SEND/
   * READY joins too -- all three streams retry independently
   * while their flags hold.
   */
  bracha87Fig1Initiator(acastF1(a, a->self), value);

  out->value = bracha87Fig1Value(acastF1(a, a->self));
  out->skip = 0;            /* fresh initiation: nothing to suppress yet */
  out->act = BKR94ACS_ACT_ACAST_SEND;
  out->process = a->self;
  out->round = 0;
  out->type = BRACHA87_INITIAL;
  out->baValue = 0;
  out->initiator = 0;
  out->accepted = 0;
  return (1);
}

/*
 * BPR retry.  Walks the cursor forward until a Fig1 instance
 * with retry output is found (or the cursor wraps back to its
 * starting position with no work).  Per-call ceiling:
 * BKR94ACS_RETRY_MAX_ACTS (3 actions = INITIAL + ECHO + READY
 * for one Fig1).
 *
 * Cursor walks two phases:
 *   phase 0: A-Cast Fig1s, process 0..N-1
 *   phase 1: BA Fig1s, (process, round, initiator)
 *            in that order, over the full round space (the
 *            sweep must reach ahead-round Fig1s that a faster
 *            process's INITIAL already drove to ECHOED).
 *
 * Per-process retry-gate (see bkr94acs.dtc BPR section) trims
 * the A-Cast walk for BAs that have decided 0 (excluded
 * from SubSet); the BA walk is unconditional, with
 * Fig1Bpr returning 0 on unsent instances.
 *
 * Internal helpers fold each cursor advance step into the
 * walker.  Plain-C three-arm switch over BA decision is the
 * BPR gate (see bkr94acs.dtc BPR documentation block); the
 * pitfall #1 reasoning is captured in the .dtc, the C
 * implementation is the trivial mechanical guard.
 */
static int
bkr94acsRetryProcessGate(
  const struct bkr94acs *a
 ,unsigned int process
){
  unsigned char dec;

  /*
   * BA decision states:
   *   0xFF -> undecided    -> retry (Q(j)=1 may still be
   *                                 learned by other processes)
   *   0    -> decided 0    -> skip (j excluded; Step 2's
   *                                 fanout already conveyed
   *                                 our enter)
   *   1    -> decided 1    -> retry (Bracha post-decide
   *                                 continuation: processes that
   *                                 have not yet observed
   *                                 Fig1 ACCEPT for j still
   *                                 need our echoes/readys)
   */
  dec = bkr94acsDecision(a)[process];
  return (dec != 0);  /* 0xFF and 1 -> retry; 0 -> skip */
}

static unsigned int
bkr94acsRetryOutputAcast(
  struct bkr94acs *a
 ,unsigned int process
 ,struct bkr94acsAct *out
){
  struct bracha87Fig1 *f1;
  const unsigned char *cv;
  unsigned char f1out[3];
  unsigned int n;
  unsigned int k;
  unsigned int nact;

  f1 = acastF1(a, process);
  n = bracha87Fig1Bpr(f1, f1out);
  if (!n)
    return (0);
  cv = bracha87Fig1Value(f1);
  if (!cv)
    return (0);

  nact = 0;
  for (k = 0; k < n; ++k) {
    out[nact].value = cv;
    out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
    out[nact].act = BKR94ACS_ACT_ACAST_SEND;
    out[nact].process = process;
    out[nact].round = 0;
    out[nact].type = (f1out[k] == BRACHA87_INITIAL_ALL)
                     ? BRACHA87_INITIAL
                   : (f1out[k] == BRACHA87_ECHO_ALL)
                     ? BRACHA87_ECHO
                   :   BRACHA87_READY;
    out[nact].baValue = 0;
    out[nact].initiator = 0;
    out[nact].accepted = (f1out[k] == BRACHA87_READY_ALL
                       && (f1->flags & BRACHA87_F1_ACCEPTED)) ? 1 : 0;
    ++nact;
  }
  return (nact);
}

static unsigned int
bkr94acsRetryOutputBa(
  struct bkr94acs *a
 ,unsigned int process
 ,unsigned int round
 ,unsigned int initiator
 ,struct bkr94acsAct *out
){
  struct bracha87Fig1 *f1;
  const unsigned char *cv;
  unsigned char f1out[3];
  unsigned int n;
  unsigned int k;
  unsigned int nact;

  f1 = baF1(a, process, round, initiator);
  n = bracha87Fig1Bpr(f1, f1out);
  if (!n)
    return (0);
  cv = bracha87Fig1Value(f1);
  if (!cv)
    return (0);

  nact = 0;
  for (k = 0; k < n; ++k) {
    out[nact].value = 0;
    out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
    out[nact].act = BKR94ACS_ACT_BA_SEND;
    out[nact].process = process;
    out[nact].round = round;
    out[nact].type = (f1out[k] == BRACHA87_INITIAL_ALL)
                     ? BRACHA87_INITIAL
                   : (f1out[k] == BRACHA87_ECHO_ALL)
                     ? BRACHA87_ECHO
                   :   BRACHA87_READY;
    out[nact].baValue = cv[0];
    out[nact].initiator = initiator;
    out[nact].accepted = (f1out[k] == BRACHA87_READY_ALL
                       && (f1->flags & BRACHA87_F1_ACCEPTED)) ? 1 : 0;
    ++nact;
  }
  return (nact);
}

unsigned int
bkr94acsRetry(
  struct bkr94acs *a
 ,struct bracha87Retry *p
 ,struct bkr94acsAct *out
){
  unsigned int N;
  unsigned int mr;
  unsigned int total;
  unsigned int idx;
  unsigned int nact;

  if (!a || !p || !out)
    return (0);

  N = A_N(a);
  mr = maxRounds(a);
  /*
   * Linear cursor space: pos in [0, N) = A-Cast Fig1 for process = pos.
   * pos in [N, N + N*mr*N) = BA Fig1, decoded as
   *   rel = pos - N
   *   process      = rel / (mr * N)
   *   round       = (rel / N) % mr
   *   initiator = rel % N
   * Same shape as bracha87Fig1RetryStep: walk forward, return first
   * instance with actions; on wrap with no actions return 0.
   */
  total = N + N * mr * N;

  for (;;) {
    if (p->pos >= total) {
      p->pos = 0;
      if (p->sweepActs == 0)
        return (0);
      p->sweepActs = 0;
    }
    idx = p->pos++;
    nact = 0;
    if (idx < N) {
      unsigned char process;

      process = idx;
      if (bkr94acsRetryProcessGate(a, process))
        nact = bkr94acsRetryOutputAcast(a, process, out);
    } else {
      unsigned int rel;
      unsigned char process;
      unsigned char round;
      unsigned char initiator;

      rel = idx - N;
      process = (rel / (mr * N));
      round = ((rel / N) % mr);
      initiator = (rel % N);
      nact = bkr94acsRetryOutputBa(a, process, round,
                                       initiator, out);
    }
    if (nact) {
      p->sweepActs += nact;
      return (nact);
    }
  }
}

/*--------------------------------------------------------------------------*/
/*  The sweep-side decisions -- BKR94 step 2 and the BA round turn          */
/*                                                                          */
/*  See the bkr94acs.dtc Step 2 and round-turn sections.  A rule's          */
/*  "upon" / "wait until" names the evidence that enables an action, not    */
/*  a moment; both decisions consume evidence that is still growing when    */
/*  it first suffices, so the caller paces each from its sweep tick,        */
/*  counting sweeps against its tolerance budget while TOLERANCE holds.     */
/*  A zero budget (fire whenever enabled) is the eager schedule an          */
/*  earlier revision hardwired into the arrival paths.                      */
/*--------------------------------------------------------------------------*/

/*
 * Lemma 2 Part A case (i) requires Step 2's enabling count to be
 * BA decides of 1 -- baDecision[] entries == 1 -- never Q(j)=1
 * events or Fig1 accepts; firing on the Q count is the wrong
 * reading of the paper (see the bkr94acs.dtc header).  The counts
 * are derived by scan, never stored (the same wrap discipline as
 * bkr94acsTurn's nDecided).
 */
unsigned char
bkr94acsFanoutDuty(
  const struct bkr94acs *a
){
  const unsigned char *entered;
  const unsigned char *dec;
  unsigned int N;
  unsigned int j;
  unsigned int one;
  unsigned int unentered;

  if (!a)
    return (BKR94ACS_DUTY_HELD);
  entered = bkr94acsEnterd(a);
  dec = bkr94acsDecision(a);
  N = A_N(a);
  one = 0;
  unentered = 0;
  for (j = 0; j < N; ++j) {
    if (dec[j] == 1)
      ++one;
    if (entered[j] == BKR94ACS_ENTER_NONE)
      ++unentered;
  }
  if (!unentered)
    return (BKR94ACS_DUTY_MET);
  if (one >= N - a->t)
    return (BKR94ACS_DUTY_TOLERANCE);
  return (BKR94ACS_DUTY_HELD);
}

unsigned int
bkr94acsFanout(
  struct bkr94acs *a
 ,struct bkr94acsAct *out
){
  unsigned int N;
  unsigned int j;
  unsigned int nact;

  if (!a || !out)
    return (0);
  /*
   * The TOLERANCE guard is the paper's floor: below the n-t
   * BA-output-1 count, entering 0 is unsound (a mass of 0-inputs
   * could force SubSet empty).  At MET nothing is unentered and
   * the loop below would output nothing; returning early keeps the
   * call cheap for a caller that fires unconditionally each sweep.
   */
  if (bkr94acsFanoutDuty(a) != BKR94ACS_DUTY_TOLERANCE)
    return (0);
  N = A_N(a);
  nact = 0;
  for (j = 0; j < N; ++j)
    nact += bkr94acsEnter(a, j, 0, &out[nact]);
  return (nact);
}

unsigned char
bkr94acsTurnDuty(
  const struct bkr94acs *a
 ,unsigned char process
){
  struct bracha87Fig4 *f4;
  struct bracha87Fig3 *f3;
  unsigned char nextRound;

  if (!a || process > a->n)
    return (BKR94ACS_DUTY_HELD);
  nextRound = bkr94acsNextRound(a)[process];
  if (nextRound >= maxRounds(a))
    return (BKR94ACS_DUTY_HELD);
  f4 = baF4(a, process);
  f3 = &f4->fig3;
  if (!bracha87Fig3RoundComplete(f3, nextRound))
    return (BKR94ACS_DUTY_HELD);
  /*
   * Count only -- the sample itself is materialized by the turn.  A
   * paced caller queries this N times per sweep, so it must not pay
   * GetValid's O(n) set walk or its 2x256-byte scratch.
   */
  if (bracha87Fig3ValidCount(f3, nextRound) >= A_N(a))
    return (BKR94ACS_DUTY_MET);
  return (BKR94ACS_DUTY_TOLERANCE);
}

unsigned int
bkr94acsTurn(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char toleranceElapsed
 ,struct bkr94acsAct *out
){
  struct bracha87Fig4 *f4;
  struct bracha87Fig3 *f3;
  unsigned char *nextRound;
  unsigned char rsnd[256];
  unsigned char rval[256];
  unsigned int rcnt;
  unsigned int act;
  unsigned int mr;
  unsigned int nact;
  unsigned char duty;
  unsigned char acsEvent;
  unsigned char inputToBAj;
  unsigned char postCountAllN;
  unsigned char doInput1;
  unsigned char doOutputSubset;

  if (!a || process > a->n || !out)
    return (0);
  duty = bkr94acsTurnDuty(a, process);
  if (duty == BKR94ACS_DUTY_HELD)
    return (0);
  if (duty == BKR94ACS_DUTY_TOLERANCE && !toleranceElapsed)
    return (0);

  mr = maxRounds(a);
  f4 = baF4(a, process);
  f3 = &f4->fig3;
  nextRound = &bkr94acsNextRound(a)[process];
  rcnt = bracha87Fig3GetValid(f3, *nextRound, rsnd, rval);
  act = bracha87Fig4Round(f4, *nextRound, rcnt, rsnd, rval);
  ++*nextRound;
  nact = 0;

  if (act & BRACHA87_DECIDE) {
    const unsigned char *dec;
    unsigned int nDecided;
    unsigned int j;

    /* BA-decided notification (always output; not part of
     * BKR94 rules, just an observability signal).  Fig4Round
     * returns DECIDE exactly once per BA (BRACHA87_F4_DECIDED is
     * set-once; the decide rule requires !haveDecided), so this
     * branch -- and the unguarded COMPLETE emission below, which
     * fires on the decided count crossing N -- is structurally
     * once-only. */
    bkr94acsDecision(a)[process] = f4->decision;
    out[nact].value = 0;
    out[nact].skip = 0;
    out[nact].act = BKR94ACS_ACT_BA_DECIDED;
    out[nact].process = process;
    out[nact].round = 0;
    out[nact].type = 0;
    out[nact].baValue = f4->decision;
    out[nact].initiator = 0;
    out[nact].accepted = 0;
    ++nact;

    /*
     * Post-output decided count, derived from baDecision[]
     * (the decision just recorded above included).  Derived,
     * not stored: a stored counter is a denormalization of
     * baDecision[], and as an unsigned char it wrapped on the
     * 256th decision, suppressing COMPLETE at 256 processes.  Only
     * 0 and 1 match -- the 0xFF (undecided) and 0xFE (exhausted)
     * sentinels fall out of the scan with no separate rule.
     * One O(N) pass per BA decision, a rare event.
     */
    dec = bkr94acsDecision(a);
    nDecided = 0;
    for (j = 0; j < A_N(a); ++j)
      if (dec[j] <= 1)
        ++nDecided;

    /*
     * BKR94 Step 3 dispatch.  The bridge maps "post-output
     * BA-output count == n" to nDecided >= n; the all-decided
     * rule (output SubSet) is guarded by that post-output
     * predicate and by acsEvent's branching on BA_1 vs BA_0.
     * Step 2 is not dispatched here -- its BA-output-1 count
     * is derived on demand by bkr94acsFanoutDuty and the
     * fanout fires from the same sweep (bkr94acsFanout).
     */
    acsEvent = f4->decision
      ? BKR94ACS_ACS_EVENT_BA1
      : BKR94ACS_ACS_EVENT_BA0;
    inputToBAj = bkr94acsEnterd(a)[process];
    postCountAllN = nDecided >= A_N(a);
    doInput1 = 0;
    doOutputSubset = 0;
#include "bkr94acsRules.c"
    /* Step 1 is unreachable on a BA-output event; the dispatch
     * still resolves doInput1 to 0 at every leaf. */
    (void)doInput1;
    if (doOutputSubset) {
      a->complete = 1;
      out[nact].value = 0;
      out[nact].skip = 0;
      out[nact].act = BKR94ACS_ACT_COMPLETE;
      out[nact].process = 0;
      out[nact].round = 0;
      out[nact].type = 0;
      out[nact].baValue = 0;
      out[nact].initiator = 0;
      out[nact].accepted = 0;
      ++nact;
    }
  }

  if ((act & BRACHA87_BROADCAST)
   && *nextRound < mr) {
    /*
     * Broadcast INITIAL for the new BA round
     * (self is initiator).  Mark the corresponding
     * (process, round=*nextRound, initiator=self)
     * BA Fig1 as the initiator and store the
     * value so bkr94acsRetry keeps retrying this INITIAL
     * on subsequent ticks while F1_INITIATOR is set, plus
     * BA_SEND/ECHO and BA_SEND/READY once F1_ECHOED /
     * F1_RDSENT join (Implementation Note 11).  Same
     * pattern as the round-0 case in bkr94acsEnter.
     */
    {
      unsigned char binary;

      binary = f4->value;
      bracha87Fig1Initiator(baF1(a, process, *nextRound, a->self),
                         &binary);
    }

    out[nact].value = 0;
    out[nact].skip = 0;     /* fresh initiation: nothing to suppress */
    out[nact].act = BKR94ACS_ACT_BA_SEND;
    out[nact].process = process;
    out[nact].round = *nextRound;
    out[nact].type = BRACHA87_INITIAL;
    out[nact].baValue = f4->value;
    out[nact].initiator = a->self;
    out[nact].accepted = 0;
    ++nact;
  }

  if (act & BRACHA87_EXHAUSTED) {
    /*
     * Fig4 reached maxPhases with no decision -- a violation
     * of BKR94 Lemma 2 Part B's "all BAs terminate" assumption.
     * Bracha87 Fig4 has probabilistic termination; the
     * BRACHA87_MAX_PHASES (85) ceiling is an unsigned-char
     * round-encoding artifact, not a paper limit, but it caps
     * the in-protocol attempts.
     *
     * Mutually exclusive with BRACHA87_DECIDE (decideV requires
     * !haveDecided, EXHAUSTED requires sub=2 of last phase with
     * !haveDecided && !decideV), so the Step 3 dispatch above
     * did not fire.  The 0xFE sentinel written below does not
     * match the decided-count scan's <= 1 predicate, so an
     * exhausted BA never counts as decided (no decision was
     * made); we do NOT enter the rules dispatch (no BA-output
     * event to feed it), do NOT substitute a value into
     * bkr94acsDecision[process] -- any unilateral substitute
     * could disagree with another process's actual decision and
     * break SubSet agreement (Lemma 2 Part C).
     *
     * baDecision[process] = 0xFE marks the BA as exhausted so
     * bkr94acsBaDecision can report the state and the
     * process-retry-gate (which only skips decided-0) keeps
     * retrying for this process -- other processes may
     * still benefit from our continued echoes / readys on
     * earlier rounds.
     *
     * After ++*nextRound above, *nextRound equals mr, so
     * bkr94acsTurnDuty reports HELD for this BA forever.  Single
     * output per BA per ACS instance is therefore structural; no
     * additional dedup guard required.
     */
    bkr94acsDecision(a)[process] = 0xFE;
    out[nact].value = 0;
    out[nact].skip = 0;
    out[nact].act = BKR94ACS_ACT_BA_EXHAUSTED;
    out[nact].process = process;
    out[nact].round = 0;
    out[nact].type = 0;
    out[nact].baValue = 0;
    out[nact].initiator = 0;
    out[nact].accepted = 0;
    ++nact;
  }

  return (nact);
}

/*--------------------------------------------------------------------------*/
/*  ACCEPTED-annotation ingress (BPR per-process READY retire)              */
/*                                                                          */
/*  The transport decodes the BKR94ACS_ACCEPTED wire bit off a received     */
/*  READY and routes it here: 'from' (the message sender) has accepted      */
/*  the named Fig1 instance, so this process retires its per-process READY  */
/*  retry to 'from' (bracha87Fig1Skip(READY) = acFrom).  Call AFTER the     */
/*  matching bkr94acs*Input for the same READY (which records rdFrom);      */
/*  acFrom is then a subset of rdFrom.  Out-of-range indices are ignored.   */
/*--------------------------------------------------------------------------*/

void
bkr94acsAcastAccepted(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char from
){
  if (!a || process > a->n || from > a->n)
    return;
  bracha87Fig1ProcessAccepted(acastF1(a, process), from);
}

void
bkr94acsBaAccepted(
  struct bkr94acs *a
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char from
){
  if (!a || process > a->n || round >= maxRounds(a)
   || initiator > a->n || from > a->n)
    return;
  bracha87Fig1ProcessAccepted(baF1(a, process, round, initiator), from);
}

/*--------------------------------------------------------------------------*/
/*  Diagnostic accessors                                                    */
/*--------------------------------------------------------------------------*/

unsigned char
bkr94acsBaDecision(
  const struct bkr94acs *a
 ,unsigned char process
){
  if (!a || process > a->n)
    return (0xFF);
  return (bkr94acsDecision(a)[process]);
}

unsigned int
bkr94acsBaEntered(
  const struct bkr94acs *a
 ,unsigned char process
){
  if (!a || process > a->n)
    return (0);
  return (bkr94acsEnterd(a)[process] != BKR94ACS_ENTER_NONE);
}

unsigned int
bkr94acsBaGetValid(
  const struct bkr94acs *a
 ,unsigned char process
 ,unsigned char *senders
 ,unsigned char *values
){
  unsigned char nextRound;

  if (!a || process > a->n || !senders || !values)
    return (0);
  /*
   * The BA's next round is the one bkr94acsTurnDuty classifies and
   * bkr94acsTurn consumes; at maxRounds the round space is exhausted
   * and there is no next round to answer for (bracha87Fig3GetValid
   * would answer 0 for the out-of-range round anyway).
   */
  nextRound = bkr94acsNextRound(a)[process];
  if (nextRound >= maxRounds(a))
    return (0);
  return (bracha87Fig3GetValid(&baF4(a, process)->fig3, nextRound,
                               senders, values));
}

unsigned int
bkr94acsAcastAllEchoed(
  const struct bkr94acs *a
 ,unsigned char process
){
  if (!a || process > a->n)
    return (0);
  return (bracha87Fig1AllEchoed(acastF1(a, process)));
}

const unsigned char *
bkr94acsAcastSkip(
  const struct bkr94acs *a
 ,unsigned char process
){
  if (!a || process > a->n)
    return (0);
  return (bracha87Fig1Skip(acastF1(a, process),
                           BRACHA87_INITIAL_ALL));
}

unsigned int
bkr94acsSentFig1Count(
  const struct bkr94acs *a
){
  unsigned int N;
  unsigned int mr;
  unsigned int process;
  unsigned int round;
  unsigned int initiator;
  unsigned int count;
  unsigned char sentMask;
  const struct bracha87Fig1 *f1;

  if (!a)
    return (0);

  /*
   * Sent = any of F1_INITIATOR / F1_ECHOED / F1_RDSENT set.
   * These are the flags that drive Bpr retry output; F1_ACCEPTED
   * piggybacks on F1_RDSENT remaining set post-accept (pitfall 10),
   * so it is implicitly counted.
   */
  sentMask = (BRACHA87_F1_ECHOED
                                | BRACHA87_F1_RDSENT
                                | BRACHA87_F1_INITIATOR);

  N = A_N(a);
  mr = maxRounds(a);
  count = 0;

  for (process = 0; process < N; ++process) {
    f1 = acastF1(a, process);
    if (f1->flags & sentMask)
      ++count;
  }

  /*
   * Walk the full round space, not just rounds below this process's
   * baNextRound[process]: a faster process's INITIAL for a round this
   * process's BA has not yet entered fires Rule 1 here, so ahead-round
   * Fig1s can be ECHOED (sent) while baNextRound lags.  The
   * retry sweep visits them; capping the count on baNextRound would
   * undercount the very sweep this accessor is meant to size.
   */
  for (process = 0; process < N; ++process) {
    for (round = 0; round < mr; ++round) {
      for (initiator = 0; initiator < N; ++initiator) {
        f1 = baF1(a,
                   process,
                   round,
                   initiator);
        if (f1->flags & sentMask)
          ++count;
      }
    }
  }

  return (count);
}

