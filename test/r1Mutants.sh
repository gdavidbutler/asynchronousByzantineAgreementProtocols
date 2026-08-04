#!/bin/sh
#
# r1Mutants.sh -- the R1 CALLER-HALF MUTANT TIER (2026-08-04).
#
# R1 is a CONJUNCTION.  The machine half (only ADMIT consumes a value,
# MAINTAIN outranks and never consumes, one launch act per opportunity)
# is system.[hc]'s and is covered by the contract suite and the machine
# dispatch's compile-time exclusivity.  The CALLER half -- stage the
# accepted bytes, re-present them byte-identically on every launch,
# retire them only on witnessing the value in an agreed subset -- is
# example/system.c's, and the coverage register recorded it as the
# tree's largest UNCOVERED row: no other instrument stages a value at
# all, so exactly-once presentation had no oracle anywhere.
#
# example/system.c IS that oracle now: it stages, re-presents, retires,
# and its exactly-once verdict quantifies every staged value over every
# correct process's agreed sequence.  Until this tier ran, though, the
# verdict was an UNFALSIFIED WITNESS -- nothing had shown it CATCHING a
# broken caller.  Each entry below copies example/system.c to a scratch
# tree, applies ONE anchored edit withdrawing ONE clause of the caller
# half (the anchor asserted to match EXACTLY ONCE first -- a missed
# anchor is a loud failure, never a silent no-op mutant), rebuilds the
# example against the UNMUTATED machine objects, runs the documented
# configuration that reaches the clause, and asserts the exactly-once
# verdict reds.  example/system.c itself is never written: its checksum
# is taken before and after the whole tier and a difference aborts.
#
# The machine objects are deliberately clean -- the machine is not
# under test here; the caller is.  This tier is machineMutants.sh one
# layer up: the same currency, at the R1 seam the register said nothing
# reached.
#
# THE INVENTORY (expected oracle per mutant):
#
#   M_R1_RETX      retire-on-exclusion: the staged value is retired
#                  whenever it rode a closing round, in or out of the
#                  agreed subset -- withdrawing "retire ONLY on
#                  witnessing the value in an agreed subset" (R2d's
#                  caller face: exclusion must leave it staged).
#                  Config 7 2 3 300: the clause needs a round that
#                  actually EXCLUDES a riding contribution, and that
#                  was measured to be a length property, not a shape
#                  property -- at 3-message runs every tried
#                  configuration (n=7, loss to 30%, seed moves) closed
#                  full subsets and the mutant survived; the wrap-
#                  length n=7 run staggers members as ordinary
#                  operation and kills it (13 values lost at seed 1).
#                  EXPECTED: exactly-once FAIL, values appearing 0
#                  times (retired unwitnessed, never re-presented).
#   M_R1_BYTES     presentation bytes drift with the launch position --
#                  withdrawing "re-present the staged bytes
#                  byte-identically on every launch".  The drifted
#                  bytes stay internally consistent everywhere below
#                  (bank, digest, exchange, sequence agreement), which
#                  is the point: no machine or content gate can catch
#                  this by construction -- the register's precedent
#                  that a caller red needs an oracle comparing GLUE
#                  artifacts, here the staged originals.
#                  EXPECTED: exactly-once FAIL, staged values appearing
#                  0 times while sequence identity stays ok.
#   M_R1_NORETIRE  the retire never fires -- withdrawing the
#                  at-most-once half.  The staged value rides round
#                  after round and lands every time; the run also
#                  never reaches its own completion gate (staged
#                  values outstanding forever), so the harness cap
#                  ends it.
#                  EXPECTED: exactly-once FAIL, the value appearing
#                  many times; run at the MAX_TICKS backstop.
#   M_R1_EARLY     the retire fires on any own-subset close whether or
#                  not the value rode it -- withdrawing the staging
#                  ledger ("the value the close witnessed is the value
#                  retired").  A maintenance round is the reachable
#                  case: it closes with this process in the subset and
#                  NO value riding (O5's precedence), so the mutant
#                  retires a value that was never presented.
#                  Config -m 2 4 1 3 3.
#                  EXPECTED: exactly-once FAIL, skipped values
#                  appearing 0 times.
#
# A CLEAN control runs first: the unmutated scratch copy must stay
# green at every configuration the mutants use, so a red is the
# mutation's and nothing else's.
#
# FIRST RUN 2026-08-04, all four killed: M_R1_RETX 13 values lost at
# 7 2 3 300 (an earlier 7 2 4 3 config SURVIVED it -- exclusion of a
# riding value measured as a length property, full subsets at every
# short-run shape tried); M_R1_BYTES exactly-once FAIL with sequence
# identity still ok (the drift agrees everywhere -- only the
# staged-original comparison catches it); M_R1_NORETIRE p0m0
# appearing 287692 times, 601s at the MAX_TICKS backstop (the tier's
# long pole -- ~10 minutes total is normal, not a hang);
# M_R1_EARLY the -m maintenance closes retiring unpresented values.
# example/system.c checksum identical before and after.
#
# Usage:  sh test/r1Mutants.sh          (or make r1-mutants)
# Env:    CC, CFLAGS, R1LIMIT (per-run watchdog seconds, default 1800)
#
# Artifacts land in r1Mutants.d/ at the repo root (Makefile clean
# removes it).  The script is idempotent: it rebuilds that tree from
# scratch on every run.

