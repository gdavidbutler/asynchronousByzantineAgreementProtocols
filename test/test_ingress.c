/*
 * test_ingress.c
 *
 * The ingress contract under an adversary who holds this library.
 *
 * An honest deployment decodes a hostile peer's bytes and calls the
 * library with the fields that decode produced.  This walks the whole
 * field space of every entry a message can reach and asserts the two
 * halves of the contract: an argument the headers do not admit is
 * REFUSED and leaves the receiver byte-identical, and any call at all
 * leaves the receiver's state within its documented domains and emits
 * only well-formed acts.  Never crashed, never corrupted.
 *
 *
 * WHAT IS IN SCOPE, and it is a line the obligations draw
 *
 * README.md's four transport obligations are load-bearing and say
 * "nothing beyond the four obligations above is required" and "the
 * library is protocol-only and supplies none of them" (README.md, The
 * Message System).  They partition this instrument's reach:
 *
 *   CONTENT AND FIELD VALUES ARE THE ADVERSARY'S.  Obligation 2 says
 *     so outright -- "A Byzantine process may send arbitrary content."
 *     Every A-Cast payload byte, every BA value byte, every annotation
 *     byte, and every index, type and round field is therefore chosen
 *     here from its full range.  This is what the instrument sweeps.
 *
 *   PAYLOAD LENGTH AND SENDER IDENTITY ARE THE TRANSPORT'S.  A framer
 *     that hands an entry fewer than vLen + 1 readable bytes makes the
 *     library consume bytes no process sent, which is obligation 2's
 *     own definition of fabrication; a framer that lets a peer choose
 *     `from` breaks obligation 3's "a Byzantine process must not be
 *     able to impersonate a correct one."  Both are outside this
 *     instrument and outside the library: no entry takes a length, and
 *     `from` is an authenticated fact by the time it is an argument.
 *     An instrument that "found" either would be reporting an
 *     obligation back to its holder.  So `value` always points at
 *     vLen + 1 readable bytes here, and `from` ranges over the field
 *     only to prove the RANGE check, never to model impersonation.
 *
 * ONE HOSTILE CALL AGAINST AN HONEST STATE.  Sequences of hostile
 * messages already have their instruments: the schedule explorer's
 * adversary configs (test_schedules.c) enumerate well-formed Byzantine
 * CONTENT over whole schedules, and the annotation forgery arms
 * (test_bkr94acs_blackbox.c) carry a lying process across one.  Both
 * grammars are well-formed by construction, which is exactly what
 * neither can reach: ONE MALFORMED CALL, against a state reached
 * honestly, at every entry.  That is this instrument's half.
 *
 *
 * WHERE THE DOMAINS COME FROM, stated exactly
 *
 * A check fed by the mechanism that constrains it cannot fail, so the
 * BOUNDARIES below are quoted from the headers and the C is measured
 * against them -- a defect that widened a check moves the code, not the
 * boundary this compares to.  But be precise about what the headers
 * say: they document a RETURNED REFUSAL for the accessors
 * (bkr94acsBaDecision "Returns 0xFF on null state or out-of-range
 * process", and the accessors beside it) and for bkr94acsInit, and they do NOT
 * state one for bkr94acsAcastInput, bkr94acsBaInput or
 * bracha87Fig1Input.  For those three the boundary is the header's and
 * the RULE -- that an argument outside it must be refused rather than
 * acted on -- is this instrument's, derived from the encoding the
 * header does state.  The one exception is the INITIAL binding, which
 * the header does assign to these entries by name:
 * "(bkr94acs{Acast,Ba}Input enforce from == process / initiator on the
 * caller's behalf.)"  Do not read the green as the headers' own
 * promise where they make none.
 *
 *   process, from, initiator   0..n, since n ENCODES the count
 *                              ("actual = n + 1", bkr94acs.h struct
 *                              bkr94acs; bracha87.h Operational limits)
 *   round                      0..3 * maxPhases - 1, since "Fig 4
 *                              instantiates Fig 3 with maxRounds =
 *                              maxPhases * 3" (bracha87.h, Figure 4)
 *                              and a BA's Fig 1 instances are keyed by
 *                              that round space.  bracha87.h's
 *                              Operational limits give the wider TYPE
 *                              bound (254) and are not this bound.
 *   type                       BRACHA87_INITIAL / ECHO / READY, and an
 *                              INITIAL only from the designated
 *                              initiator -- from == process for an
 *                              A-Cast, from == initiator for a BA
 *                              (bracha87.h bracha87Fig1Input's INITIAL
 *                              sender obligation; README Note 17)
 *   act (Fig1Skip)             INITIAL_ALL / ECHO_ALL / READY_ALL
 *   annot                      NO DOMAIN.  "Only those two bits are
 *                              read, and only when type is
 *                              BRACHA87_READY, so a caller may pass the
 *                              whole packed discriminator byte unmasked
 *                              and every other bit is ignored"
 *                              (bkr94acs.h bkr94acsAcastInput).  That
 *                              is a testable claim, not a domain: see
 *                              ORACLE 5.
 *   A-Cast value bytes         NO DOMAIN -- content.
 *   BA value byte              NO DOMAIN -- content.  Fig 1 stores
 *                              whatever Rule 1 echoes.  Fig 3's
 *                              VALID^k membership test IS reached
 *                              inside this call on the message that
 *                              accepts -- bkr94acsBaInput banks it with
 *                              bracha87Fig3Accept -- but its verdict is
 *                              not routed into a refusal here: the
 *                              message is stored either way and the
 *                              return is discarded.  Fig 4's thresholds
 *                              are the ones genuinely out of reach,
 *                              being caller-paced on bkr94acsTurn.  So
 *                              no value is refused at this entry, and
 *                              asserting one would assert against a
 *                              layer that does not answer here.
 *
 *
 * THE ORACLES
 *
 *   1  REFUSAL IS RETURNED, NOT TAKEN.  An argument tuple outside the
 *      domains above must produce the entry's documented refusal --
 *      0 acts, 0xFF from bkr94acsBaDecision, a null pointer from an
 *      accessor -- and the library must not abort.  "A library never
 *      aborts" is the standing rule; the contract is a returned
 *      refusal.
 *   2  AND LEAVES NO TRACE.  The receiver's image is byte-identical
 *      across a refused call.  This is the half a return-value test
 *      cannot carry: an entry that validated its argument AFTER
 *      mutating something would return 0 and pass oracle 1.  The
 *      comparison is the whole image, so it covers every field no
 *      accessor exposes.
 *   3  EGRESS IS WELL-FORMED WHATEVER CAME IN.  Every act written by
 *      any call -- refused or not -- carries a known act code, indices
 *      within 0..n, a round below maxRounds, a type in {INITIAL, ECHO,
 *      READY}, and borrowed pointers (.value, .skip, .received) that
 *      are either null or INSIDE the image.  Hostile bytes in,
 *      well-formed acts out.  The out[] array is over-allocated and
 *      poisoned past the entry's stated bound, so an act written past
 *      it is caught rather than assumed absent.
 *   4  THE IMAGE STAYS IN ITS DOMAINS.  After any call: the
 *      configuration bytes are unchanged from Init, complete is 0 or 1,
 *      every BA decision is one of {0, 1, 0xFE, 0xFF}, every entered
 *      flag is 0 or 1, and every owned Fig 1 carries only defined flag
 *      bits and its Init configuration.  This is the "never corrupted"
 *      half, read through the const accessors and the public struct
 *      fields only.
 *   5  ONLY TWO ANNOTATION BITS ARE READ.  For one fixed call, the 256
 *      annot values must produce outcomes CONSTANT within their
 *      (bit 4, bit 5) class, and constant across all 256 on a type
 *      that is not a READY.  Not "four distinct outcomes": two classes
 *      may legitimately coincide in a given state, and at the later
 *      milestones they do.  The oracle is therefore ONE-DIRECTIONAL --
 *      it catches a bit being read that the header calls ignored, and
 *      cannot catch a documented bit going UNread.
 *   6  THE PACKED WIRE BYTE IS CLOSED.  bkr94acs.h's canonical
 *      discriminator layout is "A CONTRACT FOR PACKERS": a framer
 *      recovers type = byte & BRACHA87_TYPE_MASK, cls = byte &
 *      BKR94ACS_CLS_MASK, and a BA value = ((byte >> 3) & 1) | (byte &
 *      BRACHA87_D_FLAG).  All 256 bytes are decoded that way and fed
 *      in, and oracles 1-4 must hold for every one.  Note what this
 *      reaches that a hand-built call does not: type == 3 is
 *      REPRESENTABLE in two bits and is not a type, so an honest framer
 *      passes it to the library on the adversary's say-so.
 *
 *      THIS IS THE CONTRACT, NOT THE EXAMPLE CODE.  Neither bundled
 *      example can be linked here (each carries its own main), so what
 *      is executed is the header's stated layout.  The examples'
 *      compliance with it is a READING -- example/bkr94acs.c's qPush
 *      composes it and its delivery loop recovers it -- not something
 *      this instrument witnesses.
 *
 *
 * THE STATES SWEPT, and why a fresh instance is not enough
 *
 * A freshly initialized image refuses almost everything for reasons
 * that have nothing to do with the argument, so a sweep against one
 * proves little.  An honest 4-process cohort is driven in-process to
 * completion and the receiver's image is captured at each milestone it
 * passes; every sweep then runs against every milestone.  Restoring a
 * milestone is a memcpy back into THE SAME ALLOCATION and never a copy
 * to a new address: a bkr94acs image holds pointers into itself (every
 * BA's Fig 4 is carved out of data[] and is its own Fig 3's N-closure),
 * so a snapshot buffer's pointer words are stale with respect to the
 * buffer's own location.  Never cast a snapshot to a library struct.
 * This is the schedule explorer's snapshot rule and it is load-bearing
 * for the same reason.
 *
 *
 * WHAT THE GREEN DOES NOT ESTABLISH
 *
 *   - WHICH guard refused.  The claim is about the COMPOSITION: a
 *     widened check that a second one absorbs is invisible here.
 *     Measured, not argued -- widening bkr94acsAcastInput's `from`
 *     bound leaves this green, because bracha87Fig1Input's own bound
 *     catches it one call deeper; so does deleting the type switch's
 *     default, because an unknown type then fires no rule and moves
 *     nothing.  Read a pass as "the composition refuses", never as
 *     "this line is tested".
 *   - THAT A DOCUMENTED BIT IS READ.  See oracle 5.
 *   - WHAT A SURVIVING HOSTILE ARGUMENT WOULD DO LATER.  The image is
 *     restored after every call, so no admitted hostile argument is
 *     ever carried into a later honest egress.  One call, one state.
 *   - THE 0 AND 0xFE DECISION ARMS.  Every milestone this cohort
 *     reaches decides 1 (the per-milestone state is printed, so this
 *     is readable off the run rather than taken on trust).  A BA that
 *     decided 0, or exhausted, is not among the states swept, so the
 *     image-domain oracle's arms for those two are carried by the
 *     reading and not by a measurement.
 *
 * Style: C89, -pedantic -Wall -Wextra, Unix kernel style, 2-space
 * indent.  The sweeps are function bodies with labeled regions;
 * the shared oracle paths are reached by goto rather than factored into
 * single-caller helpers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bracha87.h"
#include "bkr94acs.h"

/*--------------------------------------------------------------------------*/
/*  The configuration swept.  Small on purpose: the sweep is over the       */
/*  FIELD SPACE, which is 0..255 whatever n is, so a larger cohort buys     */
/*  no adversary reach and costs image bytes per restore.                    */
/*--------------------------------------------------------------------------*/

