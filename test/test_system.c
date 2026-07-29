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
 * Configuration: 7 processes (n = 6 encoded), t = 2, self = 0 --
 * so the R4 advance signal needs n-t = 5 composition possessions.
 * The helper grease() feeds possession from processes 1..4 until
 * the duty class leaves HELD; processes 5 and 6 stay free for the
 * want/serve scenarios.
 *
 * Sections:
 *
 *   A. API edges -- Sz monotone, Init defense (null, self > n and
 *      n+1 < 3t+1 rejections leave an inert instance), entry-point
 *      null/range defense, fresh-instance queries.
 *   B. PARTICIPATE -- existence evidence for the frontier round
 *      records owed (DERIVED, system.md R2a); evidence beyond
 *      reach is inert; owed discharges as JOIN; own COMPLETE
 *      retires it.
 *   C. ADMIT and the R4 advance rule -- the capacity gate; the duty
 *      classes (HELD holds regardless of tolerance; TOLERANCE
 *      advances only with tolerance elapsed; all-n release reads
 *      MET and advances without tolerance); joins gated exactly as
 *      admissions; owed work outranks chosen work.
 *   D. SERVE / possession -- want births with the sender in .want;
 *      a possessing sender births nothing; riding indications
 *      record and retire; forged possession marks only its sender;
 *      the walk round-robins two owed rounds.
 *   E. RETAIN / RELEASE -- all-n possession releases via the
 *      received path (and below all-n does NOT); window-full
 *      eviction releases the oldest; systemEvict oldest-first.
 *   F. Round-space wrap -- full-window and small-window regimes
 *      across the 255 -> 0 crossing.
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
 *      byte-identical snapshot of the WHOLE caller allocation and
 *      compares the allocations with memcmp, so no conjunct of
 *      "identical" is chosen by the test.  The one legitimate
 *      output difference -- order A's ADOPT acts, output before the
 *      close -- is encoded per scenario rather than ignored, and
 *      the close's own acts must match kind-for-kind and
 *      round-for-round.  Carries the once-per-round corollary in
 *      both of the proof's arms (before a later launch the second
 *      close finds no live instance; after one it names a round
 *      that is no longer the frontier).
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
 * round byte -> B, C and D; eviction taking the newest -> E x3 and
 * J's full-window non-vacuity arm; the SERVE walk gated on the
 * held-members grain -> D x3 and G.
 *
 * Section F stands ALONE for one of them: removing the frontier+1
 * lookahead release fires five F checks in both wrap regimes and
 * NOTHING else -- the invariant falsifier cannot reach that guard at
 * any feasible horizon (an entry 255 rounds behind the completing
 * frontier), so F plus the hand discharge of I1 is the whole of its
 * coverage.
 *
 * Two of the nine crash this suite before its count line: a round
 * released early or born under the wrong byte makes systemPossess
 * return 0, and the SYSTEM_TST of a null bitmap faults.  The checks
 * printed before the fault are the red, and the falsifier is the
 * designated oracle for both, so the suite was left unhardened
 * against a machine that breaks the queries' own preconditions.
 *
 * Header encoding convention (CRITICAL):
 *   n parameter is encoded; actual process count = n + 1
 *   w parameter is encoded; actual retained window = w + 1
 *
 * Style: C89, K&R, 2-space indent, single monolithic main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

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

/* Feed possession of the prior round from low process indices until
 * the duty class leaves HELD (reaches n-t).  Inert at genesis or
 * when the prior round is already released. */
static void
grease(
  struct system *s
 ,struct systemAct *out
){
  unsigned char prior;
  unsigned char p;

  prior = (unsigned char)(systemFrontier(s) - 1);
  if (!systemRetained(s, prior))
    return;
  for (p = 1; systemDuty(s) == SYSTEM_DUTY_HELD && p != 0; ++p)
    systemPossessed(s, prior, p, out);
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
  return (systemComplete(s, systemFrontier(s), 0, out));
}

