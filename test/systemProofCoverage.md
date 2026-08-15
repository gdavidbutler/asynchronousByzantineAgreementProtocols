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
  RED (tw)   TRIPWIRE RED (added 2026-08-14 by the line-by-line read of
             the arms, section 11b).  The oracle fires, but it is a
             counter that moves only inside the block the mutation
             edits -- so a deployment with the same defect and no
             self-report passes it.  Worth having and worth not
             mistaking for the arm that would catch a silent defect.
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

  md N     `system.md`, line N.  RE-CITED 2026-08-13 against the
           spec as it then stood, every range re-derived from the
           section it names rather than adjusted by an offset.
           Line citations rot at every landing that moves text
           above them, so re-citing is part of landing a spec
           change, not a later cleanup: the ranges here name
           sections, and a session that finds one off should
           re-derive it from the section title and fix it in
           place
  ctr X    `test/test_system.c`, section X (A-M); ctr N = line N
  str X    `test/test_systemStore.c`, section X (A-J)
  inv N    `test/test_system_invariant.c`, line N (header runs to 176)
  seam N   `test/test_system_seam.c`, line N (header runs to 1885)
  mm N     `test/machineMutants.sh`, line N (header runs to 88)
  sm N     `test/storeMutants.sh`, line N (inventory at 47-140)
  gm N     `test/seamMutants.sh`, line N (the matrix is its table at
           the foot, one mutant per line with the check it must fire)
  r1m N    `test/r1Mutants.sh`, line N (inventory at 33-79)

Reproduction tokens:

  chk   `make check`            (runs the contract suite)
  inv   `make test_system_invariant` / `_hrtwin`
  mut   `make machine-mutants`
  smut  `make store-mutants`     (the retention-store tier)
  cfg   `make seam-configs`
  prem  `make seam-premises`
  sch   `make seam-sched`
  loss  `make seam-loss`
  glue  `make seam-mutants` for the whole 22-mutant matrix, or
        `make test_system_seam CPPFLAGS=-D<mutant>` for one arm
  enum  `make seam-enum`
  r1    `make r1-mutants`       (the R1 caller-half tier)

## 1. The state invariant I1-I11 (md 1034-1152)

| conjunct | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| I1 distinctness | ctr F deep retention | HAND | ctr F | chk |
| I2 disjoint | MM_CURSOR_UNGATED | RED | I2 + ctr K | mut |
| I3 self possesses | SM_C19_PREPOP_UNOWNED | RED | I3 x8 | smut |
| I4 short of n | SM_C18_NO_DROP | RED | I4 x8, ctr E x9 F x11 | smut |
| I5 latch | MM_I5_LATCH_AT_T | RED | path 82 | mut |
| I5 no self | MM_I5_COUNT_SELF | RED | path 81 82 | mut |
| I6 clear | MM_I6_BOOK_SURVIVES | RED | ctr H x4 J x10 | mut |
| I7 F not held | SM_C19_PREPOP_FRONTIER | RED | I7 x8; SM_C20_MINT_EQUAL x9 | smut |
| I8 at all-n | MM_I8_EARLY | RED | ctr C x1 D x1 | mut |
| I9 announced | MM_I9_SLOT_NOFREE | RED | ctr E F J | mut |
| I10 birth byte | MM_I10_RETAIN_WRONG | RED | ctr B C D | mut |
| I11 oldest | MM_I11_EVICT_NEWEST | RED | ctr E x3 J x1 | mut |

Notes.

- I1's COUNT arm is not a proof obligation at all.  RE-POINTED
  2026-08-13 (the retention seam): it was the ALLOCATION's while the
  machine owned the reach; the fixed array is gone, so the bound is the
  caller store's, reported by a refused `retain` and answered by the
  release rule's eviction arm.  It rests on C13 (section 10b) --
  as do I9, I10a and I11, which constrained ENTRY POINTS and now
  bound only a machine that is no longer the sole party able to
  change the retained set.  RE-POINTED 2026-08-10 (the round-name
  widening -- the
  byte dissolution): distinctness and strictly-behind now rest on the
  BIRTH DISCIPLINE alone (the one retaining entry point names the
  monotone pre-advance frontier), not on any structural release --
  the frontier+1 wrap lookahead was DELETED with the wrapping byte
  name, and MM_WRAP_LOOKAHEAD_SKIP retired with its target (mm header
  inventory carries the dormancy record as a dated closure).
  Coverage is the induction's hand discharge plus contract section F,
  now the deep-retention dual: no release before the reach fills, an
  entry 256 behind is position-exact beside a frontier whose
  byte-world name would have collided, a lingerer survives at depth
  300 and still serves, and releases come only from all-n or
  eviction.
- I2 gained its FIRST matched red 2026-08-10 with the cursor birth:
  MM_CURSOR_UNGATED drops the walk's possession gate, so cursor want
  is born exactly where possession is set (self included, by I3) and
  the falsifier's I2 arm fires, beside contract section K.
- R4'S NAMING DUTY HAS NO INVARIANT AND NO ARM (2026-08-12).  R4's
  absence dimension requires an eviction to name the processes it
  leaves unserved; the machine does not, so nothing here covers it
  and nothing should pretend to.  Every RELEASE act sets .want to 0
  (system.c 410, 587, 688, 762), so a caller reads WHICH round left
  and never WHOM; I9 constrains only that a departure be announced
  BY ROUND, and adding a conjunct today would assert of the machine
  something false of it.  The gap is spec-side and admitted in
  Mechanization status, not a defect of these instruments -- when
  the eviction arm carries the set, the conjunct and its matched
  red become owed together.  Note WHICH set, since the near
  reading is wrong and would build the wrong red: it is the
  COMPLEMENT of the possession record, not the still-owed record,
  which is a strict subset missing exactly the returner that has
  not yet reached the round.  No stored bitmap holds the
  complement, so the eventual arm tests a value derived at the
  act site, not a field copied from the entry.
  UPDATED 2026-08-13 (the retention seam): the row stands -- the act
  still carries nothing -- but its stated BLOCKER is gone.  The
  reason recorded above ("no caller can query an entry that no
  longer exists") was an artifact of the machine owning the storage.
  The machine now drops a round by calling the CALLER's own
  `release`, which runs while that round's records still exist, so
  the complement is derivable at the withdrawal itself.  The duty is
  dischargeable where it is incurred; whether the ACT should also
  carry the set is the open question, and only that question is
  still spec-side.
- I3, I4 AND I7 GOT THEIR FIRST MATCHED REDS 2026-08-14 from the
  STORE-MUTANT TIER (`test/storeMutants.sh`), and the reason it took a
  store to fire them is worth keeping.  Each forbids a state no
  correct MACHINE can reach by any sequence of its own calls -- a
  retained round self does not possess, one every process possesses
  that is still held, a retained frontier -- so mutating the machine
  reaches them only through some other conjunct first.  The retained
  set is the CALLER's now, and a store that is not empty at init hands
  the machine exactly those states before it has taken a step: I3 from
  a round nobody's record was written for (SM_C19_PREPOP_UNOWNED), I7
  from the genesis name being held (SM_C19_PREPOP_FRONTIER), I4 from a
  round that survives its own release (SM_C18_NO_DROP).  Contract
  section D still exercises the I2 DIRECTION (a riding indication
  records possession and retires the want) as a green witness, not as
  a red.
- WHICH CONJUNCT FIRES IS NOT WHICH CONJUNCT IS VIOLATED, and two of
  the tier's arms measure it: the falsifier keeps the FIRST bad it
  finds per state, so SM_C15_STORE_WRITES (want set equal to possess
  at every round) reports I10's birth clause though I2 is violated in
  the same states, and SM_C19_PREPOP_ALLN reports I8 though I4 was the
  conjunct the design named.  Rows above cite the arm that actually
  fires them; the tier's header carries both readings per arm.
- The tier's FAIL counts are 9 for every mutant BY CONSTRUCTION (eight
  violations stop run 0, the ninth stops run 1), so what a row records
  is WHICH conjunct fired and the first-violation action path, never a
  count (inv 99-104).
- RE-RUN END TO END 2026-08-13 after the retention-seam re-point:
  eight kills, `system.c` checksum identical before and after, and the
  CLEAN -DHRTWIN pairing PASSING at the new freeze (187,550 /
  20,891,344 both runs, 35,949,976 differing-grain observations, zero
  divergence).  MM_I9_SLOT_NOFREE is now MM_I9_DROP_SILENT -- no slot
  exists to leave unfreed, and a departure nothing announced is what
  the conjunct was always about.
- The unmutated frozen enumeration: 187,550 states / 20,891,344
  transitions per run, asserted in-program with `-DEXPECTSTATES`
  (re-frozen 2026-08-13 with the RETENTION SEAM, from 621,286 /
  59,371,888).  The set SHRANK, and the derivation is in the
  falsifier's header rather than here: a retained round used to live
  in one of ER FIXED SLOTS and the slot index was in the key, so one
  retained set appeared once per assignment; and a freed slot kept
  its round name and bitmaps, so a state carried the ghost of what it
  had released.  Neither exists under the store.  Nothing observable
  was lost -- the canonical descriptor was already blind to both, and
  the two runs' state sets still match exactly.  Earlier freezes:
  621,286 at the round abstraction (the growth from 621,094 was the
  machine's morgue, whose in-use byte the translation-invariance arm
  forced), 621,094 / 59.4M at the cursor alphabet, 621,094 /
  43,711,360 before it.  Line-number citations into inv/ctr predate
  the abstraction sweep and may be offset; section labels are stable).

