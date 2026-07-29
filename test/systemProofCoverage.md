# system.md -- the honest-coverage register

## What this register is

One row per premise the proofs of `system.md` consume, mapped to the
instrument arm that tests it and to an honest status.  The charter it
answers, in one sentence: this layer has no paper, so THE PROOFS
THEMSELVES ARE TESTED rather than taken as peer-reviewed the way
`Bracha87.txt` and `BKR94ACS.txt` are.

It is a VALIDATION ARTIFACT and stands alone.  Every row cites a
record that lives in this repo; where no record exists the row reads
UNCOVERED rather than guessing, and where a record says a red is
seed-dependent, mode-bound or configuration-bound the row says so.
Counts are the instruments' own, reproduced verbatim.

## Status values

  RED        MATCHED RED.  A live countermodel proves the premise
             load-bearing: the designated oracle fires when, and only
             when, the premise is withdrawn or the mechanism mutated.
  GREEN      CONTROL GREEN.  A predicted NON-failure, measured: the
             withdrawal costs what the spec says it costs and nothing
             else.  Building it and running it IS the result.
  ABSORBED   Briefed as falsifying, measured as absorbed.  The reason
             is recorded and is itself a finding.
  SIZING     The outcome is a stated sizing obligation, not a red.
  HAND       HAND + CONTRACT.  Unreachable by search for a stated
             structural reason; carried by the hand proof plus a
             named contract-suite section.
  UNCOVERED  Named, with the reason no instrument reaches it.

## Citation and reproduction legend

Evidence cells cite a file and a line or section, plus the count the
record carries:

  md N     `system.md`, line N
  ctr X    `test/test_system.c`, section X (A-J); ctr N = line N
  inv N    `test/test_system_invariant.c`, line N (header runs to 176)
  seam N   `test/test_system_seam.c`, line N (header runs to 1552)
  mm N     `test/machineMutants.sh`, line N (header runs to 88)

Reproduction tokens:

  chk   `make check`            (runs the contract suite)
  inv   `make test_system_invariant` / `_hrtwin`
  mut   `make machine-mutants`
  cfg   `make seam-configs`
  prem  `make seam-premises`
  sch   `make seam-sched`
  loss  `make seam-loss`
  glue  `make test_system_seam CPPFLAGS=-D<mutant>`
  enum  `make seam-enum`

## 1. The state invariant I1-I11 (md 656-922)

| conjunct | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| I1 distinctness | MM_WRAP_LOOKAHEAD_SKIP | HAND | ctr F x5 | mut |
| I2 disjoint | none in tier | UNCOVERED | inv 258 | inv |
| I3 self possesses | none in tier | UNCOVERED | inv 259 | inv |
| I4 short of n | none in tier | UNCOVERED | inv 260 | inv |
| I5 latch | MM_I5_LATCH_AT_T | RED | path 82 | mut |
| I5 no self | MM_I5_COUNT_SELF | RED | path 81 82 | mut |
| I6 clear | MM_I6_BOOK_SURVIVES | RED | ctr H x4 J x10 | mut |
| I7 F not held | none in tier | UNCOVERED | inv 264 | inv |
| I8 at all-n | MM_I8_EARLY | RED | ctr C x1 D x1 | mut |
| I9 announced | MM_I9_SLOT_NOFREE | RED | ctr E F J | mut |
| I10 birth byte | MM_I10_RETAIN_WRONG | RED | ctr B C D | mut |
| I11 oldest | MM_I11_EVICT_NEWEST | RED | ctr E x3 J x1 | mut |

Notes.

- I1's COUNT arm is not a proof obligation at all: the allocation
  enforces it (md 668-670).  What IS an obligation -- distinctness and
  strictly-behind-within-255 -- rests on the frontier+1 lookahead
  release, and the falsifier cannot reach that guard at any feasible
  horizon (an entry 255 rounds behind the completing frontier; inv
  36-40, mm 53-60).  MM_WRAP_LOOKAHEAD_SKIP is DORMANT in the
  falsifier at all three configurations tried, and dormant for a
  STRUCTURAL reason: the horizon bounds an entry to HORIZON-1 behind,
  while the guard needs 255, so all three runs reproduce the clean
  machine's state and transition counts exactly (inv 129-148).  Its
  only red is contract section F, five checks in both wrap regimes.
