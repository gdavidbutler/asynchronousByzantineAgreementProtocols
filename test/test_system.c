/*
 * test_system.c
 *
 * Contract test for the public C API in system.h.
 *
 * Tests are derived ONLY from the documented contract in system.h,
 * system.md (the governing specification -- this layer has no paper),
 * and system.dtc (the obligation decision tables).  No part of this
 * file inspects system.c or reaches past the public surface except
 * through the documented queries.
 *
 * ROUND NAMES: the machine takes rounds as opaque rs-byte names
 * under one caller comparator (system.h, the operations on a
 * round).  This suite drives the ORDINAL INSTANTIATION -- rs =
 * sizeof (unsigned long), a name is the ordinal's bytes, ordCmp is
 * the header's reference displacement comparator, and every close
 * mints ordinal succession (closeFrontier).  rn()/rv() convert at
 * the boundary so the checks read in ordinals; the machine sees
 * only names.
 *
 * THE STORE: the retained rounds and their records are the CALLER's,
 * reached through the four retention operations (system.h, THE
 * RETENTION OPERATIONS).  This suite supplies them from the
 * reference store systemStore.[hc] through the SEAT macro below,
 * which re-initializes the store beside every systemInit because an
 * EMPTY store is that call's precondition -- a seat reset that left
 * rounds behind would exercise a different machine's state than the
 * invariants describe.  The store's 'cap' is the declared recovery
 * reach: the round count the retired 'w' encoding named (w = 3 held
 * FOUR rounds), spelled here as the count itself.
 *
 * Configuration: 7 processes (n = 6 encoded), t = 2, self = 0 --
 * so the R4 advance signal needs n-t = 5 composition possessions.
 * The helper grease() feeds possession from processes 1..4 until
 * the duty class leaves HELD; processes 5 and 6 stay free for the
 * want/serve scenarios.
 *
 * Sections:
 *
 *   A. API edges -- Sz monotone (the seat, the per-round records and
 *      the serve cursor each in their own arguments), Init defense
 *      (null, self > n, n+1 < 3t+1, n = 0, rs = 0, null genesis/cmp
 *      and a MISSING RETENTION OPERATION each leave an inert
 *      instance), entry-point null/range defense, fresh-instance
 *      queries.
 *   B. PARTICIPATE -- existence evidence for the frontier round
 *      records owed (DERIVED, system.md R2a); evidence beyond
 *      reach is inert; owed discharges as JOIN; own COMPLETE
 *      retires it.
 *   C. ADMIT and the R4 advance rule -- the capacity gate; the duty
 *      classes (HELD holds regardless of tolerance; TOLERANCE
 *      advances only with tolerance elapsed; all-n release reads
 *      MET and advances without it); joins gated exactly as
 *      admissions; owed work outranks chosen work.
 *   D. SERVE / possession -- want births with the sender in .want;
 *      a possessing sender births nothing; riding indications
 *      record and retire; forged possession marks only its sender;
 *      the walk round-robins two owed rounds.
 *   E. RETAIN / RELEASE -- all-n possession releases via the
 *      received path (and below all-n does NOT); no-room
 *      eviction releases the oldest; systemEvict oldest-first.
 *   F. Deep retention -- names never recur: no structural release
 *      exists, eviction is the only one, an entry lingers to any
 *      depth and still serves, and names 256 apart never collide
 *      (exhausted-reach, short-reach, and lingerer regimes) -- plus
 *      the ordinal instantiation crossing its width's wrap as a
 *      non-event (the displacement comparator owns the wrap; the
 *      machine never sees it).
 *   G. Large-n bitmaps -- the bs > 1 regime at 201 processes
 *      (t = 66): bits past byte 0, duty threshold at n-t = 135,
 *      all-n release at exactly the 201st possession.
 *   H. O2 / O3 / O5 -- MAINTAIN outranks the value and sits under
 *      the same gates; the have grain (at completion, via
 *      systemAssembled, and riding SERVE acts) and the walk's
 *      0-return at quiescence; the witness book (t+1 distinct
 *      servers = t forgers cannot adopt, self/range/wrong-round
 *      rejected, idempotent, single-fire ADOPT, reset re-arms,
 *      completion clears -- including supersession of a
 *      below-threshold book -- the one consume region).
 *   I. Premature possession indications -- an indication for a round
 *      with no record yet is dropped, and the record a held-then-
 *      re-presented one reaches is byte-identical to the record a
 *      timely one reaches.
 *   J. L3 COMMUTATION (added 2026-07-25) -- the direct oracle for
 *      system.md L3: from a common pre-state, witnesses-then-close
 *      and close-then-late-witnesses reach the IDENTICAL machine
 *      state.  Sections B/C/H check pieces of the claim by hand
 *      (the book clears, the latch re-arms, a stale round is
 *      refused); this section replays both orders from a
 *      byte-identical snapshot of the WHOLE caller allocation AND
 *      ITS STORE, and compares both with memcmp, so no conjunct of
 *      "identical" is chosen by the test.  The store side is not
 *      decoration: machine state is no longer the whole state, so
 *      the two orders could reach identical machine bytes and
 *      differ in the rounds retained.  Both orders mint the
 *      SAME successor name, so the close is one event replayed.
 *      The one legitimate output difference -- order A's ADOPT
 *      acts, output before the close -- is encoded per scenario
 *      rather than ignored, and the close's own acts must match
 *      kind-for-kind and round-for-round.  Carries the
 *      once-per-round corollary in both of the proof's arms
 *      (before a later launch the second close finds no live
 *      instance; after one it names a round that is no longer the
 *      frontier).
 *   K. The cursor birth (added with the want-birth ruling) -- a
 *      high-water act of C births want, for its sender, of every
 *      retained round after C not evidenced by that sender: direct
 *      birth intact beside it, nothing below the cursor, stale
 *      (non-high-water) acts birth nothing above, the possession
 *      gate pre-empts and possession recording retires (I2), self
 *      gated by I3, the frontier inert, a RELEASED cursor round
 *      still locates, and the serve walk discharges.
 *   L. The serve cursor is a round NAME (added with the retention
 *      seam) -- a zeroed cursor starts at the oldest and walks the
 *      order, the turn WRAPS at the end, a release beneath the cursor
 *      does not displace the walk (a count would skip), a cursor
 *      naming a RELEASED round still positions it, and a round closed
 *      mid-turn joins the rotation instead of resetting it.
 *   M. An act's borrows outlive the call that made them (added with
 *      the store-mutant tier) -- the STABILITY term read from the
 *      machine's side: a SERVE act hands back the store's own name in
 *      .round and its own record in .want / .have, and both must
 *      still be that round's after a later call that changes no
 *      retained round.  TWO acts are held at once, because one
 *      per-call scratch buffer merges them and no single act can
 *      carry that: the arms are what must stay DIFFERENT.  A
 *      scratch-answering store passes every other arm in this file
 *      and in test_systemStore.c -- it holds the right bytes at the
 *      instant of return -- which is why this section exists at all.
 *
 * MATCHED REDS (2026-07-25, the machine-mutant tier -- see
 * test/machineMutants.sh, which rebuilds this file against nine
 * mutated scratch copies of system.c).  Until that tier ran, no
 * section here had been shown to CATCH a broken machine.  Which
 * sections each mutant fires: adopt latch at t -> H x4 and J's
 * ADOPT-count arm x2; self counted as a server -> H x2; the witness
 * book surviving completion -> H x4 and J's state-identity arm in ALL
 * TEN commutation scenarios; all-n read one possession early -> C and
 * D; a completion eviction that reuses its slot with no RELEASE act
 * -> E, F and J's close-acts arms; the born entry taking the wrong
 * round name -> B, C and D; eviction taking the newest -> E x3 and
 * J's exhausted-reach non-vacuity arm; the SERVE walk gated on the
 * held-members grain -> D x3 and G.
 *
 * MATCHED REDS FROM THE STORE SIDE (2026-08-14, test/storeMutants.sh,
 * which rebuilds this file against thirteen mutated retention stores
 * and two mutated drivers).  The machine is CLEAN under all of them,
 * so what fires here is a section catching a broken CALLER: section M
 * x3 for each C17 scratch sibling (its designated oracle, and the
 * reason M exists); M x3 again beside L x7 K x5 E x3 F x6 J x1 for a
 * store enumerating newest-first; K x5 D x2 H x1 for a store writing
 * a record bit behind the machine; E x9 F x11 G K L for a release
 * that does not drop.  Two arms crash this suite the way two machine
 * mutants do -- a pre-populated store makes systemPossess answer for
 * a round the machine never bore -- and it is left unhardened for the
 * same reason: the falsifier is their designated oracle.
 *
 * Section F was the sole matched red for the frontier+1 wrap
 * lookahead while the machine's round name was a wrapping byte
 * (MM_WRAP_LOOKAHEAD_SKIP, retired with the guard when the name
 * widened).  F now pins the abstraction's contract from the other
 * side: distinctness at any depth rests on the birth discipline
 * and the caller's name discipline alone (I1), reach-behind is
 * retention, and the ordinal instantiation's wrap belongs to its
 * comparator, not to the machine.
 *
 * Two of the nine crash this suite before its count line: a round
 * released early or born under the wrong name makes systemPossess
 * return 0, and the SYSTEM_TST of a null bitmap faults.  The checks
 * printed before the fault are the red, and the falsifier is the
 * designated oracle for both, so the suite was left unhardened
 * against a machine that breaks the queries' own preconditions.
 *
 * Header encoding convention (CRITICAL):
 *   n parameter is encoded; actual process count = n + 1
 *
 * Style: C89, K&R, 2-space indent, single monolithic main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"
#include "systemStore.h"

static int Failures = 0;
static int Checks   = 0;
static const char *CurTest = "<none>";

#define CHECK(cond, msg) do {                                  \
    ++Checks;                                                  \
    if (!(cond)) {                                             \
      ++Failures;                                              \
      fprintf(stderr, "FAIL [%s]: %s  (%s:%d)\n",              \
              CurTest, (msg), __FILE__, __LINE__);             \
    }                                                          \
  } while (0)

#define BANNER(name) do { CurTest = (name); } while (0)

/* Set process p's bit in a caller-built bitmap (the SYSTEM_TST bit
 * convention; the header exposes only the test macro -- building a
 * have bitmap is the caller's own encoding). */
