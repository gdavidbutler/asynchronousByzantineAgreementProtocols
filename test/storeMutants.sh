#!/bin/sh
#
# storeMutants.sh -- the STORE-MUTANT TIER (2026-08-14).
#
# Retention was EXTERNALIZED to the caller: the rounds this process
# holds and their three per-round records live in caller storage
# reached through records / retain / release / after, and the terms
# that storage must meet are THE RETENTION REQUIREMENTS (system.md
# Mechanization status, mirrored in system.h, registered C13-C20 in
# test/systemProofCoverage.md).
#
# Every one of those rows was a GREEN CONTROL and not a matched red,
# and the register says why that matters: what the arms showed was
# that a CORRECT store discharges each term and that the machine
# composed with it behaves.  What nothing showed was that a BROKEN
# store gets CAUGHT -- the same status the falsifier's I1-I11 arms
# held before machineMutants.sh landed.
#
# This script closes that.  Each entry copies systemStore.c to a
# scratch tree, applies ONE anchored edit (the anchor asserted to
# match EXACTLY ONCE first -- a missed anchor is a loud failure, never
# a silent no-op mutant), rebuilds the instruments against the mutated
# copy with the CLEAN machine (system.o is not under test here), runs
# them, and asserts the DESIGNATED oracle fires.  systemStore.c itself
# is never written: its checksum is taken before and after the whole
# tier and a difference aborts.
#
# TWO ARMS MUTATE THE DRIVER INSTEAD (SM_C20_*), and that is not a
# lapse: SUCCESSION is a ROUND operation, not a retention one -- the
# name a close mints is handed to the machine by its caller and the
# store never sees it -- so the only place to withdraw C20 is the
# caller.  test/r1Mutants.sh is the precedent (it mutates the caller
# half in example/system.c against clean machine objects).
#
# WHAT EACH ARM RUNS.  All four instruments, every time: the store's
# own contract suite, the machine's contract suite, the composed seam,
# and the invariant falsifier.  Only the DESIGNATED oracle is
# asserted; the other three are RECORDED, because which instruments a
# broken store reaches is exactly the fact this tier exists to
# establish, and a tier that asserted every red it happened to see
# would be measuring the mutation, not the arm.
#
# A CLEAN PAIRING RUNS LAST, mutating nothing: all four instruments
# must come back green through this same build and link arrangement,
# and the falsifier carries -DEXPECTSTATES so the frozen enumeration is
# pinned rather than assumed.  A mutation harness lies UPWARD -- a
# build that reds by itself looks exactly like a tier of kills -- and
# the pairing is what that costs to rule out.
#
# WHAT THE FIRST RUN ESTABLISHED, beyond the kills (2026-08-14):
#   - THE STORE'S OWN SUITE WAS BLIND TO TWO OF ITS OWN TERMS.
#     SM_C15_STORE_WRITES and both SM_C17_SCRATCH_* arms passed
#     test_systemStore.c outright -- a store that writes a record bit
#     on the way past, or answers from one reusable buffer, holds the
#     right bytes at the instant of every return that file looked at.
#     CLOSED the same day by its new section J (a pure-lookup arm over
#     the whole allocation, and two answers held live at once), which
#     is the tier's first yield and the reason a mutant tier over a
#     shared artifact is worth its cost.  Section J's FIRST cut was
#     blind too, and the second run of this tier is what said so: it
#     filled each record with one repeated byte, and a store that
#     folds one record byte into another leaves a uniform fill
#     unchanged.  The fill ramps now.  An arm is not an oracle until
#     something has failed it.
#   - THE COMPOSED SEAM IS GREEN under SM_C16_NEWEST_FIRST, both
#     SM_C17_* and two of the C19 arms: at a reach of 3 with its
#     schedule, an order or lifetime defect never reaches a check
#     there.  Named because a seam that greens is evidence about the
#     seam's reach, not about the store.
#   - THE FALSIFIER IS BLIND TO C14 (it runs the full enumeration
#     clean), which is exactly why the register named the seam's D arm
#     for that term before either was run.
#
# THE INVENTORY (term -> mutation -> designated oracle):
#
#   SM_C13_RETAIN_EVICTS   C13.  retain makes room by dropping the
#                       oldest instead of refusing.  The register's
#                       own note: the arm must test the ANNOUNCEMENT,
#                       not the eviction -- a machine-directed
#                       eviction is legitimate and reported; what C13
#                       forbids is the SILENT departure.
#                       ORACLE: falsifier I9 (a round left retention
#                       with no RELEASE act naming it).
#   SM_C14_SECOND_REFUSES  C14.  the refusal at the reach also LOWERS
#                       the reach, so the retry after the machine's
#                       announced eviction refuses too and the close
#                       advances the frontier having retained nothing.
#                       ORACLE: the composed seam's D ground-truth arm
#                       (no advance may outrun n-t processes having
#                       actually closed the prior round) -- the
#                       register named it before it was run, and it
#                       already existed.
#   SM_C15_STORE_WRITES    C15.  the store ORs each record's possess
#                       bitmap into its want bitmap on every ask --
#                       one bit written by something other than the
#                       machine.
#                       ORACLE: falsifier I10's BIRTH clause (at birth
#                       possession is exactly self and nothing is
#                       owed).  The register named I2 for this term
#                       and I2 is indeed violated -- want equals
#                       possess at every retained round, and contract
#                       section K fires -- but the falsifier reports
#                       the FIRST conjunct it finds and the birth
#                       clause is checked ahead of I2, so I10 is what
#                       this arm actually kills with.  Either counts
#                       here; which one fired is in the log.
#   SM_C16_NEWEST_FIRST    C16, the ORDER half.  after enumerates the
#                       comparator's order backwards.
#                       ORACLE: falsifier I11 (eviction releases the
#                       OLDEST) -- the caller-side twin of
#                       MM_I11_EVICT_NEWEST.
#   SM_C16_NOT_FORWARD     C16, the FORWARD half.  after answers the
#                       round it was asked about instead of the one
#                       after it.  THE MACHINE CANNOT SURVIVE THIS and
#                       says so in system.h: the walk does not
#                       terminate, so the mutant HANGS rather than
#                       reds.
#                       ORACLE: a TIMEOUT (the watchdog's 124), which
#                       is why this tier carries a short per-arm limit
#                       beside the long one.  Named in the register
#                       before it was built.
#   SM_C17_SCRATCH_NAME    C17.  after answers *name from one reusable
#                       scratch buffer, so a SERVE act's .round
#                       dangles at the next call.
#                       ORACLE: contract suite section M.
#   SM_C17_SCRATCH_RECORD  C17's sibling.  after answers the RECORD
#                       from one reusable scratch buffer, so a SERVE
#                       act's .want comes to belong to a different
#                       round than its .round.
#                       ORACLE: contract suite section M.
#   SM_C18_NO_DROP         C18.  release does not drop the round.
#                       ORACLE: falsifier I4 (a retained round short
#                       of all n -- release fires exactly at all n, so
#                       a round that survives its own release is
#                       retained with every process possessing it).
#   SM_C19_PREPOP_FRONTIER C19.  the store holds one round at init:
#                       the genesis name.
#                       ORACLE: falsifier I7 (the frontier is never
#                       retained).
#   SM_C19_PREPOP_UNOWNED  C19.  the store holds one round at init
#                       whose record nobody wrote.
#                       ORACLE: falsifier I3 (self possesses every
#                       retained round).
#   SM_C19_PREPOP_ALLN     C19.  the store holds one round at init
#                       whose possession record is already all n.
#                       ORACLE: falsifier I8 (release fires exactly at
#                       all n) -- the caller-side twin of MM_I8_EARLY:
#                       a round the machine never counted to all n is
#                       already there, so the release accounting on
#                       the first possession path is wrong.  I4 was
#                       the register's guess and is the conjunct one
#                       reads first, but the machine RELEASES the
#                       round rather than keeping it, so I8 is what
#                       the state actually violates; I4's matched red
#                       is SM_C18_NO_DROP, where the round does stay.
#   SM_C20_MINT_EQUAL      C20, in the DRIVER.  the close mints the
#                       closed round's own name as its successor -- a
#                       name that does not FOLLOW the frontier it
#                       succeeds, it equals it.
#                       ORACLE: falsifier I7.
#   SM_C20_MINT_BEHIND     C20's sharper face, in the DRIVER.  the
#                       close mints a name strictly BEHIND the round
#                       it closes -- distinct from everything retained
#                       (which is all "released before recurrence"
#                       gives) and behind them all, which is what the
#                       register says distinctness does not give.
#                       NO ORACLE, AND THE ARM IS RECORDED RATHER THAN
#                       ASSERTED.  Measured: not one conjunct fires --
#                       the reachable set COLLAPSES from 187,550
#                       states to 25, because a frontier behind its
#                       own retained rounds classifies everything
#                       ahead and the machine refuses its way to a
#                       standstill.  That is the honest shape of C20:
#                       the ORDER term is the caller's discipline, the
#                       machine checks nothing, and what a violation
#                       costs is liveness, which no invariant here
#                       states.  Recorded on the MM_HR_GATES
#                       plain-falsifier precedent -- a blindness named
#                       is worth more than an oracle invented for it.
#
# I3, I4 AND I7 GET THEIR FIRST MATCHED REDS HERE.  Section 1 of the
# register listed them as unfalsified witnesses with no designated
# mutant -- MM_CURSOR_UNGATED had given I2 one, and I5/I6/I8/I9/I10/
# I11 had theirs from the machine tier, but nothing in the tree broke
# a machine or a store in a way those three catch.  The C19 arms do:
# a store that is not empty at init is precisely a state their
# conjuncts forbid and no correct machine can produce.
#
# Usage:  sh test/storeMutants.sh          (or make store-mutants)
# Env:    CC, CFLAGS, SMLIMIT (per-run watchdog seconds, default 3600),
#         SMHANG (the hang arm's watchdog, default 90)
#
# Artifacts land in storeMutants.d/ at the repo root (Makefile clean
# removes it).  The script is idempotent: it rebuilds that tree from
# scratch on every run.