- I2, I3, I4 and I7 carry falsifier arms that have never fired: no
  mutant in the tier designates them (mm 23-71 lists nine mutations,
  none aimed at these four).  Their arms remain UNFALSIFIED WITNESSES
  in the file's own words (inv 89-90).  Contract section D exercises
  the I2 DIRECTION (a riding indication records possession and retires
  the want) as a green witness, not as a red.
- The tier's FAIL counts are 9 for every mutant BY CONSTRUCTION (eight
  violations stop run 0, the ninth stops run 1), so what a row records
  is WHICH conjunct fired and the first-violation action path, never a
  count (inv 99-104).
- The unmutated frozen enumeration: 621,094 states / 43,711,360
  transitions per run, asserted in-program with `-DEXPECTSTATES`
  (inv 82-84, 159).

## 2. L1 BOUNDED HOLD (md 926-1113)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| A6 gates, pinned shut | W_A6_PIN0 | GREEN | 12643/0, seam 766 | prem |
| A6 gates, pinned open | W_A6_PIN1 | GREEN | 16095/0, seam 770 | prem |
| T_p corollary | BYZ_WITHHOLD/SILENT | GREEN | seam 596-599 | cfg |
| A4 delivery | W_A4_PARTITION | GREEN | 4048/0, seam 746 | prem |
| A5 O1 inference | W_A5_NOINFER | GREEN | 641/0, seam 815 | prem |
| A9 for R4's floor | W_A9_SYBIL | RED | D-arm x5, s841 | prem |
| SERVE floor | W_SERVE_CAP0 | RED | 211, seam 945 | prem |
| SERVE rotation | W_SERVE_ROTDROP | ABSORBED | 14049/0, s981 | prem |
| rotation control | W_SERVE_ROTOK | GREEN | 14049/0, s1033 | prem |
| REACH proviso | W_REACH_WSHRINK | SIZING | 14073/0, s1077 | prem |
| M1 retirement | M_LEG_LOCALRETIRE | RED | seam 253, 334 | glue |
| R2c continuation | W_R2C_SILENT | GREEN | 12999/0, s1044 | prem |
| A2, more than t | none built | UNCOVERED | out of model | -- |

Notes.

- The A6 pins are ABSORBED OUTRIGHT by PLAIN and LAGGARD at 16 seeds
  in both directions, and the record keeps that as a finding rather
  than smoothing it: the serve/adopt heal restores all-n possession
  before the window rolls off the round being served, so MET carries
  every advance and the tolerance escape is never the only route
  (seam 774-789).  The pins are SHARP only on the mute arm, where
  all-n is unreachable: pinned shut the correct cohort wedges at
  frontier 1 and abandons (~950 ticks, asserted positively), honest
  ~10000 ticks at one T_p per rung, pinned open ~6000 with
  `tolUnearned > 0` asserting the pin was consumed (seam 791-813).
  Those three numbers ARE L1's tolerance half measured.
- The corollary is measured by the tick contrast between the two
  Byzantine arms: WITHHOLD ~57 ticks against SILENT ~10000, one T_p
  per rung (seam 596-599).
- A5's withdrawal is STRONGER THAN BRIEFED: 80 stall dumps, PLAIN
  stalling at MAXTICKS at about half the seeds, every other run
  ending in an accepted strand -- and the safety arms silent at every
  seed.  A5 is load-bearing for LIVENESS ALONE (seam 820-839).
- W_A9_SYBIL's L1/R4 relevance is D's ground-truth arm, 5 firings: a
  mis-attributed possession bit lets an advance outrun the processes
  that really closed the prior round (seam 865-868).
- W_SERVE_CAP0's 211 decompose as B 16+16+3, C 128, P 16, F
  structural 16, hold-overflow 16, with D both halves, E, F's unsafe
  arm and H silent at every seed (seam 953-979).  Withdrawing the
  floor costs the heal and costs nothing else.
- The rotation ABSORPTION and its reason: O1's linkage bounds a
  flooding solicitor to ONE duty at a server (it proves possession of
  every round but its highest and erases its own want bits), measured
  as solicitor max 1, cohort max 6, cap 2; and a merely-displaced
  correct wanting process still completes on its own account under
  BPR.  Falsifying the clause needs t liars filling the cap AND one
  correct wanting process that cannot complete on its own, which
  costs t+1 faults and is out of model at every t (seam 995-1031).
