#!/bin/sh
#
# fidelityMutants.sh -- THE FIDELITY-MUTANT TIER (2026-08-15).
#
# test/test_system_fidelity.c is a SPEC-DIRECT correspondence: a second
# reading of system.md, written from the specification and system.h
# alone, driven in lockstep with the machine over an exhaustive
# enumeration of the product's reachable states.  It runs green at
# pinned counts, and a green correspondence is worth exactly what its
# TEETH are worth -- which nothing in the tree could re-run.  Its
# builder probed five one-off mutations of the reference and saw each
# red, and that probe is the kind of claim this repo has watched go
# stale: "22 of 22 fire" sat through three landings unre-runnable, and
# the finding there was ACCESS, not content.
#
# This script is that access.  It is the machineMutants.sh /
# storeMutants.sh pattern pointed at BOTH SIDES of the correspondence:
# each arm copies its target to a scratch tree, applies ONE anchored
# edit (the anchor asserted to match EXACTLY ONCE first -- a missed
# anchor is a loud failure, never a silent no-op mutant), rebuilds, and
# runs the driver.  NEITHER target is ever written: system.c and
# test/test_system_fidelity.c are checksummed before and after the
# whole tier and a difference aborts.
#
# THE DEFAULT BUILD IS WHAT THE TIER RUNS.  The driver's -DDEEP arm
# (one shape at reach 3, its own pinned counts) exists and is NOT
# tiered: it costs about four times the default run per arm, and the
# default shapes already discriminate every rule these arms withdraw.
#
# ---------------------------------------------------------------
# FAMILY 1 -- THE REFERENCE SIDE (five arms, all asserted)
# ---------------------------------------------------------------
#
# A correspondence that cannot red on a BROKEN REFERENCE cannot be
# trusted to red on a broken machine: both halves feed one comparison,
# and a comparison blind to one input is blind, full stop.  These five
# withdraw one reference decision each, build against the CLEAN machine
# objects, and assert the driver reds -- non-zero exit with a FAIL line
# that is a COMPARISON channel (an act kind, an act count, an act
# round, a query, or the record bytes), not merely the pinned-count or
# coverage lines a stopped enumeration prints on its way out.
#
#   FM_REF_EVICT_NEWEST  the reference's eviction takes the NEWEST
#                        retained round instead of the oldest, at BOTH
#                        of its sites (the no-room withdrawal at a
#                        close and the explicit eviction entry), the
#                        way MM_I11_EVICT_NEWEST binds both on the
#                        machine side.
#   FM_REF_ADOPT_AT_T    the witness latch fires at t distinct servers
#                        instead of t+1.  Shape A (t = 0) cannot see
#                        this -- there t and t+1 name the same first
#                        witness -- so the red comes from shape B,
#                        which is the shape that exists for exactly
#                        that reason.
#   FM_REF_SERVE_NOWRAP  the serve walk never wraps to the oldest owed
#                        round: the second pass runs only where the
#                        first pass already covered everything, so the
#                        wrap is inert.
#   FM_REF_OWED_NOCLEAR  a join does not retire the owed flag.
#   FM_REF_CURSOR_INERT  the cursor-evidence walk writes nothing.
#
# ---------------------------------------------------------------
# FAMILY 2 -- THE MACHINE SIDE (nine arms, measured)
# ---------------------------------------------------------------
#
# The nine anchored mutations of test/machineMutants.sh, carried over
# VERBATIM -- same anchors, same replacements -- applied to a scratch
# copy of system.c, with the CLEAN driver linked against the scratch
# object.  This family is what gives the fidelity oracle its
# credibility against the artifact it actually guards; the reference
# family only says the instrument has teeth at all.
#
# MEASURED, NOT PREDICTED.  The driver stops at the FIRST disagreement,
# so what an arm reports is its earliest reachable divergence and
# nothing more.  ALL NINE RED at these shapes, so all nine are
# asserted -- a landing that blinds one of them shows up here.  Had one
# not, the discipline is the SM_C20_MINT_BEHIND and
# MM_HR_GATES-plain-falsifier precedent: record it with its structural
# reason and assert nothing, because a blindness named is worth more
# than an oracle invented for it and a no-kill must never fail a tier.
#
# The measured mutant-to-channel table is at the FOOT of this file.
#
# ---------------------------------------------------------------
# THE CLEAN PAIRING runs LAST
# ---------------------------------------------------------------
#
# Unmutated reference against unmutated machine, through this same
# scratch build-and-link arrangement, asserted to PASS at the pinned
# counts.  A mutation harness lies UPWARD: a tier of kills and a
# harness that reds by itself look exactly alike, and the pairing is
# what that costs to rule out.
#
# Usage:  sh test/fidelityMutants.sh          (or make fidelity-mutants)
# Env:    CC, CFLAGS, FMLIMIT (per-run watchdog seconds, default 3600)
#
# Artifacts and per-arm logs land in fidelityMutants.d/ at the repo
# root (Makefile clean removes it).  The script is idempotent: it
# rebuilds that tree from scratch on every run.