set -u

Root=`cd \`dirname "$0"\`/.. && pwd`
Work=$Root/storeMutants.d
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c89 -pedantic -Wall -Wextra -Os -g}
Limit=${SMLIMIT:-3600}
Hang=${SMHANG:-90}
Summary=$Work/summary.txt

Bad=0
Rc=0
Sec=0
Lim=$Limit

rm -rf "$Work"
mkdir -p "$Work" || exit 2

for f in system.o bkr94acs.o bracha87.o; do
  if [ ! -f "$Root/$f" ]; then
    echo "FATAL: $Root/$f is missing -- run make first" >&2
    exit 2
  fi
done

Before=`shasum -a 256 "$Root/systemStore.c" | awk '{print $1}'`
BeforeD=`shasum -a 256 "$Root/test/test_system_invariant.c" | awk '{print $1}'`
echo "systemStore.c sha256 before:            $Before" | tee "$Summary"
echo "test/test_system_invariant.c sha256 before: $BeforeD" >> "$Summary"
echo "" >> "$Summary"

# ------------------------------------------------------------------
# mechanism (the machineMutants.sh pattern)
# ------------------------------------------------------------------

# count occurrences of a literal anchor in a file
anchorCount() {
  SM_A="$2" perl -0777 -ne 'my $c = () = /\Q$ENV{SM_A}\E/g; print $c' "$1"
}