- W_REACH_WSHRINK is a SIZING report, not an L1 red: with w = 1 and
  the window made to BIND by a withholder at n=7 t=2, the heal still
  completes inside the single rung the round is retained for -- 14
  evictions, zero stalls or strands (seam 1077-1117).  Read with the
  A6 pins it is the same fact from the other side.

## 3. L2 ADOPTION AGREEMENT (md 1115-1226)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| A10 fold binding | M_INJ_CANDIDATE | RED | 227, seam 122 | glue |
| I5 witness ground | MM_I5_* + inv I5 | RED | paths 82, 81 82 | mut |
| A3 server asserts | BYZ_MIXED/EQUIVOC | GREEN | seam 601-608 | cfg |
| C6 byte-match | W_L2_NOBYTEMATCH | RED | 4, seam 1119 | prem |
| C6 re-arm | W_L2_NOREARM | RED | 2, seam 1155 | prem |
| C6 void ADOPT | M_SEAM_NOVOID | RED | 10, seam 658 | glue |
| identity half | M_SEAM_UNBOUND | RED | 391, seam 319 | glue |
| A9 for L2 | none reachable | UNCOVERED | seam 870-884 | -- |
| A2 at t=0 | _t0 config, ctr J | GREEN | 2156/0, ctr J | cfg |

Notes.

- The three C6 clauses have three SEPARATE countermodels and the
  records keep them apart: NOBYTEMATCH latches one book from a mixed
  set (4 failures at BYZ-MIXED seeds 6 and 16 -- seed-dependent and
  STRUCTURALLY so, since the fabrication reaches a witness path only
  at an even-indexed wanting process); NOREARM keeps stale witnesses
  across a candidate switch and latches a new candidate prematurely
  (2 failures, BYZ-MIXED seed 5); NOVOID keeps a stale adopt debt
  alive across the switch (10 failures at seeds 1, 4, 12, 14, 16).
  NOVOID does NOT fire at 0% loss and CANNOT fire on an honest
  schedule -- honest servers all serve one composition -- so it is
  unreachable by every pre-Byzantine instrument in this repo by
  construction (seam 658-671, 1119-1173).
- M_SEAM_UNBOUND's 391 is the tranche-2 re-run total at 16 seeds; the
  stage-1 record notes the E firings themselves are seed-dependent (3
  in 32 runs, none at 2 seeds) (seam 319-322, 1244).
- A9's L2 half is UNPROBED AT t = 1 and the reason is structural, not
  a gap in the arm: falsifying L2 through attribution needs a false
  server NAME and a fabricated BYTE at once -- a Byzantine server
  whose serving is then re-attributed -- which is two faults, and one
  leg per (round, server, wanting process) means a fabricated byte can
  never accrue two distinct server names at one process.  Reachable
  only at t >= 2 (seam 870-884, 906-911).
- The t = 0 degeneration (one witness IS t+1) is exercised at the
  smallest deployment the spec admits: the n=2 t=0 config point
  (2156/0) and contract section J's three t=0 commutation scenarios
  over pre-state 2 (seam 680-698; ctr J, pre-state 2).

## 4. L3 SUPERSESSION (md 1227-1264)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| commutation, both orders | ctr J, 10 scenarios | RED | MM_I6 J x10 | mut |
| full-window pre-state | ctr J pre-state 1 | RED | MM_I11 J x1 | mut |
| once-per-round | ctr J second close | UNCOVERED | no mutant | chk |
| wrap-crossing pre-state | none | UNCOVERED | J bases 0/4/0 | -- |
| reset voids ADOPT (caller) | M_SEAM_NOVOID | RED | 10, seam 658 | glue |
| close voids ADOPT (caller) | W_L2_NOCLOSEVOID | RED | 53, seam 1559 | prem |

Notes.

- Section J replays both orders from a byte-identical snapshot of the
  WHOLE caller allocation and compares with memcmp, so no conjunct of
  "identical" is chosen by the test; the close's own acts must match
  kind-for-kind and round-for-round, and order A's ADOPT acts are
  encoded per scenario rather than ignored (ctr 56-71).