#define CFG_N        4    /* actual process count */
#define CFG_T        1
#define CFG_VLEN     4    /* actual A-Cast value length */
#define CFG_PHASES   1
#define SELF         0    /* the receiver every sweep runs against */

#define MAX_MILE     8
#define QCAP     65536
#define TICK_CAP  2000
#define CANARY      16    /* acts written past the stated bound are caught */
#define DEFER        3    /* this process submits its A-Cast late */
#define DEFER_TICK   6

/*--------------------------------------------------------------------------*/
/*  Run state                                                               */
/*--------------------------------------------------------------------------*/

static unsigned char *Img[CFG_N];   /* the FIXED library allocations */
static unsigned long ImgSz;

static unsigned char *Mile[MAX_MILE];
static const char *MileName[MAX_MILE];
static unsigned int MileCnt;

static struct bracha87Fig1 *F1;     /* the bare surface, its own allocation */
static unsigned long F1sz;
static unsigned char *F1mile[MAX_MILE];
static const char *F1mileName[MAX_MILE];
static unsigned int F1mileCnt;

/* out[] for a swept call: the stated bound, then a poisoned tail. */
static struct bkr94acsAct Acts[BKR94ACS_MAX_ACTS(CFG_N - 1) + CANARY];
static unsigned char Pacts[BRACHA87_FIG1_RETRY_MAX_ACTS + CANARY];

static unsigned char Aval[CFG_N][CFG_VLEN];
static unsigned char Vbuf[CFG_VLEN];

static unsigned long Calls;
static unsigned long Refusals;
static unsigned long Admitted;
static unsigned long Fails;
static const char *FailMsg;
static char FailWhere[256];

/* The cohort's wire. */
struct wire {
  unsigned char cls;
  unsigned char process;
  unsigned char round;
  unsigned char initiator;
  unsigned char type;
  unsigned char annot;
  unsigned char from;
  unsigned char to;
  unsigned char bav;
  unsigned char val[CFG_VLEN];
};

static struct wire *Q;
static unsigned int Qhead;
static unsigned int Qtail;
static struct bracha87Retry Cursor[CFG_N];  /* caller-owned, persistent */

/*--------------------------------------------------------------------------*/
/*  The coin.  Deterministic alternating, the bundled examples' choice --   */
/*  every honest process draws the same value, which is what makes the      */
/*  cohort below converge without modelling a shared random source.         */
/*--------------------------------------------------------------------------*/

static unsigned char
demoCoin(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
  return ((unsigned char)(phase % 2));
}

/*--------------------------------------------------------------------------*/
/*  Failure reporting.  One line, then the run stops: a contract violation  */
/*  is not worked around here.                                              */
/*--------------------------------------------------------------------------*/

static void
fail(
  const char *msg
 ,const char *where
){
  if (Fails++)
    return;
  FailMsg = msg;
  strncpy(FailWhere, where, sizeof (FailWhere) - 1);
  FailWhere[sizeof (FailWhere) - 1] = 0;
  printf("\nFAILURE: %s\n     at: %s\n", msg, where);
  /*
   * FLUSH IT.  A defect this instrument catches can be one that also
   * corrupts memory, and the very next swept call may fault: buffered
   * output is discarded by a signal, so the detection would be lost
   * and the run would read as a bare crash with no named check.  That
   * is not hypothetical -- it is how M50 first graded.
   */
  fflush(stdout);
}

/*--------------------------------------------------------------------------*/
/*  The honest cohort.  Four processes in one address space, every act      */
/*  expanded to every unsuppressed recipient INCLUDING SELF (README         */
/*  obligation 4 -- a broadcast must reach the process that sent it), no    */
/*  loss, zero patience.  Its only job is to produce the milestone images   */
/*  the sweeps run against, so it is the shortest honest driver that        */
/*  reaches completion and not a model of anything.                         */
/*--------------------------------------------------------------------------*/

static void
qPush(
  unsigned char cls
 ,unsigned char process
 ,unsigned char round
 ,unsigned char initiator
 ,unsigned char type
 ,unsigned char annot
 ,unsigned char from
 ,unsigned char to
 ,unsigned char bav
 ,const unsigned char *val
){
  struct wire *w;

  if (Qtail >= QCAP) {
    /* The queue grows or the run stops; it never truncates.  A silent
     * drop would fake the very quiescence the milestones are read
     * from, and every sweep below would then run against a state the
     * protocol never reached. */
    fprintf(stderr, "test_ingress: cohort queue overflow at %u --"
            " the milestones would be fiction\n", (unsigned)QCAP);
    exit(2);
  }
  w = &Q[Qtail++];
  w->cls = cls;
  w->process = process;
  w->round = round;
  w->initiator = initiator;
  w->type = type;
  w->annot = annot;
  w->from = from;
  w->to = to;
  w->bav = bav;
  if (val)
    memcpy(w->val, val, CFG_VLEN);
  else
    memset(w->val, 0, CFG_VLEN);
}

static void
qActs(
  const struct bkr94acsAct *acts
 ,unsigned int nacts
 ,unsigned char self
){
  unsigned int k;
  unsigned int p;
  unsigned char annot;

  for (k = 0; k < nacts; ++k) {
    if (acts[k].act != BKR94ACS_ACT_ACAST_SEND
     && acts[k].act != BKR94ACS_ACT_BA_SEND)
      continue;
    for (p = 0; p < CFG_N; ++p) {
      if (acts[k].skip && BRACHA87_SKIP_TST(acts[k].skip, p))
        continue;
      annot = (unsigned char)
        ((acts[k].accepted ? BKR94ACS_ACCEPTED : 0)
       | ((acts[k].received && BRACHA87_SKIP_TST(acts[k].received, p))
          ? BKR94ACS_RECEIVED : 0));
      if (acts[k].act == BKR94ACS_ACT_ACAST_SEND)
        qPush(BKR94ACS_CLS_ACAST, acts[k].process, 0, 0, acts[k].type,
              annot, self, (unsigned char)p, 0, acts[k].value);
      else
        qPush(BKR94ACS_CLS_BA, acts[k].process, acts[k].round,
              acts[k].initiator, acts[k].type, annot, self,
              (unsigned char)p, acts[k].baValue, 0);
    }
  }
}

static void
mileTake(
  const char *name
){
  unsigned int i;

  if (MileCnt >= MAX_MILE)
    return;
  /*
   * A milestone byte-identical to one already held is not a state --
   * it is the same sweep run twice, and counting it would report
   * coverage the run does not have.  Refuse it and name it.
   */
  for (i = 0; i < MileCnt; ++i)
    if (!memcmp(Mile[i], Img[SELF], ImgSz)) {
      printf("  note: the state at \"%s\" is byte-identical to \"%s\""
             " -- not swept twice\n", name, MileName[i]);
      return;
    }
  if (!(Mile[MileCnt] = malloc(ImgSz))) {
    fprintf(stderr, "test_ingress: milestone allocation failed\n");
    exit(2);
  }
  memcpy(Mile[MileCnt], Img[SELF], ImgSz);
  MileName[MileCnt] = name;
  ++MileCnt;
}

static void
f1mileTake(
  const char *name
){
  if (F1mileCnt >= MAX_MILE)
    return;
  if (!(F1mile[F1mileCnt] = malloc(F1sz))) {
    fprintf(stderr, "test_ingress: milestone allocation failed\n");
    exit(2);
  }
  memcpy(F1mile[F1mileCnt], F1, F1sz);
  F1mileName[F1mileCnt] = name;
  ++F1mileCnt;
}