# apply one anchored edit; the anchor MUST match exactly once
mutate() {
  c=`anchorCount "$1" "$2"`
  if [ "$c" != 1 ]; then
    echo "FATAL: anchor matched $c times (expected 1) in $1" >&2
    echo "--- anchor ---" >&2
    echo "$2" >&2
    exit 2
  fi
  SM_A="$2" SM_R="$3" perl -0777 -i -pe 's/\Q$ENV{SM_A}\E/$ENV{SM_R}/' "$1"
}

# run a command under a watchdog (no GNU timeout on macOS); sets Rc, Sec
runLog() {
  log=$1
  shift
  t0=`date +%s`
  perl -e '
    my $lim = shift @ARGV;
    my $pid = fork();
    die "fork: $!" unless defined $pid;
    if (!$pid) { exec @ARGV; exit 127; }
    $SIG{ALRM} = sub { kill "KILL", $pid; waitpid $pid, 0; exit 124; };
    alarm $lim;
    waitpid $pid, 0;
    my $st = $?;
    alarm 0;
    exit($st & 127 ? 128 + ($st & 127) : $st >> 8);
  ' "$Lim" "$@" > "$log" 2>&1
  Rc=$?
  t1=`date +%s`
  Sec=`expr $t1 - $t0`
}

# the distinct FAIL lines of a falsifier log, with counts
fired() {
  grep '^FAIL:' "$1" 2>/dev/null \
  | sed 's/^FAIL: //; s/ violated (run [0-9]*)//' \
  | sort | uniq -c \
  | awk '{c=$1; $1=""; sub(/^ */,""); printf "%s x%d; ", $0, c}'
}