set -u

Root=`cd \`dirname "$0"\`/.. && pwd`
Work=$Root/r1Mutants.d
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c89 -pedantic -Wall -Wextra -Os -g}
Limit=${R1LIMIT:-1800}
Summary=$Work/summary.txt

Bad=0
Rc=0
Sec=0

rm -rf "$Work"
mkdir -p "$Work" || exit 2

Before=`shasum -a 256 "$Root/example/system.c" | awk '{print $1}'`
echo "example/system.c sha256 before: $Before" | tee "$Summary"
echo "" >> "$Summary"

# ------------------------------------------------------------------
# mechanism (machineMutants.sh's, verbatim where it applies)
# ------------------------------------------------------------------

anchorCount() {
  R1_A="$2" perl -0777 -ne 'my $c = () = /\Q$ENV{R1_A}\E/g; print $c' "$1"
}

mutate() {
  c=`anchorCount "$1" "$2"`
  if [ "$c" != 1 ]; then
    echo "FATAL: anchor matched $c times (expected 1) in $1" >&2
    echo "--- anchor ---" >&2
    echo "$2" >&2
    exit 2
  fi
  R1_A="$2" R1_R="$3" perl -0777 -i -pe 's/\Q$ENV{R1_A}\E/$ENV{R1_R}/' "$1"
}

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

report() {
  echo "$1" >> "$Summary"
  echo "$1"
}

verdict() {
  if [ "$3" = 1 ]; then
    report "  VERDICT: KILL -- $2"
  else
    report "  VERDICT: *** NO RED *** -- expected $2"
    Bad=`expr $Bad + 1`
  fi
}

# the two verdict lines of a run log
onceLine() {
  grep 'exactly-once presentation' "$1" 2>/dev/null | head -1
}
seqLine() {
  grep 'sequence identity' "$1" 2>/dev/null | head -1
}

# fresh scratch copy of the caller
prep() {
  mkdir -p "$Work/$1" || exit 2
  cp "$Root/example/system.c" "$Work/$1/system.c" || exit 2
}

# compile the (possibly mutated) caller against the CLEAN machine
compile() {
  d=$Work/$1
  $CC $CFLAGS -I"$Root" -o "$d/example_system" "$d/system.c" \
    "$Work/system.o" "$Work/bkr94acs.o" "$Work/bracha87.o" \
    > "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 did not compile -- see $d/build.log" >&2
    exit 2
  fi
  if [ -s "$d/build.log" ]; then
    echo "  note: compiler diagnostics in $d/build.log"
  fi
}

# ------------------------------------------------------------------
# the clean machine objects, built once from the tree's own sources
# ------------------------------------------------------------------
for f in system bkr94acs bracha87; do
  $CC $CFLAGS -I"$Root" -c -o "$Work/$f.o" "$Root/$f.c" \
    > "$Work/$f.build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $f.c did not compile -- see $Work/$f.build.log" >&2
    exit 2
  fi
done

# ------------------------------------------------------------------
# 0. CLEAN control -- the unmutated scratch must be green at every
#    configuration a mutant below uses
# ------------------------------------------------------------------
N=CLEAN
report "$N control"
prep $N
compile $N
Ok=1
for cfg in "4 1 3 3" "7 2 3 300" "-m 2 4 1 3 3" "4 1 3 1"; do
  runLog "$Work/$N/run.log" "$Work/$N/example_system" $cfg
  report "  $cfg: rc $Rc, ${Sec}s -- `onceLine $Work/$N/run.log`"
  if [ "$Rc" != 0 ]; then Ok=0; fi
done
if [ "$Ok" = 1 ]; then
  report "  VERDICT: PASS -- clean caller green at all mutant configs"
else
  report "  VERDICT: *** CONTROL FAILED *** -- reds below prove nothing"
  Bad=`expr $Bad + 1`
fi

# ------------------------------------------------------------------
# 1. M_R1_RETX -- retire on exclusion
# ------------------------------------------------------------------
N=M_R1_RETX
report "$N"
report "  edit: the retire drops its agreed-subset conjunct"
prep $N
mutate "$Work/$N/system.c" \
  '  if (comp[ANCHOR + p->self] && s->rode != NO_MSG
   && s->rode == p->msgHead)
    ++p->msgHead;' \
  '  if (s->rode != NO_MSG
   && s->rode == p->msgHead)
    ++p->msgHead;'
compile $N
runLog "$Work/$N/run.log" "$Work/$N/example_system" 7 2 3 300
report "  7 2 3 300: rc $Rc, ${Sec}s"
report "    `onceLine $Work/$N/run.log`"
report "    `grep 'appears 0 times' $Work/$N/run.log | head -2 | tr '\n' ';'`"
F=`grep -c 'exactly-once presentation (R1):              FAIL' "$Work/$N/run.log"`
Z=`grep -c 'appears 0 times' "$Work/$N/run.log"`
if [ "$F" -gt 0 ] && [ "$Z" -gt 0 ] && [ "$Rc" != 0 ]; then r=1; else r=0; fi
verdict $N "exactly-once FAIL with values appearing 0 times" $r

