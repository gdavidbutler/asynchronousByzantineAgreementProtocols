#!/bin/sh
#
# mutants.sh -- does the standing battery have teeth?
#
# The six test suites and the two instruments -- the schedule explorer
# and the ingress contract -- are this repository's whole review.  A
# suite that passes proves nothing by itself: it proves
# something only if it would FAIL on a machine that is wrong.  This
# walks a catalogue of single, anchored defects, applies each one to a
# scratch copy of bracha87.c or bkr94acs.c, and asks whether a NAMED
# check in the battery goes red -- or records, with the argument, that
# the defect is invisible to the battery's power.
#
# WHY EACH DISCIPLINE IS HERE.  Every one of them exists because the
# opposite would let this report a good number falsely:
#
#   whole-tree scratch copy.  test/test_predicates.c includes
#     "../bracha87.c" -- a quoted include resolves relative to the
#     INCLUDING FILE first and only then along -I, so mutating one file
#     and adding -I to a scratch directory compiles the untouched
#     source and every mutant looks perfect.  The copy carries the
#     whole tree and the build runs with its working directory inside
#     it, so no include path reaches the real sources at all.
#
#   fixed build path.  Each round rebuilds the same directory from the
#     pristine copy rather than patching and reverting, so a botched
#     revert cannot carry state forward; the path is constant so the
#     shadow control below compares like with like (-g records the
#     source path, and two paths alone would make every binary differ).
#
#   shadow control.  A clean control proves the build works; it does
#     NOT prove the mutation reached the binary.  Every binary that
#     should hold the mutated file must differ from the clean one.
#     Identical means the mutation never arrived.
#
#   unique anchors, asserted before use.  Near-identical output-filling
#     blocks recur at five sites in bkr94acs.c.  An anchor that matches
#     more than once, or none, stops the run instead of quietly landing
#     somewhere else or nowhere.
#
#   named-label credit.  The six suites accumulate failures and print
#     a stable label; the explorer stops a config's search at
#     its first per-state red and prints one witness, and a config with
#     none prints its whole-config labels.  A kill is credited only
#     when the DESIGNATED label appears.  A nonzero status is not a
#     kill: a mutation can fault or run away before any check runs --
#     that is the mutation announcing itself, and it is graded CRASH.
#
#   suites run one at a time.  `make check` stops at the first failing
#     suite, which would hide the designated one behind an earlier
#     incidental failure.
#
#   explorer parameters untouched.  Its frozen counts are a
#     deterministic prefix of a ceiling-bound search; change a bound and
#     every count moves, which would read as a kill everywhere.  A count
#     that moves is SENSITIVITY to a behavioral change, never by itself
#     a detected defect, and is reported as such.
#
# Cost is real: one full battery per catalogue entry.  This is a
# deliberate target, run by name, never from `make check`.
#
# Usage:
#   sh test/mutants.sh              run the whole catalogue
#   sh test/mutants.sh M07 M23      run only the named entries
#

set -u

WORK=mutantWork
CFLAGS="-std=c89 -pedantic -Wall -Wextra -I. -Os -g"
ALARM=900

SOURCES="bracha87.c bracha87.h bkr94acs.c bkr94acs.h \
bracha87Fig1Rules.c bracha87Fig3Rules.c bracha87Fig4Rules.c \
bkr94acsRules.c"
SUITESRC="test_bracha87.c test_bkr94acs.c test_predicates.c \
test_bracha87_blackbox.c test_bkr94acs_blackbox.c test_schedules.c \
test_ingress.c test_ceiling.c"
BINS="test_bracha87 test_bkr94acs test_predicates \
test_bracha87_blackbox test_bkr94acs_blackbox test_schedules \
test_ingress test_ceiling"

if [ ! -f bracha87.c ] || [ ! -f test/test_schedules.c ]; then
  echo "mutants: run from the repository root" >&2
  exit 2
fi

CC=${CC:-cc}

# ---------------------------------------------------------------------
# THE CATALOGUE.  One place, never truncated.  Each entry carries its
# anchor, its replacement, its designated oracle, and the argument that
# the mutated behavior is REACHABLE by that oracle -- written from the
# papers, the headers and the suites, and readable before the entry is
# trusted.  An entry whose ORACLE is "-" is one this catalogue claims
# NO oracle reaches; its argument says why, and the run records whether
# the battery agrees.
# ---------------------------------------------------------------------

mkdir -p "$WORK" || exit 2
cat > "$WORK/catalogue" <<'CATALOGUE_END'
#MUTANT M01
#FAMILY retire gates -- READY retired on LOCAL accept
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL BPR post-accept: 1 action (READY only, ECHO retired)
#EXPECT KILLED
#WHY
An accepted process still owes (ready, v) to every process below the
2t+1 accept threshold, and retry is the only delivery this design
offers under loss.  Retiring READY on the local accept is the forbidden
gate.  The oracle drives one instance to ACCEPTED and then calls the
retry entry once: the contract is exactly one action, READY_ALL.  Under
the mutation the retry returns nothing, so the count check goes red on
the same call.  Reachability is immediate -- the arm constructs the
accepted state directly and does not depend on any schedule.
Corroborated by the explorer, whose quiescent-terminal arm reads WHOSE
evidence closed the gate: a locally-retired READY quiesces with a
suppress mask short of all n.
#ANCHOR
    readyMaskFull = fig1FromCnt(sk, B_N(b)) >= B_N(b);
#WITH
    readyMaskFull = fig1FromCnt(sk, B_N(b)) >= B_N(b)
                 || (b->flags & BRACHA87_F1_ACCEPTED);
#END

#MUTANT M02
#FAMILY retire gates -- READY quiescence on a count threshold
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Quiescence: READY still output at 3/4 accepted
#EXPECT KILLED
#WHY
The sound whole-action retire is FULL coverage: every process has
announced its own accept.  A count threshold of 2t+1 instead would let
up to t forged announcements plus a partial correct set trip quiescence
while a correct process is still short of its 2t+1 readys.  The oracle
sits at n=4, t=1, where 2t+1 is 3: it records three accepts and
requires READY to still be output, then records the fourth and requires
it retired.  Under the mutation the three-accept call already retires,
so the still-output check goes red.  Reachability is direct -- the arm
sets the accepted bitmap through the public setter.
#ANCHOR
    readyMaskFull = fig1FromCnt(sk, B_N(b)) >= B_N(b);
#WITH
    readyMaskFull = fig1FromCnt(sk, B_N(b)) >= 2u * b->t + 1;
#END

#MUTANT M03
#FAMILY retire gates -- INITIAL retired at merely echoed
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL BPR initiator post-loopback: INITIAL_ALL first
#EXPECT KILLED
#WHY
At the n = 3t+1 boundary the Rule 2 echo threshold (n+t)/2 + 1 equals
the count of correct processes, so at local-echo time no readys need
exist yet: the rescue set is not established and a process that missed
the bootstrap can stay one echo short forever.  Only the initiator
breaks that, so local echo is not a sound stop.  The oracle marks an
instance as initiator, feeds its own INITIAL back (setting ECHOED
without accepting), and requires the retry's first action to still be
INITIAL_ALL.  Under the mutation the initiator output is suppressed the
moment ECHOED is set, so the first action becomes the echo retry and
the check goes red.  Corroborated end to end by the silent-Byzantine
convergence arm, the schedule this gate strands.
#ANCHOR
  amInitiator   = (b->flags & BRACHA87_F1_INITIATOR) ? 1 : 0;
#WITH
  amInitiator   = (b->flags & BRACHA87_F1_INITIATOR)
               && !(b->flags & BRACHA87_F1_ECHOED) ? 1 : 0;
#END

#MUTANT M04
#FAMILY retire gates -- INITIAL all-echoed path removed
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL BPR initiator all-echoed: INITIAL_ALL absent
#EXPECT KILLED
#WHY
INITIAL induces only echoes, so once every process has echoed there is
nothing left for it to induce and it retires whether or not this
instance has accepted.  The oracle builds exactly that state: an
initiator with an echo from all n and no ready at all, hence not
accepted.  It then requires no INITIAL_ALL among the retry's actions.
Under the mutation only the accept gate remains, the instance has not
accepted, and INITIAL_ALL is still output -- red on that call.  The two
gates are independent, which is why removing one is visible while the
other still holds.
#ANCHOR
  allEchoed     = fig1FromCnt(F1_ECFROM(b), B_N(b)) >= B_N(b);
#WITH
  allEchoed     = 0;
#END

#MUTANT M05
#FAMILY retire gates -- ECHO retire dropped
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL BPR post-accept: 1 action (READY only, ECHO retired)
#EXPECT KILLED
#WHY
ECHO is bootstrap-only.  Accept witnesses at least t+1 correct readys,
which circulate forever, so the amplification tail consumes no echo and
the echo retry is dead weight from that point.  The oracle drives an
instance to ACCEPTED with ECHOED set and requires exactly one action.
Under the mutation the echo retry is still output beside the ready, the
count is two, and the check goes red.  Reachability is immediate: the
arm constructs the state directly.
#ANCHOR
  if (retryEcho)
    out[nout++] = BRACHA87_ECHO_ALL;
#WITH
  if (haveEchoed)
    out[nout++] = BRACHA87_ECHO_ALL;
#END

#MUTANT M06
#FAMILY thresholds -- echo threshold raised
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Threshold: 5th echo -> echoed
#EXPECT KILLED
#WHY
The figure's rule reads "more than (n+t)/2", and the Lemma 1 pigeonhole
argument needs the strict form, so in integer arithmetic the threshold
is (n+t)/2 + 1.  The oracle sits at n=7, t=2, where that is 5: it feeds
distinct echoes one at a time and requires the fifth to fire.  Raising
the threshold by one leaves the fifth short and the check goes red on
that call.  The reaching argument leans on the suite's discipline of
guarding every echoed-value read on the non-null the read needs, so a
machine whose threshold never fires runs the whole suite red rather
than dying before this arm.  The contract suite's arm at the same
boundary ("n=7,t=2: echo 5 fires (strict > 4)") is the second,
independent detector, derived from the header alone.  Corroborated by
the explorer, where a raised threshold at n=2, t=0 puts the threshold
past the process count and no schedule reaches quiescence.
#ANCHOR
  ecGtHalfNT    = ec >= (B_N(b) + b->t) / 2 + 1;
#WITH
  ecGtHalfNT    = ec >= (B_N(b) + b->t) / 2 + 2;
#END

#MUTANT M07
#FAMILY thresholds -- echo threshold lowered
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Threshold: 4 echoes, not echoed
#EXPECT KILLED
#WHY
The same boundary read from below.  At n=7, t=2 the oracle feeds four
distinct echoes and requires ECHOED still clear; lowering the threshold
to (n+t)/2 fires Rule 2 on that fourth echo and the check goes red.
The second, independent detector is the contract suite's precise-Rule-2
arms at n=4, t=1 and n=7, t=2, which assert the same boundary from the
header alone.  Recorded scope fact: no equivocation arm in this battery
detects this.  Worked at each of them -- a lowered threshold makes
every correct process cross on the SAME value, so the accepted value
agrees everywhere, Lemma 1 and Lemma 2 both hold, the agreed subset is
unchanged in size and contents, and the balanced-split arms assert
nothing beyond that.  A false accept of the right value is what an
end-to-end arm under honest delivery cannot see; an arm sees it by
presenting exactly one short of the threshold -- the unit arms
directly, and the contract suite's self-delivery pair end-to-end, with
t silent and every process withholding its own hand-back, which reds
here on the readys the lowered threshold lets out.
#ANCHOR
  ecGtHalfNT    = ec >= (B_N(b) + b->t) / 2 + 1;
#WITH
  ecGtHalfNT    = ec >= (B_N(b) + b->t) / 2;
#END

#MUTANT M08
#FAMILY thresholds -- ready amplification threshold lowered
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Threshold: 2 readys, not echoed
#EXPECT KILLED
#WHY
Rules 3 and 5 fire at t+1 readys, the count that guarantees at least
one correct sender.  Lowering it to t lets a Byzantine set alone drive
an echo and a ready.  The oracle sits at n=7, t=2, where t+1 is 3: it
feeds two readys and requires ECHOED still clear, then the third and
requires it set.  Under the mutation the second ready fires Rule 3 and
the first check goes red.
#ANCHOR
  rdGeTPlus1    = rd >= (unsigned int)b->t + 1;
#WITH
  rdGeTPlus1    = rd >= (unsigned int)b->t;
#END