set -u

Root=`cd \`dirname "$0"\`/.. && pwd`
Work=$Root/fidelityMutants.d
CC=${CC:-cc}
CFLAGS=${CFLAGS:--std=c89 -pedantic -Wall -Wextra -Os -g}
Limit=${FMLIMIT:-3600}
Summary=$Work/summary.txt

Bad=0
Rc=0
Sec=0
Chan=

rm -rf "$Work"
mkdir -p "$Work" || exit 2

for f in system.o systemStore.o; do
  if [ ! -f "$Root/$f" ]; then
    echo "FATAL: $Root/$f is missing -- run make first" >&2
    exit 2
  fi
done

BeforeM=`shasum -a 256 "$Root/system.c" | awk '{print $1}'`
BeforeR=`shasum -a 256 "$Root/test/test_system_fidelity.c" | awk '{print $1}'`
echo "system.c sha256 before:                     $BeforeM" | tee "$Summary"
echo "test/test_system_fidelity.c sha256 before:  $BeforeR" >> "$Summary"
echo "" >> "$Summary"

# ------------------------------------------------------------------
# mechanism (the machineMutants.sh pattern)
# ------------------------------------------------------------------

# count occurrences of a literal anchor in a file
anchorCount() {
  FM_A="$2" perl -0777 -ne 'my $c = () = /\Q$ENV{FM_A}\E/g; print $c' "$1"
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
  FM_A="$2" FM_R="$3" perl -0777 -i -pe 's/\Q$ENV{FM_A}\E/$ENV{FM_R}/' "$1"
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

report() {
  echo "$1" >> "$Summary"
  echo "$1"
}

# THE COMPARISON CHANNEL that fired first.  The driver halts the shape
# at its first disagreement, so this line names WHERE the two readings
# parted -- an act kind, an act count, an act round, a query, or the
# record bytes -- and everything after it is consequence.
firstFail() {
  grep -m1 '^FAIL:' "$1" 2>/dev/null | sed 's/^FAIL: //; s/ -- .*//'
}

# every distinct channel a run reddened, with counts
channels() {
  grep '^FAIL:' "$1" 2>/dev/null \
  | sed 's/^FAIL: //; s/ -- .*//; s/[0-9][0-9]*/N/g' \
  | sort | uniq -c \
  | awk '{c=$1; $1=""; sub(/^ */,""); printf "%s x%d; ", $0, c}'
}

# the per-shape enumeration counts the run printed
shapes() {
  grep '^   states ' "$1" 2>/dev/null \
  | awk '{printf "%s %s %s %s ", $1, $2, $3, $4}'
}

# the run's own check tally
tally() {
  grep '^Checks: ' "$1" 2>/dev/null | tail -1
}

# 1 when the first FAIL is a COMPARISON channel; 0 when it is only a
# pinned-count or coverage line (which a halted enumeration prints on
# its way out, and which is a consequence of the red, not the red)
isCmpChan() {
  case "$1" in
  '')                     echo 0 ;;
  'state count'*)         echo 0 ;;
  'call count'*)          echo 0 ;;
  'an act kind'*)         echo 0 ;;
  'duty class'*)          echo 0 ;;
  *)                      echo 1 ;;
  esac
}

# fresh scratch copy of the reference driver
prepRef() {
  mkdir -p "$Work/$1" || exit 2
  cp "$Root/test/test_system_fidelity.c" "$Work/$1/driver.c" || exit 2
}

# fresh scratch copy of the machine
prepMach() {
  mkdir -p "$Work/$1" || exit 2
  cp "$Root/system.c" "$Work/$1/system.c" || exit 2
}