int
main(
  void
){
  struct system *s;
  struct systemAct out[SYSTEM_MAX_ACTS];
  unsigned char cursor;
  unsigned int n;
  unsigned int k;
  unsigned int rel;
  unsigned char r;

  /* ---------------------------------------------------------------- */
  BANNER("A: sizes and init defense");
  /* ---------------------------------------------------------------- */

  CHECK(systemSz(TN, 0) > sizeof (struct system), "Sz exceeds header");
  CHECK(systemSz(TN, 1) > systemSz(TN, 0), "Sz monotone in w");
  CHECK(systemSz(255, 0) > systemSz(TN, 0), "Sz monotone in n");

  systemInit(0, TN, TT, 0, TSELF); /* must not crash */

  if (!(s = malloc(systemSz(TN, 3)))) {
    fprintf(stderr, "malloc failed\n");
    return (1);
  }

  /* self > n is rejected: the instance is inert on every entry */
  systemInit(s, TN, TT, 3, TN + 1);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected self: no ADMIT");
  CHECK(systemReceived(s, 0, 1, 0, out) == 0, "rejected self: inert");
  CHECK(systemComplete(s, systemFrontier(s), 0, out) == 0,
   "rejected self: no complete");
  cursor = 0;
  CHECK(systemServe(s, &cursor, out) == 0, "rejected self: no serve");
  CHECK(systemPossessed(s, 0, 1, out) == 0, "rejected self: no possession");
  CHECK(systemWitness(s, 0, 1, out) == 0, "rejected self: no witness");
  CHECK(systemEvict(s, out) == 0, "rejected self: no evict");
  systemWitnessReset(s); /* must not crash on a rejected instance */
  systemAssembled(s, 0, 1); /* must not crash on a rejected instance */

  /* n + 1 < 3t + 1 is rejected (7 < 10) */
  systemInit(s, TN, 3, 3, TSELF);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected t: inert");

  /* the single-process encoding n = 0 is rejected */
  systemInit(s, 0, 0, 3, 0);
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "rejected single: inert");

  systemInit(s, TN, TT, 3, TSELF);
  CHECK(systemFrontier(s) == 0, "fresh frontier 0");
  CHECK(!systemLive(s), "fresh not live");
  CHECK(!systemOwed(s), "fresh not owed");
  CHECK(systemDuty(s) == SYSTEM_DUTY_MET, "genesis duty met");
  CHECK(!systemRetained(s, 0), "fresh retains nothing");
  CHECK(systemPossess(s, 0) == 0, "no bitmap for unretained");
  CHECK(systemWant(s, 0) == 0, "no want for unretained");
  CHECK(systemFrontier(0) == 0 && !systemLive(0) && !systemOwed(0),
   "null-state queries");
  CHECK(systemDuty(0) == SYSTEM_DUTY_MET, "null-state duty met");
  CHECK(systemReceived(0, 0, 0, 0, out) == 0, "null state received");
  CHECK(systemReceived(s, 0, TN + 1, 0, out) == 0, "from out of range");
  CHECK(systemPossessed(s, 0, TN + 1, out) == 0, "possess out of range");
  CHECK(systemEvict(s, out) == 0, "evict with nothing retained");

  /* ---------------------------------------------------------------- */
  BANNER("B: PARTICIPATE (R2a rule)");
  /* ---------------------------------------------------------------- */

  /* existence evidence for the frontier round, no live instance:
   * no act, but participation owed is recorded */
  n = systemReceived(s, 0, 2, 0, out);
  CHECK(n == 0, "frontier evidence: no act");
  CHECK(systemOwed(s), "frontier evidence records owed");

  /* evidence beyond chain reach is inert and does NOT record owed */
  systemInit(s, TN, TT, 3, TSELF);
  CHECK(systemReceived(s, 5, 2, 0, out) == 0, "beyond reach: no act");
  CHECK(!systemOwed(s), "beyond reach: no owed");

  /* owed discharges as JOIN at the next launch opportunity, even
   * with no value pending and backlog undrained (owed work is not
   * capacity-gated; genesis duty is met, so the advance rule is
   * satisfied without tolerance) */
  systemReceived(s, 0, 2, 0, out);
  n = systemLaunch(s, 0, 0, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN && out[0].round == 0,
   "owed discharges as JOIN of the frontier round");
  CHECK(systemLive(s), "JOIN marks live");
  CHECK(!systemOwed(s), "JOIN clears owed");

  /* live instance: traffic for the frontier round DELIVERs */
  n = systemReceived(s, 0, 1, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_DELIVER && out[0].round == 0,
   "live round traffic delivers");

  /* own COMPLETE retires participation, retains, advances */
  n = systemComplete(s, systemFrontier(s), 0, out);
  CHECK(n == 0, "first complete releases nothing");
  CHECK(systemFrontier(s) == 1, "frontier advanced");
  CHECK(!systemLive(s), "complete clears live");
  CHECK(systemRetained(s, 0), "round 0 retained");
  CHECK(systemPossess(s, 0) && SYSTEM_TST(systemPossess(s, 0), TSELF),
   "possession starts at self");
  CHECK(systemComplete(s, systemFrontier(s), 0, out) == 0,
   "complete without live is inert");

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
  systemPossessed(s, 0, 1, out);
  systemPossessed(s, 0, 2, out);
  systemPossessed(s, 0, 3, out);
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "4 of 5: still held");
  systemPossessed(s, 0, 4, out);
  CHECK(systemDuty(s) == SYSTEM_DUTY_TOLERANCE, "n-t reached: tolerance");
  CHECK(systemLaunch(s, 1, 0, 1, 0, out) == 0,
   "tolerance not elapsed: hold");
  n = systemLaunch(s, 1, 0, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && out[0].round == 1,
   "tolerance elapsed: ADMIT");
  CHECK(systemLive(s), "ADMIT marks live");
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "live: no second launch");
  /* the close speaks its round: a superseded instance's stale
   * result cannot be booked as a later frontier's completion */
  CHECK(systemComplete(s, 0, 0, out) == 0, "stale round refused");
  CHECK(systemLive(s), "refused close leaves the instance live");
  systemComplete(s, systemFrontier(s), 0, out);

  /* all-n possession releases the prior round; duty reads MET and
   * the frontier advances without tolerance */
  systemPossessed(s, 1, 1, out);
  systemPossessed(s, 1, 2, out);
  systemPossessed(s, 1, 3, out);
  systemPossessed(s, 1, 4, out);
  systemPossessed(s, 1, 5, out);
  n = systemPossessed(s, 1, 6, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && out[0].round == 1,
   "all-n possession releases");
  CHECK(systemDuty(s) == SYSTEM_DUTY_MET, "released prior: duty met");
  n = systemLaunch(s, 1, 0, 1, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && out[0].round == 2,
   "all-n advance needs no tolerance");
  systemComplete(s, systemFrontier(s), 0, out);

  /* joins are gated exactly as admissions (an early launcher must
   * gain nothing), and owed work outranks chosen work */
  systemReceived(s, 3, 5, 0, out);
  CHECK(systemOwed(s), "evidence for frontier 3 records owed");
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "prior round 2: held");
  CHECK(systemLaunch(s, 1, 0, 1, 1, out) == 0, "join held with the frontier");
  systemPossessed(s, 2, 1, out);
  systemPossessed(s, 2, 2, out);
  systemPossessed(s, 2, 3, out);
  systemPossessed(s, 2, 4, out);
  CHECK(systemDuty(s) == SYSTEM_DUTY_TOLERANCE, "prior at n-t");
  CHECK(systemLaunch(s, 1, 0, 1, 0, out) == 0, "join waits for tolerance");
  /* the full O5 precedence chain in one call: owed outranks
   * maintenance outranks the value */
  n = systemLaunch(s, 1, 1, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN && out[0].round == 3,
   "owed work before maintenance before chosen work");
  systemComplete(s, systemFrontier(s), 0, out);

  /* ---------------------------------------------------------------- */
  BANNER("D: SERVE and possession");
  /* ---------------------------------------------------------------- */

  /* want evidence on a retained round from a process not possessing
   * it births a serve carrying that process */
  n = systemReceived(s, 3, 5, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && out[0].round == 3,
   "want evidence births serve");
  CHECK(out[0].want && SYSTEM_TST(out[0].want, 5), "sender in .want");
  CHECK(!SYSTEM_TST(out[0].want, 6), "only the sender in .want");

  /* re-arrival re-outputs the serve (retry posture) */
  n = systemReceived(s, 3, 5, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE, "re-arrival re-serves");

  /* a second wanting process joins the owed set */
  systemReceived(s, 3, 6, 0, out);
  CHECK(SYSTEM_TST(systemWant(s, 3), 5) && SYSTEM_TST(systemWant(s, 3), 6),
   "both wanting processes owed");

  /* a sender already recorded as possessing births nothing */
  CHECK(systemReceived(s, 2, 1, 0, out) == 0, "possessing sender: no serve");

  /* an indication RIDING traffic records and births no serve */
  n = systemReceived(s, 2, 5, 1, out);
  CHECK(n == 0, "riding possession births no serve");
  CHECK(SYSTEM_TST(systemPossess(s, 2), 5), "riding indication recorded");

  /* possession recording is idempotent on a retained round */
  CHECK(systemPossessed(s, 2, 5, out) == 0, "repeat possession: no act");
  CHECK(systemRetained(s, 2), "repeat possession: still retained");

  /* the walk round-robins two simultaneously-owed rounds */
  systemReceived(s, 2, 6, 0, out);
  cursor = 0;
  n = systemServe(s, &cursor, out);
  r = out[0].round;
  CHECK(n == 1 && (r == 2 || r == 3), "walk serves an owed round");
  n = systemServe(s, &cursor, out);
  CHECK(n == 1 && out[0].round != r
   && (out[0].round == 2 || out[0].round == 3),
   "walk advances to the other owed round");
  CHECK(systemServe(s, &cursor, out) == 1, "walk cycles while owed");

  /* possession retires per-process; a forged indication from
   * another process strands nobody */
  systemPossessed(s, 3, 5, out);
  CHECK(!SYSTEM_TST(systemWant(s, 3), 5), "possession retires per-process");
  CHECK(SYSTEM_TST(systemWant(s, 3), 6), "other process still owed");
  systemPossessed(s, 3, 2, out);
  CHECK(SYSTEM_TST(systemWant(s, 3), 6), "forged possession strands nobody");

  /* ---------------------------------------------------------------- */
  BANNER("E: RETAIN / RELEASE");
  /* ---------------------------------------------------------------- */

  /* round 2 sits at 6 of 7 possessions: below all-n must not
   * release (no count shortcut); the 7th arrives riding traffic,
   * so the release fires from the received path */
  CHECK(systemRetained(s, 2), "below all-n does not release");
  n = systemReceived(s, 2, 6, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && out[0].round == 2,
   "all-n possession releases (received path)");
  CHECK(!systemRetained(s, 2), "released round not retained");
  CHECK(systemPossessed(s, 2, 6, out) == 0, "possession on released: inert");

  /* window-full eviction: w = 3 -> 4 retained rounds max.  Rounds
   * 0, 3 retained; complete rounds 4, 5 -> 4 retained; completing
   * round 6 evicts the oldest (round 0). */
  CHECK(roundTrip(s, out) == 0, "complete round 4");
  CHECK(roundTrip(s, out) == 0, "complete round 5");
  n = roundTrip(s, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && out[0].round == 0,
   "window-full eviction releases the oldest");
  CHECK(systemRetained(s, 3) && systemRetained(s, 4)
   && systemRetained(s, 5) && systemRetained(s, 6),
   "newer rounds survive eviction");

  /* budget eviction: oldest-first until nothing is retained */
  n = systemEvict(s, out);
  CHECK(n == 1 && out[0].round == 3, "evict oldest first");
  n = systemEvict(s, out);
  CHECK(n == 1 && out[0].round == 4, "evict next oldest");
  systemEvict(s, out);
  systemEvict(s, out);
  CHECK(systemEvict(s, out) == 0, "evict with nothing retained");
  cursor = 0;
  CHECK(systemServe(s, &cursor, out) == 0,
   "walk 0-return with nothing retained");

  /* traffic for a released round is inert (out-of-band territory) */
  CHECK(systemReceived(s, 4, 1, 0, out) == 0, "released round inert");

  /* ---------------------------------------------------------------- */
  BANNER("F: round-space wrap");
  /* ---------------------------------------------------------------- */

  free(s);
  if (!(s = malloc(systemSz(TN, 255)))) {
    fprintf(stderr, "malloc failed\n");
    return (1);
  }
  systemInit(s, TN, TT, 255, TSELF);

  /* run the frontier through the full round space and past the
   * wrap; grease stops at n-t, so releases come only from the wrap
   * boundary (an entry must release before its round byte recurs --
   * retention tops out at 255 rounds) */
  for (k = 0; k < 300; ++k) {
    r = systemFrontier(s);
    n = roundTrip(s, out);
    if (k < 255) {
      if (n != 0) {
        CHECK(0, "no release before the window fills");
        break;
      }
    } else {
      if (n != 1 || out[0].act != SYSTEM_ACT_RELEASE
       || out[0].round != (unsigned char)(r + 1)) {
        CHECK(0, "wrap boundary releases the recurring round byte");
        break;
      }
    }
  }
  CHECK(k == 300, "300 rounds completed across the wrap");
  CHECK(systemFrontier(s) == (unsigned char)300, "frontier wrapped");

  /* classification is intact across the wrap: the round just
   * completed serves, a reused-looking byte 40 ahead is the OLD
   * round 216 behind and serving it is correct, the frontier round
   * records owed */
  r = (unsigned char)(systemFrontier(s) - 1);
  n = systemReceived(s, r, 5, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && out[0].round == r,
   "retained round serves across wrap");
  n = systemReceived(s, (unsigned char)(systemFrontier(s) + 40), 5, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE,
   "full window: old round at a reused-looking byte still serves");
  systemReceived(s, systemFrontier(s), 5, 0, out);
  CHECK(systemOwed(s), "frontier evidence records owed across wrap");

  /* small window across the wrap: eviction keeps only the last 4
   * rounds, so behind-the-window bytes classify released (inert)
   * and far-ahead bytes fold into beyond-reach (inert) */
  systemInit(s, TN, TT, 3, TSELF);
  for (k = 0; k < 300; ++k) {
    n = roundTrip(s, out);
    if (k >= 4 && (n != 1 || out[0].act != SYSTEM_ACT_RELEASE)) {
      CHECK(0, "full small window evicts each round");
      break;
    }
  }
  CHECK(k == 300, "300 small-window rounds across the wrap");
  CHECK(systemReceived(s, (unsigned char)(systemFrontier(s) - 24), 5, 0, out)
   == 0, "wrap-released round inert");
  CHECK(systemReceived(s, (unsigned char)(systemFrontier(s) + 116), 5, 0, out)
   == 0, "beyond reach inert across wrap");
  CHECK(systemReceived(s, (unsigned char)(systemFrontier(s) + 127), 5, 0, out)
   == 0, "ahead boundary byte (+127) inert");
  CHECK(systemReceived(s, (unsigned char)(systemFrontier(s) + 128), 5, 0, out)
   == 0, "behind boundary byte (+128) inert");
  r = (unsigned char)(systemFrontier(s) - 1);
  n = systemReceived(s, r, 5, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE && out[0].round == r,
   "small window: retained round serves across wrap");

  /* a lingering entry in a small window: siblings release via all-n
   * each round, so the window never fills and round 0 survives until
   * the wrap boundary must release it (255 rounds behind) */
  systemInit(s, TN, TT, 7, TSELF);
  CHECK(roundTrip(s, out) == 0, "lingering: round 0 retained");
  for (k = 1; k < 255; ++k) {
    if (roundTrip(s, out) != 0) {
      CHECK(0, "lingering: no eviction while siblings release");
      break;
    }
    for (r = 1; r <= TN; ++r)
      n = systemPossessed(s, (unsigned char)k, r, out);
    if (n != 1 || out[0].act != SYSTEM_ACT_RELEASE) {
      CHECK(0, "lingering: sibling all-n release");
      break;
    }
  }
  CHECK(k == 255, "lingering: 254 sibling rounds released");
  n = roundTrip(s, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE && out[0].round == 0,
   "wrap releases the 255-behind lingerer in a small window");

  free(s);

  /* ---------------------------------------------------------------- */
  BANNER("G: large-n bitmaps (bs > 1)");
  /* ---------------------------------------------------------------- */

  /* 201 processes (n = 200 encoded), t = 66 (n-t = 135) -> 26-byte
   * bitmaps; self past the first byte */
  if (!(s = malloc(systemSz(200, 3)))) {
    fprintf(stderr, "malloc failed\n");
    return (1);
  }
  systemInit(s, 200, 66, 3, 60);
  CHECK(roundTrip(s, out) == 0, "large-n round 0 completes");
  CHECK(SYSTEM_TST(systemPossess(s, 0), 60), "self bit past byte 0");
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "1 of 135: held");

  /* want bits at byte offsets 1 and 25 of the mask */
  n = systemReceived(s, 0, 9, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE
   && SYSTEM_TST(out[0].want, 9), "want bit in byte 1");
  n = systemReceived(s, 0, 200, 0, out);
  CHECK(n == 1 && SYSTEM_TST(out[0].want, 9)
   && SYSTEM_TST(out[0].want, 200)
   && !SYSTEM_TST(out[0].want, 199), "want bits span the mask");
  cursor = 0;
  CHECK(systemServe(s, &cursor, out) == 1, "large-n walk serves");

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
    systemComplete(s, systemFrontier(s), hv, out);
  }
  CHECK(systemHave(s, 1) && SYSTEM_TST(systemHave(s, 1), 9)
   && SYSTEM_TST(systemHave(s, 1), 200)
   && !SYSTEM_TST(systemHave(s, 1), 199),
   "large-n have copy spans the mask");

  /* all-n release fires at the 201st distinct possession and never
   * before (no count shortcut at any byte boundary) */
  rel = 0;
  for (k = 0; k <= 200; ++k) {
    if (k == 60)
      continue;
    n = systemPossessed(s, 0, (unsigned char)k, out);
    if (n) {
      CHECK(n == 1 && out[0].act == SYSTEM_ACT_RELEASE
       && out[0].round == 0, "large-n release is the release act");
      CHECK(k == 200, "release only at all-n");
      ++rel;
    }
  }
  CHECK(rel == 1, "exactly one release across 200 recordings");
  CHECK(!systemRetained(s, 0), "large-n round released");
  free(s);

  /* ---------------------------------------------------------------- */
  BANNER("H: O2 / O3 / O5");
  /* ---------------------------------------------------------------- */

  if (!(s = malloc(systemSz(TN, 3)))) {
    fprintf(stderr, "malloc failed\n");
    return (1);
  }
  systemInit(s, TN, TT, 3, TSELF);

  /* O5: maintenance outranks the value and never consumes it */
  n = systemLaunch(s, 1, 1, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_MAINTAIN && out[0].round == 0,
   "maintenance outranks the pending value");
  systemComplete(s, systemFrontier(s), 0, out);

  /* maintenance is chosen work: capacity- and advance-gated */
  CHECK(systemLaunch(s, 0, 1, 0, 1, out) == 0, "maintain: backlog gate");
  CHECK(systemDuty(s) == SYSTEM_DUTY_HELD, "prior at self only");
  CHECK(systemLaunch(s, 0, 1, 1, 1, out) == 0, "maintain held by advance");
  grease(s, out);
  CHECK(systemLaunch(s, 0, 1, 1, 0, out) == 0, "maintain waits tolerance");
  n = systemLaunch(s, 0, 1, 1, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_MAINTAIN && out[0].round == 1,
   "maintenance without a value");

  /* O2: held members at completion, late assembly, riding on SERVE */
  {
    unsigned char hv[(TN + 8) / 8];

    memset(hv, 0, sizeof (hv));
    BIT_SET(hv, TSELF);
    BIT_SET(hv, 3);
    systemComplete(s, systemFrontier(s), hv, out);
  }
  r = (unsigned char)(systemFrontier(s) - 1); /* round 1 */
  CHECK(systemHave(s, r) && SYSTEM_TST(systemHave(s, r), TSELF)
   && SYSTEM_TST(systemHave(s, r), 3) && !SYSTEM_TST(systemHave(s, r), 5),
   "held members recorded at completion");
  systemAssembled(s, r, 5);
  CHECK(SYSTEM_TST(systemHave(s, r), 5), "late assembly recorded");
  systemAssembled(s, r, TN + 1);        /* out of range: ignored */
  systemAssembled(s, (unsigned char)(r + 100), 2); /* unretained: ignored */
  n = systemReceived(s, r, 6, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_SERVE
   && out[0].want && SYSTEM_TST(out[0].want, 6)
   && out[0].have && SYSTEM_TST(out[0].have, 3)
   && SYSTEM_TST(out[0].have, 5) && !SYSTEM_TST(out[0].have, 4),
   "serve carries the held-members grain");
  systemPossessed(s, r, 6, out);
  cursor = 0;
  CHECK(systemServe(s, &cursor, out) == 0,
   "walk 0-return at quiescence (rounds retained, nothing owed)");

  /* O3: the witness book on a fresh (behind) instance */
  systemInit(s, TN, TT, 3, TSELF);
  systemReceived(s, 0, 2, 0, out);
  CHECK(systemOwed(s), "recovery traffic records owed");
  CHECK(systemWitness(s, 0, 2, out) == 0, "1 of t+1: accumulate");
  CHECK(systemWitness(s, 0, 2, out) == 0, "witness idempotent per server");
  CHECK(systemWitness(s, 0, 3, out) == 0,
   "2 of t+1: accumulate (= t distinct forgers cannot adopt)");
  CHECK(systemWitness(s, 0, TSELF, out) == 0, "self is not a server");
  CHECK(systemWitness(s, 0, TN + 1, out) == 0, "witness from out of range");
  CHECK(systemWitness(s, 1, 4, out) == 0, "wrong round ignored");
  n = systemWitness(s, 0, 4, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADOPT && out[0].round == 0,
   "t+1 distinct servers: adopt");
  CHECK(systemWitness(s, 0, 5, out) == 0, "adopt fires exactly once");
  CHECK(systemWitnesses(s) && SYSTEM_TST(systemWitnesses(s), 4)
   && SYSTEM_TST(systemWitnesses(s), 5), "witness bits recorded");

  /* candidate switch: reset re-arms and the true candidate
   * re-accumulates */
  systemWitnessReset(s);
  CHECK(systemWitnesses(s) && !SYSTEM_TST(systemWitnesses(s), 2),
   "reset clears the book");
  systemWitness(s, 0, 2, out);
  systemWitness(s, 0, 3, out);
  n = systemWitness(s, 0, 4, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADOPT, "reset re-arms adopt");

  /* adoption closes through the one consume region: JOIN (the
   * recovery traffic recorded owed), then systemComplete -- which
   * clears the book and re-arms for the next frontier */
  n = systemLaunch(s, 0, 0, 0, 0, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN, "adoption closes via JOIN");
  systemComplete(s, systemFrontier(s), 0, out);
  CHECK(systemFrontier(s) == 1, "adoption advanced the frontier");
  CHECK(systemWitnesses(s) && !SYSTEM_TST(systemWitnesses(s), 2),
   "completion clears the book");

  /* a BELOW-threshold book is superseded by own COMPLETE: the
   * partial accumulation clears and the latch stays clean */
  CHECK(systemWitness(s, 1, 5, out) == 0, "partial book: 1 of t+1");
  systemReceived(s, 1, 5, 0, out);      /* owed for frontier 1 */
  grease(s, out);                       /* round 0 to n-t */
  n = systemLaunch(s, 0, 0, 0, 1, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN, "join for frontier 1");
  systemComplete(s, systemFrontier(s), 0, out);
  CHECK(systemFrontier(s) == 2, "frontier 2");
  CHECK(systemWitnesses(s) && !SYSTEM_TST(systemWitnesses(s), 5),
   "own COMPLETE supersedes a partial book");
  systemWitness(s, 2, 2, out);
  systemWitness(s, 2, 3, out);
  n = systemWitness(s, 2, 4, out);
  CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADOPT && out[0].round == 2,
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
    unsigned int bs;

    bs = ((TN + 1) + 7) / 8;

    systemInit(s, TN, TT, 3, TSELF);
    /* one wire, two effects: it records participation owed for the
     * frontier, and its indication is dropped for want of a record */
    CHECK(systemReceived(s, 0, 3, 1, out) == 0,
     "premature indication takes no act");
    CHECK(systemOwed(s), "the same wire recorded participation owed");
    CHECK(!systemRetained(s, 0), "the frontier round has no record yet");
    n = systemLaunch(s, 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_JOIN, "owed discharges as JOIN");
    systemComplete(s, 0, 0, out);
    CHECK(systemRetained(s, 0), "round 0 retained by the close");
    CHECK(systemPossess(s, 0) && !SYSTEM_TST(systemPossess(s, 0), 3),
     "the premature indication was dropped, not banked");

    /* the caller re-presents what it held */
    systemPossessed(s, 0, 3, out);
    CHECK(SYSTEM_TST(systemPossess(s, 0), 3),
     "re-presented indication is banked");

    /* THE EQUIVALENCE.  s2 differs in one respect only: its
     * indication arrives after the round is retained, riding traffic
     * the way system.h's primary source does.  Both instances take
     * the same existence evidence first, so the launch is a JOIN in
     * both and only the indication's TIMING varies. */
    if ((s2 = calloc(1, systemSz(TN, 3)))) {
      systemInit(s2, TN, TT, 3, TSELF);
      systemReceived(s2, 0, 3, 0, out);
      systemLaunch(s2, 1, 0, 1, 1, out);
      systemComplete(s2, 0, 0, out);
      systemReceived(s2, 0, 3, 1, out);
      /* non-vacuity: the comparison below would pass on two EMPTY
       * records, so pin that the timely indication really was banked */
      CHECK(SYSTEM_TST(systemPossess(s2, 0), 3),
       "the timely indication was banked");
      CHECK(!memcmp(systemPossess(s, 0), systemPossess(s2, 0), bs),
       "re-presented possession record is byte-identical to a timely one");
      CHECK(!memcmp(systemWant(s, 0), systemWant(s2, 0), bs),
       "re-presented want record is byte-identical to a timely one");
      CHECK(systemDuty(s) == systemDuty(s2),
       "re-presented duty class is identical to a timely one");
      free(s2);
    }
  }

  free(s);

  /* ---------------------------------------------------------------- */
  BANNER("J: L3 commutation");
  /* ---------------------------------------------------------------- */

  /* system.md L3.  Order A is the adoption arm: k witness recordings,
   * then the close.  Order B is the late-witness arm: the close, then
   * the SAME k recordings -- which now name a round that is no longer
   * the frontier.  Both replays start from a byte-identical copy of
   * the whole caller allocation (zeroed before systemInit, so padding
   * is deterministic), and the two final allocations are compared
   * whole.  ops[] entries below 0 are systemWitnessReset. */
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
      { ops3,  "J: k=t+1 over a full window",   3, 1, 1, 1, 6 },
      { opsd1, "J: t=0, one witness is t+1",    1, 1, 0, 2, 1 },
      { opsd2, "J: t=0, idempotent server",     2, 1, 0, 2, 1 },
      { opsd3, "J: t=0, reset re-arms",         3, 2, 0, 2, 1 }
    };
    struct system *jbase[3];
    struct system *ja;
    struct system *jb;
    struct system *jsnap;
    struct systemAct ob[SYSTEM_MAX_ACTS];
    unsigned char actA[SYSTEM_MAX_ACTS];
    unsigned char rndA[SYSTEM_MAX_ACTS];
    unsigned long jsz;
    unsigned int c;
    unsigned int i;
    unsigned int j;
    unsigned int wa;
    unsigned int wb;
    unsigned int ca;
    unsigned int cb;
    unsigned char closeR;

    /* one allocation size covers every pre-state, so a replay copies
     * and compares the same span whatever the configuration */
    jsz = systemSz(TN, 3);
    if (systemSz(1, 3) > jsz)
      jsz = systemSz(1, 3);
    if (!(jbase[0] = calloc(1, jsz)) || !(jbase[1] = calloc(1, jsz))
     || !(jbase[2] = calloc(1, jsz)) || !(ja = calloc(1, jsz))
     || !(jb = calloc(1, jsz)) || !(jsnap = calloc(1, jsz))) {
      fprintf(stderr, "malloc failed\n");
      return (1);
    }

    /* pre-state 0: fresh, frontier 0 live */
    systemInit(jbase[0], TN, TT, 3, TSELF);
    n = systemLaunch(jbase[0], 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT,
     "pre-state 0: frontier 0 live");

    /* pre-state 1: the window full (w = 3 -> 4 entries), so the close
     * itself must output a RELEASE in both orders */
    systemInit(jbase[1], TN, TT, 3, TSELF);
    for (j = 0; j < 4; ++j)
      if (roundTrip(jbase[1], out) != 0) {
        CHECK(0, "pre-state 1: four rounds retained");
        break;
      }
    grease(jbase[1], out);
    n = systemLaunch(jbase[1], 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && out[0].round == 4,
     "pre-state 1: frontier 4 live over a full window");

    /* pre-state 2: the degenerate arm -- 2 processes, t = 0, where a
     * single witness IS the t+1 threshold */
    systemInit(jbase[2], 1, 0, 3, 0);
    n = systemLaunch(jbase[2], 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT,
     "pre-state 2: t = 0 frontier 0 live");

    for (c = 0; c < sizeof (comm) / sizeof (comm[0]); ++c) {
      BANNER(comm[c].name);

      memcpy(ja, jbase[comm[c].base], jsz);
      memcpy(jb, jbase[comm[c].base], jsz);
      closeR = systemFrontier(ja);

      /* order A: the recordings, then the close */
      wa = 0;
      for (j = 0; j < comm[c].nops; ++j) {
        if (comm[c].ops[j] < 0) {
          systemWitnessReset(ja);
          continue;
        }
        n = systemWitness(ja, closeR, comm[c].ops[j], out);
        for (i = 0; i < n; ++i) {
          CHECK(out[i].act == SYSTEM_ACT_ADOPT && out[i].round == closeR,
           "order A: a witness act is ADOPT of the frontier round");
          ++wa;
        }
      }
      ca = systemComplete(ja, closeR, 0, out);
      for (i = 0; i < ca && i < SYSTEM_MAX_ACTS; ++i) {
        actA[i] = out[i].act;
        rndA[i] = out[i].round;
      }

      /* order B: the close, then the same recordings -- late */
      cb = systemComplete(jb, closeR, 0, ob);
      wb = 0;
      for (j = 0; j < comm[c].nops; ++j) {
        if (comm[c].ops[j] < 0) {
          systemWitnessReset(jb);
          continue;
        }
        wb += systemWitness(jb, closeR, comm[c].ops[j], out);
      }

      CHECK(wa == comm[c].adopts, "order A output the expected ADOPTs");
      CHECK(wb == 0, "order B: late recordings are refused whole");
      CHECK(ca == comm[c].closeActs, "the close output the expected acts");
      CHECK(cb == ca, "both orders' closes output the same count");
      for (i = 0; i < ca && i < cb && i < SYSTEM_MAX_ACTS; ++i)
        CHECK(actA[i] == ob[i].act && rndA[i] == ob[i].round,
         "close act kind and round agree across the orders");
      if (comm[c].closeActs)
        CHECK(actA[0] == SYSTEM_ACT_RELEASE && rndA[0] == 0,
         "the full-window close releases the oldest (non-vacuity)");
      CHECK(!memcmp(ja, jb, jsz),
       "both orders reach the byte-identical machine state");

      /* the late-witness arm stated directly: a recording naming the
       * completed round is refused whole and changes nothing */
      memcpy(jsnap, ja, jsz);
      CHECK(systemWitness(ja, closeR, comm[c].late, out) == 0
       && !memcmp(ja, jsnap, jsz),
       "a witness after the close changes nothing");

      /* the region consumes once per round, first arm: before any
       * later launch the second close finds no live instance */
      memcpy(jsnap, ja, jsz);
      CHECK(systemComplete(ja, closeR, 0, out) == 0
       && !memcmp(ja, jsnap, jsz), "second close of the round is inert");
      memcpy(jsnap, jb, jsz);
      CHECK(systemComplete(jb, closeR, 0, out) == 0
       && !memcmp(jb, jsnap, jsz),
       "second close of the round is inert in either order");
    }

    /* the corollary's second arm: after a later launch the same round
     * is no longer the frontier, so the close is refused on the round
     * binding rather than on liveness */
    BANNER("J: second close after a relaunch");
    memcpy(ja, jbase[0], jsz);
    systemComplete(ja, 0, 0, out);
    grease(ja, out);
    n = systemLaunch(ja, 1, 0, 1, 1, out);
    CHECK(n == 1 && out[0].act == SYSTEM_ACT_ADMIT && out[0].round == 1,
     "relaunch at the next frontier");
    memcpy(jsnap, ja, jsz);
    CHECK(systemComplete(ja, 0, 0, out) == 0 && !memcmp(ja, jsnap, jsz),
     "the stale round's close is refused with an instance live");

    free(jsnap);
    free(jb);
    free(ja);
    free(jbase[2]);
    free(jbase[1]);
    free(jbase[0]);
  }

  printf("test_system: %d checks, %d failures\n", Checks, Failures);
  if (Failures) {
    printf("FAILED\n");
    return (1);
  }
  printf("PASSED\n");
  return (0);
}
