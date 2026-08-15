/*
 * test_system_invariant.c
 *
 * Exhaustive reachable-state enumeration of a single system instance,
 * checking the candidate state invariant I1-I11 of system.md ("Lemmas
 * and the state invariant") after every entry-point call.
 *
 * This is an INVARIANT FALSIFIER, not a coverage instrument.  Its
 * purpose is to break a candidate conjunct cheaply before the effort
 * of proving it is spent; a clean run is evidence the conjunct is
 * worth proving, never a substitute for the proof.  The lemmas are
 * discharged by hand against the obligations, in the manner of
 * Bracha87.txt and BKR94ACS.txt.
 *
 * ROUND NAMES: the machine takes rounds as opaque rs-byte names under
 * one caller comparator (system.h, the operations on a round).  This
 * file drives the ORDINAL INSTANTIATION -- rs = sizeof (unsigned
 * long), a name is the ordinal's bytes, ordCmp is the header's
 * reference displacement comparator, every close mints ordinal
 * succession -- and rn()/rv() convert at the boundary, so the
 * enumeration's bookkeeping stays in ordinals while the machine sees
 * only names.
 *
 * Method: breadth-first enumeration over the reachable states of one
 * instance.  A state is the entire caller-allocated buffer, the serve
 * cursor, AND THE RETENTION STORE, so state identity is exact and no
 * abstraction is assumed.  Every state is expanded by every action in
 * the alphabet.
 *
 * THE STORE IS PART OF THE STATE (ruled with the retention seam).
 * The retained rounds and their records are the caller's now, reached
 * through the four retention operations; they are reachable state the
 * machine DIRECTS but does not HOLD.  Keying on the seat alone would
 * merge two states that differ in what is retained -- the enumeration
 * would report a smaller reachable set and never visit the
 * transitions out of the merged half, which is exactly the failure
 * the morgue's in-use byte was found by.  So each BFS node carries
 * three spans laid end to end: the seat, the serve cursor, and the
 * store (the reference store systemStore.[hc], one per node).  The
 * seat's ctx is the ONE byte range excluded: it is a pointer INTO the
 * node's own span, so it says where the node lives and nothing about
 * the machine.  It is zeroed in the stored key and re-pointed on
 * every copy out.
 *
 * Round arguments are drawn PER STATE, from the rounds that state can
 * actually be asked about: every currently retained round, plus the
 * frontier and its immediate neighbors.  A fixed offset SPAN does NOT
 * suffice -- sibling rounds releasing at all-n keep the reach unfull, so a retained entry drifts arbitrarily far behind the
 * frontier and becomes unaddressable, taking its transitions and the
 * states beyond them out of the enumeration.  Rounds are listed by
 * ascending distance behind the frontier.
 *
 * The frontier is bounded by a horizon measured as distance from the
 * run's start.
 *
 * Two runs: one from a fresh instance, one from a frontier driven to
 * 252 -- a deep start whose horizon carries the frontier across 256,
 * the boundary where the retired byte-name world wrapped; names
 * cross it as a non-event, and the translation-invariance comparison
 * asserts exactly that.  (The ordinal instantiation's own width
 * boundary is the displacement comparator's to absorb -- covered by
 * contract in test_system section F's wrap arm; here the two runs'
 * canonical state sets are compared in-program, so the
 * translation-invariance claim is asserted rather than eyeballed.)
 *
 * Derived only from the documented contract in system.h and the
 * invariant statement in system.md.  The public struct fields (flags,
 * the frontier query) and the documented queries are the only state
 * read; no part of this file inspects system.c or the data[] layout.
 *
 * TWIN DRIVE -- L7's second half (-DHRTWIN, added 2026-07-25).
 *
 * The exclusion of the held-members grain described above is an
 * ASSUMPTION of the enumeration: H_r is held constant because no rule
 * reads it, which is exactly what L7's second half claims.  Compiled
 * with -DHRTWIN the run PROVES what it assumes instead of resting on
 * it.  Each BFS node then carries TWO buffers.  Twin A is driven
 * exactly as the default build drives it (completion with have = 0,
 * no late assembly), so its reachable set, its canonical descriptors,
 * and its state counts are byte-for-byte the default run's -- the
 * visited set and the horizon key on twin A alone, and -DEXPECTSTATES
 * asserts the count against the default build's.  Twin B takes the
 * SAME action sequence with the grain driven as far from twin A's as
 * the surface allows: every completion carries the all-n have bitmap,
 * and each is followed by a systemAssembled for one member, varied by
 * BFS index so the late-assembly ingress is exercised at every depth.
 *
 * After every expansion the twins are compared through the DOCUMENTED
 * QUERIES ONLY -- frontier, live, owed, duty, the serve cursor, the
 * witness book, and, over every retained round, retention itself and
 * the possession and want bitmaps -- plus each call's act count, act
 * kinds, act rounds, and the want bitmap a SERVE carries.  systemHave
 * and a SERVE act's .have are deliberately NOT compared: they are
 * where the grain is allowed, and expected, to differ.  Any other
 * divergence is H_r having entered a decision -- an L7 countermodel --
 * and is reported with the BFS action path like an invariant
 * violation.
 *
 * MATCHED RED (2026-07-25): MM_HR_GATES of the machine-mutant tier
 * below.  It gates systemServe on the held-members grain -- an owed
 * round is served only where H_r has a bit in the same byte -- which
 * is H_r entering a decision, and the twins diverge on the serve
 * cursor.  The pairing that gives that red its meaning ran in the
 * same session: the UNMUTATED machine under the same twin drive with
 * ZERO divergence and a counted differing-grain non-vacuity arm.
 * The PLAIN build of this file, on the same mutated machine, runs
 * CLEAN (the gate suppresses every serve): H_r is outside its
 * alphabet, which is precisely why this arm exists.  A clean HRTWIN
 * run was an UNFALSIFIED WITNESS for L7's second half until then; it
 * is now a validated check.
 *
 * THE MACHINE-MUTANT TIER (2026-07-25) -- this file's teeth.
 *
 * Every arm above ran only against a CORRECT system.c, so none of
 * them had been shown to CATCH a broken machine.  test/machineMutants
 * .sh (make machine-mutants) applies one anchored edit per mutant to
 * a SCRATCH COPY -- system.c is checksummed before and after the tier
 * and never written -- rebuilds this file and test_system.c against
 * each, and asserts the designated oracle fires.  The FAIL counts are
 * 9 for every mutant BY CONSTRUCTION (eight violations stop run 0 and
 * the ninth stops run 1), so what the tier records is WHICH conjunct
 * and the first-violation action path, not a count; the trailing
 * "runs differ" line is the same artifact, two truncated searches.
 * All at the frozen config, all red in seconds:
 *
 *   MM_I5_LATCH_AT_T     adopt guard t+1 -> t.  I5.
 *                        Also test_system H x4, J x2.
 *   MM_I5_COUNT_SELF     the from == self refusal dropped.  I5.
 *                        Also test_system H x2.
 *   MM_I6_BOOK_SURVIVES  completion no longer clears the book.  I6.
 *                        Also test_system H x4 and ALL TEN of J's
 *                        commutation scenarios -- the surviving book
 *                        is exactly the state difference the two
 *                        orders must not have.
 *   MM_I8_EARLY          all-n read one possession early.  I8.
 *                        Also test_system C, D.
 *   MM_I9_SLOT_NOFREE    the completion eviction reuses its slot with
 *                        no RELEASE act.  I9, the retained-set shadow
 *                        -- a departure nothing announced.  Also
 *                        test_system E, F, J.
 *   MM_I10_RETAIN_WRONG  the born entry takes the prior round's name.
 *                        I10.  Also test_system B, C, D.
 *   MM_I11_EVICT_NEWEST  both bound eviction sites take the newest.
 *                        I11.  Also test_system E x3 and J's
 *                        exhausted-reach non-vacuity arm.
 *   MM_CURSOR_UNGATED    the cursor walk's possession gate dropped.
 *                        I2's matched red; also test_system K.
 *   MM_HR_GATES          the -DHRTWIN red; see MATCHED RED above.
 *   MM_WRAP_LOOKAHEAD_SKIP
 *                        RETIRED with the round-name widening (the
 *                        byte dissolution): the frontier+1 wrap
 *                        lookahead release it removed no longer
 *                        exists -- names never recur (the caller's
 *                        obligation under the round abstraction), so
 *                        no structural release guards the name and
 *                        there is nothing to skip.  test_system
 *                        section F pins the contract from the other
 *                        side (deep retention, names never recur,
 *                        the ordinal wrap in the comparator).
 *
 * Header encoding convention (CRITICAL):
 *   n parameter is encoded; actual process count = n + 1
 *   ER is NOT encoded: it is the recovery reach itself, the count of
 *   rounds the store will hold.  (The retired 'w' argument encoded
 *   reach - 1; ER = 2 is the old EW = 1.)
 *
 * Configurations run clean (override EN/ET/ER/HORIZON/MAXSTATES/
 * TBLBITS on the command line; raise MAXSTATES and TBLBITS together
 * until the state cap is no longer reported, since a truncated
 * search exits non-zero rather than reading as a pass):
 *
 *   EN=3 ET=1 ER=2 HORIZON=6     187,550 states / 20,891,344 transitions
 *
 *     RE-FROZEN with the retention seam, from 621,286 / 59,371,888.
 *     The reachable set SHRANK, and both causes are the store having
 *     replaced a fixed slot array -- nothing observable was lost, the
 *     canonical descriptor was already blind to both, and the two
 *     runs' state sets still match exactly:
 *
 *       PLACEMENT.  A retained round used to live in one of ER FIXED
 *       SLOTS and the slot index was in the key, so one retained set
 *       appeared once per assignment of its rounds to slots.  The
 *       store keeps entries in the comparator's order, so placement
 *       is the round order and there is nothing to permute.  At
 *       ER = 2 that is a factor of exactly 2 wherever a round is
 *       held.
 *       RESIDUE.  A freed slot kept its round name and its bitmaps
 *       -- only the in-use byte was cleared -- so a state carried the
 *       ghost of what it had released.  A released entry is cleared
 *       whole now (systemStore.c release, which is where that
 *       property is stated and why).
 *
 *     The horizon-3 cross-check separates them: 25,960 pre-seam
 *     against 13,177 now is 1.97, essentially placement alone,
 *     because few states that shallow have released anything.  By
 *     horizon 6 releases are common and the factor rises to 3.31.
 *     Residue's own weight was MEASURED during the port, on the
 *     store side, before it was cleared: 58,882 states against
 *     13,177 at horizon 3, a 4.5x inflation from bytes no operation
 *     can read.
 *
 *     The pre-seam figure's own history: 621,094 / 59.4M under the
 *     pre-abstraction unsigned-long machine, and the growth to
 *     621,286 was the MORGUE -- a state fresh from an evicting close
 *     differed from its otherwise-identical neighbor until the next
 *     proceeding call re-zeroed the borrow.  The morgue's in-use byte
 *     was FORCED by this file's own cross-run comparison: without it,
 *     run 0's evicted round 0 -- an all-zero name -- merged with the
 *     cleared-morgue twin while run 1's round 252 stayed distinct,
 *     25,948 vs 25,960 at horizon 3 and a 48-state asymmetry at
 *     horizon 6.  THE SAME DETECTOR FIRED TWICE MORE at the seam, and
 *     both times it was the instrument or the store, not the machine:
 *     the store's freed-entry residue above, and a preamble that ends
 *     on a RELEASE, leaving run 1's root carrying a borrow a fresh
 *     instance has no counterpart for (see the preamble).
 *   EN=3 ET=1 ER=2 HORIZON=9   (recorded pre-abstraction: 2,002,192)
 *   EN=6 ET=2 ER=2 HORIZON=2   (recorded pre-abstraction:   475,538)
 *   EN=6 ET=2 ER=1 HORIZON=6   (recorded pre-abstraction: 1,188,993)
 *
 * At t=2 the space explodes past horizon 2: HORIZON=3 exceeds
 * 24,000,000 states, and HORIZON 4/5/6 exhaust the same cap at
 * nearly identical transition counts -- the cap is reached before the
 * horizon binds, so raising the horizon alone buys nothing.  The
 * reachable count there is order 1e9-1e10 (per retained round, self
 * is fixed in possession and each other process is possessing /
 * wanting / neither, 3^6 per round, squared over two retained rounds,
 * times the witness record), so no cap that fits in memory completes
 * it.  Exhaustive t=2 needs a shorter reach (ER=1) or a symmetry
 * reduction over the non-self processes, which no rule distinguishes.
 *
 * Style: C89, K&R, 2-space indent, single monolithic main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "systemStore.h"

/* n = 3 encoded -> 4 processes, t = 1 (n-t = 3), reach 2, self = 0 */
#ifndef EN
#define EN 3
#endif
#ifndef ET
#define ET 1
#endif
#ifndef ER
#define ER 2
#endif
#define ESELF 0
#define NP (EN + 1)
#define NR ER
#define BS ((NP + 7) / 8)

