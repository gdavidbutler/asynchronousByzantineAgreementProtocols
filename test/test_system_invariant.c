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
 * Method: breadth-first enumeration over the reachable states of one
 * instance.  A state is the entire caller-allocated buffer plus the
 * serve cursor, so state identity is exact and no abstraction is
 * assumed.  Every state is expanded by every action in the alphabet.
 *
 * Round arguments are drawn PER STATE, from the rounds that state can
 * actually be asked about: every currently retained round, plus the
 * frontier and its immediate neighbors.  A fixed offset window does
 * NOT suffice -- sibling rounds releasing at all-n keep the window
 * unfull, so a retained entry drifts arbitrarily far behind the
 * frontier and becomes unaddressable, taking its transitions and the
 * states beyond them out of the enumeration.  Rounds are listed by
 * ascending distance behind the frontier, which is stable across the
 * round-byte wrap.
 *
 * The frontier is bounded by a horizon measured as wrapping distance
 * from the run's start, so the same rule serves the wrap run with no
 * special case.
 *
 * Two runs: one from a fresh instance, one from a frontier driven to
 * just below the 255 -> 0 crossing.  Run 1 exercises round-byte
 * ARITHMETIC across the wrap; it does NOT reach the frontier+1
 * lookahead release, which needs an entry 255 behind the completing
 * frontier and is unreachable at any feasible horizon -- that guard's
 * coverage lives in the contract suite (test_system.c) and in the
 * hand discharge of I1.  The two
 * runs' canonical state sets are compared in-program, so the
 * translation-invariance claim is asserted rather than eyeballed.
 *
 * Derived only from the documented contract in system.h and the
 * invariant statement in system.md.  The public struct fields (flags,
 * frontier) and the documented queries are the only state read; no
 * part of this file inspects system.c or the data[] layout.
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
 * cursor at BFS path "45 56 2 99".  The pairing that gives that red
 * its meaning ran in the same session: the UNMUTATED machine under
 * the same twin drive, 621,094 states and 43,711,360 transitions per
 * run with 29,742,272 differing-grain observations and ZERO
 * divergence.  The PLAIN build of this file, on the same mutated
 * machine, runs CLEAN (310,579 states -- half the frozen count, since
 * the gate suppresses every serve -- "invariant held on all reachable
 * states"): H_r is outside its alphabet, which is precisely why this
 * arm exists.  A clean HRTWIN run was an UNFALSIFIED WITNESS for L7's
 * second half until then; it is now a validated check.
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
 * All at the frozen config, all red in under three seconds:
 *
 *   MM_I5_LATCH_AT_T     adopt guard t+1 -> t.  I5, path "82".
 *                        Also test_system H x4, J x2.
 *   MM_I5_COUNT_SELF     the from == self refusal dropped.  I5, path
 *                        "81 82".  Also test_system H x2.
 *   MM_I6_BOOK_SURVIVES  completion no longer clears the book.  I6,
 *                        path "45 82 56".  Also test_system H x4 and
 *                        ALL TEN of J's commutation scenarios -- the
 *                        surviving book is exactly the state
 *                        difference the two orders must not have.
 *   MM_I8_EARLY          all-n read one possession early.  I8, path
 *                        "45 56 3 5".  Also test_system C, D.
 *   MM_I9_SLOT_NOFREE    the completion eviction reuses its slot with
 *                        no RELEASE act.  I9, the retained-set shadow
 *                        -- a departure nothing announced -- path
 *                        "45 56 3 5 53 56 3 5 53 56".  Also
 *                        test_system E, F, J.
 *   MM_I10_RETAIN_WRONG  the born entry takes frontier - 1.  I10,
 *                        path "45 56".  Also test_system B, C, D.
 *   MM_I11_EVICT_NEWEST  both bound eviction sites take the newest.
 *                        I11, path "45 56 3 5 53 56 98".  Also
 *                        test_system E x3 and J's full-window
 *                        non-vacuity arm.
 *   MM_HR_GATES          the -DHRTWIN red; see MATCHED RED above.
 *   MM_WRAP_LOOKAHEAD_SKIP
 *                        the frontier+1 lookahead release removed.
 *                        DORMANT HERE at every config tried -- the
 *                        frozen one, HORIZON=9, and EN=6 ET=2 EW=1
 *                        HORIZON=2 -- and dormant for a STRUCTURAL
 *                        reason, not for want of searching: an entry
 *                        is born at the frontier and the horizon
 *                        bounds the frontier's travel from the run's
 *                        start, so no entry can sit more than
 *                        HORIZON - 1 rounds behind, while the guard
 *                        needs 255.  All three runs reach the state
 *                        and transition counts of the clean machine
 *                        EXACTLY (the three configurations listed
 *                        below), so the mutation is INVISIBLE to this
 *                        enumeration, not merely unfired.  Its red is
 *                        test_system section F (five checks, both
 *                        wrap regimes) and nothing else, which is the
 *                        I1 coverage note above made concrete: hand
 *                        proof plus the contract suite carry that
 *                        guard alone.
 *
 * Header encoding convention (CRITICAL):
 *   n parameter is encoded; actual process count = n + 1
 *   w parameter is encoded; actual retained window = w + 1
 *
 * Configurations run clean (override EN/ET/EW/HORIZON/MAXSTATES/
 * TBLBITS on the command line; raise MAXSTATES and TBLBITS together
 * until the state cap is no longer reported, since a truncated
 * search exits non-zero rather than reading as a pass):
 *
 *   EN=3 ET=1 EW=1 HORIZON=6     621,094 states /  43.7M transitions
 *   EN=3 ET=1 EW=1 HORIZON=9   2,002,192 states / 140.7M transitions
 *   EN=6 ET=2 EW=1 HORIZON=2     475,538 states /  49.4M transitions
 *   EN=6 ET=2 EW=0 HORIZON=6   1,188,993 states / 123.7M transitions
 *
 * At t=2 the space explodes past horizon 2: HORIZON=3 exceeds
 * 24,000,000 states, and HORIZON 4/5/6 exhaust the same cap at
 * nearly identical transition counts -- the cap is reached before the
 * horizon binds, so raising the horizon alone buys nothing.  The
 * reachable count there is order 1e9-1e10 (per retained round, self
 * is fixed in possession and each other process is possessing /
 * wanting / neither, 3^6 per round, squared over two window slots,
 * times the witness record), so no cap that fits in memory completes
 * it.  Exhaustive t=2 needs a smaller window (EW=0) or a symmetry
 * reduction over the non-self processes, which no rule distinguishes.
 *
 * Style: C89, K&R, 2-space indent, single monolithic main().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system.h"

/* n = 3 encoded -> 4 processes, t = 1 (n-t = 3), window 2, self = 0 */
#ifndef EN
#define EN 3
#endif
#ifndef ET
#define ET 1
#endif
#ifndef EW
#define EW 1
#endif
#define ESELF 0
#define NP (EN + 1)
#define NW (EW + 1)
#define BS ((NP + 7) / 8)

/*
 * Round slots per state: every retained round, plus F-1, F, F+1.
 * Slots past the state's actual round count are skipped.
 */
#define RMAX (NW + 3)

/*
 * Action alphabet, with R = RMAX round slots and P = NP senders:
 *
 *   [0, A_RECV)       systemReceived   (R x P x 2 possesses)
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
#define A_RECV (2 * RMAX * NP)
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
#define DESCLEN (2 + BS + NW * (1 + 3 * BS))

#define BIT_SET(map, p) ((map)[(p) >> 3] |= (unsigned char)(1 << ((p) & 7)))
#define BAD(n) do { if (!bad) bad = (n); } while (0)

int
main(
  void
){
  static const char *inv[] = {
    "", "I1 window bound", "I2 possess/want disjoint", "I3 self possesses",
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

  sz = systemSz(EN, EW);
  keyLen = sz + 1;                       /* + serve cursor */
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
    unsigned char startF;

    /* Root state.  The buffer is zeroed first so padding is
     * deterministic and state identity is exact. */
    memset(root, 0, slotLen);
    s = (struct system *)root;
    systemInit(s, EN, ET, EW, ESELF);
#ifdef HRTWIN
    sB = (struct system *)(root + keyLen);
    systemInit(sB, EN, ET, EW, ESELF);
#endif
    if (!systemWitnesses(s)) {
      /* the documented rejected-instance probe: a rejected instance
       * is inert, so enumerating it asserts nothing */
      fprintf(stderr, "configuration rejected by systemInit\n");
      return (2);
    }

    if (run) {
      /* Drive the frontier to just below the wrap crossing.  Each
       * step: admit, complete, then feed every process's possession
       * of the round just retained -- reaching all n releases it, so
       * the window stays empty and the next duty class reads MET. */
      while (systemFrontier(s) != 252) {
        unsigned char prior;

        if (!systemLaunch(s, 1, 0, 1, 1, out)) {
          fprintf(stderr, "preamble: launch refused at %u\n", systemFrontier(s));
          return (2);
        }
        prior = systemFrontier(s);
        systemComplete(s, systemFrontier(s), have, out);
        for (i = 1; i < NP; ++i)
          systemPossessed(s, prior, (unsigned char)i, out);
      }
#ifdef HRTWIN
      /* the same preamble on twin B, under twin B's grain
       * convention -- every round it retains releases at all n, so
       * the two roots differ in no query */
      while (systemFrontier(sB) != 252) {
        unsigned char prior;

        if (!systemLaunch(sB, 1, 0, 1, 1, out)) {
          fprintf(stderr, "preamble: twin B launch refused at %u\n",
                  systemFrontier(sB));
          return (2);
        }
        prior = systemFrontier(sB);
        systemComplete(sB, prior, have, out);
        systemAssembled(sB, prior, (unsigned char)(prior % NP));
        for (i = 1; i < NP; ++i)
          systemPossessed(sB, prior, (unsigned char)i, out);
      }
#endif
    }
    startF = systemFrontier(s);

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
        unsigned int nactsB;
#endif
        const unsigned char *p;
        const unsigned char *q;
        unsigned char *cursor;
        unsigned char roundList[RMAX];
        unsigned char prePoss[BS];
        unsigned char preRet[32];
        unsigned char postRet[32];
        unsigned char relRet[32];
        unsigned char prevF;
        unsigned char rnd;
        unsigned char probeRound;
        unsigned char probeFrom;
        unsigned char probeRecord;
        unsigned char probePath;
        unsigned char probeRelease;
        unsigned char oldestRound;
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
        cursor = work + sz;
#ifdef HRTWIN
        vB = (struct system *)(work + keyLen);
        cursorB = work + keyLen + sz;
        hrWhy = 0;
#endif
        prevF = systemFrontier(v);

        /* Horizon: wrapping distance from this run's start. */
        if ((unsigned char)(prevF - startF) >= HORIZON)
          continue;

        /*
         * Per-state round alphabet: the frontier and its neighbors,
         * then every retained round by ascending distance behind the
         * frontier.  Distance order is wrap-stable; a fixed offset
         * window is not sufficient (see the header note).
         */
        roundList[0] = (unsigned char)(prevF - 1);
        roundList[1] = prevF;
        roundList[2] = (unsigned char)(prevF + 1);
        nRounds = 3;
        for (d = 2; d < 256 && nRounds < RMAX; ++d) {
          rnd = (unsigned char)(prevF - d);
          if (systemRetained(v, rnd))
            roundList[nRounds++] = rnd;
        }

        /* I9 / I10 need the retained set on both sides of the call.
         * I11 needs the oldest of it: the greatest wrapping distance
         * behind the frontier, which the ascending scan leaves last.
         * Distances are distinct because round bytes are, so there is
         * no tie to break. */
        memset(preRet, 0, sizeof (preRet));
        for (r = 0; r < 256; ++r)
          if (systemRetained(v, (unsigned char)r))
            BIT_SET(preRet, r);
        haveOldest = 0;
        oldestRound = 0;
        for (d = 1; d < 256; ++d)
          if (systemRetained(v, (unsigned char)(prevF - d))) {
            oldestRound = (unsigned char)(prevF - d);
            haveOldest = 1;
          }

        /*
         * I8's reconstruction: the possession record as it will stand
         * after this call records its bit, captured before the call
         * consumes it.  Only the possession paths are checked --
         * systemComplete and systemEvict release by eviction or the
         * wrap boundary, which system.md RETAIN permits below all-n.
         */
        probePath = 0;
        probeRelease = 0;
        probeRound = 0;
        probeFrom = 0;
        probeRecord = 0;
        if (a < A_RECV) {
          slot = a / (2 * NP);
          if (slot >= nRounds)
            continue;
          b = a % (2 * NP);
          probeRound = roundList[slot];
          probeFrom = (unsigned char)(b / 2);
          probeRecord = (unsigned char)(b % 2);
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
         && systemRetained(v, probeRound)
         && (p = systemPossess(v, probeRound))) {
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
          nacts = systemReceived(v, probeRound, probeFrom, probeRecord, out);
        } else if (a < A_LAUN) {
          b = a - A_RECV;
          nacts = systemLaunch(v, (unsigned char)(b & 1),
                               (unsigned char)((b >> 1) & 1),
                               (unsigned char)((b >> 2) & 1),
                               (unsigned char)((b >> 3) & 1), out);
        } else if (a < A_COMP) {
          nacts = systemComplete(v, systemFrontier(v), 0, out);
        } else if (a < A_POSS) {
          nacts = systemPossessed(v, probeRound, probeFrom, out);
        } else if (a < A_WITN) {
          b = a - A_POSS;
          nacts = systemWitness(v, roundList[b / NP], (unsigned char)(b % NP),
                                out);
        } else if (a == A_WITN) {
          systemWitnessReset(v);
          nacts = 0;
        } else if (a == A_WITN + 1) {
          nacts = systemEvict(v, out);
        } else
          nacts = systemServe(v, cursor, out);

#ifdef HRTWIN
        /* the same action on twin B, with the grain driven hard:
         * all-n at every close, late assembly after it */
        outB[SYSTEM_MAX_ACTS].act = 0xA5;
        outB[SYSTEM_MAX_ACTS + 1].act = 0x5A;
        if (a < A_RECV) {
          nactsB = systemReceived(vB, probeRound, probeFrom, probeRecord, outB);
        } else if (a < A_LAUN) {
          b = a - A_RECV;
          nactsB = systemLaunch(vB, (unsigned char)(b & 1),
                                (unsigned char)((b >> 1) & 1),
                                (unsigned char)((b >> 2) & 1),
                                (unsigned char)((b >> 3) & 1), outB);
        } else if (a < A_COMP) {
          unsigned char closed;

          closed = systemFrontier(vB);
          nactsB = systemComplete(vB, closed, have, outB);
          systemAssembled(vB, closed, (unsigned char)(head % NP));
        } else if (a < A_POSS) {
          nactsB = systemPossessed(vB, probeRound, probeFrom, outB);
        } else if (a < A_WITN) {
          b = a - A_POSS;
          nactsB = systemWitness(vB, roundList[b / NP], (unsigned char)(b % NP),
                                 outB);
        } else if (a == A_WITN) {
          systemWitnessReset(vB);
          nactsB = 0;
        } else if (a == A_WITN + 1) {
          nactsB = systemEvict(vB, outB);
        } else
          nactsB = systemServe(vB, cursorB, outB);
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
          for (cnt = 0, i = 0; i < nacts; ++i)
            if (out[i].act == SYSTEM_ACT_RELEASE) {
              ++cnt;
              if (out[i].round != probeRound)
                BAD(8);
            }
          if (cnt != probeRelease)
            BAD(8);
        }

        /* I1 / I7 -- window bound, and the frontier is never retained. */
        memset(postRet, 0, sizeof (postRet));
        for (cnt = 0, r = 0; r < 256; ++r)
          if (systemRetained(v, (unsigned char)r)) {
            BIT_SET(postRet, r);
            ++cnt;
          }
        if (cnt > NW)
          BAD(1);
        if (systemRetained(v, systemFrontier(v)))
          BAD(7);

        /*
         * I9 -- every round that left retention is named by a RELEASE
         * act, and every RELEASE names a round that was retained.
         * Stated as those two containments rather than set equality:
         * an eviction whose freed slot is immediately reused for the
         * new frontier round legitimately leaves the byte retained.
         *
         * I10 -- a round ENTERS retention only at completion, and the
         * round it enters as is the pre-advance frontier.  Without
         * this a machine retaining some other byte satisfies every
         * other conjunct while serving and releasing the wrong round.
         */
        memset(relRet, 0, sizeof (relRet));
        for (i = 0; i < nacts; ++i)
          if (out[i].act == SYSTEM_ACT_RELEASE)
            BIT_SET(relRet, out[i].round);
        for (r = 0; r < 256; ++r) {
          unsigned int wasIn = (preRet[r >> 3] >> (r & 7)) & 1;
          unsigned int isIn = (postRet[r >> 3] >> (r & 7)) & 1;
          unsigned int said = (relRet[r >> 3] >> (r & 7)) & 1;

          if ((wasIn && !isIn && !said) || (said && !wasIn))
            BAD(9);
          if (!wasIn && isIn
           && (a < A_LAUN || a >= A_COMP || r != prevF))
            BAD(10);
        }

        /*
         * I11 -- the eviction paths (window-full at completion, the
         * wrap boundary, and systemEvict) release the OLDEST retained
         * round.  Nothing else constrains which round eviction takes;
         * without this a machine evicting the newest satisfies I8-I10
         * while discarding the round most likely to still be wanted.
         * Vacuous at a single-slot window, where the choice is forced.
         */
        if ((a >= A_LAUN && a < A_COMP) || a == A_WITN + 1)
          for (i = 0; i < nacts; ++i)
            if (out[i].act == SYSTEM_ACT_RELEASE
             && (!haveOldest || out[i].round != oldestRound))
              BAD(11);

        /* I10c -- at birth, possession is exactly self and nothing owed. */
        if (a >= A_LAUN && a < A_COMP && systemRetained(v, prevF)
         && (p = systemPossess(v, prevF)) && (q = systemWant(v, prevF))) {
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

        for (r = 0; !bad && r < 256; ++r) {
          if (!systemRetained(v, (unsigned char)r))
            continue;
          if (!(p = systemPossess(v, (unsigned char)r))
           || !(q = systemWant(v, (unsigned char)r))) {
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
            else if (systemFrontier(v) != prevF
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
          else if (systemFrontier(vB) != systemFrontier(v))
            hrWhy = "frontier";
          else if (systemLive(vB) != systemLive(v))
            hrWhy = "live";
          else if (systemOwed(vB) != systemOwed(v))
            hrWhy = "owed";
          else if (systemDuty(vB) != systemDuty(v))
            hrWhy = "duty class";
          else if (*cursorB != *cursor)
            hrWhy = "serve cursor";
          else if (!(p = systemWitnesses(v)) || !(q = systemWitnesses(vB))
                || memcmp(p, q, BS))
            hrWhy = "witness book";
          else if (nactsB != nacts)
            hrWhy = "act count";
          for (i = 0; !hrWhy && i < nacts; ++i) {
            if (outB[i].act != out[i].act || outB[i].round != out[i].round)
              hrWhy = "act kind or round";
            else if (out[i].act == SYSTEM_ACT_SERVE
                  && (!out[i].want || !outB[i].want
                   || memcmp(out[i].want, outB[i].want, BS)))
              hrWhy = "SERVE want bitmap";
          }
          for (r = 0; !hrWhy && r < 256; ++r) {
            if (systemRetained(v, (unsigned char)r)
             != systemRetained(vB, (unsigned char)r)) {
              hrWhy = "retained set";
              break;
            }
            if (!systemRetained(v, (unsigned char)r))
              continue;
            if (memcmp(systemPossess(v, (unsigned char)r),
                       systemPossess(vB, (unsigned char)r), BS))
              hrWhy = "possession record";
            else if (memcmp(systemWant(v, (unsigned char)r),
                            systemWant(vB, (unsigned char)r), BS))
              hrWhy = "want record";
            /* non-vacuity: the whole oracle rests on the two grains
             * actually differing, so count where they do */
            else if (memcmp(systemHave(v, (unsigned char)r),
                            systemHave(vB, (unsigned char)r), BS))
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
          fprintf(stderr, "  action %u from state %u (frontier %u -> %u)\n",
                  a, head, prevF, systemFrontier(v));
#ifdef HRTWIN
          if (hrWhy)
            fprintf(stderr, "  twin divergence: %s\n", hrWhy);
#endif
          for (c = 0, k = head; k > 0 && c < 64; k = parent[k])
            chain[c++] = k;
          fprintf(stderr, "  path (start frontier %u):", startF);
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

        /* Insert if new. */
        {
          unsigned long h;
          unsigned int sl;

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

          /*
           * Canonical descriptor, so the two runs can be compared as
           * SETS rather than by eyeballing two printed counts: the
           * frontier relative to the run's start, the flags, the
           * witness record, then each retained round keyed by its
           * distance behind the frontier (wrap-stable, and blind to
           * freed-slot residue, which no query exposes and no rule
           * reads).  Summing per-state hashes is order-independent.
           */
          memset(desc, 0, sizeof (desc));
          desc[0] = (unsigned char)(systemFrontier(v) - startF);
          desc[1] = v->flags;
          if ((p = systemWitnesses(v)))
            memcpy(desc + 2, p, BS);
          for (cnt = 2 + BS, d = 1; d < 256; ++d) {
            rnd = (unsigned char)(systemFrontier(v) - d);
            if (!systemRetained(v, rnd))
              continue;
            desc[cnt++] = (unsigned char)d;
            memcpy(desc + cnt, systemPossess(v, rnd), BS);
            memcpy(desc + cnt + BS, systemWant(v, rnd), BS);
            memcpy(desc + cnt + 2 * BS, systemHave(v, rnd), BS);
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
    printf("run %u (start frontier %u, horizon %u): %u states, %lu transitions\n",
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
   * the fresh run and the wrap run must reach the same canonical
   * state set.  Equal counts alone would not say this.
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