## 2. L1 BOUNDED HOLD (md 1369-1594)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| A6 gates, pinned shut | W_A6_PIN0 | GREEN | 12667/0, seam 766 | prem |
| A6 gates, pinned open | W_A6_PIN1 | GREEN | 16094/0, seam 770 | prem |
| T_p corollary | BYZ_WITHHOLD/SILENT | GREEN | seam 596-599 | cfg |
| A4 delivery | W_A4_PARTITION | GREEN | 4048/0, seam 746 | prem |
| A5 O1 inference | W_A5_NOINFER | GREEN | 641/0, seam 815 | prem |
| A9 for R4's floor | W_A9_SYBIL | RED | D-arm x5, s841 | prem |
| SERVE floor | W_SERVE_CAP0 | RED | 212, seam 947 | prem |
| SERVE rotation | W_SERVE_ROTDROP | ABSORBED | 14049/0, s983 | prem |
| rotation control | W_SERVE_ROTOK | GREEN | 14049/0, s1035 | prem |
| discharge order | W_SERVE_YIELD | GREEN | 72758/0 | prem |
| self-funding relief | W_SERVE_WIRE | GREEN | 80463/0 | prem |
| deferred not retired | W_SERVE_NORESUME | RED | 80 at 16 | prem |
| REACH proviso | W_REACH_WSHRINK | SIZING | 14073/0, s1225 | prem |
| A12 permanence | -- | RETIRED 2026-08-14 | see the A12 note | -- |
| M1 retirement | M_LEG_LOCALRETIRE | RED | seam 253, 334 | glue |
| R2c continuation | W_R2C_SILENT | GREEN | 12608/0, s1192 | prem |
| A2, more than t | none built | UNCOVERED | out of model | -- |

Notes.

- The A6 pins are ABSORBED OUTRIGHT by PLAIN and LAGGARD at 16 seeds
  in both directions, and the record keeps that as a finding rather
  than smoothing it: the serve/adopt heal restores all-n possession
  before the reach rolls off the round being served, so MET carries
  every advance and the tolerance escape is never the only route
  (seam 774-789).  The pins are SHARP only on the mute arm, where
  all-n is unreachable: pinned shut the correct cohort wedges at
  frontier 1 and abandons (~950 ticks, asserted positively), honest
  ~10000 ticks at one T_p per rung, pinned open ~6000 with
  `tolUnearned > 0` asserting the pin was consumed (seam 791-813).
  Those three numbers ARE L1's tolerance half measured.

- W_SERVE_YIELD builds the order SERVE states -- the sequence first,
  without remainder -- and is GREEN at 72758/0 with serves ZERO at
  every seed and scenario: the returner is starved for the whole run,
  the sequence completes without it, and that IS the arm passing.
  It reported 417 failures first, and all of them were the INSTRUMENT
  disagreeing with the spec.  THE GROUND-TRUTH ARM KNEW ONE LEGITIMATE
  DEFICIT AND NOW KNOWS TWO: the EVIDENTIAL one (the process holds the
  round its duty is held on, only the evidence is missing -- it reads
  the duty class because R4 is what strands it) and the CAPACITY one
  (the serves it was owed were withheld by the order).  The second
  takes NEITHER conjunct of the first: a starved returner sits at duty
  MET, not HELD, because nothing about R4 blocks it -- it cannot
  COMPLETE a round the cohort has left behind, and the adoption that
  would close it is what the order withheld.  Requiring HELD there was
  the first cut and it accepted nothing.
  THE VETO IS UNCHANGED and that is what keeps this from being a
  weakening: arrived-but-unbanked still REJECTS under either deficit,
  so M_SEAM_NOPEND keeps its red (11, re-verified), and the ledger is
  all-zero in every build compiling no order, so the three config
  baselines and all sixteen other premise arms re-run byte-identical.
  Two arms INVERTED rather than lapsing -- B's serves-born arm asserts
  serveMsgs == 0 where the order granted nothing all run, and the
  BYZ-MIXED coverage arm lapses NAMED on the sweep-level grant total
  rather than passing on an empty set.
  W_SERVE_YIELDFLOOR (80225/0) is kept only as the contrast that
  localized the original 417 to the yield; it corresponds to no
  clause, since the spec reserves no share.
- THE FALSIFIABLE CONTENT MOVED WITH THE RULING AND IS NOW BUILT, as a
  pair over a wire made a QUANTITY (WIREBUDGET slots per process per
  tick; mission never dropped but spending them, the serve walk taking
  the remainder).  W_SERVE_WIRE is the SELF-FUNDING claim measured:
  80463/0 at 16 seeds with both tick classes asserted to occur --
  `wireStarved > 0` and `wireFreed > 0`, non-vacuity in both
  directions -- and the heal completing on slots the sequence left
  because it was holding under R4 or had only its retry tail to send.
  Nothing reserves those slots; a stalled sequence IS the bandwidth.
  W_SERVE_NORESUME withdraws the other half -- a duty the wire had no
  slot for is DROPPED rather than deferred -- and reds 80 at 16 seeds,
  16 at each of the five scenarios, ALL of them the designated oracle
  ("spare capacity never leaves a want unserved") with the heal
  checks, the safety arms and the posture arm silent at every seed.
- THE ORACLE IS THE CLAUSE, NOT ITS CONSEQUENCE, and the record keeps
  why so it is not re-attempted: a heal-completion oracle cannot score
  this pair.  Measured at budget 20 before the direct oracle existed,
  the heal checks fired 12 times in the control and 12 in the
  withdrawal -- indistinguishable, because a legitimately starved
  returner reds the every-process quantifiers exactly as a retired
  duty does.  That is the same instrument limitation the discharge-
  order rows above record, and it is why the clause needed reading
  directly: the walk grants until the cap or until nothing is owed, so
  a slot left unspent beside a standing want is not a deferral.
- The budget is MEASURED, not derived: 4 and 8 never free the wire
  (104 and 97 failures), 128 never binds it (20 -- the starved-tick
  assertion), 32 is where both classes occur and the control is clean.
- One bound on what this instrument can show, from the Fable review
  (2026-08-01): counting only the LIVE frontier round's carriers as
  the yielded-to class, the same floorless arm runs 80225/0 -- the
  yield never bites, because a process holding under R4 runs no
  instance and its carriers are quiet.  That is R4's hold seen as the
  wire freeing, and it is why the broad class is the one worth
  instrumenting.
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
  arm and H silent at every seed (seam 955-981).  Withdrawing the
  floor costs the heal and costs nothing else.
- The rotation ABSORPTION and its reason: O1's linkage bounds a
  flooding solicitor to ONE duty at a server (it proves possession of
  every round but its highest and erases its own want bits), measured
  as solicitor max 1, cohort max 6, cap 2; and a merely-displaced
  correct wanting process still completes on its own account under
  BPR.  Falsifying the clause needs t liars filling the cap AND one
  correct wanting process that cannot complete on its own, which
  costs t+1 faults and is out of model at every t (seam 997-1033).
- W_REACH_WSHRINK is a SIZING report, not an L1 red: with w = 1 and
  the reach made to BIND by a withholder at n=7 t=2, the heal still
  completes inside the single rung the round is retained for -- 14
  evictions, zero stalls or strands (seam 1225-1265).  Read with the
  A6 pins it is the same fact from the other side.

## 3. L2 ADOPTION AGREEMENT (md 1595-1707)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| A10 fold binding | M_INJ_CANDIDATE | RED | 227, seam 122 | glue |
| I5 witness ground | MM_I5_* + inv I5 | RED | paths 82, 81 82 | mut |
| A3 server asserts | BYZ_MIXED/EQUIVOC | GREEN | seam 601-608 | cfg |
| C6 byte-match | W_L2_NOBYTEMATCH | RED | 4, seam 1267 | prem |
| C6 re-arm | W_L2_NOREARM | RED | 18 at 16 x 8% | prem |
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
  (BYZ-MIXED; see the RELOCATION note below -- the seed moved); NOVOID
  keeps a stale adopt debt
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
- W_L2_NOREARM RELOCATED, and the record corrects a wrong conclusion of
  its own.  The arm was first reported DARK at HEAD on a 16- and then a
  32-seed sample, and the row was set UNCOVERED.  THAT WAS WRONG: the
  arm reds at 64 seeds -- 2 failures, BYZ-MIXED seed 40, the E plus
  fabrication pair exactly as recorded.  Its single seed had moved past
  every sample drawn, which a 2-failure red at one seed can do without
  anything about the clause changing.
  THE ATTRIBUTION STANDS: the step-2 and round-turn relocation bridged
  in the DELIVER arm splits one emitAcs into per-source batches and
  shifts the scheduler RNG, the same effect that re-froze the default
  sweep from 42781 to 42804.  The pre-bridge build reds at 16 seeds
  (31528/2, built from HEAD~1 against its own objects); the post-bridge
  build needs 64.  Exposed, not caused -- and the deferral its record
  named ("mutant kill matrix not re-run") is what this was holding.
  THE REPRODUCTION MOVED TO LOSS, because a 64-seed run is not a matrix
  step: at 16 seeds and 8% loss the same designated red fires ten times
  over (20 failures, all of them the E plus fabrication pair), and the
  CLEAN build is 0 failures at that rate -- 8% and 12% both, with 20%
  outside the envelope where the clean build itself reds.  The loss is
  the reproduction, not the cause.  `make seam-premises` now runs the
  arm that way.
