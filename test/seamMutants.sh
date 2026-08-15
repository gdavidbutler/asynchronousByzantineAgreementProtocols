#!/bin/sh
#
# seamMutants.sh -- THE GLUE-MUTANT MATRIX, made re-runnable (2026-08-14).
#
# The composed seam carries 22 glue mutants (-DM_*), each breaking one
# caller obligation so that a designated check catches it.  "22 of 22
# FIRE" was the seam header's claim, and it was true when it was
# measured (2026-07-25).  What it had no way to be was CHECKED: the
# repro token for a glue mutant is one hand-built binary at a time, so
# the matrix sat through three landings -- the step-2 and round-turn
# relocation, the round abstraction, the retention seam -- without
# being re-run once.  Each of those re-verified the seam's three CONFIG
# baselines byte-exact and left the mutant matrix alone, which is
# exactly the shape of a claim that goes stale silently.
#
# The wider validation read of 2026-08-14 re-ran it by hand and found
# all 22 still firing, with three profile drifts recorded in the seam
# header.  This script is that read's remedy: the matrix is a target
# now, so the next landing either keeps it true or is told.
#
# WHAT IS ASSERTED PER MUTANT, and it is deliberately ONE thing: that
# the mutant's DESIGNATED check fires.  Totals are recorded and never
# asserted -- they move with any RNG-stream shift (the recovery-leg
# precedent), and a tier that asserted them would red on every
# unrelated landing and teach the reader to ignore it.  What may not
# move is WHICH arm catches the broken glue.
#
# A CLEAN control runs first and must be 42804/0 at 16 seeds -- the
# default config's frozen baseline.  Without it a build that reds by
# itself would read as 22 kills.
#
# Usage:  sh test/seamMutants.sh          (or make seam-mutants)
# Env:    CC, CFLAGS, SEEDS (default 16), SMLIMIT (per-run watchdog
#         seconds, default 900)
#
# Artifacts land in seamMutants.d/ at the repo root (Makefile clean
# removes it).  Idempotent: the tree is rebuilt from scratch each run.
# The seam source is never written -- it is checksummed before and
# after, like every other tier here.

set -u

Root=`cd \`dirname "$0"\`/.. && pwd`
Work=$Root/seamMutants.d
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c89 -pedantic -Wall -Wextra -Os -g}
Seeds=${SEEDS:-16}
Limit=${SMLIMIT:-900}
Summary=$Work/summary.txt

Bad=0
Rc=0
Sec=0

rm -rf "$Work"
mkdir -p "$Work" || exit 2

for f in system.o systemStore.o bkr94acs.o bracha87.o; do
  if [ ! -f "$Root/$f" ]; then
    echo "FATAL: $Root/$f is missing -- run make first" >&2
    exit 2
  fi
done

Before=`shasum -a 256 "$Root/test/test_system_seam.c" | awk '{print $1}'`
echo "test/test_system_seam.c sha256 before: $Before" | tee "$Summary"
echo "" >> "$Summary"

report() {
  echo "$1" >> "$Summary"
  echo "$1"
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
  ' "$Limit" "$@" > "$log" 2>&1 < /dev/null
  Rc=$?
  t1=`date +%s`
  Sec=`expr $t1 - $t0`
}

build() {
  $CC $CFLAGS ${2:-} -I"$Root" -o "$Work/$1" \
    "$Root/test/test_system_seam.c" "$Root/system.o" "$Root/systemStore.o" \
    "$Root/bkr94acs.o" "$Root/bracha87.o" > "$Work/$1.build" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 did not build -- see $Work/$1.build" >&2
    exit 2
  fi
}

counts() {
  grep 'checks,' "$1" 2>/dev/null | tail -1
}

# the scenarios a mutant reddened, with counts
scenarios() {
  grep -o '^FAIL \[[^]]*\]' "$1" 2>/dev/null \
  | sed 's/^FAIL \[//; s/ seed [0-9]*\]//' \
  | sort | uniq -c \
  | awk '{printf "%s x%d ", $2, $1}'
}

# ------------------------------------------------------------------
# the CLEAN control
# ------------------------------------------------------------------
report "CLEAN control -- the unmutated glue at the default config"
build CLEAN
runLog "$Work/CLEAN.log" "$Work/CLEAN" $Seeds
report "  rc $Rc, ${Sec}s, `counts $Work/CLEAN.log`"
if [ "$Rc" = 0 ]; then
  report "  VERDICT: PASS"