/* The ordinal instantiation's name size. */
#define RS ((unsigned int)sizeof (unsigned long))

/*
 * Round slots per state: every retained round, plus F-1, F, F+1.
 * Slots past the state's actual round count are skipped.  (At a
 * fresh instance F-1 sits behind the genesis and probes as a
 * released round -- inert, deliberately kept in the alphabet.)
 */
#define RMAX (NR + 3)

/*
 * Action alphabet, with R = RMAX round slots and P = NP senders:
 *
 *   [0, A_RECV)       systemReceived   (R x P x 2 possesses x 2 cursor)
 *   [A_RECV, A_LAUN)  systemLaunch     (4 self-local input bits)
 *   [A_LAUN, A_COMP)  systemComplete   (have = none)
 *   [A_COMP, A_POSS)  systemPossessed  (R x P)
 *   [A_POSS, A_WITN)  systemWitness    (R x P)
 *   A_WITN            systemWitnessReset
 *   A_WITN + 1        systemEvict
 *   A_WITN + 2        systemServe
 *
 * The held-members grain is deliberately absent from the alphabet:
 * systemAssembled and a non-empty 'have' write only H_r, which
 * appears in NO conjunct of the invariant and in no advance or
 * release decision (system.md L7, second half).  Carrying it as a
 * state dimension would multiply the space by 2^(members x retained
 * rounds) for no invariant coverage, and that alone exhausted a
 * 4,000,000-state cap.  Holding it constant is what makes the
 * remaining space exhaustible -- and -DHRTWIN discharges the
 * assumption that makes holding it constant sound, at twin-drive
 * cost rather than state-space cost (see the header).
 */