- MM_I6_BOOK_SURVIVES fires ALL TEN commutation scenarios -- the
  surviving book is exactly the state difference the two orders must
  not have (inv 110-114; mm 31-35).  MM_I9_SLOT_NOFREE fires J x2 and
  MM_I11_EVICT_NEWEST J x1 (the full-window non-vacuity arm), so the
  pre-state that makes the close output a RELEASE is non-vacuous.
- The once-per-round corollary is checked in both of the proof's arms
  (before a later launch the second close finds no live instance;
  after one it names a round that is no longer the frontier), but no
  mutant in the tier designates those checks, so they are green
  witnesses and not matched reds.
- J's three pre-states are frontier 0, frontier 4 over a full window,
  and a t=0 frontier 0 (ctr, section J).  NONE crosses the round-byte
  wrap: the wrap is covered for the CLOSE alone in section F, without
  a witness book, so the commutation claim across a wrap-crossing
  pre-state is not covered anywhere.

## 5. L4 PRESENTATION (md 1265-1301)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| launch answers exclusive | md 760-763, ctr C | UNCOVERED | no mutant | chk |
| MAINTAIN outranks ADMIT | ctr H | UNCOVERED | no mutant | chk |
| one launch act per opp. | ctr C | UNCOVERED | no mutant | chk |
| value rides a JOIN | ctr C join arms | UNCOVERED | no mutant | chk |
| caller: staging | none | UNCOVERED | seam 3766 | -- |
| caller: byte-identical | none | UNCOVERED | -- | -- |
| caller: retire on subset | none | UNCOVERED | -- | -- |
| honoring the answer | M_SEAM_FREE | RED | seam 313-318 | glue |

Notes.

- The machine half rests on the tables' compile-time exclusivity and
  exhaustiveness (the dtc discipline, md 760-763) and is exercised by
  contract sections C and H -- the full precedence chain in one call
  ("owed work before maintenance before chosen work"), MAINTAIN
  outranking a pending value without consuming it, and "live: no
  second launch".  Those are GREEN WITNESSES: no mutant in the
  machine-mutant tier targets the launch rules, so the machine half
  carries no matched red.
- The CALLER half is not modeled anywhere.  The composed seam passes
  `valuePending` as a constant of the round index and `maintenanceDue`
  as 0 (seam source, the launch site), stages no application value,
  and keeps no exactly-once ledger -- so PRESENT's staging, its
  byte-identical re-presentation, and its retire-on-witnessing are
  untested, and R1 (the conjunction of the two halves) is untested end
  to end.  The row is UNCOVERED, not stretched.
- M_SEAM_FREE is a caller red for a different claim -- the glue
  launching on its own account, ignoring the machine's answer -- and
  fires D's machine-consistency arm at every seed while correctly NOT
  tripping D's ground-truth arm (seam 313-318).

## 6. L5 RELEASE SAFETY (md 1303-1368)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| I8 release at all-n | MM_I8_EARLY | RED | ctr C x1 D x1 | mut |
| A9 attribution | W_A9_SYBIL | RED | 464, seam 841 | prem |
| A8 truthful evidence | BYZ_FORGE_POSSESS | GREEN | seam 588-593 | cfg |
| I2 nothing owed | ctr D, inv I2 | UNCOVERED | seam 4649 | chk |
| R2b resume | none | UNCOVERED | no arm | -- |
| O2 content carve-out | seam I, BYZ_CONTENT | RED | M_EXCH_* | glue |
| eviction exception | seam F structural | GREEN | 3 instances | prem |
| A2 at most t | none built | UNCOVERED | out of model | -- |

Notes.

- W_A9_SYBIL's 464 failures ARE L5's red: F's `releaseUnsafe` arm --
  an all-n release for a round a correct process had not closed --
  16/16 seeds at PLAIN, 16/16 at BYZ-SILENT, 16/16 at BYZ-FORGE,
  15/16 at LAGGARD.  BYZ-FORGE's own "L5 strict arm silent under a
  forging sender" arm falls 16/16 with it, which is the sharpest
  statement of the finding: that arm is green at every seed of the
  stage-1 sweep and the only thing changed is the attribution
  (seam 850-868; Fable-verified inventory at seam 900-920).