# the count line of a contract-style suite
checks() {
  grep 'checks, .* failures' "$1" 2>/dev/null | tail -1
}

# which sections of the machine's contract suite fired, with counts
sections() {
  grep '^FAIL \[' "$1" 2>/dev/null \
  | sed 's/^FAIL \[\(.\):.*/\1/' | sort | uniq -c \
  | awk '{printf "%s x%d; ", $2, $1}'
}

# the distinct seam arms that fired, with counts
seamArms() {
  grep '^FAIL \[' "$1" 2>/dev/null \
  | sed 's/^FAIL \[[^]]*\]: //; s/  (.*//' \
  | sort | uniq -c | head -8 \
  | awk '{c=$1; $1=""; sub(/^ */,""); printf "%s x%d; ", $0, c}'
}

# fresh scratch copy of the store (or of the driver, for the C20 arms)
prep() {
  mkdir -p "$Work/$1" || exit 2
  cp "$Root/systemStore.c" "$Work/$1/systemStore.c" || exit 2
}

prepDrv() {
  mkdir -p "$Work/$1" || exit 2
  cp "$Root/test/test_system_invariant.c" "$Work/$1/driver.c" || exit 2
}

# compile the mutated store and the four instruments against it
compile() {
  d=$Work/$1
  $CC $CFLAGS -I"$Root" -c -o "$d/systemStore.o" "$d/systemStore.c" \
    > "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 did not compile -- see $d/build.log" >&2
    exit 2
  fi
  $CC $CFLAGS -I"$Root" -o "$d/store" \
    "$Root/test/test_systemStore.c" "$d/systemStore.o" "$Root/system.o" \
    >> "$d/build.log" 2>&1 || { echo "FATAL: $1 store suite link" >&2; exit 2; }
  $CC $CFLAGS -I"$Root" -o "$d/contract" \
    "$Root/test/test_system.c" "$Root/system.o" "$d/systemStore.o" \
    >> "$d/build.log" 2>&1 || { echo "FATAL: $1 contract link" >&2; exit 2; }
  $CC $CFLAGS -I"$Root" -o "$d/seam" \
    "$Root/test/test_system_seam.c" "$Root/system.o" "$d/systemStore.o" \
    "$Root/bkr94acs.o" "$Root/bracha87.o" \
    >> "$d/build.log" 2>&1 || { echo "FATAL: $1 seam link" >&2; exit 2; }
  $CC $CFLAGS ${2:-} -I"$Root" -o "$d/falsifier" \
    "$Root/test/test_system_invariant.c" "$Root/system.o" "$d/systemStore.o" \
    >> "$d/build.log" 2>&1 || { echo "FATAL: $1 falsifier link" >&2; exit 2; }
  if [ -s "$d/build.log" ]; then
    echo "  note: compiler diagnostics in $d/build.log"
  fi
}

# compile a mutated DRIVER against the clean store and machine
compileDrv() {
  d=$Work/$1
  $CC $CFLAGS -I"$Root" -o "$d/falsifier" \
    "$d/driver.c" "$Root/system.o" "$Root/systemStore.o" \
    > "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 driver did not build -- see $d/build.log" >&2
    exit 2
  fi
}

report() {
  echo "$1" >> "$Summary"
  echo "$1"
}

# run every instrument and record what each did; only the arm's own
# designated oracle is asserted afterward
drive() {
  d=$Work/$1
  runLog "$d/store.log" "$d/store"
  StoreRc=$Rc
  report "  store suite:    rc $Rc, ${Sec}s, `checks $d/store.log`"
  runLog "$d/contract.log" "$d/contract"
  ContractRc=$Rc
  report "  contract suite: rc $Rc, ${Sec}s, `checks $d/contract.log`, sections: `sections $d/contract.log`"
  runLog "$d/seam.log" "$d/seam" 16
  SeamRc=$Rc
  report "  composed seam:  rc $Rc, ${Sec}s, `checks $d/seam.log`, arms: `seamArms $d/seam.log`"
  runLog "$d/falsifier.log" "$d/falsifier"
  FalsRc=$Rc
  report "  falsifier:      rc $Rc, ${Sec}s, fired: `fired $d/falsifier.log`"
}

