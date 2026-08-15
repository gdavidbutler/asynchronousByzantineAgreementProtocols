/*
 * test/test_system_fidelity.c -- SPEC-DIRECT CORRESPONDENCE for the
 * system obligation layer.
 *
 * Every other instrument in this tree tests system.c against oracles
 * that reached it through the same chain the implementation did:
 * system.md -> system.dtc -> systemRules.c -> system.c.  A reading
 * error anywhere in that chain is invisible to all of them, because
 * they inherit it.  This file closes that gap the way
 * test/test_predicates.c closes it one layer down, where fig3IsValid
 * and fig4Nfn are anchored against paper-direct enumeration
 * references: it carries a SECOND, INDEPENDENT implementation of the
 * machine's decision surface, written from system.md's prose and
 * system.h's API contract ALONE, and drives the two in lockstep.
 *
 * INDEPENDENCE IS THE INSTRUMENT.  Nothing in this file was derived
 * from system.c, systemRules.c, system.dtc, or systemToC.dtc -- none
 * of the four was read while it was written, not even to check an
 * answer.  A reference that consulted the implementation would agree
 * with it by construction and would measure nothing.  Every reference
 * decision below carries a comment naming the system.md SECTION it
 * derives from (sections, not line numbers -- lines drift).  The
 * store is systemStore.[hc], the reference retention store, so a
 * divergence is a finding about the machine and not about two
 * hand-rolled stores (systemStore.h says exactly that).
 *
 * The reference is deliberately NAIVE: an explicit array of retained
 * rounds in the comparator's order, three per-round bitmaps beside
 * each, and quantifier loops that mirror the specification's own
 * sentences.  Clarity over speed everywhere -- the machine is the
 * artifact under test, and a clever reference is a second thing to
 * doubt.
 *
 * ROUND NAMES: the machine takes rounds as opaque rs-byte names under
 * one caller comparator (system.h, THE OPERATIONS ON A ROUND).  This
 * file drives the ORDINAL INSTANTIATION -- rs = sizeof (unsigned
 * long), a name is the ordinal's bytes, ordCmp is the header's
 * reference displacement comparator, and every close mints ordinal
 * succession -- so the reference's bookkeeping stays in ordinals
 * while the machine sees only names.  rn() and rv() convert at the
 * boundary.
 *
 * METHOD: breadth-first enumeration over the reachable states of the
 * PRODUCT -- one machine instance (its seat, its serve cursor, its
 * store) beside the reference model -- with the pair as the visited
 * key.  Every state is expanded by every action in the alphabet; at
 * every call the return value, every act (kind, round name, and the
 * want/have bytes it carries), every documented query, and the record
 * bytes the machine wrote into the store are compared against the
 * reference.  Keying on the pair is sound: if the pair repeats, every
 * future of it repeats too.  Sequence counts are pinned in-program
 * (ExpectStates / ExpectCalls) so a silent truncation -- a horizon
 * that stopped short, an alphabet slot that stopped resolving --
 * cannot pass as agreement.
 *
 * THE DRIVER IS A CALLER, and system.h states what a caller owes.
 * Each obligation and where it is discharged here:
 *   - the store is EMPTY at systemInit          systemStoreInit first
 *   - acts are consumed before the next call     compared immediately
 *   - the round argument names a RESOLVED place  every round argument
 *     is drawn from the reference's own frontier or retained set
 *   - the minted name FOLLOWS the frontier and every retained round
 *     (system.md, Mechanization status: succession)   next = F + 1,
 *     and every retained round is strictly behind F
 *   - the set changes only through retain/release   nothing here
 *     touches the store behind the machine
 *   - the premature possession indication is HELD and re-presented
 *     through systemPossessed once the close retains the round.  A
 *     driver cannot violate this one into a false disagreement: the
 *     machine's drop and the reference's drop are compared like any
 *     other answer, and the alphabet carries systemPossessed at every
 *     retained round, which IS the re-presentation.
 *
 * SCOPE.  Two shapes, both at reach 2 with self = 0:
 *   A  n = 2, t = 0   the specification's smallest deployment (the one
 *                     systemInit's refusal list still admits), where
 *                     n-t is n so the TOLERANCE class is unreachable
 *                     and the frontier holds on HELD alone
 *   B  n = 4, t = 1   the smallest with a TOLERANCE class, hence the
 *                     only one where the tolerance escape advances a
 *                     frontier and a close ever meets a full store
 * The alphabet, horizon, state and call counts, act tallies, and duty
 * classes are printed per shape by the run itself; the NOT-COVERED
 * list is printed at the end, so the artifact stands alone.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../system.h"
#include "../systemStore.h"

#define MAXN     4                     /* processes at the widest shape */
#define MAXBS    ((MAXN + 7) / 8)
#define MAXREACH 3
#define MAXSLOT  (MAXREACH + 3)
#define RS       ((unsigned int)sizeof (unsigned long))
#define ESELF    0

/*
 * How far the frontier may advance from the run's start.  The names
 * are unbounded (system.md Model: the position is unbounded), so the
 * enumeration needs a bound the specification does not supply.
 *
 * -DDEEP swaps the shapes below for ONE arm at reach 3 -- the retained
 * set three deep, where eviction's oldest-first and the serve
 * rotation's wrap have three rounds to be wrong about instead of two.
 * It is a separate build because it costs about four times the default
 * run; the default carries reach 2, where both rules are still
 * discriminated.
 */
#ifndef HORIZON
#ifdef DEEP
#define HORIZON 5
#else
#define HORIZON 7
#endif
#endif
#ifndef MAXSTATES
#ifdef DEEP
#define MAXSTATES 2000000u
#else
#define MAXSTATES 700000u
#endif
#endif
#ifndef TBLBITS
#ifdef DEEP
#define TBLBITS 24
#else
#define TBLBITS 22
#endif
#endif
#define TBLSZ (1u << TBLBITS)

#define TST(m, p) ((m)[(p) >> 3] & (1 << ((p) & 7)))
#define SET(m, p) ((m)[(p) >> 3] |= (unsigned char)(1 << ((p) & 7)))
#define CLR(m, p) ((m)[(p) >> 3] &= (unsigned char)~(1 << ((p) & 7)))