- A COVERAGE HAZARD THE ABOVE EXPOSES, recorded for the next reader:
  the matrix runs its arms at 8 seeds, so any arm whose red is one or
  two failures at a single seed can leave the sample silently whenever
  the schedule shifts, and will report GREEN while its clause has no
  countermodel.  W_L2_NOREARM is the one that did.  Every other
  falsifying arm in the matrix reds at 8 seeds with counts well clear
  of a single-seed accident (NOBYTEMATCH 2, I10 8, NOCLOSEVOID 66,
  SYBIL 180, CAP0 106, NORESUME 41), so none is presently at risk --
  but a count of 2 is the warning sign, not a comfort.

## 3b. The cursor birth (md Model, want; landed 2026-08-10)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| birth above the cursor, gated | ctr K + MM_CURSOR_UNGATED | RED | K x19, I2 | mut |
| stale act births nothing | ctr K | GREEN | K | chk |
| heal route end to end | none | UNCOVERED | see note | -- |

Notes.

- The machine side is covered: section K pins the birth's whole
  contract (above-only, possession-gated, self gated by I3, frontier
  inert, released cursor rounds locate, serve-walk discharge) and
  MM_CURSOR_UNGATED is the matched red through the falsifier's I2
  arm.  The CALLER side -- deriving cursor evidence from the
  authorship order -- is wired in example/system.c (the offset
  floor's authored high-water IS the observation) and passed as 0 by
  the composed seam, which has no authorship layer; 0 is that
  harness's truthful value and costs only the re-entry route.
- The heal route end to end (a closed-cut-round returner re-entering
  through cursor want, serves of its successor, and the possession
  their traffic carries) needs a long partition with survivors
  advancing many rounds -- not stageable by the in-repo instruments
  (the example's -L cuts one position and its heal wins every race).
  It lands with the deployment-side measurements when the sister
  port unfreezes; UNCOVERED here, stated rather than absorbed.
- W_SERVE_ROTDROP's absorption record (section 6) stands AS
  INSTRUMENTED: the seam passes no cursor evidence, so its
  one-duty-per-server measurement is unchanged.  Under cursor
  evidence a solicitor's act births up to a reach of duties toward
  it; the price is the same containment (per-sender, possession
  gated, cap and rotation) but the measurement does not exist yet --
  it rides the same deployment-side work as the heal route.

## 4. L3 SUPERSESSION (md 1708-1745)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| commutation, both orders | ctr J, 10 scenarios | RED | MM_I6 J x10 | mut |
| full-reach pre-state | ctr J pre-state 1 | RED | MM_I11 J x1 | mut |
| once-per-round | ctr J second close | UNCOVERED | no mutant | chk |
| wrap-crossing pre-state | CLOSED 2026-08-10 | CLOSED | no wrap exists | -- |
| reset voids ADOPT (caller) | M_SEAM_NOVOID | RED | 10, seam 658 | glue |
| close voids ADOPT (caller) | W_L2_NOCLOSEVOID | RED | 111, seam 1726 | prem |

Notes.

- Section J replays both orders from a byte-identical snapshot of the
  WHOLE caller allocation and compares with memcmp, so no conjunct of
  "identical" is chosen by the test; the close's own acts must match
  kind-for-kind and round-for-round, and order A's ADOPT acts are
  encoded per scenario rather than ignored (ctr 56-71).
- MM_I6_BOOK_SURVIVES fires ALL TEN commutation scenarios -- the
  surviving book is exactly the state difference the two orders must
  not have (inv 110-114; mm 31-35).  MM_I9_SLOT_NOFREE fires J x2 and
  MM_I11_EVICT_NEWEST J x1 (the full-reach non-vacuity arm), so the
  pre-state that makes the close output a RELEASE is non-vacuous.
- The once-per-round corollary is checked in both of the proof's arms
  (before a later launch the second close finds no live instance;
  after one it names a round that is no longer the frontier), but no
  mutant in the tier designates those checks, so they are green
  witnesses and not matched reds.
- J's three pre-states are frontier 0, frontier 4 over a full reach,
  and a t=0 frontier 0 (ctr, section J).  The once-open wrap-crossing
  hole CLOSED 2026-08-10 with the round-name widening: positions do
  not wrap, so no pre-state can cross a boundary that no longer
  exists (the falsifier's run 1 drives the frontier across 256 as a
  non-event and the canonical state sets match run 0 exactly).

## 5. L4 PRESENTATION (md 1746-1783)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| launch answers exclusive | md 986-987, ctr C | UNCOVERED | no mutant | chk |
| MAINTAIN outranks ADMIT | ctr H | UNCOVERED | no mutant | chk |
| one launch act per opp. | ctr C | UNCOVERED | no mutant | chk |
| value rides a JOIN | ctr C join arms | UNCOVERED | no mutant | chk |
| caller: staging | M_R1_EARLY | RED | r1m 69 | r1 |
| caller: byte-identical | M_R1_BYTES | RED | r1m 50 | r1 |
| caller: retire on subset | M_R1_RETX, M_R1_NORETIRE | RED | r1m 35, 61 | r1 |
| honoring the answer | M_SEAM_FREE | RED (tw) | seam 313-330 | glue |

Notes.

- The machine half rests on the tables' compile-time exclusivity and
  exhaustiveness (the dtc discipline, md 986-987) and is exercised by
  contract sections C and H -- the full precedence chain in one call
  ("owed work before maintenance before chosen work"), MAINTAIN
  outranking a pending value without consuming it, and "live: no
  second launch".  Those are GREEN WITNESSES: no mutant in the
  machine-mutant tier targets the launch rules, so the machine half
  carries no matched red.
- The CALLER half was UNCOVERED here until 2026-08-04, and the note
  recording that stands as history: the composed seam passes
  `valuePending` as a constant of the round index and `maintenanceDue`
  as 0, stages no application value, and keeps no exactly-once ledger
  -- those facts are unchanged, and the seam is still not the vehicle.
  The vehicle is `example/system.c` (built 2026-07-26 onward): it
  stages accepted values, re-presents them byte-identically at the
  same signing offset, retires only on witnessing the value in an
  agreed subset, and its exactly-once verdict quantifies every staged
  value over every correct process's sequence -- R1's end-to-end
  oracle.  `test/r1Mutants.sh` (make r1-mutants) is its matched-red
  tier: four anchored mutations of a scratch copy, one per caller
  clause, each killed by that verdict against the CLEAN machine
  objects.  The sharpest is M_R1_BYTES: the drifted presentation
  stays internally consistent at every gate below (bank, digest,
  exchange, sequence identity all green) and ONLY the comparison
  against the staged originals reds -- the "oracle must compare GLUE
  artifacts" precedent, measured at the R1 seam.  M_R1_RETX also
  measured a reachability fact: exclusion of a RIDING value is a
  length property (full subsets at every short-run shape tried; the
  wrap-length n=7 run staggers as ordinary operation), which is why
  its config is 7 2 3 300.  R1 (the conjunction of the two halves) is
  now covered end to end: machine half by contract sections C and H,
  caller half by the tier's four reds.
- M_SEAM_FREE is a caller red for a different claim -- the glue
  launching on its own account, ignoring the machine's answer -- and
  it is a TRIPWIRE red, corrected 2026-08-14 by the line-by-line read
  (section 11b).  It was recorded here as firing "D's
  machine-consistency arm at every seed", and D's arm did print,
  because the mutation incremented the very counter that arm asserts
  on.  The mutant's site now has a counter and a check of its own, and
  what fires is that self-report: BOTH D arms stay silent.  Correctly
  so for the ground-truth arm -- with n-t real closes behind it the
  advance is permitted by R4, and what the mutant bypasses is the
  tolerance BUDGET, which only the signal sees.  The row keeps its
  place: the obligation is real and the arm does catch a glue that
  reports itself; what it cannot catch is a silent one, and only
  crypto-side evidence would (seam 313-330).

## 6. L5 RELEASE SAFETY (md 1784-1848)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| I8 release at all-n | MM_I8_EARLY | RED | ctr C x1 D x1 | mut |
| A9 attribution | W_A9_SYBIL | RED | 362, seam 841 | prem |
| A8 truthful evidence | BYZ_FORGE_POSSESS | GREEN | seam 588-593 | cfg |
| I2 nothing owed | ctr D, inv I2 | UNCOVERED | seam 5310 | chk |
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
  (seam 1388-1391).
- The I2 direction is deliberately NOT asserted at the seam -- the
  machine maintains it by construction, so asserting it there is
  unfalsifiable; it belongs to the contract suite (seam 5310-5313).
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
- The eviction exception is out-of-band by definition and is
  exercised three times over: BYZ_SILENT, W_A4_PARTITION's victim,
  and W_SERVE_CAP0's starved cohort each make all-n unreachable so
  that eviction is the only release path left (seam 610-616, 760-764,
  967-970).

## 7. L6 SEQUENCE IDENTITY (md 1849-1909)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| L2 downstream | seam E everywhere | RED | see L2 rows | cfg |
| I10 caller half | W_I10_WRONGARTIFACT | RED | 14, seam 1342 | prem |
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
  candidate differs was reached (seam 1342-1370).
- A11 is supplied BY CONSTRUCTION: the seam folds every first round
  over one shared `Genesis` constant, and no arm withdraws it or
  seeds divergent bases.  The induction base of L6 is therefore
  untested.

## 8. L7 NAMING AND TWO-GRAIN SOUNDNESS (md 1910-1944)

| premise | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| first half (I1) | ctr F deep retention | HAND | ctr F | chk |
| second half (H_r) | -DHRTWIN + MM_HR_GATES | RED | path 45 56 2 99 | mut |

Notes.

- RE-POINTED 2026-08-10 (the round-name widening): the lemma's first
  half is now NAMING soundness -- a position denotes one round at any
  depth, from I1 distinctness plus the birth discipline -- and it
  inherits I1's re-pointed coverage exactly: the induction's hand
  discharge plus contract section F's deep-retention checks.
- The twin drive was an UNFALSIFIED WITNESS until the machine-mutant
  tier landed; MM_HR_GATES gates the SERVE walk on the held-members
  grain -- H_r entering a decision -- and the twins diverge on the
  serve cursor at BFS path "45 56 2 99".  The PLAIN falsifier is
  BLIND to the same mutation by construction (H_r is outside its
  alphabet) and runs clean at ~310k states, which is precisely why
  the twin arm exists.  The pairing that gives the red its meaning
  re-ran 2026-08-10 under the ROUND ABSTRACTION: the unmutated
  machine under the same twin drive, 621,286 states / 59,371,888
  transitions per run, 40,704,464 differing-grain observations, ZERO
  divergence (earlier freeze: 621,094 / 43,711,360 / 29,742,272).
  The 2026-08-10 re-run also CAUGHT AN INSTRUMENT DEFECT the
  abstraction created: an act's round became a BORROW (valid until
  the next machine call), and the twin drive read its close acts'
  rounds after the late-assembly call -- the pairing failed until
  the drive snapshotted the names first.  The same class was found
  and fixed in the seam's sysClose (acts applied after the pendF
  re-present); the example was audited safe.  The caller audit rule
  is recorded in mm and the session memory (inv 77-90; mm 61-71).

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
| config n=4 t=1 reach 3 | seam default | GREEN | 42804/0 | cfg |
| config n=2 t=0 | seam _t0 | GREEN | 2156/0 | cfg |
| config n=7 t=2 | seam _big | GREEN | 80249/0 | cfg |