# assert a designated oracle fired; $1 = mutant, $2 = what, $3 = 1/0
verdict() {
  if [ "$3" = 1 ]; then
    report "  VERDICT: KILL -- $2"
  else
    report "  VERDICT: *** NO RED *** -- expected $2"
    Bad=`expr $Bad + 1`
  fi
  report ""
}

# how many times a falsifier invariant fired
fals() {
  grep -c "FAIL: $2" "$Work/$1/falsifier.log"
}

# ------------------------------------------------------------------
# 1. SM_C13_RETAIN_EVICTS
# ------------------------------------------------------------------
N=SM_C13_RETAIN_EVICTS
report "$N"
report "  edit: retain makes room by dropping the oldest instead of refusing"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  if (s->cnt >= s->cap)
    return (0); /* the reach binds -- and nothing has changed */' \
  '  if (s->cnt >= s->cap) {
    /* C13 withdrawn: room is made silently, with no RELEASE act */
    for (j = 0; j + 1 < s->cnt; ++j)
      memcpy(stEnt(s, j), stEnt(s, j + 1), stEsz(s));
    --s->cnt;
    memset(stEnt(s, s->cnt), 0, stEsz(s));
    i = stFind(s, round, &found);
  }'
compile $N
drive $N
if [ "`fals $N 'I9 rounds leave retention only by RELEASE'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I9 (a departure nothing announced)" $r

# ------------------------------------------------------------------
# 2. SM_C14_SECOND_REFUSES
# ------------------------------------------------------------------
N=SM_C14_SECOND_REFUSES
report "$N"
report "  edit: the refusal at the reach also lowers the reach, so the"
report "        retry after the machine's eviction refuses too"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  if (s->cnt >= s->cap)
    return (0); /* the reach binds -- and nothing has changed */' \
  '  if (s->cnt >= s->cap) {
    /* C14 withdrawn: retain does not succeed for the round a close
     * names, even after the machine has made room */
    if (s->cap)
      --s->cap;
    return (0);
  }'
compile $N
drive $N
if [ "`grep -c 'D: no advance outran' $Work/$N/seam.log`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "the seam's D ground-truth advance arm" $r

# ------------------------------------------------------------------
# 3. SM_C15_STORE_WRITES
# ------------------------------------------------------------------
N=SM_C15_STORE_WRITES
report "$N"
report "  edit: the store ORs possess into want on every records ask"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  i = stFind(s, round, &found);
  if (!found)
    return (0);
  return (stEnt(s, i) + s->rs);
}' \
  '  i = stFind(s, round, &found);
  if (!found)
    return (0);
  /* C15 withdrawn: the machine is not the records sole writer */
  *(stEnt(s, i) + s->rs + stBs(s)) |= *(stEnt(s, i) + s->rs);
  return (stEnt(s, i) + s->rs);
}'
compile $N
drive $N
if [ "`fals $N 'I10 rounds enter retention'`" -gt 0 ] \
|| [ "`fals $N 'I2 possess/want disjoint'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I10's birth clause (I2 is violated too -- see the header)" $r

# ------------------------------------------------------------------
# 4. SM_C16_NEWEST_FIRST
# ------------------------------------------------------------------
N=SM_C16_NEWEST_FIRST
report "$N"
report "  edit: after enumerates the comparator's order backwards"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  if (!round)
    i = 0;
  else if ((i = stFind(s, round, &found)), found)
    ++i;
  if (i >= s->cnt)
    return (0);' \
  '  /* C16 withdrawn: the enumeration runs newest first */
  if (!round)
    i = s->cnt;
  else if ((i = stFind(s, round, &found)), found)
    i += 0;
  if (!i || i > s->cnt)
    return (0);
  --i;'
compile $N
drive $N
if [ "`fals $N 'I11 eviction releases the oldest'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I11 (eviction releases the OLDEST)" $r

# ------------------------------------------------------------------
# 5. SM_C16_NOT_FORWARD  (the arm whose oracle is a TIMEOUT)
# ------------------------------------------------------------------
N=SM_C16_NOT_FORWARD
report "$N"
report "  edit: after answers the round it was asked about, not the one after"
report "  (system.h: the one place this layer is not inert on bad input)"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  else if ((i = stFind(s, round, &found)), found)
    ++i;' \
  '  else if ((i = stFind(s, round, &found)), found)
    i -= 0; /* C16 withdrawn: after is not strictly forward */'
compile $N
Lim=$Hang
drive $N
Lim=$Limit
report "  (every run above is under the ${Hang}s hang watchdog: 124 IS the oracle)"
if [ "$ContractRc" = 124 ] || [ "$FalsRc" = 124 ] || [ "$StoreRc" = 124 ] \
|| [ "$SeamRc" = 124 ]; then
  r=1
else
  r=0
fi
verdict $N "a TIMEOUT -- the walk does not terminate" $r

# ------------------------------------------------------------------
# 6. SM_C17_SCRATCH_NAME
# ------------------------------------------------------------------
N=SM_C17_SCRATCH_NAME
report "$N"
report "  edit: after answers *name from one reusable scratch buffer"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  *name = stEnt(s, i);
  return (stEnt(s, i) + s->rs);' \
  '  /* C17 withdrawn: the name is answered from per-call scratch */
  {
    static unsigned char scr[64];

    memcpy(scr, stEnt(s, i), s->rs);
    *name = scr;
  }
  return (stEnt(s, i) + s->rs);'
compile $N
drive $N
if [ "`grep -c "^FAIL \[M:" $Work/$N/contract.log`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "contract suite section M (the act's .round dangles)" $r

# ------------------------------------------------------------------
# 7. SM_C17_SCRATCH_RECORD
# ------------------------------------------------------------------
N=SM_C17_SCRATCH_RECORD
report "$N"
report "  edit: after answers the RECORD from one reusable scratch buffer"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  *name = stEnt(s, i);
  return (stEnt(s, i) + s->rs);' \
  '  /* C17 withdrawn: the record is answered from per-call scratch */
  *name = stEnt(s, i);
  {
    static unsigned char scr[3 * 32];

    memcpy(scr, stEnt(s, i) + s->rs, 3 * stBs(s));
    return (scr);
  }'
compile $N
drive $N
if [ "`grep -c "^FAIL \[M:" $Work/$N/contract.log`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "contract suite section M (the act's .want belongs to another round)" $r

# ------------------------------------------------------------------
# 8. SM_C18_NO_DROP
# ------------------------------------------------------------------
N=SM_C18_NO_DROP
report "$N"
report "  edit: release does not drop the round"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  s = v;
  if (!s || !s->cmp || !round)
    return;
  i = stFind(s, round, &found);' \
  '  s = v;
  if (!s || !s->cmp || !round)
    return;
  return; /* C18 withdrawn: release is not the drop */
  i = stFind(s, round, &found);'
compile $N
drive $N
if [ "`fals $N 'I4 retained short of all n'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I4 (a round survives its own release)" $r

# ------------------------------------------------------------------
# 9. SM_C19_PREPOP_FRONTIER
# ------------------------------------------------------------------
N=SM_C19_PREPOP_FRONTIER
report "$N"
report "  edit: the store holds the genesis name at init"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  s->rs = rs;
  s->n = n;
}' \
  '  s->rs = rs;
  s->n = n;
  /* C19 withdrawn: the store is not empty at init.  The entry is
   * already zeroed, so its name is the all-zero one -- the genesis
   * every instrument here names. */
  s->cnt = 1;
}'
compile $N
drive $N
if [ "`fals $N 'I7 frontier not retained'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I7 (the frontier is never retained)" $r

# ------------------------------------------------------------------
# 10. SM_C19_PREPOP_UNOWNED
# ------------------------------------------------------------------
N=SM_C19_PREPOP_UNOWNED
report "$N"
report "  edit: the store holds a round at init whose record nobody wrote"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  s->rs = rs;
  s->n = n;
}' \
  '  s->rs = rs;
  s->n = n;
  /* C19 withdrawn: the store is not empty at init.  The all-ones
   * name is the earliest the ordinal instantiation orders and no
   * instrument mints it, so the entry stays put; its record is the
   * zeroed one, which self does not appear in. */
  memset(s->data, 0xFF, rs);
  s->cnt = 1;
}'
compile $N
drive $N
if [ "`fals $N 'I3 self possesses'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I3 (self possesses every retained round)" $r

# ------------------------------------------------------------------
# 11. SM_C19_PREPOP_ALLN
# ------------------------------------------------------------------
N=SM_C19_PREPOP_ALLN
report "$N"
report "  edit: the store holds a round at init already possessed by all n"
prep $N
mutate "$Work/$N/systemStore.c" \
  '  s->rs = rs;
  s->n = n;
}' \
  '  s->rs = rs;
  s->n = n;
  /* C19 withdrawn: the store is not empty at init, and the round it
   * holds is one release fires exactly at */
  memset(s->data, 0xFF, rs);
  memset(s->data + rs, 0xFF, ((unsigned long)n + 8) / 8);
  s->cnt = 1;
}'
compile $N
drive $N
if [ "`fals $N 'I8 release exactly at all-n'`" -gt 0 ] \
|| [ "`fals $N 'I4 retained short of all n'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I8 (the release accounting is wrong from the start)" $r

# ------------------------------------------------------------------
# 12. SM_C20_MINT_EQUAL  (the DRIVER, not the store)
# ------------------------------------------------------------------
N=SM_C20_MINT_EQUAL
report "$N"
report "  edit: the close mints the closed round's own name as its successor"
prepDrv $N
mutate "$Work/$N/driver.c" \
  '          nacts = systemComplete(v, systemFrontier(v),
                                 rn(rv(systemFrontier(v)) + 1), 0, out);' \
  '          nacts = systemComplete(v, systemFrontier(v),
                                 rn(rv(systemFrontier(v)) + 0), 0, out);'
compileDrv $N
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier:      rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
if [ "`fals $N 'I7 frontier not retained'`" -gt 0 ]; then
  r=1
else
  r=0
fi
verdict $N "falsifier I7 (a successor that does not follow)" $r

# ------------------------------------------------------------------
# 13. SM_C20_MINT_BEHIND  (the DRIVER, not the store)
# ------------------------------------------------------------------
N=SM_C20_MINT_BEHIND
report "$N"
report "  edit: the close mints a name strictly BEHIND the round it closes"
prepDrv $N
mutate "$Work/$N/driver.c" \
  '          nacts = systemComplete(v, systemFrontier(v),
                                 rn(rv(systemFrontier(v)) + 1), 0, out);' \
  '          nacts = systemComplete(v, systemFrontier(v),
                                 rn(rv(systemFrontier(v)) - 1), 0, out);'
compileDrv $N
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier:      rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
report "  `grep '^run 0' $Work/$N/falsifier.log`"
report "  `grep '^run 1' $Work/$N/falsifier.log`"
report "  RECORDED, NOT ASSERTED: no conjunct fires -- the reachable set"
report "  collapses instead (187,550 states clean).  A frontier behind its"
report "  own retained rounds refuses its way to a standstill, and liveness"
report "  is not what these conjuncts state.  See the header."
report ""

# ------------------------------------------------------------------
# the pairing: the UNMUTATED store under the same four instruments
# ------------------------------------------------------------------
# A tier reports kills, and a kill is only worth what the harness's own
# green is worth: if this build/link arrangement reddened an instrument
# by itself, every verdict above would be an artifact.  So the last arm
# mutates nothing and asserts all four run CLEAN through the same
# machinery -- and the falsifier carries -DEXPECTSTATES, so the frozen
# enumeration is pinned here too rather than taken on faith.
N=CLEAN
report "$N pairing -- the unmutated store through the same machinery"
report "  edit: none"
prep $N
compile $N "-DEXPECTSTATES=187550"
drive $N
if [ "$StoreRc" = 0 ] && [ "$ContractRc" = 0 ] && [ "$SeamRc" = 0 ] \
&& [ "$FalsRc" = 0 ]; then
  report "  VERDICT: PASS -- the harness reds nothing on its own"
  report ""
else
  report "  VERDICT: *** THE PAIRING FAILED *** -- an instrument reds with"
  report "           no mutation applied; every kill above is suspect"
  Bad=`expr $Bad + 1`
  report ""
fi

# ------------------------------------------------------------------
# neither the store nor the driver was written
# ------------------------------------------------------------------
After=`shasum -a 256 "$Root/systemStore.c" | awk '{print $1}'`
AfterD=`shasum -a 256 "$Root/test/test_system_invariant.c" | awk '{print $1}'`
report "systemStore.c sha256 after:             $After"
report "test/test_system_invariant.c sha256 after:  $AfterD"
if [ "$Before" != "$After" ] || [ "$BeforeD" != "$AfterD" ]; then
  report "*** FATAL: a file under test CHANGED during the tier ***"
  exit 2
fi
report "systemStore.c and the driver unchanged."

if [ "$Bad" != 0 ]; then
  report "STORE-MUTANT TIER: $Bad mutant(s) produced no red"
  exit 1
fi
report "STORE-MUTANT TIER: every mutant killed by its designated oracle"
exit 0
