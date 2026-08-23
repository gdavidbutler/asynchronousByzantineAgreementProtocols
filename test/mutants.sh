#!/bin/sh
#
# mutants.sh -- does the standing battery have teeth?
#
# The five test suites and the schedule explorer are this repository's
# whole review.  A suite that passes proves nothing by itself: it proves
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
#   named-label credit.  Four of the five suites accumulate failures
#     and print a stable label; the explorer stops at its first and
#     prints one.  A kill is credited only when the DESIGNATED label
#     appears.  A nonzero status is not a kill: the machines carry live
#     asserts with no NDEBUG anywhere, so a mutation can abort the
#     process before any check runs -- that is the mutation announcing
#     itself, and it is graded CRASH.
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
test_bracha87_blackbox.c test_bkr94acs_blackbox.c test_schedules.c"
BINS="test_bracha87 test_bkr94acs test_predicates \
test_bracha87_blackbox test_bkr94acs_blackbox test_schedules"

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
    if (retryReady) {
#WITH
    if (retryReady && !(b->flags & BRACHA87_F1_ACCEPTED)) {
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
      if (fig1FromCnt(sk, B_N(b)) < B_N(b))
#WITH
      if (fig1FromCnt(sk, B_N(b)) < 2u * b->t + 1)
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
  if ((b->flags & BRACHA87_F1_INITIATOR)
   && !(b->flags & BRACHA87_F1_ACCEPTED)
   && fig1FromCnt(F1_ECFROM(b), B_N(b)) < B_N(b))
#WITH
  if ((b->flags & BRACHA87_F1_INITIATOR)
   && !(b->flags & BRACHA87_F1_ECHOED)
   && !(b->flags & BRACHA87_F1_ACCEPTED)
   && fig1FromCnt(F1_ECFROM(b), B_N(b)) < B_N(b))
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
  if ((b->flags & BRACHA87_F1_INITIATOR)
   && !(b->flags & BRACHA87_F1_ACCEPTED)
   && fig1FromCnt(F1_ECFROM(b), B_N(b)) < B_N(b))
#WITH
  if ((b->flags & BRACHA87_F1_INITIATOR)
   && !(b->flags & BRACHA87_F1_ACCEPTED))
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
    if (retryEcho && !(b->flags & BRACHA87_F1_ACCEPTED))
#WITH
    if (retryEcho)
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
end-to-end arm cannot see; only a unit arm that presents exactly one
short of the threshold can.
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
    if (BIT_TST(F1_ECFROM(b), from))
      return (0);
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
    if (BIT_TST(F1_RDFROM(b), from))
      return (0);
    fig1SetRd(b, from, value);
#WITH
    fig1SetRd(b, from, value);
#END

#MUTANT M13
#FAMILY annotation fills -- answer mask dropped at the array retry egress
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Livelock: honest n=4 quiesces
#EXPECT KILLED
#WHY
A ready that arrives without the answer annotation is its sender saying
it does not hold this instance's accept, and the receiver arms a want
that un-suppresses it for one egress.  If the egress never carries the
annotation, every ready reads as a want, every want re-opens the
suppress mask, and the mask can never reach full coverage.  The oracle
drives n=4 all-correct instances under the caller discipline both
example loops follow and requires all four to reach the zero return
inside a small sweep bound.  Under the mutation none of them ever does,
so the quiesced count falls short and the check goes red.  Reachability
needs no loss and no adversary -- the arm is the plain schedule.
Corroborated by the explorer's quiescent-terminal reachability.
#ANCHOR
          out[i].answer = (acts[i] == BRACHA87_READY_ALL)
            ? bracha87Fig1Answer(instances[idx]) : 0;
#WITH
          out[i].answer = 0;
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
The effective mask is the accepted set MINUS the processes that have
asked for this instance's announcement.  Written as an intersection
instead, the mask is empty whenever nothing has asked -- which is the
ordinary case -- so coverage never completes and the action never
retires.  The oracle records all n accepts with no want armed and
requires the retry to stop outputting READY.  Under the mutation the
mask is zero, coverage is zero, and READY is still output -- red on
that call.
#ANCHOR
        sk[i] = ac[i] & ~wt[i];
#WITH
        sk[i] = ac[i] & wt[i];
#END

#MUTANT M16
#FAMILY want arm -- the accept guard dropped
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Want: pre-accept want does not arm (bit 1 still suppressed)
#EXPECT KILLED
#WHY
A want records that a process lacks THIS instance's accept.  Before
this instance has accepted there is no accept to announce, so arming
would un-suppress a process for an egress that has nothing to say and
would re-arm on every subsequent ready.  The oracle drives an instance
to RDSENT but not ACCEPTED, records one process's accept, then routes a
want from that same process, and requires it to remain suppressed.
Under the mutation the want arms and the suppression is cleared -- red
on that read.
#ANCHOR
  if (!b || from > b->n || !(b->flags & BRACHA87_F1_ACCEPTED))
    return;
  BIT_SET(F1_WTFROM(b), from);
#WITH
  if (!b || from > b->n)
    return;
  BIT_SET(F1_WTFROM(b), from);
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
n-t.  The paper-direct enumeration runs at n=4, t=1, where n-t is 3 and
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
on that read.  The paper-direct cascade correspondence arm is the
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
#FAMILY figure 4 -- post-decide value preservation broken
#FILE bracha87.c
#ORACLE test_bracha87
#LABEL Adversarial sub=0: value=decision (not majority)
#EXPECT KILLED
#WHY
A decided process must keep broadcasting, and what it broadcasts is its
DECISION -- not whatever majority the later samples show.  Letting the
step 1 update run after a decision lets an adversarial majority drag
the broadcast value away from the decision, which breaks the
continuation the later processes are relying on.  The oracle decides an
instance and then feeds it step 1 samples whose majority is the
opposite value, requiring the broadcast value to stay the decision.
Under the mutation it follows the majority and the check goes red on
the first such round.
#ANCHOR
  if (setMajority)
    b->value = (cnt[1] > cnt[0]) ? 1 : 0;
#WITH
  if (setMajority || (b->flags & BRACHA87_F4_DECIDED))
    b->value = (cnt[1] > cnt[0]) ? 1 : 0;
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
  if (one >= N - a->t)
#WITH
  if (one >= N)
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
  if (bracha87Fig3ValidCount(f3, nextRound) >= A_N(a))
#WITH
  if (bracha87Fig3ValidCount(f3, nextRound) >= A_N(a) - a->t)
#END

#MUTANT M28
#FAMILY retry gate -- the decided-0 arm inverted
#FILE bkr94acs.c
#ORACLE test_bkr94acs
#LABEL BPR gate: process 1 (decided 1) IS retried (post-decide)
#EXPECT KILLED
#WHY
The three-arm gate skips only an agreement that decided 0, whose
process is excluded and whose enter has already been conveyed.  An
agreement that decided 1 must keep being retried: processes that have
not yet seen the accept for that process still need this one's echoes
and readys, or their own agreement stays undecided and can be driven to
0 by the fanout, breaking cross-process agreement on the subset.
Inverting the arms skips exactly the case that must continue.  The
oracle writes a decided-1 outcome and requires the retry sweep to still
visit that process.  Under the mutation the walk skips it and the check
goes red.  The oracle drives the decision by direct write rather than
through the gate, so it is independent of the mutated line.
#ANCHOR
  return (dec != 0);  /* 0xFF and 1 -> retry; 0 -> skip */
#WITH
  return (dec != 1);
#END

#MUTANT M29
#FAMILY annotation fills -- answer mask dropped at the A-Cast retry egress
#FILE bkr94acs.c
#ORACLE test_schedules
#LABEL FAILURE: no schedule reached a QUIESCENT terminal
#EXPECT KILLED
#WHY
This is the one entry whose designated oracle is the explorer, because
nothing cheaper detects it.  Dropping the answer mask on the A-Cast
retry means every such ready reads as a want at its receiver, the want
re-opens the suppress mask, and the instance never retires its ready.
The composed suites drive to COMPLETION, not to quiescence, so they
finish green: completion is decided by the agreements, and the ready
tail this breaks runs after it.  The explorer's terminal class is
quiescence -- every process quiescent and the pool empty -- and its
whole-config assertion is that some schedule reaches one.  Under the
mutation no schedule does, and the explorer says so on the surface-2
config.  The surface-1 config does not touch this file and stays green,
so the attribution is clean.
#ANCHOR
    out[nact].value = cv;
    out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
    out[nact].answer = (f1out[k] == BRACHA87_READY_ALL)
      ? bracha87Fig1Answer(f1) : 0;
#WITH
    out[nact].value = cv;
    out[nact].skip = bracha87Fig1Skip(f1, f1out[k]);
    out[nact].answer = 0;
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
#ORACLE -
#LABEL -
#EXPECT INVISIBLE
#WHY
Counts compared against the ACTUAL process count must hold 256, one
past what a byte can carry, so a byte counter wraps to 0 on the 256th
increment and a comparison against 256 can never fire.  Narrowing the
derived decided count reinstates exactly that: at 256 processes the
completion condition becomes unreachable.  NO ORACLE IN THIS BATTERY
REACHES IT.  The largest configuration anywhere is 37 processes, in the
large-n reliable-broadcast arm; every other arm runs at 16 or fewer,
and the explorer hosts at most 8.  Nor does any frozen count move: at
these sizes the narrowed counter holds the same values, so the mutated
machine is behaviorally identical within the battery's reach and even
the sensitivity signal is silent.  Fielded to record the fact, not to
be killed.
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
    out[nact].answer = 0;
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
CATALOGUE_END

# ---------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------

sums() {
  shasum -a 256 bracha87.c bkr94acs.c
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

rm -rf "$WORK/pristine" "$WORK/clean" "$WORK/build"
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
    shadowBins="test_bkr94acs test_bkr94acs_blackbox test_schedules"
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
