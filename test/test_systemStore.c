/*
 * test_systemStore.c
 *
 * Contract test for the public C API in systemStore.h.
 *
 * systemStore.[hc] is one implementation of the four retention
 * operations system.h fixes at systemInit -- the reference one, shared
 * by every instrument in this repo.  That sharing is why it needs a
 * suite of its own: an instrument that drives the MACHINE through this
 * store cannot tell a store defect from a machine defect, and a store
 * defect would be wrong IDENTICALLY in all of them.  So the store is
 * pinned here, alone, against its own documented contract.
 *
 * Tests are derived ONLY from systemStore.h and from the RETENTION
 * REQUIREMENTS in system.h (the caller-side contract the store
 * discharges).  Nothing here reads systemStore.c, and nothing here
 * calls the machine: this file plays the machine's part itself, since
 * the machine is the records' sole writer and the store's obligation
 * is to hand the same bytes back.
 *
 * Sections:
 *
 *   A. Sizes and init defense -- Sz monotone in n / rs / cap; a null
 *      store, rs = 0, cap = 0 and a null comparator each leave a
 *      store that holds nothing and refuses every retain.
 *   B. THE COMPARATOR IS THE AUTHORITY -- the same names inserted
 *      under a REVERSED comparator enumerate in the reversed order.
 *      A store that fell back to comparing the name bytes would pass
 *      every other arm here and fail this one; a round name is a
 *      chain construct, and memcmp would be a different order
 *      silently.
 *   C. Order under out-of-order insert -- names arriving in any order
 *      enumerate ascending, and 'after' is strictly forward, each
 *      held round once, 0 at the end (system.h: after ENUMERATES IN
 *      THE COMPARATOR'S ORDER).  'after' with no round answers the
 *      OLDEST, and its round argument need not be retained.
 *   D. Records are the caller's storage and the machine's bits -- the
 *      bytes written into a round's records survive an insert BELOW
 *      it (which moves the entry) and a release below it.
 *   E. A refusal changes NOTHING -- at cap, retain answers 0 and the
 *      whole allocation is byte-identical to what it was.  This is
 *      the term the machine's no-room close reads as a boundary
 *      input, so a refusal with a side effect would corrupt the
 *      close.  Retaining a round already held is NOT a refusal: it
 *      answers that round's records.
 *   F. release IS the drop -- the round leaves the set, its records
 *      stop being reachable, releasing an unheld round is inert, and
 *      the set changes ONLY through retain and release.
 *   G. CANONICALITY -- two stores that hold the same rounds with the
 *      same records are byte-identical, whatever route they took
 *      there.  Checked over systemStoreState AND over the whole
 *      allocation, because a state enumeration must key on the
 *      retained set and a residue left behind by a release would
 *      split states no operation can tell apart.
 *   H. Multi-instance independence -- two stores share nothing (the
 *      composed seam holds one per seat, and the falsifier's twin
 *      drive two per node).
 *   I. Names are COPIES, both ways -- retain keys the round by a
 *      copy of the presented name (system.h: "key it by a COPY"),
 *      and 'after' hands back the store's OWN name, so the
 *      caller's buffer is dead the moment either call returns.
 *      Plus the edges the earlier sections skip: 'after' a round
 *      on an EMPTY store, a cap-1 store (the forced-choice edge
 *      I11 is vacuous at), and a retain of a HELD round at full
 *      cap (not a refusal -- it answers that round's records).
 *   J. The store writes NOTHING of its own, and two answers are live
 *      at once (added 2026-08-14 by the store-mutant tier, which
 *      found both terms unwatched here).  Every operation that is not
 *      retain or release is a pure lookup, asserted over the WHOLE
 *      allocation rather than over the bytes a reader thought to
 *      check -- a record bit the store sets is indistinguishable
 *      above from one the machine set.  And two answers taken from
 *      one walk name their own rounds and read their own records
 *      AFTER the second call: one reusable scratch buffer passes
 *      every arm above -- it holds the right bytes at the instant of
 *      return -- and merges them.
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

/* n = 6 encoded -> 7 processes; the ordinal instantiation's names. */
#define TN 6
#define RS ((unsigned int)sizeof (unsigned long))
#define CAP 4