static void
driveCohort(
  void
){
  unsigned int i;
  unsigned int p;
  unsigned int q;
  unsigned int tick;
  unsigned int oldTail;
  unsigned int nacts;
  int sawAccept;
  int sawDecide;
  struct bkr94acs *a;

  for (i = 0; i < CFG_N; ++i) {
    if (!(Img[i] = calloc(1, ImgSz))) {
      fprintf(stderr, "test_ingress: image allocation failed\n");
      exit(2);
    }
    if (!bkr94acsInit((struct bkr94acs *)Img[i], CFG_N - 1, CFG_T,
                      CFG_VLEN - 1, CFG_PHASES, (unsigned char)i,
                      demoCoin, 0)) {
      fprintf(stderr, "test_ingress: bkr94acsInit refused the"
              " configuration\n");
      exit(2);
    }
    bracha87RetryInit(&Cursor[i]);
  }
  mileTake("fresh");

  /*
   * Process DEFER's submission is held back.  BKR94ACS.txt's "each
   * player enters his inputs asynchronously as evidence accumulates"
   * is the licence, and the point here is coverage: with every A-Cast
   * at the root, step 1 enters 1 into all N BAs and every decision is
   * 1, so the 0 and 0xFE arms of the image-domain oracle are never
   * populated and the step-2 fanout never fires.  A late submission
   * gives the cohort a chance to exclude it.
   */
  for (i = 0; i < CFG_N; ++i) {
    if (i == DEFER)
      continue;
    nacts = bkr94acsAcast((struct bkr94acs *)Img[i], Aval[i], Acts);
    qActs(Acts, nacts, (unsigned char)i);
  }
  mileTake("own A-Cast issued");

  sawAccept = 0;
  sawDecide = 0;
  for (tick = 0; tick < TICK_CAP; ++tick) {
    /* Drain exactly what was queued when the tick began.  Draining to
     * EMPTY instead would make a tick the whole transitive closure of
     * one delivery, which collapses the state space: the cohort then
     * passes from "nothing decided" to "everything decided" inside a
     * single tick and no partially-decided image is ever captured. */
    oldTail = Qtail;
    while (Qhead < oldTail) {
      struct wire *w;

      w = &Q[Qhead++];
      if (w->cls == BKR94ACS_CLS_ACAST)
        nacts = bkr94acsAcastInput((struct bkr94acs *)Img[w->to],
                                   w->process, w->type, w->annot,
                                   w->from, w->val, Acts);
      else
        nacts = bkr94acsBaInput((struct bkr94acs *)Img[w->to],
                                w->process, w->round, w->initiator,
                                w->type, w->annot, w->from, w->bav,
                                Acts);
      qActs(Acts, nacts, w->to);
    }
    if (Qhead) {
      memmove(Q, Q + Qhead, (Qtail - Qhead) * sizeof (struct wire));
      Qtail -= Qhead;
      Qhead = 0;
    }

    if (tick == DEFER_TICK) {
      nacts = bkr94acsAcast((struct bkr94acs *)Img[DEFER], Aval[DEFER],
                            Acts);
      qActs(Acts, nacts, (unsigned char)DEFER);
    }

    for (p = 0; p < CFG_N; ++p) {
      a = (struct bkr94acs *)Img[p];
      /*
       * ONE retry call per process per tick.  bracha87.h's NETWORK
       * FLOOD WARNING is explicit that a `while (Retry(...))` loop
       * empties the cursor space onto the wire as fast as the CPU
       * runs: until convergence every sent instance has actions, so
       * the 0 return that would end such a loop does not come.  The
       * tick IS the rate limit.  The turn below is the one drain the
       * header does sanction for a zero-patience caller.
       */
      nacts = bkr94acsRetryStep(a, &Cursor[p], Acts);
      qActs(Acts, nacts, (unsigned char)p);
      for (q = 0; q < CFG_N; ++q)
        while ((nacts = bkr94acsTurn(a, (unsigned char)q, 1, Acts))) {
          qActs(Acts, nacts, (unsigned char)p);
          /*
           * Between two turns some BAs hold a decision and others do
           * not.  That is a state the receiver genuinely passes
           * through, and it is the only place it can be caught: a
           * whole tick advances every BA, so at a tick boundary the
           * cohort has already gone from none decided to all.
           */
          if (p == SELF && !sawDecide) {
            unsigned int dec;

            dec = 0;
            for (i = 0; i < CFG_N; ++i)
              if (bkr94acsBaDecision(a, (unsigned char)i) <= 1)
                ++dec;
            if (dec && dec < CFG_N) {
              mileTake("some BAs decided, not all");
              sawDecide = 1;
            }
          }
        }
      nacts = bkr94acsFanout(a, 1, Acts);
      qActs(Acts, nacts, (unsigned char)p);
    }

    a = (struct bkr94acs *)Img[SELF];
    if (!sawAccept) {
      for (q = 0; q < CFG_N; ++q)
        if (q != SELF && bkr94acsAcastValue(a, (unsigned char)q)) {
          mileTake("a peer's A-Cast accepted");
          sawAccept = 1;
          break;
        }
    }
    if (a->complete) {
      mileTake("complete");
      return;
    }
  }
  fprintf(stderr, "test_ingress: the honest cohort did not complete in"
          " %u ticks -- the milestones this instrument sweeps are not"
          " reachable, so nothing below would mean anything\n",
          (unsigned)TICK_CAP);
  exit(2);
}

/*
 * The bare Fig 1 surface, driven the same way and for the same reason:
 * example/bracha87Fig1.c is a supported deployment, and its caller
 * reaches bracha87Fig1Input directly.  n = 4, t = 1, so the echo
 * threshold is 3, ready amplification is 2, and accept is 3.
 */
static void
driveFig1(
  void
){
  unsigned int i;
  unsigned char out[3];

  if (!(F1 = malloc(F1sz))) {
    fprintf(stderr, "test_ingress: Fig 1 allocation failed\n");
    exit(2);
  }
  if (!bracha87Fig1Init(F1, CFG_N - 1, CFG_T, CFG_VLEN - 1)) {
    fprintf(stderr, "test_ingress: bracha87Fig1Init refused\n");
    exit(2);
  }
  f1mileTake("fresh");

  /* Initiator 1's INITIAL: Rule 1 echoes unconditionally. */
  bracha87Fig1Input(F1, BRACHA87_INITIAL, 1, Aval[1], out);
  f1mileTake("echoed");

  /* Three distinct echoes cross (n+t)/2 + 1 and send ready. */
  for (i = 0; i < 3; ++i)
    bracha87Fig1Input(F1, BRACHA87_ECHO, (unsigned char)i, Aval[1], out);
  f1mileTake("ready sent");

  /* Three distinct readys are 2t+1: accept. */
  for (i = 0; i < 3; ++i)
    bracha87Fig1Input(F1, BRACHA87_READY, (unsigned char)i, Aval[1], out);
  if (!(F1->flags & BRACHA87_F1_ACCEPTED)) {
    fprintf(stderr, "test_ingress: the bare Fig 1 did not accept -- the"
            " milestones below would not be the states they name\n");
    exit(2);
  }
  bracha87Fig1ProcessAccepted(F1, 1);
  f1mileTake("accepted");
}

/*--------------------------------------------------------------------------*/
/*  The swept call's arguments, at file scope so the shared call-and-oracle */
/*  region below runs with them in hand and takes no parameters -- it is a  */
/*  labeled region reached by goto, not a helper.                           */
/*--------------------------------------------------------------------------*/

static unsigned int Sproc;
static unsigned int Stype;
static unsigned int Sannot;
static unsigned int Sfrom;
static unsigned int Sround;
static unsigned int Sinit;
static unsigned int Sbav;
static unsigned int Sentry;     /* 1 = AcastInput, 2 = BaInput */
static unsigned int Sbound;     /* the entry's stated act bound */
static unsigned int Snacts;
static int Sexpect;             /* 1 = the headers do not admit this tuple */
static unsigned int Sret;
static unsigned int Cur;        /* milestone under sweep */
static char Swhere[256];

#define POISON 0xA5

/*
 * Is a pointer inside the receiver's image?  An act's borrowed pointers
 * must be null or point into it (bkr94acs.h, struct bkr94acsAct: "a
 * borrowed pointer into library-owned storage").  The integer cast is
 * the reason this is written out rather than compared directly:
 * relational comparison of pointers into different objects is not
 * defined, and the question here is exactly whether they ARE the same
 * object.
 */
#define IN_IMAGE(p, base, sz) \
  ((unsigned long)(p) >= (unsigned long)(base) \
   && (unsigned long)(p) < (unsigned long)(base) + (sz))