else
  report "  VERDICT: *** THE CONTROL FAILED *** -- every kill below is suspect"
  Bad=`expr $Bad + 1`
fi
report ""

# ------------------------------------------------------------------
# the matrix: one mutant per line, with the check it must fire
#
# The pairing IS the inventory -- the seam header explains what each
# mutant breaks and why that check is the one that catches it.  Where a
# mutant fires several arms, the one named here is the one the header
# calls its designated check; the rest are recorded per run.
#
# TWO OF THE TWENTY-TWO ARE TRIPWIRE-GRADE and the table says so rather
# than hiding it behind a check name that sounds like a property.  A
# tripwire is a counter that moves only inside the block the mutation
# edits, so a deployment with the same defect and no self-report passes
# it (the seam header's own definition, from the 2026-07-20 review that
# demoted four decoration arms):
#   M_SEAM_NOHOLD  no behavioral check falls -- BPR resends what the
#                  glue discarded, so the hold discipline is not
#                  liveness-load-bearing here.  It is Model-load-bearing
#                  (hold-unverified), which this instrument cannot see
#                  without crypto.  Its two arms both count the discard.
#   M_SEAM_FREE    caught by its own rogue-launch counter alone;
#                  NEITHER D arm falls, which is correct and is why the
#                  2026-08-14 read split the counter out of D's arm.
# Both are kept: a tripwire kill is worth having.  What it is not worth
# is being counted as the arm that would have caught a silent defect.
# ------------------------------------------------------------------
while IFS='|' read -r m arm; do
  [ -z "$m" ] && continue
  report "$m"
  build "$m" "-D$m"
  runLog "$Work/$m.log" "$Work/$m" $Seeds
  n=`grep -c "$arm" "$Work/$m.log"`
  report "  rc $Rc, ${Sec}s, `counts $Work/$m.log`"
  report "  scenarios: `scenarios $Work/$m.log`"
  report "  designated: \"$arm\" x$n"
  if [ "$n" -gt 0 ]; then
    report "  VERDICT: KILL"
  else
    report "  VERDICT: *** NO RED *** -- the designated check did not fire"
    Bad=`expr $Bad + 1`
  fi
  report ""
done <<EOF
M_SEAM_DROP|B: serves were born from want evidence
M_SEAM_DELIVER|B: serves were born from want evidence
M_SEAM_WANT|C: every round closed at every non-strand process
M_SEAM_OVERCLAIM|F: no all-n release of a round a process had not closed
M_SEAM_NOHOLD|instrument: hold queue never overflowed
M_SEAM_NOPEND|posture: every classification is an accepted strand
M_SEAM_FREE|tripwire: the glue launched without the machine's answer
M_SEAM_UNBOUND|E: compositions byte-identical across processes
M_SEAM_STALE|H: every close advanced the frontier
M_SEAM_NOVOID|BYZ MIXED: no correct process closed on the fabricated variant
M_LEG_NORETRY|C: every round closed at every non-strand process
M_LEG_LOCALRETIRE|B: the held process closed by adoption
M_LEG_MISCLASS|B: the held process closed by adoption
M_EXCH_MISCLASS|I: every closer holds every in-subset member's content
M_EXCH_EARLYRETIRE|I: every closer holds every in-subset member's content
M_EXCH_NOASSEMBLE|I: the machine was told of content delivered while retained
M_INJ_ATTRIB|oracle: injector-on state byte-identical to injector-off
M_INJ_WITNESS|oracle: injector-on state byte-identical to injector-off
M_INJ_CANDIDATE|oracle: injector-on state byte-identical to injector-off
M_INJ_LEG|oracle: injector-on state byte-identical to injector-off
M_INJ_EXCH|oracle: injector-on state byte-identical to injector-off
M_INJ_POSSESS|oracle: injector-on state byte-identical to injector-off
EOF

# ------------------------------------------------------------------
# the seam source was never written
# ------------------------------------------------------------------
After=`shasum -a 256 "$Root/test/test_system_seam.c" | awk '{print $1}'`
report "test/test_system_seam.c sha256 after:  $After"
if [ "$Before" != "$After" ]; then
  report "*** FATAL: the seam source CHANGED during the run ***"
  exit 2
fi
report "test/test_system_seam.c unchanged."

if [ "$Bad" != 0 ]; then
  report "GLUE-MUTANT MATRIX: $Bad arm(s) did not fire"
  exit 1
fi
report "GLUE-MUTANT MATRIX: all 22 mutants fired their designated check"
exit 0
