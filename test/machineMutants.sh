#!/bin/sh
#
# machineMutants.sh -- the IN-TREE MACHINE-MUTANT TIER (2026-07-25).
#
# The single-seat instruments (test/test_system_invariant.c and
# test/test_system.c) run only against a CORRECT system.c, so nothing
# had ever shown them CATCHING a broken machine.  The invariant
# falsifier's I1-I11 arms and the -DHRTWIN twin drive were, in the
# repo's own words, unfalsified witnesses: no reachable red exists
# without mutating system.c, and instruments do not touch it.
#
# This script closes that.  Each entry below copies system.c to a
# scratch tree, applies ONE anchored edit (the anchor asserted to
# match EXACTLY ONCE first -- a missed anchor is a loud failure, never
# a silent no-op mutant), rebuilds the instruments against the mutated
# copy, runs them, and asserts the DESIGNATED oracle fires.  system.c
# itself is never written: its checksum is taken before and after the
# whole tier and a difference aborts.
#
# The tier is the machine's counterpart to the composed seam's M_*
# glue mutants: the same currency, one layer down.
#
# THE INVENTORY (expected oracle per mutant):
#
#   MM_I5_LATCH_AT_T    the adopt latch fires at t distinct witnesses
#                       instead of t+1.
#                       EXPECTED: falsifier I5, plus test_system H.
#   MM_I5_COUNT_SELF    self accepted into the witness count.
#                       EXPECTED: falsifier I5 (self never among them),
#                       plus test_system H.
#   MM_I6_BOOK_SURVIVES the witness book is not cleared at completion.
#                       EXPECTED: falsifier I6, plus test_system H and
#                       the section-J commutation oracle (a surviving
#                       book makes the two orders reach different
#                       states).
#   MM_I8_EARLY         the all-n release path fires one possession
#                       short of all n.
#                       EXPECTED: falsifier I8, plus test_system C/D
#                       (MEASURED; the build brief predicted E/G --
#                       corrected at Fable verification, and
#                       summary.txt agrees: sections C x1, D x1).
#   MM_I9_DROP_SILENT   the no-room eviction at completion drops the
#                       oldest round through the caller's own release
#                       without outputting its RELEASE act.
#                       EXPECTED: falsifier I9 (the retained-set
#                       shadow: a departure nothing announced), plus
#                       test_system E.
#   MM_I10_RETAIN_WRONG the close asks the store to retain the prior
#                       round's name, not the pre-advance frontier's
#                       -- at BOTH retain calls, since the second one
#                       (after the eviction) would otherwise repair
#                       the first.  (Under the round abstraction
#                       the prior name equals the genesis name at
#                       the FIRST close, so this mutant's first red
#                       fires one close later than the ordinal
#                       arithmetic form did.)
#                       EXPECTED: falsifier I10, plus test_system C
#                       (MEASURED 2026-08-14: C x5, D x2.  The record
#                       said "B, C and D" and B no longer fires; the
#                       suite faults after D, as it has since this
#                       mutant existed).
#   MM_I11_EVICT_NEWEST eviction takes the NEWEST retained round
#                       instead of the oldest, at BOTH bound sites
#                       (the no-room path at completion and the
#                       explicit eviction entry) -- one mutation, two
#                       anchored edits, because I11 binds both.  The
#                       store answers 'the oldest' now, so the edit
#                       walks its enumeration to the end instead of
#                       flipping a comparison.
#                       EXPECTED: falsifier I11, plus test_system E.
#   MM_CURSOR_UNGATED   the cursor walk's possession gate dropped:
#                       cursor want born over an evidenced possession.
#                       EXPECTED: falsifier I2 (possess/want disjoint
#                       -- its FIRST matched red; the walk writes want
#                       exactly where possession is set, self included
#                       by I3), plus test_system K.
#   MM_WRAP_LOOKAHEAD_SKIP
#                       RETIRED with the round-name widening (the byte
#                       dissolution): its target -- the frontier+1 wrap
#                       lookahead release -- no longer exists; positions
#                       never recur, so no structural release guards
#                       the name.  While the byte world stood it was
#                       structurally DORMANT in the falsifier and
#                       test_system section F was its sole red; F now
#                       pins deep retention from the other side.
#   MM_HR_GATES         the SERVE walk gated on the held-members grain
#                       (an owed round is served only where H_r has a
#                       bit in the same byte) -- H_r entering a machine
#                       decision, which L7's second half forbids.
#                       EXPECTED: the -DHRTWIN twin drive DIVERGES.
#                       The plain falsifier is BLIND to it by
#                       construction (H_r is outside its alphabet), and
#                       that blindness is the reason the twin drive
#                       exists; it is run here to record the fact.
#                       Paired with a CLEAN -DHRTWIN run in the same
#                       session, which must stay at zero divergence.
#
# FABLE VERIFICATION 2026-07-25: the tier was re-run independently end
# to end -- nine kills, the clean -DHRTWIN pairing green (621,094 /
# 43,711,360 with zero divergence), system.c checksum identical before
# and after, exit 0.  Every mutation was inspected against its
# conjunct's countermodel shape in system.md and is what it claims;
# the anchors are asserted exactly-once before applying.
# ROUND-NAME WIDENING (the byte dissolution): the anchors were
# re-pointed at the position-keyed entry layout (e[SYSTEM_PS] in-use,
# sysRnd/sysRndSet for the position field), MM_WRAP_LOOKAHEAD_SKIP was
# retired with its target (record above), and the tier is eight
# mutants; the frozen counts 621,094 / 43,711,360 held byte for byte
# across the widening.
# THE RETENTION SEAM 2026-08-13: the anchors were re-pointed a third
# time, at the store seam -- the retained rounds and their records are
# the CALLER's now, so the mutations land where the machine DIRECTS
# them: sysAll reads the record 'records' handed back, the eviction
# sites ask 'after' for the oldest and announce through sysDrop, the
# close's births are its two 'retain' calls, and both walks (serve and
# cursor) run over 'after'.  Both instruments gain systemStore.o on
# their link lines.  MM_I9_SLOT_NOFREE was RENAMED MM_I9_DROP_SILENT:
# there is no slot to leave unfreed, and what the conjunct is about --
# a departure nothing announced -- is what the name should say.  The
# frozen enumeration re-froze at 187,550 / 20,891,344, SMALLER by
# construction (the derivation is in the falsifier's header: fixed
# slots permuted a retained set, and a freed slot kept its ghost;
# neither exists now).
# ROUND ABSTRACTION 2026-08-10 (same day, superseding the widening's
# encoding): the anchors re-pointed again at the name machine -- the
# witness book behind sysWit(), the comparator sites (sysCmp
# displacement order replacing every position compare), the morgue
# copy in the completion eviction, and memcpy name births replacing
# sysRndSet.  The instruments drive the ordinal instantiation; the
# frozen enumeration re-froze (see EXPECTSTATES below -- the growth is
# the morgue, whose in-use byte the translation-invariance arm itself
# forced: an evicted all-zero name must not merge with the cleared
# morgue).
#
# THE LINE-BY-LINE READ OF THESE NINE ARMS, 2026-08-14, and what it
# changed.  Each mutation was read at its site in system.c against the
# conjunct it names: every one lands where its conjunct lives (the
# adopt guard is I5's subject, the cursor walk's possession gate is
# I2's, the two eviction sites and the two retain calls are I9/I11's
# and I10's), and each designated conjunct is asserted BY NAME, so a
# masked conjunct would show as NO RED rather than passing quietly.
# THIS TIER HAS NO TRIPWIRE PROBLEM by construction, and that is worth
# stating beside the seam's: the oracle is a conjunct over machine
# state read through the public queries, and the clean falsifier is
# green at 187,550 states -- so a conjunct that fires here fired
# because the machine was mutated, and no counter the mutation writes
# can be mistaken for it.
#
# WHAT WAS WEAK: the contract half.  The inventory above names a
# section per mutant, but the verdicts asserted only that the contract
# suite EXITED NON-ZERO -- and three of these nine make it SIGSEGV, so
# the condition was satisfied by a crash, which is not a red.  The
# verdicts now assert the NAMED SECTION (the `sect` helper), which is
# the same correction the composed seam's matrix took the same day.
# MM_I9_DROP_SILENT asserted nothing contract-side at all while its
# entry claimed test_system E; it asserts E now, and E does fire.
#
# DRIFTS the re-run measured, all recorded above beside their
# expectations: MM_I10 lost its B firings, MM_I11 gained section F's
# six (F was re-pointed at deep retention in 2026-08-10), and
# MM_HR_GATES gained section M's one -- the borrows section added the
# same week, which turns out to have teeth against a MUTATED MACHINE
# and not only against a broken store.  M also made this mutant a
# third crashing one until M's dependent arms were guarded on the act
# their claims read; it reds cleanly now.
#
# Usage:  sh test/machineMutants.sh          (or make machine-mutants)
# Env:    CC, CFLAGS, MMLIMIT (per-run watchdog seconds, default 3600)
#
# Artifacts land in machineMutants.d/ at the repo root (Makefile clean
# removes it).  The script is idempotent: it rebuilds that tree from
# scratch on every run.