static void
sweepAcs(
  void
){
  struct bkr94acs *a;
  const unsigned char *cp;
  const struct bracha87Fig1 *cf;
  unsigned char *poison;
  unsigned char sub[CFG_N];
  unsigned char sval[CFG_N];
  unsigned int i;
  unsigned int j;
  unsigned int k;
  unsigned int v;
  unsigned long di;   /* the shared region's own cursors: the sweep    */
  unsigned int dj;    /* loops above are live across the goto into it  */

  a = (struct bkr94acs *)Img[SELF];
  poison = (unsigned char *)Acts;

  /*----------------------------------------------------------------*/
  /*  Region A -- bkr94acsAcastInput.  Baseline is a legal ECHO from  */
  /*  process 2 about process 1's A-Cast; each sweep varies ONE field */
  /*  and leaves the rest legal, so a refusal is attributable.        */
  /*----------------------------------------------------------------*/

  Sentry = 1;
  Sbound = BKR94ACS_MAX_ACTS(CFG_N - 1);

  for (i = 0; i < 256; ++i) {
    Sproc = i; Stype = BRACHA87_ECHO; Sannot = BKR94ACS_RECEIVED;
    Sfrom = 2;
    Sexpect = (i >= CFG_N);
    sprintf(Swhere, "bkr94acsAcastInput process=%u (type=ECHO from=2)", i);
    Sret = 1; goto docall;
   r1: ;
  }

  for (i = 0; i < 256; ++i) {
    Sproc = 1; Stype = BRACHA87_ECHO; Sannot = BKR94ACS_RECEIVED;
    Sfrom = i;
    Sexpect = (i >= CFG_N);
    sprintf(Swhere, "bkr94acsAcastInput from=%u (type=ECHO process=1)", i);
    Sret = 2; goto docall;
   r2: ;
  }

  /* type, with from != process: an INITIAL is a forged broadcast here
   * and must be dropped (bracha87.h's INITIAL sender obligation). */
  for (i = 0; i < 256; ++i) {
    Sproc = 1; Stype = i; Sannot = BKR94ACS_RECEIVED; Sfrom = 2;
    Sexpect = (i > BRACHA87_READY) || (i == BRACHA87_INITIAL);
    sprintf(Swhere, "bkr94acsAcastInput type=%u (from=2 != process=1)", i);
    Sret = 3; goto docall;
   r3: ;
  }

  /* type, with from == process: the INITIAL is the initiator's own. */
  for (i = 0; i < 256; ++i) {
    Sproc = 1; Stype = i; Sannot = BKR94ACS_RECEIVED; Sfrom = 1;
    Sexpect = (i > BRACHA87_READY);
    sprintf(Swhere, "bkr94acsAcastInput type=%u (from=1 == process=1)", i);
    Sret = 4; goto docall;
   r4: ;
  }

  /* The payload is CONTENT: every byte value is admissible and none is
   * refused.  What this asserts is that the library stores and echoes
   * an arbitrary A-Cast byte without leaving its own domains. */
  for (i = 0; i < 256; ++i) {
    memset(Vbuf, (unsigned char)i, sizeof (Vbuf));
    Sproc = 1; Stype = BRACHA87_ECHO; Sannot = BKR94ACS_RECEIVED;
    Sfrom = 2;
    Sexpect = 0;
    sprintf(Swhere, "bkr94acsAcastInput value bytes=0x%02x", i);
    Sret = 5; goto docall;
   r5: ;
  }
  memcpy(Vbuf, Aval[1], sizeof (Vbuf));

  /* The boundary neighborhood as a cross product: one field at a time
   * cannot reach a tuple whose fields are individually legal. */
  for (i = 0; i < CFG_N + 2u; ++i)
    for (j = 0; j < CFG_N + 2u; ++j)
      for (k = 0; k <= BRACHA87_READY; ++k) {
        Sproc = i; Stype = k; Sannot = BKR94ACS_RECEIVED; Sfrom = j;
        Sexpect = (i >= CFG_N) || (j >= CFG_N)
               || (k == BRACHA87_INITIAL && i != j);
        sprintf(Swhere, "bkr94acsAcastInput process=%u from=%u type=%u",
                i, j, k);
        Sret = 6; goto docall;
       r6: ;
      }

  /*----------------------------------------------------------------*/
  /*  Region B -- bkr94acsBaInput.                                    */
  /*----------------------------------------------------------------*/

  Sentry = 2;
  Sbound = 2;

  for (i = 0; i < 256; ++i) {
    Sproc = i; Sround = 0; Sinit = 2; Stype = BRACHA87_ECHO;
    Sannot = BKR94ACS_RECEIVED; Sfrom = 2; Sbav = 1;
    Sexpect = (i >= CFG_N);
    sprintf(Swhere, "bkr94acsBaInput process=%u", i);
    Sret = 7; goto docall;
   r7: ;
  }

  for (i = 0; i < 256; ++i) {
    Sproc = 1; Sround = i; Sinit = 2; Stype = BRACHA87_ECHO;
    Sannot = BKR94ACS_RECEIVED; Sfrom = 2; Sbav = 1;
    Sexpect = (i >= BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES);
    sprintf(Swhere, "bkr94acsBaInput round=%u", i);
    Sret = 8; goto docall;
   r8: ;
  }

  for (i = 0; i < 256; ++i) {
    Sproc = 1; Sround = 0; Sinit = i; Stype = BRACHA87_ECHO;
    Sannot = BKR94ACS_RECEIVED; Sfrom = 2; Sbav = 1;
    Sexpect = (i >= CFG_N);
    sprintf(Swhere, "bkr94acsBaInput initiator=%u", i);
    Sret = 9; goto docall;
   r9: ;
  }

  for (i = 0; i < 256; ++i) {
    Sproc = 1; Sround = 0; Sinit = 2; Stype = BRACHA87_ECHO;
    Sannot = BKR94ACS_RECEIVED; Sfrom = i; Sbav = 1;
    Sexpect = (i >= CFG_N);
    sprintf(Swhere, "bkr94acsBaInput from=%u", i);
    Sret = 10; goto docall;
   r10: ;
  }

  for (i = 0; i < 256; ++i) {
    Sproc = 1; Sround = 0; Sinit = 2; Stype = i;
    Sannot = BKR94ACS_RECEIVED; Sfrom = 3; Sbav = 1;
    Sexpect = (i > BRACHA87_READY) || (i == BRACHA87_INITIAL);
    sprintf(Swhere, "bkr94acsBaInput type=%u (from=3 != initiator=2)", i);
    Sret = 11; goto docall;
   r11: ;
  }

  for (i = 0; i < 256; ++i) {
    Sproc = 1; Sround = 0; Sinit = 2; Stype = i;
    Sannot = BKR94ACS_RECEIVED; Sfrom = 2; Sbav = 1;
    Sexpect = (i > BRACHA87_READY);
    sprintf(Swhere, "bkr94acsBaInput type=%u (from=2 == initiator=2)", i);
    Sret = 12; goto docall;
   r12: ;
  }

  /* The BA value is CONTENT at this layer.  Fig 3's VALID^k test does
   * run inside this call on the message that accepts, but its verdict
   * is discarded -- the message is banked either way -- and Fig 4's
   * thresholds are caller-paced on bkr94acsTurn.  So no value is
   * refused here and the claim is the integrity one. */
  for (i = 0; i < 256; ++i) {
    Sproc = 1; Sround = 0; Sinit = 2; Stype = BRACHA87_ECHO;
    Sannot = BKR94ACS_RECEIVED; Sfrom = 2; Sbav = i;
    Sexpect = 0;
    sprintf(Swhere, "bkr94acsBaInput value=0x%02x", i);
    Sret = 13; goto docall;
   r13: ;
  }

  for (i = 0; i < CFG_N + 2u; ++i)
    for (j = 0; j < BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES + 2u; ++j)
      for (k = 0; k < CFG_N + 2u; ++k) {
        Sproc = i; Sround = j; Sinit = k; Stype = BRACHA87_ECHO;
        Sannot = BKR94ACS_RECEIVED; Sfrom = 2; Sbav = 1;
        Sexpect = (i >= CFG_N) || (k >= CFG_N)
               || (j >= BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES);
        sprintf(Swhere, "bkr94acsBaInput process=%u round=%u initiator=%u",
                i, j, k);
        Sret = 14; goto docall;
       r14: ;
      }

  /*----------------------------------------------------------------*/
  /*  Region C -- the query surface.  An application routes these on  */
  /*  a process index it may well have taken off a message, so each   */
  /*  carries its own documented refusal and each is swept over the    */
  /*  whole field.  The const ones are checked for image identity too: */
  /*  a "read-only" accessor that wrote would be caught here.          */
  /*----------------------------------------------------------------*/

  memcpy(Img[SELF], Mile[Cur], ImgSz);
  for (i = 0; i < 256 && !Fails; ++i) {
    int bad;

    bad = (i >= CFG_N);
    ++Calls;
    if (bad)
      ++Refusals;
    else
      ++Admitted;

    if (bkr94acsBaDecision(a, (unsigned char)i) != 0xFF && bad) {
      sprintf(Swhere, "bkr94acsBaDecision process=%u", i);
      fail("an out-of-range process did not read 0xFF", Swhere);
    }
    if (bkr94acsBaEntered(a, (unsigned char)i) && bad) {
      sprintf(Swhere, "bkr94acsBaEntered process=%u", i);
      fail("an out-of-range process did not read 0", Swhere);
    }
    if (bkr94acsAcastAllEchoed(a, (unsigned char)i) && bad) {
      sprintf(Swhere, "bkr94acsAcastAllEchoed process=%u", i);
      fail("an out-of-range process did not read 0", Swhere);
    }
    if (bkr94acsTurnDuty(a, (unsigned char)i) != BKR94ACS_DUTY_HELD && bad) {
      sprintf(Swhere, "bkr94acsTurnDuty process=%u", i);
      fail("an out-of-range process did not read HELD", Swhere);
    }
    if ((cp = bkr94acsAcastValue(a, (unsigned char)i))) {
      if (bad) {
        sprintf(Swhere, "bkr94acsAcastValue process=%u", i);
        fail("an out-of-range process returned a value", Swhere);
      } else if (!IN_IMAGE(cp, Img[SELF], ImgSz)) {
        sprintf(Swhere, "bkr94acsAcastValue process=%u", i);
        fail("a borrowed value pointer is outside the image", Swhere);
      }
    }
    if ((cp = bkr94acsAcastSkip(a, (unsigned char)i))) {
      if (bad) {
        sprintf(Swhere, "bkr94acsAcastSkip process=%u", i);
        fail("an out-of-range process returned a mask", Swhere);
      } else if (!IN_IMAGE(cp, Img[SELF], ImgSz)) {
        sprintf(Swhere, "bkr94acsAcastSkip process=%u", i);
        fail("a borrowed mask pointer is outside the image", Swhere);
      }
    }
    if ((cf = bkr94acsAcastFig1(a, (unsigned char)i))) {
      if (bad) {
        sprintf(Swhere, "bkr94acsAcastFig1 process=%u", i);
        fail("an out-of-range process returned an instance", Swhere);
      } else if (!IN_IMAGE(cf, Img[SELF], ImgSz)) {
        sprintf(Swhere, "bkr94acsAcastFig1 process=%u", i);
        fail("a borrowed instance pointer is outside the image", Swhere);
      }
    }
    /* Two arrays of n + 1 entries, as the header specifies -- passing
     * one buffer for both would prove whatever it proves under a
     * calling convention the header does not describe. */
    if (bkr94acsBaGetValid(a, (unsigned char)i, sub, sval) && bad) {
      sprintf(Swhere, "bkr94acsBaGetValid process=%u", i);
      fail("an out-of-range process returned a set", Swhere);
    }
    if (memcmp(Img[SELF], Mile[Cur], ImgSz)) {
      sprintf(Swhere, "the query surface at process=%u", i);
      fail("a const accessor changed the receiver's image", Swhere);
      break;
    }
  }

  /* bkr94acsBaFig1 keys on three fields; sweep the boundary of each. */
  for (i = 0; i < CFG_N + 2u; ++i)
    for (j = 0; j < BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES + 2u; ++j)
      for (k = 0; k < CFG_N + 2u; ++k) {
        ++Calls;
        cf = bkr94acsBaFig1(a, (unsigned char)i, (unsigned char)j,
                            (unsigned char)k);
        if (i >= CFG_N || k >= CFG_N
         || j >= BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES) {
          ++Refusals;
          if (cf) {
            sprintf(Swhere, "bkr94acsBaFig1 %u/%u/%u", i, j, k);
            fail("an out-of-range key returned an instance", Swhere);
          }
        } else {
          ++Admitted;
          if (!cf || !IN_IMAGE(cf, Img[SELF], ImgSz)) {
            sprintf(Swhere, "bkr94acsBaFig1 %u/%u/%u", i, j, k);
            fail("an in-range key returned nothing, or a pointer outside"
                 " the image", Swhere);
          }
        }
      }

  /* bkr94acsTurn MUTATES, so it is swept with the restore discipline
   * the two Input entries get. */
  for (i = 0; i < 256 && !Fails; ++i) {
    memcpy(Img[SELF], Mile[Cur], ImgSz);
    memset(Acts, POISON, sizeof (Acts));
    ++Calls;
    Snacts = bkr94acsTurn(a, (unsigned char)i, 1, Acts);
    if (i >= CFG_N) {
      ++Refusals;
      if (Snacts) {
        sprintf(Swhere, "bkr94acsTurn process=%u", i);
        fail("an out-of-range process turned a round", Swhere);
      }
      if (memcmp(Img[SELF], Mile[Cur], ImgSz)) {
        sprintf(Swhere, "bkr94acsTurn process=%u", i);
        fail("a refused turn changed the receiver's image", Swhere);
      }
    } else
      ++Admitted;
    if (Snacts > 3) {
      sprintf(Swhere, "bkr94acsTurn process=%u", i);
      fail("act count above the entry's stated bound of 3", Swhere);
    }
    /* The same egress and canary oracles the Input entries get.  This
     * is the one swept entry that can emit BA_DECIDED, COMPLETE and
     * BA_EXHAUSTED, so without it those three act codes are never
     * checked anywhere. */
    for (j = 0; j < Snacts && j < 3; ++j) {
      if (Acts[j].act < BKR94ACS_ACT_ACAST_SEND
       || Acts[j].act > BKR94ACS_ACT_BA_EXHAUSTED) {
        sprintf(Swhere, "bkr94acsTurn process=%u", i);
        fail("an act carries an unknown BKR94ACS_ACT_* code", Swhere);
      }
      if (Acts[j].act != BKR94ACS_ACT_COMPLETE && Acts[j].process >= CFG_N) {
        sprintf(Swhere, "bkr94acsTurn process=%u", i);
        fail("an act names a process outside 0..n", Swhere);
      }
      if (Acts[j].value && !IN_IMAGE(Acts[j].value, Img[SELF], ImgSz)) {
        sprintf(Swhere, "bkr94acsTurn process=%u", i);
        fail("an act's borrowed pointer is outside the image", Swhere);
      }
    }
    for (k = Snacts * sizeof (Acts[0]); k < sizeof (Acts); ++k)
      if (((unsigned char *)Acts)[k] != POISON) {
        sprintf(Swhere, "bkr94acsTurn process=%u", i);
        fail("the entry wrote past the act count it returned", Swhere);
        break;
      }
  }
  memcpy(Img[SELF], Mile[Cur], ImgSz);

  /*----------------------------------------------------------------*/
  /*  Region D -- only two annotation bits are read.  The header says  */
  /*  every other bit is ignored, so outcomes must be CONSTANT within  */
  /*  an (annot & bits 4,5) class -- and, on a type that is not a      */
  /*  READY, constant across all 256.  Not "four distinct outcomes":   */
  /*  two classes may legitimately coincide in a given state.          */
  /*----------------------------------------------------------------*/

  for (v = 0; v <= BRACHA87_READY; ++v) {
    unsigned char *ref[4];
    unsigned int refn[4];
    int have[4];

    for (i = 0; i < 4; ++i) {
      have[i] = 0;
      refn[i] = 0;
      if (!(ref[i] = malloc(ImgSz))) {
        fprintf(stderr, "test_ingress: annot reference allocation"
                " failed\n");
        exit(2);
      }
    }
    for (i = 0; i < 256; ++i) {
      unsigned int cls;

      cls = (((i & BKR94ACS_ACCEPTED) ? 1u : 0u)
           | ((i & BKR94ACS_RECEIVED) ? 2u : 0u));
      if (v != BRACHA87_READY)
        cls = 0;
      memcpy(Img[SELF], Mile[Cur], ImgSz);
      memset(Acts, POISON, sizeof (Acts));
      ++Calls;
      ++Admitted;
      /* from == process, so the INITIAL arm of this loop is the
       * initiator's own and no tuple here is a forged broadcast --
       * otherwise a third of this region would be calls the headers
       * refuse, counted as though they had been admitted. */
      Snacts = bkr94acsAcastInput(a, 1, (unsigned char)v,
                                  (unsigned char)i, 1, Vbuf, Acts);
      if (!have[cls]) {
        memcpy(ref[cls], Img[SELF], ImgSz);
        refn[cls] = Snacts;
        have[cls] = 1;
      } else if (Snacts != refn[cls]
              || memcmp(ref[cls], Img[SELF], ImgSz)) {
        sprintf(Swhere, "bkr94acsAcastInput type=%u annot=0x%02x", v, i);
        fail(v == BRACHA87_READY
             ? "two annot values with the same ACCEPTED/RECEIVED bits"
               " produced different outcomes -- a bit the header calls"
               " ignored is being read"
             : "annot changed the outcome on a type that is not a READY",
             Swhere);
      }
    }
    for (i = 0; i < 4; ++i)
      free(ref[i]);
  }
  memcpy(Img[SELF], Mile[Cur], ImgSz);

  /*----------------------------------------------------------------*/
  /*  Region E -- the canonical packed wire byte, all 256 of them.     */
  /*  bkr94acs.h's layout is a CONTRACT FOR PACKERS; this decodes by   */
  /*  it exactly and feeds the result in.  type == 3 is representable  */
  /*  in the two type bits and is not a type, so an honest framer      */
  /*  hands it over on the adversary's say-so.                         */
  /*----------------------------------------------------------------*/

  for (i = 0; i < 256; ++i)
    for (j = 0; j < 2; ++j) {
      unsigned int cls;

      cls = i & BKR94ACS_CLS_MASK;
      Stype = i & BRACHA87_TYPE_MASK;
      Sannot = i;
      /* j = 0 puts the authenticated sender at the designated
       * initiator, j = 1 elsewhere: the first admits an INITIAL, the
       * second is the forged broadcast the library must drop. */
      Sproc = 1;
      Sinit = 2;
      Sfrom = j ? 3 : (cls ? Sinit : Sproc);
      Sround = 0;
      Sbav = (unsigned char)(((i >> 3) & 1) | (i & BRACHA87_D_FLAG));
      if (cls == BKR94ACS_CLS_ACAST) {
        Sentry = 1;
        Sbound = BKR94ACS_MAX_ACTS(CFG_N - 1);
        Sexpect = (Stype > BRACHA87_READY)
               || (Stype == BRACHA87_INITIAL && Sfrom != Sproc);
      } else {
        Sentry = 2;
        Sbound = 2;
        Sexpect = (Stype > BRACHA87_READY)
               || (Stype == BRACHA87_INITIAL && Sfrom != Sinit);
      }
      sprintf(Swhere, "framer: wire byte 0x%02x, sender %s", i,
              j ? "off the initiator" : "at the initiator");
      Sret = 15; goto docall;
     r15: ;
    }
  return;

  /*----------------------------------------------------------------*/
  /*  The shared call-and-oracle region.  Both Input entries reach it  */
  /*  by goto with the arguments already in the file-scope slots, so   */
  /*  the oracles are written once and run identically for every       */
  /*  sweep above.  Restoring before AND after is what makes each      */
  /*  call independent: every one starts from the milestone.           */
  /*----------------------------------------------------------------*/

 docall:
  memcpy(Img[SELF], Mile[Cur], ImgSz);
  memset(Acts, POISON, sizeof (Acts));

  if (Sentry == 1)
    Snacts = bkr94acsAcastInput(a, (unsigned char)Sproc,
                                (unsigned char)Stype,
                                (unsigned char)Sannot,
                                (unsigned char)Sfrom, Vbuf, Acts);
  else
    Snacts = bkr94acsBaInput(a, (unsigned char)Sproc,
                             (unsigned char)Sround,
                             (unsigned char)Sinit, (unsigned char)Stype,
                             (unsigned char)Sannot, (unsigned char)Sfrom,
                             (unsigned char)Sbav, Acts);

  ++Calls;

  /* ORACLES 1 and 2.  A tuple the headers do not admit must be refused
   * -- and a return of 0 alone does not settle it: an entry that
   * validated after mutating would pass that and fail this. */
  if (Sexpect) {
    ++Refusals;
    if (Snacts)
      fail("a tuple outside the headers' domains was admitted", Swhere);
    if (memcmp(Img[SELF], Mile[Cur], ImgSz))
      fail("a refused call changed the receiver's image", Swhere);
  } else
    ++Admitted;

  /* ORACLE 3.  Hostile bytes in, well-formed acts out -- and nothing
   * written past the count the entry returned. */
  if (Snacts > Sbound)
    fail("act count above the entry's stated bound", Swhere);
  for (di = 0; di < Snacts && di < Sbound; ++di) {
    if (Acts[di].act < BKR94ACS_ACT_ACAST_SEND
     || Acts[di].act > BKR94ACS_ACT_BA_EXHAUSTED)
      fail("an act carries an unknown BKR94ACS_ACT_* code", Swhere);
    if (Acts[di].process >= CFG_N)
      fail("an act names a process outside 0..n", Swhere);
    if (Acts[di].act == BKR94ACS_ACT_ACAST_SEND
     || Acts[di].act == BKR94ACS_ACT_BA_SEND) {
      if (Acts[di].type > BRACHA87_READY)
        fail("an act carries a type outside INITIAL/ECHO/READY", Swhere);
      if (Acts[di].act == BKR94ACS_ACT_BA_SEND
       && (Acts[di].round >= BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES
        || Acts[di].initiator >= CFG_N))
        fail("a BA act names a round or initiator outside its space",
             Swhere);
    }
    if (Acts[di].value && !IN_IMAGE(Acts[di].value, Img[SELF], ImgSz))
      fail("an act's borrowed value pointer is outside the image",
           Swhere);
    if (Acts[di].skip && !IN_IMAGE(Acts[di].skip, Img[SELF], ImgSz))
      fail("an act's borrowed skip mask is outside the image", Swhere);
    if (Acts[di].received
     && !IN_IMAGE(Acts[di].received, Img[SELF], ImgSz))
      fail("an act's borrowed RECEIVED mask is outside the image",
           Swhere);
  }
  for (di = Snacts * sizeof (Acts[0]); di < sizeof (Acts); ++di)
    if (poison[di] != POISON) {
      fail("the entry wrote past the act count it returned", Swhere);
      break;
    }

  /* ORACLE 4.  Never corrupted: every domain the public surface
   * exposes still holds, and the configuration is the one Init set. */
  if (a->n != CFG_N - 1 || a->t != CFG_T || a->vLen != CFG_VLEN - 1
   || a->maxPhases != CFG_PHASES || a->self != SELF)
    fail("the configuration bytes moved", Swhere);
  if (a->complete > 1)
    fail("complete is neither 0 nor 1", Swhere);
  for (di = 0; di < CFG_N; ++di) {
    unsigned char dec;

    dec = bkr94acsBaDecision(a, (unsigned char)di);
    if (dec > 1 && dec != 0xFE && dec != 0xFF)
      fail("a BA decision is outside {0, 1, 0xFE, 0xFF}", Swhere);
    if (bkr94acsBaEntered(a, (unsigned char)di) > 1)
      fail("an entered flag is neither 0 nor 1", Swhere);
    cf = bkr94acsAcastFig1(a, (unsigned char)di);
    if (!cf)
      fail("an in-range A-Cast instance vanished", Swhere);
    else if ((cf->flags & ~(unsigned int)(BRACHA87_F1_ECHOED
                                        | BRACHA87_F1_RDSENT
                                        | BRACHA87_F1_ACCEPTED
                                        | BRACHA87_F1_INITIATOR))
          || cf->n != CFG_N - 1 || cf->t != CFG_T
          || cf->vLen != CFG_VLEN - 1)
      fail("an A-Cast Fig 1 carries undefined flag bits or a moved"
           " configuration", Swhere);
    for (dj = 0; dj < BRACHA87_ROUNDS_PER_PHASE * CFG_PHASES; ++dj) {
      unsigned int dk;

      for (dk = 0; dk < CFG_N; ++dk) {
        cf = bkr94acsBaFig1(a, (unsigned char)di, (unsigned char)dj,
                            (unsigned char)dk);
        if (!cf)
          fail("an in-range BA instance vanished", Swhere);
        else if ((cf->flags & ~(unsigned int)(BRACHA87_F1_ECHOED
                                            | BRACHA87_F1_RDSENT
                                            | BRACHA87_F1_ACCEPTED
                                            | BRACHA87_F1_INITIATOR))
              || cf->n != CFG_N - 1 || cf->t != CFG_T || cf->vLen)
          fail("a BA Fig 1 carries undefined flag bits or a moved"
               " configuration", Swhere);
      }
    }
  }

  memcpy(Img[SELF], Mile[Cur], ImgSz);

  if (Fails)
    return;

  switch (Sret) {
  case 1:  goto r1;
  case 2:  goto r2;
  case 3:  goto r3;
  case 4:  goto r4;
  case 5:  goto r5;
  case 6:  goto r6;
  case 7:  goto r7;
  case 8:  goto r8;
  case 9:  goto r9;
  case 10: goto r10;
  case 11: goto r11;
  case 12: goto r12;
  case 13: goto r13;
  case 14: goto r14;
  default: goto r15;
  }
}