- A8's containment control is BYZ_FORGE_POSSESS RUNNING GREEN at
  every seed and every config: a forged bit is the liar's own to
  give, so a release may fire one bit ahead of the truth but never
  ahead of the correct cohort.  The register's own note records that
  A8's withdrawal must NOT red and that A9 carries the weight
  (seam 1221-1224).
- The I2 direction is deliberately NOT asserted at the seam -- the
  machine maintains it by construction, so asserting it there is
  unfalsifiable; it belongs to the contract suite (seam 4649-4652).
  It carries no matched red for the same reason I2 has none above.
- R2b (resume, never re-execute) binds ACS instance state the caller
  owns.  No instrument interrupts and resumes an instance, so nothing
  tests it.
- The content carve-out is exercised by the exchange plane: check I
  (content completeness plus the machine-told arm) with three matched
  reds -- M_EXCH_MISCLASS, M_EXCH_EARLYRETIRE and M_EXCH_NOASSEMBLE
  (the last recorded DORMANT on 2026-07-24 and RESTORED by the
  Byzantine arm, firing at BYZ-MIXED seed 16) -- and by
  BYZ_WRONG_CONTENT, whose mis-tagged sidecars are dropped as loss
  (seam 265-274, 356-367, 617-619, 707-716).
- The eviction/wrap exception is out-of-band by definition and is
  exercised three times over: BYZ_SILENT, W_A4_PARTITION's victim,
  and W_SERVE_CAP0's starved cohort each make all-n unreachable so
  that eviction is the only release path left (seam 610-616, 760-764,
  967-970).

## 7. L6 SEQUENCE IDENTITY (md 1370-1430)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| L2 downstream | seam E everywhere | RED | see L2 rows | cfg |
| I10 caller half | W_I10_WRONGARTIFACT | RED | 16, seam 1175 | prem |
| A1 ACS agreement | BYZ_EQUIVOCATE_VALUE | GREEN | seam 606-608 | cfg |
| A7 close speaks round | M_SEAM_STALE | RED | seam 323, ctr C | glue |
| A11 common base | none | UNCOVERED | seam Genesis | -- |
| L3 one consume region | see L3 table | RED | MM_I6 J x10 | mut |

Notes.

- E is the oracle everywhere: every correct process's composition for
  a round must be byte-identical, and it is what W_L2_NOBYTEMATCH,
  W_L2_NOREARM, M_SEAM_NOVOID, M_SEAM_UNBOUND and
  W_I10_WRONGARTIFACT all bring down.  E became FALSIFIABLE only
  after the adopt path was corrected -- while the glue closed an
  adoption without JOINing first, every wrong-composition close hit
  the machine's inert path and E read green for the wrong reason
  (seam 424-429).
- W_I10_WRONGARTIFACT is the purest caller red in the matrix: 16
  failures at 16 seeds (E plus the fabrication arm at BYZ-MIXED seeds
  1, 3, 4, 6, 7, 12, 14, 16) with EVERY MACHINE CONJUNCT CLEAN at
  every seed -- the machine cannot catch it by construction, since it
  never sees a composition.  Its reachability was measured, not
  assumed: a coverage counter asserts the state where a standing
  candidate differs was reached (seam 1175-1203).
- A11 is supplied BY CONSTRUCTION: the seam folds every first round
  over one shared `Genesis` constant, and no arm withdraws it or
  seeds divergent bases.  The induction base of L6 is therefore
  untested.

## 8. L7 WRAP AND TWO-GRAIN SOUNDNESS (md 1432-1465)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| first half (I1) | MM_WRAP_LOOKAHEAD_SKIP | HAND | ctr F x5 | mut |
| second half (H_r) | -DHRTWIN + MM_HR_GATES | RED | path 45 56 2 99 | mut |

Notes.

- The first half is I1 read as a corollary, so it inherits I1's
  coverage exactly: hand proof plus contract section F (inv 36-40,
  ctr 87-92).
- The twin drive was an UNFALSIFIED WITNESS until the machine-mutant
  tier landed; MM_HR_GATES gates the SERVE walk on the held-members
  grain -- H_r entering a decision -- and the twins diverge on the
  serve cursor at BFS path "45 56 2 99".  The PLAIN falsifier is
  BLIND to the same mutation by construction (H_r is outside its
  alphabet) and runs clean at 310,579 states, which is precisely why
  the twin arm exists.  The pairing that gives the red its meaning
  ran in the same session: the unmutated machine under the same twin
  drive, 621,094 states / 43,711,360 transitions per run, 29,742,272
  differing-grain observations, ZERO divergence (inv 77-90; mm 61-71).