/* The header's reference comparator: displacement order. */
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

/* The same order, reversed.  Section B's whole point: the store must
 * take its order from HERE and from nowhere else. */
static int
revCmp(
  void *ctx
 ,const unsigned char *a
 ,const unsigned char *b
){
  return (-ordCmp(ctx, a, b));
}

/* Rotating name pool, as the other suites carry it. */
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
  struct systemStore *s;
  struct systemStore *s2;
  const unsigned char *nm;
  const unsigned char *nm2;
  const unsigned char *st;
  unsigned char *snap;
  unsigned char *rec;
  unsigned char *rec2;
  unsigned long sz;
  unsigned long rsz;
  unsigned long len;
  unsigned long len2;
  unsigned long v;
  unsigned int i;
  unsigned int k;
  unsigned char tmp[sizeof (unsigned long)];

  rsz = systemRecordsSz(TN);
  sz = systemStoreSz(TN, RS, CAP);

  /* ---------------------------------------------------------------- */
  BANNER("A: sizes and init defense");
  /* ---------------------------------------------------------------- */

  CHECK(sz > sizeof (struct systemStore), "Sz exceeds the header");
  CHECK(systemStoreSz(255, RS, CAP) > sz, "Sz monotone in n");
  CHECK(systemStoreSz(TN, RS + 8, CAP) > sz, "Sz monotone in rs");
  CHECK(systemStoreSz(TN, RS, CAP + 1) > sz, "Sz monotone in cap");
  /* the store holds the machine's records verbatim: one entry is a
   * name and exactly one round's records, nothing else */
  CHECK(systemStoreSz(TN, RS, CAP + 1) - sz == RS + rsz,
   "one entry is a name plus one round's records");

  systemStoreInit(0, TN, RS, CAP, ordCmp, 0);       /* must not crash */
  CHECK(systemStoreCount(0) == 0, "null store holds nothing");
  CHECK(systemStoreState(0, &len) == 0, "null store has no state");
  CHECK(systemStoreRecords(0, rn(1)) == 0, "null store: no records");
  CHECK(systemStoreRetain(0, rn(1)) == 0, "null store: retain refuses");
  systemStoreRelease(0, rn(1));                     /* must not crash */
  CHECK(systemStoreAfter(0, 0, &nm) == 0, "null store: nothing after");

  if (!(s = malloc(systemStoreSz(TN, RS + 8, CAP + 1)))
   || !(s2 = malloc(systemStoreSz(TN, RS + 8, CAP + 1)))
   || !(snap = malloc(systemStoreSz(TN, RS + 8, CAP + 1)))) {
    fprintf(stderr, "malloc failed\n");
    return (1);
  }

  /* a rejected store holds nothing and refuses every retain */
  systemStoreInit(s, TN, 0, CAP, ordCmp, 0);
  CHECK(systemStoreRetain(s, rn(1)) == 0, "rejected rs 0: retain refuses");
  CHECK(systemStoreCount(s) == 0, "rejected rs 0: holds nothing");
  systemStoreInit(s, TN, RS, 0, ordCmp, 0);
  CHECK(systemStoreRetain(s, rn(1)) == 0, "rejected cap 0: retain refuses");
  systemStoreInit(s, TN, RS, CAP, 0, 0);
  CHECK(systemStoreRetain(s, rn(1)) == 0, "rejected null cmp: retain refuses");
  CHECK(systemStoreAfter(s, 0, &nm) == 0, "rejected null cmp: nothing after");
  CHECK(systemStoreState(s, &len) && len == 0,
   "rejected store's state is empty");

  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  CHECK(systemStoreCount(s) == 0, "a fresh store is EMPTY (system.h)");
  CHECK(systemStoreAfter(s, 0, &nm) == 0, "fresh store: no oldest");
  CHECK(systemStoreRecords(s, rn(1)) == 0, "fresh store: nothing retained");
  CHECK(systemStoreState(s, &len) && len == 0, "fresh state length 0");
  CHECK(systemStoreState(s, 0) == 0, "state needs somewhere to put the length");

  /* ---------------------------------------------------------------- */
  BANNER("B: the comparator is the authority");
  /* ---------------------------------------------------------------- */

  /* Under ordCmp, 1 precedes 9; under revCmp, 9 precedes 1.  Same
   * names, same insertion order, opposite enumerations -- so the
   * order cannot be coming from the bytes. */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  systemStoreRetain(s, rn(9));
  systemStoreRetain(s, rn(1));
  CHECK(systemStoreAfter(s, 0, &nm) && rv(nm) == 1,
   "ordCmp: the oldest is 1");
  systemStoreInit(s2, TN, RS, CAP, revCmp, 0);
  systemStoreRetain(s2, rn(9));
  systemStoreRetain(s2, rn(1));
  CHECK(systemStoreAfter(s2, 0, &nm) && rv(nm) == 9,
   "revCmp: the oldest is 9 -- the order is the caller's, never the bytes");
  CHECK(systemStoreAfter(s2, rn(9), &nm) && rv(nm) == 1,
   "revCmp: 1 follows 9");
  CHECK(systemStoreAfter(s2, rn(1), &nm) == 0, "revCmp: 1 is the newest");

  /* the same claim through the forwarding comparator the seam uses */
  CHECK(systemStoreCmp(s, rn(1), rn(9)) < 0, "forwarded cmp: 1 precedes 9");
  CHECK(systemStoreCmp(s, rn(9), rn(1)) > 0, "forwarded cmp: 9 follows 1");
  CHECK(systemStoreCmp(s, rn(4), rn(4)) == 0, "forwarded cmp: equal is 0");
  CHECK(systemStoreCmp(s2, rn(1), rn(9)) > 0,
   "forwarded cmp carries the store's OWN comparator");

  /* ---------------------------------------------------------------- */
  BANNER("C: order and enumeration");
  /* ---------------------------------------------------------------- */

  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  CHECK(systemStoreRetain(s, rn(5)) != 0, "retain 5");
  CHECK(systemStoreRetain(s, rn(1)) != 0, "retain 1 -- below 5");
  CHECK(systemStoreRetain(s, rn(3)) != 0, "retain 3 -- between them");
  CHECK(systemStoreCount(s) == 3, "three rounds held");

  /* the walk 'after' promises: ascending, each round once, 0 at the end */
  v = 0;
  k = 0;
  nm = 0;
  while (systemStoreAfter(s, nm, &nm)) {
    if (k && rv(nm) <= v) {
      CHECK(0, "after is strictly forward in the comparator's order");
      break;
    }
    v = rv(nm);
    if (++k > 8)
      break;
  }
  CHECK(k == 3, "after enumerated each held round exactly once");
  CHECK(v == 5, "the walk ended at the newest");

  CHECK(systemStoreAfter(s, 0, &nm) && rv(nm) == 1,
   "after with no round is the OLDEST");
  /* the round 'after' is asked about need not be retained: it is an
   * ORDER question, which is what lets a serve cursor naming a
   * released round still position the walk (system.h systemServe) */
  CHECK(systemStoreAfter(s, rn(2), &nm) && rv(nm) == 3,
   "after an unheld round answers the next held one");
  CHECK(systemStoreAfter(s, rn(0), &nm) && rv(nm) == 1,
   "after a round below everything held answers the oldest");
  CHECK(systemStoreAfter(s, rn(99), &nm) == 0,
   "after a round past everything held answers nothing");
  CHECK(systemStoreAfter(s, rn(5), &nm) == 0, "nothing follows the newest");
  CHECK(systemStoreAfter(s, rn(3), 0) == 0, "after needs somewhere for the name");

  /* ---------------------------------------------------------------- */
  BANNER("D: records are the caller's storage");
  /* ---------------------------------------------------------------- */

  /* the machine is the sole writer, so this file writes them: a
   * distinct byte pattern per round, read back after the entry has
   * been MOVED by an insert below it and by a release below it */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  for (i = 0; i < 3; ++i) {
    if (!(rec = systemStoreRetain(s, rn((unsigned long)(2 * i + 2))))) {
      CHECK(0, "retain for the record arms");
      break;
    }
    memset(rec, (int)(0x10 + i), rsz);
  }
  CHECK(systemStoreRecords(s, rn(4))
   && *systemStoreRecords(s, rn(4)) == 0x11, "records read back");
  CHECK(systemStoreRetain(s, rn(3)) != 0, "insert BELOW two held rounds");
  for (i = 0; i < 3; ++i) {
    unsigned int ok;

    rec = systemStoreRecords(s, rn((unsigned long)(2 * i + 2)));
    ok = rec && rec[0] == 0x10 + i && rec[rsz - 1] == 0x10 + i;
    CHECK(ok, "an insert below a round leaves its records intact");
  }
  systemStoreRelease(s, rn(2));
  CHECK((rec = systemStoreRecords(s, rn(6))) && rec[0] == 0x12,
   "a release below a round leaves its records intact");
  CHECK(systemStoreRecords(s, rn(7)) == 0, "no records for an unheld round");
  CHECK(systemStoreRecords(s, 0) == 0, "records needs a round");

  /* a retain of a round already held is not a second entry: it hands
   * back that round's records */
  CHECK(systemStoreRetain(s, rn(6)) == systemStoreRecords(s, rn(6)),
   "retaining a held round answers its records");
  CHECK(systemStoreCount(s) == 3, "and adds no entry");

  /* ---------------------------------------------------------------- */
  BANNER("E: a refusal changes nothing");
  /* ---------------------------------------------------------------- */

  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  for (i = 0; i < CAP; ++i)
    if (!(rec = systemStoreRetain(s, rn((unsigned long)(10 - i))))) {
      CHECK(0, "retain up to cap");
      break;
    } else
      memset(rec, (int)(0x40 + i), rsz);
  CHECK(systemStoreCount(s) == CAP, "the reach is full");
  memcpy(snap, s, sz);
  CHECK(systemStoreRetain(s, rn(99)) == 0,
   "at the reach, retain REFUSES -- that refusal is how the reach binds");
  CHECK(!memcmp(s, snap, sz),
   "and the refusal changed NOTHING (system.h: retain never makes room)");
  CHECK(systemStoreRetain(s, rn(1)) == 0, "a refusal below the set too");
  CHECK(!memcmp(s, snap, sz), "and that one changed nothing either");
  CHECK(systemStoreRetain(s, 0) == 0, "retain needs a round");
  CHECK(!memcmp(s, snap, sz), "a null round changed nothing");

  /* ---------------------------------------------------------------- */
  BANNER("F: release IS the drop");
  /* ---------------------------------------------------------------- */

  systemStoreRelease(s, rn(8));
  CHECK(systemStoreCount(s) == CAP - 1, "release drops one round");
  CHECK(systemStoreRecords(s, rn(8)) == 0, "the dropped round has no records");
  nm = 0;
  k = 0;
  while (systemStoreAfter(s, nm, &nm)) {
    CHECK(rv(nm) != 8, "the dropped round is not enumerated");
    if (++k > 8)
      break;
  }
  CHECK(k == CAP - 1, "the walk sees exactly what is held");
  memcpy(snap, s, sz);
  systemStoreRelease(s, rn(8));
  CHECK(!memcmp(s, snap, sz), "releasing an unheld round is inert");
  systemStoreRelease(s, rn(99));
  systemStoreRelease(s, 0);
  CHECK(!memcmp(s, snap, sz), "so is releasing a round never held, or none");
  CHECK(systemStoreRetain(s, rn(99)) != 0,
   "the released round's room is available again");

  /* ---------------------------------------------------------------- */
  BANNER("G: canonicality");
  /* ---------------------------------------------------------------- */

  /* Two routes to one retained set.  s takes the long way -- four
   * rounds retained and one released; s2 goes straight there.  They
   * must be indistinguishable, and not only through systemStoreState:
   * a state enumeration keys on the retained set (it is reachable
   * state the machine directs but does not hold), and a residue left
   * behind by the release would split states nothing can tell apart. */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  systemStoreInit(s2, TN, RS, CAP, ordCmp, 0);
  for (i = 0; i < 4; ++i) {
    rec = systemStoreRetain(s, rn((unsigned long)(i + 1)));
    if (rec)
      memset(rec, (int)(0x50 + i), rsz);
  }
  systemStoreRelease(s, rn(2));
  for (i = 0; i < 4; ++i) {
    if (i == 1)
      continue;
    rec = systemStoreRetain(s2, rn((unsigned long)(i + 1)));
    if (rec)
      memset(rec, (int)(0x50 + i), rsz);
  }
  st = systemStoreState(s, &len);
  CHECK(st && len == 3 * (RS + rsz),
   "the state is exactly the live entries");
  CHECK(systemStoreState(s2, &len2) && len2 == len,
   "both routes reach the same state length");
  CHECK(st && !memcmp(st, systemStoreState(s2, &len2), len),
   "both routes reach byte-identical state");
  CHECK(!memcmp(s, s2, sz),
   "and byte-identical ALLOCATIONS -- a release leaves no residue");

  /* the state tracks the set, not the history */
  systemStoreRelease(s, rn(1));
  CHECK(systemStoreState(s, &len2) && len2 == len - (RS + rsz),
   "a release shortens the state by exactly one entry");

  /* ---------------------------------------------------------------- */
  BANNER("H: multi-instance independence");
  /* ---------------------------------------------------------------- */

  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  systemStoreInit(s2, TN, RS, CAP, ordCmp, 0);
  if ((rec = systemStoreRetain(s, rn(7))))
    memset(rec, 0x77, rsz);
  CHECK(systemStoreCount(s2) == 0, "the second store saw nothing");
  CHECK(systemStoreRecords(s2, rn(7)) == 0, "nor holds the round");
  if ((rec = systemStoreRetain(s2, rn(7))))
    memset(rec, 0x22, rsz);
  CHECK((rec = systemStoreRecords(s, rn(7))) && rec[0] == 0x77,
   "the first store's records are its own");
  systemStoreRelease(s2, rn(7));
  CHECK(systemStoreCount(s) == 1, "a release in one is not a release in both");

  /* ---------------------------------------------------------------- */
  BANNER("I: names are copies, both ways");
  /* ---------------------------------------------------------------- */

  /* retain keys the round by a COPY of the presented name: the
   * caller's buffer is dead the moment the call returns */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  v = 5;
  memcpy(tmp, &v, sizeof (v));
  if ((rec = systemStoreRetain(s, tmp)))
    memset(rec, 0x66, rsz);
  memset(tmp, 0xFF, sizeof (tmp));
  CHECK((rec = systemStoreRecords(s, rn(5))) && rec[0] == 0x66,
   "retain keyed a COPY of the name, not the caller's buffer");
  CHECK(systemStoreAfter(s, 0, &nm) && rv(nm) == 5,
   "and the walk enumerates the copy");

  /* after hands back the store's OWN name, not an echo of the
   * caller's argument */
  CHECK(systemStoreRetain(s, rn(7)) != 0, "retain 7 above it");
  v = 5;
  memcpy(tmp, &v, sizeof (v));
  if (systemStoreAfter(s, tmp, &nm)) {
    memset(tmp, 0xFF, sizeof (tmp));
    CHECK(rv(nm) == 7, "the name handed back is the store's own copy");
    CHECK(nm != tmp, "and never the caller's buffer");
  } else
    CHECK(0, "after the held 5 answers 7");

  /* after a round on an EMPTY store answers nothing */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  CHECK(systemStoreAfter(s, rn(3), &nm) == 0,
   "after a round on an empty store answers nothing");

  /* a cap-1 store: the forced-choice edge (I11 is vacuous there) */
  systemStoreInit(s, TN, RS, 1, ordCmp, 0);
  CHECK(systemStoreRetain(s, rn(4)) != 0, "cap 1: the one round retains");
  memcpy(snap, s, systemStoreSz(TN, RS, 1));
  CHECK(systemStoreRetain(s, rn(6)) == 0, "cap 1: the second refuses");
  CHECK(!memcmp(s, snap, systemStoreSz(TN, RS, 1)),
   "cap 1: and the refusal changed nothing");
  systemStoreRelease(s, rn(4));
  CHECK(systemStoreRetain(s, rn(6)) != 0, "cap 1: release makes the room");
  CHECK(systemStoreAfter(s, 0, &nm) && rv(nm) == 6, "cap 1: the oldest is it");

  /* a retain of a HELD round at FULL cap is not a refusal */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  for (i = 0; i < CAP; ++i)
    if (!systemStoreRetain(s, rn((unsigned long)(i + 1)))) {
      CHECK(0, "retain up to cap for the held-retain arm");
      break;
    }
  CHECK(systemStoreRetain(s, rn(2)) == systemStoreRecords(s, rn(2)),
   "at full cap, retaining a HELD round answers its records");
  CHECK(systemStoreCount(s) == CAP, "and adds no entry");

  /* ---------------------------------------------------------------- */
  BANNER("J: the store writes nothing of its own, and two answers live");
  /* ---------------------------------------------------------------- */

  /* Two terms this suite could not see until something went looking
   * for them (test/storeMutants.sh, arms SM_C15_STORE_WRITES and
   * SM_C17_SCRATCH_*: both passed every arm above), and both are the
   * store's own to keep.
   *
   * THE MACHINE IS THE RECORDS SOLE WRITER, so every operation that is
   * not retain or release is a pure LOOKUP.  A store that touched a
   * record byte on the way past would be writing state the machine
   * believes only it writes, and nothing above can tell that bit from
   * one the machine set -- which is why the arm is stated over the
   * WHOLE allocation and not over the bytes a reader thought to check.
   *
   * A RECORD AND A NAME STAY VALID until the set next changes, so two
   * answers taken from one walk are live AT THE SAME TIME.  One
   * reusable scratch buffer satisfies every other arm in this file --
   * it holds the right bytes at the instant of return -- and merges
   * them. */
  /* the pattern RAMPS inside each record and never repeats a byte:
   * under a uniform fill a store that folded one record byte into
   * another -- the exact shape of a sole-writer violation, since the
   * machine's three bitmaps sit end to end in here -- would leave the
   * bytes it merged unchanged, and this arm would pass it.  (Measured:
   * it did, on the tier's first run.) */
  systemStoreInit(s, TN, RS, CAP, ordCmp, 0);
  for (i = 0; i < 3; ++i)
    if ((rec = systemStoreRetain(s, rn((unsigned long)(2 * i + 1)))))
      for (k = 0; k < rsz; ++k)
        rec[k] = (unsigned char)(0x11 * (i + 1) + k);
  CHECK(systemStoreCount(s) == 3, "three rounds held, each record written");
  memcpy(snap, s, sz);

  /* the walk is BOUNDED for the same reason section C's is: a store
   * whose 'after' is not strictly forward does not terminate, and a
   * suite that hangs where it could red is worth less than one that
   * reds (measured -- an unbounded walk here turned the C16 tier arm's
   * store-suite red into a hang) */
  nm = 0;
  k = 0;
  while ((rec = systemStoreAfter(s, nm, &nm)) && ++k <= 8)
    ;
  CHECK(k == 3, "the walk covers the three rounds and stops");
  CHECK(systemStoreRecords(s, rn(1)) && systemStoreRecords(s, rn(5)),
   "the held rounds answer records");
  CHECK(systemStoreRecords(s, rn(4)) == 0, "an unheld round answers none");
  CHECK(systemStoreState(s, &len) && len == 3 * (RS + rsz),
   "state answers the three live entries");
  CHECK(systemStoreCmp(s, rn(1), rn(3)) < 0, "the comparator answers");
  CHECK(!memcmp(s, snap, sz),
   "and not one byte of the allocation moved: every ask is a LOOKUP");

  /* two answers from one walk, live at the same time */
  rec = systemStoreAfter(s, 0, &nm);        /* the oldest, round 1 */
  nm2 = nm;                                 /* held across the next call */
  rec2 = systemStoreAfter(s, nm, &nm);      /* round 3 */
  CHECK(rec && rec2, "two answers taken from one walk");
  CHECK(rv(nm2) == 1 && rv(nm) == 3,
   "each still names its own round after the second call");
  CHECK(nm2 != nm && rec != rec2, "each answer has storage of its own");
  CHECK(rec[0] == 0x11 && rec2[0] == 0x22,
   "and each record still reads its own round's bytes");
  CHECK(rec == systemStoreRecords(s, nm2),
   "after and records answer the SAME storage for a round"
   " (the machine writes through both)");

  free(snap);
  free(s2);
  free(s);

  printf("test_systemStore: %d checks, %d failures\n", Checks, Failures);
  if (Failures) {
    printf("FAILED\n");
    return (1);
  }
  printf("PASSED\n");
  return (0);
}