Notes.

- Each `-DSCHED_*` build replaces the POP CHOICE and nothing else, and
  every policy draws exactly one RNG step per successful pop at the
  uniform policy's position, so the loss coin sequence a seed names is
  policy-invariant (seam 1452-1467).  Zero failures under every policy
  at every seed, zero stalls, every classification an accepted strand;
  the safety arms never move.
- Two schedule measurements are FINDINGS and neither is a red.  LIFO
  spends the fault budget with the SCHEDULE: the agreed subsets shrink
  to 3.156 members of 4 against 3.692 elsewhere -- the tolerance R4
  reserves consumed by ORDERING rather than by fault -- and the heal
  pays for it (2584 adoptions against 521).  KINDFLIP produces one
  LAGGARD accepted strand the uniform policy never reaches, because
  the possession indication rides the ACS tails that carrier-priority
  inversion defers (seam 1482-1527).
- The enumeration's honest scope, in its own terms: the full tree is
  on the order of 20^400, so every honest form of the mode is a
  BOUNDED-DEPTH one -- what is exhaustive is the PREFIX (depth 6 at
  ROUNDS=1, depth 5 at ROUNDS=2) and the tail is one sample.  Both
  leaf counts are asserted in-program.  It does NOT reach the
  Byzantine arms (t=0 has no budget), the laggard (out of model at
  n-t = n, verified), or any divergence beginning after the enumerated
  prefix.  It is a DEEP BOUND at a TINY shape complementing the seeded
  sweeps' shallow bound at three shapes; NEITHER SUBSUMES THE OTHER
  (seam 1605-1677).
- The loss envelope: safety is green at every swept level and liveness
  degrades by CLASSIFICATION alone, monotone in the loss.  The first
  accepted strand outside the STARVE positive control appears at 12%;
  the highest fully-green level is 15%.  At 20% the 7 failures are
  posture x4 and C x3 at STARVE seeds 9 and 13, with D both halves, E,
  F's unsafe arm and H silent -- an S-against-loss-rate SIZING
  boundary, not a safety one (seam 1539-1585).
- The n=2 t=0 point carries two arms of its own: TOLERANCE is
  asserted NEVER READ (and never is), and the serve floor of ONE is
  the entire heal capacity, exercised by ordinary loss (7-13 serves a
  run, adoption at 9 of 16 seeds).  LAGGARD is OUT OF MODEL there,
  verified rather than assumed (seam 680-698).

## 10. Caller obligations C1-C12

C1-C6 are the Mechanization-status caller list in the order it states
them (md 2131-2170); the composed seam cites C1 and C6 by those
numbers.  C7-C11 were caller halves the spec consumed but did not
enumerate; on 2026-07-25 the architect landed C7 (I10's caller half,
md 2173-2180) and C11 (the possession-evidence return leg plus the
O1-inference translation, md 2181-2193) in that list, appended after
C6 so the cited order of C1-C6 is unmoved.  C8-C10 remain reference
labels for obligations stated elsewhere in the spec (A9's second
clause, A6's honest gates, the sequential-entry premise).  The same
landing added a COMPLETION analog to C6's void clause (md
2158-2165): a close consuming the round voids any unconsumed ADOPT
for it.  Its matched red landed the same day (architect-ordered):
W_L2_NOCLOSEVOID, 53 failures at 16 seeds with 32 unvoided debts
counted -- and 9 of its 25 E firings occur at PLAIN/LAGGARD with
ZERO faults present, making it the one C6 clause that is NOT
Byzantine-conditional: the stale candidate is the right bytes for
the WRONG round, so an honest schedule reaches the red.  Its
mute-liar face also wedges the correct cohort outright (the
BYZ-SILENT block), so the clause is load-bearing for liveness
beside L6.

C12 was landed 2026-07-30 by the same pattern and for the same
reason -- a half the proofs consume and the spec had not enumerated
as an obligation: evidence is presented for the round its IDENTITY
proves, never for the round its byte names (md 2194-2211; the clause now
reads "never for the round a bounded name suggests", the
byte-dissolution wording).  It was
found by the layer's first caller, whose ingress table banked
possession indications on the byte; the Model already states the
fact (an act is OF a round by its identity) and A9 already pinned
the SENDER coordinate of a recorded bit, but no caller half bound
the ingress round argument.  system.h carries it at both ingress
entries; system.dtc's act-received preamble speaks it beside C11's
hold (comment-only -- no dispatch input changed).

| obligation | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| C1 serve cap floor | W_SERVE_CAP0 | RED | 212, seam 947 | prem |
| C1 serve rotation | W_SERVE_ROTDROP | ABSORBED | 14049/0 | prem |

| C1 discharge order | W_SERVE_YIELD | GREEN | 72758/0 | prem |
| C1 self-funding | W_SERVE_WIRE | GREEN | 80463/0 | prem |
| C1 yield never retires | W_SERVE_NORESUME | RED | 80 at 16 | prem |
| C2 byte budget | none | UNCOVERED | no evict call | -- |
| C3 PRESENT staging | M_R1_* tier | RED | r1m 33-79 | r1 |
| C4 have-grain currency | M_EXCH_NOASSEMBLE | RED | seam I arm | glue |
| C5 R2b resume | none | UNCOVERED | no arm | -- |
| C6 byte-identical count | W_L2_NOBYTEMATCH | RED | 4, seam 1267 | prem |
| C6 re-arm on switch | W_L2_NOREARM | RED | 18 at 16 x 8% | prem |
| C6 reset voids ADOPT | M_SEAM_NOVOID | RED | 10, seam 658 | glue |
| C6 close voids ADOPT | W_L2_NOCLOSEVOID | RED | 111, seam 1726 | prem |
| C6 identity of round | M_SEAM_UNBOUND | RED | 391, seam 319 | glue |
| C6 fold ground | M_INJ_CANDIDATE | RED | 227, seam 122 | glue |
| C7 I10 caller half | W_I10_WRONGARTIFACT | RED | 14, seam 1342 | prem |
| C8 A9 sender argument | W_A9_SYBIL | RED | 362, seam 841 | prem |
| C9 A6 honest gates | W_A6_PIN0 / PIN1 | GREEN | 12667, 16094 | prem |
| C10 sequential entry | none | UNCOVERED | out of model | -- |
| C11 hold, re-present | M_SEAM_NOPEND | RED | 11, seam 301 | glue |
| C12 round argument | resolution, by construction | DISCHARGED | md Mechanization status | -- |

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
  restored its opening, then 1 failure at BYZ-MIXED seed 16.
- C11's red is SEED-DEPENDENT (11 failures at LAGGARD seed 5; 0 at 4
  seeds, fires by 16) and cannot be caught by the STARVE scenario at
  all, because there the indications never arrive.  Its partner is
  contract section I, which pins that the record a held-then-
  re-presented indication reaches is byte-identical to the record a
  timely one reaches, with a non-vacuity arm (ctr 52-55, seam 301-311).
- C12 DISCHARGED 2026-08-10 by the round-name widening: the machine's
  round argument is the POSITION the caller resolves from the asserted
  identity, so the byte-keyed mis-translation C12 forbade has no
  argument to ride -- there is no bounded name at the API to translate
  by.  What remains of the obligation is that the caller RESOLVE
  (md Mechanization status, the round-argument clause); a deployment
  that routes a bounded wire hint into the round argument re-opens the
  hazard on its own side of the API, which no machine-side oracle can
  see -- that residue keeps the caller-arm note below: an ingress arm
  that feeds
  a colliding byte with an indication set and asserts no bank, with
  reversion to byte-keyed banking as the mutant.  Until a caller
  enforces the obligation there is nothing to falsify, which is why
  this row reads UNCOVERED rather than HAND.