## 9. The schedule, loss and shape quantifiers

| quantifier | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| all schedules (FIFO) | SCHED_FIFO | GREEN | 42677/0 | sch |
| all schedules (LIFO) | SCHED_LIFO | GREEN | 37557/0 | sch |
| all schedules (delay) | SCHED_STARVE1 | GREEN | 43193/0 | sch |
| all schedules (kind) | SCHED_KINDFLIP | GREEN | 42523/0 | sch |
| deep bound, 1 round | SCHED_ENUM ROUNDS=1 | GREEN | 29487680 leaves | enum |
| deep bound, 2 rounds | SCHED_ENUM ROUNDS=2 | GREEN | 1593008 leaves | enum |
| loss 0-15% | loss sweep | GREEN | 42277/0 at 15% | loss |
| loss 20% | loss sweep edge | SIZING | 42245/7 | loss |
| config n=4 t=1 w=3 | seam default | GREEN | 42781/0 | cfg |
| config n=2 t=0 | seam _t0 | GREEN | 2156/0 | cfg |
| config n=7 t=2 | seam _big | GREEN | 80249/0 | cfg |

Notes.

- Each `-DSCHED_*` build replaces the POP CHOICE and nothing else, and
  every policy draws exactly one RNG step per successful pop at the
  uniform policy's position, so the loss coin sequence a seed names is
  policy-invariant (seam 1285-1300).  Zero failures under every policy
  at every seed, zero stalls, every classification an accepted strand;
  the safety arms never move.
- Two schedule measurements are FINDINGS and neither is a red.  LIFO
  spends the fault budget with the SCHEDULE: the agreed subsets shrink
  to 3.156 members of 4 against 3.692 elsewhere -- the tolerance R4
  reserves consumed by ORDERING rather than by fault -- and the heal
  pays for it (2584 adoptions against 521).  KINDFLIP produces one
  LAGGARD accepted strand the uniform policy never reaches, because
  the possession indication rides the ACS tails that carrier-priority
  inversion defers (seam 1315-1360).
- The enumeration's honest scope, in its own terms: the full tree is
  on the order of 20^400, so every honest form of the mode is a
  BOUNDED-DEPTH one -- what is exhaustive is the PREFIX (depth 6 at
  ROUNDS=1, depth 5 at ROUNDS=2) and the tail is one sample.  Both
  leaf counts are asserted in-program.  It does NOT reach the
  Byzantine arms (t=0 has no budget), the laggard (out of model at
  n-t = n, verified), or any divergence beginning after the enumerated
  prefix.  It is a DEEP BOUND at a TINY shape complementing the seeded
  sweeps' shallow bound at three shapes; NEITHER SUBSUMES THE OTHER
  (seam 1438-1510).
- The loss envelope: safety is green at every swept level and liveness
  degrades by CLASSIFICATION alone, monotone in the loss.  The first
  accepted strand outside the STARVE positive control appears at 12%;
  the highest fully-green level is 15%.  At 20% the 7 failures are
  posture x4 and C x3 at STARVE seeds 9 and 13, with D both halves, E,
  F's unsafe arm and H silent -- an S-against-loss-rate SIZING
  boundary, not a safety one (seam 1372-1418).
- The n=2 t=0 point carries two arms of its own: TOLERANCE is
  asserted NEVER READ (and never is), and the serve floor of ONE is
  the entire heal capacity, exercised by ordinary loss (7-13 serves a
  run, adoption at 9 of 16 seeds).  LAGGARD is OUT OF MODEL there,
  verified rather than assumed (seam 680-698).

## 10. Caller obligations C1-C11