/*--------------------------------------------------------------------------*/
/*  The bare Fig 1 surface.                                                 */
/*                                                                          */
/*  ONE DOMAIN DIFFERS HERE, and it is the header's own point: this entry    */
/*  does NOT enforce the INITIAL sender binding.  "This entry does not know  */
/*  its own initiator index, so it cannot enforce the binding -- the CALLER  */
/*  must drop any INITIAL whose authenticated sender is not the designated   */
/*  initiator before calling here" (bracha87.h).  So an INITIAL from any     */
/*  in-range sender is ADMITTED at this layer, and expecting a refusal would */
/*  be asserting against a contract the header assigns elsewhere.  The       */
/*  composition enforces it instead, which is what region A above measured.  */
/*--------------------------------------------------------------------------*/

static unsigned int F1cur;

static void
sweepFig1(
  void
){
  const unsigned char *cp;
  unsigned char *poison;
  unsigned int i;
  unsigned long di;

  poison = Pacts;

  for (i = 0; i < 256; ++i) {
    Stype = BRACHA87_ECHO; Sfrom = i;
    Sexpect = (i >= CFG_N);
    sprintf(Swhere, "bracha87Fig1Input from=%u (type=ECHO)", i);
    Sret = 1; goto docall;
   q1: ;
  }

  for (i = 0; i < 256; ++i) {
    Stype = i; Sfrom = 2;
    Sexpect = (i > BRACHA87_READY);
    sprintf(Swhere, "bracha87Fig1Input type=%u (from=2)", i);
    Sret = 2; goto docall;
   q2: ;
  }

  for (i = 0; i < 256; ++i) {
    memset(Vbuf, (unsigned char)i, sizeof (Vbuf));
    Stype = BRACHA87_ECHO; Sfrom = 2;
    Sexpect = 0;
    sprintf(Swhere, "bracha87Fig1Input value bytes=0x%02x", i);
    Sret = 3; goto docall;
   q3: ;
  }
  memcpy(Vbuf, Aval[1], sizeof (Vbuf));

  /* The two annotation setters the caller routes off a READY.  Their
   * contract is "out-of-range 'from' and a null instance are ignored"
   * (bracha87.h), so the refusal here is silence plus identity -- the
   * only form a void entry can take, and the reason identity is the
   * oracle that carries it. */
  for (i = 0; i < 256 && !Fails; ++i) {
    memcpy(F1, F1mile[F1cur], F1sz);
    ++Calls;
    bracha87Fig1ProcessAccepted(F1, (unsigned char)i);
    if (i >= CFG_N) {
      ++Refusals;
      if (memcmp(F1, F1mile[F1cur], F1sz)) {
        sprintf(Swhere, "bracha87Fig1ProcessAccepted from=%u", i);
        fail("an out-of-range announcement was recorded", Swhere);
      }
    } else
      ++Admitted;

    memcpy(F1, F1mile[F1cur], F1sz);
    ++Calls;
    bracha87Fig1ProcessResend(F1, (unsigned char)i);
    if (i >= CFG_N) {
      ++Refusals;
      if (memcmp(F1, F1mile[F1cur], F1sz)) {
        sprintf(Swhere, "bracha87Fig1ProcessResend from=%u", i);
        fail("an out-of-range arm was recorded", Swhere);
      }
    } else
      ++Admitted;
  }
  memcpy(F1, F1mile[F1cur], F1sz);

  /* The mask selector: anything that is not a retry act answers 0
   * ("0 for a null instance or non-retry act"). */
  for (i = 0; i < 256 && !Fails; ++i) {
    ++Calls;
    cp = bracha87Fig1Skip(F1, (unsigned char)i);
    if (i != BRACHA87_INITIAL_ALL && i != BRACHA87_ECHO_ALL
     && i != BRACHA87_READY_ALL) {
      ++Refusals;
      if (cp) {
        sprintf(Swhere, "bracha87Fig1Skip act=%u", i);
        fail("a non-retry act returned a mask", Swhere);
      }
    } else {
      ++Admitted;
      if (cp && !IN_IMAGE(cp, F1, F1sz)) {
        sprintf(Swhere, "bracha87Fig1Skip act=%u", i);
        fail("a borrowed mask pointer is outside the instance", Swhere);
      }
    }
  }
  if (memcmp(F1, F1mile[F1cur], F1sz))
    fail("a const accessor changed the instance",
         "bracha87Fig1Skip sweep");
  return;

 docall:
  memcpy(F1, F1mile[F1cur], F1sz);
  memset(Pacts, POISON, sizeof (Pacts));
  Snacts = bracha87Fig1Input(F1, (unsigned char)Stype,
                             (unsigned char)Sfrom, Vbuf, Pacts);
  ++Calls;

  if (Sexpect) {
    ++Refusals;
    if (Snacts)
      fail("a tuple outside the headers' domains was admitted", Swhere);
    if (memcmp(F1, F1mile[F1cur], F1sz))
      fail("a refused call changed the instance", Swhere);
  } else
    ++Admitted;

  if (Snacts > BRACHA87_FIG1_RETRY_MAX_ACTS)
    fail("act count above the entry's stated bound of 3", Swhere);
  for (di = 0; di < Snacts && di < BRACHA87_FIG1_RETRY_MAX_ACTS; ++di)
    if (Pacts[di] < BRACHA87_INITIAL_ALL || Pacts[di] > BRACHA87_ACCEPT)
      fail("an action carries an unknown BRACHA87_* code", Swhere);
  for (di = Snacts; di < sizeof (Pacts); ++di)
    if (poison[di] != POISON) {
      fail("the entry wrote past the action count it returned", Swhere);
      break;
    }

  if ((F1->flags & ~(unsigned int)(BRACHA87_F1_ECHOED
                                 | BRACHA87_F1_RDSENT
                                 | BRACHA87_F1_ACCEPTED
                                 | BRACHA87_F1_INITIATOR))
   || F1->n != CFG_N - 1 || F1->t != CFG_T || F1->vLen != CFG_VLEN - 1)
    fail("the instance carries undefined flag bits or a moved"
         " configuration", Swhere);
  if ((cp = bracha87Fig1Value(F1)) && !IN_IMAGE(cp, F1, F1sz))
    fail("the echoed value pointer is outside the instance", Swhere);
  if ((cp = bracha87Fig1Received(F1)) && !IN_IMAGE(cp, F1, F1sz))
    fail("the RECEIVED mask is outside the instance", Swhere);

  memcpy(F1, F1mile[F1cur], F1sz);

  if (Fails)
    return;

  switch (Sret) {
  case 1:  goto q1;
  case 2:  goto q2;
  default: goto q3;
  }
}

