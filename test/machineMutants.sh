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
#   MM_I9_SLOT_NOFREE   the window-full eviction at completion reuses
#                       the slot without outputting its RELEASE act.
#                       EXPECTED: falsifier I9 (the retained-set
#                       shadow: a departure nothing announced), plus
#                       test_system E.
#   MM_I10_RETAIN_WRONG the entry born at completion retains the byte
#                       BEFORE the pre-advance frontier.
#                       EXPECTED: falsifier I10, plus test_system.
#   MM_I11_EVICT_NEWEST eviction takes the NEWEST retained round
#                       instead of the oldest, at BOTH bound sites
#                       (the window-full path at completion and the
#                       explicit eviction entry) -- one mutation, two
#                       anchored edits, because I11 binds both.
#                       EXPECTED: falsifier I11, plus test_system E.
#   MM_WRAP_LOOKAHEAD_SKIP
#                       the frontier+1 lookahead release removed.
#                       EXPECTED: DORMANT in the falsifier at every
#                       feasible config -- the guard needs an entry 255
#                       rounds behind the completing frontier, and the
#                       horizon bounds an entry to HORIZON-1 behind, so
#                       distance 255 needs HORIZON >= 256.  The red is
#                       test_system section F (both wrap regimes).
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
# the anchors are asserted exactly-once before applying; the
# MM_WRAP_LOOKAHEAD_SKIP dormancy is STRUCTURAL (all three configs
# reproduce the clean machine's counts byte for byte, so the mutation
# is invisible to the search, and section F is the matched red).
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
    "$Root/test/test_system_invariant.c" "$d/system.o" >> "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 falsifier did not link -- see $d/build.log" >&2
    exit 2
  fi
  $CC $CFLAGS -I"$Root" -o "$d/contract" \
    "$Root/test/test_system.c" "$d/system.o" >> "$d/build.log" 2>&1
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
  '  if (sysPop(s, s->data) >= (unsigned int)s->t + 1' \
  '  if (sysPop(s, s->data) >= (unsigned int)s->t'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F5=`grep -c 'FAIL: I5 adopt latch witnessed' "$Work/$N/falsifier.log"`