C1-C6 are the Mechanization-status caller list in the order it states
them (md 1484-1541); the composed seam cites C1 and C6 by those
numbers.  C7-C11 were caller halves the spec consumed but did not
enumerate; on 2026-07-25 the architect landed C7 (I10's caller half,
md 1520-1528) and C11 (the possession-evidence return leg plus the
O1-inference translation, md 1529-1541) in that list, appended after
C6 so the cited order of C1-C6 is unmoved.  C8-C10 remain reference
labels for obligations stated elsewhere in the spec (A9's second
clause, A6's honest gates, the sequential-entry premise).  The same
landing added a COMPLETION analog to C6's void clause (md
1506-1513): a close consuming the round voids any unconsumed ADOPT
for it.  Its matched red landed the same day (architect-ordered):
W_L2_NOCLOSEVOID, 53 failures at 16 seeds with 32 unvoided debts
counted -- and 9 of its 25 E firings occur at PLAIN/LAGGARD with
ZERO faults present, making it the one C6 clause that is NOT
Byzantine-conditional: the stale candidate is the right bytes for
the WRONG round, so an honest schedule reaches the red.  Its
mute-liar face also wedges the correct cohort outright (the
BYZ-SILENT block), so the clause is load-bearing for liveness
beside L6.

| obligation | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| C1 serve cap floor | W_SERVE_CAP0 | RED | 211, seam 945 | prem |
| C1 serve rotation | W_SERVE_ROTDROP | ABSORBED | 14049/0 | prem |
| C2 byte budget | none | UNCOVERED | no evict call | -- |
| C3 PRESENT staging | none | UNCOVERED | seam 3766 | -- |
| C4 have-grain currency | M_EXCH_NOASSEMBLE | RED | seam I arm | glue |
| C5 R2b resume | none | UNCOVERED | no arm | -- |
| C6 byte-identical count | W_L2_NOBYTEMATCH | RED | 4, seam 1119 | prem |
| C6 re-arm on switch | W_L2_NOREARM | RED | 2, seam 1155 | prem |
| C6 reset voids ADOPT | M_SEAM_NOVOID | RED | 10, seam 658 | glue |
| C6 close voids ADOPT | W_L2_NOCLOSEVOID | RED | 53, seam 1559 | prem |
| C6 identity of round | M_SEAM_UNBOUND | RED | 391, seam 319 | glue |
| C6 fold ground | M_INJ_CANDIDATE | RED | 227, seam 122 | glue |
| C7 I10 caller half | W_I10_WRONGARTIFACT | RED | 16, seam 1175 | prem |
| C8 A9 sender argument | W_A9_SYBIL | RED | 464, seam 841 | prem |
| C9 A6 honest gates | W_A6_PIN0 / PIN1 | GREEN | 12643, 16095 | prem |
| C10 sequential entry | none | UNCOVERED | out of model | -- |
| C11 hold, re-present | M_SEAM_NOPEND | RED | 11, seam 301 | glue |

Notes.

- C2's machine side (systemEvict takes the oldest retained round) IS
  covered -- contract section E and MM_I11_EVICT_NEWEST -- but the
  CALLER half, the cross-kind byte budget that decides WHEN to evict,
  is not: the composed seam never calls `systemEvict` at all, so no
  instrument drives an eviction from a budget decision.
- C4's red is the machine arm of check I: ground truth still holds the
  content, but the skipped late-assembly ingress leaves the machine's
  have grain stale for content delivered while the round was retained
  -- a delivered-but-untold mismatch (seam 362-367).  Seed-dependent
  and mode-bound: DORMANT from 2026-07-24 until the Byzantine arm
  restored its window, then 1 failure at BYZ-MIXED seed 16.
- C11's red is SEED-DEPENDENT (11 failures at LAGGARD seed 5; 0 at 4
  seeds, fires by 16) and cannot be caught by the STARVE scenario at
  all, because there the indications never arrive.  Its partner is
  contract section I, which pins that the record a held-then-
  re-presented indication reaches is byte-identical to the record a
  timely one reaches, with a non-vacuity arm (ctr 52-55, seam 301-311).

## Permanently uncoverable

Named here with the reason, because a register that only listed what
fired would be a coverage report and not an honest one.

- THE I1 FRONTIER+1 LOOKAHEAD.  The guard needs a retained entry 255
  rounds behind the completing frontier; the falsifier's horizon
  bounds an entry to HORIZON-1 behind, so distance 255 needs HORIZON
  >= 256 and no feasible search reaches it.  MM_WRAP_LOOKAHEAD_SKIP
  is invisible to the enumeration -- three configurations reproduce
  the clean machine's counts exactly -- not merely unfired.  Carried
  by the hand discharge of I1 plus contract section F, five checks in
  both wrap regimes, and by nothing else (inv 36-40, 129-148; ctr
  87-92; mm 53-60).