# the mutated driver against the CLEAN machine objects.  The driver
# reaches its headers as ../system.h, so the scratch copy gets the
# test directory on the include path to resolve that the same way.
compileRef() {
  d=$Work/$1
  $CC $CFLAGS -I"$Root" -I"$Root/test" -o "$d/fidelity" \
    "$d/driver.c" "$Root/system.o" "$Root/systemStore.o" \
    > "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 driver did not build -- see $d/build.log" >&2
    exit 2
  fi
  if [ -s "$d/build.log" ]; then
    echo "  note: compiler diagnostics in $d/build.log"
  fi
}

# the mutated machine with the CLEAN driver
compileMach() {
  d=$Work/$1
  $CC $CFLAGS -I"$Root" -c -o "$d/system.o" "$d/system.c" \
    > "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 did not compile -- see $d/build.log" >&2
    exit 2
  fi
  $CC $CFLAGS -I"$Root" -o "$d/fidelity" \
    "$Root/test/test_system_fidelity.c" "$d/system.o" "$Root/systemStore.o" \
    >> "$d/build.log" 2>&1
  if [ $? -ne 0 ]; then
    echo "FATAL: $1 driver did not link -- see $d/build.log" >&2
    exit 2
  fi
  if [ -s "$d/build.log" ]; then
    echo "  note: compiler diagnostics in $d/build.log"
  fi
}

# run the arm's driver and record what it did
drive() {
  d=$Work/$1
  runLog "$d/run.log" "$d/fidelity"
  Chan=`firstFail "$d/run.log"`
  report "  driver: rc $Rc, ${Sec}s, first channel: ${Chan:-(none)}"
  report "  channels: `channels $d/run.log`"
  report "  enumeration: `shapes $d/run.log`"
  report "  `tally $d/run.log`"
}

# assert a reference-side arm reddened on a comparison channel
refVerdict() {
  n=`grep -c '^FAIL:' "$Work/$1/run.log" 2>/dev/null`
  if [ "$Rc" != 0 ] && [ "$n" -gt 0 ] && [ "`isCmpChan \"$Chan\"`" = 1 ]; then
    report "  VERDICT: KILL -- the correspondence red on '$Chan'"
  else
    report "  VERDICT: *** NO RED *** -- the driver did not part from a"
    report "           broken reference on a comparison channel"
    Bad=`expr $Bad + 1`
  fi
  report ""
}

# a machine-side arm MEASURED to red: assert it, so a landing that
# blinds the correspondence to it shows up here
machKill() {
  n=`grep -c '^FAIL:' "$Work/$1/run.log" 2>/dev/null`
  if [ "$Rc" != 0 ] && [ "$n" -gt 0 ]; then
    report "  VERDICT: KILL -- the correspondence red on '$Chan'"
  else
    report "  VERDICT: *** NO RED *** -- this arm reddened when it was"
    report "           measured; the correspondence has been blinded to it"
    Bad=`expr $Bad + 1`
  fi
  report ""
}

# ==================================================================
# FAMILY 1 -- THE REFERENCE SIDE
# ==================================================================