/*--------------------------------------------------------------------------*/
/*  The standalone Fig 2, Fig 3 and Fig 4 surfaces.                         */
/*                                                                          */
/*  These are not reached through bkr94acs with a hostile argument -- the    */
/*  ACS range-checks process/round/initiator before it routes, so their own  */
/*  guards never see one from that direction.  They are public entries all   */
/*  the same, and a caller composing the figures itself (which is what       */
/*  bkr94acs.c is) feeds them round and sender fields that came off a wire.  */
/*  So they are swept directly.                                             */
/*--------------------------------------------------------------------------*/

#define BARE_ROUNDS 6

static struct bracha87Fig2 *F2;
static struct bracha87Fig3 *F3;
static struct bracha87Fig4 *F4;
static unsigned long F2sz;
static unsigned long F3sz;
static unsigned long F4sz;
static unsigned char *F2ref;
static unsigned char *F3ref;
static unsigned char *F4ref;

/*
 * A protocol function for the standalone Fig 3.  Permissive on the base
 * value with no D_FLAG marked legitimate, which is the header's shape
 * for "any binary value could have been produced, and no d-message
 * could": it writes result on every non-error return, as required.
 */
static int
bareNfn(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  (void)closure;
  (void)k;
  (void)senders;
  (void)values;
  if (!n_msgs)
    return (-1);
  *result = 0;
  return (1);
}