- MORE THAN t BYZANTINE.  Out of model.  No arm exists, and none was
  built even as an observation arm, so this register records it as
  UNCOVERED rather than claiming an observation.
- SEQUENTIAL ENTRY (C10).  Out of model: the mechanization is
  sequential -- ten entry points, one at a time, no interleaving
  inside the machine (md 605-607) -- and every instrument calls the
  entry points one at a time, so the instrument's own construction is
  the discharge and cannot also be its test.
- THE L3 WRAP-CROSSING PRE-STATE.  Section J's three pre-states are
  frontier 0, frontier 4 over a full window, and t=0 frontier 0.  The
  commutation claim is therefore never replayed across the round-byte
  wrap; section F covers the wrap for the CLOSE alone, without a
  witness book.
- A9-FOR-L2 AT t = 1.  Falsifying L2 through attribution needs a
  false server name and a fabricated byte at once, which is two
  faults; one leg per (round, server, wanting process) means a
  fabricated byte can never accrue two distinct server names at one
  process.  Reachable only at t >= 2 (seam 870-884, 906-911).
- THE SERVE ROTATION'S NON-FAULT FRAME.  The clause's load-bearing
  frame is the serve floor's own two NON-FAULT grounds -- the
  abandonment posture's stale-cursor re-offer, and an EXHAUSTED
  instance -- neither of which is a fault and neither of which this
  instrument can manufacture.  Inside the fault budget an unhealable
  correct wanting process costs a fault, so the countermodel needs
  t+1 and is out of model at every t (seam 1011-1031, 1256-1262).
- THE HOLD-UNVERIFIED RULE -- DEMOTED 2026-07-26.  The Model no
  longer states a hold: ahead-of-reach traffic is not evidence and
  re-arrives on its sender's retry cadence; a caller MAY hold it as
  a latency optimization, and no obligation reads the hold
  (system.dtc act-received text; system.md Model EVIDENCE).  There
  is no Model half left to cover: M_SEAM_NOHOLD's teeth are the
  STARVE scenario's discard counters (16 failures at 4 seeds), and
  that is now the whole story rather than a residue (seam 289-300;
  the seam header's "Model-load-bearing" remark predates the
  demotion and stands as a dated record).
- L4's CALLER HALF AND R1.  Not permanently uncoverable in principle,
  but uncovered today by every instrument in the tree: no harness
  stages an accepted value, re-presents it byte-identically, or
  retires it on witnessing it in an agreed subset, so exactly-once
  presentation -- the conjunction L4 names -- has no oracle here.

## Spec observations queued to the architect

Recorded in the instrument headers, carried here as pointers only.

LANDED 2026-07-25 (architect): F1 (the return leg and inference
translation, now C11 in the caller list, md 1529-1541), F2 (the
completion analog of the void clause, md 1506-1513 -- no arm yet,
see the C6 rows), F3 (causal well-foundedness, now a Model
paragraph the L2 proof cites), and F6 (I10's caller half, now C7 in
the caller list, md 1520-1528).  The pointers below remain open.

- The SERVE rotation clause's load-bearing frame is the floor's two
  non-fault grounds, which no instrument inside the fault budget can
  reach (seam 1256-1262).
- REACH's binding quantity is heal-time against rung-time, not w
  alone (seam 1264-1266).
- S against the loss rate is a budget coupling the spec does not
  price -- the w x T_p family's third face (seam 1543-1546).
- R4's reserved t is consumed by adversarial ORDERING as well as by
  faults (seam 1543-1546).

## Discrepancy found while assembling this register

`test/machineMutants.sh` recorded MM_I8_EARLY's expected contract-suite
oracle as "test_system E/G", while `test_system_invariant.c` and
`test_system.c` both record the MEASURED sections as C and D.  The two
are disjoint, so one record was stale.  RESOLVED at verification: the
script's line was the build brief's PREDICTION, never a measurement --
the tier's own summary artifact reads "sections: C x1; D x1" -- so the
script header now carries the measured pair with the prediction noted.
This register carries the measured pair (C x1, D x1).