#define A_RECV (4 * RMAX * NP)
#define A_LAUN (A_RECV + 16)
#define A_COMP (A_LAUN + 1)
#define A_POSS (A_COMP + RMAX * NP)
#define A_WITN (A_POSS + RMAX * NP)
#define NACT   (A_WITN + 3)

#ifndef HORIZON
#define HORIZON 6
#endif
#ifndef MAXSTATES
#define MAXSTATES 4000000u
#endif
#ifndef TBLBITS
#define TBLBITS 23
#endif
#define TBLSZ (1u << TBLBITS)

/* Canonical descriptor: frontier offset, flags, witnesses, then each
 * retained round by ascending distance behind the frontier. */
#define DESCLEN (2 + BS + NR * (1 + 3 * BS))

#define BIT_SET(map, p) ((map)[(p) >> 3] |= (unsigned char)(1 << ((p) & 7)))
#define BAD(n) do { if (!bad) bad = (n); } while (0)

/* The header's reference comparator: displacement order, sound at
 * any width and across the ordinal's wrap. */
static int
ordCmp(
  void *ctx
 ,const unsigned char *a
 ,const unsigned char *b
){
  unsigned long av;
  unsigned long bv;
  unsigned long d;

  (void)ctx;
  memcpy(&av, a, sizeof (av));
  memcpy(&bv, b, sizeof (bv));
  if (!(d = bv - av))
    return (0);
  return (d <= ((unsigned long)-1 >> 1) ? -1 : 1);
}

/* Rotating name pool: rn(v) yields a name pointer stable across the
 * enclosing call expression. */
static unsigned char RnPool[8][sizeof (unsigned long)];
static unsigned int RnI;

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

/* Read a borrowed name back as its ordinal. */
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
  static const char *inv[] = {
    "", "I1 reach bound", "I2 possess/want disjoint", "I3 self possesses",
    "I4 retained short of all n", "I5 adopt latch witnessed",
    "I6 witness state clear on advance", "I7 frontier not retained",
    "I8 release exactly at all-n",
    "I9 rounds leave retention only by RELEASE",
    "I10 rounds enter retention only at completion, as the completed round",
    "I11 eviction releases the oldest retained round",
    "L7 H_r non-interference (twin drive)"
  };
  unsigned char *states;
  int *parent;
  unsigned char *acts;
  int *table;
  unsigned char *work;
  unsigned char *root;
  unsigned char have[BS];
  unsigned char desc[DESCLEN];
  unsigned long runSum[2];
  unsigned int runStates[2];
  struct systemAct out[SYSTEM_MAX_ACTS + 2];
#ifdef HRTWIN
  struct systemAct outB[SYSTEM_MAX_ACTS + 2];
#endif
  unsigned long sz;
  unsigned int czOff;
  unsigned int stOff;
  unsigned int keyLen;
  unsigned int slotLen;
  unsigned int nStates;
  unsigned int head;
  unsigned int run;
  unsigned int i;
  unsigned int a;
  unsigned int failures;
  unsigned long expanded;