static void
sweepBare(
  void
){
  unsigned char out[BRACHA87_FIG1_RETRY_MAX_ACTS];
  unsigned char senders[CFG_N];
  unsigned char values[CFG_N];
  unsigned char vals[CFG_N];
  unsigned int i;
  unsigned int j;
  unsigned int nextK;

  /*----------------------------------------------------------------*/
  /*  Null arguments.  Every entry that takes a pointer documents a   */
  /*  refusal for a null one, and nothing else in this instrument     */
  /*  ever passes one -- so without this region those guards are      */
  /*  untested and a reader would still see green.                    */
  /*----------------------------------------------------------------*/

  Calls += 22;
  Refusals += 22;
  if (bracha87Fig1Input(0, BRACHA87_ECHO, 0, Vbuf, out)
   || bracha87Fig1Input(F1, BRACHA87_ECHO, 0, 0, out)
   || bracha87Fig1Input(F1, BRACHA87_ECHO, 0, Vbuf, 0)
   || bracha87Fig1Bpr(0, out)
   || bracha87Fig1Bpr(F1, 0)
   || bracha87Fig1AllEchoed(0)
   || bracha87Fig1Value(0)
   || bracha87Fig1Skip(0, BRACHA87_READY_ALL)
   || bracha87Fig1Received(0)
   || bracha87Fig1SentCount(0, 1))
    fail("a null argument was not refused", "bracha87Fig1 entries");
  bracha87Fig1Initiator(0, Vbuf);
  bracha87Fig1Initiator(F1, 0);
  bracha87Fig1ProcessAccepted(0, 0);
  bracha87Fig1ProcessResend(0, 0);
  bracha87RetryInit(0);
  if (bracha87Fig2Receive(0, 0, 0, 0) || bracha87Fig2RecvCount(0, 0)
   || bracha87Fig2GetReceived(0, 0, senders, values)
   || bracha87Fig3Accept(0, 0, 0, 0, 0) || bracha87Fig3ValidCount(0, 0)
   || bracha87Fig3GetValid(0, 0, senders, values)
   || bracha87Fig3RoundComplete(0, 0)
   || bracha87Fig4Round(0, 0, 1, vals)
   || bracha87Fig4Round(F4, 0, 1, 0))
    fail("a null argument was not refused", "bracha87Fig2/3/4 entries");

  Calls += 12;
  Refusals += 12;
  if (bkr94acsAcastInput(0, 0, BRACHA87_ECHO, 0, 0, Vbuf, Acts)
   || bkr94acsAcastInput((struct bkr94acs *)Img[SELF], 0, BRACHA87_ECHO,
                         0, 0, 0, Acts)
   || bkr94acsAcastInput((struct bkr94acs *)Img[SELF], 0, BRACHA87_ECHO,
                         0, 0, Vbuf, 0)
   || bkr94acsBaInput(0, 0, 0, 0, BRACHA87_ECHO, 0, 0, 1, Acts)
   || bkr94acsBaInput((struct bkr94acs *)Img[SELF], 0, 0, 0,
                      BRACHA87_ECHO, 0, 0, 1, 0)
   || bkr94acsSubset(0, senders)
   || bkr94acsSubset((const struct bkr94acs *)Img[SELF], 0)
   || bkr94acsAcastValue(0, 0)
   || bkr94acsAcast(0, Vbuf, Acts)
   || bkr94acsRetryStep(0, &Cursor[0], Acts)
   || bkr94acsFanout(0, 1, Acts)
   || bkr94acsTurn(0, 0, 1, Acts))
    fail("a null argument was not refused", "bkr94acs entries");
  if (bkr94acsFanoutDuty(0) != BKR94ACS_DUTY_HELD
   || bkr94acsTurnDuty(0, 0) != BKR94ACS_DUTY_HELD
   || bkr94acsBaDecision(0, 0) != 0xFF)
    fail("a null argument was not refused", "bkr94acs duty queries");

  /*----------------------------------------------------------------*/
  /*  Fig 2 -- round and sender over the whole field.                 */
  /*----------------------------------------------------------------*/

  memcpy(F2, F2ref, F2sz);
  for (i = 0; i < 256 && !Fails; ++i) {
    ++Calls;
    if (i >= BARE_ROUNDS) {
      ++Refusals;
      if (bracha87Fig2Receive(F2, (unsigned char)i, 1, 1)
       || memcmp(F2, F2ref, F2sz)) {
        sprintf(Swhere, "bracha87Fig2Receive round=%u", i);
        fail("an out-of-range round was admitted, or moved a byte",
             Swhere);
      }
      if (bracha87Fig2RecvCount(F2, (unsigned char)i)
       || bracha87Fig2GetReceived(F2, (unsigned char)i, senders, values)) {
        sprintf(Swhere, "bracha87Fig2 queries round=%u", i);
        fail("an out-of-range round answered", Swhere);
      }
    } else {
      ++Admitted;
      bracha87Fig2Receive(F2, (unsigned char)i, 1, 1);
      memcpy(F2, F2ref, F2sz);
    }
  }
  for (i = 0; i < 256 && !Fails; ++i) {
    ++Calls;
    if (i >= CFG_N) {
      ++Refusals;
      if (bracha87Fig2Receive(F2, 0, (unsigned char)i, 1)
       || memcmp(F2, F2ref, F2sz)) {
        sprintf(Swhere, "bracha87Fig2Receive sender=%u", i);
        fail("an out-of-range sender was admitted, or moved a byte",
             Swhere);
      }
    } else {
      ++Admitted;
      bracha87Fig2Receive(F2, 0, (unsigned char)i, 1);
      memcpy(F2, F2ref, F2sz);
    }
  }

  /*----------------------------------------------------------------*/
  /*  Fig 3 -- the same two fields, plus the value as CONTENT.  A 0   */
  /*  return here is ALSO "stored but not valid", so the oracle on an */
  /*  in-range call is identity-free: only the out-of-range half can  */
  /*  be asserted, which is exactly what the header promises.         */
  /*----------------------------------------------------------------*/

  for (i = 0; i < 256 && !Fails; ++i) {
    memcpy(F3, F3ref, F3sz);
    ++Calls;
    if (i >= BARE_ROUNDS) {
      ++Refusals;
      if (bracha87Fig3Accept(F3, (unsigned char)i, 1, 1, 0)
       || memcmp(F3, F3ref, F3sz)) {
        sprintf(Swhere, "bracha87Fig3Accept round=%u", i);
        fail("an out-of-range round was admitted, or moved a byte",
             Swhere);
      }
      if (bracha87Fig3ValidCount(F3, (unsigned char)i)
       || bracha87Fig3GetValid(F3, (unsigned char)i, senders, values)
       || bracha87Fig3RoundComplete(F3, (unsigned char)i)) {
        sprintf(Swhere, "bracha87Fig3 queries round=%u", i);
        fail("an out-of-range round answered", Swhere);
      }
    } else {
      ++Admitted;
      bracha87Fig3Accept(F3, (unsigned char)i, 1, 1, 0);
    }
  }
  for (i = 0; i < 256 && !Fails; ++i) {
    memcpy(F3, F3ref, F3sz);
    ++Calls;
    if (i >= CFG_N) {
      ++Refusals;
      if (bracha87Fig3Accept(F3, 0, (unsigned char)i, 1, 0)
       || memcmp(F3, F3ref, F3sz)) {
        sprintf(Swhere, "bracha87Fig3Accept sender=%u", i);
        fail("an out-of-range sender was admitted, or moved a byte",
             Swhere);
      }
    } else
      ++Admitted;
  }
  for (i = 0; i < 256; ++i) {
    unsigned int vc;

    memcpy(F3, F3ref, F3sz);
    ++Calls;
    ++Admitted;
    vc = 0xFFFF;
    bracha87Fig3Accept(F3, 0, 1, (unsigned char)i, &vc);
    if (vc > CFG_N) {
      sprintf(Swhere, "bracha87Fig3Accept value=0x%02x", i);
      fail("the validated count is above the process count", Swhere);
    }
  }

  /*----------------------------------------------------------------*/
  /*  Fig 4 -- k must be the machine's own next round.  Every other    */
  /*  k is refused, which is 255 of the 256 and the one real           */
  /*  sequence guard in the library.                                   */
  /*----------------------------------------------------------------*/

  memcpy(F4, F4ref, F4sz);
  nextK = F4->phase * BRACHA87_ROUNDS_PER_PHASE + F4->subRound;
  memset(vals, 0, sizeof (vals));
  for (i = 0; i < 256 && !Fails; ++i) {
    memcpy(F4, F4ref, F4sz);
    ++Calls;
    if (i != nextK) {
      ++Refusals;
      if (bracha87Fig4Round(F4, (unsigned char)i, CFG_N, vals)
       || memcmp(F4, F4ref, F4sz)) {
        sprintf(Swhere, "bracha87Fig4Round k=%u (next is %u)", i, nextK);
        fail("a round other than the machine's next was admitted, or"
             " moved a byte", Swhere);
      }
    } else
      ++Admitted;
  }
  /* An empty sample computes nothing and advances nothing -- the
   * header's own fourth reading of a 0 return. */
  memcpy(F4, F4ref, F4sz);
  ++Calls;
  ++Refusals;
  if (bracha87Fig4Round(F4, (unsigned char)nextK, 0, vals)
   || memcmp(F4, F4ref, F4sz))
    fail("an empty sample was admitted, or moved a byte",
         "bracha87Fig4Round n_msgs=0");
  /* Every value byte as CONTENT: Fig 4 counts only what strips to 0 or
   * 1, so a sample of arbitrary bytes must compute and advance without
   * leaving its domains. */
  for (i = 0; i < 256; ++i) {
    memcpy(F4, F4ref, F4sz);
    for (j = 0; j < CFG_N; ++j)
      vals[j] = (unsigned char)i;
    ++Calls;
    ++Admitted;
    bracha87Fig4Round(F4, (unsigned char)nextK, CFG_N, vals);
    if (F4->value > 1 && !(F4->value & BRACHA87_D_FLAG)) {
      sprintf(Swhere, "bracha87Fig4Round values=0x%02x", i);
      fail("the estimate left {0, 1} without carrying D_FLAG", Swhere);
    }
    if (F4->flags & ~(unsigned int)(BRACHA87_F4_DECIDED
                                  | BRACHA87_F4_EXHAUSTED)) {
      sprintf(Swhere, "bracha87Fig4Round values=0x%02x", i);
      fail("the instance carries undefined flag bits", Swhere);
    }
    if (F4->phase > CFG_PHASES
     || F4->subRound >= BRACHA87_ROUNDS_PER_PHASE) {
      sprintf(Swhere, "bracha87Fig4Round values=0x%02x", i);
      fail("phase or subRound left its space", Swhere);
    }
  }
  memcpy(F4, F4ref, F4sz);
}