# ------------------------------------------------------------------
# 1. FM_REF_EVICT_NEWEST  (two anchored edits -- both eviction sites)
# ------------------------------------------------------------------
N=FM_REF_EVICT_NEWEST
report "$N"
report "  edit: the reference evicts the NEWEST retained round, not the"
report "        oldest, at the close's no-room withdrawal and at the"
report "        explicit eviction entry"
prepRef $N
mutate "$Work/$N/driver.c" \
  '            ea[nEa].act = SYSTEM_ACT_RELEASE;
            ea[nEa].round = rp->rnd[0];
            ++nEa;
            for (j = 1; j < rp->cnt; ++j) {' \
  '            ea[nEa].act = SYSTEM_ACT_RELEASE;
            ea[nEa].round = rp->rnd[rp->cnt - 1];
            ++nEa;
            for (j = rp->cnt; j < rp->cnt; ++j) {'
mutate "$Work/$N/driver.c" \
  '          if (!rp->cnt)
            break;
          ea[nEa].act = SYSTEM_ACT_RELEASE;
          ea[nEa].round = rp->rnd[0];
          ++nEa;
          for (j = 1; j < rp->cnt; ++j) {' \
  '          if (!rp->cnt)
            break;
          ea[nEa].act = SYSTEM_ACT_RELEASE;
          ea[nEa].round = rp->rnd[rp->cnt - 1];
          ++nEa;
          for (j = rp->cnt; j < rp->cnt; ++j) {'
compileRef $N
drive $N
refVerdict $N

# ------------------------------------------------------------------
# 2. FM_REF_ADOPT_AT_T
# ------------------------------------------------------------------
N=FM_REF_ADOPT_AT_T
report "$N"
report "  edit: the reference latches ADOPT at t distinct servers, not t+1"
prepRef $N
mutate "$Work/$N/driver.c" \
  '          if (c >= et + 1 && !rp->adopt) {' \
  '          if (c >= et && !rp->adopt) {'
compileRef $N
drive $N
refVerdict $N

# ------------------------------------------------------------------
# 3. FM_REF_SERVE_NOWRAP
# ------------------------------------------------------------------
N=FM_REF_SERVE_NOWRAP
report "$N"
report "  edit: the reference serve walk never wraps to the oldest owed"
report "        round (the wrap pass runs only where the first pass"
report "        already swept everything, so it finds nothing)"
prepRef $N
mutate "$Work/$N/driver.c" \
  '          if (k == rp->cnt && rp->hasCursor)
            for (j = 0; j < rp->cnt && k == rp->cnt; ++j) {' \
  '          if (k == rp->cnt && !rp->hasCursor)
            for (j = 0; j < rp->cnt && k == rp->cnt; ++j) {'
compileRef $N
drive $N
refVerdict $N

# ------------------------------------------------------------------
# 4. FM_REF_OWED_NOCLEAR
# ------------------------------------------------------------------
N=FM_REF_OWED_NOCLEAR
report "$N"
report "  edit: the reference join does not retire the owed flag"
prepRef $N
mutate "$Work/$N/driver.c" \
  '            ea[nEa].act = SYSTEM_ACT_JOIN;
            ea[nEa].round = rp->f;
            ++nEa;
            rp->owed = 0;
            rp->live = 1;' \
  '            ea[nEa].act = SYSTEM_ACT_JOIN;
            ea[nEa].round = rp->f;
            ++nEa;
            rp->live = 1;'
compileRef $N
drive $N
refVerdict $N

# ------------------------------------------------------------------
# 5. FM_REF_CURSOR_INERT
# ------------------------------------------------------------------
N=FM_REF_CURSOR_INERT
report "$N"
report "  edit: the reference cursor-evidence walk writes nothing"
prepRef $N
mutate "$Work/$N/driver.c" \
  '          if ((bits >> 1) & 1)
            for (j = 0; j < rp->cnt; ++j)
              if (ordVCmp(rp->rnd[j], rr) > 0 && !TST(rp->pos[j], fp))
                SET(rp->wnt[j], fp);' \
  '          /* FM_REF_CURSOR_INERT: the walk writes nothing */'
compileRef $N
drive $N
refVerdict $N

# ==================================================================
# FAMILY 2 -- THE MACHINE SIDE
# (the nine anchors and replacements of test/machineMutants.sh,
#  carried over verbatim; a failed exactly-once assertion here means
#  system.c moved under BOTH tiers and both must be re-pointed)
# ==================================================================

# ------------------------------------------------------------------
# 6. FM_MM_I5_LATCH_AT_T
# ------------------------------------------------------------------
N=FM_MM_I5_LATCH_AT_T
report "$N"
report "  edit: systemWitness adopt guard >= t + 1  ->  >= t"
prepMach $N
mutate "$Work/$N/system.c" \
  '  if (sysPop(s, sysWit(s)) >= (unsigned int)s->t + 1' \
  '  if (sysPop(s, sysWit(s)) >= (unsigned int)s->t'
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 7. FM_MM_I5_COUNT_SELF
# ------------------------------------------------------------------
N=FM_MM_I5_COUNT_SELF
report "$N"
report "  edit: systemWitness drops the from == self refusal"
prepMach $N
mutate "$Work/$N/system.c" \
  '  if (!s || !round || !out || s->self > s->n || from > s->n
   || from == s->self)' \
  '  if (!s || !round || !out || s->self > s->n || from > s->n)'
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 8. FM_MM_I6_BOOK_SURVIVES
# ------------------------------------------------------------------
N=FM_MM_I6_BOOK_SURVIVES
report "$N"
report "  edit: systemComplete no longer clears the witness book"
prepMach $N
mutate "$Work/$N/system.c" \
  '  memset(sysWit(s), 0, bs);
  /*
   * Advance:' \
  '  /*
   * Advance:'
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 9. FM_MM_I8_EARLY
# ------------------------------------------------------------------
N=FM_MM_I8_EARLY
report "$N"
report "  edit: sysAll reads all-n one possession early (== n+1 -> >= n)"
prepMach $N
mutate "$Work/$N/system.c" \
  '  return (sysPop(s, r) == (unsigned int)s->n + 1);' \
  '  return (sysPop(s, r) >= (unsigned int)s->n);'
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 10. FM_MM_I9_DROP_SILENT
# ------------------------------------------------------------------
N=FM_MM_I9_DROP_SILENT
report "$N"
report "  edit: the completion eviction drops the oldest round with no"
report "        RELEASE act"
prepMach $N
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
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 11. FM_MM_I10_RETAIN_WRONG
# ------------------------------------------------------------------
N=FM_MM_I10_RETAIN_WRONG
report "$N"
report "  edit: the entry born at completion takes the prior round's name,"
report "        not the frontier's -- at both retain calls"
prepMach $N
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
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 12. FM_MM_I11_EVICT_NEWEST  (two anchored edits -- both bound sites)
# ------------------------------------------------------------------
N=FM_MM_I11_EVICT_NEWEST
report "$N"
report "  edit: both eviction scans take the latest name (newest), not the"
report "        earliest (oldest)"
prepMach $N
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
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 13. FM_MM_HR_GATES
# ------------------------------------------------------------------
N=FM_MM_HR_GATES
report "$N"
report "  edit: systemServe serves an owed round only where H_r has a bit"
report "        in the same byte"
prepMach $N
mutate "$Work/$N/system.c" \
  '        if (*(rec + bs + b)) {' \
  '        if (*(rec + bs + b) && *(rec + 2 * bs + b)) {'
compileMach $N
drive $N
machKill $N

# ------------------------------------------------------------------
# 14. FM_MM_CURSOR_UNGATED
# ------------------------------------------------------------------
N=FM_MM_CURSOR_UNGATED
report "$N"
report "  edit: the cursor walk drops the sender-possession gate"
prepMach $N
mutate "$Work/$N/system.c" \
  '      if (!SYSTEM_TST(r2, from))
        SYSTEM_SET(r2 + bs, from);' \
  '      SYSTEM_SET(r2 + bs, from);'
compileMach $N
drive $N
machKill $N

# ==================================================================
# THE CLEAN PAIRING -- both sides unmutated, same machinery
# ==================================================================
N=CLEAN
report "$N pairing -- the unmutated reference against the unmutated machine"
report "  edit: none"
prepRef $N
prepMach $N
d=$Work/$N
$CC $CFLAGS -I"$Root" -c -o "$d/system.o" "$d/system.c" > "$d/build.log" 2>&1
if [ $? -ne 0 ]; then
  echo "FATAL: CLEAN did not compile -- see $d/build.log" >&2
  exit 2
fi
$CC $CFLAGS -I"$Root" -I"$Root/test" -o "$d/fidelity" \
  "$d/driver.c" "$d/system.o" "$Root/systemStore.o" >> "$d/build.log" 2>&1
if [ $? -ne 0 ]; then
  echo "FATAL: CLEAN driver did not link -- see $d/build.log" >&2
  exit 2
fi
drive $N
if [ "$Rc" = 0 ]; then
  report "  VERDICT: PASS -- the harness reds nothing on its own, and the"
  report "           enumeration stands at its pinned counts"
else
  report "  VERDICT: *** THE PAIRING FAILED *** -- the correspondence reds"
  report "           with no mutation applied; every kill above is suspect"
  Bad=`expr $Bad + 1`
fi
report ""

# ------------------------------------------------------------------
# neither target was written
# ------------------------------------------------------------------
AfterM=`shasum -a 256 "$Root/system.c" | awk '{print $1}'`
AfterR=`shasum -a 256 "$Root/test/test_system_fidelity.c" | awk '{print $1}'`
report "system.c sha256 after:                      $AfterM"
report "test/test_system_fidelity.c sha256 after:   $AfterR"
if [ "$BeforeM" != "$AfterM" ] || [ "$BeforeR" != "$AfterR" ]; then
  report "*** FATAL: a target CHANGED during the tier ***"
  exit 2
fi
report "system.c and test/test_system_fidelity.c unchanged."

if [ "$Bad" != 0 ]; then
  report "FIDELITY-MUTANT TIER: $Bad arm(s) failed their assertion"
  exit 1
fi
report "FIDELITY-MUTANT TIER: every asserted arm red, the pairing green"
exit 0

# ==================================================================
# THE MEASURED MUTANT-TO-CHANNEL TABLE (2026-08-15)
# ==================================================================
#
# Fourteen arms, FOURTEEN KILLS, and the clean pairing green at the
# pinned counts (shape A 1,364 states / 99,404 calls; shape B 489,470
# states / 65,336,902 calls; 1,339,795,893 checks, 0 failures, 33s).
# "where" is the shape and the state/call count at which the run
# halted -- the FIRST reachable divergence, not a total.  An arm whose
# shape A count is the clean 1,364 / 99,404 was INVISIBLE at shape A
# and reddened at shape B.
#
#   arm                     channel that fired   where
#   ----------------------  -------------------  --------------------
#   FM_REF_EVICT_NEWEST     act 0 round          B, 717 / 44,907
#   FM_REF_ADOPT_AT_T       act count            B, 3 / 83
#   FM_REF_SERVE_NOWRAP     act count            A, 51 / 1,890
#   FM_REF_OWED_NOCLEAR     owed (a query)       A, 4 / 88
#   FM_REF_CURSOR_INERT     records of round     B, 711 / 44,823
#   FM_MM_I5_LATCH_AT_T     act count            B, 3 / 83
#   FM_MM_I5_COUNT_SELF     act count            A, 3 / 51
#   FM_MM_I6_BOOK_SURVIVES  witness book bytes   A, 17 / 510
#   FM_MM_I8_EARLY          act count            A, 9 / 332
#   FM_MM_I9_DROP_SILENT    act count            B, 3,412 / 217,805
#   FM_MM_I10_RETAIN_WRONG  duty (a query)       A, 51 / 2,057
#   FM_MM_I11_EVICT_NEWEST  act 0 round          B, 717 / 44,907
#   FM_MM_HR_GATES          act count            A, 25 / 686
#   FM_MM_CURSOR_UNGATED    records of round     B, 711 / 44,819
#
# SIX DISTINCT CHANNELS fired across the tier: the act count, an act's
# round, two of the flat queries (owed and duty), the witness book
# bytes, and a retained round's record bytes.  The rest of the
# driver's channels -- the act kind, the SERVE want/have bytes, the
# frontier and live queries, the retained walk's name and count, and
# the per-slot systemRetained / slice queries -- went UNFIRED, and the
# reason is the comparison ORDER, not their weakness: the acts are
# compared before the queries, the queries before the retained walk,
# and the walk before the per-slot queries, so an earlier channel
# shadows every later one it disturbs.  Reading an unfired channel as
# a dead channel would be reading the halt order as coverage.
#
# THREE PAIRS MIRROR ACROSS THE COMPARISON, and that is the argument
# for keeping both halves: FM_REF_EVICT_NEWEST and
# FM_MM_I11_EVICT_NEWEST are one defect on opposite sides and halt at
# the SAME state (B, 717 / 44,907) on the SAME channel;
# FM_REF_ADOPT_AT_T and FM_MM_I5_LATCH_AT_T likewise (B, 3 / 83);
# FM_REF_CURSOR_INERT and FM_MM_CURSOR_UNGATED land four calls apart.
# A comparison sees its two inputs symmetrically or it is not a
# comparison, and this is that symmetry measured rather than assumed.
#
# WHAT THIS TIER SAYS THAT machineMutants.sh DOES NOT.  There, the
# nine reds are read off the invariant falsifier's named conjuncts and
# test_system's named sections -- oracles that reached the instrument
# through system.dtc and system.c, the same chain the machine did.
# Here they are read off a SECOND READING of system.md, so a kill is
# evidence about the specification's content and not only about the
# machine's self-consistency.  MM_HR_GATES is the sharpest case: the
# plain falsifier is BLIND to it by construction (H_r is outside its
# alphabet, which is why the -DHRTWIN twin drive exists at all), while
# the correspondence catches it at shape A in under a second, because
# the reference models the held-members record and the serve walk
# both.  FM_MM_I9_DROP_SILENT is the other: there it needs the
# falsifier's retained-set shadow to see a departure nothing
# announced; here it is a plain act count.