/* Action kinds -- one per entry point of system.h. */
#define K_RECV 0
#define K_LAUN 1
#define K_COMP 2
#define K_POSS 3
#define K_WITN 4
#define K_ASM  5
#define K_RST  6
#define K_EVI  7
#define K_SRV  8

/*
 * THE REFERENCE MODEL'S STATE.
 *
 * system.md, "The state invariant I", names the machine's state:
 * "the frontier F; the live / owed / adopt flags; the frontier
 * round's distinct-witness record W; and, per retained round r, the
 * possession record P_r, the still-owed record Q_r, and the
 * held-members record H_r".  Two more places are held because the
 * specification says the machine holds them and not the caller:
 * "the round below the frontier is the last-closed round -- history
 * the machine holds" (Mechanization status, on why predecessor is
 * not an operation), consumed by the R4 advance signal; and the
 * serve walk's cursor, which system.h places in CALLER storage and
 * which is therefore modeled here as the caller's byte too.
 *
 * The retained rounds are an explicit array in the comparator's
 * order -- the naive shape the seam describes ("A reference
 * retention store for system.h").
 */
struct ref {
  unsigned long f;                    /* the frontier */
  unsigned long last;                 /* the round below it: last closed */
  unsigned long cursor;               /* serve walk: round last served */
  unsigned long rnd[MAXREACH];        /* retained, ascending under cmp */
  unsigned int cnt;                   /* rounds retained now */
  unsigned char hasLast;              /* 0 until the first close */
  unsigned char hasCursor;            /* 0 until the first serve */
  unsigned char live;
  unsigned char owed;
  unsigned char adopt;                /* the single-fire ADOPT latch */
  unsigned char wit[MAXBS];           /* W: distinct witnesses of F */
  unsigned char pos[MAXREACH][MAXBS]; /* P_r */
  unsigned char wnt[MAXREACH][MAXBS]; /* Q_r */
  unsigned char hav[MAXREACH][MAXBS]; /* H_r */
};

/* One act the reference predicts. */
struct eact {
  unsigned long round;
  unsigned char want[MAXBS];
  unsigned char have[MAXBS];
  unsigned char act;
};

/* Shapes, in run order: (n encoding, t, reach, have variants at a
 * close, systemAssembled in the alphabet). */
#ifdef DEEP
static const unsigned char ShapeN[] = {3};
static const unsigned char ShapeT[] = {1};
static const unsigned char ShapeR[] = {3};
static const unsigned char ShapeH[] = {1};
static const unsigned char ShapeA[] = {0};
#else
static const unsigned char ShapeN[] = {1, 3};
static const unsigned char ShapeT[] = {0, 1};
static const unsigned char ShapeR[] = {2, 2};
static const unsigned char ShapeH[] = {2, 1};
static const unsigned char ShapeA[] = {1, 0};
#endif

/* Pinned so a truncated search cannot pass as agreement.  Regenerate
 * deliberately (the run prints what it saw) -- never to make a run
 * green. */
#ifdef DEEP
static const unsigned long ExpectStates[] = {1515908};
static const unsigned long ExpectCalls[] = {231591876};
#else
static const unsigned long ExpectStates[] = {1364, 489470};
static const unsigned long ExpectCalls[] = {99404, 65336902};
#endif

static unsigned long Checks;
static unsigned long Fails;

/* Rotating name pool: rn(v) yields a name pointer stable across the
 * enclosing call expression. */
static unsigned char RnPool[8][sizeof (unsigned long)];
static unsigned int RnI;

/*
 * The ordinal instantiation's comparator, verbatim from system.h's
 * systemInit note: displacement order, sound at any width and across
 * the ordinal's wrap.  The reference asks its order questions of
 * ordVCmp so the two cannot diverge -- system.h, "equal and order are
 * answered by ONE caller-supplied comparator".
 */
static int
ordVCmp(
  unsigned long a
 ,unsigned long b
){
  unsigned long d;

  if (!(d = b - a))
    return (0);
  return (d <= ((unsigned long)-1 >> 1) ? -1 : 1);
}

static int
ordCmp(
  void *ctx
 ,const unsigned char *a
 ,const unsigned char *b
){
  unsigned long av;
  unsigned long bv;

  (void)ctx;
  memcpy(&av, a, sizeof (av));
  memcpy(&bv, b, sizeof (bv));
  return (ordVCmp(av, bv));
}

/* An ordinal as a name the machine can take, and back again. */
static const unsigned char *
rn(
  unsigned long v
){
  unsigned char *p;

  p = RnPool[RnI];
  RnI = (RnI + 1) % (sizeof (RnPool) / sizeof (RnPool[0]));
  memcpy(p, &v, sizeof (v));
  return (p);
}

static unsigned long
rv(
  const unsigned char *p
){
  unsigned long v;

  memcpy(&v, p, sizeof (v));
  return (v);
}