#define BIT_SET(map, p) ((map)[(p) >> 3] |= (unsigned char)(1 << ((p) & 7)))

/* n = 6 encoded -> 7 processes, t = 2 (n-t = 5), self = 0 */
#define TN 6
#define TT 2
#define TSELF 0

/* The ordinal instantiation's name size. */
#define RS ((unsigned int)sizeof (unsigned long))

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
 * enclosing call expression (8 live names suffice everywhere here). */
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

/* The seat's retention store (systemStore.[hc]); one allocation big
 * enough for every configuration below, re-initialized at each seat
 * reset.  Sections I and J hold their own beside it. */
static struct systemStore *Store;

/*
 * Seat a system on a store.  Two calls that must not drift apart:
 * an EMPTY store is systemInit's precondition (system.h, the
 * retention requirements), and systemInit is a seat reset wherever
 * it appears here.  systemInit hands ONE ctx to the comparator and
 * to the four operations, so the store IS the ctx and
 * systemStoreCmp forwards to the comparator the store was given
 * (systemStore.h, the closure carries the comparator).  'cap' is
 * the declared recovery reach.
 */
#define SEAT(sy, st, en, et, esf, gen, cap)                            \
  (systemStoreInit((st), (en), RS, (cap), ordCmp, 0),                  \
   systemInit((sy), (en), (et), (esf), RS, (gen), systemStoreCmp,      \
    systemStoreRecords, systemStoreRetain, systemStoreRelease,         \
    systemStoreAfter, (st)))

/*
 * The MACHINE-OWNED span of a seat's allocation, for section J's
 * whole-state comparison.  The head of struct system is the
 * caller's own closure -- the comparator, the four retention
 * operations, and the ctx -- written once by systemInit and never
 * again; two seats holding two stores differ in ctx by
 * construction, and not by anything the machine did.  Everything
 * from 'rs' on is the machine's.
 */
#define MSTATE(sy) ((const unsigned char *)&(sy)->rs)
#define MSTATEOFF(sy) \
  ((unsigned long)(MSTATE(sy) - (const unsigned char *)(sy)))

/* Feed possession of the prior round from low process indices until
 * the duty class leaves HELD (reaches n-t).  Inert at genesis or
 * when the prior round is already released. */
static void
grease(
  struct system *s
 ,struct systemAct *out
){
  unsigned long prior;
  unsigned char p;

  prior = rv(systemFrontier(s)) - 1;
  if (!systemRetained(s, rn(prior)))
    return;
  for (p = 1; systemDuty(s) == SYSTEM_DUTY_HELD && p != 0; ++p)
    systemPossessed(s, rn(prior), p, out);
}

/* Close the live frontier round, minting ordinal succession (the
 * next frontier's name is the closed ordinal + 1). */
static unsigned int
closeFrontier(
  struct system *s
 ,const unsigned char *have
 ,struct systemAct *out
){
  return (systemComplete(s, systemFrontier(s),
   rn(rv(systemFrontier(s)) + 1), have, out));
}

/* Drive one chosen round: grease the prior to n-t, ADMIT with
 * tolerance elapsed, COMPLETE.  Returns the COMPLETE's acts. */
static unsigned int
roundTrip(
  struct system *s
 ,struct systemAct *out
){
  struct systemAct lo[SYSTEM_MAX_ACTS];

  grease(s, lo);
  if (systemLaunch(s, 1, 0, 1, 1, lo) != 1 || lo[0].act != SYSTEM_ACT_ADMIT)
    return ((unsigned int)-1);
  return (closeFrontier(s, 0, out));
}