#MUTANT M09
#FAMILY thresholds -- accept threshold lowered
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Threshold: 4 readys, not accepted
#EXPECT KILLED
#WHY
Rule 6 accepts at 2t+1 readys, the count that guarantees at least t+1
correct senders and so carries Lemma 2.  The oracle sits at n=7, t=2,
where 2t+1 is 5: after driving the instance to RDSENT it feeds four
readys and requires ACCEPTED still clear, then the fifth and requires
it set.  Under the mutation the fourth ready accepts and the first
check goes red.  The setup uses the echo path, which this mutation does
not touch, so the arm reaches its own precondition unchanged.
#ANCHOR
  rdGe2TPlus1   = rd >= 2u * b->t + 1;
#WITH
  rdGe2TPlus1   = rd >= 2u * b->t;
#END

#MUTANT M10
#FAMILY thresholds -- accept threshold raised
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Threshold: 5th ready -> accepted
#EXPECT KILLED
#WHY
The same boundary from above.  At n=7, t=2 the fifth distinct ready
must accept; raising the threshold to 2t+2 leaves it short and the
check goes red on that call.  A raised accept threshold is a liveness
defect rather than a safety one, which is why the both-sided arm
matters: the value-agreement checks elsewhere would stay green.
#ANCHOR
  rdGe2TPlus1   = rd >= 2u * b->t + 1;
#WITH
  rdGe2TPlus1   = rd >= 2u * b->t + 2;
#END

#MUTANT M11
#FAMILY dedup -- echo per-sender dedup dropped
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL echo dedup per sender
#EXPECT KILLED
#WHY
Every threshold in the figure counts DISTINCT senders.  Without the
per-sender guard one Byzantine sender repeating an echo drives the
(n+t)/2 + 1 cascade alone.  Reaching that needs the arm to run at a
BINARY value: there the count is a real counter the second registration
increments again, so the repeat inflates it.  At a multi-byte value the
count is derived by scanning the per-sender bitmap, and a repeated
registration is idempotent -- the bit is already set -- so the same
defect is invisible.  The contract suite is the designated oracle
because its dedup arm runs at the binary value: it feeds one sender's
echo twice and requires the second to output nothing, and under the
mutation the inflated count fires the rule on the repeat.
#ANCHOR
    if (!BIT_TST(F1_ECFROM(b), from))
      fig1SetEc(b, from, value);
#WITH
    fig1SetEc(b, from, value);
#END

#MUTANT M12
#FAMILY dedup -- ready per-sender dedup dropped
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL ready dedup pre-rule3
#EXPECT KILLED
#WHY
Same argument at the ready counts, where the consequence is worse: the
t+1 and 2t+1 gates are exactly what separate a correct sender from a
Byzantine one, and a repeated ready would let a single sender reach
both.  The same binary-value condition applies -- only there is the
count a real counter a repeat can inflate -- so the contract suite's
dedup arm is the designated oracle: it feeds one sender's ready twice
and requires the second to output nothing.
#ANCHOR
    if (!BIT_TST(F1_RDFROM(b), from))
      fig1SetRd(b, from, value);
#WITH
    fig1SetRd(b, from, value);
#END

#MUTANT M13
#FAMILY annotation fills -- RECEIVED mask dropped at the array retry egress
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Livelock: honest n=4 quiesces
#EXPECT KILLED
#WHY
A ready that arrives unmarked is its sender showing this instance's
accept was not received there, and the receiver arms a re-send that
un-suppresses it for one egress.  If the egress never carries the
annotation, every ready arrives unmarked, every one re-opens the
suppress mask, and the mask can never reach full coverage.  The oracle
drives n=4 all-correct instances under the caller discipline both
example loops follow and requires all four to reach the zero return
inside a small sweep bound.  Under the mutation none of them ever does,
so the quiesced count falls short and the check goes red.  Reachability
needs no loss and no adversary -- the arm is the plain schedule.
Corroborated by the explorer's quiescent-terminal reachability.
#ANCHOR
          out[i].received = (acts[i] == BRACHA87_READY_ALL)
            ? bracha87Fig1Received(instances[idx]) : 0;
#WITH
          out[i].received = 0;
#END

#MUTANT M14
#FAMILY annotation fills -- accepted flag dropped at the array retry egress
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Livelock: honest n=4 quiesces
#EXPECT KILLED
#WHY
Accept is silent on the wire in the bare figure, so the only way a
process learns that another has accepted is the annotation riding on
that other's ready retries.  Dropped, the accepted bitmap never grows
past the local self-record, the suppress mask stays one process wide,
and the whole-action retire never fires.  The oracle is the same n=4
drive to quiescence, which needs every process's announcement to
arrive.  Under the mutation no announcement ever leaves and the
quiesced count falls short.
#ANCHOR
          out[i].accepted = (acts[i] == BRACHA87_READY_ALL
            && (instances[idx]->flags & BRACHA87_F1_ACCEPTED)) ? 1 : 0;
#WITH
          out[i].accepted = 0;
#END

#MUTANT M15
#FAMILY suppress-mask formula -- negation dropped
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Quiescence: READY retired when all n accepted
#EXPECT KILLED
#WHY
The effective mask is the accepted set MINUS the processes with an
outstanding arm.  Written as an intersection
instead, the mask is empty whenever nothing is armed -- which is the
ordinary case -- so coverage never completes and the action never
retires.  The oracle records all n accepts with nothing armed and
requires the retry to stop outputting READY.  Under the mutation the
mask is zero, coverage is zero, and READY is still output -- red on
that call.
#ANCHOR
      sk[i] = ac[i] & ~am[i];
#WITH
      sk[i] = ac[i] & am[i];
#END

#MUTANT M16
#FAMILY re-send arm -- the accept guard dropped
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Resend: pre-accept unmarked READY does not arm (bit 1 still suppressed)
#EXPECT KILLED
#WHY
An unmarked READY shows its sender has not received THIS instance's
accept.  Before this instance has accepted there is no accept to
announce, so arming would un-suppress a process for an egress that has
nothing to say and would re-arm on every subsequent ready.  The oracle
drives an instance to RDSENT but not ACCEPTED, records one process's
accept, then calls bracha87Fig1ProcessResend for that same process
directly, and requires it to remain suppressed.  Direct, because the
Input path cannot see this refusal: the dispatch's arm row refuses the
same pre-accept arm before the setter is reached (BPR.md, Declared
Invisible).  Under the mutation the arm is recorded and the
suppression is cleared -- red on that read.
#ANCHOR
  if (!b || from > b->n || !(b->flags & BRACHA87_F1_ACCEPTED))
    return;
  BIT_SET(F1_ARMFROM(b), from);
#WITH
  if (!b || from > b->n)
    return;
  BIT_SET(F1_ARMFROM(b), from);
#END

#MUTANT M17
#FAMILY per-sender value stores -- echo value written under a fixed index
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Rule 2: 3 echoes -> ECHO_ALL
#EXPECT KILLED
#WHY
Echo counts are per value, and for a multi-byte value the count is a
scan of the per-sender mirror.  Collapsing every sender's value onto
one slot leaves the other slots zero, so the scan finds at most one
match and no threshold is ever reached.  The oracle sits at n=4, t=1
with a four-byte value -- the scan path, not the binary fast path --
and feeds three distinct senders the same echo, requiring the third to
output the echo action.  Under the mutation the count stays at one and
the check goes red.
#ANCHOR
  memcpy(F1_ECVAL(b) + (unsigned long)from * F1_VLEN(b), v, F1_VLEN(b));
#WITH
  memcpy(F1_ECVAL(b), v, F1_VLEN(b));
#END

#MUTANT M18
#FAMILY figure 4 protocol function -- subset reachability made symmetric
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Subset n=8: value 0 accepted
#EXPECT KILLED
#WHY
Step 1 takes the majority of an n-t sample and breaks a tie toward 0,
so the two values are NOT symmetric: value 1 needs a strict majority of
the sample while value 0 needs only half of it.  Writing the same
strict test for both wrongly rejects a correct process whose sample
ties.  The two formulas AGREE whenever the sample size is odd and
differ only when it is even, so the oracle must be an arm at an EVEN
n-t.  The subset enumeration runs at n=4, t=1, where n-t is 3 and
the two formulas are the same expression -- exhaustive there, and blind
to this.  The designated arm sits at n=8, t=2, where n-t is 6: it
presents a sample in which value 0 is reachable only through the
tie-breaking half and requires it accepted.  Under the mutation the
strict test rejects it and the check goes red.  The companion arm at
n=5, t=1 (n-t = 4) is the second, independent detector.
#ANCHOR
     && cnt[0] >= (nt + 1) / 2
#WITH
     && cnt[0] >= nt / 2 + 1
#END