int
main(
  void
){
  static const char *actName[] = {
    "?", "DELIVER", "SERVE", "JOIN", "ADMIT", "RELEASE", "ADOPT", "MAINTAIN"
  };
  struct systemAct out[SYSTEM_MAX_ACTS + 2];
  struct eact ea[SYSTEM_MAX_ACTS + 2];
  unsigned char *states;
  unsigned char *work;
  unsigned char *root;
  int *table;
  int *parent;
  unsigned short *acts;
  unsigned long slot[MAXSLOT];
  unsigned long stateSum;
  unsigned long callSum;
  unsigned long served;
  unsigned long released;
  unsigned long evicted;
  unsigned long evictedAtClose;
  unsigned long adopted;
  unsigned long delivered;
  unsigned long joined;
  unsigned long admitted;
  unsigned long maintained;
  unsigned long refused;
  unsigned long dutySeen[3];
  unsigned long sz;
  unsigned int shape;
  unsigned int en;
  unsigned int np;
  unsigned int et;
  unsigned int reach;
  unsigned int bs;
  unsigned int nHave;
  unsigned int asmOn;
  unsigned int rmax;
  unsigned int aRecv;
  unsigned int aLaun;
  unsigned int aComp;
  unsigned int aPoss;
  unsigned int aWitn;
  unsigned int aAsm;
  unsigned int nact;
  unsigned int czOff;
  unsigned int stOff;
  unsigned int rfOff;
  unsigned int slotLen;
  unsigned int nStates;
  unsigned int head;
  unsigned int nslot;
  unsigned int i;
  unsigned int j;
  unsigned int k;
  unsigned int a;
  unsigned int stop;

  printf("=================================================\n");
  printf("test_system_fidelity: spec-direct correspondence\n");
  printf("=================================================\n");

  sz = 0;
  for (shape = 0; shape < sizeof (ShapeN) / sizeof (ShapeN[0]); ++shape) {
    unsigned long z;

    if ((z = systemSz(ShapeN[shape], RS)) > sz)
      sz = z;
  }
  czOff = (unsigned int)((sz + sizeof (unsigned long) - 1)
   & ~(sizeof (unsigned long) - 1));
  stOff = (unsigned int)((czOff + systemCursorSz(RS)
   + sizeof (unsigned long) - 1) & ~(sizeof (unsigned long) - 1));
  rfOff = (unsigned int)((stOff + systemStoreSz(MAXN - 1, RS, MAXREACH)
   + sizeof (unsigned long) - 1) & ~(sizeof (unsigned long) - 1));
  slotLen = (unsigned int)((rfOff + sizeof (struct ref)
   + sizeof (unsigned long) - 1) & ~(sizeof (unsigned long) - 1));

  if (!(states = malloc((size_t)MAXSTATES * slotLen))
   || !(parent = malloc((size_t)MAXSTATES * sizeof (int)))
   || !(acts = malloc((size_t)MAXSTATES * sizeof (unsigned short)))
   || !(table = malloc((size_t)TBLSZ * sizeof (int)))
   || !(work = malloc(slotLen))
   || !(root = malloc(slotLen))) {
    fprintf(stderr, "allocation failed\n");
    return (2);
  }

  for (shape = 0; shape < sizeof (ShapeN) / sizeof (ShapeN[0]); ++shape) {
    struct system *s;
    struct systemStore *st;
    struct ref *rp;

    en = ShapeN[shape];
    et = ShapeT[shape];
    reach = ShapeR[shape];
    nHave = ShapeH[shape];
    asmOn = ShapeA[shape];
    np = en + 1;
    bs = (np + 7) / 8;
    rmax = reach + 3;

    /*
     * THE ALPHABET.  One block per entry point of system.h, with the
     * round argument drawn from a per-state slot list (below) and the
     * process argument from the whole roster -- including out-of-range
     * and self, which the specification says are refused (system.md,
     * The induction: "An entry that refuses its call -- an inert
     * instance, an out-of-range process, a round it does not speak
     * for -- writes nothing and preserves everything").
     */
    aRecv = rmax * np * 4;             /* round x from x possesses x cursor */
    aLaun = aRecv + 16;                /* the four self-local launch bits */
    aComp = aLaun + nHave;
    aPoss = aComp + rmax * np;
    aWitn = aPoss + rmax * np;
    aAsm = aWitn + (asmOn ? rmax * np : 0);
    nact = aAsm + 3;                   /* reset, evict, serve */

    printf("\n-- shape %c: n = %u, t = %u, reach %u, self %u --\n",
           (char)('A' + shape), np, et, reach, ESELF);
    printf("   alphabet %u actions, horizon %u frontier advances\n",
           nact, HORIZON);

    stateSum = 0;
    callSum = 0;
    served = 0;
    released = 0;
    evicted = 0;
    evictedAtClose = 0;
    adopted = 0;
    delivered = 0;
    joined = 0;
    admitted = 0;
    maintained = 0;
    refused = 0;
    dutySeen[0] = dutySeen[1] = dutySeen[2] = 0;
    stop = 0;

    /* The root: a zeroed buffer so padding is deterministic and the
     * product state key is exact. */
    memset(root, 0, slotLen);
    s = (struct system *)root;
    st = (struct systemStore *)(root + stOff);
    rp = (struct ref *)(root + rfOff);
    systemStoreInit(st, en, RS, reach, ordCmp, 0);
    systemInit(s, (unsigned char)en, (unsigned char)et, ESELF, RS, rn(0),
               systemStoreCmp, systemStoreRecords, systemStoreRetain,
               systemStoreRelease, systemStoreAfter, st);
    if (!systemWitnesses(s)) {
      fprintf(stderr, "configuration rejected by systemInit\n");
      return (2);
    }
    /*
     * BASE (system.md, The induction): "Initialization zeroes what it
     * holds -- W empty, the latch clear, the frontier at the first
     * round -- and CONSUMES an empty retained set".  The reference
     * establishes exactly that; the store's emptiness is the caller's
     * precondition, discharged by systemStoreInit above.
     */
    rp->f = 0;
    s->ctx = 0;

    for (i = 0; i < TBLSZ; ++i)
      table[i] = -1;
    memcpy(states, root, slotLen);
    parent[0] = -1;
    acts[0] = 0;
    nStates = 1;
    {
      unsigned long h;

      h = 2166136261UL;
      for (i = 0; i < slotLen; ++i) {
        h ^= states[i];
        h *= 16777619UL;
      }
      table[h & (TBLSZ - 1)] = 0;
    }

    for (head = 0; head < nStates && !stop; ++head) {

      /* The slot list is built from the REFERENCE's state: every
       * retained round, plus F-1, F, F+1 -- the rounds a caller
       * within reach can present (system.h: reach is bounded ahead by
       * the frontier and behind by what is retained). */
      rp = (struct ref *)(states + (size_t)head * slotLen + rfOff);
      slot[0] = rp->f + 1;
      slot[1] = rp->f;
      slot[2] = rp->f - 1;
      nslot = 3;
      for (j = 0; j < rp->cnt; ++j)
        slot[nslot++] = rp->rnd[j];

      for (a = 0; a < nact && !stop; ++a) {
        unsigned long rr;
        unsigned int kind;
        unsigned int si;
        unsigned int fp;
        unsigned int bits;
        unsigned int nEa;
        unsigned int na;
        unsigned int duty;
        unsigned int dutyWhy;
        unsigned int c;
        unsigned char hv[MAXBS];

        /* ---- decode ------------------------------------------- */
        si = 0;
        fp = 0;
        bits = 0;
        duty = SYSTEM_DUTY_MET;
        dutyWhy = 0;
        if (a < aRecv) {
          kind = K_RECV;
          si = a / (np * 4);
          fp = (a / 4) % np;
          bits = a % 4;
        } else if (a < aLaun) {
          kind = K_LAUN;
          bits = a - aRecv;
        } else if (a < aComp) {
          kind = K_COMP;
          si = 1;
          bits = a - aLaun;
        } else if (a < aPoss) {
          kind = K_POSS;
          si = (a - aComp) / np;
          fp = (a - aComp) % np;
        } else if (a < aWitn) {
          kind = K_WITN;
          si = (a - aPoss) / np;
          fp = (a - aPoss) % np;
        } else if (a < aAsm) {
          kind = K_ASM;
          si = (a - aWitn) / np;
          fp = (a - aWitn) % np;
        } else if (a == aAsm) {
          kind = K_RST;
        } else if (a == aAsm + 1) {
          kind = K_EVI;
        } else
          kind = K_SRV;
        if (si >= nslot)
          continue;

        /* Fresh copy of the parent for every action; ctx is a pointer
         * into the state's own span, so it is zero in the stored key
         * and re-pointed here. */
        memcpy(work, states + (size_t)head * slotLen, slotLen);
        s = (struct system *)work;
        st = (struct systemStore *)(work + stOff);
        rp = (struct ref *)(work + rfOff);
        s->ctx = st;
        rr = slot[si];

        if (kind == K_COMP && rp->f - 0 >= HORIZON)
          continue;

        for (j = 0; j < bs; ++j)
          hv[j] = 0;
        if (kind == K_COMP && bits)
          for (j = 0; j < np; ++j)
            SET(hv, j);

        /* ---- the machine ------------------------------------- */
        na = 0;
        switch (kind) {
        case K_RECV:
          na = systemReceived(s, rn(rr), (unsigned char)fp,
                              (unsigned char)(bits & 1),
                              (unsigned char)((bits >> 1) & 1), out);
          break;
        case K_LAUN:
          na = systemLaunch(s, (unsigned char)(bits & 1),
                            (unsigned char)((bits >> 1) & 1),
                            (unsigned char)((bits >> 2) & 1),
                            (unsigned char)((bits >> 3) & 1), out);
          break;
        case K_COMP:
          na = systemComplete(s, rn(rr), rn(rr + 1), bits ? hv : 0, out);
          break;
        case K_POSS:
          na = systemPossessed(s, rn(rr), (unsigned char)fp, out);
          break;
        case K_WITN:
          na = systemWitness(s, rn(rr), (unsigned char)fp, out);
          break;
        case K_ASM:
          systemAssembled(s, rn(rr), (unsigned char)fp);
          break;
        case K_RST:
          systemWitnessReset(s);
          break;
        case K_EVI:
          na = systemEvict(s, out);
          break;
        default:
          na = systemServe(s, work + czOff, out);
          break;
        }

        /* ---- the reference ----------------------------------- */
        nEa = 0;
        switch (kind) {

        case K_RECV:
          /*
           * system.md, The induction, RECEIVED; system.h,
           * systemReceived.  Order of the three regions is the
           * specification's: the refusals, then the possession
           * recording ("the possession record is updated FIRST, then
           * the dispatch classifies -- reads following writes"), then
           * the classification, then the Model's cursor birth.
           */
          if (fp >= np)
            break;                      /* an out-of-range process refuses */
          if (ordVCmp(rr, rp->f) > 0)
            break;                      /* Model: beyond chain reach, inert */
          if (!ordVCmp(rr, rp->f)) {
            if (rp->live) {
              /* "The frontier live: the answer is deliver; nothing
               * is written." */
              ea[nEa].act = SYSTEM_ACT_DELIVER;
              ea[nEa].round = rp->f;
              ++nEa;
            } else
              /* "The frontier not yet launched: participation owed
               * is recorded" -- PARTICIPATE's birth (R2a). */
              rp->owed = 1;
          } else {
            k = rp->cnt;
            for (j = 0; j < rp->cnt; ++j)
              if (!ordVCmp(rp->rnd[j], rr))
                k = j;
            if (k < rp->cnt) {
              /*
               * RECORD WRITES (The induction): possession recording
               * sets the given sender's possession bit and clears the
               * same sender's still-owed bit.  Only RETAINED rounds
               * have a record (system.h) -- an indication for a round
               * not retained is dropped, and the caller holds it.
               */
              if (bits & 1) {
                SET(rp->pos[k], fp);
                CLR(rp->wnt[k], fp);
              }
              c = 0;
              for (j = 0; j < np; ++j)
                if (TST(rp->pos[k], j))
                  ++c;
              if (c == np) {
                /*
                 * RETAIN, retire: RELEASE at all n.  I8 fixes the
                 * moment -- "exactly when the post-record reaches
                 * all n -- not earlier, not later".
                 */
                ea[nEa].act = SYSTEM_ACT_RELEASE;
                ea[nEa].round = rr;
                ++nEa;
                for (j = k + 1; j < rp->cnt; ++j) {
                  rp->rnd[j - 1] = rp->rnd[j];
                  memcpy(rp->pos[j - 1], rp->pos[j], MAXBS);
                  memcpy(rp->wnt[j - 1], rp->wnt[j], MAXBS);
                  memcpy(rp->hav[j - 1], rp->hav[j], MAXBS);
                }
                --rp->cnt;
                rp->rnd[rp->cnt] = 0;
                memset(rp->pos[rp->cnt], 0, MAXBS);
                memset(rp->wnt[rp->cnt], 0, MAXBS);
                memset(rp->hav[rp->cnt], 0, MAXBS);
              } else if (!TST(rp->pos[k], fp)) {
                /*
                 * SERVE, born: Model, want, DIRECT -- "an act of R
                 * received from a process which, per retained state,
                 * has not evidenced possession of R's composition".
                 * RECORD WRITES: "the rule answers yes only when that
                 * sender's post-record possession bit is clear".
                 */
                SET(rp->wnt[k], fp);
                ea[nEa].act = SYSTEM_ACT_SERVE;
                ea[nEa].round = rr;
                memcpy(ea[nEa].want, rp->wnt[k], MAXBS);
                memcpy(ea[nEa].have, rp->hav[k], MAXBS);
                ++nEa;
              }
            }
            /* "Released R: nothing is written." */
          }
          /*
           * Model, want, CURSOR: an act at its sender's authored
           * high-water "is want evidence for every retained round
           * after C whose possession that sender has not evidenced".
           * The induction: "only the given sender's still-owed bits,
           * only on retained rounds after the presented one, and only
           * where that sender's possession bit is clear".  Self is
           * inert by I3 (self is in P_r for every retained r), so no
           * separate guard is needed here.  No act is output.
           */
          if ((bits >> 1) & 1)
            for (j = 0; j < rp->cnt; ++j)
              if (ordVCmp(rp->rnd[j], rr) > 0 && !TST(rp->pos[j], fp))
                SET(rp->wnt[j], fp);
          break;

        case K_LAUN:
          /*
           * system.md, The calculus (PARTICIPATE / ADMIT), R4's
           * advance rule, and The induction, LAUNCH.  Model: "Each
           * process runs at most one non-COMPLETE instance at a
           * time", and R2b forbids a from-scratch second execution,
           * so a launch opportunity with an instance already live
           * answers nothing.
           */
          if (rp->live)
            break;
          dutyWhy = 1;
          goto refDuty;
        refDutyBack:
          ++dutySeen[duty];
          /*
           * R4: "SYSTEM_DUTY_MET advances; SYSTEM_DUTY_TOLERANCE
           * advances only with toleranceElapsed; SYSTEM_DUTY_HELD
           * holds regardless" (system.h), and "the tolerance applies
           * to JOINS as well as admissions".
           */
          if (duty == SYSTEM_DUTY_HELD)
            break;
          if (duty == SYSTEM_DUTY_TOLERANCE && !((bits >> 3) & 1))
            break;
          /*
           * The precedence is owed, then maintenance, then value
           * (Mechanization status: "join, then maintain, then
           * admit"; L4: "owed work forecloses both chosen acts, and
           * maintenance due forecloses admit").  R4 clause (1)
           * capacity-gates the CHOSEN advances only: "A join is
           * never capacity-gated".
           */
          if (rp->owed) {
            ea[nEa].act = SYSTEM_ACT_JOIN;
            ea[nEa].round = rp->f;
            ++nEa;
            rp->owed = 0;
            rp->live = 1;
          } else if (((bits >> 1) & 1) && ((bits >> 2) & 1)) {
            ea[nEa].act = SYSTEM_ACT_MAINTAIN;
            ea[nEa].round = rp->f;
            ++nEa;
            rp->live = 1;
          } else if ((bits & 1) && ((bits >> 2) & 1)) {
            ea[nEa].act = SYSTEM_ACT_ADMIT;
            ea[nEa].round = rp->f;
            ++nEa;
            rp->live = 1;
          }
          break;

        case K_COMP:
          /*
           * system.md, The induction, COMPLETE, in the call's own
           * order: (1) the refusals, (2) the budget eviction, (3) the
           * birth, (4) the advance.
           */
          if (!rp->live || ordVCmp(rr, rp->f))
            break;                      /* "the close speaks its round" */
          if (rp->cnt == reach) {
            /*
             * (2) "The release rule's eviction arm answers yes
             * exactly when the store has no room ... the earliest
             * retained entry -- the oldest, first in the order -- is
             * released and announced (I9, I11)."  A WITHDRAWAL, not
             * an all-n release (system.h, SYSTEM_ACT_RELEASE).
             */
            ea[nEa].act = SYSTEM_ACT_RELEASE;
            ea[nEa].round = rp->rnd[0];
            ++nEa;
            for (j = 1; j < rp->cnt; ++j) {
              rp->rnd[j - 1] = rp->rnd[j];
              memcpy(rp->pos[j - 1], rp->pos[j], MAXBS);
              memcpy(rp->wnt[j - 1], rp->wnt[j], MAXBS);
              memcpy(rp->hav[j - 1], rp->hav[j], MAXBS);
            }
            --rp->cnt;
          }
          /*
           * (3) I10b / I10c: the entry born is exactly the
           * pre-advance frontier, "possession exactly {self},
           * nothing owed, the held members as given".  Every birth
           * follows every retained round (A7), so it appends.
           */
          k = rp->cnt++;
          rp->rnd[k] = rp->f;
          memset(rp->pos[k], 0, MAXBS);
          memset(rp->wnt[k], 0, MAXBS);
          memcpy(rp->hav[k], hv, MAXBS);
          SET(rp->pos[k], ESELF);
          /*
           * (4) ADVANCE: "F moves to its successor ... and W, the
           * latch, and the live, owed, and adopt flags clear in the
           * same call -- I6's clear-on-advance."  Succession is
           * MINTED by the close (Mechanization status), so the new
           * frontier is the caller's 'next' argument.
           */
          rp->last = rp->f;
          rp->hasLast = 1;
          rp->f = rr + 1;
          rp->live = 0;
          rp->owed = 0;
          rp->adopt = 0;
          memset(rp->wit, 0, MAXBS);
          break;

        case K_POSS:
          /*
           * system.md, The induction, POSSESSED: "RECEIVED's
           * retained-possession case without the deliver and serve
           * arms".  system.h: "Idempotent; unretained rounds and
           * out-of-range indices are ignored."
           */
          if (fp >= np)
            break;
          k = rp->cnt;
          for (j = 0; j < rp->cnt; ++j)
            if (!ordVCmp(rp->rnd[j], rr))
              k = j;
          if (k >= rp->cnt)
            break;
          SET(rp->pos[k], fp);
          CLR(rp->wnt[k], fp);
          c = 0;
          for (j = 0; j < np; ++j)
            if (TST(rp->pos[k], j))
              ++c;
          if (c == np) {
            ea[nEa].act = SYSTEM_ACT_RELEASE;
            ea[nEa].round = rr;
            ++nEa;
            for (j = k + 1; j < rp->cnt; ++j) {
              rp->rnd[j - 1] = rp->rnd[j];
              memcpy(rp->pos[j - 1], rp->pos[j], MAXBS);
              memcpy(rp->wnt[j - 1], rp->wnt[j], MAXBS);
              memcpy(rp->hav[j - 1], rp->hav[j], MAXBS);
            }
            --rp->cnt;
            rp->rnd[rp->cnt] = 0;
            memset(rp->pos[rp->cnt], 0, MAXBS);
            memset(rp->wnt[rp->cnt], 0, MAXBS);
            memset(rp->hav[rp->cnt], 0, MAXBS);
          }
          break;

        case K_WITN:
          /*
           * system.md, The wanting side (O3: t+1 distinct servers),
           * and The induction, WITNESS: "The entry refuses
           * self as a server and any round but the frontier ... The
           * latch is set only at a post-record distinct count of t+1
           * or more".  I5, and BOOK WRITES: "the latch set exactly
           * when the post-record distinct count reaches t+1 with the
           * latch clear" -- the single fire.
           */
          if (fp >= np || fp == ESELF || ordVCmp(rr, rp->f))
            break;
          SET(rp->wit, fp);
          c = 0;
          for (j = 0; j < np; ++j)
            if (TST(rp->wit, j))
              ++c;
          if (c >= et + 1 && !rp->adopt) {
            rp->adopt = 1;
            ea[nEa].act = SYSTEM_ACT_ADOPT;
            ea[nEa].round = rp->f;
            ++nEa;
          }
          break;

        case K_ASM:
          /*
           * system.md, The induction, ASSEMBLED: "Writes one member
           * bit of a retained round's held-members record, which no
           * conjunct reads".  system.h: unretained rounds and
           * out-of-range members are ignored.
           */
          if (fp >= np)
            break;
          for (j = 0; j < rp->cnt; ++j)
            if (!ordVCmp(rp->rnd[j], rr))
              SET(rp->hav[j], fp);
          break;

        case K_RST:
          /* "WITNESS RESET.  Clears W and the latch together." */
          memset(rp->wit, 0, MAXBS);
          rp->adopt = 0;
          break;

        case K_EVI:
          /*
           * system.md, RETAIN's reach clause, and The induction,
           * EVICT: "With nothing retained
           * the call refuses.  Otherwise ... releases the earliest
           * retained entry -- the oldest, unique by I1 -- announced
           * (I9, I11)."  RETAIN's reach: oldest first is load-bearing
           * (L1's rung-by-rung climb).
           */
          if (!rp->cnt)
            break;
          ea[nEa].act = SYSTEM_ACT_RELEASE;
          ea[nEa].round = rp->rnd[0];
          ++nEa;
          for (j = 1; j < rp->cnt; ++j) {
            rp->rnd[j - 1] = rp->rnd[j];
            memcpy(rp->pos[j - 1], rp->pos[j], MAXBS);
            memcpy(rp->wnt[j - 1], rp->wnt[j], MAXBS);
            memcpy(rp->hav[j - 1], rp->hav[j], MAXBS);
          }
          --rp->cnt;
          rp->rnd[rp->cnt] = 0;
          memset(rp->pos[rp->cnt], 0, MAXBS);
          memset(rp->wnt[rp->cnt], 0, MAXBS);
          memset(rp->hav[rp->cnt], 0, MAXBS);
          break;

        default:
          /*
           * system.md, The induction, SERVE; system.h systemServe.
           * "Each call serves the first round still owed to some
           * process AFTER the one the last call served, wrapping to
           * the oldest owed round at the end of the retained set."
           * The cursor is the NAME of the round last served, so the
           * walk is positioned in the order itself; a cursor naming a
           * round since released still positions it.  The retained
           * array is ascending, so "after the cursor" is a suffix and
           * the wrap is the prefix -- the two passes below are one
           * full sweep, and only a sweep that finds nothing owed
           * returns 0.
           */
          k = rp->cnt;
          for (j = 0; j < rp->cnt && k == rp->cnt; ++j) {
            if (rp->hasCursor && ordVCmp(rp->rnd[j], rp->cursor) <= 0)
              continue;
            for (c = 0; c < bs; ++c)
              if (rp->wnt[j][c]) {
                k = j;
                break;
              }
          }
          if (k == rp->cnt && rp->hasCursor)
            for (j = 0; j < rp->cnt && k == rp->cnt; ++j) {
              if (ordVCmp(rp->rnd[j], rp->cursor) > 0)
                continue;
              for (c = 0; c < bs; ++c)
                if (rp->wnt[j][c]) {
                  k = j;
                  break;
                }
            }
          if (k < rp->cnt) {
            ea[nEa].act = SYSTEM_ACT_SERVE;
            ea[nEa].round = rp->rnd[k];
            memcpy(ea[nEa].want, rp->wnt[k], MAXBS);
            memcpy(ea[nEa].have, rp->hav[k], MAXBS);
            ++nEa;
            rp->cursor = rp->rnd[k];
            rp->hasCursor = 1;
          }
          break;
        }
        goto refDone;

        /*
         * THE R4 ADVANCE SIGNAL, reached from the launch region and
         * from the systemDuty comparison below -- one reading, two
         * consumers.  system.h, SYSTEM_DUTY_*: MET when "all n
         * possess it, or it is no longer retained (duty is bounded by
         * retention)"; TOLERANCE at n-t but short of all n; HELD
         * below n-t.  R4: "A round no longer retained reads as duty
         * met"; and before any close there is no round below the
         * frontier at all.
         */
      refDuty:
        duty = SYSTEM_DUTY_MET;
        if (rp->hasLast) {
          k = rp->cnt;
          for (j = 0; j < rp->cnt; ++j)
            if (!ordVCmp(rp->rnd[j], rp->last))
              k = j;
          if (k < rp->cnt) {
            c = 0;
            for (j = 0; j < np; ++j)
              if (TST(rp->pos[k], j))
                ++c;
            if (c == np)
              duty = SYSTEM_DUTY_MET;
            else if (c >= np - et)
              duty = SYSTEM_DUTY_TOLERANCE;
            else
              duty = SYSTEM_DUTY_HELD;
          }
        }
        if (dutyWhy)
          goto refDutyBack;
        goto refDutyQuery;

      refDone:
        /* ---- the acts ---------------------------------------- */
        ++callSum;
        ++Checks;
        if (na != nEa) {
          ++Fails;
          printf("FAIL: act count -- machine %u, reference %u\n", na, nEa);
          stop = 1;
        }
        for (j = 0; j < na && j < nEa && !stop; ++j) {
          ++Checks;
          if (out[j].act != ea[j].act) {
            ++Fails;
            printf("FAIL: act %u kind -- machine %s, reference %s\n", j,
                   out[j].act < sizeof (actName) / sizeof (actName[0])
                    ? actName[out[j].act] : "?",
                   ea[j].act < sizeof (actName) / sizeof (actName[0])
                    ? actName[ea[j].act] : "?");
            stop = 1;
            break;
          }
          ++Checks;
          if (!out[j].round || rv(out[j].round) != ea[j].round) {
            ++Fails;
            printf("FAIL: act %u round -- machine %lu, reference %lu\n",
                   j, out[j].round ? rv(out[j].round) : 0, ea[j].round);
            stop = 1;
            break;
          }
          ++Checks;
          if (out[j].act == SYSTEM_ACT_SERVE) {
            if (!out[j].want || !out[j].have
             || memcmp(out[j].want, ea[j].want, bs)
             || memcmp(out[j].have, ea[j].have, bs)) {
              ++Fails;
              printf("FAIL: SERVE want/have bytes at round %lu\n",
                     ea[j].round);
              stop = 1;
              break;
            }
          } else if (out[j].want || out[j].have) {
            ++Fails;
            printf("FAIL: act %u carries want/have outside SERVE\n", j);
            stop = 1;
            break;
          }
          switch (out[j].act) {
          case SYSTEM_ACT_DELIVER:  ++delivered;  break;
          case SYSTEM_ACT_SERVE:    ++served;     break;
          case SYSTEM_ACT_JOIN:     ++joined;     break;
          case SYSTEM_ACT_ADMIT:    ++admitted;   break;
          case SYSTEM_ACT_MAINTAIN: ++maintained; break;
          case SYSTEM_ACT_ADOPT:    ++adopted;    break;
          default:
            ++released;
            /* ONE act, TWO causes the caller must not conflate
             * (system.h, SYSTEM_ACT_RELEASE): a withdrawal at the
             * eviction entry or at a close with no room, an all-n
             * release everywhere else. */
            if (kind == K_EVI)
              ++evicted;
            else if (kind == K_COMP)
              ++evictedAtClose;
            break;
          }
        }
        if (!na && !nEa && kind != K_ASM && kind != K_RST)
          ++refused;
        if (stop)
          goto trace;

        /* ---- the queries ------------------------------------- */
        ++Checks;
        if (!systemFrontier(s) || rv(systemFrontier(s)) != rp->f) {
          ++Fails;
          printf("FAIL: frontier -- machine %lu, reference %lu\n",
                 systemFrontier(s) ? rv(systemFrontier(s)) : 0, rp->f);
          stop = 1;
          goto trace;
        }
        ++Checks;
        if (systemLive(s) != rp->live) {
          ++Fails;
          printf("FAIL: live -- machine %u, reference %u\n",
                 systemLive(s), rp->live);
          stop = 1;
          goto trace;
        }
        ++Checks;
        if (systemOwed(s) != rp->owed) {
          ++Fails;
          printf("FAIL: owed -- machine %u, reference %u\n",
                 systemOwed(s), rp->owed);
          stop = 1;
          goto trace;
        }
        dutyWhy = 0;
        goto refDuty;

      refDutyQuery:
        ++dutySeen[duty];
        ++Checks;
        if (systemDuty(s) != duty) {
          ++Fails;
          printf("FAIL: duty -- machine %u, reference %u\n",
                 systemDuty(s), duty);
          stop = 1;
          goto trace;
        }
        ++Checks;
        if (!systemWitnesses(s) || memcmp(systemWitnesses(s), rp->wit, bs)) {
          ++Fails;
          printf("FAIL: witness book bytes\n");
          stop = 1;
          goto trace;
        }

        /*
         * THE RETAINED SET AND ITS RECORDS.  'after' enumerates in
         * the comparator's order, strictly forward, each retained
         * round once (system.h, the retention requirements), so the
         * walk below is the machine's own account of what it directed
         * the store to hold -- compared name for name and byte for
         * byte against the reference's list.  The three slices are
         * laid end to end (systemRecordsSz): possession, still owed,
         * held members.
         */
        {
          const unsigned char *nm;
          const unsigned char *pv;
          unsigned char *rec;

          pv = 0;
          j = 0;
          while ((rec = systemStoreAfter(st, pv, &nm))) {
            ++Checks;
            if (j >= rp->cnt || rv(nm) != rp->rnd[j]) {
              ++Fails;
              printf("FAIL: retained round %u -- machine %lu, reference %s\n",
                     j, rv(nm), j >= rp->cnt ? "(none)" : "differs");
              stop = 1;
              break;
            }
            ++Checks;
            if (memcmp(rec, rp->pos[j], bs)
             || memcmp(rec + bs, rp->wnt[j], bs)
             || memcmp(rec + 2 * bs, rp->hav[j], bs)) {
              ++Fails;
              printf("FAIL: records of round %lu -- machine"
                     " %02x/%02x/%02x, reference %02x/%02x/%02x\n",
                     rv(nm), rec[0], rec[bs], rec[2 * bs],
                     rp->pos[j][0], rp->wnt[j][0], rp->hav[j][0]);
              stop = 1;
              break;
            }
            pv = nm;
            ++j;
          }
          if (!stop) {
            ++Checks;
            if (j != rp->cnt
             || systemStoreCount(st) != rp->cnt) {
              ++Fails;
              printf("FAIL: retained count -- walk %u, store %lu,"
                     " reference %u\n", j, systemStoreCount(st), rp->cnt);
              stop = 1;
            }
          }
          if (stop)
            goto trace;
        }

        /* The per-round queries, over every slot the state can be
         * asked about -- including the ones that are not retained,
         * where system.h documents a 0 answer. */
        for (i = 0; i < nslot && !stop; ++i) {
          k = rp->cnt;
          for (j = 0; j < rp->cnt; ++j)
            if (!ordVCmp(rp->rnd[j], slot[i]))
              k = j;
          ++Checks;
          if (systemRetained(s, rn(slot[i])) != (k < rp->cnt)) {
            ++Fails;
            printf("FAIL: retained(%lu) -- machine %u, reference %u\n",
                   slot[i], systemRetained(s, rn(slot[i])),
                   k < rp->cnt ? 1 : 0);
            stop = 1;
            break;
          }
          ++Checks;
          if (k < rp->cnt) {
            if (!systemPossess(s, rn(slot[i]))
             || !systemWant(s, rn(slot[i]))
             || !systemHave(s, rn(slot[i]))
             || memcmp(systemPossess(s, rn(slot[i])), rp->pos[k], bs)
             || memcmp(systemWant(s, rn(slot[i])), rp->wnt[k], bs)
             || memcmp(systemHave(s, rn(slot[i])), rp->hav[k], bs)) {
              ++Fails;
              printf("FAIL: slices of round %lu\n", slot[i]);
              stop = 1;
              break;
            }
          } else if (systemPossess(s, rn(slot[i]))
                  || systemWant(s, rn(slot[i]))
                  || systemHave(s, rn(slot[i]))) {
            ++Fails;
            printf("FAIL: slices of unretained round %lu are not 0\n",
                   slot[i]);
            stop = 1;
            break;
          }
        }
        if (stop)
          goto trace;

        /* ---- enqueue ----------------------------------------- */
        s->ctx = 0;
        {
          unsigned long h;
          unsigned int p;

          h = 2166136261UL;
          for (i = 0; i < slotLen; ++i) {
            h ^= work[i];
            h *= 16777619UL;
          }
          p = (unsigned int)(h & (TBLSZ - 1));
          while (table[p] >= 0) {
            if (!memcmp(states + (size_t)table[p] * slotLen, work, slotLen))
              break;
            p = (p + 1) & (TBLSZ - 1);
          }
          if (table[p] < 0) {
            if (nStates >= MAXSTATES) {
              fprintf(stderr, "state cap %u reached -- INCOMPLETE\n",
                      MAXSTATES);
              return (2);
            }
            memcpy(states + (size_t)nStates * slotLen, work, slotLen);
            parent[nStates] = (int)head;
            acts[nStates] = (unsigned short)a;
            table[p] = (int)nStates;
            ++nStates;
          }
        }
        continue;

      trace:
        /* The action path to the disagreement, root first. */
        printf("  action path from init:\n");
        {
          unsigned int pathN;
          unsigned int path[64];
          int q;

          pathN = 0;
          for (q = (int)head; q > 0 && pathN < sizeof (path) / sizeof (path[0]);
               q = parent[q])
            path[pathN++] = acts[q];
          while (pathN)
            printf("    a=%u\n", path[--pathN]);
          printf("    a=%u  <-- the disagreeing call\n", a);
        }
        break;
      }
    }

    stateSum = nStates;
    printf("   states %lu, calls %lu\n", stateSum, callSum);
    printf("   acts: deliver %lu, serve %lu, join %lu, admit %lu,"
           " maintain %lu, adopt %lu\n         release %lu -- all-n %lu,"
           " withdrawal at the eviction entry %lu,\n         withdrawal at a"
           " close with no room %lu\n",
           delivered, served, joined, admitted, maintained, adopted,
           released, released - evicted - evictedAtClose, evicted,
           evictedAtClose);
    printf("   duty classes seen: met %lu, tolerance %lu, held %lu\n",
           dutySeen[SYSTEM_DUTY_MET], dutySeen[SYSTEM_DUTY_TOLERANCE],
           dutySeen[SYSTEM_DUTY_HELD]);
    printf("   calls answering nothing (refusals and inert paths): %lu\n",
           refused);

    /* The pinned counts: a search that silently stopped short agrees
     * with the reference on everything it ran. */
    ++Checks;
    if (stateSum != ExpectStates[shape]) {
      ++Fails;
      printf("FAIL: state count %lu, expected %lu\n",
             stateSum, ExpectStates[shape]);
    }
    ++Checks;
    if (callSum != ExpectCalls[shape]) {
      ++Fails;
      printf("FAIL: call count %lu, expected %lu\n",
             callSum, ExpectCalls[shape]);
    }
    /* Non-vacuity: every act kind and every duty class the shape can
     * reach must have been reached. */
    ++Checks;
    if (!delivered || !served || !joined || !admitted || !maintained
     || !adopted || released <= evicted + evictedAtClose || !evicted
     || (et && !evictedAtClose) || (!et && evictedAtClose)) {
      ++Fails;
      printf("FAIL: an act kind or release cause was never exercised\n");
    }
    ++Checks;
    if (!dutySeen[SYSTEM_DUTY_MET] || !dutySeen[SYSTEM_DUTY_HELD]
     || (et && !dutySeen[SYSTEM_DUTY_TOLERANCE])
     || (!et && dutySeen[SYSTEM_DUTY_TOLERANCE])) {
      ++Fails;
      printf("FAIL: duty class coverage\n");
    }
  }

  free(root);
  free(work);
  free(table);
  free(acts);
  free(parent);
  free(states);

  printf("\nNOT covered by this instrument:\n");
  printf("  - reach 3 (build -DDEEP; one arm at n = 4, t = 1, its own"
         " pinned counts)\n");
  printf("  - shapes past n = 4, t = 1; the widest here is the smallest\n");
  printf("    with a TOLERANCE class, not a wide roster\n");
  printf("  - the frontier crossing the ordinal encoding's own wrap"
         " (the\n    comparator's to absorb -- test_system's wrap arm and"
         " the\n    invariant falsifier's deep-start run carry it)\n");
  printf("  - the held-members grain is VARIED only at shape A (both"
         " close\n    forms and the late-assembly ingress); shape B closes"
         " with none\n");
  printf("  - the serve cursor's BYTES (caller storage of an opaque"
         " layout);\n    only the walk's behavior across calls is"
         " compared\n");
  printf("  - systemInit's refusal list -- a rejected instance is inert,"
         " so\n    it enumerates nothing (test_system carries the"
         " refusals)\n");
  printf("  - anything above the API: PRESENT's byte staging, the fold"
         " ground,\n    the caller's t+1 grouping, the serve"
         " concurrency cap and pacing\n    (system.md, Mechanization"
         " status: deliberately caller-side)\n");

  printf("\n=================================================\n");
  printf("Checks: %lu  Fail: %lu\n", Checks, Fails);
  printf("=================================================\n");
  return (Fails ? 1 : 0);
}