int
main(
  void
){
  struct system *s;
  struct systemAct out[SYSTEM_MAX_ACTS];
  struct systemAct outM[SYSTEM_MAX_ACTS];    /* section M's held act */
  const unsigned char *mRndP;
  const unsigned char *mWntP;
  unsigned long ssz;
  unsigned long sz;
  unsigned long r;
  unsigned long mRnd;
  unsigned int n;
  unsigned int k;
  unsigned int rel;
  unsigned char cursor[sizeof (unsigned long) + 1];  /* systemCursorSz(RS) */
  unsigned char mWnt[(TN + 8) / 8];
  unsigned char p;

  /* ---------------------------------------------------------------- */
  BANNER("A: sizes and init defense");
  /* ---------------------------------------------------------------- */

  CHECK(systemSz(TN, RS) > sizeof (struct system), "Sz exceeds header");
  CHECK(systemSz(255, RS) > systemSz(TN, RS), "Sz monotone in n");
  CHECK(systemSz(TN, RS + 8) > systemSz(TN, RS), "Sz monotone in rs");

  /* the two sizes the caller allocates AGAINST the seat: one round's
   * records, and the serve cursor.  Records grow with the roster
   * (three bitmaps), the cursor with the name (a name plus its
   * in-use byte), and neither reads the other's argument */
  CHECK(systemRecordsSz(255) > systemRecordsSz(TN),
   "records size monotone in n");
  CHECK(systemRecordsSz(TN) == 3 * ((TN + 8) / 8),
   "records size is three bitmaps");
  CHECK(systemCursorSz(RS + 8) > systemCursorSz(RS),
   "cursor size monotone in rs");
  CHECK(systemCursorSz(RS) == sizeof (cursor),
   "the cursor storage below matches the header's sizing");

  systemInit(0, TN, TT, TSELF, RS, rn(0), ordCmp, systemStoreRecords,
   systemStoreRetain, systemStoreRelease, systemStoreAfter, 0);
                                            /* must not crash */

  /* ONE seat allocation and ONE store allocation carry every
   * configuration below.  The seat's size no longer answers to the
   * reach -- what the reach sizes is the STORE -- so the sections
   * differ only in the roster (TN, and G's 201) and in the reach
   * they hand systemStoreInit.  The store covers the widest of each:
   * the deep-retention reach of 256 at TN, and G's roster at 4. */
  ssz = systemStoreSz(TN, RS, 256);
  if (systemStoreSz(200, RS, 4) > ssz)
    ssz = systemStoreSz(200, RS, 4);
  sz = systemSz(TN, RS);
  if (systemSz(200, RS) > sz)
    sz = systemSz(200, RS);
  if (!(Store = malloc(ssz)) || !(s = malloc(sz))) {
    fprintf(stderr, "malloc failed\n");
    return (1);
  }

  /* self > n is rejected: the instance is inert on every entry */
  SEAT(s, Store, TN, TT, TN + 1, rn(0), 4);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected self: no ADMIT");
  CHECK(systemReceived(s, rn(0), 1, 0, 0, out) == 0, "rejected self: inert");
  CHECK(systemComplete(s, systemFrontier(s), rn(1), 0, out) == 0,
   "rejected self: no complete");
  memset(cursor, 0, sizeof (cursor));
  CHECK(systemServe(s, cursor, out) == 0, "rejected self: no serve");
  CHECK(systemPossessed(s, rn(0), 1, out) == 0, "rejected self: no possession");
  CHECK(systemWitness(s, rn(0), 1, out) == 0, "rejected self: no witness");
  CHECK(systemEvict(s, out) == 0, "rejected self: no evict");
  systemWitnessReset(s); /* must not crash on a rejected instance */
  systemAssembled(s, rn(0), 1); /* must not crash on a rejected instance */

  /* n + 1 < 3t + 1 is rejected (7 < 10) */
  SEAT(s, Store, TN, 3, TSELF, rn(0), 4);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected t: inert");

  /* the single-process encoding n = 0 is rejected */
  SEAT(s, Store, 0, 0, 0, rn(0), 4);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected single: inert");

  /* a nameless round space is rejected: rs = 0, null genesis, null
   * comparator each leave an inert instance */
  systemInit(s, TN, TT, TSELF, 0, rn(0), systemStoreCmp, systemStoreRecords,
   systemStoreRetain, systemStoreRelease, systemStoreAfter, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected rs 0: inert");
  systemInit(s, TN, TT, TSELF, RS, 0, systemStoreCmp, systemStoreRecords,
   systemStoreRetain, systemStoreRelease, systemStoreAfter, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected null genesis: inert");
  systemInit(s, TN, TT, TSELF, RS, rn(0), 0, systemStoreRecords,
   systemStoreRetain, systemStoreRelease, systemStoreAfter, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected null cmp: inert");

  /* a MISSING RETENTION OPERATION is rejected the same way, one arm
   * per operation: the machine reaches the caller's rounds through
   * all four and holds no fallback for any of them, so a seat
   * missing one is not a degraded seat but no seat at all */
  systemInit(s, TN, TT, TSELF, RS, rn(0), systemStoreCmp, 0,
   systemStoreRetain, systemStoreRelease, systemStoreAfter, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected null records: inert");
  systemInit(s, TN, TT, TSELF, RS, rn(0), systemStoreCmp, systemStoreRecords,
   0, systemStoreRelease, systemStoreAfter, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected null retain: inert");
  systemInit(s, TN, TT, TSELF, RS, rn(0), systemStoreCmp, systemStoreRecords,
   systemStoreRetain, 0, systemStoreAfter, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected null release: inert");
  systemInit(s, TN, TT, TSELF, RS, rn(0), systemStoreCmp, systemStoreRecords,
   systemStoreRetain, systemStoreRelease, 0, Store);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected null after: inert");

  SEAT(s, Store, TN, TT, TSELF, rn(0), 4);
  CHECK(systemFrontier(s) && rv(systemFrontier(s)) == 0,
   "fresh frontier is the genesis name");
  CHECK(!systemLive(s), "fresh not live");
  CHECK(!systemOwed(s), "fresh not owed");
  CHECK(systemDuty(s) == SYSTEM_DUTY_MET, "genesis duty met");
  CHECK(!systemRetained(s, rn(0)), "fresh retains nothing");
  CHECK(systemPossess(s, rn(0)) == 0, "no bitmap for unretained");
  CHECK(systemWant(s, rn(0)) == 0, "no want for unretained");
  CHECK(systemFrontier(0) == 0 && !systemLive(0) && !systemOwed(0),
   "null-state queries");
  CHECK(systemDuty(0) == SYSTEM_DUTY_MET, "null-state duty met");
  CHECK(systemReceived(0, rn(0), 0, 0, 0, out) == 0, "null state received");
  CHECK(systemReceived(s, 0, 1, 0, 0, out) == 0, "null round name inert");
  CHECK(systemReceived(s, rn(0), TN + 1, 0, 0, out) == 0, "from out of range");
  CHECK(systemPossessed(s, rn(0), TN + 1, out) == 0, "possess out of range");
  CHECK(systemEvict(s, out) == 0, "evict with nothing retained");

  /* ---------------------------------------------------------------- */
  BANNER("B: PARTICIPATE (R2a rule)");
  /* ---------------------------------------------------------------- */

  /* existence evidence for the frontier round, no live instance:
   * no act, but participation owed is recorded */
  n = systemReceived(s, rn(0), 2, 0, 0, out);
  CHECK(n == 0, "frontier evidence: no act");
  CHECK(systemOwed(s), "frontier evidence records owed");

  /* evidence beyond chain reach is inert and does NOT record owed */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 4);
  CHECK(systemReceived(s, rn(5), 2, 0, 0, out) == 0, "beyond reach: no act");
  CHECK(!systemOwed(s), "beyond reach: no owed");

  /* owed discharges as JOIN at the next launch opportunity, even
   * with no value pending and backlog undrained (owed work is not
   * capacity-gated; genesis duty is met, so the advance rule is
   * satisfied without tolerance) */
  systemReceived(s, rn(0), 2, 0, 0, out);
  n = systemLaunch(s, 0, 0, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN && rv(out[0].round) == 0,
   "owed discharges as JOIN of the frontier round");
  CHECK(systemLive(s), "JOIN marks live");
  CHECK(!systemOwed(s), "JOIN clears owed");

  /* live instance: traffic for the frontier round DELIVERs */
  n = systemReceived(s, rn(0), 1, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_DELIVER && rv(out[0].round) == 0,
   "live round traffic delivers");

  /* own COMPLETE retires participation, retains, advances */
  n = closeFrontier(s, 0, out);
  CHECK(n == 0, "first complete releases nothing");
  CHECK(rv(systemFrontier(s)) == 1, "frontier advanced");
  CHECK(!systemLive(s), "complete clears live");
  CHECK(systemRetained(s, rn(0)), "round 0 retained");
  CHECK(systemPossess(s, rn(0))
   && SYSTEM_TST(systemPossess(s, rn(0)), TSELF),
   "possession starts at self");
  CHECK(closeFrontier(s, 0, out) == 0, "complete without live is inert");

  /* ---------------------------------------------------------------- */
  BANNER("C: ADMIT gate and the R4 advance rule");
  /* ---------------------------------------------------------------- */

  CHECK(systemLaunch(s, 0, 0, 1, 1, out) == 0, "no value: idle");
  CHECK(systemLaunch(s, 1, 0, 0, 1, out) == 0, "backlog undrained: gate holds");

  /* short of n-t: HELD, and tolerance cannot override */
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "prior at self only: held");
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0,
   "held: no advance regardless of tolerance");

  /* n-t reached: TOLERANCE -- advance only with tolerance elapsed */
  systemPossessed(s, rn(0), 1, out);
  systemPossessed(s, rn(0), 2, out);
  systemPossessed(s, rn(0), 3, out);
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "4 of 5: still held");
  systemPossessed(s, rn(0), 4, out);
  CHECK(systemDuty(s) == SYSTEM_DUTY_TOLERANCE, "n-t reached: tolerance");
  CHECK(systemLaunch(s, 1, 0, 1, 0, out) == 0,
   "tolerance not elapsed: hold");
  n = systemLaunch(s, 1, 0, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && rv(out[0].round) == 1,
   "tolerance elapsed: ADMIT");
  CHECK(systemLive(s), "ADMIT marks live");
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "live: no second launch");
  /* the close speaks its round: a superseded instance's stale
   * result cannot be booked as a later frontier's completion */
  CHECK(systemComplete(s, rn(0), rn(9), 0, out) == 0, "stale round refused");
  CHECK(systemLive(s), "refused close leaves the instance live");
  closeFrontier(s, 0, out);

  /* all-n possession releases the prior round; duty reads MET and
   * the frontier advances without tolerance */
  systemPossessed(s, rn(1), 1, out);
  systemPossessed(s, rn(1), 2, out);
  systemPossessed(s, rn(1), 3, out);
  systemPossessed(s, rn(1), 4, out);
  systemPossessed(s, rn(1), 5, out);
  n = systemPossessed(s, rn(1), 6, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 1,
   "all-n possession releases");
  CHECK(systemDuty(s) == SYSTEM_DUTY_MET, "released prior: duty met");
  n = systemLaunch(s, 1, 0, 1, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && rv(out[0].round) == 2,
   "all-n advance needs no tolerance");
  closeFrontier(s, 0, out);

  /* joins are gated exactly as admissions (an early launcher must
   * gain nothing), and owed work outranks chosen work */
  systemReceived(s, rn(3), 5, 0, 0, out);
  CHECK(systemOwed(s), "evidence for frontier 3 records owed");
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "prior round 2: held");
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "join held with the frontier");
  systemPossessed(s, rn(2), 1, out);
  systemPossessed(s, rn(2), 2, out);
  systemPossessed(s, rn(2), 3, out);
  systemPossessed(s, rn(2), 4, out);
  CHECK(systemDuty(s) == SYSTEM_DUTY_TOLERANCE, "prior at n-t");
  CHECK(systemLaunch(s, 1, 0, 1, 0, out) == 0, "join waits for tolerance");
  /* the full O5 precedence chain in one call: owed outranks
   * maintenance outranks the value */
  n = systemLaunch(s, 1, 1, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN && rv(out[0].round) == 3,
   "owed work before maintenance before chosen work");
  closeFrontier(s, 0, out);

  /* ---------------------------------------------------------------- */
  BANNER("D: SERVE and possession");
  /* ---------------------------------------------------------------- */

  /* want evidence on a retained round from a process not possessing
   * it births a serve carrying that process */
  n = systemReceived(s, rn(3), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && rv(out[0].round) == 3,
   "want evidence births serve");
  CHECK(out[0].want && SYSTEM_TST(out[0].want, 5), "sender in .want");
  CHECK(!SYSTEM_TST(out[0].want, 6), "only the sender in .want");

  /* re-arrival re-outputs the serve (retry posture) */
  n = systemReceived(s, rn(3), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE, "re-arrival re-serves");

  /* a second wanting process joins the owed set */
  systemReceived(s, rn(3), 6, 0, 0, out);
  CHECK(SYSTEM_TST(systemWant(s, rn(3)), 5)
   && SYSTEM_TST(systemWant(s, rn(3)), 6),
   "both wanting processes owed");

  /* a sender already recorded as possessing births nothing */
  CHECK(systemReceived(s, rn(2), 1, 0, 0, out) == 0,
   "possessing sender: no serve");

  /* an indication RIDING traffic records and births no serve */
  n = systemReceived(s, rn(2), 5, 1, 0, out);
  CHECK(n == 0, "riding possession births no serve");
  CHECK(SYSTEM_TST(systemPossess(s, rn(2)), 5), "riding indication recorded");

  /* possession recording is idempotent on a retained round */
  CHECK(systemPossessed(s, rn(2), 5, out) == 0, "repeat possession: no act");
  CHECK(systemRetained(s, rn(2)), "repeat possession: still retained");

  /* the walk round-robins two simultaneously-owed rounds */
  systemReceived(s, rn(2), 6, 0, 0, out);
  memset(cursor, 0, sizeof (cursor));
  n = systemServe(s, cursor, out);
  r = rv(out[0].round);
  CHECK(n == 1 && (r == 2 || r == 3), "walk serves an owed round");
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) != r
   && (rv(out[0].round) == 2 || rv(out[0].round) == 3),
   "walk advances to the other owed round");
  CHECK(systemServe(s, cursor, out) == 1, "walk cycles while owed");

  /* possession retires per-process; a forged indication from
   * another process strands nobody */
  systemPossessed(s, rn(3), 5, out);
  CHECK(!SYSTEM_TST(systemWant(s, rn(3)), 5),
   "possession retires per-process");
  CHECK(SYSTEM_TST(systemWant(s, rn(3)), 6), "other process still owed");
  systemPossessed(s, rn(3), 2, out);
  CHECK(SYSTEM_TST(systemWant(s, rn(3)), 6), "forged possession strands nobody");

  /* ---------------------------------------------------------------- */
  BANNER("E: RETAIN / RELEASE");
  /* ---------------------------------------------------------------- */

  /* round 2 sits at 6 of 7 possessions: below all-n must not
   * release (no count shortcut); the 7th arrives riding traffic,
   * so the release fires from the received path */
  CHECK(systemRetained(s, rn(2)), "below all-n does not release");
  n = systemReceived(s, rn(2), 6, 1, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 2,
   "all-n possession releases (received path)");
  CHECK(!systemRetained(s, rn(2)), "released round not retained");
  CHECK(systemPossessed(s, rn(2), 6, out) == 0,
   "possession on released: inert");

  /* the reach exhausted: 4 retained rounds is the whole reach.  Rounds
   * 0, 3 retained; complete rounds 4, 5 -> 4 retained; completing
   * round 6 evicts the oldest (round 0). */
  CHECK(roundTrip(s, out) == 0, "complete round 4");
  CHECK(roundTrip(s, out) == 0, "complete round 5");
  n = roundTrip(s, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 0,
   "an exhausted reach evicts the oldest");
  CHECK(systemRetained(s, rn(3)) && systemRetained(s, rn(4))
   && systemRetained(s, rn(5)) && systemRetained(s, rn(6)),
   "newer rounds survive eviction");

  /* budget eviction: oldest-first until nothing is retained */
  n = systemEvict(s, out);
  CHECK(n == 1 && rv(out[0].round) == 3, "evict oldest first");
  n = systemEvict(s, out);
  CHECK(n == 1 && rv(out[0].round) == 4, "evict next oldest");
  systemEvict(s, out);
  systemEvict(s, out);
  CHECK(systemEvict(s, out) == 0, "evict with nothing retained");
  memset(cursor, 0, sizeof (cursor));
  CHECK(systemServe(s, cursor, out) == 0,
   "walk 0-return with nothing retained");

  /* traffic for a released round is inert (out-of-band territory) */
  CHECK(systemReceived(s, rn(4), 1, 0, 0, out) == 0, "released round inert");

  /* ---------------------------------------------------------------- */
  BANNER("F: deep retention -- names never recur");
  /* ---------------------------------------------------------------- */

  SEAT(s, Store, TN, TT, TSELF, rn(0), 256);

  /* run the frontier well past the byte-name space; grease stops at
   * n-t, so nothing releases until the reach fills -- eviction
   * (oldest first) is then the ONLY structural release; no
   * name-driven release exists at any depth */
  for (k = 0; k < 300; ++k) {
    r = rv(systemFrontier(s));
    n = roundTrip(s, out);
    if (k < 256) {
      if (n != 0) {
        CHECK(0, "no release before the reach fills");
        break;
      }
    } else {
      if (n != 1 || out[0].act != SYSTEM_ACT_RELEASE
       || rv(out[0].round) != r - 256) {
        CHECK(0, "a full reach evicts the oldest, at any depth");
        break;
      }
    }
  }
  CHECK(k == 300, "300 rounds completed");
  CHECK(rv(systemFrontier(s)) == 300, "frontier at 300 -- no name recurred");

  /* classification at depth: the round just completed serves; the
   * retained round exactly 256 behind -- whose byte-world name would
   * collide with the frontier's -- is name-exact and serves; the
   * frontier records owed; ahead names are inert at every distance,
   * and no behind name ever reads as ahead */
  r = rv(systemFrontier(s)) - 1;
  n = systemReceived(s, rn(r), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && rv(out[0].round) == r,
   "retained round serves at depth");
  n = systemReceived(s, rn(rv(systemFrontier(s)) - 256), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE,
   "an entry 256 behind is name-exact: no collision, no alias");
  systemReceived(s, systemFrontier(s), 5, 0, 0, out);
  CHECK(systemOwed(s), "frontier evidence records owed at depth");
  CHECK(systemReceived(s, rn(rv(systemFrontier(s)) + 40), 5, 0, 0, out) == 0,
   "ahead name inert");
  CHECK(systemReceived(s, rn(rv(systemFrontier(s)) + 300), 5, 0, 0, out) == 0,
   "far-ahead name inert -- ahead at every distance");

  /* a short reach: eviction keeps only the last 4 rounds, so behind
   * names classify released (inert) and ahead names are beyond
   * reach (inert) at any distance */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 4);
  for (k = 0; k < 300; ++k) {
    n = roundTrip(s, out);
    if (k >= 4 && (n != 1 || out[0].act != SYSTEM_ACT_RELEASE)) {
      CHECK(0, "a full short reach evicts each round");
      break;
    }
  }
  CHECK(k == 300, "300 short-reach rounds");
  CHECK(systemReceived(s, rn(rv(systemFrontier(s)) - 24), 5, 0, 0, out)
   == 0, "evicted round inert");
  CHECK(systemReceived(s, rn(rv(systemFrontier(s)) + 116), 5, 0, 0, out)
   == 0, "ahead inert");
  CHECK(systemReceived(s, rn(rv(systemFrontier(s)) + 128), 5, 0, 0, out)
   == 0, "a +128 name is ahead, never a behind alias");
  r = rv(systemFrontier(s)) - 1;
  n = systemReceived(s, rn(r), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && rv(out[0].round) == r,
   "short reach: retained round serves");

  /* a lingering entry under a short reach: siblings release via all-n
   * each round, so the reach never fills and round 0 survives past
   * every byte-world boundary -- reach-behind is retention, and the
   * lingerer releases only at all-n */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 8);
  CHECK(roundTrip(s, out) == 0, "lingering: round 0 retained");
  for (k = 1; k < 300; ++k) {
    if (roundTrip(s, out) != 0) {
      CHECK(0, "lingering: no eviction while siblings release");
      break;
    }
    for (p = 1; p <= TN; ++p)
      n = systemPossessed(s, rn(k), p, out);
    if (n != 1 || out[0].act != SYSTEM_ACT_RELEASE) {
      CHECK(0, "lingering: sibling all-n release");
      break;
    }
  }
  CHECK(k == 300, "lingering: 299 sibling rounds released");
  CHECK(systemRetained(s, rn(0)), "the lingerer survives at depth 300");
  n = systemReceived(s, rn(0), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && rv(out[0].round) == 0,
   "a 300-deep entry still serves -- reach-behind is retention");
  for (p = 1; p <= TN; ++p)
    n = systemPossessed(s, rn(0), p, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 0,
   "the lingerer releases only at all-n");

  /* the ordinal instantiation crosses its width's wrap as a
   * non-event: the displacement comparator owns the wrap, the
   * machine never sees a width.  Deep start just below the
   * boundary, run across it; eviction stays oldest-first and
   * classification stays exact through the crossing. */
  SEAT(s, Store, TN, TT, TSELF, rn((unsigned long)-8), 4);
  for (k = 0; k < 32; ++k) {
    r = rv(systemFrontier(s));
    n = roundTrip(s, out);
    if (k < 4) {
      if (n != 0) {
        CHECK(0, "wrap: no release before the reach fills");
        break;
      }
    } else if (n != 1 || out[0].act != SYSTEM_ACT_RELEASE
     || rv(out[0].round) != r - 4) {
      CHECK(0, "wrap: eviction stays oldest across the boundary");
      break;
    }
  }
  CHECK(k == 32, "wrap: 32 rounds crossed the width boundary");
  CHECK(rv(systemFrontier(s)) == (unsigned long)-8 + 32,
   "wrap: the frontier crossed as a non-event");
  r = rv(systemFrontier(s)) - 1;
  n = systemReceived(s, rn(r), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && rv(out[0].round) == r,
   "wrap: retained round serves across the boundary");
  CHECK(systemReceived(s, rn(r + 20), 5, 0, 0, out) == 0,
   "wrap: ahead stays inert across the boundary");


  /* ---------------------------------------------------------------- */
  BANNER("G: large-n bitmaps (bs > 1)");
  /* ---------------------------------------------------------------- */

  /* 201 processes (n = 200 encoded), t = 66 (n-t = 135) -> 26-byte
   * bitmaps; self past the first byte */
  SEAT(s, Store, 200, 66, 60, rn(0), 4);
  CHECK(roundTrip(s, out) == 0, "large-n round 0 completes");
  CHECK(SYSTEM_TST(systemPossess(s, rn(0)), 60), "self bit past byte 0");
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "1 of 135: held");

  /* want bits at byte offsets 1 and 25 of the mask */
  n = systemReceived(s, rn(0), 9, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE
   && SYSTEM_TST(out[0].want, 9), "want bit in byte 1");
  n = systemReceived(s, rn(0), 200, 0, 0, out);
  CHECK(n == 1 && SYSTEM_TST(out[0].want, 9)
   && SYSTEM_TST(out[0].want, 200)
   && !SYSTEM_TST(out[0].want, 199), "want bits span the mask");
  memset(cursor, 0, sizeof (cursor));
  CHECK(systemServe(s, cursor, out) == 1, "large-n walk serves");

  /* the have grain copies across the full mask at bs > 1 (grease
   * pre-possesses ~134 low processes on round 0; the all-n loop
   * below still releases only at its last recording) */
  {
    unsigned char hv[26];

    memset(hv, 0, sizeof (hv));
    BIT_SET(hv, 9);
    BIT_SET(hv, 200);
    grease(s, out);
    n = systemLaunch(s, 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT, "large-n admit");
    closeFrontier(s, hv, out);
  }
  CHECK(systemHave(s, rn(1)) && SYSTEM_TST(systemHave(s, rn(1)), 9)
   && SYSTEM_TST(systemHave(s, rn(1)), 200)
   && !SYSTEM_TST(systemHave(s, rn(1)), 199),
   "large-n have copy spans the mask");

  /* all-n release fires at the 201st distinct possession and never
   * before (no count shortcut at any byte boundary) */
  rel = 0;
  for (k = 0; k <= 200; ++k) {
    if (k == 60)
      continue;
    n = systemPossessed(s, rn(0), (unsigned char)k, out);
    if (n) {
      CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE
       && rv(out[0].round) == 0, "large-n release is the release act");
      CHECK(k == 200, "release only at all-n");
      ++rel;
    }
  }
  CHECK(rel == 1, "exactly one release across 200 recordings");
  CHECK(!systemRetained(s, rn(0)), "large-n round released");

  /* ---------------------------------------------------------------- */
  BANNER("H: O2 / O3 / O5");
  /* ---------------------------------------------------------------- */

  SEAT(s, Store, TN, TT, TSELF, rn(0), 4);

  /* O5: maintenance outranks the value and never consumes it */
  n = systemLaunch(s, 1, 1, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_MAINTAIN && rv(out[0].round) == 0,
   "maintenance outranks the pending value");
  closeFrontier(s, 0, out);

  /* maintenance is chosen work: capacity- and advance-gated */
  CHECK(systemLaunch(s, 0, 1, 0, 1, out) == 0, "maintain: backlog gate");
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "prior at self only");
  CHECK(systemLaunch(s, 0, 1, 1, 1, out) == 0, "maintain held by advance");
  grease(s, out);
  CHECK(systemLaunch(s, 0, 1, 1, 0, out) == 0, "maintain waits tolerance");
  n = systemLaunch(s, 0, 1, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_MAINTAIN && rv(out[0].round) == 1,
   "maintenance without a value");

  /* O2: held members at completion, late assembly, riding on SERVE */
  {
    unsigned char hv[(TN + 8) / 8];

    memset(hv, 0, sizeof (hv));
    BIT_SET(hv, TSELF);
    BIT_SET(hv, 3);
    closeFrontier(s, hv, out);
  }
  r = rv(systemFrontier(s)) - 1; /* round 1 */
  CHECK(systemHave(s, rn(r)) && SYSTEM_TST(systemHave(s, rn(r)), TSELF)
   && SYSTEM_TST(systemHave(s, rn(r)), 3)
   && !SYSTEM_TST(systemHave(s, rn(r)), 5),
   "held members recorded at completion");
  systemAssembled(s, rn(r), 5);
  CHECK(SYSTEM_TST(systemHave(s, rn(r)), 5), "late assembly recorded");
  systemAssembled(s, rn(r), TN + 1);   /* out of range: ignored */
  systemAssembled(s, rn(r + 100), 2);  /* unretained: ignored */
  n = systemReceived(s, rn(r), 6, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE
   && out[0].want && SYSTEM_TST(out[0].want, 6)
   && out[0].have && SYSTEM_TST(out[0].have, 3)
   && SYSTEM_TST(out[0].have, 5) && !SYSTEM_TST(out[0].have, 4),
   "serve carries the held-members grain");
  systemPossessed(s, rn(r), 6, out);
  memset(cursor, 0, sizeof (cursor));
  CHECK(systemServe(s, cursor, out) == 0,
   "walk 0-return at quiescence (rounds retained, nothing owed)");

  /* O3: the witness book on a fresh (behind) instance */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 4);
  systemReceived(s, rn(0), 2, 0, 0, out);
  CHECK(systemOwed(s), "recovery traffic records owed");
  CHECK(systemWitness(s, rn(0), 2, out) == 0, "1 of t+1: accumulate");
  CHECK(systemWitness(s, rn(0), 2, out) == 0, "witness idempotent per server");
  CHECK(systemWitness(s, rn(0), 3, out) == 0,
   "2 of t+1: accumulate (= t distinct forgers cannot adopt)");
  CHECK(systemWitness(s, rn(0), TSELF, out) == 0, "self is not a server");
  CHECK(systemWitness(s, rn(0), TN + 1, out) == 0, "witness from out of range");
  CHECK(systemWitness(s, rn(1), 4, out) == 0, "wrong round ignored");
  n = systemWitness(s, rn(0), 4, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADOPT && rv(out[0].round) == 0,
   "t+1 distinct servers: adopt");
  CHECK(systemWitness(s, rn(0), 5, out) == 0, "adopt fires exactly once");
  CHECK(systemWitnesses(s) && SYSTEM_TST(systemWitnesses(s), 4)
   && SYSTEM_TST(systemWitnesses(s), 5), "witness bits recorded");

  /* candidate switch: reset re-arms and the true candidate
   * re-accumulates */
  systemWitnessReset(s);
  CHECK(systemWitnesses(s) && !SYSTEM_TST(systemWitnesses(s), 2),
   "reset clears the book");
  systemWitness(s, rn(0), 2, out);
  systemWitness(s, rn(0), 3, out);
  n = systemWitness(s, rn(0), 4, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADOPT, "reset re-arms adopt");

  /* adoption closes through the one consume region: JOIN (the
   * recovery traffic recorded owed), then systemComplete -- which
   * clears the book and re-arms for the next frontier */
  n = systemLaunch(s, 0, 0, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN, "adoption closes via JOIN");
  closeFrontier(s, 0, out);
  CHECK(rv(systemFrontier(s)) == 1, "adoption advanced the frontier");
  CHECK(systemWitnesses(s) && !SYSTEM_TST(systemWitnesses(s), 2),
   "completion clears the book");

  /* a BELOW-threshold book is superseded by own COMPLETE: the
   * partial accumulation clears and the latch stays clean */
  CHECK(systemWitness(s, rn(1), 5, out) == 0, "partial book: 1 of t+1");
  systemReceived(s, rn(1), 5, 0, 0, out);  /* owed for frontier 1 */
  grease(s, out);                          /* round 0 to n-t */
  n = systemLaunch(s, 0, 0, 0, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN, "join for frontier 1");
  closeFrontier(s, 0, out);
  CHECK(rv(systemFrontier(s)) == 2, "frontier 2");
  CHECK(systemWitnesses(s) && !SYSTEM_TST(systemWitnesses(s), 5),
   "own COMPLETE supersedes a partial book");
  systemWitness(s, rn(2), 2, out);
  systemWitness(s, rn(2), 3, out);
  n = systemWitness(s, rn(2), 4, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADOPT && rv(out[0].round) == 2,
   "supersession left the latch clean for the next frontier");

  /* ---------------------------------------------------------------- */
  BANNER("I: premature possession indications");
  /* ---------------------------------------------------------------- */

  /* An indication for a round with no record yet is DROPPED, not
   * banked (system.h systemReceived: only RETAINED rounds have a
   * record).  The drop is safe but NOT self-correcting, so the caller
   * holds it and re-presents it through systemPossessed once the close
   * has retained the round.  What that obligation is worth is the
   * EQUIVALENCE below: the record a held-then-re-presented indication
   * reaches must be the record it would have reached had it arrived
   * after retention.  Nothing else in this file feeds possesses = 1
   * for the frontier round.  (systemPossessed on a RELEASED round is
   * inert -- section E already covers that edge.) */
  {
    struct system *s2;
    struct systemStore *st2;
    unsigned int bs;

    bs = ((TN + 1) + 7) / 8;

    SEAT(s, Store, TN, TT, TSELF, rn(0), 4);
    /* one wire, two effects: it records participation owed for the
     * frontier, and its indication is dropped for want of a record */
    CHECK(systemReceived(s, rn(0), 3, 1, 0, out) == 0,
     "premature indication takes no act");
    CHECK(systemOwed(s), "the same wire recorded participation owed");
    CHECK(!systemRetained(s, rn(0)), "the frontier round has no record yet");
    n = systemLaunch(s, 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN, "owed discharges as JOIN");
    systemComplete(s, rn(0), rn(1), 0, out);
    CHECK(systemRetained(s, rn(0)), "round 0 retained by the close");
    CHECK(systemPossess(s, rn(0))
     && !SYSTEM_TST(systemPossess(s, rn(0)), 3),
     "the premature indication was dropped, not banked");

    /* the caller re-presents what it held */
    systemPossessed(s, rn(0), 3, out);
    CHECK(SYSTEM_TST(systemPossess(s, rn(0)), 3),
     "re-presented indication is banked");

    /* THE EQUIVALENCE.  s2 differs in one respect only: its
     * indication arrives after the round is retained, riding traffic
     * the way system.h's primary source does.  Both instances take
     * the same existence evidence first, so the launch is a JOIN in
     * both and only the indication's TIMING varies. */
    if ((s2 = calloc(1, systemSz(TN, RS)))
     && (st2 = calloc(1, systemStoreSz(TN, RS, 4)))) {
      SEAT(s2, st2, TN, TT, TSELF, rn(0), 4);
      systemReceived(s2, rn(0), 3, 0, 0, out);
      systemLaunch(s2, 1, 0, 1, 1, out);
      systemComplete(s2, rn(0), rn(1), 0, out);
      systemReceived(s2, rn(0), 3, 1, 0, out);
      /* non-vacuity: the comparison below would pass on two EMPTY
       * records, so pin that the timely indication really was banked */
      CHECK(SYSTEM_TST(systemPossess(s2, rn(0)), 3),
       "the timely indication was banked");
      CHECK(!memcmp(systemPossess(s, rn(0)), systemPossess(s2, rn(0)), bs),
       "re-presented possession record is byte-identical to a timely one");
      CHECK(!memcmp(systemWant(s, rn(0)), systemWant(s2, rn(0)), bs),
       "re-presented want record is byte-identical to a timely one");
      CHECK(systemDuty(s) == systemDuty(s2),
       "re-presented duty class is identical to a timely one");
      free(st2);
    }
    free(s2);
  }


  /* ---------------------------------------------------------------- */
  BANNER("J: L3 commutation");
  /* ---------------------------------------------------------------- */

  /* system.md L3.  Order A is the adoption arm: k witness recordings,
   * then the close.  Order B is the late-witness arm: the close, then
   * the SAME k recordings -- which now name a round that is no longer
   * the frontier.  Both replays start from a byte-identical copy of
   * the whole caller allocation (zeroed before systemInit, so padding
   * is deterministic) AND of the store beside it, mint the SAME
   * successor name, and both are compared whole at the end.
   *
   * TWO ALLOCATIONS PER REPLAY, and the store side is load-bearing:
   * the retained rounds are the caller's now, so the two orders can
   * reach identical machine bytes and still differ in what is
   * retained -- an oracle over the seat alone would no longer state
   * L3.  The seats are compared over their MACHINE-OWNED span
   * (MSTATE): two replays hold two stores, so their ctx differs by
   * construction and says nothing about the machine.  The stores are
   * compared through systemStoreState, which hands back the live
   * entries in the comparator's order with no freed tail, so equal
   * retained sets give equal bytes.
   *
   * ops[] entries below 0 are systemWitnessReset. */
  {
    static const signed char ops1[] = { 1 };
    static const signed char ops2[] = { 1, 2 };
    static const signed char ops3[] = { 1, 2, 3 };
    static const signed char ops4[] = { 1, 2, 3, 4 };
    static const signed char ops5[] = { 1, 2, -1, 3, 4, 5 };
    static const signed char ops6[] = { 1, 1, 2, 2, 3 };
    static const signed char opsd1[] = { 1 };
    static const signed char opsd2[] = { 1, 1 };
    static const signed char opsd3[] = { 1, -1, 1 };
    static const struct {
      const signed char *ops;  /* < 0 = systemWitnessReset */
      const char *name;
      unsigned int nops;
      unsigned int adopts;     /* ADOPT acts order A must output */
      unsigned int closeActs;  /* acts the close itself must output */
      unsigned int base;       /* which common pre-state */
      unsigned char late;      /* server for the after-the-close probe */
    } comm[] = {
      { ops1,  "J: k=1, below t+1",             1, 0, 0, 0, 6 },
      { ops2,  "J: k=2, below t+1",             2, 0, 0, 0, 6 },
      { ops3,  "J: k=t+1, latch fired",         3, 1, 0, 0, 6 },
      { ops4,  "J: k past t+1",                 4, 1, 0, 0, 6 },
      { ops5,  "J: candidate switch",           6, 1, 0, 0, 6 },
      { ops6,  "J: repeated servers",           5, 1, 0, 0, 6 },
      { ops3,  "J: k=t+1 over a full reach",    3, 1, 1, 1, 6 },
      { opsd1, "J: t=0, one witness is t+1",    1, 1, 0, 2, 1 },
      { opsd2, "J: t=0, idempotent server",     2, 1, 0, 2, 1 },
      { opsd3, "J: t=0, reset re-arms",         3, 2, 0, 2, 1 }
    };
    struct system *jbase[3];
    struct system *ja;
    struct system *jb;
    struct system *jsnap;
    struct systemStore *jbaseSt[3];
    struct systemStore *jaSt;
    struct systemStore *jbSt;
    struct systemAct ob[SYSTEM_MAX_ACTS];
    const unsigned char *sa;
    const unsigned char *sb;
    unsigned char *jsnapSt;
    unsigned long rndA[SYSTEM_MAX_ACTS];
    unsigned long jsz;
    unsigned long jssz;
    unsigned long la;
    unsigned long lb;
    unsigned long closeRv;
    unsigned long nv;
    unsigned int c;
    unsigned int i;
    unsigned int j;
    unsigned int wa;
    unsigned int wb;
    unsigned int ca;
    unsigned int cb;
    unsigned char actA[SYSTEM_MAX_ACTS];
    unsigned char closeName[sizeof (unsigned long)];
    unsigned char nextName[sizeof (unsigned long)];

    /* one seat size and one store size cover every pre-state, so a
     * replay copies and compares the same spans whatever the
     * configuration */
    jsz = systemSz(TN, RS);
    if (systemSz(1, RS) > jsz)
      jsz = systemSz(1, RS);
    jssz = systemStoreSz(TN, RS, 4);
    if (systemStoreSz(1, RS, 4) > jssz)
      jssz = systemStoreSz(1, RS, 4);
    if (!(jbase[0] = calloc(1, jsz)) || !(jbase[1] = calloc(1, jsz))
     || !(jbase[2] = calloc(1, jsz)) || !(ja = calloc(1, jsz))
     || !(jb = calloc(1, jsz)) || !(jsnap = calloc(1, jsz))
     || !(jbaseSt[0] = calloc(1, jssz)) || !(jbaseSt[1] = calloc(1, jssz))
     || !(jbaseSt[2] = calloc(1, jssz)) || !(jaSt = calloc(1, jssz))
     || !(jbSt = calloc(1, jssz)) || !(jsnapSt = calloc(1, jssz))) {
      fprintf(stderr, "malloc failed\n");
      return (1);
    }

    /* pre-state 0: fresh, frontier 0 live */
    SEAT(jbase[0], jbaseSt[0], TN, TT, TSELF, rn(0), 4);
    n = systemLaunch(jbase[0], 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT,
     "pre-state 0: frontier 0 live");

    /* pre-state 1: the reach exhausted (4 rounds held), so the close
     * itself must output a RELEASE in both orders */
    SEAT(jbase[1], jbaseSt[1], TN, TT, TSELF, rn(0), 4);
    for (j = 0; j < 4; ++j)
      if (roundTrip(jbase[1], out) != 0) {
        CHECK(0, "pre-state 1: four rounds retained");
        break;
      }
    grease(jbase[1], out);
    n = systemLaunch(jbase[1], 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && rv(out[0].round) == 4,
     "pre-state 1: frontier 4 live over a full reach");

    /* pre-state 2: the degenerate arm -- 2 processes, t = 0, where a
     * single witness IS the t+1 threshold */
    SEAT(jbase[2], jbaseSt[2], 1, 0, 0, rn(0), 4);
    n = systemLaunch(jbase[2], 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT,
     "pre-state 2: t = 0 frontier 0 live");

    for (c = 0; c < sizeof (comm) / sizeof (comm[0]); ++c) {
      BANNER(comm[c].name);

      /* each replay gets the pre-state's seat AND its store; the
       * copied seat carries the pre-state's ctx, so the replay's own
       * store is re-pointed into it (the one field a byte copy
       * cannot carry across allocations) */
      memcpy(ja, jbase[comm[c].base], jsz);
      memcpy(jb, jbase[comm[c].base], jsz);
      memcpy(jaSt, jbaseSt[comm[c].base], jssz);
      memcpy(jbSt, jbaseSt[comm[c].base], jssz);
      ja->ctx = jaSt;
      jb->ctx = jbSt;
      memcpy(closeName, systemFrontier(ja), RS);
      closeRv = rv(closeName);
      nv = closeRv + 1;
      memcpy(nextName, &nv, RS);

      /* order A: the recordings, then the close */
      wa = 0;
      for (j = 0; j < comm[c].nops; ++j) {
        if (comm[c].ops[j] < 0) {
          systemWitnessReset(ja);
          continue;
        }
        n = systemWitness(ja, closeName, comm[c].ops[j], out);
        for (i = 0; i < n; ++i) {
          CHECK(out[i].act == SYSTEM_ACT_ADOPT
           && rv(out[i].round) == closeRv,
           "order A: a witness act is ADOPT of the frontier round");
          ++wa;
        }
      }
      ca = systemComplete(ja, closeName, nextName, 0, out);
      for (i = 0; i < ca && i < SYSTEM_MAX_ACTS; ++i) {
        actA[i] = out[i].act;
        rndA[i] = rv(out[i].round);
      }

      /* order B: the close, then the same recordings -- late */
      cb = systemComplete(jb, closeName, nextName, 0, ob);
      wb = 0;
      for (j = 0; j < comm[c].nops; ++j) {
        if (comm[c].ops[j] < 0) {
          systemWitnessReset(jb);
          continue;
        }
        wb += systemWitness(jb, closeName, comm[c].ops[j], out);
      }

      CHECK(wa == comm[c].adopts, "order A output the expected ADOPTs");
      CHECK(wb == 0, "order B: late recordings are refused whole");
      CHECK(ca == comm[c].closeActs, "the close output the expected acts");
      CHECK(cb == ca, "both orders' closes output the same count");
      for (i = 0; i < ca && i < cb && i < SYSTEM_MAX_ACTS; ++i)
        CHECK(actA[i] == ob[i].act && rndA[i] == rv(ob[i].round),
         "close act kind and round agree across the orders");
      if (comm[c].closeActs)
        CHECK(actA[0] == SYSTEM_ACT_RELEASE && rndA[0] == 0,
         "the exhausted-reach close releases the oldest (non-vacuity)");
      CHECK(!memcmp(MSTATE(ja), MSTATE(jb), jsz - MSTATEOFF(ja)),
       "both orders reach the byte-identical machine state");
      sa = systemStoreState(jaSt, &la);
      sb = systemStoreState(jbSt, &lb);
      CHECK(la == lb && !memcmp(sa, sb, la),
       "both orders retain the byte-identical set of rounds");
      CHECK(la, "the compared retained set is not empty (non-vacuity)");

      /* the late-witness arm stated directly: a recording naming the
       * completed round is refused whole and changes nothing -- in
       * the seat or in what it retains */
      memcpy(jsnap, ja, jsz);
      sa = systemStoreState(jaSt, &la);
      memcpy(jsnapSt, sa, la);
      CHECK(systemWitness(ja, closeName, comm[c].late, out) == 0
       && !memcmp(ja, jsnap, jsz),
       "a witness after the close changes nothing");
      sa = systemStoreState(jaSt, &lb);
      CHECK(lb == la && !memcmp(sa, jsnapSt, la),
       "a witness after the close retains what it retained");

      /* the region consumes once per round, first arm: before any
       * later launch the second close finds no live instance */
      memcpy(jsnap, ja, jsz);
      sa = systemStoreState(jaSt, &la);
      memcpy(jsnapSt, sa, la);
      CHECK(systemComplete(ja, closeName, nextName, 0, out) == 0
       && !memcmp(ja, jsnap, jsz), "second close of the round is inert");
      sa = systemStoreState(jaSt, &lb);
      CHECK(lb == la && !memcmp(sa, jsnapSt, la),
       "the inert second close retained nothing further");
      memcpy(jsnap, jb, jsz);
      sb = systemStoreState(jbSt, &la);
      memcpy(jsnapSt, sb, la);
      CHECK(systemComplete(jb, closeName, nextName, 0, out) == 0
       && !memcmp(jb, jsnap, jsz),
       "second close of the round is inert in either order");
      sb = systemStoreState(jbSt, &lb);
      CHECK(lb == la && !memcmp(sb, jsnapSt, la),
       "the inert second close retains identically in either order");
    }

    /* the corollary's second arm: after a later launch the same round
     * is no longer the frontier, so the close is refused on the round
     * binding rather than on liveness */
    BANNER("J: second close after a relaunch");
    memcpy(ja, jbase[0], jsz);
    memcpy(jaSt, jbaseSt[0], jssz);
    ja->ctx = jaSt;
    systemComplete(ja, rn(0), rn(1), 0, out);
    grease(ja, out);
    n = systemLaunch(ja, 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && rv(out[0].round) == 1,
     "relaunch at the next frontier");
    memcpy(jsnap, ja, jsz);
    sa = systemStoreState(jaSt, &la);
    memcpy(jsnapSt, sa, la);
    CHECK(systemComplete(ja, rn(0), rn(9), 0, out) == 0
     && !memcmp(ja, jsnap, jsz),
     "the stale round's close is refused with an instance live");
    sa = systemStoreState(jaSt, &lb);
    CHECK(lb == la && !memcmp(sa, jsnapSt, la),
     "the refused stale close retained nothing");

    free(jsnapSt);
    free(jbSt);
    free(jaSt);
    free(jbaseSt[2]);
    free(jbaseSt[1]);
    free(jbaseSt[0]);
    free(jsnap);
    free(jb);
    free(ja);
    free(jbase[2]);
    free(jbase[1]);
    free(jbase[0]);
  }

  /* ---------------------------------------------------------------- */
  BANNER("K: the cursor birth");
  /* ---------------------------------------------------------------- */

  /* A high-water act of round C births want, for its sender, of every
   * retained round AFTER C whose possession that sender has not
   * evidenced (system.md Model, want: the cursor birth) -- and of
   * nothing else.  Config as ever: grease leaves each retained round
   * possessed by {0..4}, so 5 and 6 are the wanting processes. */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 8);
  for (k = 0; k < 5; ++k)
    roundTrip(s, out);             /* rounds 0..4 retained, 5 = frontier */

  /* cursor evidence at round 1 from process 6: direct want of 1 (one
   * SERVE act), cursor want of 2..4, nothing at 0 (below the cursor),
   * no act for the cursor births (the serve walk discharges them) */
  n = systemReceived(s, rn(1), 6, 0, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && rv(out[0].round) == 1,
   "cursor act: the direct birth still serves its own round");
  CHECK(systemWant(s, rn(1)) && SYSTEM_TST(systemWant(s, rn(1)), 6),
   "direct want of the cursor round");
  CHECK(systemWant(s, rn(2)) && SYSTEM_TST(systemWant(s, rn(2)), 6),
   "cursor want of the round after");
  CHECK(systemWant(s, rn(4)) && SYSTEM_TST(systemWant(s, rn(4)), 6),
   "cursor want reaches the newest retained round");
  CHECK(systemWant(s, rn(0)) && !SYSTEM_TST(systemWant(s, rn(0)), 6),
   "nothing below the cursor: the sender proved it holds those");

  /* below the high-water: the same act without cursor evidence births
   * only the direct want */
  n = systemReceived(s, rn(1), 5, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE,
   "stale retry: direct birth only");
  CHECK(systemWant(s, rn(3)) && !SYSTEM_TST(systemWant(s, rn(3)), 5),
   "stale retry: no cursor want above");

  /* the possession gate: evidenced possession pre-empts the birth,
   * and possession recording retires it */
  systemPossessed(s, rn(3), 6, out);
  CHECK(systemWant(s, rn(3)) && !SYSTEM_TST(systemWant(s, rn(3)), 6),
   "possession recording clears the cursor want (I2)");
  n = systemReceived(s, rn(1), 6, 0, 1, out);
  CHECK(systemWant(s, rn(3)) && !SYSTEM_TST(systemWant(s, rn(3)), 6),
   "a later cursor act cannot re-birth over possession (the gate)");
  CHECK(systemWant(s, rn(2)) && SYSTEM_TST(systemWant(s, rn(2)), 6),
   "while the unpossessed rounds stay owed (idempotent)");

  /* self is gated by I3: a cursor act attributed to self births
   * nothing (self possesses every retained round) */
  n = systemReceived(s, rn(1), TSELF, 0, 1, out);
  for (k = 2; k < 5; ++k)
    CHECK(systemWant(s, rn(k)) && !SYSTEM_TST(systemWant(s, rn(k)), TSELF),
     "no cursor want for self (I3 gates the walk)");

  /* a cursor act of the frontier finds nothing retained after it */
  n = systemReceived(s, systemFrontier(s), 6, 0, 1, out);
  CHECK(systemWant(s, rn(4)) && SYSTEM_TST(systemWant(s, rn(4)), 6),
   "frontier cursor act: retained wants unchanged");
  CHECK(systemOwed(s), "frontier cursor act still records owed");

  /* a RELEASED cursor round still locates: complete round 2's record
   * to all n (release), then cursor evidence OF the released round
   * births want of the retained rounds after it */
  for (k = 5; k <= TN; ++k)
    n = systemPossessed(s, rn(2), (unsigned char)k, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 2,
   "round 2 released at all n");
  n = systemReceived(s, rn(2), 5, 0, 1, out);
  CHECK(n == 0, "released cursor round: no direct act");
  CHECK(systemWant(s, rn(3)) && SYSTEM_TST(systemWant(s, rn(3)), 5),
   "released cursor round still births want above");

  /* the serve walk discharges cursor wants like any others */
  memset(cursor, 0, sizeof (cursor));
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE,
   "the serve walk carries the cursor wants");

  /* ---------------------------------------------------------------- */
  BANNER("L: the serve cursor is a round NAME");
  /* ---------------------------------------------------------------- */

  /* systemServe's cursor holds the NAME of the round last served, so
   * the rotation is positioned in the round order ITSELF and not by a
   * count into a set that changes between calls.  What that buys is
   * the whole point of the walk: the returner it exists for stands at
   * the OLDEST rung, which is the one a count into a growing set
   * starves last.  Three claims, none of them observable from the
   * cursor's bytes: the walk visits every owed round before revisiting
   * one, it WRAPS to the oldest at the end, and a cursor naming a
   * round since RELEASED still positions it -- an order question does
   * not require the round be retained. */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 8);
  for (k = 0; k < 6; ++k)
    roundTrip(s, out);               /* rounds 0..5 retained, 6 = frontier */
  for (k = 0; k < 6; ++k)
    systemReceived(s, rn(k), 6, 0, 0, out);  /* every round owed to 6 */

  memset(cursor, 0, sizeof (cursor));
  rel = 0;
  for (k = 0; k < 6; ++k) {
    if (systemServe(s, cursor, out) != 1 || rv(out[0].round) != k)
      break;
    ++rel;
  }
  CHECK(rel == 6, "a zeroed cursor starts oldest and walks the order");
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 0,
   "the walk WRAPS to the oldest -- no rung is served twice per turn");

  /* the cursor is not a count: a release BENEATH it must not shift the
   * walk, and a release OF the round it names must not lose it */
  memset(cursor, 0, sizeof (cursor));
  systemServe(s, cursor, out);       /* cursor at round 0 */
  systemServe(s, cursor, out);       /* cursor at round 1 */
  systemServe(s, cursor, out);       /* cursor at round 2 */
  for (p = 1; p <= TN; ++p)
    n = systemPossessed(s, rn(0), p, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 0,
   "round 0 released beneath the cursor");
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 3,
   "a release beneath the cursor does not displace the walk"
   " (a count would have skipped)");

  /* now release the round the cursor NAMES */
  for (p = 1; p <= TN; ++p)
    n = systemPossessed(s, rn(3), p, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && rv(out[0].round) == 3,
   "the cursor's own round released");
  CHECK(!systemRetained(s, rn(3)), "and it is gone from the retained set");
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 4,
   "a cursor naming a RELEASED round still positions the walk");

  /* a round closed while the walk is mid-turn joins the rotation
   * rather than resetting it: the cursor names a place, and the new
   * round is after it */
  roundTrip(s, out);                 /* round 6 retained, 7 = frontier */
  systemReceived(s, rn(6), 6, 0, 0, out);
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 5, "the turn continues past a close");
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 6, "and reaches the round just closed");
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 1,
   "then wraps to the oldest still held");

  /* ---------------------------------------------------------------- */
  BANNER("M: an act's borrows outlive the call that made them");
  /* ---------------------------------------------------------------- */

  /* THE STABILITY TERM read from the machine's side (system.h, the
   * retention requirements): a record and a name stay valid until the
   * set NEXT changes through retain or release, and a relocation moves
   * what it moves intact.  Until-return is not enough, and this is the
   * section that says why: a SERVE act hands back the store's OWN name
   * in .round and the store's OWN record in .want / .have, and the
   * caller reads them after the call returns.  A store answering
   * either from per-call scratch satisfies every other arm in this
   * file and in test_systemStore.c -- it holds the right bytes at the
   * instant of return -- and dangles these.
   *
   * Two acts are held at once here for one reason: one scratch buffer
   * MERGES them, so the arms are stated as what must stay DIFFERENT
   * across a later call, which is not a property either act can carry
   * alone.  With the reference store every check below is a GREEN
   * CONTROL; its matched reds are test/storeMutants.sh's
   * SM_C17_SCRATCH_NAME and SM_C17_SCRATCH_RECORD. */
  SEAT(s, Store, TN, TT, TSELF, rn(0), 8);
  for (k = 0; k < 4; ++k)
    roundTrip(s, out);                 /* 0..3 retained, 4 = frontier */
  systemReceived(s, rn(0), 5, 0, 0, out);      /* round 0 owed to 5 */
  systemReceived(s, rn(1), 6, 0, 0, out);      /* round 1 owed to 6 */

  memset(cursor, 0, sizeof (cursor));
  n = systemServe(s, cursor, outM);
  CHECK(n == 1 && rv(outM[0].round) == 0,
   "the walk starts at the oldest owed round");
  /* every arm below reads the act this call produced, so the act is a
   * PRECONDITION and not an assumption: a machine that serves nothing
   * (MM_HR_GATES gates the walk on the held-members grain) would
   * otherwise fault here instead of reporting, and a suite that dies
   * where it could red is worth less.  Found by the 2026-08-14
   * line-by-line read of the machine tier, which this section had just
   * made a third crashing mutant. */
  if (n != 1)
    goto mDone;
  mRndP = outM[0].round;
  mWntP = outM[0].want;
  mRnd = rv(mRndP);
  memcpy(mWnt, mWntP, (TN + 8) / 8);
  CHECK(SYSTEM_TST(mWnt, 5),
   "non-vacuity: the act's .want really carries round 0's owed bit");
  CHECK(systemWant(s, outM[0].round)
   && !memcmp(systemWant(s, outM[0].round), outM[0].want, (TN + 8) / 8),
   "the act's .want is the record of the round its .round names");

  /* a second serve changes no retained round -- it only walks the
   * store from the cursor to the end -- so the first act's borrows
   * are still inside their lifetime */
  n = systemServe(s, cursor, out);
  CHECK(n == 1 && rv(out[0].round) == 1, "the walk moves on to round 1");
  CHECK(rv(mRndP) == mRnd,
   "the first act's .round still names its round after the next call");
  CHECK(!memcmp(mWntP, mWnt, (TN + 8) / 8),
   "and its .want still reads that round's record");
  CHECK(rv(out[0].round) != rv(mRndP),
   "the two held acts name DIFFERENT rounds"
   " (one scratch name would have merged them)");
  CHECK(memcmp(out[0].want, mWntP, (TN + 8) / 8),
   "and hand back DIFFERENT records"
   " (one scratch record would have merged them)");

  /* a call that does NOT reach the store at all leaves them alone too
   * -- the lifetime is the SET's, not the call's */
  systemDuty(s);
  CHECK(rv(mRndP) == mRnd && !memcmp(mWntP, mWnt, (TN + 8) / 8),
   "a query that changes nothing leaves both borrows standing");

mDone:
  free(s);
  free(Store);

  printf("test_system: %d checks, %d failures\n", Checks, Failures);
  if (Failures) {
    printf("FAILED\n");
    return (1);
  }
  printf("PASSED\n");
  return (0);
}