#MUTANT M19
#FAMILY figure 3 cascade -- gated on the first crossing only
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Recascade: stored round 1 v=1 re-validated on growth
#EXPECT KILLED
#WHY
The validity of a round-r message is existential over n-t subsets of
round r-1 and monotone in that set, so growth at r-1 AFTER it first
reached n-t can still unlock a stored round-r message.  Gating the
re-check on the round not yet being complete strands exactly those
messages.  The oracle fills a round past n-t, stores a higher-round
message that is invalid against the smaller set, grows the lower round
further, and requires the stored message to become valid.  Under the
mutation the later growth fires no re-check and it stays invalid -- red
on that read.  The cascade correspondence arm is the
second detector.
#ANCHOR
  if (doCascade) {
#WITH
  if (doCascade && !roundKComplete) {
#END

#MUTANT M20
#FAMILY figure 3 validity -- the decision-flag permission dropped
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Case 0 permissive: 1|D_FLAG rejected
#EXPECT KILLED
#WHY
When the protocol function answers permissively it also says whether a
decision-flagged value could legitimately have been produced by SOME
n-t subset.  Dropping that consultation accepts a flagged value in the
windows where no subset produces one, which is a direct injection route
into the step 3 decision counts.  The oracle builds a step 1 sample
that is permissive on the base value only and requires both flagged
arrivals to be rejected.  The residual base comparison the mutation
leaves behind still rejects the arrival whose base differs from the
answer's, so the designated arm is the OTHER one of that pair -- the
flagged value whose base matches, which now passes on the base test
alone.  Under the mutation that check goes red.
#ANCHOR
      if (value & BRACHA87_D_FLAG) {
        if (!(result & BRACHA87_D_FLAG))
          return (0);
        if ((value & 1) != (result & 1))
          return (0);
      }
#WITH
      if (value & BRACHA87_D_FLAG) {
        if ((value & 1) != (result & 1))
          return (0);
      }
#END

#MUTANT M21
#FAMILY figure 4 step 2 -- the (d, v) update guarded on the decided state
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL figure-unchanged arm: the peer validates a decided process's round-5 (d, v) and completes the round
#EXPECT KILLED
#WHY
Figure 4 has no decided state: case (i) reads "decision_p := value_p
:= v" and every case ends "Go to round 1 of phase i+1", so a decided
process sets (d, v) at step 2 of every later phase exactly as an
undecided one does.  Guarding that update on the decision is the
plausible "preserve the decision value" reading, and it is a liveness
defect dressed as safety: the decided process then
broadcasts a bare v at round 3i+3, and Figure 3 at every peer rejects
it, because more than n/2 of the round-(3i+2) messages agree and N
demands (d, v).  Theorem 2's Agreement proof consumes exactly the
message the guard withholds -- an undecided q that adopted v at case
(ii) decides at phase r+1 on 2t+1 (d, v), the decided processes' among
them.  Reachability: the oracle decides a process at phase 0, plays it
through phase 1 on the Lemma 9 trajectory, and feeds its own
broadcasts as three senders to a peer's Fig 3; the correct machine's
round-5 (d, 1) validates and completes the round, and under the
mutation the round-5 message is a bare 1, nothing validates, and the
completion check goes red.
#ANCHOR
  if (setDMajority)
    b->value = (((cnt[1] * 2 > B_N(b)) ? 1 : 0) | BRACHA87_D_FLAG);
#WITH
  if (setDMajority && !haveDecided)
    b->value = (((cnt[1] * 2 > B_N(b)) ? 1 : 0) | BRACHA87_D_FLAG);
#END

#MUTANT M22
#FAMILY figure 4 -- the exhausted early return dropped
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL post-EXHAUSTED Round(2) returns 0
#EXPECT KILLED
#WHY
Exhaustion means the round space is spent with no decision; the
instance must then be inert, because a further round would compute over
a sample it can no longer broadcast for and could output a second
terminal action for one instance.  The contract suite drives an
instance to exhaustion from the header alone and then calls the round
entry again, requiring zero actions.  Under the mutation the call runs
the dispatch and returns a broadcast action -- red on that call.
#ANCHOR
  if (b->flags & BRACHA87_F4_EXHAUSTED)
    return (0);
#WITH
#END

#MUTANT M23
#FAMILY forged initial -- the A-Cast filter dropped
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL forged A-Cast INITIAL (from!=process) outputs no action
#EXPECT KILLED
#WHY
A reliable-broadcast instance is keyed to ONE designated initiator, and
Rule 1 echoes the first initial message unconditionally.  A sender that
is not the initiator claiming to carry the initiator's value therefore
drives the echo cascade to a false accept of a value the correct
process never broadcast.  Authenticated channels do not close this:
they bind the sender, not the initiator field the message claims, and a
sender different from the initiator is normal for echo and ready.  The
oracle submits an initial message for one process from a different
sender and requires no action.  Under the mutation the message reaches
Rule 1, the echo action is output, and the check goes red.
#ANCHOR
  if (type == BRACHA87_INITIAL && from != process)
    return (0);
#WITH
#END

#MUTANT M24
#FAMILY forged initial -- the BA filter dropped
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL forged BA INITIAL (from!=initiator) outputs no action
#EXPECT KILLED
#WHY
The same protocol-semantic check on the binary-agreement leg, where the
instance is keyed by process, round and initiator.  A forged initial
here steers one round of one agreement toward a value its initiator
never broadcast.  The oracle submits such a message and requires no
action; under the mutation Rule 1 fires and outputs the echo.
#ANCHOR
  if (type == BRACHA87_INITIAL && from != initiator)
    return (0);
#WITH
#END

#MUTANT M25
#FAMILY enter latch -- the single-input guard dropped
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL BaEntered: fanout enters the three un-entered BAs
#EXPECT KILLED
#WHY
The protocol demands one input per agreement instance: once step 1 has
entered 1 or step 2 has entered 0, neither step touches it again.
Without the guard the step 2 fanout re-enters 0 into an instance that
already carries a 1, contradicting the input that instance already
holds.  The oracle enters 1 into one of four agreements, then writes
the n-t decided-1 count that enables the fanout and requires the fanout
to output exactly three actions.  Under the mutation it outputs four
and the check goes red.
#ANCHOR
  if (entered[process] != BKR94ACS_ENTER_NONE)
    return (0);
#WITH
#END

#MUTANT M26
#FAMILY duty classification -- the fanout floor raised to n
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL BaEntered: fanout duty TOLERANCE with entries outstanding
#EXPECT KILLED
#WHY
Step 2's floor is n-t decided-1 outcomes: below it, entering 0 could
force the agreed subset empty, and above it the count cannot be raised
to unanimity because no correct process can wait on the t it may never
hear from.  Raising the floor to n is that forbidden wait.  The oracle
writes three decided-1 outcomes at n=4, t=1 with one entry outstanding
and requires the classification to read TOLERANCE.  Under the mutation
three is short of four, the classification reads HELD, and the check
goes red.
#ANCHOR
    enabled = one >= N - a->t;
#WITH
    enabled = one >= N;
#END

#MUTANT M27
#FAMILY duty classification -- the turn's full-sample boundary collapsed
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL BaGetValid: count >= n-t reads TurnDuty TOLERANCE
#EXPECT KILLED
#WHY
A round may be turned once n-t messages have been validated, but the
sample keeps growing, so the classification separates "enabled, still
growable" from "full sample, waiting buys nothing".  Collapsing the
full-sample boundary down to n-t erases that distinction and makes
every enabled turn fire without the caller's signal.  The oracle feeds
exactly n-t validated round-0 messages and requires TOLERANCE, then
feeds the n-th and requires MET.  Under the mutation the first read
already returns MET and the check goes red.
#ANCHOR
      nothingLeft = enabled
                 && bracha87Fig3ValidCount(f3, nextRound) >= A_N(a);
#WITH
      nothingLeft = enabled
                 && bracha87Fig3ValidCount(f3, nextRound) >= A_N(a) - a->t;
#END

#MUTANT M28
#FAMILY retry gate -- the verdict rows inverted
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL BPR gate: process 1 (decided 1) IS retried (post-decide)
#EXPECT KILLED
#WHY
The verdict gate -- bkr94acs.dtc's "walk A-Cast j" rows -- skips only
an agreement that decided 0, whose process is excluded at every correct
process by BA agreement.  An
agreement that decided 1 must keep being retried: processes that have
not yet seen the accept for that process still need this one's echoes
and readys, or their own agreement stays undecided and can be driven to
0 by the fanout, breaking cross-process agreement on the subset.
Inverting the arms -- here by feeding the row the decision byte
inverted, decided 0 read as decided 1 and everything else as decided 0
-- skips exactly the case that must continue.  The
oracle writes a decided-1 outcome and requires the retry sweep to still
visit that process.  Under the mutation the walk skips it and the check
goes red.  The oracle drives the decision by direct write rather than
through the gate, so it is independent of the mutated line.
Corroborated by the explorer at config 3a, outside the smoke run: its
quiescent-terminal ending claim reds on a sent A-Cast whose RECEIVED
mask is short of all n -- the decided-1 instance the inverted gate
stopped serving while its READY was still owed.  The smoke run reds
first at surface 2 on the same claim, before any count is compared.
#ANCHOR
      baDecision = bkr94acsDecision(a)[process];
#WITH
      baDecision = bkr94acsDecision(a)[process] == 0 ? 1 : 0;
#END

#MUTANT M29
#FAMILY annotation fills -- RECEIVED mask dropped at the A-Cast retry egress
#FILE bkr94acs.c
#ORACLE test_schedules
#LABEL FAILURE: no schedule reached a QUIESCENT terminal
#EXPECT KILLED
#WHY
This is the one entry whose designated oracle is the explorer, because
nothing cheaper detects it.  Dropping the RECEIVED mask on the A-Cast
retry means every such ready arrives unmarked at its receiver, each
re-opens the suppress mask, and the instance never retires its ready.
The composed suites drive to COMPLETION, not to quiescence, so they
finish green: completion is decided by the agreements, and the ready
tail this breaks runs after it.  The explorer's terminal class is
quiescence -- every process quiescent and the pool empty -- and its
whole-config assertion is that some schedule reaches one.  Under the
mutation no schedule does, and the explorer says so on the surface-2
config.  The surface-1 configs do not touch this file and stay green,
so the attribution is clean.
#ANCHOR
    out[nact].value = cv;
    out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
    out[nact].received = (f1out[k] == BRACHA87_READY_ALL)
      ? bracha87Fig1Received(f1) : 0;
#WITH
    out[nact].value = cv;
    out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
    out[nact].received = 0;
#END

#MUTANT M30
#FAMILY decided count -- the exhausted sentinel counted as decided
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL Exhausted among decided: no COMPLETE action is ever output
#EXPECT KILLED
#WHY
An exhausted agreement made no decision, so it must not count toward
the all-n condition: counting it would let a process announce a subset
while one agreement has no outcome at all, and any unilateral substitute
could disagree with another process's real outcome.  The scan admits
only 0 and 1 precisely so both sentinels fall out with no separate
rule.  What the kill needs is narrow, and the designated arm is the one
built for it: the widened scan changes the count only where an
exhausted entry is the LAST one missing, so the reaching state is every
other agreement decided AND one exhausted.  An arm that exhausts one
agreement while the rest stay undecided leaves the count short either
way and separates nothing.  The designated arm constructs the reaching
state deliberately at n=4, t=1, one phase -- one agreement split 2:2
across its initiators in every round so it takes the coin and
exhausts, the other three carried 0, 0, (d, 0) so each decides -- and
then requires that no completion action is output and the completion
flag stays clear, as a standing fact rather than a reading at one
instant: the drive continues past the last decision with further
inputs and further turns.  Under the mutation the last decision's scan
counts the sentinel, the all-n condition is met, and the completion
action is output on that turn.
#ANCHOR
      if (dec[j] <= 1)
#WITH
      if (dec[j] != 0xFF)
#END

#MUTANT M31
#FAMILY decided count -- the all-n completion condition raised
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL n=4 t=1 complete
#EXPECT KILLED
#WHY
Step 3 reads the subset once all n agreements have output.  Raising
that condition past n makes it unreachable, so the completion action is
never output and no caller ever learns the subset is final.  The oracle
runs an ordinary all-correct instance at n=4, t=1 and requires every
process to complete.  Under the mutation none does and the check goes
red.  Reachability is the plainest schedule in the battery.
#ANCHOR
    postCountAllN = nDecided >= A_N(a);
#WITH
    postCountAllN = nDecided >= A_N(a) + 1;
#END

#MUTANT M32
#FAMILY counter width -- the derived decided count narrowed to a byte
#FILE bkr94acs.c
#ORACLE test_ceiling
#LABEL ACS: COMPLETE on the 256th decision, once
#EXPECT KILLED
#WHY
Counts compared against the ACTUAL process count must hold 256, one
past what a byte can carry, so a byte counter wraps to 0 on the 256th
increment and a comparison against 256 can never fire.  Narrowing the
derived decided count reinstates exactly that: at 256 processes the
completion condition becomes unreachable.  Only the ceiling suite
reaches it: every other configuration in the battery is 37 processes
or fewer, where the narrowed counter holds the same values and the
mutated machine is behaviorally identical, so every other suite and
the explorer's frozen counts stay green.  The oracle drives one ACS
instance's 256 BAs to decision through bkr94acsBaInput and
bkr94acsTurn and requires COMPLETE on the 256th; under the mutation
the scan reads 0 on that turn and the action never comes.
#ANCHOR
    unsigned int nDecided;
#WITH
    unsigned char nDecided;
#END

#MUTANT M33
#FAMILY subset membership -- the predicate widened past decided-1
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL fresh: Subset returns 0
#EXPECT KILLED
#WHY
The subset is the set of processes whose agreement output 1.  Widening
the test to "not 0" admits the undecided and exhausted sentinels, which
breaks the documented read -- a caller would be handed processes whose
agreement has not decided.  Every reader that runs AFTER a drive has
settled is blind to this, because by then each entry is already 0 or 1
and the two tests agree; the size checks would pass a LARGER set
anyway.  What reaches it is a read taken BEFORE anything has decided,
and the contract suite opens with exactly that: on a freshly
initialized instance -- every entry the undecided sentinel -- it
requires the subset to be empty.  Under the mutation every entry
qualifies and the count is n, so the check goes red on the first
assertion of the suite.
#ANCHOR
    if (dec[i] == 1)
#WITH
    if (dec[i] != 0)
#END
#MUTANT M34
#FAMILY sweep-side co-emission -- decision recorded, BA_DECIDED act dropped
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL F4: the fanout's window opens on a BA_DECIDED act
#EXPECT KILLED
#WHY
The derived abandon sizing's ordering claim (BPR.md, the registry)
rests on one machine fact: the decision byte and the BA_DECIDED act
are written in the same straight-line block, so the fanout's duty
window can only open on a decision the caller was handed as progress.
This mutation keeps the byte and drops the act.  The machine still
decides, still completes, and the whole F4 ending vector is
byte-identical to the correct machine's -- included laggard, no
enter-0, gate never near -- because Input acts keep the sweeps
un-barren either way.  What reds is exactly the premise: the window
opens (FanoutDuty leaves HELD on the recorded byte) in a tick whose
turn handed the caller nothing, so the designated co-emission check
fails at every opening, once per process per lane.  The other F4
labels PASS under this defect, which is why the co-emission check is
the one carrying the teeth.  The schedule explorer co-detects, but
not through a dedicated clause: at maxPhases = 1 a deciding turn
under this defect returns 0 acts, so the explorer's act-derived
turned-tracking sees a phantom within-round duty regression and its
"fell back within one round" clause reds -- witnessed at four
configs.  A dedicated opening-carries-an-act clause was tried and
removed as structurally shadowed there (see the oracle notes in
test/test_schedules.c).
#ANCHOR
    bkr94acsDecision(a)[process] = f4->decision;
    out[nact].value = 0;
    out[nact].skip = 0;
    out[nact].received = 0;
    out[nact].act = BKR94ACS_ACT_BA_DECIDED;
    out[nact].process = process;
    out[nact].round = 0;
    out[nact].type = 0;
    out[nact].baValue = f4->decision;
    out[nact].initiator = 0;
    out[nact].accepted = 0;
    ++nact;
#WITH
    bkr94acsDecision(a)[process] = f4->decision;
#END

#MUTANT M35
#FAMILY duty classification -- the fanout floor lowered to the paper's 2t+1
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL fanout floor: three decided-1 at n=5 t=1 (the paper's 2t+1) reads HELD
#EXPECT KILLED
#WHY
BKR94 Figure 3 step 2 fires at 2t+1 decided-1 outcomes; this library
fires at n-t (Implementation Note 15).  The two are one integer at
n = 3t+1, which is where every black-box arm runs, and in a lossless
all-honest schedule every BA is entered by step 1 before any decides,
so the duty answers MET and never reaches the comparison: the rest of
the battery is green under this defect.  The oracle is the one arm
that reaches the comparison off the edge -- n=5, t=1 (n-t = 4,
2t+1 = 3), every entry outstanding, three decided-1 outcomes written
-- and requires the classification to read HELD.  Under the mutation
three meets 2t+1, the classification reads TOLERANCE, and the check
goes red.
#ANCHOR
    enabled = one >= N - a->t;
#WITH
    enabled = one >= 2u * a->t + 1;
#END
#MUTANT M36
#FAMILY figure 1 rule chaining -- ready reads the echo flag as it arrived
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL t=0 overtaking READY: ECHO_ALL, READY_ALL and ACCEPT together
#EXPECT KILLED
#WHY
Rule 5 sends (ready, v) upon t+1 (ready, v) when the process has echoed
and has not sent ready.  "Has echoed" is read AFTER the echo rules have
run on this same message: the ready tables take "send (echo, v)" as an
input, so a (ready, v) arriving at a process with the flag clear fires
Rule 3, and Rule 5 reads that echo rather than the flag the message
arrived on.  Gating the ready output on the flag AS IT STOOD WHEN THE
MESSAGE ARRIVED breaks the chain at the C egress, downstream of where
the dispatch could see it.  Reachability: at t = 0 the two ready
thresholds t+1 and 2t+1 are the same integer, so a first ready at a
process that has neither echoed nor sent ready is the chain's cleanest
witness -- the oracle requires three actions out of that single call
and names them, and under the mutation the ready is suppressed and two
arrive.  The same mutation also reds the un-echoed echo-crossing arm,
which is the other end of the same chain.
#ANCHOR
  if (sendReady) {
#WITH
  if (sendReady && haveEchoed) {
#END

#MUTANT M37
#FAMILY figure 3 cascade -- the decision-flag permission dropped on the re-derivation path
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL cascade, no d-flag permitted: only the plain 0 is re-derived valid
#EXPECT KILLED
#WHY
VALID^k admits (q, k, v) only when v could have been produced by N over
SOME n-t subset of VALID^(k-1).  A permissive N reports both that more
than one value was reachable and which decision-flagged value, if any,
was among them; a (d, v) the permission does not cover was not
producible and must be refused.  M20 anchors that refusal in
fig3IsValid -- the path taken when round k-1 is already complete at the
moment the k-message arrives.  The cascade is a SECOND evaluation of
the same predicate, run when later growth at k-1 completes the round
after the k-message was stored invalid, and it carries its own copy of
the check.  Dropping the permission there admits a d-flag under an N
that permits none, on the arrival order an adversary controls.
Reachability: the oracle delivers a round-1 0|D_FLAG and a plain 0
before round 0 is complete under an N that permits no d-flag, then
completes round 0 and requires exactly one message to be re-derived
valid; under the mutation the d-flagged one is re-derived valid too and
the count check goes red.
#ANCHOR
              if (!(cres & BRACHA87_D_FLAG))
                valid = 0;
              else if ((rvl[i] & 1) != (cres & 1))
                valid = 0;
#WITH
              if ((rvl[i] & 1) != (cres & 1))
                valid = 0;
#END

#MUTANT M38
#FAMILY figure 4 -- a decided process reports exhaustion at its last phase
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL decided arm: the last phase of a decided process returns 0, not EXHAUSTED
#EXPECT KILLED
#WHY
EXHAUSTED means the round space is spent WITHOUT a decision -- the
condition Note 12 hands to the BKR94 layer as a locally unrecoverable
failure.  A process that decided at an earlier phase is in the opposite
state: it has its decision and, per Note 1, keeps broadcasting it to
the end of the round space.  Running out of phases after that is the
end of this instance's participation, not a failure to agree, so the
caller must not be told EXHAUSTED.  Testing maxPhases ahead of the
decided state is the confusion, and it is invisible until a decided
process actually reaches its last phase.  Reachability: the oracle
decides at phase 0 of a two-phase instance and plays phase 1 to its
end; the correct machine returns 0 with the EXHAUSTED flag clear, and
under the mutation it returns BRACHA87_EXHAUSTED and sets the flag.
#ANCHOR
    if (haveDecided) {
#WITH
    if (haveDecided && ph + 1 < b->maxPhases) {
#END

#MUTANT M39
#FAMILY figure 4 step 3 -- the decide gated on step 2's majority condition
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL n=6 arm: three (d,1) among five decide at the last phase
#EXPECT KILLED
#WHY
Step 3 case (i) decides when MORE THAN 2t validated (3i+3)-messages
carry value (d, v) -- the count of decision-flagged messages alone.
Requiring in addition that the sample carry a base-value camp of more
than n/2 (step 2's own condition) is a plausible-looking strengthening,
and it is invisible wherever n < 4t+2: there, more than 2t flagged
messages already exceed n/2, so the added conjunct is free.  At n =
4t+2 the two come apart.  Reachability: the oracle runs n = 6, t = 1,
where the n-t = 5 sample carries three (d, 1) and two 0 -- more than
2t = 2 flagged, but 3 is not more than n/2 = 3.  The correct machine
decides; under the mutation no decide fires, the phase is the
instance's last, so it reports EXHAUSTED and the oracle's DECIDE check
goes red.
#ANCHOR
  gt2T        = dc[dmax] > 2u * b->t;
#WITH
  gt2T        = dc[dmax] > 2u * b->t && n2Half;
#END

#MUTANT M40
#FAMILY figure 1 rule chaining -- accept reads the ready flag as it arrived
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL t=0 overtaking READY: three actions in one step
#EXPECT KILLED
#WHY
The figure's accept row carries "readySent" (Bracha87.txt), and the
accept table takes "send (ready, v)" as an input so that the conjunct
is read after the ready rules have run on this same message.  2t+1
readys implies t+1, so a process that had not sent ready fires Rule 5
here and accepts on the same message: the conjunct never withholds an
accept, which is why stating it costs nothing and keeps the row the
figure's.  Reading the flag AS IT STOOD WHEN THE MESSAGE ARRIVED is
the defect, and it withholds exactly the accepts the chain exists to
allow.  Reachability: for t >= 1 the t+1 crossing strictly precedes
the 2t+1 crossing, so the flag is already set a message earlier and
the mutation is invisible; at t = 0 the two thresholds are one integer
and the accept rides the same message as the ready.  The oracle's
first ready at a t = 0 process must produce three actions; under the
mutation the accept is withheld and two arrive.  The explorer also
goes red: the two honest smoke configs run at n = 2, t = 0, which is
exactly where this defect bites, so their frozen counts move.  That is
SENSITIVITY to the behavioral change and not a second detection.
#ANCHOR
  if (acceptV) {
#WITH
  if (acceptV && haveSentReady) {
#END

#MUTANT M41
#FAMILY figure 1 rule chaining -- the un-echoed echo crossing does not ready
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL un-echoed crossing: ECHO_ALL and READY_ALL together
#EXPECT KILLED
#WHY
Rule 2 sends (echo, v) when more than (n+t)/2 (echo, v) arrive at a
process that has not echoed; Rule 4 sends (ready, v) on the same
evidence once the process has echoed.  Chained, both fire on the
crossing message.  Suppressing the ready whenever the echo fires on
the same dispatch is the plausible reading of "if I have echoed" as a
state the message must have found -- it restores the one-message delay
the chain removes.  Reachability: a process is un-echoed at the
crossing only if the INITIAL had not arrived by then, since Rule 1
echoes on arrival; that is the ordinary
condition under loss and under a silent initiator.  The oracle drives
three echoes into a fresh n = 4, t = 1 instance with no INITIAL and
requires two actions out of the third; under the mutation the ready is
withheld and one arrives.
#ANCHOR
  if (sendReady) {
    memcpy(F1_VALUE(b), value, F1_VLEN(b));
#WITH
  if (sendReady && !sendEcho) {
    memcpy(F1_VALUE(b), value, F1_VLEN(b));
#END

#MUTANT M42
#FAMILY figure 4 step 3 -- case (i)'s value update guarded on the decided state
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL MultiPhase: no D_FLAG in step 1
#EXPECT KILLED
#WHY
Case (i) is two assignments, "decision_p := value_p := v", and only
the first is once-only: the library reports DECIDE once, so the
dispatch gates "decide v" on the decided state and leaves "adopt v"
-- the value_p half, shared with case (ii) -- to fire every phase.
Gating the whole case is the plausible slip, since the figure writes
it as one line.  The value then stays where step 2 left it, (d, v),
and rides into the next phase's step-1 broadcast, which VALID^(3i+1)
rejects (no correct process sends a d-message at step 1).
Reachability: the oracle decides at phase 0 and plays three more
phases feeding each round the value it just broadcast; under the
mutation the round-6 broadcast carries D_FLAG and the leak check goes
red.
#ANCHOR
  if (adoptV)
    b->value = dmax;
#WITH
  if (adoptV && !haveDecided)
    b->value = dmax;
#END

#MUTANT M43
#FAMILY subset membership -- gated on holding the A-Cast value
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL O1: SubSet includes a process whose value we lack
#EXPECT KILLED
#WHY
SubSet_i is { j : BA_j had output 1 }, and BKR94 Lemma 2 Part C makes
it identical at every correct process.  Nothing in it refers to whether
THIS process has the A-Cast value for j yet.  Requiring the value looks
like a safety improvement -- why name a process whose payload you
cannot read? -- and it silently breaks Part C, because which values a
process holds is a local, schedule-dependent fact while the SubSet is
a global one.  The value is owed, not lost: Bracha's retry delivers it
after the close.  Reachability: the oracle withholds one A-Cast
instance from process 0 alone, so the rest of the cluster accepts it
and BA_3 decides 1 while process 0 still has nothing for it; the
correct machine names 3 in the SubSet anyway, and under the mutation
process 0's SubSet is one short and disagrees with the other three.
#ANCHOR
    if (dec[i] == 1)
#WITH
    if (dec[i] == 1 && bkr94acsAcastValue(a, (unsigned char)i))
#END

#MUTANT M44
#FAMILY BA decision -- recorded from our own input, not the BA's output
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL O3: |SubSet| == n -- nothing was excluded
#EXPECT KILLED
#WHY
A BA's output is the cluster's, not this process's input to it.  The
two coincide under every schedule where a process enters BA_j only by
accepting j's A-Cast, which is why reading the decision off the entry
would pass most of a battery: step 1 enters 1 exactly when it will
accept, and BKR94's validity makes an all-1 BA decide 1.  They come
apart at the step-2 fanout, which enters an un-entered BA with 0 while
the rest of the cluster has already entered 1 -- the BA then decides 1
against this process's 0.  Reachability: the oracle holds two A-Cast
instances and their BAs away from process 0 long enough for its own
fanout to enter both with 0, then releases them; both decide 1, so the
correct machine closes a SubSet of all n, and under the mutation
process 0 records its own zeros and closes a SubSet of n-2 that
disagrees with every other process.
#ANCHOR
    bkr94acsDecision(a)[process] = f4->decision;
#WITH
    bkr94acsDecision(a)[process] =
      (bkr94acsEnterd(a)[process] == BKR94ACS_ENTER_ONE) ? 1 : 0;
#END

#MUTANT M45
#FAMILY annotation containment -- an accept announcement marks every readier
#FILE bracha87.c
#ORACLE test_bkr94acs_blackbox
#LABEL P1: no honest process ever recorded an accept nobody announced
#EXPECT KILLED
#WHY
The accept announcement is the one wire field no paper backs, and its
whole Byzantine argument is per-sender containment: the annotation
rides a (ready, v) whose sender authentication binds, so it marks the
SENDER and nobody else.  Widening it to every process already in the
readied set is the plausible slip -- a process that readied is on its
way to accepting anyway.  The designated arm reads the accepted
evidence at every tick of a schedule carrying a lying process and a
correct laggard, and requires every bit to be a process that actually
announced; under the mutation the readied-but-not-yet-accepted
processes appear in it and the unannounced count goes nonzero.

What reds here does NOT depend on the forger.  The mutated setter is
called on every announcement-carrying READY, a process's own hand-back
included, so the unearned bits appear in forger-free schedules too and this defect reds several lanes that have
no Byzantine process at all.  The lying process is what makes the
arm's OTHER readings meaningful -- the containment question only
exists if a lie is in flight -- but the credit here is for the
widening itself.

WHAT THIS ENTRY IS AND IS NOT.  The MECHANISM is already covered from
below and this run says so: the same defect reds the white-box
"Received: an unannounced process is not marked", the composition's
"Ingress: skip leaves process 3 clear (no accept announced)", and the
never-announcer's short-by-exactly-the-leaver's-bit reading.  The
explorer is NOT in that list: it prints count drift only, and this
harness's own preamble says a count that moves is sensitivity to a
behavioral change and never by itself a detected defect.  This entry
is not a first detector and does not claim to be.  What it red-proves
is the END-TO-END sentence -- that a forged announcement can never
strand a correct laggard -- which until the forging arm existed had no
arm at all, only the argument at the gate.

The laggard half does not red here, and the reason is narrower than
"the laggard has readied nowhere" -- it readied on the pre-cut
instances, and after the heal it readies on every instance before it
accepts, which is how it catches up.  What protects it is that the
widening can only copy a readied bit into the accepted set at the
moment an announcement arrives, and on the instances
the laggard lacks, no announcement-carrying READY arrives at an honest
process between the laggard's READY and the laggard's own true
announcement: the honest cohort is mutually suppressed on those
instances by then, and the forger's forged one came earlier.  So the
arm did not move under this mutation in this schedule, and the
recorded counters are identical to the clean run.  The laggard
sentence carries no teeth from this entry, and none are claimed.

Nor from any other.  The one anchored defect that DOES strand the
laggard -- widening the marking to ALL n rather than to the readied
set -- turns out to fire on the first announcement-carrying READY an
instance takes, its own hand-back among them, filling the mask before
any other announcement arrives; that is the local-accept READY
retire, Note 10's forbidden gate, and it is M01's class under a
different anchor (its red set is a strict subset of M01's, forger-free
lanes included).  A probe entry to that effect was built, graded
KILLED, and withdrawn once its mechanism was read.  No
announcement-containment defect reaches the laggard half; the sentence
rests on the argument at the gate.
#ANCHOR
  if (!b || from > b->n)
    return;
  BIT_SET(F1_ACFROM(b), from);
#WITH
  if (!b || from > b->n)
    return;
  {
    unsigned int q;

    for (q = 0; q < B_N(b); ++q)
      if (BIT_TST(F1_RDFROM(b), q))
        BIT_SET(F1_ACFROM(b), q);
  }
  BIT_SET(F1_ACFROM(b), from);
#END

#MUTANT M46
#FAMILY annotation containment -- an arm un-suppresses every process
#FILE bracha87.c
#ORACLE test_bkr94acs_blackbox
#LABEL P2: and every one of them is aimed at the forger alone
#EXPECT KILLED
#WHY
The other half of the containment argument: an unmarked READY
un-suppresses ITS OWN SENDER for one egress, so a forger that keeps
re-sending unmarked buys one masked READY per instance per pass of the
victim's cursor, aimed back at itself, and displaces nothing owed to a
correct process.  Arming every process instead turns each lie into a
broadcast the whole cohort pays for.  The designated arm prices every
honest READY egress by its recipient set -- the set the caller's
broadcast would actually reach, read off the act's own suppress mask --
and requires that set to be the forger alone; under the mutation the
first arm each pass carries that instance's READY to the honest
processes too and the reaching-a-correct-process count goes nonzero.

WHAT THIS ENTRY IS AND IS NOT, and the run is the authority.  The
MECHANISM is covered from below at the unit level: this defect reds
the black-box "Resend: un-suppresses the armed sender only" and the
white-box "Resend: an un-armed accept stays suppressed" and "Gate: the
egress's mask excludes the armed sender only".  The two suites carry
DIFFERENT check text -- an earlier draft of this paragraph claimed one
string in both and the tree refuted it -- so read the strings, not the
summary.  The composed arm was expected to be first at this and is
not.

THE DESIGNATED LABEL IS SENSITIVE, NOT SPECIFIC, and that is a
limitation of this entry rather than of the arm.  P2's
reaching-a-correct-process reading goes red under M15 (the suppress
mask's negation dropped) and under M29 (the RECEIVED mask dropped at
the A-Cast retry egress) with a signature indistinguishable from this
one, because all three end with an egress whose recipient set is no
longer the forger alone.  The masks genuinely differ -- under M15 the
forger stays suppressed, under this mutation nobody is -- and no arm
in the battery separates them.  A reader taking KILLED here as
"the per-sender routing of the arm is what failed" is reading more
than the grade carries.

What the composed arm adds that no unit arm states is the drain: stop
the forger and the residue owes one more masked READY per armed
instance and then falls silent, at a depth its rate did not buy.  That
is the bitmap rather than the counter, and it is measured by the arm's
two rates plus its drain lane, not detected -- no single anchored
defect in this catalogue makes it fail.
#ANCHOR
  BIT_SET(F1_ARMFROM(b), from);
  BIT_CLR(F1_SKFROM(b), from);
#WITH
  {
    unsigned int q;

    for (q = 0; q < B_N(b); ++q) {
      BIT_SET(F1_ARMFROM(b), q);
      BIT_CLR(F1_SKFROM(b), q);
    }
  }
#END

#MUTANT M47
#FAMILY retire gates -- the whole-action retire ignores outstanding arms
#FILE bracha87.c
#ORACLE test_bkr94acs_blackbox
#LABEL P3: the retry still owes after the gate, mask-complete or not
#EXPECT KILLED
#WHY
The READY whole-action retire reads the effective mask -- the announced
accepts NET OF the outstanding arms -- and reading the raw accepted set
instead retires the action while a process is still showing that it
lacks this instance's announcement.  That is the stranding the second
annotation exists to prevent, and it is the exact defect a forged
announcement would exploit if containment did not hold: an announcement
this process never earned would then close its gate for good.  The
designated arm is the residue lane, where the accepted evidence covers
all n -- the forger accepts and announces like anyone else, and turning
its announcement forgery off moves none of that -- while its unmarked
re-sends keep taking the arm; the arm requires the retry to still owe,
and to owe it to the forger alone.  Under the mutation the count reaches n on the raw set,
the action retires, and the still-owes check reads zero -- red.

This is a second detector at a state the others do not reach, not a
first one, and the run names the others: the white-box "Gate: an arm
keeps READY alive at all-n accepted" and its phase-offset stranding
arm, and the composed quiescence lane, whose processes never reach the
zero return because the one whose arm is ignored never receives the
marked re-send.  What this arm holds that they do not is the shape a
re-arming adversary produces: evidence complete at all n and still
owing, indefinitely, against a cohort that has nothing left to answer
with.  The never-announcer residue lane is its opposite -- evidence
short by one bit -- so between them the two readings separate the two
ways a run can fail to fall silent.

THE DESIGNATED LABEL IS SENSITIVE, NOT SPECIFIC.  It reads zero
whenever the residue egress dies for ANY reason, and M02 -- READY
quiescence on a lowered count threshold, a different file and a
different defect class -- produces a P3 signature identical to this
one, check for check.  The white-box gate arm does not separate them
either; both red it.  What does separate them in the recorded run is
the explorer's third smoke config, which under this mutation reds on
its frozen counts and under M02 on the readied set at the initiator
being short of a correct process.  That
reading is not this entry's designated credit; it is named here so the
grade is not read for more than it carries.
#ANCHOR
    readyMaskFull = fig1FromCnt(sk, B_N(b)) >= B_N(b);
#WITH
    readyMaskFull = fig1FromCnt(ac, B_N(b)) >= B_N(b);
#END

#MUTANT M48
#FAMILY ingress range -- the A-Cast process index unchecked
#FILE bkr94acs.c
#ORACLE test_ingress
#LABEL a refused call changed the receiver's image
#EXPECT KILLED
#WHY
n ENCODES the process count (actual = n + 1), so the admissible
indices are 0..n and everything above is a field value an adversary
chose.  Dropping the check does not merely admit it: acastF1 then
computes an instance address past the A-Cast array, and
bracha87Fig1Input writes its per-sender echo record there before any
rule can fire -- so the entry returns 0 acts, looking exactly like an
ordinary dedup, while a Fig 1 belonging to some BA pipeline has been
written through.  That is the shape this oracle exists for: the
return value is the same as a correct machine's and only the image
tells them apart.  No honest generator produces an out-of-range index,
so no other suite sends one; the ingress instrument sweeps the whole
0..255 field at every milestone and restores the receiver between
calls, so the byte comparison against the milestone is what reds.

WHAT THIS ENTRY IS AND IS NOT.  It is NOT the first detector, and the
run says so: test_bkr94acs_blackbox's H2 arm ("out-of-range process
refused") reds too, from the header alone.  What that arm reads is the
RETURN -- no acts -- which a machine that validated after writing would
also produce.  This entry's designated label is the other half, that
the receiver is byte-identical afterward, and no arm in the contract
suites checks it.  Credit the entry for the identity, not the refusal.
#ANCHOR
  if (!a || process > a->n || from > a->n || !value || !out)
#WITH
  if (!a || from > a->n || !value || !out)
#END

#MUTANT M49
#FAMILY ingress range -- the BA round unchecked
#FILE bkr94acs.c
#ORACLE test_ingress
#LABEL a refused call changed the receiver's image
#EXPECT KILLED
#WHY
A BA's Fig 1 instances are keyed by round over the space Fig 4
instantiates, maxPhases * 3, and the round arrives on the wire.
Without the bound the pipeline offset is computed from a round outside
that space and the write lands past the BA's own region -- again
returning 0 acts, because the Fig 1 it reached is not one this
schedule has driven.  The honest cohort never emits a round it has not
reached, so the round field is another one no other suite varies.  The
instrument sweeps it 0..255 with every other field legal, so the
attribution is to the round alone.

WHAT THIS ENTRY IS AND IS NOT, the same shape as M48's: the contract
suite's H2 arm ("BA out-of-range round refused") is the first detector
and reads the return; this entry's designated label is the identity
half that arm does not read.
#ANCHOR
  if (round >= maxRounds(a))
    return (0);
#WITH
#END

#MUTANT M50
#FAMILY ingress order -- the announcement recorded before its guard
#FILE bracha87.c
#ORACLE test_ingress
#LABEL an out-of-range announcement was recorded
#EXPECT KILLED
#WHY
bracha87Fig1ProcessAccepted returns void, so its whole contract is
"out-of-range 'from' and a null instance are ignored" -- there is no
return value a caller could read and no value a test could assert.
Moving the RANGE half of the guard below the bit set keeps that
contract's LETTER (the call still returns nothing) and breaks it
entirely: an out-of-range announcement sets a bit outside the acFrom
bitmap.  This is the entry that proves why the identity oracle is not
a convenience -- for a void entry it is the ONLY oracle there can be.
The instrument sweeps 'from' over the whole field at each Fig 1
milestone and compares the instance against the milestone image.
The null half of the guard is deliberately LEFT IN PLACE: moving it
too would fault several suites before any check ran, and a mutation
that announces itself with a signal is graded CRASH here and credits
nothing.  One anchored defect means one, and the one this entry is
about is the range.

This is one of the two entries whose designated oracle is the ONLY
suite that reds -- measured in the run, not assumed.  Nothing else in
the battery calls a void entry with an argument outside its range.
#ANCHOR
  if (!b || from > b->n)
    return;
  BIT_SET(F1_ACFROM(b), from);
#WITH
  if (!b)
    return;
  BIT_SET(F1_ACFROM(b), from);
  if (from > b->n)
    return;
#END

#MUTANT M51
#FAMILY annotation ingress -- a bit the header calls ignored is read
#FILE bkr94acs.c
#ORACLE test_ingress
#LABEL a bit the header calls ignored is being read
#EXPECT KILLED
#WHY
bkr94acsAcastInput's contract is that only BKR94ACS_ACCEPTED and
BKR94ACS_RECEIVED are read off annot, and only on a READY, "so a
caller may pass the whole packed discriminator byte unmasked and every
other bit is ignored" (bkr94acs.h).  Every framer in this repository
does pass the raw byte, and bit 6 of the canonical layout is reserved
for the APPLICATION's own message classes -- so a machine that read it
would take an application's private bit as a protocol annotation, and
an adversary setting it would retire a retry that is still owed.  The
defect is invisible to any suite that masks before calling, which is
every other one.  The instrument holds one call fixed and runs all 256
annot values, requiring the outcome to be constant within each
(bit 4, bit 5) class; widening the mask splits a class and reds.
Scope, stated because the grade does not carry it: this oracle is
ONE-DIRECTIONAL.  It catches a bit being read that should not be; it
cannot catch either documented bit going unread, and no entry here
claims otherwise.
#ANCHOR
  nf1 = bracha87Fig1Input(f1, type, from, value, annot & BKR94ACS_ACCEPTED,
                          annot & BKR94ACS_RECEIVED, f1out);
#WITH
  nf1 = bracha87Fig1Input(f1, type, from, value,
                          annot & (BKR94ACS_ACCEPTED | 0x40),
                          annot & BKR94ACS_RECEIVED, f1out);
#END
#MUTANT M52
#FAMILY figure 4 step 1 -- the round transition's tie broken to 1
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Fig4Round sub 0: 2:2 tie breaks to 0
#EXPECT KILLED
#WHY
Fig 4 step 1 sets value_p to the majority of the validated sample and
names no tie.  A tie is reachable because the sample can be even, and
the library breaks it to 0 at two sites that must agree: fig4Nfn on the
validation side, and bracha87Fig4Round's sub-0 value update.  The
fig4Nfn site has its subset-majority arms; this entry anchors the other
site.  Every end-to-end run in the battery starts its BAs from a
unanimous or odd sample, so a flipped tie there is a false majority of
the right shape and nothing downstream reds; the oracle hands the
round a 2:2 sample directly and requires 0.  Verified by hand before
this entry was written: under the mutation exactly the two tie checks
go red and nothing else in the suite does.
#ANCHOR
    b->value = (cnt[1] > cnt[0]) ? 1 : 0;
#WITH
    b->value = (cnt[1] >= cnt[0]) ? 1 : 0;
#END
#MUTANT M53
#FAMILY figure 4 step 3 -- the adopt case read as free
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Adopt window: with more than t (d,0) in every subset, the other value is rejected
#EXPECT KILLED
#WHY
Fig 4 step 3 has two deterministic cases and one free one: (i) decides
v on more than 2t (d,v), (ii) adopts v on more than t, and only (iii)
tosses.  fig4Nfn answers Fig 3's existential over n-t subsets of the
round-3i+2 set, so a phase-opening message carrying the other value
validates only if some subset reaches (iii); when every subset holds
more than t (d,dm) -- dc[dm] - excess > t -- only dm is producible and
the answer is exact.  This mutation raises the exactness floor to the
decide case's 2t, which admits the other value in the window
t < dc[dm] - excess <= 2t, a value no n-t subset of this validator's
current set produces (a correct sender whose own sample reached the
coin may send it; it is then stored and re-evaluated as the set grows,
so the rejection is a deferral).  The
oracle builds that window at n=4 t=1 -- two (d,0) and one plain 0 in
a complete round-2 set, every 3-subset the whole set -- and requires
a round-3 value 1 to be rejected.  The predicate correspondence is a
second detector: its reference enumerates the subsets and applies (ii)
as the paper writes it, and disagrees on 42 of 960 inputs under this
mutation.  No end-to-end arm sees it: every schedule in the battery
opens its phases from a unanimous sample, and a wrongly admitted value
that never arrives changes nothing.
#ANCHOR
        if (dc[dm] - excess > f4->t) {
#WITH
        if (dc[dm] - excess > 2u * f4->t) {
#END

#MUTANT M54
#FAMILY counter width -- the Fig 1 sender coverage narrowed to a byte
#FILE bracha87.c
#ORACLE test_ceiling
#LABEL Fig1: all echoed at 256
#EXPECT KILLED
#WHY
Two Fig 1 retire gates compare a sender count against n: INITIAL
retires when every process has echoed, READY when every process has
accepted and none is owed an announcement.  Both counts come from one
bitmap scan, and narrowing its accumulator to a byte makes each read
0 at exactly the coverage that should retire -- so at 256 processes
neither gate closes and bracha87Fig1AllEchoed never answers 1.  The
oracle feeds an initiator 256 echo senders and then 256 accept
announcements, requiring each gate open at 255 and closed at 256;
under the mutation the 256th reads as 0 and all three closings go
red.  Invisible below the ceiling: the scan holds every count up to
255 exactly.
#ANCHOR
  unsigned int cnt;
  unsigned int j;

  cnt = 0;
  for (j = 0; j < N; ++j)
    if (BIT_TST(from, j))
#WITH
  unsigned char cnt;
  unsigned int j;

  cnt = 0;
  for (j = 0; j < N; ++j)
    if (BIT_TST(from, j))
#END

#MUTANT M55
#FAMILY counter width -- the Fig 2 received count stored in a byte
#FILE bracha87.c
#ORACLE test_ceiling
#LABEL Fig2: received count 256
#EXPECT KILLED
#WHY
Fig 2's per-round received count is compared against n - t and read
back through bracha87Fig2RecvCount.  Narrowed to a byte at its
increment it wraps to 0 on the 256th sender of a full house; the
threshold crossing at n - t is untouched, which is what makes this
invisible at every battery size and at the ceiling everywhere but the
count read back.  The oracle receives one round from all 256 senders
and requires the count to read 256.
#ANCHOR
  ++F2_RCNT(b, k);
#WITH
  F2_RCNT(b, k) = (unsigned char)(F2_RCNT(b, k) + 1);
#END

#MUTANT M56
#FAMILY counter width -- the Fig 3 VALID^k count stored in a byte
#FILE bracha87.c
#ORACLE test_ceiling
#LABEL Fig3: valid count 256
#EXPECT KILLED
#WHY
Fig 3's per-round VALID^k count is read by the next round's
validation gate (VALID^{k+1} needs n - t validated at k) and by the
composition's turn duty (MET at all n validated).  The count has two
increments, the direct one here and the cascade's (M61).  Narrowed to
a byte here it wraps to 0 when all 256 senders of a full house
validate directly in one round, and every round after it stalls: the
gate reads 0 < n - t forever.  The oracle validates round 0 from all
256 senders and requires the count to read 256; the ACS half reds
too, since no BA's round ever turns.  The threshold crossing at n - t
fires before the wrap, which is why no smaller configuration sees
it.
#ANCHOR
    ++*vcnt;
#WITH
    *vcnt = (unsigned char)(*vcnt + 1);
#END

#MUTANT M57
#FAMILY counter width -- the Fig 4 per-value tallies narrowed to a byte
#FILE bracha87.c
#ORACLE test_ceiling
#LABEL Fig4: step 1 majority over 256
#EXPECT KILLED
#WHY
bracha87Fig4Round tallies the round's values and their (d, v) marks
and compares the tallies against n / 2, 2t and t.  Over a unanimous
full house of 256 a byte tally wraps to 0: step 1 sees a 0:0 tie and
takes the tie-break value 0, step 2 finds no value above n / 2, step 3
finds no (d, v) above 2t.  The oracle computes all three rounds over
256 copies of value 1 and requires the majority, the (d, 1) and the
decision; the ACS half reds too, no BA deciding: its step-3 tally
finds no (d, 1) at all and the phase ends on the coin.  Below the
ceiling the tallies fit a byte.  The N that validates the next round
keeps tallies of its own; those are M60.
#ANCHOR
  unsigned int cnt[2];
  unsigned int dc[2];
  unsigned int sub;
  unsigned int ph;
#WITH
  unsigned char cnt[2];
  unsigned char dc[2];
  unsigned int sub;
  unsigned int ph;
#END

#MUTANT M58
#FAMILY counter width -- the step-2 BA-output-1 count narrowed to a byte
#FILE bkr94acs.c
#ORACLE test_ceiling
#LABEL ACS: fanout TOLERANCE on 256 BA outputs of 1
#EXPECT KILLED
#WHY
bkr94acsFanoutDuty derives the BA-output-1 count by scan and compares
it against n - t.  A byte accumulator wraps to 0 when all 256 BAs
have output 1, so a full house of 1-outputs with anything left
unentered reads HELD where it should read TOLERANCE.  The oracle
drives all 256 BAs to 1 with no A-Cast entered and requires
TOLERANCE.  The threshold at n - t fires before the wrap at every
smaller size, and at 256 the same scan holds any count under 256, so
only the full house sees it.
#ANCHOR
  unsigned int one;
#WITH
  unsigned char one;
#END

#MUTANT M59
#FAMILY counter width -- the unentered count narrowed to a byte
#FILE bkr94acs.c
#ORACLE test_ceiling
#LABEL ACS: fanout HELD with 256 unentered
#EXPECT KILLED
#WHY
bkr94acsFanoutDuty's MET is an EMPTY unentered set, derived by scan.
A byte accumulator wraps to 0 whenever all 256 BAs are unentered, so
the duty reads MET -- nothing to enter -- on a fresh 256-process
instance and again once every BA has output 1 with none entered,
where bkr94acsFanout, which fires only on TOLERANCE, has 256 BAs to
enter with 0 and cannot fire.  The oracle queries the duty of a
fresh instance and requires HELD; the TOLERANCE check at the end of
the drive reds as well.  Below the ceiling the unentered count fits
a byte at every moment.
#ANCHOR
  unsigned int unentered;
#WITH
  unsigned char unentered;
#END

#MUTANT M60
#FAMILY counter width -- the N function's tallies narrowed to a byte
#FILE bracha87.c
#ORACLE test_ceiling
#LABEL ACS: every turn MET on 256 validated
#EXPECT KILLED
#WHY
fig4Nfn is Fig 4's N, called by Fig 3 over the whole validated set of
the previous round to decide what the next round's messages may
carry.  Its tallies are its own, not bracha87Fig4Round's (M57), and
over a unanimous full house of 256 a byte tally wraps to 0: the
step-1 majority reads 0:0 and answers 0, so every round-1 message
carrying the true majority 1 is rejected and no round after 0 ever
completes.  The bare figures do not reach it -- the bare Fig 3 arm's
N is the suite's own -- so the oracle is the ACS half: with no BA's
round 1 turnable, the turn duty stays HELD from round 1 on and the
MET count falls short.  Green everywhere else: the predicate suite's
reference runs at n = 4, where the tallies fit a byte.
#ANCHOR
  unsigned int cnt[2];
  unsigned int dc[2];
  unsigned int i;
  unsigned int sub;
#WITH
  unsigned char cnt[2];
  unsigned char dc[2];
  unsigned int i;
  unsigned int sub;
#END

#MUTANT M61
#FAMILY counter width -- the Fig 3 VALID^k count's cascade increment in a byte
#FILE bracha87.c
#ORACLE test_ceiling
#LABEL Fig3: cascade valid count 256
#EXPECT KILLED
#WHY
The VALID^k count's second increment: a message stored before its
previous round was complete is validated later by the cascade, when
that round crosses n - t, and counted here rather than at M56's site.
Narrowed to a byte, a round validated entirely by cascade reads 0
when all 256 senders of a full house were stored early, and the round
after it never validates.  The oracle stores round 1 from all 256
senders before round 0 has validated any, then completes round 0, and
requires round 1's count to read 256, round 1 complete, and a round-2
message to validate over it.  No other arm stores a full house ahead
of its round: the ACS half feeds rounds in order, so the cascade
there finds nothing to validate.
#ANCHOR
            ++F3_VCNT(b, r);
#WITH
            F3_VCNT(b, r) = (unsigned char)(F3_VCNT(b, r) + 1);
#END

#MUTANT M62
#FAMILY figure 4 step 1 -- the majority update guarded on the decided state
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Post-decide ungated: step 1 follows the majority
#EXPECT KILLED
#WHY
Figure 4 has no decided state, so step 1 sets value_p to the majority
of the sample after a decision as before it.  A guard here is the
same "preserve the decision" reading as M21 at step 2, and unlike
M21 it is invisible on every sample the model presents: after a
decision the majority IS the decision (Lemma 9), so the guarded and
the unguarded machine write the same value.  The only witness is a
sample Lemma 9 excludes, which is what the oracle feeds -- a decided
process handed a step-1 majority against its decision -- requiring
the figure's answer.  Under the mutation the value stays at the
decision and the check goes red.
#ANCHOR
  if (setMajority)
    b->value = (cnt[1] > cnt[0]) ? 1 : 0;
#WITH
  if (setMajority && !haveDecided)
    b->value = (cnt[1] > cnt[0]) ? 1 : 0;
#END

#MUTANT M63
#FAMILY figure 4 step 3 -- the coin guarded on the decided state
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Post-decide ungated: step 3 tosses without a d-message
#EXPECT KILLED
#WHY
Case (iii) tosses when no more than t (d, v) messages are validated,
decided or not; the figure has no decided state to consult.  Guarding
the toss is the same reading as M62, and it is invisible on the same
grounds: after a decision every validated step-3 message is (d, v)
(Lemma 9), so case (iii) is never reached by a correct process.  The
oracle hands a decided process a step-3 sample with no d-message and
requires the coin; under the mutation the value stays where step 2
left it and the check goes red.
#ANCHOR
  if (setCoin)
    b->value = b->coin(b->coinClosure, b->instance, ph);
#WITH
  if (setCoin && !haveDecided)
    b->value = b->coin(b->coinClosure, b->instance, ph);
#END

#MUTANT M64
#FAMILY paired-payload retire -- the readied set read as the echoed set
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL Q1: every correct process is in the retire set at its initiator
#EXPECT KILLED
#WHY
bkr94acsAcastReadied is the set a paired side channel retires on per
process, and the header says why it is the READY set and not the ECHO
set: ECHO is suppressed toward readied processes and retires at
ACCEPTED, so a process whose first echo fires after the initiator's
READY reached it is never recorded there, while READY is re-sent until
that process's accept is recorded.  The oracle forces exactly that
order -- one process takes no row of the initiator's A-Cast until the
initiator has sent READY -- drives the annotation exchange to
quiescence, and requires every correct process in the set at its
initiator.  Under the mutation the entry hands back the INITIAL mask,
the echoed set, which is short at one or more honest initiators in
every lane -- in the lossless lanes at the held initiator alone, short
of the held process; under loss at whichever initiators loss left
short -- and the check goes red.  This is the defect the tree carried
through f84b731: the two side-channel entries read the echoed set,
which no convergence-reading arm sees, since nothing the protocol does
to converge needs it to close.  The white-box readied-set arm in
test_bkr94acs reds too.
#ANCHOR
  return (bracha87Fig1Skip(acastF1(a, process),
                           BRACHA87_ECHO_ALL));
#WITH
  return (bracha87Fig1Skip(acastF1(a, process),
                           BRACHA87_INITIAL_ALL));
#END

#MUTANT M65
#FAMILY paired-payload retire -- the all-or-nothing stop read off the echoed set
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL Q1: the all-or-nothing stop reads 1 at quiescence
#EXPECT KILLED
#WHY
The whole stop is the same set covering every other process.  Reading
the echoed set instead is M64's defect at the stop: the late process's
echo never arrives at the initiator, so the stop never reads 1 and a
side channel pinned to it re-sends until the application abandons.
The oracle's lanes quiesce with every process readied and require the
stop to read 1; under the mutation it reads 0 at every initiator whose
late process echoed after the READY, and the check goes red.  The
silent-process lane is unaffected either way and does not discriminate.
#ANCHOR
  rd = bracha87Fig1Skip(acastF1(a, process), BRACHA87_ECHO_ALL);
#WITH
  rd = bracha87Fig1Skip(acastF1(a, process), BRACHA87_INITIAL_ALL);
#END

#MUTANT M66
#FAMILY suppress mask -- ECHO suppressed toward echoed processes
#FILE bracha87.c
#ORACLE test_bkr94acs_blackbox
#LABEL C7: silent Byzantine process -- honest processes converge
#EXPECT KILLED
#WHY
The ECHO mask is the readied set, "not merely echoed, because an
echoed-but-not-readied process still consumes echoes toward its ready
threshold" (BPR.md, Suppression and the Announcements).  Widening it to
the echoed set withholds echoes from processes still short of Rule 4.
At n = 3t+1 with one silent process the (n+t)/2+1 threshold equals the
honest count, so every honest echo must reach at least two honest
processes for the first READYs to exist; a process that echoed on the
INITIAL is then skipped by every later echo and stays one short, and
with too few readiers nothing amplifies.  The oracle's silent-Byzantine
lane requires the three honest processes to converge; under the
mutation the run hits its iteration cap and the check goes red.  Also
red, incidentally: the unit mask arms in both bracha87 suites, the
readied-set arms in test_bkr94acs and in B8 and Q1 (the entries
forward the same mask), H1's quiescence, and the explorer -- two smoke
configs' frozen counts move, and s3's subset oracle reds because it
reads rdFrom through the mutated accessor.
#ANCHOR
  case BRACHA87_ECHO_ALL:
    return (F1_RDFROM(b));
#WITH
  case BRACHA87_ECHO_ALL:
    return (F1_ECFROM(b));
#END

#MUTANT M67
#FAMILY suppress mask -- INITIAL suppressed toward readied processes only
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Skip INITIAL: bit 0 set (echoed)
#EXPECT KILLED
#WHY
The INITIAL mask is the echoed set: "INITIAL is suppressed to processes
that have echoed (nothing left to induce there)".  Narrowing it to the
readied set under-suppresses -- an echoed-but-not-readied process is
still sent an INITIAL it cannot consume.  The direction is harmless to
the protocol: an extra INITIAL induces nothing at a process that has
echoed, so no driven lane moves.  Measured: red in test_bracha87 and
test_bracha87_blackbox at unit mask arms, and in the bkr94acs black-box
suite only at Q2's harness witness, which reads the INITIAL mask
through the mutated accessor; every convergence check green and the
explorer's frozen counts unchanged on every smoke config.  The oracle
feeds an
echo from process 0 and requires its bit in the INITIAL mask; under the
mutation the bit is read off rdFrom, process 0 has not readied, and the
check goes red.  The harmful direction, over-suppressing INITIAL, has
no entry here.
#ANCHOR
  case BRACHA87_INITIAL_ALL:
    return (F1_ECFROM(b));
#WITH
  case BRACHA87_INITIAL_ALL:
    return (F1_RDFROM(b));
#END

#MUTANT M68
#FAMILY annotation exchange -- the arm output never applied
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Annot: arm re-send = (ready, v) without RECEIVED, once accepted
#EXPECT KILLED
#WHY
bracha87Fig1.dtc's "arm re-send to sender" row, applied by
bracha87Fig1Input after the paper actions, arms one marked re-send toward the sender of an
unmarked (ready, v) once this instance has accepted -- the half of the
exchange that repairs a suppression taken before the announcement went
out (BPR.md, Suppression and the Announcements).  The white-box row
enumeration pre-announces the sender and requires its READY suppress
bit clear after an unmarked (ready, v) at an accepted instance; under
the mutation the bit stays set and the check goes red.  End to end the
same defect is the strand the RECEIVED annotation exists to repair,
which the H1 laggard shape and the P2 forgery lanes red on.
#ANCHOR
  if (armSender)
    bracha87Fig1ProcessResend(b, from);
#WITH
  if (0)
    bracha87Fig1ProcessResend(b, from);
#END

#MUTANT M69
#FAMILY annotation exchange -- the announcement output never applied
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Annot: record sender accepted = (ready, v) carrying ACCEPTED
#EXPECT KILLED
#WHY
bracha87Fig1.dtc's "record sender accepted" row, applied by
bracha87Fig1Input, records the sender of a (ready, v) carrying ACCEPTED
as accepted, which drives the per-process READY suppression and the
all-n retire.  With it dropped no announcement is ever recorded: every
READY is re-sent to every process forever and nothing quiesces.  The
white-box row enumeration feeds a marked (ready, v) and requires the
sender in the RECEIVED mask; under the mutation it is absent and the
check goes red.  H1 and the explorer's quiescent terminals red too.
#ANCHOR
  if (recordSender)
    bracha87Fig1ProcessAccepted(b, from);
#WITH
  if (0)
    bracha87Fig1ProcessAccepted(b, from);
#END

#MUTANT M70
#FAMILY duty table -- the turn's MET cell does not fire
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL G3: MET turn fires
#EXPECT KILLED
#WHY
bkr94acs.dtc's one asymmetry between the seams is what MET fires:
the fanout's MET is an empty unentered set and is moot, the turn's is
the full sample and firing is free.  Swapping the turn's MET cell to
the fanout's reading leaves every TOLERANCE-fired round intact and
refuses only the rounds that completed at all n -- which, at n=4 t=1
with every process delivering, is most of them, so the caller that
fires at MET is refused and the BA stalls.  The oracle builds a round
complete at all n, reads MET, calls the turn and requires acts; under
the mutation the turn returns 0 and the check goes red.  The MET cell
is duplicated across the merged dispatch's leaves, so the defect is
placed where the turn consumes the table's answer rather than in the
generated snippet.
#ANCHOR
  bkr94acsDuty(a, BKR94ACS_SEAM_TURN, process, &fire);
  if (!fire)
    return (0);
#WITH
  bkr94acsDuty(a, BKR94ACS_SEAM_TURN, process, &fire);
  if (!fire || bkr94acsDuty(a, BKR94ACS_SEAM_TURN, process, 0)
              == BKR94ACS_DUTY_MET)
    return (0);
#END

#MUTANT M71
#FAMILY retry cursor -- the sweep counter advances only on an empty pass
#FILE bkr94acs.c
#ORACLE test_bkr94acs_blackbox
#LABEL H1: no pass cost more calls than bkr94acsFig1SentCount (plus the call that crosses the wrap)
#EXPECT KILLED
#WHY
The cursor's `sweeps` is the pass boundary exactly (bkr94acs.h, the
sweep-side banner: "CLOSING A SWEEP: read the cursor's `sweeps`
counter, which the library advances on every wrap"), and every
sweep-denominated policy -- the duty patience, the barren gate --
counts it.  Advancing it only when a pass found nothing to output
leaves a live run's clock stopped: a caller's patience never elapses
and its abandon gate never fires while anything is still owed.  The
oracle counts the calls each closed pass took against the sent-instance
ceiling; under the mutation no pass closes while acts flow, the count
runs past the ceiling, and the check goes red.  Also red: Q2's
sweep-walk witness, which is bounded by the same ceiling precisely so
that this defect reds rather than hangs the suite -- unbounded, the
walk never ends against this machine, and only an alarm would.
#ANCHOR
      ++p->sweeps;
      if (p->sweepActs == 0)
        return (0);
#WITH
      if (p->sweepActs == 0) {
        ++p->sweeps;
        return (0);
      }
#END

#MUTANT M72
#FAMILY once-only accept -- the "have accepted" row fed clear
#FILE bracha87.c
#ORACLE test_bracha87_blackbox
#LABEL PostAccept: every post-accept Input returns 0 actions
#EXPECT KILLED
#WHY
"have accepted" is the input of the paper rules that Fig 1 does not
write down (bracha87Fig1.dtc): a step program ends at accept(v), so a
process accepts once, and once accepted the paper's outputs are
finished with every later arrival: every arrival runs the dispatch,
the row withholds accept(v), and the sent flags withhold the sends.
Feeding the row clear at Input makes a
post-accept (ready, v) that still meets 2t+1 fire accept(v) again, and
one that meets t+1 with ready already sent fire nothing new -- but the
first is enough: the oracle drives an instance to ACCEPT, then feeds
every later message, and requires 0 actions from each.  Under the
mutation the second accept is output and the check goes red.  Also
red: the white-box "Rule 6: post-accept ignored" and test_ceiling's
accepted-on-the-2t+1'th-ready read.  The explorer's smoke run stays
green.
#ANCHOR
  haveAccepted  = (b->flags & BRACHA87_F1_ACCEPTED) ? 1 : 0;
  ecGtHalfNT    = ec >= (B_N(b) + b->t) / 2 + 1;
#WITH
  haveAccepted  = 0;
  ecGtHalfNT    = ec >= (B_N(b) + b->t) / 2 + 1;
#END

CATALOGUE_END

# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

sums() {
  shasum -a 256 bracha87.c bkr94acs.c bracha87Fig1Rules.c \
    bkr94acsRules.c
}

# Run a command under an alarm.  No GNU timeout on this platform; perl
# raises SIGALRM and the shell sees 128 + 14.
runAlarm() {
  secs=$1
  shift
  perl -e 'my $s = shift; alarm $s; exec @ARGV; exit 127;' "$secs" "$@"
}

buildTree() {
  # $1 = directory to build in.  Returns 0 on success; compiler output
  # lands in $WORK/build.log.
  bt=$1
  ( cd "$bt" || exit 2
    $CC $CFLAGS -c -o bracha87.o bracha87.c || exit 1
    $CC $CFLAGS -c -o bkr94acs.o bkr94acs.c || exit 1
    $CC $CFLAGS -o test_bracha87 test/test_bracha87.c bracha87.o || exit 1
    $CC $CFLAGS -o test_bkr94acs test/test_bkr94acs.c bkr94acs.o \
        bracha87.o || exit 1
    $CC $CFLAGS -o test_predicates test/test_predicates.c || exit 1
    $CC $CFLAGS -o test_bracha87_blackbox test/test_bracha87_blackbox.c \
        bracha87.o || exit 1
    $CC $CFLAGS -o test_bkr94acs_blackbox test/test_bkr94acs_blackbox.c \
        bkr94acs.o bracha87.o || exit 1
    $CC $CFLAGS -o test_schedules test/test_schedules.c bkr94acs.o \
        bracha87.o || exit 1
    $CC $CFLAGS -o test_ingress test/test_ingress.c bkr94acs.o \
        bracha87.o || exit 1
    $CC $CFLAGS -o test_ceiling test/test_ceiling.c bkr94acs.o \
        bracha87.o || exit 1
  ) > "$WORK/build.log" 2>&1
}

runBattery() {
  # $1 = directory holding the binaries, $2 = output prefix.
  # Writes $2.<bin> per suite and $2.status as "<bin> <status>" lines.
  rb=$1
  rp=$2
  : > "$rp.status"
  for b in $BINS; do
    if [ "$b" = test_schedules ]; then
      ( cd "$rb" && runAlarm $ALARM ./test_schedules smoke ) \
        > "$rp.$b" 2>&1
    else
      ( cd "$rb" && runAlarm $ALARM "./$b" ) > "$rp.$b" 2>&1
    fi
    echo "$b $?" >> "$rp.status"
  done
}

statusOf() {
  awk -v b="$2" '$1 == b { print $2 }' "$1"
}

# ---------------------------------------------------------------------
# Pristine copy, verified at copy time
# ---------------------------------------------------------------------

echo "=========================================================="
echo "mutant tier"
echo "=========================================================="
echo
echo "machine sources before the run:"
sums | sed 's/^/  /'
sums > "$WORK/sums.before"
echo

# The per-entry outputs too: a withdrawn entry's output would otherwise
# outlive the catalogue that no longer names it.
rm -rf "$WORK/pristine" "$WORK/clean" "$WORK/build" "$WORK"/out.* \
       "$WORK"/clean.out.*
mkdir -p "$WORK/pristine/test" || exit 2
for f in $SOURCES; do
  cp "$f" "$WORK/pristine/$f" || exit 2
done
for f in $SUITESRC; do
  cp "test/$f" "$WORK/pristine/test/$f" || exit 2
done

copyDrift=0
for f in $SOURCES; do
  cmp -s "$f" "$WORK/pristine/$f" || copyDrift=1
done
for f in $SUITESRC; do
  cmp -s "test/$f" "$WORK/pristine/test/$f" || copyDrift=1
done
if [ $copyDrift -ne 0 ]; then
  echo "mutant tier FAILURE: the copy does not match the source" >&2
  exit 2
fi
echo "pristine copy verified against the sources at copy time"

# ---------------------------------------------------------------------
# Clean control
# ---------------------------------------------------------------------

cp -R "$WORK/pristine" "$WORK/build" || exit 2
if ! buildTree "$WORK/build"; then
  echo "mutant tier FAILURE: the clean control does not build" >&2
  sed 's/^/  /' "$WORK/build.log" >&2
  exit 2
fi
if [ -s "$WORK/build.log" ]; then
  echo "mutant tier FAILURE: the clean control build is not silent" >&2
  sed 's/^/  /' "$WORK/build.log" >&2
  exit 2
fi
echo "clean control built with no compiler output"

controlStart=$(date +%s)
runBattery "$WORK/build" "$WORK/clean.out"
controlEnd=$(date +%s)
cleanRed=0
for b in $BINS; do
  s=$(statusOf "$WORK/clean.out.status" "$b")
  [ "$s" = 0 ] || { echo "  clean control RED: $b status $s"; cleanRed=1; }
done
if [ $cleanRed -ne 0 ]; then
  echo "mutant tier FAILURE: the clean control is not green" >&2
  exit 2
fi
echo "clean control green; one battery takes $((controlEnd - controlStart)) s"
mkdir -p "$WORK/clean" || exit 2
for b in $BINS; do
  cp "$WORK/build/$b" "$WORK/clean/$b" || exit 2
done
echo

# ---------------------------------------------------------------------
# The rounds
# ---------------------------------------------------------------------

wanted=$*
: > "$WORK/table"
: > "$WORK/notes"
nKilled=0; nMiscredit=0; nCrash=0; nBuildfail=0; nNoop=0
nTimeout=0; nSurvivor=0; nInvisible=0; nFinding=0
runStart=$(date +%s)

ids=$(awk '/^#MUTANT /{ print $2 }' "$WORK/catalogue")

for id in $ids; do
  if [ -n "$wanted" ]; then
    hit=0
    for w in $wanted; do [ "$w" = "$id" ] && hit=1; done
    [ $hit -eq 1 ] || continue
  fi

  # Clear every section first: a record whose replacement is EMPTY (a
  # plain deletion) writes no file, and a stale one left from the last
  # round would be applied instead.
  for p in family file oracle label expect why anchor with; do
    : > "$WORK/cur.$p"
  done

  awk -v id="$id" '
    $1 == "#MUTANT" { inrec = ($2 == id); sect = ""; next }
    !inrec { next }
    $1 == "#END" { inrec = 0; next }
    $1 == "#FAMILY" { sub(/^#FAMILY /, ""); print > (out "family"); next }
    $1 == "#FILE"   { print $2 > (out "file"); next }
    $1 == "#ORACLE" { print $2 > (out "oracle"); next }
    $1 == "#LABEL"  { sub(/^#LABEL /, ""); print > (out "label"); next }
    $1 == "#EXPECT" { print $2 > (out "expect"); next }
    $1 == "#WHY"    { sect = "why"; next }
    $1 == "#ANCHOR" { sect = "anchor"; next }
    $1 == "#WITH"   { sect = "with"; next }
    sect != ""      { print > (out sect) }
  ' out="$WORK/cur." "$WORK/catalogue"

  mFamily=$(cat "$WORK/cur.family")
  mFile=$(cat "$WORK/cur.file")
  mOracle=$(cat "$WORK/cur.oracle")
  mLabel=$(cat "$WORK/cur.label")
  mExpect=$(cat "$WORK/cur.expect")

  echo "---------------------------------------------------------"
  echo "$id  $mFamily"
  echo "  file $mFile   oracle ${mOracle}   label ${mLabel}"

  rm -rf "$WORK/build"
  cp -R "$WORK/pristine" "$WORK/build" || exit 2

  # Apply.  The anchor must be present exactly once.
  applyRc=0
  perl -e '
    my ($src, $anc, $rep) = @ARGV;
    local $/;
    open my $s, "<", $src or die "open $src\n";  my $t = <$s>; close $s;
    open my $a, "<", $anc or die "open $anc\n";  my $p = <$a>; close $a;
    open my $r, "<", $rep or die "open $rep\n";  my $q = <$r>; close $r;
    my $n = () = ($t =~ /\Q$p\E/g);
    if ($n != 1) { print STDERR "anchor matched $n times\n"; exit 3; }
    $t =~ s/\Q$p\E/$q/;
    open my $o, ">", $src or die "write $src\n"; print $o $t; close $o;
  ' "$WORK/build/$mFile" "$WORK/cur.anchor" "$WORK/cur.with" || applyRc=$?

  if [ $applyRc -ne 0 ]; then
    echo "  NO-OP: the anchor did not apply"
    echo "$id^$mFile^$mOracle^$mLabel^NO-OP^-" >> "$WORK/table"
    nNoop=$((nNoop + 1))
    continue
  fi
  if cmp -s "$WORK/build/$mFile" "$WORK/pristine/$mFile"; then
    echo "  NO-OP: the source is unchanged after the apply"
    echo "$id^$mFile^$mOracle^$mLabel^NO-OP^-" >> "$WORK/table"
    nNoop=$((nNoop + 1))
    continue
  fi

  if ! buildTree "$WORK/build"; then
    echo "  BUILDFAIL"
    sed 's/^/    /' "$WORK/build.log"
    echo "$id^$mFile^$mOracle^$mLabel^BUILDFAIL^-" >> "$WORK/table"
    nBuildfail=$((nBuildfail + 1))
    continue
  fi
  if [ -s "$WORK/build.log" ]; then
    echo "  note: the build was not silent"
    sed 's/^/    /' "$WORK/build.log"
    echo "$id: build not silent" >> "$WORK/notes"
  fi

  # Shadow control: every binary that should hold the mutated file
  # must differ from the clean one.
  if [ "$mFile" = bracha87.c ]; then
    shadowBins=$BINS
  else
    shadowBins="test_bkr94acs test_bkr94acs_blackbox test_schedules \
test_ingress test_ceiling"
  fi
  shadowBad=""
  for b in $shadowBins; do
    if cmp -s "$WORK/build/$b" "$WORK/clean/$b"; then
      shadowBad="$shadowBad $b"
    fi
  done
  if [ -n "$shadowBad" ]; then
    echo "  NO-OP: identical to the clean control in$shadowBad"
    echo "$id^$mFile^$mOracle^$mLabel^NO-OP^-" >> "$WORK/table"
    nNoop=$((nNoop + 1))
    continue
  fi

  runBattery "$WORK/build" "$WORK/out.$id"

  red=""
  for b in $BINS; do
    s=$(statusOf "$WORK/out.$id.status" "$b")
    [ "$s" = 0 ] || red="$red $b($s)"
  done
  echo "  red:${red:- none}"

  if [ "$mOracle" = "-" ]; then
    # A catalogue entry that claims no oracle reaches it.
    if [ -z "$red" ]; then
      # The frozen counts are part of the explorer's status, so a green
      # explorer here also says the sensitivity signal stayed silent.
      echo "  INVISIBLE as claimed: the whole battery is green"
      echo "$id^$mFile^-^-^INVISIBLE^none" >> "$WORK/table"
      nInvisible=$((nInvisible + 1))
    else
      echo "  FINDING: claimed invisible, but the battery went red"
      echo "$id: claimed invisible, red in$red" >> "$WORK/notes"
      echo "$id^$mFile^-^-^FINDING^${red:- none}" >> "$WORK/table"
      nFinding=$((nFinding + 1))
    fi
    continue
  fi

  oStatus=$(statusOf "$WORK/out.$id.status" "$mOracle")
  if grep -F -q -- "$mLabel" "$WORK/out.$id.$mOracle"; then
    labelSeen=1
  else
    labelSeen=0
  fi

  if [ "$oStatus" = 142 ] && [ $labelSeen -eq 0 ]; then
    echo "  TIMEOUT in $mOracle"
    echo "$id^$mFile^$mOracle^$mLabel^TIMEOUT^${red:- none}" >> "$WORK/table"
    nTimeout=$((nTimeout + 1))
  elif [ $labelSeen -eq 1 ]; then
    echo "  KILLED: the designated check went red"
    echo "$id^$mFile^$mOracle^$mLabel^KILLED^${red:- none}" >> "$WORK/table"
    nKilled=$((nKilled + 1))
    if [ "$oStatus" -ge 128 ] 2>/dev/null; then
      # The designated check demonstrably fired, so this is a kill; but
      # the binary died afterwards, which is the suite reading library
      # state the mutation made absent.  Worth surfacing either way.
      echo "  note: $mOracle died on a signal after the designated check"
      echo "$id: $mOracle signal $oStatus after the designated check" \
        >> "$WORK/notes"
    fi
  elif [ "$oStatus" -ge 128 ] 2>/dev/null; then
    echo "  CRASH: $mOracle died on a signal before the designated check"
    tail -3 "$WORK/out.$id.$mOracle" | sed 's/^/    /'
    echo "$id^$mFile^$mOracle^$mLabel^CRASH^${red:- none}" >> "$WORK/table"
    nCrash=$((nCrash + 1))
  elif [ -n "$red" ]; then
    echo "  MISCREDIT: the battery went red, the designated check did not"
    echo "$id: designated label absent; red in$red" >> "$WORK/notes"
    echo "$id^$mFile^$mOracle^$mLabel^MISCREDIT^${red:- none}" >> "$WORK/table"
    nMiscredit=$((nMiscredit + 1))
  else
    echo "  SURVIVOR: the whole battery is green under this defect"
    echo "$id: SURVIVOR" >> "$WORK/notes"
    echo "$id^$mFile^$mOracle^$mLabel^SURVIVOR^none" >> "$WORK/table"
    nSurvivor=$((nSurvivor + 1))
  fi

  if [ "$mExpect" = KILLED ] && [ $labelSeen -eq 0 ]; then
    echo "$id: expected KILLED" >> "$WORK/notes"
  fi
done

runEnd=$(date +%s)

# ---------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------

echo
echo "=========================================================="
echo "table"
echo "=========================================================="
awk -F'^' '
  { printf "  %-4s %-12s %-23s %-10s %s\n", $1, $2, $3, $5, $4
    printf "       red:%s\n", $6 }' "$WORK/table"

echo
echo "=========================================================="
echo "counts"
echo "=========================================================="
printf "  KILLED      %d\n" $nKilled
printf "  MISCREDIT   %d\n" $nMiscredit
printf "  CRASH       %d\n" $nCrash
printf "  BUILDFAIL   %d\n" $nBuildfail
printf "  NO-OP       %d\n" $nNoop
printf "  TIMEOUT     %d\n" $nTimeout
printf "  SURVIVOR    %d\n" $nSurvivor
printf "  INVISIBLE   %d\n" $nInvisible
printf "  FINDING     %d\n" $nFinding
echo
echo "  a count that moves in the schedule explorer is SENSITIVITY to a"
echo "  behavioral change, never by itself a detected defect."
echo
echo "  the two suites whose queues drop silently at their caps are"
echo "  test_bracha87 and test_bkr94acs; a SURVIVOR or MISCREDIT read"
echo "  from either is provisional until the contract suites or the"
echo "  explorer confirm it."
echo
if [ -s "$WORK/notes" ]; then
  echo "  notes:"
  sed 's/^/    /' "$WORK/notes"
  echo
fi
echo "  elapsed $((runEnd - runStart)) s"
echo
echo "machine sources after the run:"
sums | sed 's/^/  /'
sums > "$WORK/sums.after"
if cmp -s "$WORK/sums.before" "$WORK/sums.after"; then
  echo "  unchanged"
else
  echo "  ** FAILURE: the machine sources drifted during the run **"
  exit 1
fi

if [ $nBuildfail -ne 0 ] || [ $nNoop -ne 0 ]; then
  echo
  echo "** this run did not run: buildfail $nBuildfail, no-op $nNoop **"
  exit 1
fi
exit 0