set -u

Root=`cd \`dirname "$0"\`/.. && pwd`
Work=$Root/machineMutants.d
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c89 -pedantic -Wall -Wextra -Os -g}
Limit=${MMLIMIT:-3600}
Summary=$Work/summary.txt

Bad=0
Rc=0
Sec=0

rm -rf "$Work"
mkdir -p "$Work" || exit 2

Before=`shasum -a 256 "$Root/system.c" | awk '{print $1}'`
echo "system.c sha256 before: $Before" | tee "$Summary"
echo "" >> "$Summary"

# ------------------------------------------------------------------
# mechanism
# ------------------------------------------------------------------

# count occurrences of a literal anchor in a file
anchorCount() {
  MM_A="$2" perl -0777 -ne 'my $c = () = /\Q$ENV{MM_A}\E/g; print $c' "$1"
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
  MM_A="$2" MM_R="$3" perl -0777 -i -pe 's/\Q$ENV{MM_A}\E/$ENV{MM_R}/' "$1"
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
  ' "$Limit" "$@" > "$log" 2>&1
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

# the check/failure line of a test_system log
checks() {
  grep '^test_system: ' "$1" 2>/dev/null | tail -1
}

# which sections of the contract suite fired, with counts
sections() {
  grep '^FAIL \[' "$1" 2>/dev/null \
  | sed 's/^FAIL \[\(.\):.*/\1/' | sort | uniq -c \
  | awk '{printf "%s x%d; ", $2, $1}'
}

# how many times a NAMED contract section fired.  $1 = mutant, $2 = letter.
#
# This replaced a bare "the contract suite exited non-zero" 2026-08-14, in
# the line-by-line read of these arms.  The inventory above names a section
# per mutant, and the verdict has to assert what the inventory claims --
# otherwise a mutant that fires some OTHER section passes on the strength of
# a record that is no longer true.  The sharper reason: THREE of these nine
# make the suite SIGSEGV (its own header says so), and a crash is not a red.
# rc != 0 accepted the crash; a section count cannot.
sect() {
  grep -c "^FAIL \[$2:" "$Work/$1/contract.log" 2>/dev/null
}

# fresh scratch copy of the machine
prep() {
  mkdir -p "$Work/$1" || exit 2
  cp "$Root/system.c" "$Work/$1/system.c" || exit 2
}

# compile the mutated machine and the two instruments against it
compile() {
  d=$Work/$1
  $CC $CFLAGS -I"$Root" -c -o "$d/system.o" "$d/system.c" > "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 did not compile -- see $d/build.log" >&2
    exit 2
  fi
  $CC $CFLAGS ${2:-} -I"$Root" -o "$d/falsifier" \
    "$Root/test/test_system_invariant.c" "$d/system.o" "$Root/systemStore.o" \
    >> "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 falsifier did not link -- see $d/build.log" >&2
    exit 2
  fi
  $CC $CFLAGS -I"$Root" -o "$d/contract" \
    "$Root/test/test_system.c" "$d/system.o" "$Root/systemStore.o" \
    >> "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 contract suite did not link -- see $d/build.log" >&2
    exit 2
  fi
  if [ -s "$d/build.log" ]; then
    echo "  note: compiler diagnostics in $d/build.log"
  fi
}

report() {
  echo "$1" >> "$Summary"
  echo "$1"
}

# assert a designated oracle fired; $1 = mutant, $2 = what, $3 = 1/0
verdict() {
  if [ "$3" = 1 ]; then
    report "  VERDICT: KILL -- $2"
  else
    report "  VERDICT: *** NO RED *** -- expected $2"
    Bad=`expr $Bad + 1`
  fi
}

# ------------------------------------------------------------------
# 1. MM_I5_LATCH_AT_T
# ------------------------------------------------------------------
N=MM_I5_LATCH_AT_T
report "$N"
report "  edit: systemWitness adopt guard >= t + 1  ->  >= t"
prep $N
mutate "$Work/$N/system.c" \
  '  if (sysPop(s, sysWit(s)) >= (unsigned int)s->t + 1' \
  '  if (sysPop(s, sysWit(s)) >= (unsigned int)s->t'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F5=`grep -c 'FAIL: I5 adopt latch witnessed' "$Work/$N/falsifier.log"`
S1=`sect $N H`
if [ "$F5" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I5 + contract section H (x$S1)" $r

# ------------------------------------------------------------------
# 2. MM_I5_COUNT_SELF
# ------------------------------------------------------------------
N=MM_I5_COUNT_SELF
report "$N"
report "  edit: systemWitness drops the from == self refusal"
prep $N
mutate "$Work/$N/system.c" \
  '  if (!s || !round || !out || s->self > s->n || from > s->n
   || from == s->self)' \
  '  if (!s || !round || !out || s->self > s->n || from > s->n)'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F5=`grep -c 'FAIL: I5 adopt latch witnessed' "$Work/$N/falsifier.log"`
S1=`sect $N H`
if [ "$F5" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I5 + contract section H (x$S1)" $r

# ------------------------------------------------------------------
# 3. MM_I6_BOOK_SURVIVES
# ------------------------------------------------------------------
N=MM_I6_BOOK_SURVIVES
report "$N"
report "  edit: systemComplete no longer clears the witness book"
prep $N
mutate "$Work/$N/system.c" \
  '  memset(sysWit(s), 0, bs);
  /*
   * Advance:' \
  '  /*
   * Advance:'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
J=`grep -c '^FAIL \[J: ' "$Work/$N/contract.log"`
report "  contract suite section-J (L3 commutation) failures: $J"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F6=`grep -c 'FAIL: I6 witness state clear on advance' "$Work/$N/falsifier.log"`
S1=`sect $N J`
if [ "$F6" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I6 + contract section J, the commutation oracle (x$S1)" $r

# ------------------------------------------------------------------
# 4. MM_I8_EARLY
# ------------------------------------------------------------------
N=MM_I8_EARLY
report "$N"
report "  edit: sysAll reads all-n one possession early (== n+1 -> >= n)"
prep $N
mutate "$Work/$N/system.c" \
  '  return (sysPop(s, r) == (unsigned int)s->n + 1);' \
  '  return (sysPop(s, r) >= (unsigned int)s->n);'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F8=`grep -c 'FAIL: I8 release exactly at all-n' "$Work/$N/falsifier.log"`
S1=`sect $N C`
if [ "$F8" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I8 + contract section C (x$S1; the suite then faults -- see the header)" $r

# ------------------------------------------------------------------
# 5. MM_I9_DROP_SILENT
# ------------------------------------------------------------------
N=MM_I9_DROP_SILENT
report "$N"
report "  edit: the completion eviction drops the oldest round with no RELEASE act"
prep $N
mutate "$Work/$N/system.c" \
  '    if ((*s->after)(s->ctx, 0, &nm)) {
      out[nact].want = 0;
      out[nact].have = 0;
      out[nact].act = SYSTEM_ACT_RELEASE;
      out[nact].round = sysDrop(s, nm);
      ++nact;
    }' \
  '    if ((*s->after)(s->ctx, 0, &nm))
      (void)sysDrop(s, nm);'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F9=`grep -c 'FAIL: I9 rounds leave retention only by RELEASE' "$Work/$N/falsifier.log"`
S1=`sect $N E`
if [ "$F9" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I9 (retained-set shadow) + contract section E (x$S1)" $r

# ------------------------------------------------------------------
# 6. MM_I10_RETAIN_WRONG
# ------------------------------------------------------------------
N=MM_I10_RETAIN_WRONG
report "$N"
report "  edit: the entry born at completion takes the prior round's name, not the frontier's"
prep $N
mutate "$Work/$N/system.c" \
  '  rec = (*s->retain)(s->ctx, sysFro(s));

  roundClass = SYSTEM_ROUND_RELEASED;' \
  '  rec = (*s->retain)(s->ctx, sysPri(s));

  roundClass = SYSTEM_ROUND_RELEASED;'
mutate "$Work/$N/system.c" \
  '    rec = (*s->retain)(s->ctx, sysFro(s));
  }' \
  '    rec = (*s->retain)(s->ctx, sysPri(s));
  }'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F10=`grep -c 'FAIL: I10 rounds enter retention only at completion' "$Work/$N/falsifier.log"`
S1=`sect $N C`
if [ "$F10" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I10 + contract section C (x$S1; the suite then faults -- see the header)" $r

# ------------------------------------------------------------------
# 7. MM_I11_EVICT_NEWEST  (two anchored edits -- I11 binds both sites)
# ------------------------------------------------------------------
N=MM_I11_EVICT_NEWEST
report "$N"
report "  edit: both eviction scans take the latest name (newest), not the earliest (oldest)"
prep $N
mutate "$Work/$N/system.c" \
  '    if ((*s->after)(s->ctx, 0, &nm)) {
      out[nact].want = 0;' \
  '    if ((*s->after)(s->ctx, 0, &nm)) {
      { const unsigned char *nw; while ((*s->after)(s->ctx, nm, &nw)) nm = nw; }
      out[nact].want = 0;'
mutate "$Work/$N/system.c" \
  '  if (!(*s->after)(s->ctx, 0, &nm))
    return (0);
  memset(sysScr(s), 0, s->rs + 1u);' \
  '  if (!(*s->after)(s->ctx, 0, &nm))
    return (0);
  { const unsigned char *nw; while ((*s->after)(s->ctx, nm, &nw)) nm = nw; }
  memset(sysScr(s), 0, s->rs + 1u);'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F11=`grep -c 'FAIL: I11 eviction releases the oldest' "$Work/$N/falsifier.log"`
S1=`sect $N E`
if [ "$F11" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I11 + contract section E (x$S1)" $r

# ------------------------------------------------------------------
# 8. MM_HR_GATES  (the twin drive's matched red)
# ------------------------------------------------------------------
N=MM_HR_GATES
report "$N"
report "  edit: systemServe serves an owed round only where H_r has a bit in the same byte"
prep $N
mutate "$Work/$N/system.c" \
  '        if (*(rec + bs + b)) {' \
  '        if (*(rec + bs + b) && *(rec + 2 * bs + b)) {'
compile $N -DHRTWIN
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/hrtwin.log" "$Work/$N/falsifier"
report "  -DHRTWIN falsifier (no EXPECTSTATES): rc $Rc, ${Sec}s, fired: `fired $Work/$N/hrtwin.log`"
report "  divergence: `grep 'twin divergence' $Work/$N/hrtwin.log | head -1`"
FH=`grep -c 'FAIL: L7 H_r non-interference' "$Work/$N/hrtwin.log"`
# the plain falsifier is expected BLIND -- recorded, not asserted
$CC $CFLAGS -I"$Root" -o "$Work/$N/plain" \
  "$Root/test/test_system_invariant.c" "$Work/$N/system.o" "$Root/systemStore.o" \
  >> "$Work/$N/build.log" 2>&1
runLog "$Work/$N/plain.log" "$Work/$N/plain"
report "  plain falsifier (no HRTWIN): rc $Rc, ${Sec}s -- `grep '^run 0' $Work/$N/plain.log`"
report "                               `tail -1 $Work/$N/plain.log`"
if [ "$FH" -gt 0 ]; then r=1; else r=0; fi
verdict $N "-DHRTWIN twin-drive divergence" $r

# the pairing: the UNMUTATED machine under the same twin drive
report "MM_HR_GATES pairing -- CLEAN -DHRTWIN at the frozen config"
prep CLEAN
compile CLEAN "-DHRTWIN -DEXPECTSTATES=187550"
runLog "$Work/CLEAN/hrtwin.log" "$Work/CLEAN/falsifier"
report "  clean -DHRTWIN -DEXPECTSTATES=187550: rc $Rc, ${Sec}s"
sed 's/^/    /' "$Work/CLEAN/hrtwin.log" >> "$Summary"
grep '^run\|^  twin\|^invariant' "$Work/CLEAN/hrtwin.log"
if [ "$Rc" = 0 ]; then
  report "  VERDICT: PASS -- the unmutated machine still shows zero divergence"
else
  report "  VERDICT: *** THE PAIRING FAILED *** -- the clean twin drive is not green"
  Bad=`expr $Bad + 1`
fi

# ------------------------------------------------------------------
# 9. MM_CURSOR_UNGATED
# ------------------------------------------------------------------
N=MM_CURSOR_UNGATED
report "$N"
report "  edit: the cursor walk drops the sender-possession gate"
prep $N
mutate "$Work/$N/system.c" \
  '      if (!SYSTEM_TST(r2, from))
        SYSTEM_SET(r2 + bs, from);' \
  '      SYSTEM_SET(r2 + bs, from);'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 ER=2 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F2=`grep -c 'FAIL: I2 possess/want disjoint' "$Work/$N/falsifier.log"`
S1=`sect $N K`
if [ "$F2" -gt 0 ] && [ "$S1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I2 (first matched red) + contract section K (x$S1)" $r

# ------------------------------------------------------------------
# 10. MM_WRAP_LOOKAHEAD_SKIP -- RETIRED (round-name widening)
# ------------------------------------------------------------------
# Its target, the frontier+1 wrap lookahead release in systemComplete,
# was deleted when the machine's round name widened to a position:
# positions never recur, so no structural release exists to skip.
# The dormancy record and the retirement rationale are in the header
# inventory; test_system section F carries the widened contract's
# deep-retention checks.

# ------------------------------------------------------------------
# the machine was never written
# ------------------------------------------------------------------
After=`shasum -a 256 "$Root/system.c" | awk '{print $1}'`
report ""
report "system.c sha256 after:  $After"
if [ "$Before" != "$After" ]; then
  report "*** FATAL: system.c CHANGED during the tier ***"
  exit 2
fi
report "system.c unchanged."

if [ "$Bad" != 0 ]; then
  report "MACHINE-MUTANT TIER: $Bad mutant(s) produced no red"
  exit 1
fi
report "MACHINE-MUTANT TIER: every mutant killed by its designated oracle"
exit 0