#ifdef HRTWIN
  unsigned long hrGrain;
#endif

  sz = systemSz(EN, RS);
  /* the serve cursor and then the store ride behind the machine
   * state, each at an aligned offset; the pad bytes are zeroed with
   * the buffer and never written, so state identity stays exact */
  czOff = (unsigned int)((sz + sizeof (unsigned long) - 1)
   & ~(sizeof (unsigned long) - 1));
  stOff = (unsigned int)((czOff + systemCursorSz(RS)
   + sizeof (unsigned long) - 1) & ~(sizeof (unsigned long) - 1));
  keyLen = (unsigned int)(stOff + systemStoreSz(EN, RS, NR));
  /* the visited key is twin A's alone; twin B rides in the slot
   * behind it, so twin A's reachable set is the default build's */
#ifdef HRTWIN
  slotLen = 2 * keyLen;
#else
  slotLen = keyLen;
#endif
  failures = 0;

  if (!(states = malloc((size_t)MAXSTATES * slotLen))
   || !(parent = malloc((size_t)MAXSTATES * sizeof (int)))
   || !(acts = malloc(MAXSTATES))
   || !(table = malloc((size_t)TBLSZ * sizeof (int)))
   || !(work = malloc(slotLen))
   || !(root = malloc(slotLen))) {
    fprintf(stderr, "allocation failed\n");
    return (2);
  }

  for (i = 0; i < BS; ++i)
    have[i] = 0;
  for (i = 0; i < NP; ++i)
    BIT_SET(have, i);

  for (run = 0; run < 2; ++run) {
    struct system *s;
#ifdef HRTWIN
    struct system *sB;
#endif
    unsigned long startF;

    /* Root state.  The buffer is zeroed first so padding is
     * deterministic and state identity is exact. */
    memset(root, 0, slotLen);
    s = (struct system *)root;
    systemStoreInit((struct systemStore *)(root + stOff), EN, RS, NR,
                    ordCmp, 0);
    systemInit(s, EN, ET, ESELF, RS, rn(0), systemStoreCmp,
               systemStoreRecords, systemStoreRetain, systemStoreRelease,
               systemStoreAfter, root + stOff);
#ifdef HRTWIN
    sB = (struct system *)(root + keyLen);
    systemStoreInit((struct systemStore *)(root + keyLen + stOff), EN, RS, NR,
                    ordCmp, 0);
    systemInit(sB, EN, ET, ESELF, RS, rn(0), systemStoreCmp,
               systemStoreRecords, systemStoreRetain, systemStoreRelease,
               systemStoreAfter, root + keyLen + stOff);
#endif
    if (!systemWitnesses(s)) {
      /* the documented rejected-instance probe: a rejected instance
       * is inert, so enumerating it asserts nothing */
      fprintf(stderr, "configuration rejected by systemInit\n");
      return (2);
    }

    if (run) {
      /* Drive the frontier to just below the byte world's old wrap
       * crossing.  Each step: admit, complete (minting ordinal
       * succession), then feed every process's possession of the
       * round just retained -- reaching all n releases it, so the
       * store stays empty and the next duty class reads MET. */
      while (rv(systemFrontier(s)) != 252) {
        unsigned long prior;

        if (!systemLaunch(s, 1, 0, 1, 1, out)) {
          fprintf(stderr, "preamble: launch refused at %lu\n",
                  rv(systemFrontier(s)));
          return (2);
        }
        prior = rv(systemFrontier(s));
        systemComplete(s, systemFrontier(s), rn(prior + 1), have, out);
        for (i = 1; i < NP; ++i)
          systemPossessed(s, rn(prior), (unsigned char)i, out);
      }
#ifdef HRTWIN
      /* the same preamble on twin B, under twin B's grain
       * convention -- every round it retains releases at all n, so
       * the two roots differ in no query */
      while (rv(systemFrontier(sB)) != 252) {
        unsigned long prior;

        if (!systemLaunch(sB, 1, 0, 1, 1, out)) {
          fprintf(stderr, "preamble: twin B launch refused at %lu\n",
                  rv(systemFrontier(sB)));
          return (2);
        }
        prior = rv(systemFrontier(sB));
        systemComplete(sB, systemFrontier(sB), rn(prior + 1), have, out);
        systemAssembled(sB, rn(prior), (unsigned char)(prior % NP));
        for (i = 1; i < NP; ++i)
          systemPossessed(sB, rn(prior), (unsigned char)i, out);
      }
      systemWitnessReset(sB);
#endif
      /*
       * THE ROOTS MUST BE EQUIVALENT for the translation-invariance
       * comparison to mean anything, and this preamble ends on a
       * RELEASE -- so the seat still carries that round's borrow,
       * which a fresh instance has no counterpart for.  One call
       * that proceeds and changes nothing else retires it (the book
       * and the latch are already clear here).  It became necessary
       * with the retention seam: a release now runs through the
       * caller's own operation and takes the borrow with it, where
       * before only the no-room close did.  Without this, run 1
       * reaches one state run 0 does not -- its root, plus the twin
       * the first proceeding call makes of it.
       */
      systemWitnessReset(s);
    }
    startF = rv(systemFrontier(s));

    /* the root becomes a stored key from here on, so its ctx -- a
     * pointer into 'root' itself -- is normalized out; every copy
     * into 'work' re-points it at the working span */
    s->ctx = 0;
#ifdef HRTWIN
    sB->ctx = 0;
#endif

    for (i = 0; i < TBLSZ; ++i)
      table[i] = -1;
    nStates = 0;
    expanded = 0;
    runSum[run] = 0;
#ifdef HRTWIN
    hrGrain = 0;
#endif

    memcpy(states, root, slotLen);
    parent[0] = -1;
    acts[0] = 0;
    nStates = 1;
    {
      unsigned long h;

      for (h = 2166136261UL, i = 0; i < keyLen; ++i) {
        h ^= root[i];
        h *= 16777619UL;
      }
      table[h & (TBLSZ - 1)] = 0;
    }

    for (head = 0; head < nStates; ++head) {
      for (a = 0; a < NACT; ++a) {
        struct system *v;
#ifdef HRTWIN
        struct system *vB;
        const char *hrWhy;
        unsigned char *cursorB;
        unsigned long bRnd[SYSTEM_MAX_ACTS];
        unsigned char bWant[SYSTEM_MAX_ACTS][BS];
        unsigned char bWantOk[SYSTEM_MAX_ACTS];
        unsigned int nactsB;
        unsigned char bSnap;
#endif
        /*
         * TWIN A'S ACTS, SNAPSHOTTED.  An act's .round and .want are
         * BORROWS -- valid only until the next call into the library
         * (system.h) -- and some of them point at the caller's own
         * storage: a SERVE born at a received event hands back the
         * ROUND ARGUMENT, and its .want points into the record the
         * store holds.  Everything below reads acts long after the
         * call, across hundreds of query calls, so the borrows are
         * consumed HERE and nothing downstream touches out[] again.
         * (The twin drive already did this for twin B; it is the same
         * discipline, and twin A needed it once a SERVE's round
         * stopped being machine storage.)
         */
        unsigned char aAct[SYSTEM_MAX_ACTS];
        unsigned long aRnd[SYSTEM_MAX_ACTS];
        unsigned char aWant[SYSTEM_MAX_ACTS][BS];
        unsigned char aWantOk[SYSTEM_MAX_ACTS];
        const unsigned char *p;
        const unsigned char *q;
        unsigned char *cursor;
        unsigned long roundList[RMAX];
        unsigned long prevF;
        unsigned long rnd;
        unsigned long probeRound;
        unsigned long oldestRound;
        unsigned long pos;
        unsigned char prePoss[BS];
        unsigned char preRet[40];
        unsigned char postRet[40];
        unsigned char relRet[40];
        unsigned char probeFrom;
        unsigned char probeRecord;
        unsigned char probeCursor;
        unsigned char probePath;
        unsigned char probeRelease;
        unsigned char haveOldest;
        unsigned int nRounds;
        unsigned int slot;
        unsigned int nacts;
        unsigned int bad;
        unsigned int cnt;
        unsigned int d;
        unsigned int r;
        unsigned int b;

        memcpy(work, states + (size_t)head * slotLen, slotLen);
        v = (struct system *)work;
        v->ctx = work + stOff;
        cursor = work + czOff;
#ifdef HRTWIN
        vB = (struct system *)(work + keyLen);
        vB->ctx = work + keyLen + stOff;
        cursorB = work + keyLen + czOff;
        hrWhy = 0;
#endif
        prevF = rv(systemFrontier(v));

        /* Horizon: distance from this run's start. */
        if (prevF - startF >= HORIZON)
          continue;

        /*
         * Per-state round alphabet: the frontier and its neighbors,
         * then every retained round by ascending distance behind the
         * frontier.  A fixed offset
         * span is not sufficient (see the header note).
         */
        roundList[0] = prevF - 1;
        roundList[1] = prevF;
        roundList[2] = prevF + 1;
        nRounds = 3;
        for (d = 2; d < 258 && nRounds < RMAX; ++d) {
          rnd = prevF - d;
          if (systemRetained(v, rn(rnd)))
            roundList[nRounds++] = rnd;
        }

        /* I9 / I10 need the retained set on both sides of the call.
         * I11 needs the oldest of it: the earliest name, which the
         * ascending-distance scan leaves last.  The tracked range is
         * ordinals startF-1 .. startF+318, bit-indexed as
         * pos + 1 - startF -- births land in [startF, startF+HORIZON)
         * under a correct machine, and the -1 margin keeps a mutated
         * machine's wrong-name birth visible instead of unmapped
         * (the retain-wrong mutant births the prior name at the
         * root).  A release naming an ordinal outside the range is
         * itself a violation, never silently unmapped. */
        memset(preRet, 0, sizeof (preRet));
        for (r = 0; r < 320; ++r) {
          pos = startF - 1 + r;
          if (systemRetained(v, rn(pos)))
            BIT_SET(preRet, r);
        }
        haveOldest = 0;
        oldestRound = 0;
        for (d = 1; d < 258; ++d)
          if (systemRetained(v, rn(prevF - d))) {
            oldestRound = prevF - d;
            haveOldest = 1;
          }

        /*
         * I8's reconstruction: the possession record as it will stand
         * after this call records its bit, captured before the call
         * consumes it.  Only the possession paths are checked --
         * systemComplete and systemEvict release by eviction, which
         * system.md RETAIN permits below all-n.
         */
        probePath = 0;
        probeRelease = 0;
        probeRound = 0;
        probeFrom = 0;
        probeRecord = 0;
        probeCursor = 0;
        if (a < A_RECV) {
          slot = a / (4 * NP);
          if (slot >= nRounds)
            continue;
          b = a % (4 * NP);
          probeRound = roundList[slot];
          probeFrom = (unsigned char)(b / 4);
          probeRecord = (unsigned char)(b % 2);
          probeCursor = (unsigned char)((b / 2) % 2);
          probePath = 1;
        } else if (a >= A_COMP && a < A_POSS) {
          b = a - A_COMP;
          slot = b / NP;
          if (slot >= nRounds)
            continue;
          probeRound = roundList[slot];
          probeFrom = (unsigned char)(b % NP);
          probeRecord = 1;
          probePath = 1;
        } else if (a >= A_POSS && a < A_WITN) {
          if ((a - A_POSS) / NP >= nRounds)
            continue;
        }
        if (probePath && probeRecord
         && systemRetained(v, rn(probeRound))
         && (p = systemPossess(v, rn(probeRound)))) {
          memcpy(prePoss, p, BS);
          BIT_SET(prePoss, probeFrom);
          for (cnt = 0, b = 0; b < BS; ++b)
            for (i = 0; i < 8; ++i)
              if (prePoss[b] & (1 << i))
                ++cnt;
          probeRelease = (unsigned char)(cnt == NP);
        }

        out[SYSTEM_MAX_ACTS].act = 0xA5;
        out[SYSTEM_MAX_ACTS + 1].act = 0x5A;

        if (a < A_RECV) {
          nacts = systemReceived(v, rn(probeRound), probeFrom, probeRecord,
                                 probeCursor, out);
        } else if (a < A_LAUN) {
          b = a - A_RECV;
          nacts = systemLaunch(v, (unsigned char)(b & 1),
                               (unsigned char)((b >> 1) & 1),
                               (unsigned char)((b >> 2) & 1),
                               (unsigned char)((b >> 3) & 1), out);
        } else if (a < A_COMP) {
          nacts = systemComplete(v, systemFrontier(v),
                                 rn(rv(systemFrontier(v)) + 1), 0, out);
        } else if (a < A_POSS) {
          nacts = systemPossessed(v, rn(probeRound), probeFrom, out);
        } else if (a < A_WITN) {
          b = a - A_POSS;
          nacts = systemWitness(v, rn(roundList[b / NP]),
                                (unsigned char)(b % NP), out);
        } else if (a == A_WITN) {
          systemWitnessReset(v);
          nacts = 0;
        } else if (a == A_WITN + 1) {
          nacts = systemEvict(v, out);
        } else
          nacts = systemServe(v, cursor, out);

        for (i = 0; i < nacts && i < SYSTEM_MAX_ACTS; ++i) {
          aAct[i] = out[i].act;
          aRnd[i] = rv(out[i].round);
          if ((aWantOk[i] = out[i].want ? 1 : 0))
            memcpy(aWant[i], out[i].want, BS);
        }

#ifdef HRTWIN
        /* the same action on twin B, with the grain driven hard:
         * all-n at every close, late assembly after it */
        outB[SYSTEM_MAX_ACTS].act = 0xA5;
        outB[SYSTEM_MAX_ACTS + 1].act = 0x5A;
        bSnap = 0;
        if (a < A_RECV) {
          nactsB = systemReceived(vB, rn(probeRound), probeFrom, probeRecord,
                                  probeCursor, outB);
        } else if (a < A_LAUN) {
          b = a - A_RECV;
          nactsB = systemLaunch(vB, (unsigned char)(b & 1),
                                (unsigned char)((b >> 1) & 1),
                                (unsigned char)((b >> 2) & 1),
                                (unsigned char)((b >> 3) & 1), outB);
        } else if (a < A_COMP) {
          unsigned long closed;

          closed = rv(systemFrontier(vB));
          nactsB = systemComplete(vB, systemFrontier(vB), rn(closed + 1),
                                  have, outB);
          /* consume the act borrows BEFORE the late-assembly call --
           * a round name lives in machine storage only until the
           * next call into the library (system.h) */
          for (i = 0; i < nactsB && i < SYSTEM_MAX_ACTS; ++i) {
            bRnd[i] = rv(outB[i].round);
            if ((bWantOk[i] = outB[i].want ? 1 : 0))
              memcpy(bWant[i], outB[i].want, BS);
          }
          bSnap = 1;
          systemAssembled(vB, rn(closed), (unsigned char)(head % NP));
        } else if (a < A_POSS) {
          nactsB = systemPossessed(vB, rn(probeRound), probeFrom, outB);
        } else if (a < A_WITN) {
          b = a - A_POSS;
          nactsB = systemWitness(vB, rn(roundList[b / NP]),
                                 (unsigned char)(b % NP), outB);
        } else if (a == A_WITN) {
          systemWitnessReset(vB);
          nactsB = 0;
        } else if (a == A_WITN + 1) {
          nactsB = systemEvict(vB, outB);
        } else
          nactsB = systemServe(vB, cursorB, outB);
        if (!bSnap)
          for (i = 0; i < nactsB && i < SYSTEM_MAX_ACTS; ++i) {
            bRnd[i] = rv(outB[i].round);
            if ((bWantOk[i] = outB[i].want ? 1 : 0))
              memcpy(bWant[i], outB[i].want, BS);
          }
#endif

        ++expanded;
        bad = 0;

        if (nacts > SYSTEM_MAX_ACTS
         || out[SYSTEM_MAX_ACTS].act != 0xA5
         || out[SYSTEM_MAX_ACTS + 1].act != 0x5A) {
          fprintf(stderr, "FAIL: MAX_ACTS exceeded (%u) at run %u action %u\n",
                  nacts, run, a);
          ++failures;
          continue;
        }

        /* L4 machine half: a launch opportunity yields at most one act. */
        if (a >= A_RECV && a < A_LAUN && nacts > 1) {
          fprintf(stderr, "FAIL: launch produced %u acts (run %u action %u)\n",
                  nacts, run, a);
          ++failures;
          continue;
        }

        /* I8 -- on the possession paths, release fires exactly when the
         * post-record reaches all n: no earlier (a correct process may
         * still need the round -- L5) and no later (I4's dual). */
        if (probePath) {
          for (cnt = 0, i = 0; i < nacts && i < SYSTEM_MAX_ACTS; ++i)
            if (aAct[i] == SYSTEM_ACT_RELEASE) {
              ++cnt;
              if (aRnd[i] != probeRound)
                BAD(8);
            }
          if (cnt != probeRelease)
            BAD(8);
        }

        /* I1 / I7 -- reach bound, and the frontier is never retained. */
        memset(postRet, 0, sizeof (postRet));
        for (cnt = 0, r = 0; r < 320; ++r) {
          pos = startF - 1 + r;
          if (systemRetained(v, rn(pos))) {
            BIT_SET(postRet, r);
            ++cnt;
          }
        }
        if (cnt > NR)
          BAD(1);
        if (systemRetained(v, systemFrontier(v)))
          BAD(7);

        /*
         * I9 -- every round that left retention is named by a RELEASE
         * act, and every RELEASE names a round that was retained.
         * Stated as those two containments rather than set equality:
         * an eviction whose freed slot is immediately reused for the
         * new frontier round legitimately leaves the name retained.
         *
         * I10 -- a round ENTERS retention only at completion, and the
         * round it enters as is the pre-advance frontier.  Without
         * this a machine retaining some other name satisfies
         * every other conjunct while serving and releasing the wrong
         * round.
         */
        memset(relRet, 0, sizeof (relRet));
        for (i = 0; i < nacts && i < SYSTEM_MAX_ACTS; ++i)
          if (aAct[i] == SYSTEM_ACT_RELEASE) {
            pos = aRnd[i] + 1 - startF;
            if (pos >= 320)
              BAD(9);
            else
              BIT_SET(relRet, pos);
          }
        for (r = 0; r < 320; ++r) {
          unsigned int wasIn = (preRet[r >> 3] >> (r & 7)) & 1;
          unsigned int isIn = (postRet[r >> 3] >> (r & 7)) & 1;
          unsigned int said = (relRet[r >> 3] >> (r & 7)) & 1;

          if ((wasIn && !isIn && !said) || (said && !wasIn))
            BAD(9);
          if (!wasIn && isIn
           && (a < A_LAUN || a >= A_COMP || startF - 1 + r != prevF))
            BAD(10);
        }

        /*
         * I11 -- the eviction paths (no room at completion, and
         * systemEvict) release the OLDEST retained
         * round.  Nothing else constrains which round eviction takes;
         * without this a machine evicting the newest satisfies I8-I10
         * while discarding the round most likely to still be wanted.
         * Vacuous at a reach of one, where the choice is forced.
         */
        if ((a >= A_LAUN && a < A_COMP) || a == A_WITN + 1)
          for (i = 0; i < nacts && i < SYSTEM_MAX_ACTS; ++i)
            if (aAct[i] == SYSTEM_ACT_RELEASE
             && (!haveOldest || aRnd[i] != oldestRound))
              BAD(11);

        /* I10c -- at birth, possession is exactly self and nothing owed. */
        if (a >= A_LAUN && a < A_COMP && systemRetained(v, rn(prevF))
         && (p = systemPossess(v, rn(prevF)))
         && (q = systemWant(v, rn(prevF)))) {
          for (cnt = 0, b = 0; b < BS; ++b) {
            if (q[b])
              BAD(10);
            for (i = 0; i < 8; ++i)
              if (p[b] & (1 << i))
                ++cnt;
          }
          if (cnt != 1 || !SYSTEM_TST(p, ESELF))
            BAD(10);
        }

        for (r = 0; !bad && r < 320; ++r) {
          pos = startF - 1 + r;
          if (!systemRetained(v, rn(pos)))
            continue;
          if (!(p = systemPossess(v, rn(pos)))
           || !(q = systemWant(v, rn(pos)))) {
            BAD(2);
            break;
          }
          for (cnt = 0, b = 0; b < BS; ++b) {
            if (p[b] & q[b]) {                                     /* I2 */
              BAD(2);
              break;
            }
            for (i = 0; i < 8; ++i)
              if (p[b] & (1 << i))
                ++cnt;
          }
          if (bad)
            break;
          if (!SYSTEM_TST(p, ESELF))                               /* I3 */
            BAD(3);
          else if (cnt >= NP)                                      /* I4 */
            BAD(4);
        }

        if (!bad) {
          if (!(p = systemWitnesses(v)))
            BAD(5);
          else {
            for (cnt = 0, b = 0; b < BS; ++b)
              for (i = 0; i < 8; ++i)
                if (p[b] & (1 << i))
                  ++cnt;
            if ((v->flags & SYSTEM_F_ADOPT)
             && (cnt < ET + 1 || SYSTEM_TST(p, ESELF)))
              BAD(5);                                              /* I5 */
            else if (rv(systemFrontier(v)) != prevF
                  && (cnt || (v->flags & SYSTEM_F_ADOPT)))
              BAD(6);                                              /* I6 */
          }
        }

#ifdef HRTWIN
        /*
         * L7's second half.  The two twins differ ONLY in the
         * held-members grain, so any divergence in a decision the
         * machine takes is H_r having entered that decision.  The
         * grain's own surface -- systemHave and a SERVE act's .have
         * -- is excluded: that is where it is allowed to differ.
         */
        if (!bad) {
          if (nactsB > SYSTEM_MAX_ACTS
           || outB[SYSTEM_MAX_ACTS].act != 0xA5
           || outB[SYSTEM_MAX_ACTS + 1].act != 0x5A)
            hrWhy = "twin B overran out[]";
          else if (rv(systemFrontier(vB)) != rv(systemFrontier(v)))
            hrWhy = "frontier";
          else if (systemLive(vB) != systemLive(v))
            hrWhy = "live";
          else if (systemOwed(vB) != systemOwed(v))
            hrWhy = "owed";
          else if (systemDuty(vB) != systemDuty(v))
            hrWhy = "duty class";
          else if (memcmp(cursorB, cursor, systemCursorSz(RS)))
            hrWhy = "serve cursor";
          else if (!(p = systemWitnesses(v)) || !(q = systemWitnesses(vB))
                || memcmp(p, q, BS))
            hrWhy = "witness book";
          else if (nactsB != nacts)
            hrWhy = "act count";
          for (i = 0; !hrWhy && i < nacts && i < SYSTEM_MAX_ACTS; ++i) {
            if (outB[i].act != aAct[i] || bRnd[i] != aRnd[i])
              hrWhy = "act kind or round";
            else if (aAct[i] == SYSTEM_ACT_SERVE
                  && (!aWantOk[i] || !bWantOk[i]
                   || memcmp(aWant[i], bWant[i], BS)))
              hrWhy = "SERVE want bitmap";
          }
          for (r = 0; !hrWhy && r < 320; ++r) {
            pos = startF - 1 + r;
            if (systemRetained(v, rn(pos)) != systemRetained(vB, rn(pos))) {
              hrWhy = "retained set";
              break;
            }
            if (!systemRetained(v, rn(pos)))
              continue;
            if (memcmp(systemPossess(v, rn(pos)),
                       systemPossess(vB, rn(pos)), BS))
              hrWhy = "possession record";
            else if (memcmp(systemWant(v, rn(pos)),
                            systemWant(vB, rn(pos)), BS))
              hrWhy = "want record";
            /* non-vacuity: the whole oracle rests on the two grains
             * actually differing, so count where they do */
            else if (memcmp(systemHave(v, rn(pos)),
                            systemHave(vB, rn(pos)), BS))
              ++hrGrain;
          }
          if (hrWhy)
            BAD(12);
        }
#endif

        if (bad) {
          int chain[64];
          int c;
          int k;

          fprintf(stderr, "FAIL: %s violated (run %u)\n", inv[bad], run);
          fprintf(stderr, "  action %u from state %u (frontier %lu -> %lu)\n",
                  a, head, prevF, rv(systemFrontier(v)));
#ifdef HRTWIN
          if (hrWhy)
            fprintf(stderr, "  twin divergence: %s\n", hrWhy);
#endif
          for (c = 0, k = head; k > 0 && c < 64; k = parent[k])
            chain[c++] = k;
          fprintf(stderr, "  path (start frontier %lu):", startF);
          for (k = c - 1; k >= 0; --k)
            fprintf(stderr, " %u", acts[chain[k]]);
          fprintf(stderr, " %u\n", a);
          if (++failures >= 8) {
            fprintf(stderr, "  (8 violations -- stopping this run)\n");
            head = nStates;
            break;
          }
          continue;
        }

        /* Insert if new.  The ctx is normalized out first: it points
         * into 'work', so a stored key carrying it would say where
         * the expansion buffer lives, and every state would look
         * new.  It is restored below for the descriptor, which reads
         * the retained rounds through the operations. */
        {
          unsigned long h;
          unsigned int sl;

          v->ctx = 0;
#ifdef HRTWIN
          vB->ctx = 0;
#endif
          for (h = 2166136261UL, i = 0; i < keyLen; ++i) {
            h ^= work[i];
            h *= 16777619UL;
          }
          for (sl = h & (TBLSZ - 1); table[sl] >= 0; sl = (sl + 1) & (TBLSZ - 1))
            if (!memcmp(states + (size_t)table[sl] * slotLen, work, keyLen))
              break;
          if (table[sl] >= 0)
            continue;
          if (nStates >= MAXSTATES) {
            fprintf(stderr, "state cap %u reached -- enumeration INCOMPLETE\n",
                    MAXSTATES);
            ++failures;
            head = nStates;
            break;
          }
          memcpy(states + (size_t)nStates * slotLen, work, slotLen);
          parent[nStates] = head;
          acts[nStates] = a;
          table[sl] = nStates;
          ++nStates;
          v->ctx = work + stOff;

          /*
           * Canonical descriptor, so the two runs can be compared as
           * SETS rather than by eyeballing two printed counts: the
           * frontier relative to the run's start, the flags, the
           * witness record, then each retained round keyed by its
           * distance behind the frontier (translation-invariant, and
           * blind to freed-slot and scratch residue, which no query
           * exposes and no rule reads).  Summing per-state hashes is
           * order-independent.
           */
          memset(desc, 0, sizeof (desc));
          desc[0] = (unsigned char)(rv(systemFrontier(v)) - startF);
          desc[1] = v->flags;
          if ((p = systemWitnesses(v)))
            memcpy(desc + 2, p, BS);
          for (cnt = 2 + BS, d = 1; d < 258; ++d) {
            rnd = rv(systemFrontier(v)) - d;
            if (!systemRetained(v, rn(rnd)))
              continue;
            desc[cnt++] = (unsigned char)d;
            memcpy(desc + cnt, systemPossess(v, rn(rnd)), BS);
            memcpy(desc + cnt + BS, systemWant(v, rn(rnd)), BS);
            memcpy(desc + cnt + 2 * BS, systemHave(v, rn(rnd)), BS);
            cnt += 3 * BS;
          }
          for (h = 2166136261UL, i = 0; i < DESCLEN; ++i) {
            h ^= desc[i];
            h *= 16777619UL;
          }
          runSum[run] += h;
        }
      }
    }

    runStates[run] = nStates;
    printf("run %u (start frontier %lu, horizon %u): %u states, %lu transitions\n",
           run, startF, HORIZON, nStates, expanded);
#ifdef HRTWIN
    printf("  twin drive: %lu retained rounds carried a differing grain\n",
           hrGrain);
    if (!hrGrain) {
      fprintf(stderr, "FAIL: run %u -- the twins' grains never differed,"
                      " so the comparison asserts nothing\n", run);
      ++failures;
    }
#endif
#ifdef EXPECTSTATES
    /* the twin drive must not perturb twin A's reachable set: pass
     * the default build's count to assert it rather than eyeball it */
    if (nStates != EXPECTSTATES) {
      fprintf(stderr, "FAIL: run %u reached %u states, expected %u\n",
              run, nStates, (unsigned int)EXPECTSTATES);
      ++failures;
    }
#endif
  }

  /*
   * The translation-invariance claim, asserted rather than observed:
   * the fresh run and the deep-start run must reach the same
   * canonical state set.  Equal counts alone would not say this.
   */
  if (runStates[0] != runStates[1] || runSum[0] != runSum[1]) {
    fprintf(stderr,
            "FAIL: runs differ -- %u/%lu vs %u/%lu (canonical state sets)\n",
            runStates[0], runSum[0], runStates[1], runSum[1]);
    ++failures;
  }

  printf("%s\n", failures ? "INVARIANT FALSIFIED"
                          : "invariant held on all reachable states");
  free(root);
  free(work);
  free(table);
  free(acts);
  free(parent);
  free(states);
  return (failures ? 1 : 0);
}