## 10b. The retention seam C13-C20 (landed 2026-08-13; C20 and the store-mutant tier 2026-08-14)

Retention was EXTERNALIZED to the caller: the rounds this process
holds, and the three per-round records, live in caller storage
reached through four operations fixed at init -- `records`,
`retain`, `release`, `after`.  Every RULE stayed in the machine;
only the storage moved.  The spec states the terms canonically as
THE RETENTION REQUIREMENTS (md Mechanization status) and `system.h`
mirrors that list.

These are caller obligations of exactly the C1-C12 kind, numbered
in the order the spec states them.  What earns them a register
entry rather than a footnote: several conjuncts were STRUCTURALLY
TRUE while the machine owned the retained set and are now PREMISES.  I1's
count arm, I9, I10a and I11 rest on C13; I2, I3, I4 and L5's
attribution step rest on C15; BASE rests on C19.  The proofs did
not weaken -- what changed is who must make their antecedents true.

| obligation | arm | status | evidence | repro |
| --- | --- | --- | --- | --- |
| C13 set changes only through retain/release | SM_C13_RETAIN_EVICTS | RED | I9 x9; str x10, ctr E F J | smut |
| C14 retain succeeds for a close | SM_C14_SECOND_REFUSES | RED | seam D x27 of 34; str x7 | smut |
| C15 machine is the records' sole writer | SM_C15_STORE_WRITES | RED | I10 birth x9; str x1, ctr K x5 D x2 H x1 | smut |
| C16 after ordered | SM_C16_NEWEST_FIRST | RED | I11 x9; str x14, ctr L x7 M x3 K x5 E x3 | smut |
| C16 after forward, terminating | SM_C16_NOT_FORWARD | RED | timeout 124 x3; str x12 | smut |
| C17 name stability | SM_C17_SCRATCH_NAME | RED | ctr M x3; str x3 | smut |
| C17 record stability | SM_C17_SCRATCH_RECORD | RED | ctr M x3; str x3, ctr K x5 | smut |
| C18 release is the drop (store half) | SM_C18_NO_DROP | RED | I4 x8; str x13, seam 322, ctr E x9 F x11 | smut |
| C18 the act is not a second drop (caller half) | inert here | UNCOVERED | note below | -- |
| C19 store empty at init | SM_C19_PREPOP_FRONTIER | RED | I7 x8; str x24 | smut |
| C19 store empty at init | SM_C19_PREPOP_UNOWNED | RED | I3 x8; str x26 | smut |
| C19 store empty at init | SM_C19_PREPOP_ALLN | RED | I8 x9; str x26 | smut |
| C20 minted successor does not EQUAL | SM_C20_MINT_EQUAL | RED | I7 x9 | smut |
| C20 minted successor FOLLOWS | SM_C20_MINT_BEHIND | UNCOVERED | 187,550 states -> 25, no conjunct | smut |

`str` = `test/test_systemStore.c` (`make check`); `ctr` =
`test/test_system.c`; `seam` = `test/test_system_seam.c`; a bare I<n>
is the falsifier's conjunct.  Each cell names the DESIGNATED oracle
first and the rest after the semicolon: the tier runs all four
instruments against every arm and RECORDS what they do without
asserting it, since which instruments a broken store reaches is the
fact the tier exists to establish.  Counts are from the record run of
2026-08-14 (41 min, exit 0, `systemStore.c` and the driver checksummed
identical before and after).

Notes.

- THE TIER LANDED 2026-08-14 (`test/storeMutants.sh`, `make
  store-mutants`), and every row above moved from GREEN CONTROL to
  MATCHED RED but two.  Until it ran, the rows said only that a
  CORRECT store discharges each term and that the machine composed
  with it behaves; nothing showed that a BROKEN store gets CAUGHT --
  the same status the falsifier's I1-I11 arms held before
  `machineMutants.sh` landed.  Thirteen arms, one anchored mutation
  each against a scratch copy of `systemStore.c` (two against the
  falsifier instead -- succession is a ROUND operation the store never
  sees), all four instruments run against every arm, and only the
  designated oracle asserted.  A CLEAN pairing runs last through the
  same build and link arrangement, with `-DEXPECTSTATES` on the
  falsifier, because a mutation harness lies upward and a tier of
  kills and a harness that reds by itself look alike.