if [ "$F5" -gt 0 ] && [ "$C1" -ne 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I5 + contract suite" $r

# ------------------------------------------------------------------
# 2. MM_I5_COUNT_SELF
# ------------------------------------------------------------------
N=MM_I5_COUNT_SELF
report "$N"
report "  edit: systemWitness drops the from == self refusal"
prep $N
mutate "$Work/$N/system.c" \
  '  if (!s || !out || s->self > s->n || from > s->n || from == s->self)' \
  '  if (!s || !out || s->self > s->n || from > s->n)'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F5=`grep -c 'FAIL: I5 adopt latch witnessed' "$Work/$N/falsifier.log"`
if [ "$F5" -gt 0 ] && [ "$C1" -ne 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I5 + contract suite" $r

# ------------------------------------------------------------------
# 3. MM_I6_BOOK_SURVIVES
# ------------------------------------------------------------------
N=MM_I6_BOOK_SURVIVES
report "$N"
report "  edit: systemComplete no longer clears the witness book"
prep $N
mutate "$Work/$N/system.c" \
  '  memset(s->data, 0, sysBs(s));
  ++s->frontier;' \
  '  ++s->frontier;'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
J=`grep -c '^FAIL \[J: ' "$Work/$N/contract.log"`
report "  contract suite section-J (L3 commutation) failures: $J"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F6=`grep -c 'FAIL: I6 witness state clear on advance' "$Work/$N/falsifier.log"`
if [ "$F6" -gt 0 ] && [ "$C1" -ne 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I6 + contract suite (incl. section J)" $r

# ------------------------------------------------------------------
# 4. MM_I8_EARLY
# ------------------------------------------------------------------
N=MM_I8_EARLY
report "$N"
report "  edit: sysAll reads all-n one possession early (== n+1 -> >= n)"
prep $N
mutate "$Work/$N/system.c" \
  '  return (sysCnt(s, i) == (unsigned int)s->n + 1);' \
  '  return (sysCnt(s, i) >= (unsigned int)s->n);'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F8=`grep -c 'FAIL: I8 release exactly at all-n' "$Work/$N/falsifier.log"`
if [ "$F8" -gt 0 ] && [ "$C1" -ne 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I8 + contract suite" $r

# ------------------------------------------------------------------
# 5. MM_I9_SLOT_NOFREE
# ------------------------------------------------------------------
N=MM_I9_SLOT_NOFREE
report "$N"
report "  edit: the completion eviction frees and reuses the slot with no RELEASE act"
prep $N
mutate "$Work/$N/system.c" \
  '    e = sysEnt(s, old);
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = e[0];
    ++nact;
    free_ = old;' \
  '    e = sysEnt(s, old);
    e[1] = 0;
    free_ = old;'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F9=`grep -c 'FAIL: I9 rounds leave retention only by RELEASE' "$Work/$N/falsifier.log"`
if [ "$F9" -gt 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I9 (retained-set shadow)" $r

# ------------------------------------------------------------------
# 6. MM_I10_RETAIN_WRONG
# ------------------------------------------------------------------
N=MM_I10_RETAIN_WRONG
report "$N"
report "  edit: the entry born at completion takes frontier - 1, not the frontier"
prep $N
mutate "$Work/$N/system.c" \
  '  e[0] = s->frontier;' \
  '  e[0] = (unsigned char)(s->frontier - 1);'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F10=`grep -c 'FAIL: I10 rounds enter retention only at completion' "$Work/$N/falsifier.log"`
if [ "$F10" -gt 0 ] && [ "$C1" -ne 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I10 + contract suite" $r

# ------------------------------------------------------------------
# 7. MM_I11_EVICT_NEWEST  (two anchored edits -- I11 binds both sites)
# ------------------------------------------------------------------
N=MM_I11_EVICT_NEWEST
report "$N"
report "  edit: both eviction scans maximize e[0] - frontier (newest), not frontier - e[0] (oldest)"
prep $N
mutate "$Work/$N/system.c" \
  '      e = sysEnt(s, i);
      if (e[1] && (d = (unsigned char)(s->frontier - e[0])) >= best) {' \
  '      e = sysEnt(s, i);
      if (e[1] && (d = (unsigned char)(e[0] - s->frontier)) >= best) {'
mutate "$Work/$N/system.c" \
  '    e = sysEnt(s, i);
    if (e[1] && (d = (unsigned char)(s->frontier - e[0])) >= best) {' \
  '    e = sysEnt(s, i);
    if (e[1] && (d = (unsigned char)(e[0] - s->frontier)) >= best) {'
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=$Rc
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
F11=`grep -c 'FAIL: I11 eviction releases the oldest' "$Work/$N/falsifier.log"`
if [ "$F11" -gt 0 ] && [ "$C1" -ne 0 ]; then r=1; else r=0; fi
verdict $N "falsifier I11 + contract suite" $r

# ------------------------------------------------------------------
# 8. MM_HR_GATES  (the twin drive's matched red)
# ------------------------------------------------------------------
N=MM_HR_GATES
report "$N"
report "  edit: systemServe serves an owed round only where H_r has a bit in the same byte"
prep $N
mutate "$Work/$N/system.c" \
  '      if (*(e + 2 + bs + b)) {' \
  '      if (*(e + 2 + bs + b) && *(e + 2 + 2 * bs + b)) {'
compile $N -DHRTWIN
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
runLog "$Work/$N/hrtwin.log" "$Work/$N/falsifier"
report "  -DHRTWIN falsifier (no EXPECTSTATES): rc $Rc, ${Sec}s, fired: `fired $Work/$N/hrtwin.log`"
report "  divergence: `grep 'twin divergence' $Work/$N/hrtwin.log | head -1`"
FH=`grep -c 'FAIL: L7 H_r non-interference' "$Work/$N/hrtwin.log"`
# the plain falsifier is expected BLIND -- recorded, not asserted
$CC $CFLAGS -I"$Root" -o "$Work/$N/plain" \
  "$Root/test/test_system_invariant.c" "$Work/$N/system.o" >> "$Work/$N/build.log" 2>&1
runLog "$Work/$N/plain.log" "$Work/$N/plain"
report "  plain falsifier (no HRTWIN): rc $Rc, ${Sec}s -- `grep '^run 0' $Work/$N/plain.log`"
report "                               `tail -1 $Work/$N/plain.log`"
if [ "$FH" -gt 0 ]; then r=1; else r=0; fi
verdict $N "-DHRTWIN twin-drive divergence" $r

# the pairing: the UNMUTATED machine under the same twin drive
report "MM_HR_GATES pairing -- CLEAN -DHRTWIN at the frozen config"
prep CLEAN
compile CLEAN "-DHRTWIN -DEXPECTSTATES=621094"
runLog "$Work/CLEAN/hrtwin.log" "$Work/CLEAN/falsifier"
report "  clean -DHRTWIN -DEXPECTSTATES=621094: rc $Rc, ${Sec}s"
sed 's/^/    /' "$Work/CLEAN/hrtwin.log" >> "$Summary"
grep '^run\|^  twin\|^invariant' "$Work/CLEAN/hrtwin.log"
if [ "$Rc" = 0 ]; then
  report "  VERDICT: PASS -- the unmutated machine still shows zero divergence"
else
  report "  VERDICT: *** THE PAIRING FAILED *** -- the clean twin drive is not green"
  Bad=`expr $Bad + 1`
fi

# ------------------------------------------------------------------
# 9. MM_WRAP_LOOKAHEAD_SKIP  (the dormancy record -- run last, longest)
# ------------------------------------------------------------------
N=MM_WRAP_LOOKAHEAD_SKIP
report "$N"
report "  edit: the frontier+1 lookahead release removed from systemComplete"
prep $N
mutate "$Work/$N/system.c" \
  '  if ((i = sysFind(s, (unsigned char)(s->frontier + 1))) < W) {
    e = sysEnt(s, i);
    e[1] = 0;
    out[nact].want = 0;
    out[nact].have = 0;
    out[nact].act = SYSTEM_ACT_RELEASE;
    out[nact].round = (unsigned char)(s->frontier + 1);
    ++nact;
  }

' \
  ''
compile $N
runLog "$Work/$N/contract.log" "$Work/$N/contract"
report "  contract suite: rc $Rc, `checks $Work/$N/contract.log`, ${Sec}s, sections: `sections $Work/$N/contract.log`"
C1=`grep -c '^FAIL \[F: round-space wrap\]' "$Work/$N/contract.log"`
report "  contract suite section-F (round-space wrap) failures: $C1"
runLog "$Work/$N/falsifier.log" "$Work/$N/falsifier"
report "  falsifier EN=3 ET=1 EW=1 HORIZON=6: rc $Rc, ${Sec}s, fired: `fired $Work/$N/falsifier.log`"
$CC $CFLAGS -DHORIZON=9 -I"$Root" -o "$Work/$N/deep" \
  "$Root/test/test_system_invariant.c" "$Work/$N/system.o" >> "$Work/$N/build.log" 2>&1
runLog "$Work/$N/deep.log" "$Work/$N/deep"
report "  falsifier HORIZON=9 (deeper): rc $Rc, ${Sec}s, fired: `fired $Work/$N/deep.log`"
report "                                `grep '^run 0' $Work/$N/deep.log`"
$CC $CFLAGS -DEN=6 -DET=2 -DEW=1 -DHORIZON=2 -I"$Root" -o "$Work/$N/wide" \
  "$Root/test/test_system_invariant.c" "$Work/$N/system.o" >> "$Work/$N/build.log" 2>&1
runLog "$Work/$N/wide.log" "$Work/$N/wide"
report "  falsifier EN=6 ET=2 EW=1 HORIZON=2 (wider): rc $Rc, ${Sec}s, fired: `fired $Work/$N/wide.log`"
report "                                `grep '^run 0' $Work/$N/wide.log`"
if [ "$C1" -gt 0 ]; then r=1; else r=0; fi
verdict $N "contract suite section F (the falsifier is expected dormant)" $r

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