/*--------------------------------------------------------------------------*/
/*  Main -- drive the honest cohort, then sweep every milestone.            */
/*--------------------------------------------------------------------------*/

int
main(
  int argc
 ,char *argv[]
){
  unsigned int i;

  (void)argc;
  (void)argv;

  printf("test_ingress: the ingress contract under an adversary who"
         " holds this library\n");

  ImgSz = bkr94acsSz(CFG_N - 1, CFG_VLEN - 1, CFG_PHASES);
  F1sz = bracha87Fig1Sz(CFG_N - 1, CFG_VLEN - 1);
  if (!ImgSz || !F1sz) {
    fprintf(stderr, "test_ingress: the configuration was refused\n");
    return (2);
  }
  if (!(Q = calloc(QCAP, sizeof (struct wire)))) {
    fprintf(stderr, "test_ingress: queue allocation failed\n");
    return (2);
  }
  for (i = 0; i < CFG_N; ++i)
    memset(Aval[i], (unsigned char)('A' + i), sizeof (Aval[i]));
  memcpy(Vbuf, Aval[1], sizeof (Vbuf));

  printf("\n  scope: content and field values are the adversary's"
         " (README obligation 2, \"arbitrary content\"); payload LENGTH"
         " and sender IDENTITY are the transport's (obligations 2 and"
         " 3) and are outside this instrument and outside the"
         " library\n");
  printf("  bounds: one hostile call against an honest state --"
         " sequences are the schedule explorer's; every unsigned char"
         " field swept over its full 0..255 range one at a time with"
         " the rest legal, plus the boundary neighborhood 0..n+1 as a"
         " cross product; all 256 packed wire bytes x 2 sender"
         " placements\n");
  printf("  config: n=%u t=%u vLen=%u maxPhases=%u, receiver = process"
         " %u; ACS image %lu bytes, bare Fig 1 %lu bytes\n",
         (unsigned)CFG_N, (unsigned)CFG_T, (unsigned)CFG_VLEN,
         (unsigned)CFG_PHASES, (unsigned)SELF, ImgSz, F1sz);
  printf("  domain BOUNDARIES are read off the headers, never off the"
         " code's own checks; for the three Input entries the rule that"
         " an out-of-domain argument must be REFUSED is this"
         " instrument's, since the headers state a returned refusal"
         " only for the accessors and for Init\n");
  printf("  a pass says the COMPOSITION refuses, not that any one guard"
         " does: a widened check another absorbs is invisible here\n");

  driveCohort();
  driveFig1();

  /* The standalone figure surfaces, each with a reference image the
   * sweep restores from -- and each driven a little first, so the
   * sweeps do not all run against a fresh machine that refuses
   * everything for reasons of its own. */
  F2sz = bracha87Fig2Sz(CFG_N - 1, BARE_ROUNDS);
  F3sz = bracha87Fig3Sz(CFG_N - 1, BARE_ROUNDS);
  F4sz = bracha87Fig4Sz(CFG_N - 1, CFG_PHASES);
  if (!F2sz || !F3sz || !F4sz
   || !(F2 = malloc(F2sz)) || !(F3 = malloc(F3sz))
   || !(F4 = malloc(F4sz)) || !(F2ref = malloc(F2sz))
   || !(F3ref = malloc(F3sz)) || !(F4ref = malloc(F4sz))) {
    fprintf(stderr, "test_ingress: figure allocation failed\n");
    return (2);
  }
  if (!bracha87Fig2Init(F2, CFG_N - 1, CFG_T, BARE_ROUNDS)
   || !bracha87Fig3Init(F3, CFG_N - 1, CFG_T, BARE_ROUNDS, bareNfn, 0)
   || !bracha87Fig4Init(F4, CFG_N - 1, CFG_T, CFG_PHASES, 1, 0,
                        demoCoin, 0)) {
    fprintf(stderr, "test_ingress: a figure refused the"
            " configuration\n");
    return (2);
  }
  for (i = 0; i < 3; ++i) {
    bracha87Fig2Receive(F2, 0, (unsigned char)i, 1);
    bracha87Fig3Accept(F3, 0, (unsigned char)i, 1, 0);
  }
  memcpy(F2ref, F2, F2sz);
  memcpy(F3ref, F3, F3sz);
  memcpy(F4ref, F4, F4sz);

  printf("\n  milestones reached by the honest cohort:");
  for (i = 0; i < MileCnt; ++i)
    printf("%s %s", i ? "," : "", MileName[i]);
  printf("\n  milestones reached by the bare Fig 1:");
  for (i = 0; i < F1mileCnt; ++i)
    printf("%s %s", i ? "," : "", F1mileName[i]);
  printf("\n");

  for (Cur = 0; Cur < MileCnt && !Fails; ++Cur) {
    unsigned long before;
    unsigned int q;
    const struct bkr94acs *ma;

    /* Print what each milestone IS, not just what it is called: a
     * reader can then see which of the image-domain oracle's arms the
     * sweep actually populates rather than taking the name for it. */
    memcpy(Img[SELF], Mile[Cur], ImgSz);
    ma = (const struct bkr94acs *)Img[SELF];
    before = Calls;
    sweepAcs();
    printf("  ACS   %-28s %7lu calls   complete=%u decisions=",
           MileName[Cur], Calls - before, (unsigned)ma->complete);
    for (q = 0; q < CFG_N; ++q)
      printf("%02x", bkr94acsBaDecision(ma, (unsigned char)q));
    printf(" entered=");
    for (q = 0; q < CFG_N; ++q)
      printf("%u", bkr94acsBaEntered(ma, (unsigned char)q) ? 1 : 0);
    printf("\n");
  }
  for (F1cur = 0; F1cur < F1mileCnt && !Fails; ++F1cur) {
    unsigned long before;

    before = Calls;
    sweepFig1();
    printf("  Fig 1 %-28s %7lu calls   flags=%02x\n",
           F1mileName[F1cur], Calls - before, (unsigned)F1->flags);
  }

  /* Only if nothing has failed yet: a defect already caught may well
   * be one that faults on the next call, and a fault would bury the
   * named oracle that caught it under a signal. */
  if (!Fails)
    sweepBare();

  printf("\n  %lu calls: %lu carried an argument tuple the domains above"
         " do not admit, %lu did not\n", Calls, Refusals, Admitted);
  printf("  (those two are the instrument's EXPECTATIONS, counted before"
         " the call; what the library did with them is what the oracles"
         " above assert, and %lu of those assertions failed)\n", Fails);
  if (!Fails)
    printf("  so: every refusal was RETURNED and never taken -- no entry"
           " aborted, and no call outside the domains moved a byte of"
           " the receiver\n");

  printf("\n=================================\n");
  printf("test_ingress: %s\n", Fails ? "FAILED" : "PASSED");

  for (i = 0; i < MileCnt; ++i)
    free(Mile[i]);
  for (i = 0; i < F1mileCnt; ++i)
    free(F1mile[i]);
  for (i = 0; i < CFG_N; ++i)
    free(Img[i]);
  free(F1);
  free(F2);
  free(F3);
  free(F4);
  free(F2ref);
  free(F3ref);
  free(F4ref);
  free(Q);
  return (Fails ? 1 : 0);
}