- WHAT THE TIER FOUND BESIDE THE KILLS, and the first item is the
  reason a tier over a shared artifact is worth its cost:
  - THE STORE'S OWN SUITE WAS BLIND TO TWO OF ITS OWN TERMS.  A store
    that ORs possess into want on every ask (C15), and one that
    answers `after` from a single reusable buffer (C17, both
    siblings), passed `test/test_systemStore.c` outright: each holds
    the right bytes at the instant of every return that file looked
    at.  CLOSED the same day by its new section J -- a pure-lookup arm
    stated over the WHOLE allocation, and two answers from one walk
    held live at once.  SECTION J'S FIRST CUT WAS BLIND TOO, and the
    tier's own re-run is what said so: it filled each record with one
    repeated byte, which a store that folds one record byte into
    another leaves unchanged.  The fill ramps now.  An arm is not an
    oracle until something has failed it -- the same sentence this
    register exists to keep saying, one layer further in.  The record
    run confirms the closure: C15 reds it x1, each C17 sibling x3, and
    C16-not-forward x12 (that last one only after J's own walk was
    BOUNDED, since an unbounded one hung where the suite had reddened).
  - THE MACHINE'S CONTRACT SUITE HAD NO ARM FOR C17 EITHER, and could
    not have: the instruments snapshot act borrows at the call (the
    falsifier's own 2026-08-13 fix), so a name that dangles at the
    NEXT call is invisible to them.  Contract section M was added for
    exactly this and is C17's designated oracle.
  - THE COMPOSED SEAM IS GREEN under C16-order, both C17 arms and two
    C19 arms: at reach 3 with its schedule, an order or lifetime
    defect never reaches one of its checks.  That is evidence about
    the seam's reach, not about the store.
  - THE FALSIFIER IS BLIND TO C14 -- it runs the full enumeration
    clean -- which is why the register named the seam's D arm for that
    term before either was built.  The prediction held: 27 of the
    seam's 34 failures are D.
- WHAT THE PORT ADDED BESIDE THE ARMS (2026-08-13).  The store is now
  under all four instruments: the contract suite seats every section
  on it, the falsifier carries ONE PER BFS NODE and keys it into the
  visited state (the retained set is reachable state the machine
  directs but does not hold, so keying the seat alone would merge
  states), the composed seam holds one per seat and hashes its live
  entries into the injector's state-equivalence oracle, and the
  example allocates one per process at its declared reach.  Two
  findings came out of that, both instrument- or store-side and
  neither a machine defect: a release left the vacated entry's bytes
  in place, which split states nothing can observe (fixed in
  `systemStore.c`, and it was inflating the horizon-3 reachable set
  4.5x), and the falsifier's deep-start preamble ended on a RELEASE,
  leaving its root carrying a borrow the fresh root has no
  counterpart for.  Both were caught by the cross-run
  translation-invariance comparison -- the third time that arm has
  found something.
- THE TARGET EXISTS AND IS NOW PINNED.  `systemStore.[hc]`
  (top-level, 2026-08-13) is the shared reference store, which is
  what makes a mutant tier possible at all: with four hand-rolled
  stores there is nothing canonical to mutate.  It is safe to share
  for one reason and only that reason -- it encodes NO machine
  knowledge, so it cannot absorb a machine defect and blind every
  instrument identically.  `test/test_systemStore.c` is its own
  contract suite, in `make check`, and it exists because a shared
  artifact that every instrument trusts must be wrong in a way
  something can SEE: its sharpest arm is the reversed comparator,
  which a store that fell back to comparing name bytes would fail
  while passing every other arm.
- THE DESIGN AS IT WAS PRESCRIBED (2026-08-13), kept because what it
  got right and what it got wrong are both worth having: a STORE-MUTANT
  TIER on the `machineMutants.sh` pattern, mutating a SCRATCH COPY of
  `systemStore.c` instead of `system.c`, one term per mutation, each
  with a designated oracle.  WHERE THE BUILT TIER DIVERGED, measured
  2026-08-14:
  - C15's oracle is I10's birth clause, not I2.  The mutation makes
    want equal possess at every retained round, so I2 IS violated --
    but the falsifier keeps the first bad per state and checks the
    birth clause ("possession is exactly self and nothing owed")
    ahead of it.  Contract section K carries the I2 face.
  - C19's third arm reports I8, not I4.  A pre-populated round that
    every process already possesses does not sit there violating I4;
    the machine RELEASES it, and what breaks is the release
    accounting.  I4's matched red is C18's arm, where the round
    genuinely survives.
  - C17 needed a NEW oracle before it could be run at all (contract
    section M) -- the designated one did not exist, since every
    instrument snapshots act borrows at the call.
  - C18's arm is the STORE half (release does not drop), not the
    caller half the design named; see the C18 note below.
  - C20 came out as two arms, only one of which reds; see its note.
  The per-term prescriptions, as written then:
  C13 -> a store that evicts inside `retain` instead of refusing
  (oracle: a round departs with no RELEASE act -- release safety, and
  the seam's content-completeness check);
  C14 -> a store that refuses the second `retain` (oracle: the close
  advances having retained nothing, duty then reads MET and the
  frontier outruns n-t possession -- the seam's D ground-truth arm,
  which already exists);
  C15 -> a store that clears a want bit behind the machine (oracle:
  I2, and the serve retirement);
  C16 -> a store enumerating newest-first (oracle: I11 -- the
  caller-side twin of MM_I11_EVICT_NEWEST);
  C17 -> a store answering `after` from one reusable scratch record
  (oracle: a SERVE act whose .want belongs to a different round than
  its .round); its sibling is the scratch NAME -- a store answering
  `after`'s *name from per-call scratch dangles every serve act's
  .round.  The 2026-08-14 reviews SETTLED the term at until-the-
  set-next-changes: an ordered store legitimately RELOCATES a
  round's record and name when a retain or release lands below it
  (contents intact, re-ask reaches them -- the store suite's D
  arms pin exactly that), so until-release was too strong and
  until-return too weak; per-call scratch violates the settled
  term, relocation does not.  systemStore conforms -- names and
  records live in entries and move only when the set changes;
  C18 -> a caller that drops the round again on the RELEASE act
  (oracle: double free, under the existing leaks discipline);
  C19 -> a store pre-populated at init (oracle: I3, I4, I7 at the
  falsifier's base state).
- C16 IS THE ONE TERM THE MACHINE CANNOT SURVIVE.  An `after` that is
  not strictly forward does not terminate: its mutant HANGS rather
  than reds, so its oracle is a timeout, which no tier here had.
  Named in 2026-08-13 so the arm would be designed rather than
  discovered, and named in `system.h` and the spec as the one place
  this layer is not inert on bad input.  BUILT 2026-08-14 as
  SM_C16_NOT_FORWARD: the tier carries a short per-arm watchdog beside
  its long one, and 124 is the verdict.  MEASURED: the contract suite,
  the seam AND the falsifier all hang, and the store's own suite reds
  in under a second -- so the non-termination is reached by every
  instrument that drives a walk, and the store suite catches this one
  where it catches neither C15 nor C17.
- C18'S CALLER HALF IS INERT AGAINST THIS STORE, and the row says
  UNCOVERED rather than pretending otherwise.  The term has two
  halves: release drops the round (the STORE's, killed by
  SM_C18_NO_DROP), and the RELEASE act is not an instruction to drop
  it again (the CALLER's).  A caller that does drop again calls
  `systemStoreRelease` with a name the store no longer holds, which is
  documented inert -- so the mutation is a NO-OP here and a no-op
  mutant is exactly what a mutation harness must never score as a
  kill.  The half binds a store whose release frees an artifact or
  drops a refcount; nothing in this tree has one, and the honest place
  for the arm is the deployment that first does.
- C14 IS WHERE THE ARCHITECT'S RULING LANDS (2026-08-13): the
  frontier MUST advance at every close, and a caller may release
  retention to keep that true.  The machine does not verify it, so a
  caller that cannot hold the round it just closed sheds that round
  silently -- nothing can serve it, and the advance signal reads its
  absence as duty met.  That is the sharpest new UNCOVERED row: it is
  an R4-absence shed with no event anywhere, and its oracle already
  exists in the composed seam the moment a store sits under it.
- C13's sub-case is the one a drafter omits: `retain` making room by
  dropping a round ITSELF.  The refusal branch is constrained
  ("changed nothing"); the SUCCESS branch is where the silent
  departure hides, and O6 is the requirement it breaks -- the floor
  advances only by release or by an eviction the process REPORTS.
- C20 (landed 2026-08-14, from the retention-seam review): the name
  a close MINTS must follow, under the comparator, the frontier it
  succeeds and every retained round -- I1's strictly-behind and the
  induction's COMPLETE case consume it, and distinctness alone
  ("released before recurrence") does not give it.  It is a ROUND
  operation's term, carried in this table because it is the same
  seam family: structurally true in the ordinal instantiation
  (every instrument mints closed + 1), a real obligation for a
  chain deployment whose comparator is a history lookup.  Its arm
  is a MINT MUTANT -- a close handing a name that compares behind
  the frontier -- with I1 and I7 the designated oracles.
  BUILT 2026-08-14 as TWO arms in the driver, and the term SPLIT
  under them.  SM_C20_MINT_EQUAL (the close mints the closed round's
  own name) reds I7 nine times: a successor that EQUALS is caught,
  because the frontier then names a retained round and a conjunct
  says so.  SM_C20_MINT_BEHIND (a name strictly behind, distinct from
  everything held -- the case the note above says distinctness does
  not give) fires NOTHING: the reachable set collapses from 187,550
  states to 25 and the enumeration reports the invariant held.  A
  frontier behind its own retained rounds classifies everything as
  ahead and the machine refuses its way to a standstill, which costs
  LIVENESS, and no conjunct here states liveness.  So the FOLLOWS row
  stays UNCOVERED with its measurement recorded, and the arm is
  RECORDED-not-asserted in the tier (the MM_HR_GATES precedent).
  What would cover it is an oracle over the enumeration itself -- the
  frozen state count is asserted only in clean builds -- and that is
  an instrument question, not a machine defect.

## 11. The wider validation read (2026-08-14)

The charter's third owed item, and the one that closes it: every
instrument re-run against ITS OWN HEADER RECORD, on this register's
discipline -- a green arm is a control, and only a matched red proves
an arm can catch.  What follows is what was re-run, what it did, and
what had moved since it was written.

| instrument | what was re-run | verdict |
| --- | --- | --- |
| `test/r1Mutants.sh` | the whole tier, `make r1-mutants` | EXACT, 16m28s |
| the seam's M_* matrix | all 22 glue mutants at 16 seeds | 22/22 fire their designated check |
| the seam's W_* matrix | all 18 premise arms at 16 seeds | every arm in its recorded CLASS; 7 totals moved |
| `example/system.c` | all five documented configurations | green, leaks 0 |

THE FINDING IS THE MATRIX'S ACCESS, NOT ITS CONTENT.  The 22 glue
mutants had one repro each -- a hand-built binary -- so "22 of 22
FIRE" (2026-07-25) sat through THREE landings without being re-run:
the step-2 and round-turn relocation, the round abstraction, and the
retention seam.  Each of those re-verified the seam's three CONFIG
baselines byte-exact and left the arm matrix alone, which is the
shape a claim goes stale in: nothing was wrong, and nothing could
have said so.  `make seam-mutants` (`test/seamMutants.sh`) is this
read's remedy -- the matrix is a target now, asserting ONE thing per
mutant (that its designated check fires) and recording totals without
asserting them, since totals move with any RNG-stream shift and a
target that reds on every unrelated landing teaches the reader to
ignore it.

WHAT THE M_* MATRIX SHOWED.  All 22 fire, the clean control at
42804/0.  Three PROFILE drifts, none of them a lost arm:

- M_SEAM_UNBOUND's E arm now fires 45 times.  The recorded "3 firings
  of E in 32 runs" is not contradicted -- it predates STARVE and the
  six Byzantine scenarios, so the denominator is 144 runs now, not 32.
- M_EXCH_NOASSEMBLE, recorded DORMANT in 2026-07-24 and restored by
  the Byzantine arm at BYZ-MIXED seed 16, now also fires at PLAIN.
- M_EXCH_MISCLASS, recorded as needing a healing-laggard seed, now
  reds at PLAIN and nowhere else.  Scenario relocation by RNG stream
  is the seam's oldest known behaviour (the strand has relocated
  three times); the arm is what matters and the arm holds.

Several mutants also cut the run far short of 42804 checks (DROP at
4828, STALE at 12287): a broken glue stalls, and the checks after the
stall never run.  Recorded because a reader comparing totals to the
clean baseline would otherwise read a shortfall as arms going silent.

WHAT THE W_* MATRIX SHOWED.  Every control still green, every
falsifying arm still red through its designated oracle, and eleven
arms EXACT to their recorded numbers.  Seven totals moved, and the
cells above have been re-cited:

| arm | recorded | 2026-08-14 | class |
| --- | --- | --- | --- |
| W_A6_PIN0 | 12643/0 | 12667/0 | control, unchanged |
| W_A6_PIN1 | 16095/0 | 16094/0 | control, unchanged |
| W_A9_SYBIL | 464 | 362 | RED, D-arm STILL EXACTLY x5 |
| W_SERVE_CAP0 | 211 | 212 | RED, decomposition holds |
| W_R2C_SILENT | 12999/0 | 12608/0 | control, unchanged |
| W_I10_WRONGARTIFACT | 16 | 14 | RED, same arms |
| W_L2_NOREARM | 20 | 18 | RED, same arm |
| W_L2_NOCLOSEVOID | 53 | 111 | RED, same arms |

Two of those deserve their own sentence, because a moved total can
hide a moved CLAIM and here it does not:

- W_A9_SYBIL's total fell by 102 while the load-bearing number held
  EXACTLY: the register's claim is "D's ground-truth arm, 5 firings",
  and it is 5.  The rest of the drop is C and F firings under a
  differently-scheduled run.
- W_SERVE_CAP0's decomposition was recorded as B 16+16+3, C 128, P
  16, F structural 16, hold-overflow 16, with D, E, F's unsafe arm and
  H silent.  Measured: the same list with B's beyond-reach arm at 4
  rather than 3, summing to the 212.  D, E and H silent at every seed
  exactly as recorded.

## 11b. The line-by-line reading of the arms (2026-08-14)

The sweep above answered whether each arm still FIRES.  This answers
the other half: does the credited check fire BECAUSE the named
obligation was withdrawn, or incidentally?  Forty-four arms were read
at their mutation site against the check they are credited with -- the
seam's 22 M_* and 18 W_*, and the r1 tier's 4.

THE GRADE THAT MATTERS is the seam's own, from the 2026-07-20 review
that demoted four decoration arms: a TRIPWIRE is a counter that moves
only inside the block the mutation edits, so a deployment with the
same defect and no self-report passes it.  A PROPERTY-grade arm reads
state the mutation did not write.

VERDICT: 42 of 44 are property- or oracle-grade and causal.  Two are
TRIPWIRE-GRADE, and the difference between them is the finding:

- M_SEAM_NOHOLD was already disclosed as one ("killed ONLY by the
  counters that see the discard directly.  No behavioral check
  falls"), and that disclosure still holds.  What did not hold is the
  2026-07-24 upgrade in the same entry -- "the STARVE scenario gave it
  teeth".  Measured at 16 seeds: 162 firings, 18 of them STARVE, and
  every one is still the overflow counter (144) or B's
  held-consumption arm (18) -- the counts the seam's own entry
  carries; this paragraph transcribed them 140/22 until 2026-08-15,
  a hit of the very line-start trap recorded below.  A scenario that makes a tripwire fire
  oftener has not made it a property.  Recorded in the entry: read the
  upgrade as a count, not a grade.
- M_SEAM_FREE was NOT disclosed, and the log actively said otherwise.
  It was credited with "D's machine-consistency arm at every seed",
  and D's arm did print -- because the mutation incremented the very
  counter that arm asserts on.  Two write sites fed one counter, one
  of them the mutant's own, so a self-report was indistinguishable
  from D catching a machine/glue disagreement.  FIXED: the mutant's
  site got its own counter and its own check, compiled only under the
  mutant so no baseline moves (all three re-run byte-exact: 42804 /
  2156 / 80249).  The honest picture, which was always true
  underneath: 144 firings of the self-report, BOTH D arms silent.
  `test/seamMutants.sh` now names that tripwire as the arm's
  designated check and marks both arms tripwire-grade in its table.

THE CONTROLS ARE NOT HOLLOW, which is the risk a green arm carries.
Every W_* control asserts either its own non-vacuity or its own
prediction, and the assertions are specific: W_A4_PARTITION requires
exactly one classification and that it be the abandoned process;
W_A6_PIN0 requires the wedge (whole cohort classified, no advance past
round 1, the signal observed withholding); W_A6_PIN1 requires an
unearned tolerance to have advanced a frontier; W_A5_NOINFER and
W_R2C_SILENT require a stall or strand to have appeared; W_A9_SYBIL
requires evidence recorded under a false sender; W_REACH_WSHRINK
requires the floor reach to have BOUND (evictions > 0, the strand
reported not required); the rotation pair requires blind re-offers to
have landed; W_SERVE_WIRE requires BOTH tick classes (the wire bound
on some tick and freed on another); and each C6 arm requires its own
withdrawn clause to have been REACHED, with the file saying in each
case that a zero there is not a green but a coverage gap.

THE r1 FOUR are each one clause of R1's caller half, killed by an
end-to-end property oracle (the example's exactly-once verdict), with
no self-report anywhere: RETX drops the agreed-subset conjunct from
the retire (values appear 0 times), BYTES salts the re-presented copy
(the drift agrees everywhere, so L6 stays green and R1 alone catches
it -- the sharpest discrimination in the tier), NORETIRE never retires
(the value appears more than once), EARLY retires on any own-subset
close (skipped values appear 0 times).

TWO READINGS WITHDRAWN before they became findings, recorded because a
read that reports only its hits is not honest about its method: the
four `W_SERVE*` tokens in the seam that match no Makefile target and
no register row are not undocumented arms but internal flags DERIVED
from the documented ones (`W_SERVE_X` is an arm, `W_SERVEX` is its
machinery); and the seed-dependent clause that appears to trail
M_SEAM_NOHOLD's entry is inside M_SEAM_NOPEND's, where it is true.

ONE MEASUREMENT TRAP, hit twice in this read and worth the sentence:
counting failures by matching a line START (`^FAIL [`) UNDER-COUNTS,
because the seam interleaves its progress line with the failure text
and a few FAIL messages land mid-line.  M_SEAM_FREE reads 140 that way
and 144 by substring; W_SERVE_NORESUME reads 78 against a stated 80,
and the 80 is right.  Count by substring, and treat a near-miss
against a recorded number as the counting method before treating it as
drift.

SCOPE.  The store tier's thirteen arms were written and read in the
session that landed them.  The machine tier's nine were read next, in
the same discipline; that read follows.

## 11c. The machine tier's nine arms, read (2026-08-14)

Each mutation read at its site in `system.c` against the conjunct it
names, and the tier re-run whole (nine kills, `system.c` checksum
identical, the CLEAN -DHRTWIN pairing green at the frozen 187,550 /
20,891,344 with zero divergence, 534s of the run).

ALL NINE LAND WHERE THEIR CONJUNCT LIVES: the adopt guard is I5's
subject, the cursor walk's possession gate is I2's, the witness-book
clear at completion is I6's, the two eviction sites are I9's and
I11's, the close's two retain calls are I10's, the all-n predicate is
I8's, and the serve walk's grain gate is L7's second half.

THIS TIER HAS NO TRIPWIRE PROBLEM, and the reason is structural rather
than careful: the oracle is a conjunct over MACHINE STATE read through
the public queries, and the clean falsifier is green at 187,550
states.  A conjunct that fires here fired because the machine was
mutated -- there is no counter the mutation could write that the check
would mistake for the property.  That is the sharpest contrast with
the seam, where the glue and the checks share a process and two of
twenty-two turned out to be self-reports.  Nor is any conjunct MASKED:
each verdict greps its designated conjunct BY NAME, so a mutant whose
red arrived through some other conjunct would show as NO RED, not as a
quiet pass.

WHAT WAS WEAK, AND IS NOW FIXED: the contract half of each verdict.
The tier's inventory names a contract section per mutant, but the
verdicts asserted only that the contract suite EXITED NON-ZERO -- and
three of the nine make that suite SIGSEGV, so the condition was
satisfied by a crash.  A crash is not a red.  Every verdict now
asserts the NAMED SECTION, the same correction the seam's matrix took
the same day; and MM_I9_DROP_SILENT, which asserted nothing
contract-side at all while its entry claimed section E, now asserts E
(which fires, x1).

THREE SECTION DRIFTS against the records, none a lost arm:

| mutant | recorded | 2026-08-14 |
| --- | --- | --- |
| MM_I10_RETAIN_WRONG | B, C and D | C x5, D x2 -- B no longer fires |
| MM_I11_EVICT_NEWEST | E x3, J x1 | E x3, J x1, plus F x6 |
| MM_HR_GATES | D x3, G, L x7 | the same, plus M x1 |

The last one is this week's own section repaying its cost: M was built
for a store that answers from scratch, and it turns out to have teeth
against a MUTATED MACHINE too -- a serve walk gated on the
held-members grain leaves the borrows it asserts on unserved.  M also
made MM_HR_GATES a THIRD crashing mutant until its dependent arms were
guarded on the act their claims read: the arm reads `out[0]` after a
call that returned no act, which is an instrument bug and not a
machine precondition break, so it was fixed rather than documented.
The suite now reds cleanly there (352 checks, 13 failures) and the
"two of the nine crash" record stays true.

## 12. The spec-direct correspondence (fidelity; landed 2026-08-15)

Every section above this one tests the machine against oracles that
reached their instrument through the same chain the implementation
took: system.md -> system.dtc -> systemRules.c -> system.c.  A
reading error anywhere in that chain is invisible to all of them,
because they inherit it.  `test/test_system_fidelity.c` (`make
fidelity`; deliberately NOT in `make check`) closes that gap the way
`test/test_predicates.c` closes it one layer down: a SECOND,
INDEPENDENT implementation of the machine's whole decision surface,
written from system.md's prose and system.h's contract alone --
system.c, systemRules.c and both .dtc files unread while it was
written -- driven in lockstep with the machine over the reachable
states of the product (seat, serve cursor, systemStore, reference),
comparing at every call the return value, every act (kind, round,
want/have bytes), every documented query, and the record bytes the
machine wrote into the store.  Every reference decision cites the
system.md section it derives from, at its site.

- PINNED, asserted in-program so a truncated search cannot pass as
  agreement: shape A (n=2 t=0, reach 2) 1,364 states / 99,404 calls;
  shape B (n=4 t=1, reach 2) 489,470 states / 65,336,902 calls;
  1,339,795,893 checks, 0 failures, ~34s, leaks clean.  The -DDEEP
  arm (n=4 t=1, reach 3) 1,515,908 states / 231,591,876 calls,
  5,513,225,053 checks, 0 failures, ~126s.  Non-vacuity asserted per
  shape: every act kind, both release causes against the all-n
  release, and exactly the duty classes the shape can reach --
  TOLERANCE zero at t=0 (L1's vacuity sentence MEASURED across
  99,404 calls) and non-zero at t=1.
- NO DISAGREEMENT AT ANY REACHED INPUT.  The two readings of
  system.md agree on every compared answer.
- TEETH: `test/fidelityMutants.sh` (`make fidelity-mutants`,
  2026-08-15) -- fourteen arms, fourteen kills, and the clean
  pairing green at the pinned counts.  FIVE mutate a scratch copy of
  the REFERENCE (a correspondence that cannot red on a broken
  reference cannot be trusted to red on a broken machine); NINE
  carry `test/machineMutants.sh`'s anchored mutations VERBATIM
  against the correspondence.  Reference arms additionally assert
  the first FAIL is a comparison channel, not the pinned-count or
  coverage lines a halted enumeration prints on its way out (the
  crash-is-not-a-red discipline).  Six distinct channels fired;
  three defect pairs mirror across the comparison and halt at the
  same state on the same channel -- the comparison's symmetry
  measured rather than assumed.  The measured mutant-to-channel
  table is at the script's foot.  Two contrasts carry the tier's
  argument: MM_HR_GATES, which the plain falsifier cannot see by
  construction (H_r is outside its alphabet -- the reason -DHRTWIN
  exists), reds the correspondence at shape A in under a second,
  because the reference models the held-members record and the
  serve walk both; and MM_I9_DROP_SILENT, which the falsifier
  needs its retained-set shadow to see, is a plain act count here.
- NOT COVERED, printed by the run itself so the artifact stands
  alone: reach past 3; shapes past n=4 t=1; the ordinal encoding's
  wrap (the comparator's to absorb -- test_system's wrap arm and
  the falsifier's deep start carry it); the held-members grain
  varied only at shape A; the serve cursor's BYTES (an opaque
  caller layout -- only the walk's behavior across calls is
  compared); systemInit's refusal list (a rejected instance
  enumerates nothing -- test_system carries the refusals); and
  everything Mechanization status places caller-side.
- FIVE PLACES THE SPEC OR HEADER UNDERDETERMINE a decision, each
  reading stated at its site in the file and the machine MEASURED
  to agree with it: four are queued below as S8-S11; the fifth --
  the order of the cursor walk against the dispatch inside one
  systemReceived call -- is verified unobservable (the two write
  sets are disjoint) and is not queued.

## Permanently uncoverable

Named here with the reason, because a register that only listed what
fired would be a coverage report and not an honest one.

- THE I1 FRONTIER+1 LOOKAHEAD -- CLOSED 2026-08-10.  The guard was
  deleted with the wrapping byte name (the round-name widening):
  positions never recur, so no structural release exists and nothing
  is left to reach.  The record is kept because the closure is the
  dissolution's own yield: what was permanently uncoverable was the
  guard the bounded name forced, and widening the name removed the
  obligation rather than covering it.
- MORE THAN t BYZANTINE.  Out of model.  No arm exists, and none was
  built even as an observation arm, so this register records it as
  UNCOVERED rather than claiming an observation.
- A12 PERMANENCE (landed 2026-08-12).  Unreachable by construction,
  not merely unbuilt.  The assumption is that at most t processes
  never again evidence anything FOR THE REMAINDER OF THE SEQUENCE,
  and every instrument here runs a bounded number of ticks: a
  process silent for a whole run is indistinguishable from one that
  would have spoken at the next tick, so no finite execution can
  witness permanence or its violation.  What IS reachable is the
  CONSEQUENCE -- BYZ_SILENT and W_R2C_SILENT hold a process mute
  for a whole run and measure what the correct cohort does
  meanwhile -- and those arms sit in the L1 table under their own
  premises.  They corroborate what A12 buys; they cannot test A12.
  The honest reading is that A12 is an assumption in the papers'
  sense, like A2, discharged by deployment posture and not by
  instrument -- which is exactly why the reach it funds (R4) is
  stated as a sizing obligation with a named failure mode.
  RETIRED 2026-08-14 (architect ruling, review of the retention
  seam): no lemma consumes A12, and the finiteness it was assumed
  for is a resource claim below the paper's level -- one level down
  the quiescence gate has no permanence axiom either, and the
  abandonment policy absorbs the never-case there.  The axiom left
  the spec together with the FINITE claims it underwrote: R4 now
  states the retention requirement UNBOUNDED outright, and what
  bounds ACTUAL retention is the reach (O6), sized by the
  deployment (Relation to a deployment).  This note is the dated
  record; the paragraph above it describes the assumption as it
  stood.
- SEQUENTIAL ENTRY (C10).  Out of model: the mechanization is
  sequential -- ten entry points, one at a time, no interleaving
  inside the machine (md 983-984) -- and every instrument calls the
  entry points one at a time, so the instrument's own construction is
  the discharge and cannot also be its test.
- THE L3 WRAP-CROSSING PRE-STATE -- CLOSED 2026-08-10 with the
  round-name widening: positions do not wrap, so the pre-state class
  no longer exists (section 4 carries the dated closure).
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
  t+1 and is out of model at every t (seam 1013-1033, 1330-1336).
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
- L4's CALLER HALF AND R1 -- CLOSED 2026-08-04, kept here as the
  dated record because this section once named it.  "Not permanently
  uncoverable in principle, but uncovered today" was exactly right:
  the missing harness was one that stages a value, and
  `example/system.c` became it.  Its exactly-once verdict is R1's
  end-to-end oracle and `test/r1Mutants.sh` gives it four matched
  reds (see the L4 table and notes above).  This row no longer
  belongs to the uncoverable set.

## Spec observations queued to the architect

Recorded in the instrument headers, carried here as pointers only.

LANDED 2026-07-25 (architect): F1 (the return leg and inference
translation, now C11 in the caller list, md 2181-2193), F2 (the
completion analog of the void clause, md 2158-2165 -- no arm yet,
see the C6 rows), F3 (causal well-foundedness, now a Model
paragraph the L2 proof cites), and F6 (I10's caller half, now C7 in
the caller list, md 2173-2180).  The pointers below remain open.

NUMBERED 2026-08-14 to match the session record, which had them as
S1-S7 while this section carried four unnumbered pointers and was
MISSING TWO: S2 and S3 landed (S2 = F6, S3 = the REACH quantity
below), and S6/S7 had never been carried here at all -- they were in
the W_L2_NOCLOSEVOID work and nowhere a reader of this file would
look.  A register that says "read it FIRST for coverage questions"
does not get to keep half a queue somewhere else.

- S1.  The SERVE rotation clause's load-bearing frame is the floor's two
  non-fault grounds, which no instrument inside the fault budget can
  reach (seam 1423-1429).
- S3.  REACH's binding quantity is heal-time against rung-time, not w
  alone (seam 1431-1433).  LANDED 2026-08-12, kept as the dated
  record: L1's proof now states the race in EVENTS -- one rung per
  adoption against one round per eviction, succeeding when the climb
  reaches the frontier before the last correct retainer of the round
  at the straggler's cursor has evicted it -- and says outright that
  neither side is measurable by any process.  The w-and-T_p rate
  comparison the observation named is gone from the spec; the
  proviso is also now stated in L1's STATEMENT and not only in its
  proof.  Whether the climb wins is stated as the deployment's
  reach and placement (O6, Relation to a deployment); A12, which
  briefly supplied a race premise here, is RETIRED 2026-08-14
  (see the A12 note under permanently-uncoverable).
- S4.  S against the loss rate is a budget coupling the spec does not
  price -- the w x T_p family's third face (seam 1710-1713).
- S5.  R4's reserved t is consumed by adversarial ORDERING as well as
  by faults (seam 1710-1713).
- S6.  C6's completion-void clause is load-bearing for LIVENESS beside
  L6, which the clause does not say: the mute-liar face forks one
  process's chain, spends the budget and wedges the correct cohort
  outright (W_L2_NOCLOSEVOID's BYZ-SILENT block).
- S7.  That same clause is the ONE C6 clause that is NOT
  Byzantine-conditional -- 9 of 25 E firings at PLAIN/LAGGARD with
  ZERO faults, the stale candidate being the right bytes for the
  WRONG round.  Both were measured when the arm landed and both
  belong in the clause's own wording, not only in a run record.

S8-S11 are the fidelity correspondence's yield (section 12,
2026-08-15): places the spec or header underdetermine a decision the
machine takes.  In every case the reference's spec-derived reading
and the machine were measured to AGREE, so each is a sentence owed,
not a defect.

- S8.  systemLaunch with an instance LIVE answers nothing -- derived
  from the Model's one-instance sentence and R2b, exercised tens of
  millions of times, but stated by neither systemLaunch's doc nor
  the induction's LAUNCH case.
- S9.  The out-of-range `from` refusal is documented at
  systemPossessed and systemAssembled and holds at systemReceived
  and systemWitness only through the induction's blanket refusal
  clause; the two headers' own docs do not say it.
- S10.  The cursor walk RUNS when the presented round is RELEASED
  (retained rounds after it still birth want); the spec states this
  only impliedly (the walk's own clause has no retention condition
  on the presented round, and its stated inertness is at-or-after
  the frontier).
- S11.  A serve sweep that finds nothing owed leaves the cursor
  unchanged; "the NAME of the round last served" does not say what
  an empty sweep does to it.

## Discrepancy found while assembling this register

`test/machineMutants.sh` recorded MM_I8_EARLY's expected contract-suite
oracle as "test_system E/G", while `test_system_invariant.c` and
`test_system.c` both record the MEASURED sections as C and D.  The two
are disjoint, so one record was stale.  RESOLVED at verification: the
script's line was the build brief's PREDICTION, never a measurement --
the tier's own summary artifact reads "sections: C x1; D x1" -- so the
script header now carries the measured pair with the prediction noted.
This register carries the measured pair (C x1, D x1).