# ------------------------------------------------------------------
# 2. M_R1_BYTES -- presentation bytes drift with the launch position
# ------------------------------------------------------------------
N=M_R1_BYTES
report "$N"
report "  edit: the presented copy is salted with the round position"
prep $N
mutate "$Work/$N/system.c" \
  '        memcpy(val, p->msg[p->msgHead], VLEN);
        s->rode = p->msgHead;' \
  '        memcpy(val, p->msg[p->msgHead], VLEN);
        val[VLEN - 2] ^= (unsigned char)(s->pos + 1);
        s->rode = p->msgHead;'
compile $N
runLog "$Work/$N/run.log" "$Work/$N/example_system" 4 1 3 3
report "  4 1 3 3: rc $Rc, ${Sec}s"
report "    `seqLine $Work/$N/run.log`"
report "    `onceLine $Work/$N/run.log`"
F=`grep -c 'exactly-once presentation (R1):              FAIL' "$Work/$N/run.log"`
Z=`grep -c 'appears 0 times' "$Work/$N/run.log"`
S=`grep -c 'sequence identity (L6, positions both hold): ok' "$Work/$N/run.log"`
if [ "$F" -gt 0 ] && [ "$Z" -gt 0 ] && [ "$Rc" != 0 ]; then r=1; else r=0; fi
verdict $N "exactly-once FAIL (sequence identity ok: $S -- the drift agrees everywhere)" $r

# ------------------------------------------------------------------
# 3. M_R1_NORETIRE -- the retire never fires
# ------------------------------------------------------------------
N=M_R1_NORETIRE
report "$N"
report "  edit: witnessing no longer retires the staged value"
prep $N
mutate "$Work/$N/system.c" \
  '  if (comp[ANCHOR + p->self] && s->rode != NO_MSG
   && s->rode == p->msgHead)
    ++p->msgHead;' \
  '  if (comp[ANCHOR + p->self] && s->rode != NO_MSG
   && s->rode == p->msgHead)
    (void)p->msgHead;'
compile $N
runLog "$Work/$N/run.log" "$Work/$N/example_system" 4 1 3 1
report "  4 1 3 1: rc $Rc, ${Sec}s (expected to run to the MAX_TICKS backstop)"
report "    `onceLine $Work/$N/run.log`"
report "    `grep 'appears' $Work/$N/run.log | head -1`"
F=`grep -c 'exactly-once presentation (R1):              FAIL' "$Work/$N/run.log"`
M=`grep 'appears' "$Work/$N/run.log" | head -1 | sed 's/.*appears \([0-9]*\) times.*/\1/'`
if [ "$F" -gt 0 ] && [ "${M:-0}" -gt 1 ] && [ "$Rc" != 0 ]; then r=1; else r=0; fi
verdict $N "exactly-once FAIL with the value appearing more than once" $r

# ------------------------------------------------------------------
# 4. M_R1_EARLY -- retire without the value having ridden
# ------------------------------------------------------------------
N=M_R1_EARLY
report "$N"
report "  edit: any own-subset close retires, riding or not"
prep $N
mutate "$Work/$N/system.c" \
  '  if (comp[ANCHOR + p->self] && s->rode != NO_MSG
   && s->rode == p->msgHead)
    ++p->msgHead;' \
  '  if (comp[ANCHOR + p->self] && p->msgHead < p->msgCnt)
    ++p->msgHead;'
compile $N
runLog "$Work/$N/run.log" "$Work/$N/example_system" -m 2 4 1 3 3
report "  -m 2 4 1 3 3: rc $Rc, ${Sec}s"
report "    `onceLine $Work/$N/run.log`"
report "    `grep 'appears 0 times' $Work/$N/run.log | head -2 | tr '\n' ';'`"
F=`grep -c 'exactly-once presentation (R1):              FAIL' "$Work/$N/run.log"`
Z=`grep -c 'appears 0 times' "$Work/$N/run.log"`
if [ "$F" -gt 0 ] && [ "$Z" -gt 0 ] && [ "$Rc" != 0 ]; then r=1; else r=0; fi
verdict $N "exactly-once FAIL with skipped values appearing 0 times" $r

# ------------------------------------------------------------------
# the caller was never written
# ------------------------------------------------------------------
After=`shasum -a 256 "$Root/example/system.c" | awk '{print $1}'`
report ""
report "example/system.c sha256 after:  $After"
if [ "$Before" != "$After" ]; then
  report "*** FATAL: example/system.c CHANGED during the tier ***"
  exit 2
fi
report "example/system.c unchanged."

if [ "$Bad" != 0 ]; then
  report "R1 CALLER-HALF TIER: $Bad mutant(s) produced no red"
  exit 1
fi
report "R1 CALLER-HALF TIER: every mutant killed by the exactly-once oracle"
exit 0
