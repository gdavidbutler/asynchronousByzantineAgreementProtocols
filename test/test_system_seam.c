/*
 * test_system_seam.c
 *
 * Composed-seam instrument: the glue between a real bkr94acs
 * instance per round and one struct system per process (the
 * MACHINE below -- one process's seat in the system, per the
 * system.md Model term pin).
 *
 * The layers below are each validated standing alone -- bkr94acs by
 * its white-box and contract suites, system by its contract suite
 * and by the reachable-state falsifier.  What neither reaches is the
 * SEAM: the caller code that decides, for each arriving act, whether
 * it belongs to the live instance (the DELIVER route), to a retained
 * round (the SERVE / possession route), or to a round beyond chain
 * reach (held until reach extends).  The unique target is the
 * post-COMPLETE continuation tail: a round's BPR retries do not stop
 * when the round closes, so every completed round keeps producing
 * traffic that must classify as retained-round traffic and never as
 * delivery.
 *
 * This is a FALSIFIER, not a gate.  A clean run is evidence the glue
 * shape is worth deploying; it discharges nothing.  It is deliberately
 * out of `make check`.
 *
 * NO CRYPTO.  The harness carries a sender field on every wire and
 * the receiver trusts it: that field IS the ingress-attribution
 * property by construction, so nothing here tests it.  The round
 * composition is a four-byte anchor folded over the previous round's
 * anchor and the agreed subset -- the linkage shape (a round's name
 * depends on its predecessor's), with a non-cryptographic fold.  It
 * exists so that two rounds are distinguishable by content, which is
 * what makes a round-blind witness match observable.
 *
 * Straight C: no threads, no channels, no allocation beyond the
 * per-round ACS instances, the recovery legs, and the exchange instances
 * (all below), which the glue frees on release or quiescence.
 *
 * THREE CARRIER PLANES, one per deployment geometry (system.md "Relation
 * to a deployment"): the round instance (ACS at (n,t), the value plane),
 * the recovery legs (Fig1 2/0, SERVE discharge -- below), and the EXCHANGE
 * (Fig1 n/t, the O2 content grain -- below).  Every plane's traffic is
 * traffic OF ITS ROUND and rides the SAME seeded shuffle + loss machinery;
 * behind a receiver's frontier it classifies as retained-round traffic.
 *
 * SERVE DISCHARGE RIDES REAL Fig1 LEGS.  A SERVE act does not send a
 * bare assertion message; it BIRTHS a recovery leg -- a two-process
 * Bracha87 Fig1 (this server the initiator, n encoded 1, t = 0) whose
 * INITIAL carries the very bytes the bare message used to (the round's
 * composition), so the witness path upstream is byte-identical.  The
 * leg is the deployment's carrier: it inherits BPR (retry retires only
 * on the other end's ACCEPT -- remote evidence -- or on the round's release,
 * never on local send progress) and rides the SAME seeded shuffle and
 * loss machinery as every other wire (WK_SERVE).  A leg's messages are
 * traffic OF ITS ROUND: at the server they sit behind its frontier and
 * classify as retained-round traffic (the server's self-fed INITIAL,
 * which is what makes the server ECHO -- the load-bearing behind-frontier
 * consumption); at the served process the leg's round IS the frontier,
 * where its ACCEPT delivers the composition into the witness path.  The
 * leg retires on quiescence (two-process all-accepted) or when its
 * round releases; the server births one per still-owed process, the
 * served process births its end on the first arriving message.
 *
 * THE O2 CONTENT GRAIN RIDES REAL Fig1 n/t EXCHANGES.  Per round R, per
 * in-subset member m, one exchange (Fig1 at (n,t), initiator = m) carries
 * m's content -- a token, not real bytes (the seam tests the grain in
 * motion, its classification and lifecycle, not the crypto).  The value
 * plane (the common token) spans all n; a PER-DESTINATION sidecar (token
 * XOR'd per destination, so misdelivery is detectable) rides the
 * initiator's INITIAL alone, single-sourced.  Content distribution is a
 * CONTINUATION of Phase A: the member births its exchange as it LAUNCHES
 * the round (WK_EXCH, birthed in launch, not close), so the grain is in
 * flight alongside the ACS and largely in hand before the round can
 * release.  A process delivers m's content when it ACCEPTs the value
 * plane AND holds its sidecar (the presence gate) -- BEFORE it closed R
 * the delivery rides the have bitmap systemComplete carries; AFTER, the
 * late-assembly ingress systemAssembled (which the seam finally
 * exercises).  SERVE acts' .have is honored from this real accrued state.
 *
 * THE INITIAL RETIRES AT ALL-ECHOED, NOT ACCEPTED (load-bearing).  The
 * standard Fig1 BPR retires INITIAL at the initiator's own ACCEPT -- sound
 * for a bare broadcast (INITIAL only induces echoes), but WRONG here: the
 * INITIAL carries the sidecar, so retiring it at accept strands any
 * process that has not yet received its per-destination content.  The
 * exchange drives INITIAL by bracha87Fig1AllEchoed instead (per-process
 * skip = echoed processes), a persistent per-process carrier unlike the
 * pair-accept legs.  The exchange is freed on all-accepted quiescence (or
 * teardown), NEVER on round release -- content may assemble past release,
 * the O2 out-of-band tail; ground truth then still holds it, but the
 * machine cannot be told (no have grain for a released round).
 *
 * ONCE-ALWAYS-OPEN, NOW GATED.  The deployment gates ECHO on VALIDATING
 * the per-destination payload (crypto); the seam keeps the payload-
 * PRESENCE requirement (a process cannot echo content it does not hold --
 * honest content stays complete).  The VALIDATION was formerly always-
 * open (crypto-invisible, like M_SEAM_NOHOLD); the invalid-injector below
 * closes it -- the exchange sidecar gate now consults the verdict flag and
 * drops a corrupted token.
 *
 * THE INVALID INJECTOR (system.md: crypto at this layer is verdict-shaped
 * -- the machine never sees a failed verdict).  Its one-sentence principle:
 * INVALID REDUCES TO LOSS -- a cryptographically-invalid item must be
 * indistinguishable from absence, so each verdict gate drops a flagged
 * item before any state touch (a counter at most), and the ORACLE proves
 * the machine and glue artifacts are byte-identical with and without an
 * adversary the gates reject.  Every (seed, scenario) runs TWICE --
 * injector off, injector on -- from the SAME schedule RNG; the injector
 * draws only its OWN stream and its gated injections push no wire and
 * touch no state, so the legitimate schedule is bit-identical across the
 * pair (verified by the plumbed-rate-0 sanity: rate 0 is oracle-silent).
 * The oracle hashes, per tick: (a) every struct system's bytes, (b) the
 * GLUE-SIDE per-round artifacts -- the machine holds composition/content
 * only as bitmaps, so machine bytes alone are blind to a false adoption
 * or corrupted content (a bit is a bit); the oracle MUST cover the glue's
 * adopted/completed composition, standing candidate, and content tokens
 * -- and (c) frontier + classification.  Injection counters are excluded
 * by construction.  First diverging tick reported.  Six gates, one red
 * each (-DM_INJ_<site>, disabling exactly that gate); each red's flagged
 * item is taken as valid and diverges the on-run:
 *
 *   M_INJ_ATTRIB     A9: a forged-sender act is recorded.
 *   M_INJ_WITNESS    O3: an invalid served assertion counts as a witness.
 *   M_INJ_CANDIDATE  O1/A10: a non-folding garbage composition is adopted
 *                    as the standing candidate (must never -- bounded work,
 *                    zero state; the red proves the gate is load-bearing).
 *   M_INJ_LEG        a forged-initiator leg's witness is delivered.
 *   M_INJ_EXCH       a corrupted/wrong-destination sidecar delivers wrong
 *                    content (the once-always-open validation gate).
 *   M_INJ_POSSESS    a forged possession indication is recorded.
 *
 * Every gate asserts its per-site injection count > 0 per run (no vacuous
 * coverage).  A red that did NOT fire would be a finding -- either a
 * genuinely absorbing site (document like NOHOLD) or an oracle blind spot
 * (fix the oracle, never the red); none is absorbing here.
 *
 * Two possession sources are wired, and both are load-bearing.  The
 * INDICATION rides a round's own traffic once this process holds that
 * round's composition (system.h's analog of the ACCEPTED bit riding a
 * READY) -- the tails of a closed round are what carry it.  The
 * INFERENCE reads an act of round R+1 OR LATER as evidence of its
 * sender's possession of R.  Wiring the inference as R+1 alone
 * strands any round whose immediate successor never arrives.  Omitting
 * the indication deadlocks a cluster outright in the quiescent limit:
 * receiving a serve does mark its server, but serves are born from
 * want evidence carried by the tails, and BPR retires those at
 * all-accepted quiescence -- after which no traffic for the round
 * exists at all and every possession record freezes below n-t.
 *
 * Configuration: n=4, t=1, a reach of 3 retained rounds, twelve rounds
 * by default, and every shape constant is -D overridable so one source
 * drives the three-point config sweep (2026-07-25, below).  Scenarios
 * per seed -- which ones a configuration runs is decided at compile
 * time by its own fault budget (SWEEP_LAGGARD / SWEEP_STARVE /
 * SWEEP_BYZ):
 *
 *   PLAIN    every process delivers; loss drives BPR retries, which
 *            are the tails.
 *   LAGGARD  one process never receives another process's traffic for
 *            one round.  Its own instance for that round cannot close,
 *            so it can only close by adoption from served evidence --
 *            the whole heal path, end to end.
 *   STARVE   LAGGARD plus an indication cut on an earlier round: the
 *            accepted-strand positive control (below).
 *   BYZ-*    a LIVE Byzantine participant, one scenario per lie
 *            (2026-07-25, below).
 *
 * THE POSTURE (system.md "The three states").  PARTITIONED is the
 * DEFAULT state; a process is a PARTICIPANT only while it can prove so
 * from self-local progress evidence.  The glue meters that proof: each
 * tick with PROGRESS (a fresh act cascade, a frontier advance from
 * completion or adoption, possession-record growth, or a release) resets
 * a per-process barren-sweep count; SP consecutive barren sweeps
 * classify the process PARTITIONED -- the deployment's abandonment policy
 * acting on a lapse that already holds, the budget its meter.  A
 * classified process KEEPS STEPPING (the posture is a retrying posture);
 * the run does not wait on it.  A glue with no such meter models a
 * NON-CONFORMING deployment -- that gap is what the seed-11 strand once
 * exposed (see the RESOLVED note below).
 *
 * THE GROUND-TRUTH ARM.  A deployment cannot tell a genuine strand (a
 * process that HOLDS its held-on round and lacks only others' expired
 * evidence) from a starved heal (a process that LACKS the round) -- but
 * this instrument can, and must, else the posture would absorb the
 * starvation reds.  A classification is ACCEPTED only when the process's
 * duty is HELD and it POSSESSES the round that duty is held on
 * (frontier - 1); an accepted strand is excluded from the every-process
 * / releases-everywhere quantifiers, a rejected one is not.
 *
 * Checks (asserted after each run; see main() for the authoritative
 * list):
 *   P  POSTURE          every classification is an accepted strand (zero
 *                       in a run with no strand), and at most t classify.
 *   A  TAILS CLASSIFY   no DELIVER is answered for a round behind the
 *                       frontier, and such traffic actually occurred.
 *                       Two further arms are MUTANT TRIPWIRES, not
 *                       checks -- see the note in assertRun.
 *   B  LAGGARD HEAL     the held process closes by adoption, its later
 *                       traffic returns possession evidence, and beyond-
 *                       reach traffic is held and re-fed.  Run-level, so
 *                       never excluded by an accepted strand.
 *   C  NO MISREAD       every round with a successor closes at every
 *                       NON-STRAND process; all-n releases occurred.
 *   D  R4               no advance outruns n-t processes having
 *                       actually closed the prior round (ground truth,
 *                       independent of the signal the launch gated on).
 *   E  SEQUENCE         every process's composition for a round is
 *                       byte-identical.
 *   F  RELEASE SAFETY   an all-n release only ever fires for a round
 *                       every correct process has closed (strict); no
 *                       eviction / wrap release UNLESS a strand forces it
 *                       (a round it never possesses cannot all-n release).
 *   G  ROUND BINDING    TRIPWIRE only -- see assertRun.
 *   H  CLOSE ADVANCES   a close refused with an instance live means the
 *                       round argument was rejected (the close speaks
 *                       its round).
 *   I  CONTENT         every NON-STRAND process that closed a round with
 *                       a successor holds every in-subset member's
 *                       content (exchange accept or late assembly);
 *                       and wherever content was delivered WHILE the
 *                       round was retained, the machine was told
 *                       (systemComplete-have / systemAssembled).
 *                       Quantifiers posture-aware exactly like C.
 *
 * A check whose counter moves only inside the mutant block it audits
 * is a TRIPWIRE: it confirms the mutant was compiled in, and would
 * pass against a deployment glue with the same defect and no
 * self-report.  Those are labeled as such rather than counted as
 * validation.
 *
 * Glue mutants (compile with -D<name>; each must fire its check, or
 * the check is decoration).  Kill paths recorded at the bottom of this
 * comment once run.
 *
 *   M_SEAM_DROP      behind-frontier traffic discarded        -> A, B
 *   M_SEAM_DELIVER   behind-frontier traffic fed to the old
 *                    instance instead of the machine          -> A, B
 *   M_SEAM_WANT      possession evidence suppressed -- neither
 *                    the indication nor the inference read    -> C
 *   M_SEAM_OVERCLAIM the opposite error: possession claimed
 *                    for a sender that indicated none         -> F, D
 *   M_SEAM_NOHOLD    beyond-reach traffic discarded instead of
 *                    held and re-fed                          -> B
 *                    CAUTION: BPR retry masks this one -- the
 *                    discarded act is resent.  Weak kill.
 *   M_SEAM_FREE      launch taken without the machine's answer -> D
 *   M_SEAM_UNBOUND   witness counted on content alone, round
 *                    tag ignored                              -> E, C, G
 *   M_SEAM_STALE     close speaks a superseded round           -> H
 *
 * Leg mutants (the SERVE-discharge carrier), same -D<name> tier:
 *
 *   M_LEG_NORETRY    legs never retried after birth -- under
 *                    loss the un-retried handshake never
 *                    completes and the heal starves          -> B/C
 *   M_LEG_LOCALRETIRE leg retired on local send (INITIAL sent
 *                    once) instead of remote accept -- the
 *                    layer's core retire discipline inverted;
 *                    the server's leg goes inert, never
 *                    echoes, the served end starves           -> B
 *   M_LEG_MISCLASS   a leg's behind-frontier traffic (the
 *                    server's own self-fed INITIAL) dropped
 *                    instead of classified as retained-round --
 *                    the server never echoes                  -> B
 *
 * Exchange mutants (the O2 content carrier), same -D<name> tier:
 *
 *   M_EXCH_MISCLASS  a behind-frontier exchange dropped
 *                    instead of classified -- post-close late
 *                    assembly starves                         -> I
 *   M_EXCH_EARLYRETIRE the initiator retires INITIAL before
 *                    all-echoed (the forbidden weak gate) --
 *                    a non-echoed process never gets its
 *                    sidecar and never delivers               -> I
 *   M_EXCH_NOASSEMBLE post-close accepts not fed to
 *                    systemAssembled -- the have grain goes
 *                    stale while content is really in hand    -> I (machine arm)
 *
 * MUTANT RESULTS.  Every mutant fires its designated check; the
 * incidental checks each also brings down are not listed.
 *
 *   M_SEAM_DROP      B, all three arms -- no serve is ever born, so the
 *                    held process never adopts.  (A's drop arm is a
 *                    tripwire and does not count.)
 *   M_SEAM_DELIVER   B, all three arms, same starvation.
 *   M_SEAM_WANT      C -- no round closes everywhere, nothing releases.
 *   M_SEAM_OVERCLAIM F (an all-n release for a round a process had not
 *                    closed) AND D's ground-truth arm -- over-claimed
 *                    possession lets an advance outrun n-t real
 *                    closes.  Note this is not a launch mutant, which
 *                    is what makes D's ground-truth arm's teeth plain.
 *   M_SEAM_NOHOLD    killed ONLY by the counters that see the discard
 *                    directly -- the held-consumption arm of B and the
 *                    overflow counter.  No behavioral check falls.
 *                    Convergence is UNAFFECTED at every seed and loss
 *                    level tried: BPR resends what the glue discarded,
 *                    so the hold discipline is not load-bearing for
 *                    liveness here.  It is load-bearing for the Model's
 *                    hold-unverified rule, which this instrument cannot
 *                    see without crypto.  WAS A WEAK KILL; the STARVE
 *                    scenario gave it teeth (16 failures at 4 seeds),
 *                    because a permanently held process accumulates
 *                    beyond-reach traffic that the discard then loses.
 *                    THE TEETH ARE FIRING RATE, NOT GRADE (line-by-line
 *                    read, 2026-08-14): measured at 16 seeds the arm
 *                    reds 162 times and STARVE contributes 18 of them,
 *                    but every one is still one of the two counters
 *                    this entry opens by naming -- 144 overflow, 18
 *                    held-consumption, no posture arm and no behavioral
 *                    arm anywhere.  A scenario that makes a tripwire
 *                    fire oftener has not made it a property.  The
 *                    entry's first sentence is the status; read the
 *                    upgrade as a count.
 *   M_SEAM_NOPEND    P (a classification that is NOT an accepted
 *                    strand), C's every-process quantifier, F and the
 *                    hold-overflow counter -- 11 failures at LAGGARD
 *                    seed 5, the location the live defect used to
 *                    occupy.  It reinstates the discarded premature
 *                    indication, and the ground-truth ledger is what
 *                    makes it lethal: the evidence ARRIVED, so the
 *                    deficit is a glue defect and the classification is
 *                    REJECTED rather than excused.  SEED-DEPENDENT --
 *                    0 at 4 seeds, fires by 16; and STARVE cannot catch
 *                    it, because there the indications never arrive.
 *   M_SEAM_FREE      ITS OWN TRIPWIRE, and NEITHER D ARM (corrected by
 *                    the line-by-line read, 2026-08-14).  This entry
 *                    used to read "D's machine-consistency arm at every
 *                    seed", and the log agreed -- because the mutant
 *                    incremented the very counter that arm asserts on.
 *                    Two write sites fed one counter, one of them the
 *                    mutation's, so a self-report was indistinguishable
 *                    from D catching a machine/glue disagreement.  The
 *                    mutant's site now has a counter and a check of its
 *                    own ("tripwire: the glue launched without the
 *                    machine's answer", compiled only under this
 *                    mutant, so no baseline moves), and the honest
 *                    picture is what was always true underneath: 144
 *                    firings of the self-report, both D arms silent.
 *                    It does NOT trip the ground-truth arm, correctly:
 *                    with n-t processes having really closed the prior
 *                    round the advance is permitted by R4 -- what the
 *                    mutant bypasses is the tolerance BUDGET, which
 *                    only the signal sees.  The two D arms catch
 *                    different things; keep both, and keep this mutant
 *                    for what it is.
 *   M_SEAM_UNBOUND   E and C.  Needs seeds: 3 firings of E in 32 runs
 *                    (16 seeds x 2 scenarios), none at 2 seeds.  A
 *                    stale round's composition is adopted as the
 *                    frontier round's and the sequence diverges.
 *   M_SEAM_STALE     H (close refused, frontier unchanged) at every
 *                    seed.
 *
 *   M_LEG_MISCLASS   B (adopts == 0) AND P at every seed, 0% included:
 *                    the dropped self-fed INITIAL is a LOGIC drop, so the
 *                    server never echoes and the held process never
 *                    adopts.  The starved laggard classifies but its duty
 *                    is MET (round-2 evidence is untouched -- a leg carries
 *                    round 3), so the ground-truth arm REJECTS it and P
 *                    plus C fire.  The reliable kill of the retained-round
 *                    classification.
 *   M_LEG_LOCALRETIRE B (adopts == 0) AND P at every seed, 0% included,
 *                    same shape: retiring on the local send leaves the
 *                    server's leg inert (it drops its own self-fed INITIAL
 *                    as a retired leg), so the server never echoes.  This
 *                    encodes the layer's core retire discipline -- never on
 *                    local progress -- a logic kill independent of loss.
 *   M_LEG_NORETRY    P and C UNDER LOSS.  A real 2/0 Fig1 leg completes in
 *                    one pass at zero loss, so with no retry the defect is
 *                    invisible at 0% and bites only the fraction of legs
 *                    that lose a handshake message -- ~2 of 16 laggard
 *                    seeds at the default 4%, reliably at higher loss.
 *                    Those laggards strand with duty MET (the round-2
 *                    carrier is fine -- only the leg retry is broken), so
 *                    the ground-truth arm rejects the classification and P
 *                    plus C fire.  A loss-probabilistic red, like
 *                    M_SEAM_UNBOUND is seed-count-dependent (RNG stream
 *                    shifted by the exchange plane: now reliable at 12%+
 *                    loss, where the baseline still heals).
 *   M_EXCH_EARLYRETIRE I and quiescence UNDER LOSS: a process that loses
 *                    its one INITIAL never gets its sidecar (the initiator
 *                    retired at first echo), never delivers, and the
 *                    content-coupled convergence stalls.
 *   M_EXCH_MISCLASS  I and quiescence.  Needs a healing-laggard seed: the
 *                    laggard adopts its round then needs its content by
 *                    LATE ASSEMBLY (behind-frontier), which the mutant
 *                    drops -- so post-close content is where the miss
 *                    lives, and a run whose content all landed pre-close
 *                    never exercises it (0 at 4 seeds, fires by 16).
 *   M_EXCH_NOASSEMBLE I's MACHINE ARM.  Ground truth still holds the
 *                    content (the accept delivered it), but the skipped
 *                    systemAssembled leaves the machine's have grain
 *                    stale for content delivered while retained -- a
 *                    delivered-but-untold mismatch.  Same healing-laggard
 *                    seed dependence.
 *
 * INVENTORY, 2026-07-24: TWENTY-ONE classes, of which TWENTY fire.
 * M_SEAM_NOPEND is new (it injects the premature-indication discard
 * described under THE LAGGARD STRANDS, above).  M_SEAM_NOHOLD is no
 * longer the lone weak kill -- STARVE gave it teeth.  The one class
 * that now fires NOWHERE is M_EXCH_NOASSEMBLE: it needs content to
 * land post-close while its round is still retained, and removing the
 * strand compressed exactly that opening (verified 0 at 24 seeds and at
 * 0% and 15% loss; 25% loss is outside the instrument's envelope --
 * the CLEAN build fails there too, on rounds that never close).
 * DORMANT, RECORDED AS SUCH: it is a real red with no live case, and
 * restoring one needs a scenario with a slow-healing laggard rather
 * than a permanently held one.
 *
 * POSTURE RE-VERIFICATION (the acceptance gate for the ground-truth arm).
 * Mutant counts here and below tally FIRING reds only: the pre-STARVE
 * counts below excluded NOHOLD, so "thirteen" is the fourteen
 * pre-injector classes less NOHOLD.
 * All THIRTEEN mutants re-run under the posture and the content grain;
 * NONE is absorbed.  Two mechanisms keep them lethal: the run-level
 * checks (B's serve/hold arms, releasesAllN, E, H, D) are never excluded
 * by a classification; and under every starvation red the stranded
 * laggard either LACKS its held-on round or has duty MET, so the
 * ground-truth arm rejects the classification (P fires) and leaves it in
 * the C quantifier (C fires).  Reds that stall many processes also trip
 * "at most t classify".  B's adoption arm is itself posture-gated (a
 * genuine accepted strand never heals), so it asserts only when no strand
 * was accepted -- exactly when the reds leave the laggard rejected.  A red
 * the posture or the content grain silently absorbed would be a failed
 * design; none does.
 *
 * INJECTOR REDS (the oracle's kills; first-divergence tick at PLAIN
 * seed 1, default loss, INJ_RATE 40).  Each disables exactly its verdict
 * gate; the injector-on run then diverges from the injector-off run and
 * the state-equivalence oracle fires.  None is absorbed.
 *
 *   M_INJ_ATTRIB     oracle, tick 15 -- a forged-sender act's want/receive
 *                    record moves a possession bitmap.
 *   M_INJ_WITNESS    oracle, tick 0 -- a forged witness moves the frontier
 *                    round's witness bitmap.
 *   M_INJ_CANDIDATE  oracle, tick 2 -- the garbage candidate (and the
 *                    witness reset it forces) move the candidate bytes and
 *                    the witness bitmap.
 *   M_INJ_LEG        oracle, tick 1 -- a forged leg's delivered witness
 *                    moves the witness bitmap.
 *   M_INJ_EXCH       oracle, tick 0 -- a corrupted sidecar writes a wrong
 *                    content token (also brings down check I wholesale).
 *   M_INJ_POSSESS    oracle, tick 14 -- a forged possession indication
 *                    moves a possession bitmap.
 *
 * The existing THIRTEEN mutants were spot-re-run WITH the injector plumbed
 * (verdict gates intact): each still fires its designated check and the
 * oracle stays SILENT -- the injector's gated items touch nothing, so the
 * mutant affects the off and on runs identically.  The new gates did not
 * change a single existing kill.
 *
 * E BECAME FALSIFIABLE ONLY AFTER THE ADOPT PATH WAS CORRECTED.  While
 * the glue closed an adoption without JOINing first, every
 * wrong-composition close M_SEAM_UNBOUND attempted hit the machine's
 * inert-without-live path and was silently lost, so E read green for
 * the wrong reason and the mutant appeared to be caught by H.  A
 * latent defect in the glue was masking a real red.
 *
 * THE STRAND, AND ITS RESOLUTION VIA THE POSTURE (RESOLVED 2026-07-20,
 * default-PARTITIONED ruling).  At a minority of seeds the LAGGARD
 * scenario ends with the held process permanently short of the advance
 * gate.  Signature, read from the machine:
 *
 *   p3 frontier 3, duty HELD, retained r2[pos 2 3  want 0 1]
 *   p0,p1,p2 frontier 12
 *
 * p3 HOLDS round 2 -- it is not missing the round.  What it is missing is
 * EVIDENCE that p0 and p1 possess round 2, which the R4 advance gate
 * consumes.  Both carriers have expired: the indication rides round 2's
 * tails, which the layer below retires at ACS quiescence; the inference
 * rides later-round traffic, all ahead of p3's frontier and held
 * unverified.  p3 cannot launch round 3, outputs no round-3 traffic, and
 * the serve/adopt heal -- which heals a round a process LACKS -- cannot
 * heal a process starved of evidence about a round it HOLDS.  This is
 * exactly the SYSTEM_DUTY_HELD two-carrier expiry system.md documents.
 *
 * The real Fig1 carrier first relocated this strand into the default
 * 16-seed sweep (LAGGARD seed 11 at 4%): the legs' WK_SERVE wires
 * consume the shared loss/shuffle RNG, diverging the stream so seed 11
 * lands on the pre-existing edge.  EXPOSED, NOT CAUSED -- the deadlock is
 * in the machine (round-2 HELD gates the adoption JOIN) plus the
 * unchanged ACS glue; T_p in [200,800] and w in [3,7] have zero effect,
 * because the limiting carrier is ACS-tail quiescence, which no system-
 * layer knob extends.
 *
 * The architect's ruling: a glue with no abandonment read models a
 * NON-CONFORMING deployment.  system.md "The three states" makes
 * PARTITIONED the default; a real deployment ALWAYS has the abandonment
 * policy, whose barren-sweep budget IS the proof-of-participation meter.
 * The instrument now carries that meter (THE POSTURE, above).  p3 runs
 * SP barren sweeps and classifies PARTITIONED; the ground-truth arm
 * ACCEPTS the classification (p3 HOLDS round 2, the round its duty is
 * held on) and excludes p3 from the every-process quantifiers; the run
 * terminates when the participants (p0,p1,p2, who climb to frontier 12
 * one rung per T_p on the tolerance escape) quiesce.  Seed 11 now passes
 * as a classified strand, not a stall.  The 11 arms that once failed --
 * quiescence, hold overflow, eight C, one F -- are precisely the every-
 * process / releases-everywhere quantifiers, now posture-aware.  No
 * check was weakened: the ground-truth arm makes the exclusion legible
 * only for a genuine strand, and the thirteen mutant reds confirm it (POSTURE
 * RE-VERIFICATION, above).  The carrier-expiry claims are UNCHANGED -- a
 * participant already holds its rounds, births no wants, and so no legs;
 * the legs neither reach nor rescue a strand.
 *
 * THE LAGGARD STRANDS WERE A GLUE DEFECT, NOT A PROPERTY (corrected
 * 2026-07-24, and the correction is the whole reason the STARVE
 * scenario exists).  Everything above about the SHAPE of a strand
 * stands.  What was wrong was the CLAIM that the LAGGARD sweeps
 * exhibited one.  This glue used to discard a possession indication
 * that arrived while its round was still live: system.h drops such an
 * indication because only retained rounds have a record, and the O1
 * inference recovers it only where later-round traffic comes to
 * exist -- which, once the cohort has nothing further to contribute,
 * it never does.  The evidence had ARRIVED and was thrown away, so
 * the deficit was manufactured here, not suffered.  Holding those
 * indications and re-presenting them at the close (deliverWire and
 * sysClose) removes the strand outright: LAGGARD seed 5 goes from
 * 6883 ticks / 40296 tails / 11 releases to 159 / 1663 / 48, and the
 * default and 0% sweeps now classify NOWHERE.  The check count ROSE
 * by ~150, because an accepted strand had been excusing its process
 * from the every-process quantifiers.
 *
 * The historical locations -- seed 11, then LAGGARD seed 5 default /
 * seed 1 at 0% -- are kept as the record of a falsified property, not
 * as expected behavior.  An earlier attempt to reach the same state
 * by re-feeding held wires produced byte-identical output to the
 * defect baseline, which is what identified the discarded indication
 * as the sole carrier.
 *
 * THE ACCEPT PATH STILL NEEDS A LIVE CASE, hence STARVE (third
 * scenario): the LAGGARD cut kills the O1 carrier for round 2 while
 * indStarve drops that round's indications to the victim in transit,
 * so it HOLDS round 2 and can never learn anyone else does -- a
 * genuine two-carrier expiry under correct glue.  It classifies at 15
 * of 16 seeds in both sweeps, and its indications never ARRIVE, which
 * is what distinguishes it from the defect above and is exactly what
 * the ground-truth arm now tests.  Without it the strand-shape
 * apparatus would defend a case no run reaches.
 *
 * ------------------------------------------------------------------
 * 2026-07-25: A LIVE BYZANTINE PARTICIPANT, AND A CONFIG SWEEP.
 * ------------------------------------------------------------------
 *
 * Everything above this line ran with n = 4, t = 1 and n honest
 * processes.  The fault budget R4 reserves was therefore never SPENT by
 * anything -- t was a number in a threshold, not an adversary in the
 * run.  Two arms close that.
 *
 * THE BYZANTINE ARM.  One process (byzProc, -1 = none) lies on EGRESS.
 * Every lie stays WITHIN AUTHENTICATION: the harness 'from' field IS
 * ingress attribution (A9 by construction), so the liar can only ever
 * speak as itself -- it can say anything, about anything, but never AS
 * anyone.  Every lie is a deterministic function of (mode, destination,
 * round) and draws NO RNG, which is what keeps the injector-off and
 * injector-on runs of a Byzantine scenario schedule-identical and the
 * state-equivalence oracle valid.  With no liar configured not one of
 * the branches is taken and no stream is touched: the PLAIN, LAGGARD and
 * STARVE lines are BYTE-IDENTICAL to the pre-arm baseline (verified).
 * Six modes, one scenario each, byzProc = 1, no laggard beside it (a
 * liar and a cut process are TWO faults, and t = 1 buys one):
 *
 *   BYZ_FORGE_POSSESS    possesses = 1 on every egress act regardless
 *                        of what it holds -- the lying-SENDER analog of
 *                        the receiver-side M_SEAM_OVERCLAIM.
 *   BYZ_WITHHOLD         possesses never set.  Read as the one behavior
 *                        it is: the bit is what makes a tail
 *                        possession-bearing, so refusing the bit IS
 *                        refusing the tail.  The O1 inference is left
 *                        alone, and carrying the cohort is its job.
 *   BYZ_MIXED_CANDIDATE  as a recovery-leg server, the true composition
 *                        to odd wanters and a byte-flipped variant to
 *                        even ones, for the SAME round.  Each wanter
 *                        has its own leg instance, so the two servings
 *                        are simultaneous, not a sequence.
 *   BYZ_EQUIVOCATE_VALUE a different A-Cast value by destination
 *                        parity -- what must contain it is Bracha
 *                        Lemma 2, inherited by the A-Cast leg.
 *   BYZ_SILENT           present, stepping, and emitting nothing to
 *                        anyone (self-feed only, which in a deployment
 *                        is not a wire at all).
 *   BYZ_WRONG_CONTENT    half its exchange sidecars mis-tagged.  The
 *                        per-destination XOR tag is what makes that
 *                        detectable, so the seam gained the matching
 *                        INGRESS GATE -- a sidecar that does not untag
 *                        to the value plane's own token is dropped
 *                        whole, INVALID REDUCES TO LOSS like every
 *                        other verdict gate.  Honest traffic cannot
 *                        trip it, so no existing run moved.
 *
 * THE CHECK SIDE IS THE LOAD-BEARING HALF.  A Byzantine process's own
 * state proves NOTHING, so it is excluded from every per-process
 * quantifier (BYZSELF): its closes enter neither the reference
 * composition nor the closed-everywhere ledger, its posture is never a
 * classification, the run never waits on its frontier or on a want owed
 * to it, and its own content is the per-member out-of-band hole O2
 * prices.  The exclusions sit BESIDE the accepted-strand gating, never
 * replacing it.  Two thresholds move with the liar, each for a stated
 * reason and neither by weakening what it once said:
 *
 *   NCORRECT   an all-n release must have every CORRECT process's
 *              close behind it.  A forgery reaches only its own bit
 *              (A9), so the other n-1 bits are true -- L5's strict arm
 *              keeps exactly its old teeth against a forging sender.
 *   SHEDFLOOR  R4's ground-truth arm reads n-t with no liar and n-t-1
 *              with one: A9 confines a forgery to its own sender, so
 *              forged bits are bounded by the liars PRESENT (this
 *              harness fields exactly one), not by the budget t.  The
 *              first cut of this floor subtracted t and surrendered a
 *              process's worth of the check's teeth at t >= 2;
 *              tightened 2026-07-25 at the Fable verification pass.
 *
 * EXPECTED OUTCOMES, one per mode, each a claim system.md already makes
 * restated where a live liar can falsify it.  All six hold -- MIXED
 * only after the C6 glue fix THE FINDING below records:
 *
 *   FORGE      containment.  No correct process is stranded, every
 *              correct process runs the whole round space, and L5's
 *              strict arm (releaseUnsafe) stays at zero -- a forged bit
 *              is the liar's OWN to give, so a release may fire one bit
 *              ahead of the truth but never ahead of the correct
 *              cohort.  GREEN at every seed and every config.
 *   WITHHOLD   the cohort closes every round and reaches the last one;
 *              the R4 hold costs the tolerance budget and nothing more
 *              (L1's corollary).  The O1 inference is what pays it:
 *              the withheld indication costs ~57 ticks, where SILENT --
 *              which kills the inference too -- costs ~10000, one T_p
 *              per rung.  That contrast IS L1's corollary measured.
 *              GREEN.
 *   MIXED      no correct process ever closes on a fabrication and E
 *              holds across correct processes.  GREEN -- but only
 *              because the glue voids its adopt debt on a candidate
 *              switch; the first cut did not, and THE FINDING below is
 *              what the arm caught.
 *   EQUIVOCATE sequence identity holds across correct processes; the
 *              equivocator's slot resolves to one value or to none.
 *              GREEN.
 *   SILENT     the cohort advances under tolerance and the mute process
 *              is in NO round's subset.  Two arms are scoped for it,
 *              both on the same premise: a process that emits nothing
 *              never has its possession evidenced by anyone, so all-n
 *              possession is UNREACHABLE and eviction is the only
 *              release path left -- system.md's own eviction exception,
 *              the same carve-out shape the accepted strand already
 *              carries.  GREEN.
 *   CONTENT    the mis-tagged sidecars are caught and dropped as loss,
 *              and the hole they leave is per-member, never the round's
 *              failure.  GREEN.
 *
 * THE FINDING -- THE ADOPT DEBT AND THE STANDING CANDIDATE ARE TWO
 * PIECES OF STATE, AND THE GLUE CAPTURES ONLY ONE.  Minimal reproducer:
 * `./test_system_seam 1 4`, BYZ-MIXED seed 1, default config.  Verbatim:
 *
 *   FAIL [BYZ-MIXED seed 1]: E: compositions byte-identical across
 *        processes
 *   FAIL [BYZ-MIXED seed 1]: BYZ MIXED: no correct process closed on
 *        the fabricated variant
 *
 * Traced, not inferred (instrumented throwaway build):
 *
 *   adopt          p2 frontier 7 cand ce 82 69 fe 01 01 01 01
 *   close-by-adopt p2 frontier 7 cand 94 82 69 fe 01 01 01 01
 *
 * and 0xce ^ 0x5a = 0x94, the fabrication exactly.  The ADOPT fired on
 * the TRUE candidate with its t+1 correct witnesses; between that output
 * and the close that consumes it, a Byzantine leg for the same frontier
 * round arrived, the candidate switched (correctly -- the book re-arms
 * on a switch), and the DEFERRED close read p->cand as it then stood.
 * The poisoned anchor then folds forward and the whole downstream
 * sequence at that process diverges, which is L6's induction failing at
 * a corrupted base.
 *
 * THE CLASSIFICATION, corrected at the Fable verification pass
 * (2026-07-25, same day): NOT a spec countermodel -- A GLUE DEFECT THE
 * SPEC ALREADY FORBIDS.  The build report read L2's caller half as
 * covering only the COUNTING discipline and called the debt span
 * uncovered; system.md's Mechanization-status seam pin (C6) in fact
 * pins exactly this: the caller "treats a reset as voiding any
 * unconsumed ADOPT".  The first-cut glue re-armed the book on the
 * switch and kept the debt alive -- obeying the counting clause,
 * violating the void clause.  FIX (legAccept): the switch now clears
 * adoptPending beside the reset; the true candidate re-accumulates
 * from honest servers and the heal completes on it (all three configs
 * green, the t=2 seed-11 case included).  A fabrication can never
 * re-fire the latch on its own account -- t liars reach at most t.
 *
 * WHAT THE FINDING IS, THEN: THE FIRST LIVE COUNTERMODEL BEHIND C6's
 * VOID CLAUSE.  Omit it and a correct process closes on a composition
 * its witnesses never attested, and the poisoned anchor folds forward
 * into the exact L6 divergence system.md prices the clause against.
 * The clause is proven load-bearing, not decorative -- which is the
 * proof-testing charter's currency.  M_SEAM_NOVOID reinstates the
 * omission and is C6's MATCHED RED: 10 failures at 16 seeds, the
 * BYZ-MIXED E + fabrication arms, seeds 1, 4, 12, 14, 16 exactly.
 * It does NOT fire at 0% loss (the opening needs a want, which needs a
 * straggler, which needs loss) and CANNOT fire on an honest schedule
 * (honest servers all serve one composition -- A1, L6 -- so only a
 * live equivocating server puts two candidates in flight for one
 * frontier round).  This red is unreachable by every pre-Byzantine
 * instrument in this repo by construction.
 *
 * THE CONFIG SWEEP (Makefile target seam-configs; ~23s total).  Which
 * scenarios each point runs is the source's own decision, because a
 * fault a configuration has no budget for is not a test there:
 *
 *   n=4 t=1 w=3   PLAIN + LAGGARD + STARVE + all six Byzantine arms.
 *                 42781 checks, 0 failures (10 before the C6 fix, all
 *                 ten THE FINDING).
 *   n=2 t=0       PLAIN only.  2156 checks, 0 failures.  The spec's
 *                 smallest deployment (the N-floor ruling), and the
 *                 point where L1's TOLERANCE half is VACUOUS rather
 *                 than merely slack: a counter asserts the class is
 *                 never READ, and it never is.  The serve floor of ONE
 *                 is then the entire heal capacity, which is asserted
 *                 too and which ordinary loss exercises (7-13 serves a
 *                 run, adoption at 9 of 16 seeds).
 *                 LAGGARD IS OUT OF MODEL HERE, verified rather than
 *                 assumed: with n-t = n the round tolerates ZERO
 *                 missing participation, so a permanent per-round
 *                 ingress cut does not make ONE process lag, it WEDGES
 *                 THE ROUND EVERYWHERE -- the victim never echoes, no
 *                 other A-Cast reaches its threshold either, the step-2
 *                 trigger never sees n-t BAs decided 1, and nobody ever
 *                 holds the round for a heal to serve FROM.  Every
 *                 LAGGARD seed ended with BOTH processes at the cut
 *                 round, duty MET, classified.  STARVE and the
 *                 Byzantine arms are out for the same zero budget.
 *   n=7 t=2 w=3   PLAIN + LAGGARD + FORGE + MIXED + WITHHOLD-with-a-
 *                 laggard, the last being the COMPOSED arm two faults
 *                 inside t finally admit -- a liar and a cut process at
 *                 once, which no instrument here had ever run.  80249
 *                 checks, 0 failures (2 before the C6 fix, both THE
 *                 FINDING at seed 11); the composed arm is GREEN at
 *                 every seed.
 *
 * MUTANT INVENTORY: 22 of 22 FIRE, up from 20.  M_SEAM_NOVOID is new
 * (C6's matched red, above).  M_EXCH_NOASSEMBLE was
 * recorded DORMANT on 2026-07-24 (a real red with no live case, needing
 * content to land post-close while its round is still retained).  The
 * Byzantine arm RESTORED ITS CASE: it fires at BYZ-MIXED seed 16 of the
 * default sweep.  All other twenty fire their designated checks
 * unchanged; the counts below are TOTALS and include the clean
 * baseline's own failures (10 at 16 seeds, 4 at 4 seeds -- THE FINDING).
 * M_SEAM_WANT's kill now shows as quiescence rather than C at 4 seeds,
 * the scope having widened; it is a kill either way.
 *
 * RE-VERIFIED 2026-08-14 (the wider validation read), AND THE READ'S
 * FINDING IS ABOUT THIS PARAGRAPH RATHER THAN ABOUT THE MUTANTS.  The
 * inventory above was measured 2026-07-25 and then sat through THREE
 * landings -- the step-2 and round-turn relocation, the round
 * abstraction, the retention seam -- without being re-run once.  Each
 * of those re-verified the three CONFIG baselines byte-exact and left
 * the arm matrix alone, because a glue mutant's only repro was one
 * hand-built binary.  That is how a true claim goes stale without
 * anything being wrong: nothing could have said so.  `make
 * seam-mutants` (test/seamMutants.sh) exists as of that read -- the
 * matrix is a target, asserting per mutant that its DESIGNATED check
 * fires and recording totals without asserting them, since totals move
 * with any RNG-stream shift.
 *
 * WHAT THE RE-RUN MEASURED: all 22 fire, clean control 42804/0, 16
 * minutes at 16 seeds.  THREE PROFILE DRIFTS, none a lost arm:
 *   M_SEAM_UNBOUND   E fires 45 times.  The "3 firings of E in 32
 *                    runs" above is not contradicted -- it predates
 *                    STARVE and the six Byzantine scenarios, so the
 *                    denominator is 144 runs now, not 32.
 *   M_EXCH_NOASSEMBLE now fires at PLAIN as well as at BYZ-MIXED, so
 *                    the DORMANT record above is doubly closed.
 *   M_EXCH_MISCLASS  reds at PLAIN and nowhere else, where the note
 *                    below says it needs a healing-laggard seed.
 *                    Scenario relocation by RNG stream is this
 *                    instrument's oldest known behaviour (the strand
 *                    has relocated three times); the ARM is the claim
 *                    and the arm holds.
 * Several mutants also cut the run short of 42804 checks (DROP 4828,
 * STALE 12287): a broken glue stalls and the checks after the stall
 * never run, so a shortfall against the clean total is the stall, not
 * an arm gone silent.
 *
 * THE PREMISE MATRIX BELOW WAS RE-RUN IN THE SAME READ, all 18 arms at
 * 16 seeds.  Every control still green, every falsifying arm still red
 * through its designated oracle, eleven arms EXACT.  Seven totals
 * moved and are re-cited in the register:
 *   W_A6_PIN0 12643 -> 12667/0        W_A6_PIN1 16095 -> 16094/0
 *   W_A9_SYBIL 464 -> 362             W_SERVE_CAP0 211 -> 212
 *   W_R2C_SILENT 12999 -> 12608/0     W_I10_WRONGARTIFACT 16 -> 14
 *   W_L2_NOREARM 20 -> 18             W_L2_NOCLOSEVOID 53 -> 111
 * A moved total can hide a moved CLAIM, and here it does not.  Both
 * places this header states a DECOMPOSITION rather than a total, the
 * decomposition held: W_A9_SYBIL's D ground-truth arm is 5 firings,
 * exactly as recorded, with the 102-failure drop entirely in C and F;
 * and W_SERVE_CAP0's 212 decomposes as the recorded B 16+16, C 128,
 * P 16, F structural 16, hold-overflow 16 -- with B's beyond-reach arm
 * at 4 rather than 3 -- and D, E, F's unsafe arm and H silent at every
 * seed, as recorded.  The lesson the read draws is the one the M_*
 * paragraph above draws: state the decomposition, not the total.  A
 * total is a number that moves; a decomposition is the claim.
 *
 * ------------------------------------------------------------------
 * 2026-07-25 (stage 2): THE PREMISE-WITHDRAWAL MATRIX.
 * ------------------------------------------------------------------
 *
 * Everything above tests the GLUE: each M_* red breaks a caller
 * obligation and a check catches it.  What none of them touches is the
 * other side of the ledger -- the PREMISES the proofs consume, which the
 * instrument had until now supplied silently and for free.  A premise
 * supplied for free is a premise never shown to be load-bearing.
 *
 * Each -DW_* arm below withdraws EXACTLY ONE premise (Makefile target
 * seam-premises), one build per arm, the M_* pattern.  Two kinds:
 *
 *   A FALSIFYING arm predicts a named red and must fire it.  Same
 *   currency as an M_* mutant, but the defect is in the ENVIRONMENT the
 *   layer was promised, not in the layer's own caller code.
 *
 *   A CONTROL predicts a NON-failure, or a failure of a stated kind, and
 *   BUILDING IT AND RUNNING IT IS THE RESULT.  A control's exit status
 *   is 0: where it predicts a liveness loss it ASSERTS the loss rather
 *   than reddening on it.  What is never withdrawn, in any arm, for any
 *   scenario, is a SAFETY arm -- D both halves, E, F's unsafe arm, H.
 *   Those staying green under a withdrawn premise is what a control is
 *   FOR; a safety red under a control would be the finding.
 *
 * Each arm runs only the scenarios its withdrawal is about; a scenario a
 * withdrawn premise does not touch is runtime, not evidence.
 *
 *   W_A4_PARTITION  A4 (eventual delivery) withdrawn for ONE process for
 *   CONTROL         the whole run -- every wire to and from it is lost,
 *                   self-feed excepted as for the mute liar.  PLAIN only:
 *                   the partition IS the fault budget.
 *                   RESULT 4048/0 at 16 seeds.  PREDICTION HELD EXACTLY.
 *                   The victim sits at frontier 0, runs its barren budget
 *                   out and classifies PARTITIONED; the correct cohort
 *                   reaches frontier 12 and quiesces; E, F's unsafe arm,
 *                   H, D all silent.  A permanent partition is NOT an L1
 *                   falsification -- L1's heal speaks of a process the
 *                   transport still reaches -- and the arm asserts
 *                   EXACTLY ONE classification, at the victim.  Its
 *                   deficit is POSSESSIONAL, not evidential, so the
 *                   accepted-strand shape correctly does not apply.
 *                   All-n possession is unreachable (a process no wire
 *                   reaches evidences nothing), so eviction is the only
 *                   release path -- system.md's own eviction exception,
 *                   the third instance of the carve-out BYZ_SILENT and
 *                   the accepted strand already carry.
 *
 *   W_A6_PIN0       A6 (self-local gates computed honestly) withdrawn,
 *   CONTROL         pinned SHUT: toleranceElapsed = 0 always, so only MET
 *                   -- all-n possession -- can advance a frontier.
 *                   RESULT 12643/0 at 16 seeds.
 *   W_A6_PIN1       A6 withdrawn, pinned OPEN: toleranceElapsed = 1
 *   CONTROL         always, so the escape never funds the tail it exists
 *                   to fund.  RESULT 16095/0 at 16 seeds.
 *
 *   THE PINS' RESULT, and it corrected the brief.  PLAIN and LAGGARD
 *   ABSORB EITHER PIN OUTRIGHT -- measured, at 16 seeds, both directions.
 *   The reason is one fact: the serve/adopt heal restores ALL-N
 *   possession before the reach rolls off the round being served, so MET
 *   carries every advance and the tolerance escape is never the only
 *   route.  PIN0 was briefed to wedge a LAGGARD run at the cut round; it
 *   does not (110 ticks, converged).  PIN1 was briefed to SHED the
 *   straggler; IT NEVER SHEDS ONE, at any seed, in either scenario.
 *
 *   That is not the pins failing to read the input -- it is the w x T_p
 *   SIZING OBLIGATION system.md states ("w and T_p are not independent")
 *   observed from the other side: at w = 3, T_p = 400, n = 4 the heal
 *   always wins the race against the reach, so R4's tolerance clause is
 *   SLACK for every fault these two scenarios manufacture.  Shedding a
 *   correct straggler needs the reach sized UNDER the heal, which is the
 *   sizing obligation violated, not the gate misread.
 *
 *   THE ONE SHARP SCENARIO IS THE MUTE ARM, and it is why both pins run
 *   BYZ-SILENT (SWEEP_BYZ 3).  A process that emits nothing is never
 *   evidenced by anyone, so all-n possession is UNREACHABLE for every
 *   round and the tolerance escape is the SOLE route past round 0.  There
 *   the pins bracket the escape exactly, and the honest gate sits between:
 *
 *     pinned SHUT  the whole correct cohort wedges at frontier 1 and runs
 *                  its barren budget out -- ~950 ticks to a three-process
 *                  abandonment.  ASSERTED positively (cohort classified,
 *                  no correct process past round 1, the signal withheld),
 *                  because abandoning is the deployment behaving
 *                  CORRECTLY on a gate that lies to it.  L1's TOLERANCE
 *                  half is thereby shown NECESSARY, not decorative: with
 *                  it withdrawn the mute arm's cohort makes no progress
 *                  at all.
 *     honest       ~10000 ticks: one T_p per rung, the tolerance escape
 *                  carrying the frontier.  (Unchanged from stage 1.)
 *     pinned OPEN  ~6000 ticks, and tolUnearned > 0 asserts the pin was
 *                  actually CONSUMED rather than merely compiled in.
 *
 *   Read together those three numbers ARE L1's tolerance half measured:
 *   the escape is what converts an unreachable all-n into bounded
 *   progress, and its BUDGET is what that progress costs.
 *
 *   W_A5_NOINFER    A5 (the O1 linkage inference -- a later-round act
 *   CONTROL         evidences its sender's possession) withdrawn, and
 *                   NOTHING else: A8's other source, the indication
 *                   riding a round's own traffic, is left exactly as it
 *                   was.  PLAIN + LAGGARD.
 *                   RESULT 641/0 at 16 seeds -- and the low check count
 *                   IS the result, because the liveness quantifiers are
 *                   withdrawn on the runs whose predicted loss landed,
 *                   and it landed on nearly all of them.  80 STALL
 *                   dumps: PLAIN stalls outright at MAXTICKS (200000
 *                   ticks) at about half the seeds, and every other run
 *                   ends with an accepted strand.  STRONGER THAN BRIEFED
 *                   (the brief predicted strands; the measurement is
 *                   hard stalls), and the SAFETY arms are silent at every
 *                   seed -- E, F's unsafe arm, H, D never move.
 *                   A5 is therefore load-bearing for LIVENESS ALONE, and
 *                   that is exactly the shape system.h's two-carrier text
 *                   claims: the indication expires with its round's
 *                   traffic, and with the inference gone nothing replaces
 *                   it.  This is the same wall the 2026-07-24 correction
 *                   hit from the other side -- there the glue THREW AWAY
 *                   arriving indications and the inference could not
 *                   cover for them; here the indications land and the
 *                   inference is gone.  Either carrier alone is
 *                   insufficient; the pair is the mechanism.
 *
 *   W_A9_SYBIL      A9 (ingress attribution) withdrawn for the EVIDENCE
 *   FALSIFYING      this layer records: every 4th act is booked against a
 *                   rotated false sender, on the possession path
 *                   (systemReceived / systemPossessed / the indication
 *                   hold) and on the O3 witness path (a leg's ACCEPT
 *                   attributed to a rotated server).  The ACS instance
 *                   still gets the true sender throughout -- this
 *                   withdraws the SYSTEM layer's attribution premise, not
 *                   Bracha's.  PLAIN + LAGGARD + all six Byzantine arms.
 *                   RESULT 464 FAILURES at 16 seeds.  THE PREDICTED RED
 *                   FIRES, and it is L5's:
 *
 *                     F, releaseUnsafe -- an all-n release for a round a
 *                     correct process had not closed.  16/16 seeds at
 *                     PLAIN, 16/16 at BYZ-SILENT, 16/16 at BYZ-FORGE,
 *                     15/16 at LAGGARD, and every other arm besides.
 *                     BYZ-FORGE's own "L5 strict arm silent under a
 *                     forging sender" arm falls 16/16 with it -- which is
 *                     the sharpest statement of the finding, since that
 *                     arm is GREEN at every seed of the stage-1 sweep and
 *                     the ONLY thing that changed is the attribution.
 *                     A9 is exactly what confines a forgery to its own
 *                     sender; withdraw it and the confinement is gone.
 *
 *                     D's ground-truth arm, 5 firings: a mis-attributed
 *                     possession bit lets an advance outrun the processes
 *                     that really closed the prior round.  R4's floor
 *                     rests on A9 through the same door.
 *
 *                   AND THE HALF THAT DOES NOT FALL, recorded because it
 *                   is the more interesting half: E NEVER FIRES.  Not at
 *                   any seed, not at BYZ-MIXED.  A false server NAME does
 *                   not make a false composition -- correct servers all
 *                   assert the one composition A1 and L6 give them -- so
 *                   inflating the distinct-server count only reaches t+1
 *                   sooner on the bytes the honest servers were already
 *                   attesting, and the adoption is correct.  Falsifying
 *                   L2's caller half through A9 needs the false NAME and
 *                   a fabricated BYTE at once: a Byzantine server whose
 *                   serving is then re-attributed, which is two faults
 *                   and t = 1 buys one.  So this arm establishes A9 for
 *                   L5 and R4 and leaves L2's dependence on A9 UNPROBED,
 *                   reachable only at t >= 2.  Not a gap in the arm -- a
 *                   statement about what one fault can reach.
 *
 * WHAT THE MATRIX ADDS UP TO.  Four premises are now shown load-bearing
 * by measurement rather than by citation: A4 for termination (its
 * withdrawal costs a process, never safety), A6 for progress past an
 * unevidenced process, A5 for liveness outright, A9 for L5's release
 * safety and R4's floor.  Two results correct the brief and are recorded
 * as findings, not smoothed over: the A6 pins are ABSORBED by PLAIN and
 * LAGGARD (the heal outruns the reach at this sizing), and A9's L2 half
 * is out of reach at t = 1.
 *
 * NOTHING ABOVE MOVED.  Every W_* construct is compiled out without its
 * -D: default 42781/0, _t0 2156/0, _big 80249/0 all byte-for-byte
 * unchanged, and the M_* kills re-verified unchanged (DROP 977,
 * NOVOID 10, NOPEND 11, INJ_CANDIDATE 227, EXCH_NOASSEMBLE 1).
 *
 * FABLE VERIFICATION 2026-07-25 (tranche 1 of the matrix, the five arms
 * above): every recorded count reproduced exactly -- the three baselines,
 * the five arm totals, and the five spot-checked mutant kills.  The
 * W_A9_SYBIL failure inventory was classified check-by-check and is the
 * NAMED red and its downstream shadow only: F 16/16 PLAIN, 16/16
 * BYZ-SILENT, 16/16 BYZ-FORGE, 15/16 LAGGARD; the FORGE strict arm
 * 16/16; D's ground-truth arm 5; E ABSENT from all 464.  The E-half
 * explanation was checked structurally: one leg per (round, server,
 * wanter) means a fabricated byte can never accrue two distinct server
 * names at one process, so falsifying L2 through A9 is unreachable at
 * one fault -- L2's A9 dependence stays UNPROBED here and is a t >= 2
 * item for the remaining arms.  The A5 stalls-without-classification
 * were checked against the meter: fresh serve-leg traffic keeps
 * resetting the barren count, so the strand is neither partitioned nor
 * progressing -- the liveness hole the inference exists to close, not a
 * posture defect (correct glue carries the inference).  The A6 pins'
 * brief correction (absorbed at PLAIN/LAGGARD; the w x T_p sizing
 * obligation observed from the other side) is honest measurement, kept.
 * One register note: seam-premises runs the falsifying arm with make's
 * '-' prefix, so its exit status is not machine-asserted -- the red
 * count is verified by reading the run, the M_* currency exactly.
 *
 * ------------------------------------------------------------------
 * 2026-07-25 (stage 2, tranche 2): THE REST OF THE MATRIX.
 * ------------------------------------------------------------------
 *
 * Ten more arms, the same two kinds and the same discipline: one
 * premise per arm, a FALSIFYING arm must fire a NAMED red, a CONTROL
 * asserts its prediction positively and exits 0.  Five arms carry a
 * configuration beside their -D, because the premise IS a configuration
 * (W_REACH_WSHRINK), the scenario the premise is about costs two
 * faults and t = 1 buys one (the two SERVE-rotation arms), or the
 * clause only reads as distinct from the cap when the cap exceeds the
 * floor (the two discharge-order arms, which need t = 2).
 *
 * THE CALLER'S SERVE MECHANICS HAD TO BE BUILT BEFORE THEY COULD BE
 * WITHDRAWN.  The baseline glue reads the SERVE cap as unbounded and
 * the rotation as vacuous: a SERVE act names a want bitmap and the glue
 * births a leg to every process in it, that tick.  Nothing is starved,
 * so both clauses are supplied for free -- and a clause supplied for
 * free is a clause never shown to be load-bearing.  The five SERVE
 * arms therefore compile in the mechanics system.md actually pins (C1):
 * a duty space of (wanting process, round) pairs ordered by process
 * then round, at most CAP duties granted per tick, and a cursor.  CAP
 * is t with a floor of one; the rotation advances the cursor past each
 * grant.  Nothing of this exists without a -DW_SERVE_* flag.
 *
 *   W_SERVE_CAP0    the FLOOR withdrawn -- the cap read to zero, which
 *   FALSIFYING      system.md names as the reading that "would retire
 *                   SERVE by silence, which M1 forbids".  No recovery
 *                   leg is ever born.  PLAIN (the no-op control side:
 *                   nothing is owed there, so a withdrawn discharge
 *                   costs nothing) + LAGGARD.
 *                   RESULT 211 FAILURES at 16 seeds.  THE PREDICTION
 *                   HELD EXACTLY, and the inventory is the serve-floor
 *                   ruling's own red, check by check:
 *
 *                     B  16 "serves were born" + 16 "closed by
 *                        adoption" + 3 "beyond-reach traffic held and
 *                        re-fed" -- every LAGGARD seed.  The heal never
 *                        starts, so it never finishes.
 *                     C  128 -- eight rounds x sixteen seeds at the one
 *                        process the heal was owed to.
 *                     P  16 -- the classification is REJECTED by the
 *                        ground-truth arm at every seed.  The laggard
 *                        LACKS the round its duty is held on, so it is
 *                        a starved heal and not a strand, and it stays
 *                        in the quantifiers.  That rejection is what
 *                        keeps the posture from absorbing this red.
 *                     F  16 structural -- the correct cohort must evict
 *                        rounds the starved process can never possess.
 *                        The instrument's hold-overflow counter falls
 *                        16 times with it, for the same reason.
 *
 *                   AND THE SAFETY ARMS ARE SILENT AT EVERY SEED, which
 *                   is the other half of the statement: D (both halves),
 *                   E, F's unsafe arm and H never move.  Withdrawing the
 *                   floor costs the heal and costs nothing else -- L1's
 *                   HELD half fails, L5 and L6 do not.  PLAIN is green
 *                   throughout, which is the floor's own derivation seen
 *                   from the other side: a cap of zero is invisible
 *                   until something is genuinely behind.
 *
 *   W_SERVE_ROTDROP the ROTATION withdrawn -- the cursor is pinned to
 *   FALSIFYING as   the front of the duty order, so the lowest-indexed
 *   briefed;        wanter is served first, always.  STRESS: a FLOODING
 *   ABSORBED as     SOLICITOR (BYZ_WANT_FLOOD), one Byzantine behavior
 *   measured        and not a second withdrawal -- it admits possession
 *                   of nothing on any egress act and re-offers blind
 *                   each tick for every round to every process,
 *                   deterministic in (mode, destination, round) and
 *                   drawing no RNG.  A blind offer that is not behind
 *                   the receiver's frontier is dropped at INGRESS
 *                   (reaching the value plane would be a SECOND lie).
 *                   Scenario LAGGARD + the flood, which is two faults,
 *                   so the arm runs at n = 7, t = 2 where the budget
 *                   buys them.
 *                   RESULT 14049/0 at 16 seeds.  THE PREDICTION DID NOT
 *                   HOLD, and the measurement says why in one number:
 *
 *                     serve duties: solicitor max 1, cohort max 6, cap 2
 *
 *                   O1'S LINKAGE IS WHAT BOUNDS A FLOOD.  Want for round
 *                   R is recorded from an act OF ROUND R carrying no
 *                   possession indication -- but an act of any LATER
 *                   round evidences its sender's possession of R (A5,
 *                   the inference).  A solicitor that offers for every
 *                   round therefore proves possession of every round but
 *                   its highest, and erases its own want bits as it
 *                   makes them.  One liar can hold at most ONE duty at a
 *                   server, so t liars can crowd at most t of a cap of
 *                   t -- never one more.  The cap's derivation and the
 *                   linkage close the displacement between them.
 *                   WHAT THE ROTATION CLAUSE WOULD NEED TO BE FALSIFIED,
 *                   stated so the next tranche does not re-walk this:
 *                   t liars to fill the cap AND one correct wanter that
 *                   cannot complete on its own account -- because a
 *                   correct wanter merely displaced still finishes its
 *                   own instance under BPR, so displacement costs it
 *                   time and nothing else.  In this instrument an
 *                   unhealable correct wanter costs a fault, so the
 *                   countermodel needs t+1 and is OUT OF MODEL at every
 *                   t.  Reachable in a deployment by the two grounds
 *                   system.md's own floor derivation names and this
 *                   instrument cannot reach -- the abandonment posture's
 *                   stale-cursor re-offer, and an EXHAUSTED instance --
 *                   neither of which is a fault.  The same shape as
 *                   tranche 1's A9-for-L2 finding: not a gap in the arm,
 *                   a statement about what the budget can reach.
 *                   COHORT max 6 against a cap of 2 is the other half of
 *                   the measurement and is worth the record: the cap is
 *                   routinely OVERSUBSCRIBED by correct wanters, and
 *                   neither cursor starves anyone, because those duties
 *                   retire by the wanter's own progress.
 *
 *   W_SERVE_ROTOK   the arm's NEGATIVE CONTROL: the same flood, the same
 *   CONTROL         cap, the rotation INTACT.  RESULT 14049/0, the heal
 *                   completing at every seed (33 adoptions under the
 *                   pinned cursor against 32 under the rotating one --
 *                   the two are indistinguishable, which is the finding
 *                   above restated as a pair of runs rather than as an
 *                   argument).  Serves differ (3276 pinned, 3751
 *                   rotating) and ticks do not (12598 against 12627):
 *                   the pin changes the SHAPE of the discharge and not
 *                   its outcome.
 *
 *   W_SERVE_YIELD   NOT A WITHDRAWAL.  This arm builds the discharge
 *   THE SPEC'S      order SERVE states -- the sequence first, without
 *   OWN ORDER       remainder -- and runs it.  The serve walk is granted
 *                   only on a tick where this process put nothing else on
 *                   the wire (missionActs, counted where traffic is framed:
 *                   emitAcs and emitExchAct).  n = 7, t = 2.
 *                   RESULT 72758/0 at 16 seeds, with serves 0 at every
 *                   seed and every scenario.  The returner is starved for
 *                   the whole run, the sequence completes without it, and
 *                   THAT IS THE ARM PASSING.
 *                   IT DID NOT START THERE: the first cut reported 417
 *                   failures, and every one of them was the INSTRUMENT
 *                   disagreeing with the spec.  What it cost to fix is
 *                   worth recording, because the same shape will recur.
 *                   THE GROUND-TRUTH ARM KNEW ONE LEGITIMATE DEFICIT AND
 *                   NOW KNOWS TWO.  It accepted a PARTITIONED
 *                   classification only where the process HOLDS the round
 *                   its duty is held on and only the evidence is missing
 *                   -- an EVIDENTIAL deficit, and it reads the duty class
 *                   because R4 is what strands such a process.  A returner
 *                   starved by the order is a CAPACITY deficit and takes
 *                   NEITHER conjunct: it sits at duty MET, not HELD,
 *                   because nothing about R4 blocks it -- it simply cannot
 *                   COMPLETE a round the cohort has left behind, and the
 *                   adoption that would close it is what the order
 *                   withheld.  Requiring HELD for the second ground was
 *                   the first cut here and it accepted nothing.
 *                   THE VETO IS UNCHANGED, and it is what keeps this from
 *                   being a weakening: arrived-but-unbanked still REJECTS
 *                   under either deficit, so evidence that reached a
 *                   process and was dropped is still a glue defect and
 *                   M_SEAM_NOPEND keeps its red (11 failures, re-verified).
 *                   The ledger (yieldDenied, per wanting process) is all
 *                   zero in every build that compiles no order, so the
 *                   predicate is literally unchanged elsewhere -- the three
 *                   config baselines and all sixteen other premise arms
 *                   re-run byte-identical.
 *                   TWO ARMS INVERTED RATHER THAN LAPSING, which is the
 *                   discipline worth copying: B's serves-born arm asserts
 *                   serveMsgs == 0 where the order granted nothing all run
 *                   (the yield really held) instead of being skipped, and
 *                   the BYZ-MIXED coverage arm -- whose subject rides a
 *                   serve and so has none here -- lapses NAMED, on the
 *                   sweep-level grant total, rather than passing quietly
 *                   on an empty set.
 *
 *                   The zero is self-sustaining and R2c is why: a correct
 *                   process is never send-silent, and the victim's own
 *                   non-accept keeps the READY retries unretirable, so
 *                   this instrument never reaches a quiet tick.  That is a
 *                   property of a run with a permanent absentee and of the
 *                   PRESSURE PROXY this arm uses, not of the order -- see
 *                   W_SERVE_WIRE, which makes the wire a quantity and gets
 *                   the relief the proxy cannot show.
 *

 *   W_SERVE_YIELDFLOOR  the CONTRAST, kept because it localized the 417.
 *   DIAGNOSTIC      Same build, same order, except a mission tick clamps
 *                   the cap to one grant instead of none.  80225/0 at 16
 *                   seeds.  Only the floor differed, so the original 417
 *                   were the yield's doing and not a defect in the arm --
 *                   which is how the instrument was identified as the
 *                   thing needing correction.  It
 *                   corresponds to no clause: the spec reserves no share,
 *                   and this build is here as an instrument control, not
 *                   as a reading of SERVE.
 *
 *                   ONE MORE MEASUREMENT, from the Fable adversarial
 *                   review 2026-08-01, kept because it bounds what this
 *                   instrument can show.  Counting ONLY the live frontier
 *                   round's carriers as mission -- a narrower yield class
 *                   than the one built here -- the floorless arm runs
 *                   80225/0: the yield never bites, because a process
 *                   holding under R4 runs no instance and its carriers
 *                   are quiet.  That is R4's hold seen as the wire
 *                   freeing, and it is why the BROAD class is the one
 *                   worth instrumenting: it is the only one under which
 *                   a deployment's wire is ever actually full.
 *
 *                   BOTH ARE DISTINCT FROM -DSCHED_KINDFLIP, which puts
 *                   leg and exchange traffic AHEAD of ACS tails in the
 *                   DELIVERY order: that reorders what was already sent,
 *                   while these two decide whether the serve is sent.
 *
 *   W_SERVE_WIRE    THE WIRE MADE A QUANTITY -- the arm the discharge
 *   CONTROL         order actually needs.  WIREBUDGET slots per process
 *                   per tick; mission traffic is never dropped (that is
 *                   what "the sequence goes first" means, and dropping it
 *                   would change the run instead of measuring it) but it
 *                   SPENDS slots, and the serve walk is granted only the
 *                   remainder.  n = 7, t = 2, budget 32.
 *                   RESULT 80463/0 at 16 seeds -- serves 104-117 per
 *                   seed, adoptions at every healing scenario, zero
 *                   classifications.
 *                   THIS IS THE SELF-FUNDING CLAIM MEASURED.  Nothing
 *                   schedules relief for recovery, and none is reserved:
 *                   the arm asserts that BOTH tick classes occurred
 *                   (wireStarved > 0 and wireFreed > 0 -- non-vacuity in
 *                   both directions, since a wire that never binds proves
 *                   nothing and one that never frees cannot show the
 *                   relief), and the heal completes anyway, on slots the
 *                   sequence left because it was holding under R4 or had
 *                   only its retry tail to send.  A stalled sequence IS
 *                   the bandwidth.
 *                   THE BUDGET IS MEASURED, NOT DERIVED, and the sweep is
 *                   the record: at 4 and 8 slots the wire never frees
 *                   (serves 0 at six of twenty scenario-seed lines; 104
 *                   and 97 failures), at 128 it never binds (20 failures,
 *                   the starved-tick assertion once per scenario-seed),
 *                   and 32 is where both classes occur and the run is
 *                   clean.  A tick here is a whole delivery drain plus a
 *                   retry plus the exchanges, so the count is coarse by
 *                   construction; what the arm needs is not a realistic
 *                   bandwidth but a wire that is sometimes full.
 *
 *   W_SERVE_NORESUME  the clause's OTHER half withdrawn: the yield makes a
 *   FALSIFYING      RETIREMENT.  Same wire, same budget; a duty the wire
 *                   had no slot for is DROPPED rather than deferred, so a
 *                   serve withheld under pressure is never offered again.
 *                   M1's forbidden gate at round granularity.
 *                   RESULT 80 FAILURES at 16 seeds, and they are the
 *                   DESIGNATED ORACLE ALONE -- 16 at each of the five
 *                   scenarios, every one of them "spare capacity never
 *                   leaves a want unserved".  Nothing else moves: the
 *                   heal checks, the safety arms and the posture arm are
 *                   silent at every seed.  The withdrawal costs the
 *                   clause and costs nothing else, which is what a
 *                   matched red is for.
 *                   WHY THE ORACLE IS THE CLAUSE AND NOT ITS CONSEQUENCE,
 *                   recorded so it is not re-attempted: a heal-completion
 *                   oracle CANNOT score this pair.  Measured at budget 20,
 *                   before this oracle existed, the heal checks fired 12
 *                   times in the control and 12 in the withdrawal at 16
 *                   seeds -- indistinguishable, because a legitimately
 *                   starved returner reds the every-process quantifiers
 *                   exactly as a retired duty does.  The direct reading
 *                   separates them wherever the wire binds at all: the
 *                   walk grants until the cap or until nothing is owed,
 *                   so a slot left unspent beside a standing want is not
 *                   a deferral -- nothing deferred it.
 *                   ONE SUBTLETY, and getting it wrong made the control
 *                   red at 79: a want clears when its owner EVIDENCES
 *                   possession, never when the serve is granted, so the
 *                   oracle must exclude the duties granted on the same
 *                   tick or it counts every healthy grant as a retirement.
 *
 *   W_R2C_SILENT    R2c withdrawn (a decided process never goes send-
 *   CONTROL         silent): the glue stops driving a round's ACS
 *                   instance the moment this process has closed it.
 *                   The tails are what carry the possession indication,
 *                   so the withdrawal kills that carrier and nothing
 *                   else.  PLAIN + LAGGARD.
 *                   RESULT 12999/0 at 16 seeds.  PREDICTION HELD.  Two
 *                   runs of the thirty-two end with a classification,
 *                   and both are ACCEPTED strands of exactly the shape
 *                   the two-carrier expiry text describes:
 *
 *                     CLASS p3 frontier 3 duty HELD closed[f-1] 1
 *                           classified 1 accepted 1
 *
 *                   -- the process HOLDS the round its duty is held on
 *                   and lacks only the evidence that others do, with no
 *                   indication ever arriving unbanked.  No stall, and
 *                   the safety arms silent at every seed.  The loss is
 *                   banked as sweep coverage (the W_A5_NOINFER pattern)
 *                   rather than asserted per run, because whether a
 *                   given schedule needs the dead carrier is a property
 *                   of the schedule: at 2 seeds it does not happen at
 *                   all, by 16 it does.  A NARROWER READING WAS MEASURED
 *                   FIRST and is recorded because it corrects an easy
 *                   assumption: silencing only the BPR RETRIES leaves
 *                   the reactive cascade (a live instance still answers
 *                   arriving traffic, and its answers carry the
 *                   indication), and that reading is what the numbers
 *                   above are -- the carrier is thinned, not cut.  A
 *                   full send-silence would cut it outright and would
 *                   also stop the ECHO/READY the cohort needs, which is
 *                   two effects; the thin reading isolates the carrier.
 *
 *   W_REACH_WSHRINK the REACH proviso withdrawn BY CONFIGURATION: the
 *   CONTROL, and    reach shrunk to its floor (REACH 1, so one
 *   a SIZING        retained round) with the LAGGARD scenario.  THIS
 *   report          ARM'S OUTCOME IS THE ONE MOST LIKELY TO BE MISREAD
 *                   AS A BUG: a straggler that cannot be served because
 *                   every correct holder evicted the round it needs is
 *                   the "w and T_p are not independent" SIZING
 *                   obligation violated by configuration -- NOT an L1
 *                   red.  L1's own REACH proviso prices it.
 *                   BUILDING IT TOOK A CORRECTION.  At n = 4, t = 1,
 *                   a reach of 1 with no other fault NEVER BINDS:
 *                   all-n release outruns it, so rounds leave by the
 *                   all-n path and eviction almost never fires (3 of 16
 *                   LAGGARD seeds at T_p = 400, and 0 of 4 at T_p = 25
 *                   and T_p = 6, where the heal is faster still).  A
 *                   reach that never binds says nothing about REACH.
 *                   The arm therefore runs the COMPOSED scenario -- a
 *                   WITHHOLDER beside the laggard, at n = 7, t = 2 --
 *                   because a withholder makes all-n possession
 *                   unreachable, which forces every close to evict and
 *                   makes the reach the binding retention constraint.
 *                   RESULT 14073/0 at 16 seeds, 14 evictions, and ZERO
 *                   stalls or strands.  THE PREDICTION DID NOT HOLD:
 *                   even with the reach at its floor and binding, the
 *                   serve/adopt heal completes within the single rung
 *                   the round is retained for.  The straggler is never
 *                   more than REACH rungs behind, because being served
 *                   IS what keeps it within one.  The eviction is asserted
 *                   as coverage (the arm would be vacuous without it);
 *                   the strand is REPORTED, not required, because
 *                   asserting it would be asserting the prediction
 *                   instead of testing it.  Read with tranche 1's A6
 *                   pins -- which were absorbed for the same reason --
 *                   the two arms measure the same fact from opposite
 *                   sides: AT THIS SIZING THE HEAL WINS EVERY RACE
 *                   AGAINST THE REACH, and the REACH proviso is slack
 *                   at REACH = 1.  What would cross it is a heal made
 *                   SLOW
 *                   (loss high enough that a leg handshake outlasts a
 *                   rung), which is outside this instrument's envelope:
 *                   the CLEAN build fails at 25% loss on rounds that
 *                   never close.
 *
 *   W_L2_NOBYTEMATCH C6's BYTE-MATCHING clause withdrawn, and only it:
 *   FALSIFYING       the witness path counts EVERY frontier-round served
 *                    assertion toward the standing candidate whatever
 *                    its bytes.  The machine counts DISTINCT SERVERS and
 *                    nothing else -- grouping the assertions by content
 *                    is the caller's half of L2 -- so without the
 *                    comparison a mixed set latches one book.  The
 *                    re-arm and void clauses are not withdrawn here,
 *                    they are UNREACHABLE: with no byte comparison there
 *                    is no switch to re-arm on.  PLAIN + all six
 *                    Byzantine arms; LAGGARD is out because an honest
 *                    schedule puts ONE composition in flight for a
 *                    frontier round (A1, L6), so only a live
 *                    equivocating server can reach the clause at all.
 *                    RESULT 4 FAILURES at 16 seeds.  THE PREDICTED RED
 *                    FIRES, at BYZ-MIXED seeds 6 and 16, and it is
 *                    exactly the pair L2's countermodel names:
 *
 *                      E, compMismatch -- a correct process's stored
 *                      composition is not the completers' result.
 *                      BYZ MIXED's own arm -- "no correct process closed
 *                      on the fabricated variant" -- falls with it.
 *
 *                    A MIXED LATCH, precisely: t Byzantine assertions of
 *                    X and correct assertions of Y counted into one
 *                    book, reaching t+1 on a candidate no t+1 servers
 *                    ever attested, and the poisoned anchor folding
 *                    forward.  Every machine conjunct stays clean -- the
 *                    machine never sees a composition, so only the
 *                    glue-artifact oracle can see this at all.
 *                    SEED-DEPENDENT AND STRUCTURALLY SO: the fabrication
 *                    reaches a witness path only at an EVEN-indexed
 *                    wanter (the liar flips for even destinations), and
 *                    at n = 4 the ordinary-loss wanters are mostly the
 *                    odd ones.  Two seeds is the whole of it here.
 *
 *   W_L2_NOREARM     C6's RE-ARM clause alone withdrawn: on a candidate
 *   FALSIFYING       switch the glue keeps the accumulated book while
 *                    still voiding the adopt debt.  DISTINCT FROM
 *                    M_SEAM_NOVOID and the header must say how: C6 is
 *                    three clauses, not one -- count only byte-identical
 *                    assertions, RE-ARM the book on a switch, and treat
 *                    a reset as VOIDING an unconsumed ADOPT.  NOVOID
 *                    keeps the third; this keeps the second.  Under
 *                    NOVOID the book is correctly re-armed and the stale
 *                    DEBT is what closes on switched bytes; here the
 *                    debt is correctly voided and the stale WITNESSES
 *                    are what latch a new candidate prematurely.  Two
 *                    different countermodels behind two different
 *                    clauses of one pin.
 *                    RESULT 2 FAILURES at 16 seeds -- E and the
 *                    fabrication arm at BYZ-MIXED seed 5.  PREDICTION
 *                    HELD.  Rarer than NOVOID's ten because the premature
 *                    latch needs the OLD candidate's witnesses to have
 *                    reached t, not merely to exist.
 *                    RELOCATED 2026-08-01, and the arm's own record
 *                    corrects a wrong conclusion: it was first reported
 *                    DARK at HEAD on a 16- then a 32-seed sample.  It is
 *                    not.  It reds at 64 seeds -- 2 failures, BYZ-MIXED
 *                    SEED 40, the same E plus fabrication pair -- its one
 *                    seed having moved past every sample drawn.  The
 *                    step-2 and round-turn relocation shifted the
 *                    scheduler RNG (the same effect that re-froze the
 *                    default sweep 42781 -> 42804); the pre-bridge build
 *                    reds at 16 seeds, 31528/2.  Exposed, not caused.
 *                    REPRODUCE IT WITH LOSS, since a 64-seed run is not a
 *                    matrix step: 16 seeds at 8% loss fires the same red
 *                    ten times over (20 failures, all the designated
 *                    pair), and the CLEAN build is 0 at that rate -- so
 *                    the loss is the reproduction and not the cause.
 *                    THE LESSON, since it generalizes: a red of one or
 *                    two failures at a single seed can leave an 8-seed
 *                    matrix silently on any schedule shift, and reports
 *                    GREEN while its clause has no countermodel.

 *   W_I10_WRONGARTIFACT I10's CALLER HALF withdrawn (the C7 seam pin,
 *   FALSIFYING       and F6 in the obligation map -- it is consumed by
 *                    L6 and appears in no caller list).  The machine
 *                    binds the BYTE: it retains exactly the pre-advance
 *                    frontier.  "The entry holds round R's actual
 *                    result" is the caller's half, and here the close
 *                    stores the STANDING CANDIDATE instead of the
 *                    composition it consumed.  PLAIN + LAGGARD + all six
 *                    Byzantine arms.
 *                    RESULT 16 FAILURES at 16 seeds -- E plus the
 *                    fabrication arm at BYZ-MIXED seeds 1, 3, 4, 6, 7,
 *                    12, 14 and 16.  PREDICTION HELD, and this is the
 *                    purest caller red in the tranche: EVERY MACHINE
 *                    CONJUNCT IS CLEAN AT EVERY SEED -- D both halves,
 *                    F's unsafe arm, H and the posture never move -- and
 *                    the machine CANNOT catch it by construction, since
 *                    it never sees a composition.  Only an oracle that
 *                    compares glue artifacts across processes sees it at
 *                    all, which is why the ledger that feeds E now reads
 *                    what the process STORED rather than what the close
 *                    was handed (byte-identical in every other build --
 *                    the two differ only when this half is withdrawn).
 *                    REACHABILITY WAS THE OPEN QUESTION and it is
 *                    answered by measurement, not by construction: the
 *                    arm stores the stale candidate ONLY where one is
 *                    standing and differs, and the coverage counter
 *                    asserts that state was reached.  Had it stayed
 *                    zero the register entry would read "not covered at
 *                    this configuration"; it does not.
 *
 * WHAT TRANCHE 2 ADDS UP TO.  Four more premises are shown load-bearing
 * by measurement: the SERVE floor for L1's HELD half (and for nothing
 * else -- the safety arms do not move), C6's byte-matching and re-arm
 * clauses for L2 separately, and I10's caller half for L6.  Two arms
 * report ABSORPTION and both are recorded as findings rather than
 * smoothed over: the serve ROTATION cannot be falsified inside the
 * fault budget, because O1's linkage bounds a solicitor to one duty and
 * because a merely-displaced correct wanter still completes on its own;
 * and the REACH proviso is slack at w = 1, because the heal wins the
 * race against the reach at this sizing -- the same measurement the A6
 * pins took from the other side.
 *
 * THE REGISTER, mapped and NOT built (each of these is already covered
 * by a red or a control this file carries, so a new arm would only
 * duplicate one):
 *
 *   A8 (a process forges its OWN possession) -- the containment control
 *      is the stage-1 BYZ_FORGE_POSSESS arm RUNNING GREEN.  A8's
 *      withdrawal must NOT red, and the arm that would red is the one
 *      that does not: A9 is what carries the weight there.
 *   A3 (a server asserts what it holds) -- withdrawing it converts the
 *      server to Byzantine, which is the BYZ server-lie family
 *      (MIXED and EQUIVOCATE), already run.
 *   A10 (the O1 fold binding) -- M_INJ_CANDIDATE is the matched red: a
 *      non-folding garbage composition adopted as the standing
 *      candidate, 227 failures at 16 seeds.
 *   M1 (retire a duty only on remote evidence) -- M_LEG_LOCALRETIRE is
 *      the matched red: the leg retired on the local send.
 *   L2's IDENTITY half (an assertion of the wrong round counted) --
 *      M_SEAM_UNBOUND is the matched red: witness counted on content
 *      alone with the round tag ignored.
 *
 * NOTHING ABOVE MOVED, again.  Every tranche-2 construct compiles out
 * without its -D: default 42781/0 byte-for-byte identical on stdout,
 * _t0 2156/0, _big 80249/0, and the M_* spot checks unchanged (DROP
 * 977, NOVOID 10, NOPEND 11, INJ_CANDIDATE 227, EXCH_NOASSEMBLE 1).
 *
 * FABLE VERIFICATION 2026-07-25 (tranche 2): every count above
 * reproduced exactly -- the three baselines, all eight arms, the five
 * spot-checked mutants, PLUS M_SEAM_UNBOUND re-run because the E ledger's
 * data source changed (391 failures -- the kill lives).  The failure
 * inventories were re-derived independently and are the NAMED reds only:
 * CAP0's 211 decompose B 16+16+3 / C 128 / F-structural 16 /
 * hold-overflow 16 / posture 16 with D, E, F-unsafe, H silent; the three
 * caller-half arms fire the E + fabrication pair and nothing else.  The
 * sysClose ledger change was inspected: p->comp[round] is copied from
 * the close argument in every non-arm build, so the grains diverge only
 * under W_I10_WRONGARTIFACT, and comparing the STORED artifact is what
 * the L6 countermodel demands of the oracle -- a strengthening, not a
 * drift.  C6's clause separation was inspected: NOBYTEMATCH leaves no
 * switch to re-arm on (re-arm/void unreachable, not bundled); NOREARM
 * keeps the comparison and the void.  The ROTDROP absorption argument
 * was checked against system.md's SERVE bounds and the floor derivation:
 * the want-erasure rests on A5 plus the machine's I2 direction, the
 * solicitor-max-1 measurement confirms it, and the honest disposition --
 * the rotation clause's load-bearing frame is the floor's two non-fault
 * grounds (stale-cursor re-offer, EXHAUSTED) this instrument cannot
 * reach -- is fairly stated.  WSHRINK's reach was made to bind by the
 * withholder before the slack was measured, so the absorption is a
 * measurement, not vacuity.  Three spec observations queued to the
 * architect (rotation-clause frame; F6 confirmed by the machine-clean
 * I10 red; REACH's binding quantity is heal-time against rung-time).
 *
 * ------------------------------------------------------------------
 * 2026-07-25 (stage 2, tranche 3): SCHEDULES.
 * ------------------------------------------------------------------
 *
 * Everything above this line -- every mutant, every withdrawal arm, every
 * config point -- ran under ONE delivery policy: a uniform-random pop
 * from the wire queue, seeded.  The Model says schedules and loss are
 * ARBITRARY, and the safety lemmas (L2, L3, L5, L6, L7 and the invariant)
 * quantify over all of them.  Sixteen seeds of one policy is sixteen
 * samples of that quantifier and no more.  Three pieces widen it: other
 * policies, other loss levels, and -- at the smallest shape the spec
 * admits -- ALL of them.
 *
 * ------------------------------------------------------------------
 * 1.  ADVERSARIAL SCHEDULER POLICIES (Makefile target seam-sched)
 * ------------------------------------------------------------------
 *
 * Each -DSCHED_* build replaces the POP CHOICE and nothing else: same
 * queue, same loss draw, same scenarios, same checks, same seeds.  RNG
 * DISCIPLINE: every policy draws EXACTLY ONE rngNext() per successful
 * pop, exactly where the uniform policy draws it, then either uses it
 * (STARVE1 and KINDFLIP pick uniformly WITHIN their preferred class) or
 * discards it (FIFO and LIFO are deterministic).  Drawing nothing would
 * shift every later draw by one position, so the loss COIN SEQUENCE a
 * seed names would differ in every build and no comparison at a seed
 * would mean anything; consuming identically keeps the stream aligned
 * draw-for-draw, so the coin sequence is the SAME under every policy.
 * (Which WIRE a given coin lands on still follows the order, necessarily
 * -- that is the thing under test.  What is pinned is that the coins
 * themselves do not shift.)  Reproducibility holds either way -- each
 * build is a pure function of the seed -- and the state-equivalence
 * oracle is untouched: it compares two runs of ONE build under ONE
 * policy.
 *
 *   policy      checks/fail   ticks   adopts  classified  avg |subset|
 *   uniform     42781 / 0    305117      521   15         3.692
 *   SCHED_FIFO  42677 / 0    303496      415   16         3.692
 *   SCHED_LIFO  37557 / 0    331542     2584   16         3.156
 *   SCHED_STARVE1 43193 / 0  303694     1673   12         3.692
 *   SCHED_KINDFLIP 42523 / 0 324569     2143   17         3.676
 *
 * PREDICTION HELD, in the half that matters: ZERO failures under every
 * policy at every seed, ZERO stalls, and EVERY classification an ACCEPTED
 * strand.  The safety arms -- D both halves, E, F's unsafe arm, H -- never
 * move under any policy.  Safety quantifies over all schedules and these
 * four say so by measurement rather than by citation.
 *
 * TWO MEASUREMENTS ARE FINDINGS, and neither is a red.
 *
 *   LIFO SPENDS THE FAULT BUDGET WITH THE SCHEDULE.  The check-count
 *   spread across policies looks alarming until it is decomposed: it is
 *   almost entirely check I's (round x in-subset member x closer) loop
 *   (16904 triples under the uniform policy, 14302 under LIFO -- 5210 of
 *   the 5224 total difference), and that loop is proportional to the SIZE
 *   of the agreed subsets.  Every policy agrees on all 1728 (run, round)
 *   pairs; what LIFO changes is how many MEMBERS are in each agreed
 *   subset, 3.156 of 4 against 3.692 for every other policy.  Newest-
 *   first delivery starves whichever A-Cast the traffic front is not
 *   producing, so more processes miss the step-2 trigger and are
 *   legitimately excluded -- BKR94's "up to t A-Casts absent" spent by
 *   ORDERING rather than by fault.  It is exactly the tolerance R4
 *   reserves, consumed by the adversary the Model actually names, and the
 *   heal pays for it: LIFO's adoptions run 2584 against the uniform
 *   policy's 521.  Nothing fails; the cohort closes every round and
 *   quiesces.
 *
 *   KINDFLIP PRODUCES A LAGGARD STRAND THE UNIFORM POLICY NEVER REACHES.
 *   Delivering the leg and exchange planes ahead of the ACS tails is
 *   carrier-priority inversion, and the possession INDICATION rides the
 *   ACS tails -- so deferring them thins exactly the carrier system.h's
 *   two-carrier text names.  One LAGGARD seed of sixteen ends with an
 *   ACCEPTED strand (frontier held, the round POSSESSED, no indication
 *   arriving unbanked), where the uniform policy has none in LAGGARD at
 *   any seed.  This is the W_R2C_SILENT arm's outcome reached by a
 *   SCHEDULE instead of by a withdrawal: the same shape, the same
 *   accepted-strand disposition, and independent evidence that the
 *   carrier is thin rather than redundant.
 *
 *   SCHED_STARVE1 IS THE SHARPEST OF THE FOUR AND IT COSTS NOTHING.  One
 *   process's inbound wires are delivered LAST, always -- maximal
 *   per-destination delay with NOTHING DROPPED, so A4 stands unbroken and
 *   the run MUST still converge.  It does, at every seed, in fewer ticks
 *   than the uniform policy (303694 against 305117) and with FEWER
 *   classifications (12 against 15).  Deferring one destination is not
 *   even a COST at this shape, which is the measurement and is left as
 *   one: the instrument does not decompose why, and the obvious reading
 *   (the deferred process receives in a burst once the others drain, and
 *   a burst suits a counted threshold better than a dribble) is a
 *   conjecture nothing here tests.  The victim is a runtime parameter
 *   (argv[3], defaulting to the laggard's index) exactly as lagProc is,
 *   and EVERY process was run as the victim at 16 seeds -- p0 42490/0,
 *   p1 42643/0, p2 42633/0, p3 43193/0 -- so the green is not a property
 *   of which process was chosen.
 *
 * ------------------------------------------------------------------
 * 2.  THE LOSS SWEEP (Makefile target seam-loss)
 * ------------------------------------------------------------------
 *
 * No new -D: the drop percentage has been argv[2] since the config sweep
 * landed, and it overrides the per-scenario default for EVERY scenario,
 * so a level is one uniform loss rate across PLAIN + LAGGARD + STARVE +
 * all six Byzantine arms.  (The default build's own 42781 is therefore
 * NOT a row here -- it runs PLAIN at 8% and everything else at 4%.)
 *
 *   drop  checks/fail    ticks   adopts  accepted strands
 *    0%   42683 / 0     124089       32  15  (STARVE 15)
 *    4%   42807 / 0     302903      396  15  (STARVE 15)
 *    8%   42553 / 0     507172     1226  16  (STARVE 16)
 *   12%   42379 / 0     779791     1739  17  (STARVE 16 + LAGGARD 1)
 *   15%   42277 / 0     864342     1815  17  (STARVE 16 + LAGGARD 1)
 *   20%   42245 / 7    1085421     2004  -- THE EDGE, below
 *
 * PREDICTION HELD EXACTLY.  Safety is green at every level; liveness
 * degrades by CLASSIFICATION and by nothing else, and the degradation is
 * monotone in the loss.  PLAIN and all six Byzantine arms strand at NO
 * swept level -- every accepted strand at 0-15% is either the STARVE
 * positive control (which strands by construction) or, from 12% up, one
 * LAGGARD seed.  THE FIRST LEVEL AT WHICH A STRAND APPEARS OUTSIDE THE
 * POSITIVE CONTROL IS 12%.  The tick cost is the other half of the same
 * statement: 124089 ticks at 0% against 864342 at 15%, a factor of seven
 * bought entirely by BPR retries.
 *
 * THE ENVELOPE EDGE, sharpened.  The header above recorded 25% as outside
 * the instrument without locating the boundary; it is between 15% and
 * 20%.  THE HIGHEST SWEPT LEVEL THAT STAYS FULLY GREEN IS 15%.  At 20%
 * the run reds -- 7 failures, all at STARVE seeds 9 and 13 -- and the
 * inventory is worth recording because of what is NOT in it:
 *
 *     posture: every classification is an accepted strand   2
 *     posture: at most t processes classify                 2
 *     C: every round closed at every non-strand process     3
 *
 * READ FROM THE CLASSIFICATION DUMPS, not inferred.  At seed 9 the whole
 * CORRECT TRIO wedges at frontier 10 with duty TOLERANCE and runs its
 * abandonment budget out -- three classifications beside the STARVE
 * victim's usual accepted one, so four against t = 1.  All three are
 * REJECTED by the ground-truth arm, and on the DUTY class rather than on
 * possession: a process mid-tolerance-climb is not evidentially stranded,
 * so the strand shape correctly does not apply, and C then falls three
 * times at round 10.  At seed 13 one correct process classifies mid-run
 * and then FINISHES (frontier 12, duty TOLERANCE) -- the flag persists
 * because a classified process keeps stepping -- so "at most t" falls
 * while C stays clean.
 *
 * D both halves, E, F's unsafe arm and H are SILENT at every seed at 20%.
 * The envelope edge is therefore a LIVENESS boundary and, more precisely,
 * a SIZING one: at 20% the heal no longer outruns the loss inside the
 * abandonment budget SP, so the barren-sweep meter fires on correct
 * processes that are merely SLOW.  That is the S-against-loss-rate
 * relative of the w x T_p sizing obligation the A6 pins and W_REACH_WSHRINK
 * measured from the other side -- not an L1 red, and not a safety one.
 *
 * ------------------------------------------------------------------
 * 3.  EXHAUSTIVE DELIVERY ENUMERATION (Makefile target seam-enum)
 * ------------------------------------------------------------------
 *
 * -DSCHED_ENUM is not a policy but a DRIVER: instead of sweeping seeds it
 * walks the delivery tree depth-first, and the pop choice comes from the
 * choice string it is walking.  It builds only at n = 2, t = 0 (the
 * source #errors otherwise) -- the smallest deployment the spec admits,
 * and the only shape where a tree of this kind is worth having.
 *
 * NO FORKED STATE.  The run state is four heap-allocated planes per
 * process; a node is reached by RE-EXECUTING its prefix, not by copying,
 * because the harness is a PURE FUNCTION of (seed, choice string) at zero
 * loss.  That is simpler than a deep copy, leak-proof by construction,
 * and cheap at this shape.  The odometer works because incrementing
 * choice position i leaves positions 0..i-1 -- and therefore the branch
 * factors 0..i -- unchanged.
 *
 * THE BOUND, STATED PLAINLY BECAUSE A TRUE EXHAUSTIVE PASS IS NOT
 * REACHABLE AND SAYING SO IS THE POINT.  A single run at this shape
 * delivers about 180 wires (1 round) or about 420 (2 rounds), and the
 * queue a pop chooses from is 16 to 31 wires wide.  The full tree is
 * therefore on the order of 20^400, which is not a state space; every
 * honest form of this mode is a BOUNDED-DEPTH one.  The mode enumerates
 * ALL interleavings of the first ENUMDEPTH deliveries and completes each
 * leaf under the uniform policy, so what is exhaustive is the PREFIX and
 * the tail is one sample -- never presented as more.
 *
 * LOSS IS NOT ENUMERATED, for the same arithmetic: a per-wire
 * delivered/lost binary choice multiplies the tree by 2^(wires pushed),
 * which is hundreds of doublings on top of the above.  The mode therefore
 * runs at ZERO loss, where pushWire takes no draw at all and the run is a
 * pure function of the choice string.  A non-zero argv[2] runs the same
 * tree with the loss SAMPLED from the tail seed; that is available and is
 * reported as sampled, never as enumerated.
 *
 *   shape        depth   leaves       checks     failures  wall
 *   ROUNDS=1       6    29487680    530778248      0       23m02s
 *   ROUNDS=2       5     1593008     44604219      0        1m57s
 *
 * Branch factors along the leftmost path are 16 17 18 17 16 (ROUNDS=2)
 * and 16 17 18 17 16 15 (ROUNDS=1); the widest queue any pop ever chose
 * from is 31.  Both leaf counts are ASSERTED in-program (-DENUMLEAVES),
 * not eyeballed -- the test_system_invariant EXPECTSTATES discipline.
 *
 * ORACLES AT EVERY LEAF, and every leaf is a WHOLE RUN: assertRun entire
 * (the safety arms E, F's unsafe arm, H and D's ground-truth arm; the
 * round-closure quantifiers; quiescence at exhausted queue; the posture
 * and its ground-truth guard) PLUS the state-equivalence oracle across
 * the leaf's own injector-off / injector-on pair.  ZERO FAILURES AT EVERY
 * LEAF OF BOTH TREES.  The prediction held: no schedule the sampled
 * sweeps never reached falsifies a safety conjunct at this shape.
 *
 * TWO ARMS ARE SCOPED TO THE SWEEP RATHER THAN TO THE LEAF, and both are
 * scope notes rather than weakenings -- the per-run form is untouched in
 * every other build:
 *
 *   the t = 0 SERVE FLOOR ("a run must be seen to serve") is a COVERAGE
 *   claim about a sweep at ordinary loss, not a property of a schedule.
 *   The tree runs at ZERO loss, where whether anything is ever owed is
 *   decided by the delivery order alone.  Accumulated tree-wide -- and
 *   the number is itself a result: 1587867 of 1593008 leaves at ROUNDS=2
 *   (99.7%) and 27150612 of 29487680 at ROUNDS=1 (92.1%) are
 *   SERVE-BEARING, so REORDERING ALONE, WITH NOTHING DROPPED, puts a
 *   process behind and exercises the floor-of-one heal in the great
 *   majority of schedules.  The rest are the orders even enough that
 *   nothing is ever owed -- and there are fewer of them at two rounds
 *   than at one, because a second round is a second chance to fall
 *   behind.
 *
 *   the INJECTOR'S PER-SITE non-vacuity, likewise: the injector fires
 *   INJ_PERTICK steps a tick and picks its site uniformly, so whether all
 *   six sites are reached in ONE run measures that run's LENGTH.  A leaf
 *   at ROUNDS=1 is a handful of ticks and reaches five sites.  The
 *   injector stream is therefore SALTED BY LEAF INDEX -- which cannot
 *   weaken the oracle (the oracle compares a leaf's own off and on runs,
 *   the off run injects nothing, and both runs of one leaf carry the same
 *   salt, so the schedule stays bit-identical across the pair) and makes
 *   the tree sample injection sequences beside delivery orders.  All six
 *   sites are then reached, and asserted, tree-wide.
 *
 * WHAT THE ENUMERATION CLOSES, AND WHAT IT DOES NOT.  It closes the
 * question "is there an early interleaving the seeds never sampled that
 * breaks a safety conjunct at the two-process point" -- 31 million
 * schedules say no.  It does NOT reach the Byzantine arms (t = 0 has no
 * budget), the laggard (out of model at n - t = n, verified above), or
 * any schedule whose divergence begins after the fifth delivery (two
 * rounds) or the sixth (one round).  The
 * honest summary is that it is a DEEP BOUND at a TINY shape, complementing
 * the seeded sweeps' shallow bound at three shapes; neither subsumes the
 * other.
 *
 * NOTHING ABOVE MOVED, a third time.  Every tranche-3 construct compiles
 * out without its -D: default 42781/0 byte-for-byte identical on stdout
 * AND stderr, _t0 2156/0, _big 80249/0; the nine re-run W_* arms
 * unchanged (A4 4048/0, PIN0 12643/0, PIN1 16095/0, R2C_SILENT 12999/0,
 * SERVE_CAP0 211, A9_SYBIL 464, L2_NOBYTEMATCH 4, L2_NOREARM 2,
 * I10_WRONGARTIFACT 16); the six M_* spot checks unchanged (DROP 977,
 * NOVOID 10, NOPEND 11, INJ_CANDIDATE 227, EXCH_NOASSEMBLE 1, UNBOUND
 * 391).
 *
 * FABLE VERIFICATION 2026-07-25 (tranche 3; closes stage 2): every count
 * reproduced -- the four policies (37557/42677/43193/42523, all 0), the
 * loss rows 0/15/20 with the 20% edge re-classified independently
 * (posture x4 + C x3 at STARVE seeds 9 and 13, D/E/F-unsafe/H silent --
 * an S-against-loss-rate SIZING boundary, not a red), BOTH enumerations
 * (ENUM1 twice, independently: 29,487,680 leaves / 530,778,248 checks /
 * 0; ENUM2 1,593,008 / 44,604,219 / 0), all twelve W_* arms incl. the
 * four the build report skipped (ROTDROP/ROTOK 14049/0, WSHRINK
 * 14073/0), and the six mutants -- zero warnings throughout.  Code
 * inspected: every policy draws exactly one rngNext per pop at the
 * uniform policy's position, so the loss-coin sequence a seed names is
 * policy-invariant; the ENUM odometer is a correct lexicographic DFS
 * (a branch factor at depth d depends only on the choice prefix, so the
 * just-run leaf's recorded factor is valid at increment time); the
 * per-leaf injector salt cannot weaken the oracle (both runs of a leaf
 * carry it); the two tree-wide arm scopes are scope notes, not
 * weakenings -- the per-run forms compile unchanged in every other
 * build.  The LIFO check-count spread was accepted on its measured
 * explanation (agreed-subset 3.156 vs 3.692; E green on every (run,
 * round) pair -- ACS's tolerated absences spent by ORDERING, safety
 * intact), and KINDFLIP's extra LAGGARD strand is the two-carrier
 * expiry reached by a schedule instead of a withdrawal -- W_R2C_SILENT's
 * outcome from the other side.  Two sizing observations queued to the
 * architect: S x loss-rate is a budget coupling system.md does not
 * price (the w x T_p family's third face), and R4's reserved t is
 * consumed by adversarial ordering as well as by faults.
 *
 * ------------------------------------------------------------------
 * 2026-07-25 (stage 2, tranche 4): C6's COMPLETION-VOID CLAUSE.
 * ------------------------------------------------------------------
 *
 * C6 gained a fourth clause the same day (system.md, the caller list):
 * "a close consuming the round voids any unconsumed ADOPT for it, because
 * L3 commutes the MACHINE state only -- an ADOPT already output is caller
 * state the machine cannot void, and acting on it after the close would
 * adopt over a completed round".  The register carried it as the ONLY C6
 * clause without a matched red.  This arm is that red.
 *
 *   W_L2_NOCLOSEVOID C6's COMPLETION-VOID clause withdrawn, and only it:
 *   FALSIFYING       the glue's own successful close no longer voids the
 *                    adopt debt, so a debt output before the close SURVIVES
 *                    this process's own COMPLETE.
 *
 *                    FOUR CLAUSES, FOUR COUNTERMODELS, and the header must
 *                    keep them apart.  C6 pins: count only byte-identical
 *                    assertions (W_L2_NOBYTEMATCH), RE-ARM the book on a
 *                    switch (W_L2_NOREARM), treat a RESET as voiding an
 *                    unconsumed ADOPT (M_SEAM_NOVOID), and treat a
 *                    superseding COMPLETE the same way (THIS ARM).  NOVOID
 *                    and this arm are the two halves of one sentence and
 *                    are NOT the same defect: NOVOID's stale debt closes
 *                    the SAME round on SWITCHED bytes and needs a live
 *                    equivocating server to put two candidates in flight;
 *                    this arm's stale debt closes the NEXT round on
 *                    UNSWITCHED bytes and needs no second composition at
 *                    all.  The two sites are disjoint -- NOVOID lives at
 *                    legAccept's reset, this one at sysClose's -- and
 *                    neither build touches the other's.
 *
 *                    THE WITHDRAWAL IS ONE CLAUSE ACROSS TWO ASSIGNMENTS,
 *                    because an unconsumed ADOPT is two pieces of caller
 *                    state: the debt flag and the candidate the debt would
 *                    close on.  procTick discharges on BOTH, so clearing
 *                    the candidate beside a surviving flag would void the
 *                    ADOPT by the back door and withdraw the clause in name
 *                    only.  The candidate clear is therefore made
 *                    conditional on no debt surviving -- byte for byte the
 *                    baseline's own clear wherever none does.  Nothing else
 *                    moves, on either path.
 *
 *                    PLAIN + LAGGARD + all six Byzantine arms; STARVE out.
 *                    The scenario restriction is NOT the two clauses above:
 *                    those drop LAGGARD because an honest schedule puts ONE
 *                    composition in flight for a frontier round (A1, L6) and
 *                    only a live equivocator can reach them.  THIS clause's
 *                    race needs no second composition -- only an ADOPT
 *                    standing when the process's own COMPLETE lands -- so an
 *                    HONEST schedule reaches it and LAGGARD is its natural
 *                    manufacturer: a served process whose own instance also
 *                    completes.  STARVE is out because its victim never
 *                    advances past the round it holds, so a surviving debt
 *                    has no NEW frontier to close over.
 *
 *                    RESULT 53 FAILURES at 16 seeds (40 at 8).  THE PREDICTED
 *                    RED FIRES, and the inventory is the named red, its
 *                    downstream shadow, and one liveness cascade:
 *
 *                      E, compMismatch -- 25 firings, at PLAIN seeds 4, 8,
 *                      11, 13, 14; LAGGARD 2, 4, 7, 11; BYZ-FORGE 3, 4, 8;
 *                      BYZ-WITHHOLD 4, 9, 11, 15; BYZ-MIXED 2, 6, 10;
 *                      BYZ-EQUIV 1, 4, 14; BYZ-CONTENT 4, 14, 15.
 *                      BYZ MIXED's own fabrication arm falls with it at its
 *                      three seeds, and BYZ EQUIVOCATE's sequence-identity
 *                      arm at its three -- six downstream firings, no
 *                      independent countermodel.
 *                      BYZ-SILENT seed 7 alone -- C 18 (nine rounds x the
 *                      two wedged correct processes), the containment arm 2,
 *                      the posture 2 -- 22 firings that are the same defect
 *                      seen as LIVENESS: see below.
 *
 *                    AND THE RED NEEDS NO BYZANTINE, which is the sharpest
 *                    thing the arm says.  Nine of the twenty-five E firings
 *                    are at PLAIN and LAGGARD, where every process is
 *                    correct and every server serves the one true
 *                    composition.  The stale candidate is not a fabrication;
 *                    it is the RIGHT bytes for the WRONG ROUND.  That is
 *                    exactly what the clause's own wording predicts ("no
 *                    grouping discipline makes THIS clause vacuous: the race
 *                    ... is a schedule's to manufacture") and it is what
 *                    separates this countermodel from every other C6 red in
 *                    the file -- NOBYTEMATCH, NOREARM and NOVOID are all
 *                    unreachable on an honest schedule.
 *
 *                    TRACED, NOT INFERRED (instrumented throwaway build,
 *                    PLAIN seed 4).  Verbatim:
 *
 *                      p0 own-close round 0 LEFT an ADOPT standing
 *                         (cand 9833aeb1, close comp 9833aeb1)
 *                      p0 discharging SURVIVED debt at frontier 1
 *                         cand 9833aeb1 prevComp 9833aeb1
 *
 *                    -- the candidate is byte-identical to what the close
 *                    consumed, so nothing was fabricated and nothing
 *                    switched; the debt simply outlived its round and the
 *                    discharge JOINed frontier 1 and closed it with round
 *                    0's composition.  The machine cannot refuse it: the
 *                    close names the CURRENT frontier and L3 commutes
 *                    machine state only.  The poisoned anchor then folds
 *                    forward, which is L6's induction failing at a corrupted
 *                    base -- the same downstream shape THE FINDING recorded,
 *                    reached from the completion side instead of the reset
 *                    side.
 *
 *                    THE LIVENESS CASCADE, recorded because it is where the
 *                    defect costs most and E is SILENT.  At BYZ-SILENT seed
 *                    7 one correct process closes round 2 on the stale
 *                    candidate and forks its anchor chain from the other
 *                    two's; under the mute liar the tolerance escape is the
 *                    sole route past a round, the cohort has no budget left
 *                    for a third divergent process, and it wedges at
 *                    frontier 2 and abandons (2210 ticks against ~50 for
 *                    every other arm at that seed; three classifications,
 *                    one accepted).  E does not fire there because the
 *                    forking process is the ONLY one that ever closed round
 *                    2 -- there is no second stored artifact to disagree
 *                    with.  A safety red that reaches a state no other
 *                    process reaches shows up as a liveness one; both are
 *                    the clause.
 *
 *                    AND THE MACHINE CONJUNCTS ARE CLEAN AT EVERY SEED: D
 *                    both halves, F's unsafe arm and H never move, and the
 *                    injector oracle never diverges.  That is the same
 *                    statement W_I10_WRONGARTIFACT makes -- the machine
 *                    never sees a composition, so only an oracle comparing
 *                    glue artifacts ACROSS processes can see this at all.
 *                    H in particular stays silent for a structural reason
 *                    worth naming: the stale close names the frontier and
 *                    therefore ADVANCES it.  A close that spoke the wrong
 *                    round would be refused; this one speaks the right round
 *                    and carries the wrong bytes, which is precisely why the
 *                    clause has to be the CALLER's.
 *
 *                    NON-VACUITY: 32 unvoided debts survived an own close
 *                    over the 16-seed sweep (17 at 8), asserted > 0.  Zero
 *                    would have meant the RACE never occurred and every
 *                    green above it was vacuous -- the register entry would
 *                    then read "not reachable in this glue shape", never
 *                    that the clause is unnecessary.  It is reached, and by
 *                    honest schedules.
 *
 * NOTHING ABOVE MOVED, a fourth time.  W_L2_NOCLOSEVOID compiles out
 * without its -D: default 42781/0, _t0 2156/0, _big 80249/0; the five
 * spot-checked W_* arms unchanged (NOBYTEMATCH 4, NOREARM 2,
 * I10_WRONGARTIFACT 16, SERVE_CAP0 211, A9_SYBIL 464) and the six M_*
 * spot checks unchanged (DROP 977, NOVOID 10, NOPEND 11, INJ_CANDIDATE
 * 227, EXCH_NOASSEMBLE 1, UNBOUND 391).
 *
 * FABLE VERIFICATION 2026-07-25 (this arm): 38102/53 and the counter
 * 32 reproduced; the inventory re-derived independently and it is the
 * NAMED red and its shadow only -- E 25 (9 of them at PLAIN/LAGGARD
 * with ZERO faults present, the honest-schedule reachability that
 * separates this clause from C6's other three), downstream 6, the
 * BYZ-SILENT seed-7 liveness block 22, with D both halves, F's unsafe
 * arm and H silent everywhere.  The withdrawal was inspected at both
 * sites: one clause (the debt flag suppressed, the candidate kept
 * alive ONLY beside a surviving debt -- byte-identical baseline clear
 * otherwise), the legAccept reset-void site untouched, and
 * M_SEAM_NOVOID re-run at 10 to prove the sibling clause's red
 * unmoved.  Baseline and NOREARM re-verified.  Two spec observations
 * queued: the clause is load-bearing for LIVENESS beside L6 (the
 * mute-liar face spends the budget and wedges the cohort), and it is
 * the one C6 clause that is NOT Byzantine-conditional.
 *
 * Header encoding convention (CRITICAL):
 *   n / w / vLen parameters are encoded; actual = value + 1
 *
 * Style: C89, K&R, 2-space indent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bracha87.h"
#include "bkr94acs.h"
#include "system.h"
#include "systemStore.h"

/* ordinal-instantiation adapter: the machine takes rs-byte names; the
 * seam's bookkeeping stays in ordinals */

#define RS ((unsigned int)sizeof (unsigned long))

static int
ordCmp(
  void *ctx
 ,const unsigned char *a
 ,const unsigned char *b
){
  unsigned long av;
  unsigned long bv;
  unsigned long d;

  (void)ctx;
  memcpy(&av, a, sizeof (av));
  memcpy(&bv, b, sizeof (bv));
  if (!(d = bv - av))
    return (0);
  return (d <= ((unsigned long)-1 >> 1) ? -1 : 1);
}

static unsigned char RnPool[16][sizeof (unsigned long)];
static unsigned int RnI;

static const unsigned char *
rn(
  unsigned long v
){
  unsigned char *p;

  p = RnPool[RnI];
  RnI = (RnI + 1) % (sizeof (RnPool) / sizeof (RnPool[0]));
  memcpy(p, &v, sizeof (v));
  return (p);
}

static unsigned long
rv(
  const unsigned char *p
){
  unsigned long v;

  memcpy(&v, p, sizeof (v));
  return (v);
}

/* ------------------------------------------------------------------ */
/*  test plumbing                                                     */
/* ------------------------------------------------------------------ */

static int Failures = 0;
static int Checks   = 0;
static const char *CurTest = "<none>";

#define CHECK(cond, msg) do {                                  \
    ++Checks;                                                  \
    if (!(cond)) {                                             \
      ++Failures;                                              \
      fprintf(stderr, "FAIL [%s]: %s  (%s:%d)\n",              \
              CurTest, (msg), __FILE__, __LINE__);             \
    }                                                          \
  } while (0)

/* ------------------------------------------------------------------ */
/*  configuration                                                     */
/* ------------------------------------------------------------------ */

/* Every shape constant is -D overridable and everything else DERIVES from
 * it, so one source drives the whole config sweep (see the Makefile's
 * seam-configs target).  Nothing below may assume four processes. */
#ifndef NENC
#define NENC      3                 /* process count encoding: actual 4 */
#endif
#define NACT      (NENC + 1)
#ifndef TVAL
#define TVAL      1
#endif
#ifndef REACH
#define REACH     3                 /* the recovery reach: rounds retained */
#endif
#define VLENENC   1                 /* A-Cast value encoding: actual 2 bytes */
#define VLENACT   (VLENENC + 1)
#ifndef MAXPHASES
#define MAXPHASES 8
#endif
#ifndef ROUNDS
#define ROUNDS    12                /* rounds this run drives */
#endif

#define BS        ((NACT + 7) / 8)  /* bitmap bytes for n processes */
#define ANCHOR    4                 /* chain anchor bytes */
#define COMPLEN   (ANCHOR + NACT)   /* anchor followed by subset membership */

#ifndef TP
#define TP        400               /* the caller's T_p duty budget, in ticks */
#endif
#ifndef SP
#define SP        (2 * TP)          /* the progress/abandon budget: barren sweeps
                                     * to a PARTITIONED classification.  system.md
                                     * "The three states": S > T_p, so a bounded
                                     * tolerance hold (T_p barren sweeps) never
                                     * classifies -- only an unbounded strand does.
                                     * Overridable so the REACH arm can shrink the
                                     * w x T_p PRODUCT without also shrinking the
                                     * abandonment budget, which is a different
                                     * quantity (S > T_p either way). */
#endif
#ifndef MAXTICKS
#define MAXTICKS  200000
#endif
#ifndef DRAIN
#define DRAIN     1024              /* wires delivered per tick (three carrier
                                     * planes -- ACS, legs, exchange -- all ride it) */
#endif

#ifndef QCAP
#define QCAP      (1u << 16)
#endif
#ifndef HOLDCAP
#define HOLDCAP   16384
#endif
#ifndef LEGCAP
#define LEGCAP    64                /* recovery legs per process at once */
#endif
#ifndef EXCHCAP
#define EXCHCAP   256               /* exchange instances per process (freed on
                                     * quiescence -- content may assemble past a
                                     * round's release, the O2 out-of-band tail) */
#endif

/* the scenarios' cut points, DERIVED so a scenario means the same thing
 * at every n: the laggard is the last process, the liar is process 1 */
#define LAGPROC   (NACT - 1)
#define LAGROUND  3
#define INDSTARVE 2
#define BYZPROC   1

/* THE BYZANTINE ARM (2026-07-25).  One process lies on EGRESS, and every
 * lie stays WITHIN AUTHENTICATION: the harness 'from' field IS ingress
 * attribution (A9 by construction), so the liar can only ever speak as
 * itself.  Every lie is a deterministic function of (mode, destination,
 * round) and draws NO RNG, so the injector-off and injector-on runs of a
 * Byzantine scenario stay schedule-identical and the state-equivalence
 * oracle keeps its teeth.  With no liar configured (byzProc < 0) not one
 * of these branches is taken and no stream is touched. */
#define BYZ_FORGE_POSSESS    1      /* possesses=1 on every egress act */
#define BYZ_WITHHOLD         2      /* possesses never set */
#define BYZ_MIXED_CANDIDATE  4      /* true composition to even wanters, a
                                     * byte-flipped variant to odd ones --
                                     * the SAME round, simultaneously */
#define BYZ_EQUIVOCATE_VALUE 8      /* a different A-Cast value by
                                     * destination parity */
#define BYZ_SILENT           16     /* present but mute */
#define BYZ_WRONG_CONTENT    32     /* half the exchange sidecars mis-tagged */
#define BYZ_WANT_FLOOD       64     /* the flooding solicitor: possession never
                                     * indicated on any egress act, plus a
                                     * blind per-tick re-offer for every round
                                     * to every process.  ONE behavior -- "I
                                     * want everything, always" -- and the
                                     * forged serve duties it manufactures are
                                     * what crowd a pinned serve cursor
                                     * (2026-07-25, stage 2 tranche 2) */

/* WHICH SWEEP THIS CONFIGURATION RUNS.  At t = 0 every scenario that
 * MANUFACTURES a fault is dropped, and the reason is one fact read three
 * ways: with n-t = n the round tolerates ZERO missing participation.
 *   - a Byzantine participant is out of model outright (zero budget);
 *   - STARVE's targeted evidence cut would classify a process
 *     PARTITIONED, which "at most t classify" makes a definitional
 *     failure rather than a test;
 *   - LAGGARD's permanent per-round ingress cut does not make ONE
 *     process lag, it WEDGES THE ROUND AT EVERY PROCESS -- the victim
 *     never echoes, so no other process's A-Cast can reach the accept
 *     threshold either, the step-2 trigger never sees n-t = n BAs
 *     decided 1, and nobody ever holds the round for the heal to serve
 *     FROM.  Verified, not assumed: at n=2 t=0 every LAGGARD seed ends
 *     with BOTH processes at the cut round, duty MET, classified.
 * The heal L1's t = 0 half speaks of is nonetheless exercised at this
 * point, by PLAIN: ordinary loss puts one process behind, the serve
 * floor of one carries it, and 9 of 16 seeds close by adoption.  The
 * large point trades arms for runtime. */
#define SWEEP_PLAIN   1             /* PLAIN runs at every configuration; only
                                     * a withdrawal arm whose premise PLAIN
                                     * cannot touch turns it off below */
#if TVAL == 0
#define SWEEP_LAGGARD 0
#define SWEEP_STARVE  0
#define SWEEP_BYZ     0
#elif NENC >= 4
#define SWEEP_LAGGARD 1
#define SWEEP_STARVE  0
#define SWEEP_BYZ     2             /* three arms, one of them composed */
#else
#define SWEEP_LAGGARD 1
#define SWEEP_STARVE  1
#define SWEEP_BYZ     1             /* all six arms */
#endif

/* ------------------------------------------------------------------
 * THE PREMISE-WITHDRAWAL MATRIX (2026-07-25, stage 2).  One -DW_* arm per
 * build, the M_* pattern.  Each withdraws EXACTLY ONE premise a system.md
 * proof consumes and carries a PREDICTED outcome; a NEGATIVE control
 * predicts NON-failure, and the safety checks staying green IS its
 * result.  Each arm runs only the scenarios its withdrawal is about -- a
 * scenario the withdrawn premise does not touch is runtime, not evidence.
 * ------------------------------------------------------------------ */
#if defined(W_A4_PARTITION)
#undef  SWEEP_LAGGARD
#define SWEEP_LAGGARD 0
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0
#undef  SWEEP_BYZ
#define SWEEP_BYZ     0             /* the partition IS the fault budget */
#elif defined(W_A6_PIN0) || defined(W_A6_PIN1)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0             /* STARVE strands by construction, so it
                                     * would satisfy these arms vacuously */
#undef  SWEEP_BYZ
#define SWEEP_BYZ     3             /* THE MUTE ARM ALONE, and it is the whole
                                     * teeth of both pins.  PLAIN and LAGGARD
                                     * absorb either pin outright (measured --
                                     * see the header): the serve/adopt heal
                                     * restores ALL-N possession before the
                                     * reach rolls, so MET carries every
                                     * advance and the tolerance escape is
                                     * never the only route.  A process that
                                     * emits nothing is never evidenced by
                                     * anyone, so under the mute liar all-n is
                                     * UNREACHABLE and the escape is the SOLE
                                     * route past it -- the one place a pin
                                     * can be read off the outcome. */
#elif defined(W_A5_NOINFER)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0
#undef  SWEEP_BYZ
#define SWEEP_BYZ     0
#elif defined(W_A9_SYBIL)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0
#elif defined(W_SERVE_CAP0)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0             /* PLAIN is the no-op control side (nothing
                                     * is owed, so a withdrawn discharge costs
                                     * nothing); LAGGARD is where the heal is */
#undef  SWEEP_BYZ
#define SWEEP_BYZ     0
#elif defined(W_SERVE_ROTDROP) || defined(W_SERVE_ROTOK)
#undef  SWEEP_PLAIN
#define SWEEP_PLAIN   0
#undef  SWEEP_LAGGARD
#define SWEEP_LAGGARD 0
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0
#undef  SWEEP_BYZ
#define SWEEP_BYZ     4             /* the ONE scenario the rotation clause is
                                     * about: a laggard needing the heal while
                                     * a flooding solicitor manufactures forged
                                     * serve duties ahead of it.  Two faults,
                                     * so the arm runs at n=7 t=2 */
#elif defined(W_R2C_SILENT)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0             /* STARVE cuts the carriers this arm is
                                     * withdrawing, so it would satisfy the
                                     * prediction vacuously */
#undef  SWEEP_BYZ
#define SWEEP_BYZ     0
#elif defined(W_REACH_WSHRINK)
#undef  SWEEP_PLAIN
#define SWEEP_PLAIN   0
#undef  SWEEP_LAGGARD
#define SWEEP_LAGGARD 0
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0
#undef  SWEEP_BYZ
#define SWEEP_BYZ     5             /* the composed WITHHOLD + LAGGARD arm, and
                                     * the composition is what makes the reach
                                     * BIND: with a withholder present all-n
                                     * possession is unreachable, so no round
                                     * ever releases by the all-n path and every
                                     * close must evict.  Measured first without
                                     * it -- see the header: at w = 1 and no
                                     * withholder the all-n release outruns the
                                     * reach at every T_p tried, so the reach
                                     * is not the binding retention constraint
                                     * and the arm would have gone vacuous.
                                     * Two faults, so n = 7, t = 2 */
#elif defined(W_L2_NOBYTEMATCH) || defined(W_L2_NOREARM)
#undef  SWEEP_LAGGARD
#define SWEEP_LAGGARD 0             /* an honest schedule puts ONE composition
                                     * in flight for a frontier round (A1, L6),
                                     * so only a live equivocating server can
                                     * reach these clauses at all */
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0
#elif defined(W_L2_NOCLOSEVOID)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0             /* LAGGARD STAYS IN, and the difference from
                                     * the two clauses above is the whole point:
                                     * this clause's race needs no second
                                     * composition in flight -- only an ADOPT
                                     * output and unconsumed when this process's
                                     * OWN COMPLETE closes the round -- so an
                                     * HONEST schedule reaches it, and LAGGARD is
                                     * its natural manufacturer (a served process
                                     * whose own instance also completes).  The
                                     * six Byzantine arms stay in because they
                                     * carry the sharper form (a fabrication
                                     * closed over a completed round).  STARVE is
                                     * OUT: its victim never advances past the
                                     * round it holds, so a surviving debt has no
                                     * NEW frontier to close over and the
                                     * scenario is runtime, not evidence. */
#elif defined(W_I10_WRONGARTIFACT)
#undef  SWEEP_STARVE
#define SWEEP_STARVE  0             /* LAGGARD manufactures the candidates and
                                     * the Byzantine arms make them DIFFER from
                                     * what a close consumes -- the only state
                                     * in which a wrong store is observable */
#endif

/* THE CALLER'S SERVE MECHANICS, MADE EXPLICIT (system.md SERVE bounds; the C1
 * seam pin).  The baseline glue reads the cap as unbounded and the rotation as
 * vacuous: it discharges every owed (round, process) duty the moment a SERVE
 * act names it.  That is legal -- a cap of n is within "capped at t ... and
 * never fewer than one" only in the sense that it never starves anyone -- but
 * it supplies both clauses for free, and a clause supplied for free is a
 * clause never shown to be load-bearing.  These three arms therefore run the
 * mechanics the spec pins: a concurrency CAP of t grants per tick with a FLOOR
 * of one, and a ROTATION over the duty space ordered by wanting process and
 * then by round -- "granted oldest-want-first in rotation".  Each arm
 * withdraws exactly one clause and nothing else. */
#if defined(W_SERVE_CAP0) || defined(W_SERVE_ROTDROP) || defined(W_SERVE_ROTOK) \
 || defined(W_SERVE_YIELD) || defined(W_SERVE_YIELDFLOOR) \
 || defined(W_SERVE_WIRE) || defined(W_SERVE_NORESUME)
#define W_SERVEWALK 1
#if defined(W_SERVE_CAP0)
#define W_SERVECAP  0               /* the FLOOR withdrawn: a cap read to zero,
                                     * which retires SERVE by silence */
#else
#define W_SERVECAP  (TVAL ? TVAL : 1)
#endif
#endif
#if defined(W_SERVE_ROTDROP) || defined(W_SERVE_ROTOK)
#define W_SERVEFLOOD 1
#endif

/* THE DISCHARGE ORDER (system.md SERVE discharge + R4's hold; the C1 seam
 * pin's third clause).  The spec orders the two traffic classes on a finite
 * wire -- the sequence first, without remainder -- and reserves NO share for
 * recovery: under pressure a want is ignored outright, the duty deferred and
 * never retired.  The baseline glue models no wire capacity at all, so it
 * supplies the order for free by never contending; these arms make the
 * contention explicit.  MISSION is the
 * sequence's own emission this tick (the round instance's acts and the
 * exchange's), counted where it is framed; recovery is the serve walk that
 * follows it in the same tick.  Distinct from -DSCHED_KINDFLIP, which
 * reorders DELIVERY of traffic already sent: this is whether the serve is
 * granted at all. */
#if defined(W_SERVE_YIELD) || defined(W_SERVE_YIELDFLOOR) \
 || defined(W_SERVE_WIRE) || defined(W_SERVE_NORESUME)
#define W_SERVEPRIO 1
#endif

/* THE FINITE WIRE, and the two arms that need one.  W_SERVE_YIELD's mission
 * class is "did this process emit at all", which is a PRESSURE PROXY and not
 * a capacity: it reads a three-message retry trickle exactly as it reads a
 * round at full volume, so its wire is never free and its serve count is
 * zero forever.  That answers whether a saturated wire starves recovery; it
 * cannot answer what the spec's self-funding claim actually says, which is
 * that the wire FREES when the sequence cannot proceed.
 * WIREBUDGET makes the wire a quantity: slots per process per tick.  Mission
 * traffic is never dropped -- that is what "the sequence goes first" means,
 * and dropping it would change the run instead of measuring it -- but it
 * SPENDS slots, and the serve walk is granted only what is left.  A tick at
 * full volume leaves nothing; a tick spent holding under R4, or one where
 * the sequence has only its retry tail to send, leaves slots and recovery
 * takes them.  Nobody schedules that relief; it is what a stalled sequence
 * IS.
 * THE NUMBER IS MEASURED, NOT DERIVED, and the sweep is the record: at 4
 * and 8 slots the wire never frees (serves 0 at six of twenty scenario-seed
 * lines, 104 and 97 failures), at 128 it never binds (20 failures -- the
 * starved-tick assertion, one per scenario-seed), and 32 is where both tick
 * classes occur and the heal still completes.  A tick here is a whole
 * delivery drain plus a retry plus the exchanges, so the slot count is
 * coarse by construction; what the arm needs is not a realistic bandwidth
 * but a wire that is sometimes full and sometimes not.  -D overridable,
 * because the number is a deployment's and not a claim. */
#if defined(W_SERVE_WIRE) || defined(W_SERVE_NORESUME)
#define W_SERVEWIRE 1
#ifndef WIREBUDGET
#define WIREBUDGET 32
#endif
#endif

#if defined(W_REACH_WSHRINK) && REACH != 1
#error "W_REACH_WSHRINK withdraws the REACH proviso BY CONFIGURATION: build it with -DREACH=1"
#endif

/* THE ARM'S OWN VICTIM.  Where an arm PREDICTS that one process is lost --
 * partitioned outright, or shed by a tolerance the withdrawal funds
 * instantly -- that process must leave the correct-cohort quantifiers
 * exactly as an accepted strand does, else the arm reds on its own
 * prediction and proves nothing.  This is the arm's PREDICTION made
 * assertable, not a weakening: it is zero in every build that compiles no
 * such arm, so no baseline and no M_* kill moves. */
#if defined(W_A4_PARTITION)
#define WVICT        ((unsigned int)(NACT - 1))   /* not the liar (process 1) */
#define WLOST(r, i)  ((i) == WVICT)
#elif defined(W_A6_PIN1)
#define WLOST(r, i)  ((r)->lagProc >= 0 && (int)(i) == (r)->lagProc)
#else
#define WLOST(r, i)  0
#endif

#ifdef W_A9_SYBIL
#define W_SYBIL_EVERY 4             /* every k-th recorded act is attributed
                                     * to a rotated false sender */
#endif

/* WHERE AN ARM'S PREDICTED LIVENESS COST LANDS.  A control arm predicting
 * a liveness loss cannot also assert liveness in the scenario it predicts
 * the loss for -- every quantifier that says a round closed everywhere is
 * that prediction inverted.  WSTARVED names the scenarios an arm expects
 * to lose, WWEDGED the sharper case an arm asserts POSITIVELY.  The SAFETY
 * arms -- D both halves, E, F's unsafe arm, H -- are never withdrawn for
 * any scenario in any build; their staying green is what a control is FOR.
 * Both are zero in every build compiling no arm, so no baseline moves. */
#if defined(W_A6_PIN0) || defined(W_A5_NOINFER) || defined(W_R2C_SILENT) \
 || defined(W_REACH_WSHRINK)
/* Both arms predict a LIVENESS cost, and the RUN's own outcome is what
 * says whether it landed.  A run that neither stalled nor abandoned
 * anyone is held to the full baseline standard, quantifier for
 * quantifier; only where the loss actually occurred are the liveness
 * quantifiers withdrawn, since there they are the prediction restated. */
#define WSTARVED(r) (!(r)->st.converged || (r)->st.numClassified)
#else
#define WSTARVED(r) 0
#endif

/* THE CAPACITY DEFICIT, readable only by the instrument.  Set when a
 * discharge order took the walk's capacity and left this process's want
 * standing -- the spec's own behaviour, not a loss and not a defect.  Zero
 * in every build that compiles no order. */
#ifdef W_SERVEPRIO
#define YIELDSTARVED(r, i) ((r)->st.yieldDenied[i])
#else
#define YIELDSTARVED(r, i) 0
#endif

/* the mute arm under a shut pin: all-n possession is unreachable, so the
 * tolerance escape is the SOLE route past round 0 and shutting it wedges
 * the cohort there.  Sharp enough to assert POSITIVELY. */
#if defined(W_A6_PIN0)
#define WWEDGED(r)  (((r)->byzMode & BYZ_SILENT) != 0)
#else
#define WWEDGED(r)  0
#endif

/* THE CORRECT COHORT is what every quantifier speaks about: a Byzantine
 * participant's own state proves nothing, so it enters neither the
 * ground-truth ledgers nor the posture, quiescence or check quantifiers. */
#define BYZSELF(r, i) ((r)->byzProc >= 0 && (int)(i) == (r)->byzProc)
#define NCORRECT(r)   ((unsigned int)(NACT - ((r)->byzProc >= 0 ? 1 : 0)))

/* R4's GROUND-TRUTH FLOOR.  With no liar every one of the n-t possession
 * bits the advance gate consumes is a true bit, so n-t processes really
 * closed the prior round.  A9 confines a forgery to its own sender, so
 * forged bits are bounded by the number of liars PRESENT -- this harness
 * fields exactly one -- not by the budget t: with one liar at most one
 * of the consumed bits is forged and n-t-1 correct processes really
 * closed.  (The general t-liar bound is n-2t; subtracting t here would
 * surrender a process's worth of the check's teeth at t >= 2.) */
#define SHEDFLOOR(r) \
  ((unsigned int)(NACT - TVAL - ((r)->byzProc >= 0 ? 1 : 0)))

/* ------------------------------------------------------------------
 * THE DELIVERY POLICY (2026-07-25, stage 2 tranche 3).  Every run above
 * this line was scheduled ONE way: a uniform-random pop from the wire
 * queue.  The Model's asynchrony says schedules are ARBITRARY, and the
 * safety lemmas (L2, L3, L5, L6, L7 and the invariant) quantify over all
 * of them, so one policy is one sample of the quantifier.  Each -DSCHED_*
 * below replaces the POP CHOICE and nothing else -- same queue, same loss
 * draw, same scenarios, same checks.
 *
 * RNG DISCIPLINE, and the choice is load-bearing: every policy draws
 * EXACTLY ONE rngNext() per successful pop, exactly where the uniform
 * policy draws it, and then either uses it (STARVE1 / KINDFLIP pick
 * uniformly WITHIN their preferred class) or discards it (FIFO / LIFO are
 * deterministic).  The alternative -- drawing nothing -- would shift every
 * subsequent loss draw by one position, so a seed would name a different
 * loss coin sequence in every policy build and the builds would not be
 * comparable at a seed.  Consuming identically keeps the stream aligned
 * draw-for-draw at the same point in the run, so a seed names the SAME
 * loss pattern under every policy and the only thing a policy changes is
 * the ORDER.  Reproducibility is unaffected either way (each build is
 * still a pure function of the seed), and the state-equivalence oracle is
 * untouched: it compares two runs of the SAME build under the SAME policy,
 * and the injector still draws its own separate stream. */
#if defined(SCHED_LIFO) || defined(SCHED_FIFO) || defined(SCHED_STARVE1) \
 || defined(SCHED_KINDFLIP) || defined(SCHED_ENUM)
#define SCHED_POLICY 1
#endif
#if defined(SCHED_FIFO) || defined(SCHED_LIFO)
#define SCHED_SEQ 1                 /* arrival order must be carried; the
                                     * queue's swap-with-last pop destroys
                                     * array position as an age proxy */
#endif

/* SCHED_ENUM: not a policy but a DRIVER -- the pop choice comes from an
 * enumerated choice string, and main() walks the tree instead of sweeping
 * seeds.  It runs at the smallest deployment the spec admits. */
#if defined(SCHED_ENUM) && (TVAL != 0 || NENC != 1)
#error "SCHED_ENUM enumerates delivery interleavings at the n=2 t=0 point: build it with -DNENC=1 -DTVAL=0"
#endif
#ifdef SCHED_ENUM
#ifndef ENUMDEPTH
#define ENUMDEPTH  5                /* deliveries enumerated exhaustively */
#endif
#endif

/* invalid-injector verdict-gate sites (system.md A9/O3/O1/A10 and the
 * carrier verdicts).  INVALID REDUCES TO LOSS: a flagged item is dropped
 * before any state touch -- indistinguishable from absence. */
#define INJ_ATTRIB    0             /* A9: a forged-sender act */
#define INJ_WITNESS   1             /* O3: an invalid served assertion */
#define INJ_CANDIDATE 2             /* O1/A10: a non-folding candidate */
#define INJ_LEG       3             /* a forged recovery leg */
#define INJ_EXCH      4             /* a corrupted exchange sidecar */
#define INJ_POSSESS   5             /* a forged possession indication */
#define INJ_SITES     6
#ifndef INJ_RATE
#define INJ_RATE  40                /* percent of injection steps that fire */
#endif
#define INJ_PERTICK 8               /* injection steps per tick */

/* actions room for one bkr94acs call */
#define ACSACTS   BKR94ACS_MAX_ACTS(NENC, MAXPHASES)

/* the commonly provisioned chain base */
static const unsigned char Genesis[ANCHOR] = { 0x5b, 0xa5, 0x11, 0x00 };

/* ------------------------------------------------------------------ */
/*  repeatable scheduler RNG                                          */
/* ------------------------------------------------------------------ */

static unsigned long Rng = 1;

static unsigned int
rngNext(
  void
){
  Rng = Rng * 6364136223846793005UL + 1442695040888963407UL;
  return ((unsigned int)(Rng >> 33));
}

static void
rngSeed(
  unsigned long s
){
  Rng = s ? s : 1;
  (void)rngNext();
}

/* the INJECTOR RNG -- a SEPARATE stream from the schedule RNG.  The
 * injector draws only from here, never from Rng, and its (gated)
 * injections touch no state and push no wire, so the legitimate schedule
 * is bit-identical with the injector off or on.  That identity is what
 * makes the state-equivalence oracle valid. */
static unsigned long Inj = 1;

static unsigned int
injNext(
  void
){
  Inj = Inj * 6364136223846793005UL + 1442695040888963407UL;
  return ((unsigned int)(Inj >> 33));
}

static void
injSeed(
  unsigned long s
){
  Inj = s ? s : 1;
  (void)injNext();
}

static unsigned char
seamCoin(
  void *closure
 ,unsigned char phase
){
  (void)closure;
  return ((unsigned char)(phase & 1));
}

/* ------------------------------------------------------------------ */
/*  wire -- one struct for both kinds the seam carries                 */
/* ------------------------------------------------------------------ */

#define WK_ACS   0    /* a round's ACS traffic */
#define WK_SERVE 1    /* a recovery-leg (Fig1 2/0) message for a round */
#define WK_EXCH  2    /* an exchange (Fig1 n/t) message for a round's member */

struct wire {
  unsigned char kind;
  unsigned char to;
  unsigned char from;
  unsigned char sysRound;            /* the system layer's round */
  unsigned char cls;                 /* ACS: BKR94ACS_CLS_ACAST / _BA */
  unsigned char process;             /* ACS: whose A-Cast / BA */
  unsigned char baRound;             /* ACS: BA round */
  unsigned char initiator;           /* ACS: BA Fig1 initiator */
  unsigned char type;                /* ACS/SERVE: BRACHA87_INITIAL/ECHO/READY */
  unsigned char baValue;             /* ACS: binary BA value */
  unsigned char accepted;            /* ACS/SERVE: ACCEPTED rides a READY */
  unsigned char possesses;           /* ACS: possession indication for sysRound */
  unsigned char legServer;           /* SERVE: the leg's initiator (leg index 0) */
  unsigned char legServed;           /* SERVE: the leg's other end (leg index 1) */
  unsigned char exchMember;          /* EXCH: the exchange's initiator (member m) */
  unsigned char exchVal;             /* EXCH: the common content token (value plane) */
  unsigned char exchPay;             /* EXCH: the per-destination sidecar payload */
#ifdef SCHED_SEQ
  unsigned long seq;                 /* arrival order, for the ordered policies */
#endif
#ifdef W_SERVEFLOOD
  unsigned char flood;               /* a flooding solicitor's blind re-offer */
#endif
  unsigned char value[VLENACT];      /* ACS: A-Cast value */
  unsigned char comp[COMPLEN];       /* SERVE: the leg's value (the composition) */
};

static struct wire WireQ[QCAP];
static unsigned int QHead = 0;
static unsigned int QTail = 0;
#ifdef SCHED_SEQ
static unsigned long QSeq = 0;
#endif

static void
qReset(
  void
){
  QHead = QTail = 0;
#ifdef SCHED_SEQ
  QSeq = 0;
#endif
}

static unsigned int
qSize(
  void
){
  return (QTail - QHead);
}

static void
qPush(
  const struct wire *w
){
  if (qSize() >= QCAP) {
    fprintf(stderr, "FATAL [%s]: wire queue overflow\n", CurTest);
    abort();
  }
  WireQ[QTail % QCAP] = *w;
#ifdef SCHED_SEQ
  WireQ[QTail % QCAP].seq = QSeq++;
#endif
  ++QTail;
}

#ifdef SCHED_ENUM
/* EXHAUSTIVE DELIVERY ENUMERATION (2026-07-25, stage 2 tranche 3).  The
 * pop choice for the first ENUMDEPTH deliveries comes from a choice
 * string the driver walks depth-first; past it the uniform policy
 * completes the run.  There is no forked run state: the harness is a pure
 * function of (seed, choice string) at zero loss, so a node is reached by
 * RE-EXECUTING its prefix -- simpler than deep-copying four heap-allocated
 * planes per process, and leak-proof by construction.
 *
 * EnumBranch[i] records the queue size at the i-th pop OF THE CURRENT
 * PATH, which is what the odometer needs: incrementing position i leaves
 * positions 0..i-1 -- and therefore branches 0..i -- unchanged. */
static unsigned int EnumChoice[ENUMDEPTH];
static unsigned int EnumBranch[ENUMDEPTH];
static unsigned int EnumPos;          /* pops taken so far in this run */
static unsigned long EnumPops;        /* pops in the last completed run */
static unsigned int EnumMaxQ;         /* the widest queue any pop ever chose from */
/* THE INJECTOR STREAM IS SALTED PER LEAF, and only here.  A leaf at this
 * shape is a handful of ticks, so one injector seed fires the same short
 * site sequence at every leaf and a site can go unreached tree-wide --
 * that would measure the leaf's LENGTH, not the gates.  Salting by the
 * leaf index makes the tree sample injection sequences beside delivery
 * orders, and it cannot weaken the oracle: the oracle compares a leaf's
 * OWN off and on runs, the off run injects nothing, and both runs of one
 * leaf carry the same salt, so the schedule stays bit-identical across
 * the pair exactly as before. */
static unsigned long EnumInjSalt;
#endif

#ifdef SCHED_POLICY
/* the pop CHOICE, and the only thing a policy replaces.  'v' is the one
 * schedule draw this pop makes -- identical in count and position to the
 * uniform policy's, see THE DELIVERY POLICY above. */
#if defined(SCHED_FIFO) || defined(SCHED_LIFO)
/* SCHED_FIFO delivers the oldest queued wire, SCHED_LIFO the newest: the
 * two endpoints of the reordering axis.  FIFO is the degenerate in-order
 * schedule (a coverage endpoint, not an adversary); LIFO is maximal
 * reordering against FIFO intuition, and starves whatever the traffic
 * front is not producing. */
static unsigned int
schedPick(
  unsigned int sz
 ,unsigned int v
){
  unsigned int i, best;

  (void)v;
  best = 0;
  for (i = 1; i < sz; ++i)
#ifdef SCHED_FIFO
    if (WireQ[(QHead + i) % QCAP].seq < WireQ[(QHead + best) % QCAP].seq)
#else
    if (WireQ[(QHead + i) % QCAP].seq > WireQ[(QHead + best) % QCAP].seq)
#endif
      best = i;
  return (best);
}
#elif defined(SCHED_STARVE1) || defined(SCHED_KINDFLIP)
/* A CLASS-PREFERRING policy: everything OUTSIDE the deferred class is
 * delivered first, uniformly at random within it; the deferred class is
 * reached only when nothing else is queued.
 *   SCHED_STARVE1  defers one process's INBOUND wires -- maximal
 *                  per-destination delay with NOTHING DROPPED, so A4
 *                  stands and the run must still converge.  The sharpest
 *                  schedule-independence statement available here.
 *   SCHED_KINDFLIP defers the ACS tails behind the leg and exchange
 *                  planes -- carrier-priority inversion across the three
 *                  carrier geometries. */
#ifdef SCHED_STARVE1
static unsigned int SchedVictim = LAGPROC;
#define SCHED_EARLY(w) ((unsigned int)(w)->to != SchedVictim)
#else
#define SCHED_EARLY(w) ((w)->kind != WK_ACS)
#endif

static unsigned int
schedPick(
  unsigned int sz
 ,unsigned int v
){
  unsigned int i, k, cnt;

  cnt = 0;
  for (i = 0; i < sz; ++i)
    if (SCHED_EARLY(&WireQ[(QHead + i) % QCAP]))
      ++cnt;
  if (!cnt || cnt == sz)
    return (v % sz);
  k = v % cnt;
  for (i = 0; i < sz; ++i)
    if (SCHED_EARLY(&WireQ[(QHead + i) % QCAP])) {
      if (!k)
        return (i);
      --k;
    }
  return (v % sz);                   /* not reached: cnt counted them */
}
#else                                /* SCHED_ENUM */
static unsigned int
schedPick(
  unsigned int sz
 ,unsigned int v
){
  unsigned int pick;

  if (sz > EnumMaxQ)
    EnumMaxQ = sz;
  if (EnumPos < ENUMDEPTH) {
    EnumBranch[EnumPos] = sz;
    pick = EnumChoice[EnumPos] < sz ? EnumChoice[EnumPos] : sz - 1;
  } else
    pick = v % sz;
  ++EnumPos;
  return (pick);
}
#endif
#endif

/* uniform-random pop via swap-with-last: the seeded shuffle */
static int
qPopRandom(
  struct wire *out
){
  unsigned int sz, pick, idx, lastIdx;

  if (!(sz = qSize()))
    return (0);
#ifdef SCHED_POLICY
  pick = schedPick(sz, rngNext());
#else
  pick = rngNext() % sz;
#endif
  idx = (QHead + pick) % QCAP;
  *out = WireQ[idx];
  --QTail;
  lastIdx = QTail % QCAP;
  if (idx != lastIdx)
    WireQ[idx] = WireQ[lastIdx];
  return (1);
}

/* ------------------------------------------------------------------ */
/*  per-process state: the machine, its instances, and the glue's own */
/* ------------------------------------------------------------------ */

/* a recovery leg: a two-process Fig1 (server = initiator, t = 0) that
 * carries one round's composition from a server to a wanting process.
 * Leg index 0 is the server, 1 is the served end.  The instance's own
 * BPR is the retry; the leg retires on quiescence (both ends accepted)
 * or when its round releases. */
struct leg {
  struct bracha87Fig1 *f1;
  unsigned char server;
  unsigned char served;
  unsigned char round;
  unsigned char inUse;
  unsigned char selfAcc;             /* this end has ACCEPTED */
  unsigned char otherAcc;            /* the other end announced ACCEPTED */
  unsigned char retired;             /* quiesced or released -- inert */
};

/* an exchange: a reliable-broadcast (Fig1 n/t) instance carrying one
 * member m's content token for round R to all n on the value plane, with
 * a per-destination sidecar riding the initiator's INITIAL.  The
 * initiator drives INITIAL until all-echoed (a persistent per-process
 * carrier, unlike the pair-accept legs); ACCEPT + sidecar delivers m's
 * content, setting the have grain.  Freed on round release. */
struct exch {
  struct bracha87Fig1 *f1;
  unsigned char round;
  unsigned char member;              /* m, the initiator */
  unsigned char inUse;
  unsigned char accepted;            /* this process accepted the value plane */
  unsigned char payload;             /* the per-destination sidecar received */
  unsigned char havePayload;         /* the sidecar is in hand (the presence gate) */
  unsigned char delivered;           /* content delivered to the have grain */
};

struct proc {
  struct system *sys;
  struct systemStore *store;         /* this seat's retention store: the
                                      * retained rounds and their records
                                      * are the CALLER's (system.h, the
                                      * retention operations), so the glue
                                      * under test supplies them */
  struct bkr94acs *acs[ROUNDS];      /* one per launched round, freed on release */
  struct leg legs[LEGCAP];           /* recovery legs, freed on retire */
  struct exch exchs[EXCHCAP];        /* exchange instances, freed on round release */
  unsigned char haveTok[ROUNDS][NACT]; /* ground truth: content token held, 0=none */
  unsigned char haveTold[ROUNDS][NACT];/* the machine was told (Complete-have/Assembled) */
  unsigned char haveToldReq[ROUNDS][NACT]; /* content delivered while retained: the
                                            * machine SHOULD have been told (check I) */
  unsigned char preDelivered[ROUNDS][NACT]; /* content in hand before this round closed */
  unsigned char preHave[ROUNDS][BS]; /* pre-close accrued have bitmap for Complete */
  struct bracha87Retry cur[ROUNDS];
  unsigned char comp[ROUNDS][COMPLEN];
  unsigned char closed[ROUNDS];
  unsigned char released[ROUNDS];
  unsigned char cand[COMPLEN];       /* the standing witness candidate */
  unsigned char candValid;
  unsigned char adoptPending;        /* ADOPT output, close still owed */
  unsigned char tolElapsed;          /* this tick's R4 escape input */
  unsigned char self;
  unsigned char serveCursor[sizeof (unsigned long) + 1];
                                     /* systemCursorSz(RS): a round NAME plus
                                      * its in-use byte, so the rotation is
                                      * positioned in the round order itself */
  unsigned char tolFrontier;
  unsigned char retryCursor;         /* which round's instance retries this tick */
  unsigned char partitioned;         /* glue-level classification (the three states):
                                      * SP barren sweeps with no progress.  A
                                      * classified process KEEPS STEPPING. */
  unsigned char activeThisTick;      /* a fresh act cascade landed this tick */
  unsigned char lastFrontier;        /* progress fingerprint: frontier ... */
  unsigned int lastMass;             /* ... and total possession bits held */
  unsigned int barren;               /* consecutive sweeps with no progress */
  unsigned int tolCount;
  unsigned char pendInd[ROUNDS][NACT]; /* frontier-round possession
                                      * indications, held for
                                      * re-presentation at the close */
  unsigned char indArrived[ROUNDS][NACT]; /* GROUND TRUTH the instrument
                                      * can read and a deployment cannot:
                                      * an indication for this round from
                                      * this sender REACHED this process,
                                      * whatever the glue then did with it */
  unsigned int held;                 /* beyond-reach traffic awaiting reach */
#ifdef W_SERVEWALK
  unsigned int serveDuty;            /* the caller's rotation over the
                                      * (wanting process, round) duty space */
#endif
#ifdef W_SERVEPRIO
  unsigned int missionActs;          /* the sequence's own emissions this
                                      * tick -- what recovery yields to */
#endif
#ifdef W_SERVEPRIO
  unsigned char granted2[NACT][ROUNDS]; /* duties granted THIS tick -- a want
                                      * clears on possession, never on the
                                      * grant, so the oracle must know */
#endif
#ifdef W_SERVE_NORESUME
  unsigned char serveDropped[NACT][ROUNDS]; /* duties this glue retired for
                                      * lack of wire -- the withdrawal */
#endif
#ifdef W_A9_SYBIL
  unsigned int sybil;                /* A9 withdrawal: recorded-act counter */
#endif
  struct wire hold[HOLDCAP];
};

struct stats {
  unsigned long tailsFed;
  unsigned long tailsDropped;
  unsigned long tailsToInstance;
  unsigned long tailsDelivered;
  unsigned long heldQueued;
  unsigned long heldConsumed;
  unsigned long heldDroppedProc[NACT]; /* per-process -- an accepted strand's
                                        * beyond-reach overflow is excluded */
  unsigned long classifications;     /* PARTITIONED classifications this run */
#ifdef W_SERVEWIRE
  unsigned long wireStarved;         /* ticks the sequence took the whole
                                      * budget and recovery got nothing */
  unsigned long wireFreed;           /* ticks it did not -- the relief the
                                      * self-funding claim rests on */
  unsigned long wireRetired;         /* THE ORACLE: wants left unserved on a
                                      * tick that had capacity to spare */
#endif
#ifdef W_SERVEPRIO
  unsigned long grantsMade;          /* serve grants the order allowed */
  unsigned char yieldDenied[NACT];   /* GROUND TRUTH the instrument can read
                                      * and a deployment cannot: this
                                      * process's serves were withheld by
                                      * the discharge order, not lost */
#endif
  unsigned long serveMsgs;
  unsigned long exchAccepts;
  unsigned long exchDelivers;
  unsigned long injSite[INJ_SITES];  /* per-site injection count (oracle-excluded) */
  unsigned long witnessFed;
  unsigned long witnessUnbound;
  unsigned long adopts;
  unsigned long releasesAllN;
  unsigned long releasesStructural;
  unsigned long blockedTicks;
  unsigned long launchWhileBlocked;
  unsigned long launchUnderShed;
#ifdef M_SEAM_FREE
  unsigned long launchRogue;         /* M_SEAM_FREE's OWN self-report, kept
                                      * apart from D's arms so the log says
                                      * which one caught the mutant */
#endif
  unsigned long closeNoAdvance;
  unsigned long releaseUnsafe;
  unsigned long compMismatch;
  unsigned long dutyTolerance;       /* TOLERANCE duty reads over the run --
                                      * unreachable at t=0 (n-t is n) */
  unsigned long byzForged;           /* possession bits forged on egress */
  unsigned long byzWithheld;         /* possession bits withheld on egress */
  unsigned long byzFabServed;        /* fabricated compositions that reached a
                                      * CORRECT process's witness path */
  unsigned long byzEquivocated;      /* A-Cast INITIALs sent with a split value */
  unsigned long byzWrongContent;     /* mis-tagged sidecars the XOR tag caught
                                      * and dropped -- detected as loss */
#ifdef W_A9_SYBIL
  unsigned long wSybil;              /* acts recorded under a false sender */
#endif
#ifdef W_SERVEFLOOD
  unsigned long wFlood;              /* blind re-offers that landed as want
                                      * evidence at a correct process */
  unsigned long wFloodDuty;          /* the most SERVE duties the solicitor
                                      * ever held at one server at one time --
                                      * the quantity that decides whether a
                                      * flood can crowd a cap of t at all */
  unsigned long wHonestDuty;         /* the same for correct wanters */
#endif
#ifdef W_L2_NOBYTEMATCH
  unsigned long wNoMatch;            /* served assertions counted toward a
                                      * candidate they are NOT byte-identical
                                      * to -- the withdrawn clause reached */
#endif
#ifdef W_L2_NOREARM
  unsigned long wNoRearm;            /* candidate switches that KEPT the book */
#endif
#ifdef W_L2_NOCLOSEVOID
  unsigned long wNoCloseVoid;        /* own closes that left an unconsumed
                                      * ADOPT standing -- the race the
                                      * withdrawn clause is about, reached */
#endif
#ifdef W_I10_WRONGARTIFACT
  unsigned long wStale;              /* closes that stored an artifact the
                                      * close did not name */
#endif
#ifdef W_A6_PIN1
  unsigned long tolUnearned;         /* ticks the pin reported the tolerance
                                      * elapsed where the honest gate would
                                      * still have been counting -- the
                                      * withdrawal actually reaching the
                                      * machine, not merely compiled in */
#endif
  unsigned long ticks;
  unsigned char closedGlobal[ROUNDS];
  unsigned char compRef[ROUNDS][COMPLEN];
  unsigned char compRefSet[ROUNDS];
  unsigned char converged;
  /* posture ground truth, captured before teardown (the sys is freed
   * before assertRun runs) */
  unsigned int numClassified;        /* processes classified PARTITIONED */
  unsigned int numAccepted;          /* of those, exhibiting the STRAND SHAPE */
  unsigned char classified[NACT];    /* classified flag per process */
  unsigned char accepted[NACT];      /* strand-shaped (excluded from quantifiers) */
  unsigned char clFrontier[NACT];    /* frontier at end (for the classification dump) */
  unsigned char pClosed[NACT][ROUNDS]; /* per-process closure, for check C */
  unsigned char pHaveTok[NACT][ROUNDS][NACT];  /* ground-truth content held (check I) */
  unsigned char pHaveTold[NACT][ROUNDS][NACT]; /* machine told of it (check I machine arm) */
  unsigned char pHaveToldReq[NACT][ROUNDS][NACT]; /* machine SHOULD have been told */
};

struct run {
  struct proc p[NACT];
  struct stats st;
  unsigned long acsSz;
  unsigned long legSz;
  unsigned long exchSz;
  unsigned int dropPercent;
  int lagProc;                       /* -1 = none */
  int lagRound;
  int indStarve;                     /* round whose possession indications
                                      * are dropped to lagProc; -1 = none.
                                      * Starves the OTHER carrier, so the
                                      * victim holds the round and can
                                      * never learn that anyone else does. */
  int byzProc;                       /* the live Byzantine participant, -1 = none */
  unsigned int byzMode;              /* BYZ_* mask; 0 with no participant */
};

/* ------------------------------------------------------------------ */
/*  the linkage fold: a round's anchor names its predecessor          */
/* ------------------------------------------------------------------ */

static void
fold(
  unsigned char *out                 /* ANCHOR bytes */
 ,const unsigned char *prev          /* ANCHOR bytes */
 ,const unsigned char *members       /* NACT bytes */
){
  unsigned long h;
  unsigned int i;

  h = 2166136261UL;
  for (i = 0; i < ANCHOR; ++i) {
    h ^= prev[i];
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
  }
  for (i = 0; i < NACT; ++i) {
    h ^= members[i];
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
  }
  out[0] = (unsigned char)(h & 0xFF);
  out[1] = (unsigned char)((h >> 8) & 0xFF);
  out[2] = (unsigned char)((h >> 16) & 0xFF);
  out[3] = (unsigned char)((h >> 24) & 0xFF);
}

/* wrapping distance ahead of the frontier: 0 = the frontier itself,
 * 1..127 ahead (beyond reach), 128..255 behind (retained territory) */
static unsigned char
aheadBy(
  unsigned char round
 ,unsigned char frontier
){
  return ((unsigned char)(round - frontier));
}

/* ------------------------------------------------------------------ */
/*  forward declarations -- the glue calls back into itself: applying  */
/*  a machine act can launch a round, whose outputs can complete it,  */
/*  which closes through the machine again.                           */
/* ------------------------------------------------------------------ */

static void applySysActs(struct run *, struct proc *, struct systemAct *,
                         unsigned int, const struct wire *, unsigned int);
static void launchRound(struct run *, struct proc *, unsigned char);
static void emitAcs(struct run *, struct proc *, unsigned char,
                    const struct bkr94acsAct *, unsigned int);
static void sysClose(struct run *, struct proc *, unsigned char,
                     const unsigned char *);
static struct leg *findLeg(struct proc *, unsigned char, unsigned char,
                           unsigned char);
static struct leg *allocLegInit(struct run *, struct proc *, unsigned char,
                                unsigned char, unsigned char);
static void legReleaseRound(struct proc *, unsigned char);
static void emitOneAct(struct run *, struct proc *, struct leg *,
                       unsigned char);
static void legAccept(struct run *, struct proc *, struct leg *);
static void legBirthServer(struct run *, struct proc *, unsigned char,
                           unsigned char);
static unsigned char exchTok(unsigned char, unsigned char);
static struct exch *findExch(struct proc *, unsigned char, unsigned char);
static struct exch *allocExchInit(struct run *, struct proc *, unsigned char,
                                  unsigned char);
static void emitExchAct(struct run *, struct proc *, struct exch *,
                        unsigned char);
static void exchDeliver(struct run *, struct proc *, struct exch *);
static void exchBirthInitiator(struct run *, struct proc *, unsigned char);

/* ------------------------------------------------------------------ */
/*  egress: one act of round 'round' becomes wires to every process   */
/*  the BPR suppress mask has not retired, minus the lossy ones.      */
/* ------------------------------------------------------------------ */

static void
pushWire(
  struct run *r
 ,struct wire *w
){
  /* the mute liar: present, stepping, and emitting nothing to anyone.
   * Its self-feed still rides the queue (in a deployment the self-feed
   * is direct and never a wire at all), so its own machinery lives; what
   * dies is every byte it owes the cohort.  Ahead of the loss draw --
   * a message never sent consumes no schedule. */
  if (r->byzProc >= 0
   && (r->byzMode & BYZ_SILENT)
   && (int)w->from == r->byzProc
   && w->to != w->from)
    return;
#ifdef W_A4_PARTITION
  /* A4 WITHDRAWN, for one process and for the whole run: every wire TO and
   * FROM it is lost.  The self-feed is excepted for the same reason the
   * mute liar's is -- in a deployment it is not a wire at all -- so what
   * dies is the transport obligation and nothing else.  Ahead of the loss
   * draw: a message never sent consumes no schedule. */
  if (w->to != w->from
   && ((unsigned int)w->to == WVICT || (unsigned int)w->from == WVICT))
    return;
#endif
  if (r->dropPercent && (rngNext() % 100) < r->dropPercent)
    return;
  if (w->kind == WK_ACS
   && r->lagProc >= 0
   && (int)w->to == r->lagProc
   && (int)w->sysRound == r->lagRound
   && w->to != w->from)
    return;
  if (w->kind == WK_ACS
   && r->lagProc >= 0
   && r->indStarve >= 0
   && (int)w->to == r->lagProc
   && (int)w->sysRound == r->indStarve
   && w->possesses
   && w->to != w->from)
    return;
  qPush(w);
}

static void
emitAcs(
  struct run *r
 ,struct proc *p
 ,unsigned char round
 ,const struct bkr94acsAct *acts
 ,unsigned int n
){
  struct wire w;
  unsigned int i, j;
  unsigned char comp[COMPLEN];

  for (i = 0; i < n; ++i) {
    switch (acts[i].act) {

    case BKR94ACS_ACT_ACAST_SEND:
    case BKR94ACS_ACT_BA_SEND:
#ifdef W_SERVEPRIO
      /* the sequence's own carrier put something on the wire this tick --
       * the first half of what recovery yields to */
      ++p->missionActs;
#endif
      for (j = 0; j < NACT; ++j) {
        if (acts[i].skip && BRACHA87_SKIP_TST(acts[i].skip, j))
          continue;
        memset(&w, 0, sizeof (w));
        w.kind = WK_ACS;
        w.to = (unsigned char)j;
        w.from = p->self;
        w.sysRound = round;
        w.process = acts[i].process;
        w.type = acts[i].type;
        w.accepted = acts[i].accepted;
        /* the possession indication rides this round's traffic once
         * we hold the round's composition -- the tails of a closed
         * round are exactly what carries it (system.h, the analog of
         * the ACCEPTED bit riding a READY) */
        w.possesses = (unsigned char)(round < ROUNDS && p->closed[round]);
        if (r->byzProc >= 0 && (int)p->self == r->byzProc) {
          /* the two possession liars, the receiver-side M_SEAM_OVERCLAIM
           * read from the SENDER's end: one claims a round it may not
           * hold, one refuses to admit the round it does */
          if (r->byzMode & BYZ_FORGE_POSSESS) {
            if (!w.possesses)
              ++r->st.byzForged;
            w.possesses = 1;
          }
          /* the flooding solicitor's possession half: it never admits holding
           * anything, which is what makes its want bits persist */
          if (r->byzMode & (BYZ_WITHHOLD | BYZ_WANT_FLOOD)) {
            if (w.possesses)
              ++r->st.byzWithheld;
            w.possesses = 0;
          }
        }
        if (acts[i].act == BKR94ACS_ACT_ACAST_SEND) {
          w.cls = BKR94ACS_CLS_ACAST;
          if (acts[i].value) {
            memcpy(w.value, acts[i].value, VLENACT);
            /* the equivocator A-Casts a DIFFERENT value to each parity
             * class of destinations.  It speaks as itself throughout --
             * what contains it is Bracha Lemma 2, inherited by the
             * A-Cast leg of bkr94acs, not attribution */
            if (r->byzProc >= 0
             && (r->byzMode & BYZ_EQUIVOCATE_VALUE)
             && (int)p->self == r->byzProc
             && acts[i].process == p->self
             && acts[i].type == BRACHA87_INITIAL
             && (j & 1)) {
              w.value[0] ^= 0x40;
              ++r->st.byzEquivocated;
            }
          }
        } else {
          w.cls = BKR94ACS_CLS_BA;
          w.baRound = acts[i].round;
          w.initiator = acts[i].initiator;
          w.baValue = acts[i].baValue;
        }
        pushWire(r, &w);
      }
      break;

    case BKR94ACS_ACT_COMPLETE:
      /* the round's result is held: build its composition and close
       * through the machine -- unless adoption already closed it (the
       * one consume region supersedes) */
      if (p->closed[round])
        break;
      {
        unsigned char members[NACT];
        unsigned int cnt, k;

        cnt = bkr94acsSubset(p->acs[round], members);
        memset(comp, 0, sizeof (comp));
        for (k = 0; k < cnt; ++k)
          if (members[k] < NACT)
            comp[ANCHOR + members[k]] = 1;
        fold(comp, round ? p->comp[round - 1] : Genesis, comp + ANCHOR);
      }
      sysClose(r, p, round, comp);
      break;

    case BKR94ACS_ACT_BA_EXHAUSTED:
      fprintf(stderr, "FATAL [%s]: BA exhausted (process %u round %u)\n",
              CurTest, (unsigned int)p->self, (unsigned int)round);
      abort();

    default:
      break;
    }
  }
}

/* ------------------------------------------------------------------ */
/*  launch: the machine said run this round's instance                */
/* ------------------------------------------------------------------ */

static void
launchRound(
  struct run *r
 ,struct proc *p
 ,unsigned char round
){
  struct bkr94acsAct out[1];
  unsigned char val[VLENACT];
  unsigned int n;

  if (round >= ROUNDS)
    return;
  if (!p->acs[round]) {
    if (!(p->acs[round] = calloc(1, r->acsSz))) {
      fprintf(stderr, "FATAL [%s]: out of memory\n", CurTest);
      abort();
    }
    bkr94acsInit(p->acs[round], NENC, TVAL, VLENENC, MAXPHASES,
                 p->self, seamCoin, 0);
    bracha87RetryInit(&p->cur[round]);
  }
  val[0] = p->self;
  val[1] = round;
  n = bkr94acsAcast(p->acs[round], val, out);
  emitAcs(r, p, round, out, n);
  /* content distribution is a continuation of Phase A: the member births
   * its own exchange as it launches the round, so the O2 grain is in
   * flight alongside the ACS and largely in hand by the time the round
   * closes (before it can release) */
  exchBirthInitiator(r, p, round);
}

/* ------------------------------------------------------------------ */
/*  close: the one consume region, driven by own COMPLETE or adoption */
/* ------------------------------------------------------------------ */

static void
sysClose(
  struct run *r
 ,struct proc *p
 ,unsigned char round
 ,const unsigned char *comp
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  unsigned char saName[SYSTEM_MAX_ACTS][sizeof (unsigned long)];
  unsigned long before, arg;
  unsigned int n, live, sni;

  before = rv(systemFrontier(p->sys));
#ifdef M_SEAM_STALE
  /* the superseded round: after a relaunch the glue closes with the
   * round the stale instance was running, not the frontier */
  arg = (unsigned char)(before ? before - 1 : 0);
#else
  arg = round;
#endif
  /* the have bitmap the close carries (O2, the pre-close grain): in-subset
   * members whose content is already in hand -- self's own, plus any
   * exchange delivered while this process was still running the round */
  if (round < ROUNDS) {
    unsigned int mm;

    memset(p->preHave[round], 0, BS);
    for (mm = 0; mm < NACT; ++mm)
      if (comp[ANCHOR + mm]
       && (mm == p->self || p->preDelivered[round][mm]))
        p->preHave[round][mm >> 3] |= (unsigned char)(1 << (mm & 7));
  }
  live = systemLive(p->sys);
  n = systemComplete(p->sys, rn(arg), rn(arg + 1),
                     round < ROUNDS ? p->preHave[round] : 0, sa);
  if (rv(systemFrontier(p->sys)) == before) {
    /* a close refused with an instance live is the round argument
     * being rejected -- the property H is about.  With nothing live
     * the call is documented inert and says nothing. */
    if (live)
      ++r->st.closeNoAdvance;
    return;
  }
  /* consume the borrows NOW: an act's round name lives in machine
   * storage only until the next call into the library (system.h),
   * and the pendF re-present below calls back in before these acts
   * are applied -- under the old ordinal API the round was a copied
   * value and this ordering was harmless */
  for (sni = 0; sni < n && sni < SYSTEM_MAX_ACTS; ++sni) {
    memcpy(saName[sni], sa[sni].round, RS);
    sa[sni].round = saName[sni];
  }
#ifndef W_L2_NOCLOSEVOID
  p->adoptPending = 0;
#else
  /* C6's COMPLETION-VOID CLAUSE WITHDRAWN, and only it: "a close consuming
   * the round voids any unconsumed ADOPT for it".  The debt now SURVIVES
   * this process's own COMPLETE, so the next discharge opportunity JOINs the
   * NEW frontier and closes it with the STALE candidate -- round R+1 closed
   * with round R's bytes.  The machine cannot refuse it: the close names the
   * current frontier, and L3 commutes MACHINE state only.  The RESET-void
   * clause (legAccept, M_SEAM_NOVOID's site) is UNTOUCHED, so a candidate
   * switch still voids the debt; only the completion analog is gone.  This
   * counts the times the withdrawn clause would have fired -- an unconsumed
   * ADOPT standing at a successful own-close.  (The adopt-discharge path
   * clears the flag before it calls in, so nothing here double-counts.) */
  if (p->adoptPending)
    ++r->st.wNoCloseVoid;
#endif
  if (round < ROUNDS) {
#ifdef W_I10_WRONGARTIFACT
    /* I10's CALLER HALF WITHDRAWN (the C7 seam pin).  The machine binds the
     * BYTE -- it retains exactly the pre-advance frontier -- but "the entry
     * holds round R's actual result" is the caller's half of the conjunct,
     * and here the glue stores the STANDING CANDIDATE instead of the
     * composition the close consumed.  The two differ only where a served
     * assertion for the frontier round is standing while an own COMPLETE
     * closes it, so this is a reachability question, not a construction
     * one; wStale is the coverage counter that answers it. */
    if (p->candValid && memcmp(p->cand, comp, COMPLEN)) {
      ++r->st.wStale;
      memcpy(p->comp[round], p->cand, COMPLEN);
    } else
      memcpy(p->comp[round], comp, COMPLEN);
#else
    memcpy(p->comp[round], comp, COMPLEN);
#endif
    p->closed[round] = 1;
    /* GROUND TRUTH is the CORRECT cohort's alone.  A Byzantine process
     * closes its own rounds honestly here (it lies only on egress), but
     * nothing about its state may be believed, so its closes enter
     * neither the reference composition, the sequence-identity count, nor
     * the closed-everywhere ledger the R4 and release-safety arms read. */
    /* the ledger reads what the process STORED, not what the close was
     * handed -- the two are the same byte for byte unless I10's caller half
     * is withdrawn, and reading the store is what makes that withdrawal
     * observable at its own round instead of one fold later */
    if (!BYZSELF(r, p->self)) {
      ++r->st.closedGlobal[round];
      if (!r->st.compRefSet[round]) {
        memcpy(r->st.compRef[round], p->comp[round], COMPLEN);
        r->st.compRefSet[round] = 1;
      } else if (memcmp(r->st.compRef[round], p->comp[round], COMPLEN))
        ++r->st.compMismatch;
    }
    {
      unsigned int mm;

      for (mm = 0; mm < NACT; ++mm)
        if (comp[ANCHOR + mm]) {
          if (mm == p->self) {
            p->haveTok[round][p->self] = exchTok(round, p->self);
            p->haveTold[round][p->self] = 1;
            p->haveToldReq[round][p->self] = 1;
          } else if (p->preDelivered[round][mm]) {
            p->haveTold[round][mm] = 1;   /* haveTok set at delivery */
            p->haveToldReq[round][mm] = 1;
          }
        }
    }
  }
  /* the round is retained now: re-present the indications that arrived
   * while it had no record */
  {
    unsigned int pj;

    for (pj = 0; pj < NACT; ++pj) {
      struct systemAct pa[SYSTEM_MAX_ACTS];
      unsigned int pn;

      if (pj == p->self || !p->pendInd[round][pj])
        continue;
      p->pendInd[round][pj] = 0;
      if (!systemRetained(p->sys, rn(round)))
        break;                     /* released underneath us */
      pn = systemPossessed(p->sys, rn(round), (unsigned char)pj, pa);
      applySysActs(r, p, pa, pn, 0, 0);
    }
  }
#ifdef W_L2_NOCLOSEVOID
  /* WITHDRAWING THE CLAUSE, NOT A VARIABLE.  The unconsumed ADOPT is two
   * pieces of caller state -- the debt flag and the candidate the debt would
   * close on -- and procTick discharges only on BOTH.  Clearing the candidate
   * beside the surviving flag would void the ADOPT by the back door and make
   * the withdrawal one in name only.  Where no debt survives, this is the
   * baseline's own clear, byte for byte. */
  if (!p->adoptPending)
    p->candValid = 0;
#else
  p->candValid = 0;
#endif
  p->tolCount = 0;
  /* a release out of the completion path is eviction-class -- the
   * retained set's oldest, or the round-space wrap boundary -- never the
   * all-n release that release safety speaks about */
  applySysActs(r, p, sa, n, 0, 1);
}

/* ------------------------------------------------------------------ */
/*  apply the machine's output actions -- the executive half of the    */
/*  glue.  'w' is the wire that provoked them, when there was one.    */
/* ------------------------------------------------------------------ */

static void
applySysActs(
  struct run *r
 ,struct proc *p
 ,struct systemAct *sa
 ,unsigned int n
 ,const struct wire *w
 ,unsigned int structural            /* 1 = releases here are eviction-class */
){
  struct bkr94acsAct out[ACSACTS];
  unsigned int i, m;

  for (i = 0; i < n; ++i)
    switch (sa[i].act) {

    case SYSTEM_ACT_DELIVER:
      if (!w || w->kind != WK_ACS)
        break;
      if (rv(sa[i].round) != rv(systemFrontier(p->sys)))
        ++r->st.tailsDelivered;
      if (!p->acs[rv(sa[i].round)])
        break;
      if (w->cls == BKR94ACS_CLS_ACAST) {
        m = bkr94acsAcastInput(p->acs[rv(sa[i].round)], w->process, w->type,
                               w->from, w->value, out);
        if (w->accepted)
          bkr94acsAcastAccepted(p->acs[rv(sa[i].round)], w->process, w->from);
      } else {
        m = bkr94acsBaInput(p->acs[rv(sa[i].round)], w->process, w->baRound,
                            w->initiator, w->type, w->from, w->baValue, out);
        if (w->accepted)
          bkr94acsBaAccepted(p->acs[rv(sa[i].round)], w->process, w->baRound,
                             w->initiator, w->from);
      }
      if (m)
        p->activeThisTick = 1;         /* a fresh cascade -- dedup returns 0 */
      emitAcs(r, p, rv(sa[i].round), out, m);
      /* the sweep-side decisions at zero tolerance budget -- the eager
       * tempo, preserved so this instrument's records stay comparable;
       * a WAN-grade budget belongs on the sweep.  Turns first (only a
       * turn produces the decisions the fanout counts; a fanout cannot
       * make a round turnable), drained per BA and over every process:
       * cascade unlocks successive rounds of one BA and an A-Cast
       * accept opens round 0 of another.  The instance pointer is
       * re-read each turn -- a release inside emitAcs frees it, and
       * bkr94acsTurn reads a freed slot as HELD */
      {
        unsigned int j;

        for (j = 0; j < NACT; ++j)
          while ((m = bkr94acsTurn(p->acs[rv(sa[i].round)], (unsigned char)j,
                                   1, out)) > 0) {
            p->activeThisTick = 1;
            emitAcs(r, p, rv(sa[i].round), out, m);
          }
      }
      m = bkr94acsFanout(p->acs[rv(sa[i].round)], out);
      if (m)
        p->activeThisTick = 1;
      emitAcs(r, p, rv(sa[i].round), out, m);
      break;

    case SYSTEM_ACT_SERVE:
      /* each still-owed process gets a recovery leg -- a two-process Fig1
       * (this server the initiator, t = 0) whose INITIAL carries the
       * composition; BPR rides the instance */
      if (rv(sa[i].round) >= ROUNDS || !p->closed[rv(sa[i].round)])
        break;
#ifdef W_SERVEWALK
      /* the bounded serve walk in procTick is the SOLE discharge under the
       * SERVE-bounds arms: the cap and the rotation are what they withdraw,
       * so the unbounded immediate discharge cannot stand beside them */
#else
      {
        unsigned int j;

        for (j = 0; j < NACT; ++j) {
          if (j == p->self || !sa[i].want || !SYSTEM_TST(sa[i].want, j))
            continue;
          legBirthServer(r, p, (unsigned char)j, rv(sa[i].round));
        }
      }
#endif
      break;

    case SYSTEM_ACT_RELEASE:
      if (rv(sa[i].round) < ROUNDS) {
        if (structural) {
          p->released[rv(sa[i].round)] = 2;
          ++r->st.releasesStructural;
        } else {
          p->released[rv(sa[i].round)] = 1;
          ++r->st.releasesAllN;
          if (r->st.closedGlobal[rv(sa[i].round)] < NCORRECT(r))
            ++r->st.releaseUnsafe;
        }
        if (p->acs[rv(sa[i].round)]) {
          free(p->acs[rv(sa[i].round)]);
          p->acs[rv(sa[i].round)] = 0;
        }
        legReleaseRound(p, rv(sa[i].round));
      }
      break;

    case SYSTEM_ACT_ADOPT:
      /* the close needs a live instance, and with none the caller
       * JOINs first -- the recovery traffic recorded owed (system.h,
       * SYSTEM_ACT_ADOPT).  ADOPT is single-fire, so the debt is
       * carried until the join's own R4 gate opens rather than
       * dropped here. */
      if (!p->candValid)
        break;
      ++r->st.adopts;
      p->adoptPending = 1;
      break;

    case SYSTEM_ACT_JOIN:
    case SYSTEM_ACT_ADMIT:
    case SYSTEM_ACT_MAINTAIN:
      /* R4 read against ground truth, not against the same signal
       * the launch gated on: no advance may outrun SHEDFLOOR correct
       * processes having actually closed the prior round.  A liar's own
       * advance is not the cohort's, so it is not read at all. */
      if (rv(sa[i].round)
       && !BYZSELF(r, p->self)
       && r->st.closedGlobal[rv(sa[i].round) - 1] < SHEDFLOOR(r))
        ++r->st.launchUnderShed;
      launchRound(r, p, rv(sa[i].round));
      break;

    default:
      break;
    }
}

/* ------------------------------------------------------------------ */
/*  recovery legs: the SERVE discharge, riding real Fig1 2/0 traffic  */
/* ------------------------------------------------------------------ */

static struct leg *
findLeg(
  struct proc *p
 ,unsigned char server
 ,unsigned char served
 ,unsigned char round
){
  unsigned int i;

  for (i = 0; i < LEGCAP; ++i)
    if (p->legs[i].inUse
     && p->legs[i].server == server
     && p->legs[i].served == served
     && p->legs[i].round == round)
      return (&p->legs[i]);
  return (0);
}

static struct leg *
allocLegInit(
  struct run *r
 ,struct proc *p
 ,unsigned char server
 ,unsigned char served
 ,unsigned char round
){
  struct leg *lg;
  unsigned int i;

  lg = 0;
  for (i = 0; i < LEGCAP; ++i)
    if (!p->legs[i].inUse) {
      lg = &p->legs[i];
      break;
    }
  if (!lg)
    return (0);
  if (!(lg->f1 = calloc(1, r->legSz))) {
    fprintf(stderr, "FATAL [%s]: out of memory\n", CurTest);
    abort();
  }
  bracha87Fig1Init(lg->f1, 1, 0, COMPLEN - 1);
  lg->server = server;
  lg->served = served;
  lg->round = round;
  lg->inUse = 1;
  lg->selfAcc = 0;
  lg->otherAcc = 0;
  lg->retired = 0;
  return (lg);
}

static void
legReleaseRound(
  struct proc *p
 ,unsigned char round
){
  unsigned int i;

  for (i = 0; i < LEGCAP; ++i)
    if (p->legs[i].inUse && p->legs[i].round == round) {
      free(p->legs[i].f1);
      p->legs[i].f1 = 0;
      p->legs[i].inUse = 0;
    }
}

/* one Fig1 send action becomes wires to the leg's two ends (server = 0,
 * served = 1), minus the ones the suppress mask has retired.  Self-feed
 * rides the queue exactly as ACS traffic does. */
static void
emitOneAct(
  struct run *r
 ,struct proc *p
 ,struct leg *lg
 ,unsigned char act
){
  struct wire w;
  const unsigned char *val;
  const unsigned char *skip;
  unsigned char type;
  unsigned int j;

  switch (act) {
  case BRACHA87_INITIAL_ALL: type = BRACHA87_INITIAL; break;
  case BRACHA87_ECHO_ALL:    type = BRACHA87_ECHO;    break;
  case BRACHA87_READY_ALL:   type = BRACHA87_READY;   break;
  default:                   return;
  }
  val = bracha87Fig1Value(lg->f1);
  skip = bracha87Fig1Skip(lg->f1, act);
  for (j = 0; j < 2; ++j) {
    if (skip && BRACHA87_SKIP_TST(skip, j))
      continue;
    memset(&w, 0, sizeof (w));
    w.kind = WK_SERVE;
    w.to = j == 0 ? lg->server : lg->served;
    w.from = p->self;
    w.sysRound = lg->round;
    w.legServer = lg->server;
    w.legServed = lg->served;
    w.type = type;
    if (type == BRACHA87_READY)
      w.accepted = lg->selfAcc;
    if (val)
      memcpy(w.comp, val, COMPLEN);
    pushWire(r, &w);
  }
}

/* the leg's ACCEPT: the served end delivers the composition exactly
 * where the old bare serve message was consumed (systemWitness path);
 * the server end only records the self-accept for quiescence. */
static void
legAccept(
  struct run *r
 ,struct proc *p
 ,struct leg *lg
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  const unsigned char *vp;
  unsigned char val[COMPLEN];
  unsigned char selfIdx;
  unsigned char srv;
  unsigned long f;
  unsigned int n;

  selfIdx = (unsigned char)(p->self == lg->server ? 0 : 1);
  lg->selfAcc = 1;
  bracha87Fig1ProcessAccepted(lg->f1, selfIdx);
  if (p->self != lg->served)
    return;
  if (!(vp = bracha87Fig1Value(lg->f1)))
    return;
  memcpy(val, vp, COMPLEN);
  /* a fabricated serving that REACHED a correct process's witness path.
   * Reaching it is not the failure -- t forgers reach at most t, and the
   * byte-identical grouping the caller half of L2 pins is what keeps the
   * count from ever latching an adoption on a mixed set. */
  if (r->byzProc >= 0
   && (int)lg->server == r->byzProc
   && !BYZSELF(r, p->self)
   && lg->round < ROUNDS
   && r->st.compRefSet[lg->round]
   && memcmp(val, r->st.compRef[lg->round], COMPLEN))
    ++r->st.byzFabServed;
  f = rv(systemFrontier(p->sys));
  /* the SERVER of the served fact -- A9 again, on the O3 witness plane
   * this time: the distinct-server count L2 consumes is only as good as
   * the attribution that names each server. */
  srv = lg->server;
#ifdef W_A9_SYBIL
  if (!(++p->sybil % W_SYBIL_EVERY)) {
    srv = (unsigned char)((lg->server + 1) % NACT);
    if (srv == p->self)
      srv = (unsigned char)((lg->server + 2) % NACT);
    ++r->st.wSybil;
  }
#endif
#ifdef M_SEAM_UNBOUND
  /* content alone, round tag ignored */
  if (!p->candValid || memcmp(p->cand, val, COMPLEN)) {
    systemWitnessReset(p->sys);
#ifndef M_SEAM_NOVOID
    p->adoptPending = 0;
#endif
    memcpy(p->cand, val, COMPLEN);
    p->candValid = 1;
  }
  if (lg->round != f)
    ++r->st.witnessUnbound;
  ++r->st.witnessFed;
  n = systemWitness(p->sys, rn(f), srv, sa);
  applySysActs(r, p, sa, n, 0, 0);
#else
  if (lg->round == f) {
#ifdef W_L2_NOBYTEMATCH
    /* C6's BYTE-MATCHING CLAUSE WITHDRAWN, and only it.  The machine counts
     * DISTINCT SERVERS and nothing else; grouping the assertions by content
     * is the caller's half of L2, and without it every frontier-round served
     * assertion counts toward whatever candidate happens to be standing --
     * whatever its bytes.  The re-arm and void clauses are not withdrawn
     * here, they are UNREACHABLE: with no byte comparison there is no switch
     * to re-arm on.  The candidate is whatever arrived first. */
    if (!p->candValid) {
      memcpy(p->cand, val, COMPLEN);
      p->candValid = 1;
    } else if (memcmp(p->cand, val, COMPLEN))
      ++r->st.wNoMatch;
#else
    if (!p->candValid || memcmp(p->cand, val, COMPLEN)) {
      /* C6's RE-ARM CLAUSE: a candidate switch discards the accumulated
       * witnesses, because they attested the OLD candidate.  W_L2_NOREARM
       * withdraws THIS CLAUSE ALONE -- the byte-matching above and the void
       * below both stand -- so the old candidate's witnesses go on counting
       * toward the new one. */
#ifdef W_L2_NOREARM
      if (p->candValid)
        ++r->st.wNoRearm;
#else
      systemWitnessReset(p->sys);
#endif
      /* C6 (system.md Mechanization status): a reset voids any
       * unconsumed ADOPT -- the debt's witnesses attested the OLD
       * candidate, and a close consuming the switched bytes is a
       * false adoption.  M_SEAM_NOVOID reinstates the omission; it
       * is C6's matched red (fires E + the BYZ MIXED arm). */
#ifndef M_SEAM_NOVOID
      p->adoptPending = 0;
#endif
      memcpy(p->cand, val, COMPLEN);
      p->candValid = 1;
    }
#endif
    ++r->st.witnessFed;
    n = systemWitness(p->sys, rn(f), srv, sa);
    applySysActs(r, p, sa, n, 0, 0);
  }
#endif
  /* the served evidences its server's possession of the round; kept last
   * so a release it might provoke cannot free the leg under our feet */
  n = systemPossessed(p->sys, rn(lg->round), srv, sa);
  applySysActs(r, p, sa, n, 0, 0);
}

static void
legBirthServer(
  struct run *r
 ,struct proc *p
 ,unsigned char served
 ,unsigned char round
){
  struct leg *lg;
  unsigned char comp[COMPLEN];

  if (round >= ROUNDS || !p->closed[round])
    return;
  if (findLeg(p, p->self, served, round))
    return;
  if (!(lg = allocLegInit(r, p, p->self, served, round)))
    return;
  memcpy(comp, p->comp[round], COMPLEN);
  /* the mixed-candidate server serves the SAME round two ways at once:
   * a byte-flipped variant to even wanters, its true composition to odd
   * ones.  Each wanter has its own leg instance, so the two servings are
   * simultaneous rather than a sequence the witness book could order.
   * The parity runs EVEN-fabricated because the liar itself is process
   * 1: odd-fabricated would leave a single destination at n = 4 and the
   * arm would go vacuous at most seeds. */
  if (r->byzProc >= 0
   && (r->byzMode & BYZ_MIXED_CANDIDATE)
   && (int)p->self == r->byzProc
   && !(served & 1))
    comp[0] ^= 0x5A;
  bracha87Fig1Initiator(lg->f1, comp);
  ++r->st.serveMsgs;
  emitOneAct(r, p, lg, BRACHA87_INITIAL_ALL);
#ifdef M_LEG_LOCALRETIRE
  /* retire on local send progress instead of remote accept -- WRONG: the
   * leg never retries, so the assertion never completes under loss */
  lg->retired = 1;
#endif
}

/* ------------------------------------------------------------------ */
/*  exchange: the O2 content grain, riding real Fig1 n/t broadcasts    */
/* ------------------------------------------------------------------ */

/* a deterministic, nonzero content token for (round, member) -- a token,
 * not real bytes; the seam tests the grain in motion, not the crypto */
static unsigned char
exchTok(
  unsigned char round
 ,unsigned char member
){
  return ((unsigned char)((((unsigned int)round + 1) * 37
                         + ((unsigned int)member + 1) * 7) | 0x01));
}

static struct exch *
findExch(
  struct proc *p
 ,unsigned char round
 ,unsigned char member
){
  unsigned int i;

  for (i = 0; i < EXCHCAP; ++i)
    if (p->exchs[i].inUse
     && p->exchs[i].round == round
     && p->exchs[i].member == member)
      return (&p->exchs[i]);
  return (0);
}

static struct exch *
allocExchInit(
  struct run *r
 ,struct proc *p
 ,unsigned char round
 ,unsigned char member
){
  struct exch *ex;
  unsigned int i;

  ex = 0;
  for (i = 0; i < EXCHCAP; ++i)
    if (!p->exchs[i].inUse) {
      ex = &p->exchs[i];
      break;
    }
  if (!ex)
    return (0);
  if (!(ex->f1 = calloc(1, r->exchSz))) {
    fprintf(stderr, "FATAL [%s]: out of memory\n", CurTest);
    abort();
  }
  bracha87Fig1Init(ex->f1, NENC, TVAL, 0);
  ex->round = round;
  ex->member = member;
  ex->inUse = 1;
  ex->accepted = 0;
  ex->payload = 0;
  ex->havePayload = 0;
  ex->delivered = 0;
  return (ex);
}

/* one Fig1 send action becomes wires to all n on the value plane (the
 * common token); the initiator's INITIAL alone carries the per-
 * destination sidecar (single-sourced, unique per destination). */
static void
emitExchAct(
  struct run *r
 ,struct proc *p
 ,struct exch *ex
 ,unsigned char act
){
  struct wire w;
  const unsigned char *val;
  const unsigned char *skip;
  unsigned char type;
  unsigned char tok;
  unsigned int d;

#ifdef W_SERVEPRIO
  /* the exchange is the live round's other carrier (Relation to a
   * deployment), so it counts as mission beside the round instance */
  ++p->missionActs;
#endif
  switch (act) {
  case BRACHA87_INITIAL_ALL:
    type = BRACHA87_INITIAL;
#ifdef M_EXCH_EARLYRETIRE
    /* retire INITIAL before all-echoed (the forbidden weak gate,
     * bracha87 pitfall 11): once ANY process has echoed, stop -- a
     * non-echoed process never gets its sidecar and never delivers */
    if (!bracha87Fig1AllEchoed(ex->f1)) {
      const unsigned char *ec;
      unsigned int q;

      ec = bracha87Fig1Skip(ex->f1, BRACHA87_INITIAL_ALL);
      for (q = 0; q < NACT; ++q)
        if (ec && BRACHA87_SKIP_TST(ec, q))
          return;
    }
#endif
    break;
  case BRACHA87_ECHO_ALL:  type = BRACHA87_ECHO;  break;
  case BRACHA87_READY_ALL: type = BRACHA87_READY; break;
  default:                 return;
  }
  val = bracha87Fig1Value(ex->f1);
  tok = val ? val[0] : 0;
  skip = bracha87Fig1Skip(ex->f1, act);
  for (d = 0; d < NACT; ++d) {
    if (skip && BRACHA87_SKIP_TST(skip, d))
      continue;
    memset(&w, 0, sizeof (w));
    w.kind = WK_EXCH;
    w.to = (unsigned char)d;
    w.from = p->self;
    w.sysRound = ex->round;
    w.exchMember = ex->member;
    w.type = type;
    w.exchVal = tok;
    if (type == BRACHA87_READY)
      w.accepted = ex->accepted;       /* the all-accepted READY quiescence gate */
    if (type == BRACHA87_INITIAL && p->self == ex->member) {
      w.exchPay = (unsigned char)(tok ^ (unsigned char)(d + 1));
      /* the wrong-content liar mis-tags the sidecar to half its
       * destinations.  The XOR tag exists precisely so the receiver can
       * tell -- the lie must reduce to loss, never to wrong content */
      if (r->byzProc >= 0
       && (r->byzMode & BYZ_WRONG_CONTENT)
       && (int)p->self == r->byzProc
       && (d & 1))
        w.exchPay ^= 0x80;
    }
    pushWire(r, &w);
  }
}

/* content is in hand: this process accepted the value plane AND holds its
 * per-destination sidecar.  Before this process closed the round it rides
 * the have bitmap systemComplete carries; after, the late-assembly
 * ingress systemAssembled.  Ground truth is recorded either way. */
static void
exchDeliver(
  struct run *r
 ,struct proc *p
 ,struct exch *ex
){
  unsigned char content;

  ex->delivered = 1;
  ++r->st.exchDelivers;
  if (ex->round >= ROUNDS)
    return;
  content = (unsigned char)(ex->payload ^ (unsigned char)(p->self + 1));
  p->haveTok[ex->round][ex->member] = content;      /* ground truth, always */
  if (!p->closed[ex->round]) {
    /* pre-close: the subset is not yet fixed; note the delivery and let
     * the close fold in-subset content into its have bitmap */
    p->preDelivered[ex->round][ex->member] = 1;
    return;
  }
  /* post-close: an in-subset member's content is late assembly */
  if (!p->comp[ex->round][ANCHOR + ex->member])
    return;                                          /* not in the subset */
  if (!systemRetained(p->sys, rn(ex->round)))
    return;   /* the round already released -- content assembly is now the
               * O2 out-of-band tail; ground truth holds it, the machine
               * cannot be told (no have grain for a released round) */
  p->haveToldReq[ex->round][ex->member] = 1;
#ifdef M_EXCH_NOASSEMBLE
  /* post-close accepts NOT fed to systemAssembled -- WRONG: the machine
   * have grain goes stale while the content is really in hand */
#else
  systemAssembled(p->sys, rn(ex->round), ex->member);
  p->haveTold[ex->round][ex->member] = 1;
#endif
}

/* the member births its own exchange on closing the round: it owns its
 * content, so it broadcasts the token to all (sidecars per destination)
 * and needs no delivery of its own. */
static void
exchBirthInitiator(
  struct run *r
 ,struct proc *p
 ,unsigned char round
){
  struct exch *ex;
  unsigned char tok;

  if (round >= ROUNDS)
    return;
  if (findExch(p, round, p->self))
    return;
  if (!(ex = allocExchInit(r, p, round, p->self)))
    return;
  tok = exchTok(round, p->self);
  bracha87Fig1Initiator(ex->f1, &tok);
  ex->havePayload = 1;
  ex->delivered = 1;
  emitExchAct(r, p, ex, BRACHA87_INITIAL_ALL);
}

/* ------------------------------------------------------------------ */
/*  ingress: the classification the seam exists to get right          */
/* ------------------------------------------------------------------ */

static void
holdWire(
  struct run *r
 ,struct proc *p
 ,const struct wire *w
){
#ifdef M_SEAM_NOHOLD
  (void)w;
  ++r->st.heldDroppedProc[p->self];
  return;
#else
  if (p->held >= HOLDCAP) {
    ++r->st.heldDroppedProc[p->self];
    return;
  }
  p->hold[p->held++] = *w;
  ++r->st.heldQueued;
#endif
}

static void
deliverWire(
  struct run *r
 ,const struct wire *w
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  struct proc *p;
  unsigned long f;
  unsigned char ab, evFrom;
  unsigned int i, n, delivered;

  p = &r->p[w->to];
  f = rv(systemFrontier(p->sys));
  ab = aheadBy(w->sysRound, f);
  /* THE SENDER OF THE EVIDENCE.  A9 says the caller passes the act's TRUE
   * author here; without a withdrawal arm that is w->from and nothing
   * else.  Kept distinct from the sender the ACS instance is told, which
   * is Bracha's own attribution and not this layer's premise. */
  evFrom = w->from;

#ifdef W_SERVEFLOOD
  /* A BLIND RE-OFFER IS WANT EVIDENCE AND NOTHING ELSE.  The solicitor
   * transmits its offers blind, for every round, modeling no receiver; the
   * RECEIVER drops any that is not behind its own frontier, because such an
   * offer would reach the value plane and that would be a SECOND lie -- and
   * the arm withdraws exactly one premise.  Dropped whole, before any state
   * touch, the verdict-gate shape. */
  if (w->flood) {
    if (ab < 128)
      return;
    ++r->st.wFlood;
  }
#endif

  if (w->kind == WK_SERVE) {
    /* a recovery-leg message.  It is traffic OF ITS ROUND: it must pass
     * the same round classification as any other, and the leg instance
     * consumes it via bracha87Fig1Input.  The server's self-fed and
     * returned traffic sits behind its frontier (retained-round
     * territory); the served process's frontier IS the leg's round. */
    struct leg *lg;
    unsigned char out3[BRACHA87_FIG1_RETRY_MAX_ACTS];
    unsigned char legFrom;
    unsigned int i, m;

    if (!(lg = findLeg(p, w->legServer, w->legServed, w->sysRound))) {
      /* the served end has no leg until the server's first message
       * arrives -- born on demand, and only for its frontier round */
      if (w->to != w->legServed || ab)
        return;
      if (!(lg = allocLegInit(r, p, w->legServer, w->legServed, w->sysRound)))
        return;
    }
    if (lg->retired)
      return;
    if (ab && ab < 128)
      return;                          /* beyond reach */
    if (ab) {
      /* behind the frontier: retained-round leg traffic */
#ifdef M_LEG_MISCLASS
      /* dropped instead of classified as retained-round traffic -- WRONG:
       * the server never consumes its own self-fed INITIAL, so it never
       * echoes and the served end is left one echo short forever */
      return;
#endif
      ;
    }
    legFrom = (unsigned char)(w->from == w->legServer ? 0 : 1);
    if (w->type == BRACHA87_INITIAL && legFrom != 0)
      return;                          /* only the server may send INITIAL */
    m = bracha87Fig1Input(lg->f1, w->type, legFrom, w->comp, out3);
    for (i = 0; i < m; ++i)
      if (out3[i] == BRACHA87_ACCEPT)
        legAccept(r, p, lg);
      else
        emitOneAct(r, p, lg, out3[i]);
    if (w->type == BRACHA87_READY && w->accepted) {
      bracha87Fig1ProcessAccepted(lg->f1, legFrom);
      lg->otherAcc = 1;
    }
    if (lg->selfAcc && lg->otherAcc)
      lg->retired = 1;                 /* two-process all-accepted quiescence */
    return;
  }

  if (w->kind == WK_EXCH) {
    /* an exchange message.  Traffic OF ITS ROUND: behind the frontier it
     * is retained-round traffic (post-close late assembly); at the
     * frontier it accrues toward the have bitmap the close carries.  The
     * value plane (the common token) drives the RB; the per-destination
     * sidecar rides the initiator's INITIAL and is REQUIRED before this
     * process echoes (the presence gate -- honest content; validating it
     * is the deployment's crypto, always-open here). */
    struct exch *ex;
    unsigned char out3[BRACHA87_FIG1_RETRY_MAX_ACTS];
    unsigned char valbuf;
    unsigned int i, m;

    ex = findExch(p, w->sysRound, w->exchMember);
    if (ab && ab < 128)
      return;                          /* beyond reach: BPR redelivers */
    if (!ex) {
      if (p->self == w->exchMember)
        return;                        /* our own initiator instance is gone */
      if (!(ex = allocExchInit(r, p, w->sysRound, w->exchMember)))
        return;
    }
    if (ab) {
      /* behind the frontier: retained-round exchange, late assembly */
#ifdef M_EXCH_MISCLASS
      /* dropped instead of classified as retained-round traffic -- WRONG:
       * a process that closed the round never receives late content and
       * the have grain never completes */
      return;
#endif
      ;
    }
    if (w->type == BRACHA87_INITIAL) {
      if (w->from != w->exchMember)
        return;                        /* only the member may send INITIAL */
      /* THE MISDELIVERY TAG.  The sidecar is XOR'd per destination, so a
       * sidecar meant for someone else -- or mis-tagged by its author --
       * does not untag to the value plane's own token.  Same principle as
       * every verdict gate: INVALID REDUCES TO LOSS, so the whole message
       * goes before any state touch.  Honest traffic never trips it. */
      if ((unsigned char)(w->exchPay ^ (unsigned char)(p->self + 1))
       != w->exchVal) {
        ++r->st.byzWrongContent;
        return;
      }
      if (!ex->havePayload) {
        ex->payload = w->exchPay;
        ex->havePayload = 1;
      }
    }
    if (!ex->havePayload)
      return;                          /* pre-gate: no sidecar yet, drop */
    valbuf = w->exchVal;
    m = bracha87Fig1Input(ex->f1, w->type, w->from, &valbuf, out3);
    for (i = 0; i < m; ++i)
      if (out3[i] == BRACHA87_ACCEPT) {
        if (!ex->accepted)
          ++r->st.exchAccepts;
        ex->accepted = 1;
        bracha87Fig1ProcessAccepted(ex->f1, p->self);
      } else
        emitExchAct(r, p, ex, out3[i]);
    if (w->type == BRACHA87_READY && w->accepted)
      bracha87Fig1ProcessAccepted(ex->f1, w->from);
    if (ex->accepted && ex->havePayload && !ex->delivered)
      exchDeliver(r, p, ex);
    return;
  }

  if (ab && ab < 128) {
    /* beyond chain reach: unverifiable here, held until reach extends */
    holdWire(r, p, w);
    return;
  }

  if (ab) {
    /* behind the frontier: a retained round's traffic -- the tails */
#ifdef M_SEAM_DROP
    ++r->st.tailsDropped;
    return;
#endif
#ifdef M_SEAM_DELIVER
    ++r->st.tailsToInstance;
    if (p->acs[w->sysRound]) {
      struct bkr94acsAct out[ACSACTS];
      unsigned int m;
      if (w->cls == BKR94ACS_CLS_ACAST)
        m = bkr94acsAcastInput(p->acs[w->sysRound], w->process, w->type,
                               w->from, w->value, out);
      else
        m = bkr94acsBaInput(p->acs[w->sysRound], w->process, w->baRound,
                            w->initiator, w->type, w->from, w->baValue, out);
      emitAcs(r, p, w->sysRound, out, m);
    }
    return;
#endif
    ++r->st.tailsFed;
  }

#ifdef W_A9_SYBIL
  /* A9 WITHDRAWN: ingress attribution fails for the EVIDENCE this layer
   * records -- every k-th act is booked against a rotated false sender.
   * The ACS instance below still gets w->from, so this withdraws the
   * SYSTEM layer's attribution premise and not Bracha's. */
  if (!(++p->sybil % W_SYBIL_EVERY)) {
    evFrom = (unsigned char)((w->from + 1) % NACT);
    if (evFrom == p->self)
      evFrom = (unsigned char)((w->from + 2) % NACT);
    ++r->st.wSybil;
  }
#endif
#if defined(M_SEAM_WANT)
  n = systemReceived(p->sys, rn(w->sysRound), evFrom, 0, 0, sa);
#elif defined(M_SEAM_OVERCLAIM)
  /* the opposite error to M_SEAM_WANT: possession claimed for a
   * sender that indicated none */
  n = systemReceived(p->sys, rn(w->sysRound), evFrom, 1, 0, sa);
#else
  n = systemReceived(p->sys, rn(w->sysRound), evFrom, w->possesses, 0, sa);
#endif
  delivered = 0;
  for (i = 0; i < n; ++i)
    if (sa[i].act == SYSTEM_ACT_DELIVER)
      delivered = 1;
  applySysActs(r, p, sa, n, w, 0);

  /* the linkage inference: an authenticated act of round R+1 OR LATER
   * evidences its sender's possession of R's composition (system.h,
   * systemPossessed).  Reading it as R+1 alone strands any round whose
   * immediate successor never reaches us. */
#if !defined(M_SEAM_WANT) && !defined(W_A5_NOINFER)
  for (i = 0; i < ROUNDS; ++i) {
    if (aheadBy((unsigned char)i, w->sysRound) < 128
     || !systemRetained(p->sys, rn((unsigned char)i)))
      continue;
    n = systemPossessed(p->sys, rn((unsigned char)i), evFrom, sa);
    applySysActs(r, p, sa, n, 0, 0);
  }
#endif
/* W_A5_NOINFER withdraws A5 and NOTHING ELSE: the glue stops translating
 * a later-round act into possession evidence, while the indication that
 * rides a round's own traffic (A8's first source) is left exactly as it
 * was.  The two carriers system.h names are thereby separated, and what
 * the indication alone can and cannot carry becomes observable. */

  /* An indication for a round with no record yet is DROPPED by the
   * machine (system.h systemReceived): only retained rounds have one.
   * The drop is not self-correcting -- the O1 inference recovers it only
   * where later-round traffic comes to exist -- so the caller holds it
   * and re-presents it at the close.  Held for the FRONTIER only: that
   * is the sole round a close can retain, and a hold keyed on "not
   * retained" would also catch released rounds, whose entries nothing
   * clears and which resurface at the next incarnation of the byte.
   * The ledger above it is instrument-only and unconditional. */
  if (w->possesses && w->sysRound < ROUNDS && evFrom < NACT) {
    p->indArrived[w->sysRound][evFrom] = 1;
#ifndef M_SEAM_NOPEND
    if (systemRetained(p->sys, rn(w->sysRound))) {
      struct systemAct pa[SYSTEM_MAX_ACTS];
      unsigned int pn;

      pn = systemPossessed(p->sys, rn(w->sysRound), evFrom, pa);
      applySysActs(r, p, pa, pn, 0, 0);
    } else if (w->sysRound == rv(systemFrontier(p->sys)))
      p->pendInd[w->sysRound][evFrom] = 1;
#endif
  }

  /* existence evidence for the frontier with no live instance records
   * participation owed and takes no act -- hold the traffic and
   * re-present it after the join */
  if (!ab && !delivered && !p->closed[w->sysRound])
    holdWire(r, p, w);
}

/* re-feed what reach now covers.  Delivery can hold again -- and can
 * advance the frontier, freeing yet more -- so the queue is taken away
 * whole and what is still out of reach goes back on it. */
static void
refeedHeld(
  struct run *r
 ,struct proc *p
){
  static struct wire snap[HOLDCAP];
  unsigned int i, cnt;
  unsigned char ab;

  if (!(cnt = p->held))
    return;
  memcpy(snap, p->hold, cnt * sizeof (snap[0]));
  p->held = 0;
  for (i = 0; i < cnt; ++i) {
    ab = aheadBy(snap[i].sysRound, rv(systemFrontier(p->sys)));
    if ((ab && ab < 128) || (!ab && !systemLive(p->sys))) {
      holdWire(r, p, &snap[i]);
      continue;
    }
    ++r->st.heldConsumed;
    deliverWire(r, &snap[i]);
  }
}

/* ------------------------------------------------------------------ */
/*  the tick                                                          */
/* ------------------------------------------------------------------ */

static void
procTick(
  struct run *r
 ,struct proc *p
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
  unsigned long f;
  unsigned int duty, tolElapsed, blocked, n, i;

  refeedHeld(r, p);

  f = rv(systemFrontier(p->sys));
  if (p->tolFrontier != f) {
    p->tolFrontier = f;
    p->tolCount = 0;
  }
  duty = systemDuty(p->sys);
  if (duty == SYSTEM_DUTY_TOLERANCE) {
    ++p->tolCount;
    ++r->st.dutyTolerance;
  }
  tolElapsed = (duty == SYSTEM_DUTY_TOLERANCE && p->tolCount >= TP) ? 1 : 0;
#if defined(W_A6_PIN0)
  /* A6 WITHDRAWN, pinned SHUT: the caller stops computing the tolerance
   * gate honestly and reports it never elapsed.  Pinned ahead of
   * 'blocked' so the glue's own read of the signal stays consistent with
   * what it passes -- D's launch-while-blocked arm keeps its teeth. */
  tolElapsed = 0;
#elif defined(W_A6_PIN1)
  /* A6 WITHDRAWN, pinned OPEN: the tolerance is reported elapsed on the
   * first sweep, so it never funds the tail it exists to fund and the
   * cohort advances the instant n-t possession stands. */
  if (duty == SYSTEM_DUTY_TOLERANCE && p->tolCount < TP)
    ++r->st.tolUnearned;
  tolElapsed = 1;
#endif
  blocked = (duty == SYSTEM_DUTY_HELD
          || (duty == SYSTEM_DUTY_TOLERANCE && !tolElapsed)) ? 1 : 0;
  if (blocked)
    ++r->st.blockedTicks;

  p->tolElapsed = (unsigned char)tolElapsed;
  n = systemLaunch(p->sys, (unsigned char)(f < ROUNDS), 0, 1,
                   (unsigned char)tolElapsed, sa);
  if (n && blocked)
    ++r->st.launchWhileBlocked;
  applySysActs(r, p, sa, n, 0, 0);

  /* an adoption owing its close: JOIN if nothing is live, then close.
   * The join rides the same R4 gate as any other launch. */
  if (p->adoptPending && p->candValid) {
    if (!systemLive(p->sys)) {
      n = systemLaunch(p->sys, 0, 0, 1, p->tolElapsed, sa);
      applySysActs(r, p, sa, n, 0, 0);
    }
    if (systemLive(p->sys)) {
      p->adoptPending = 0;
      sysClose(r, p, rv(systemFrontier(p->sys)), p->cand);
    }
  }

#ifdef M_SEAM_FREE
  /* the glue launches on its own account, ignoring the answer.
   * THE COUNTER IS THIS MUTANT'S OWN, and separating it from D's arm is
   * the point (the 2026-08-14 line-by-line read): the arm at the launch
   * site above counts the MACHINE answering while the glue's independent
   * duty read says withhold, which is a machine/glue disagreement worth
   * a check.  Feeding this mutant's rogue launches into that same
   * counter made the kill read as D catching the defect, when what
   * caught it was the defect reporting itself.  A deployment glue that
   * launched on its own account and kept no counter passes both. */
  if (!n && f < ROUNDS && !systemLive(p->sys)) {
    if (blocked)
      ++r->st.launchRogue;
    launchRound(r, p, f);
  }
#endif

  /* the tails: one instance's BPR step per tick, round-robin */
  for (i = 0; i < ROUNDS; ++i) {
    unsigned char rd;

    rd = (unsigned char)((p->retryCursor + i) % ROUNDS);
    if (!p->acs[rd])
      continue;
#ifdef W_R2C_SILENT
    /* R2c WITHDRAWN (post-decide continuation).  Once this process has
     * closed the round it stops driving that instance's retries -- the
     * send-silence a decided process is forbidden to fall into.  The tails
     * are what carry the possession indication, so withdrawing R2c kills
     * the carrier and nothing else. */
    if (p->closed[rd])
      continue;
#endif
    p->retryCursor = (unsigned char)((rd + 1) % ROUNDS);
    n = bkr94acsRetry(p->acs[rd], &p->cur[rd], out);
    emitAcs(r, p, rd, out, n);
    break;
  }

  /* the leg tails: retire only on remote evidence (the other end's accept) or
   * round release, never on local send -- so retry each live leg */
#ifndef M_LEG_NORETRY
  for (i = 0; i < LEGCAP; ++i) {
    unsigned char legOut[BRACHA87_FIG1_RETRY_MAX_ACTS];
    unsigned int b;
    struct leg *lg;

    lg = &p->legs[i];
    if (!lg->inUse || lg->retired)
      continue;
    n = bracha87Fig1Bpr(lg->f1, legOut);
    for (b = 0; b < n; ++b)
      emitOneAct(r, p, lg, legOut[b]);
  }
#endif

  /* the exchange tails: each live instance's BPR -- the initiator retires
   * INITIAL at all-echoed, the RB tail persists until round release */
  for (i = 0; i < EXCHCAP; ++i) {
    unsigned char exOut[BRACHA87_FIG1_RETRY_MAX_ACTS];
    unsigned int b;
    struct exch *ex;

    ex = &p->exchs[i];
    if (!ex->inUse)
      continue;
    n = bracha87Fig1Bpr(ex->f1, exOut);
    for (b = 0; b < n; ++b)
      /* the INITIAL carries the per-destination sidecar, so it must
       * retire at ALL-ECHOED, NOT at ACCEPTED -- the standard Fig1 Bpr
       * retires it at accept, which would strand a process that has not
       * yet received its sidecar.  Drive it below by all-echoed instead. */
      if (exOut[b] != BRACHA87_INITIAL_ALL)
        emitExchAct(r, p, ex, exOut[b]);
    if (ex->member == p->self && !bracha87Fig1AllEchoed(ex->f1))
      emitExchAct(r, p, ex, BRACHA87_INITIAL_ALL);
    if (!n && ex->accepted) {
      /* all-accepted quiescence: content delivered everywhere it will be;
       * free the instance (release does not free it -- content may lag) */
      free(ex->f1);
      ex->f1 = 0;
      ex->inUse = 0;
    }
  }

  /* the serve walk: one step per tick */
  n = systemServe(p->sys, p->serveCursor, sa);
  applySysActs(r, p, sa, n, 0, 0);

#ifdef W_SERVEFLOOD
  /* THE FLOODING SOLICITOR.  A legal Byzantine behavior inside the budget,
   * NOT a second withdrawal: the liar asserts want for everything, always --
   * it admits possession of nothing on any egress act (above, in emitAcs)
   * and re-offers blind each tick for every round to every process.  The
   * offer is a deterministic function of (mode, destination, round) and
   * draws no RNG, so the injector-off and injector-on runs stay
   * schedule-identical and the state-equivalence oracle keeps its teeth.
   * Its whole purpose is the forged serve duties that crowd the caller's
   * serve cursor -- the displacement the rotation clause forbids. */
  if (r->byzProc >= 0
   && (r->byzMode & BYZ_WANT_FLOOD)
   && (int)p->self == r->byzProc) {
    struct wire fw;
    unsigned int d;
    unsigned char rd;

    for (rd = 0; rd < ROUNDS; ++rd)
      for (d = 0; d < NACT; ++d) {
        if (d == p->self)
          continue;
        memset(&fw, 0, sizeof (fw));
        fw.kind = WK_ACS;
        fw.to = (unsigned char)d;
        fw.from = p->self;
        fw.sysRound = rd;
        fw.cls = BKR94ACS_CLS_ACAST;
        fw.process = p->self;
        fw.type = BRACHA87_READY;
        fw.flood = 1;
        pushWire(r, &fw);
      }
  }
#endif

#ifdef W_SERVEWALK
  /* THE BOUNDED SERVE WALK (system.md SERVE bounds, the C1 seam pin).  The
   * duty space is (wanting process, round), ordered by process and then by
   * round, so the front of it is the lowest-indexed wanter -- exactly the
   * order the rotation clause exists to stop a flood from monopolizing.  At
   * most W_SERVECAP duties are granted per tick; a grant to a process
   * already carrying a leg for that round is still a grant (the serve is
   * re-offered, the leg's own BPR carries it).  ROTATION advances the cursor
   * past each grant, so every duty comes up; the pinned arm never advances
   * it and takes the front, always. */
  {
    unsigned int k, d, granted, tot, cap;

#ifdef W_SERVEFLOOD
    /* MEASURED, NOT ASSUMED: how many duties the solicitor actually holds
     * here, beside how many the correct cohort holds.  Whether a flood can
     * crowd a cap of t turns entirely on this number, and O1's linkage is
     * what bounds it -- an act of a later round evidences its sender's
     * possession of the earlier ones, so a solicitor offering for every
     * round proves possession of every round but its highest. */
    {
      unsigned int c2, ch, kk, jl;

      c2 = ch = 0;
      for (kk = 0; kk < ROUNDS; ++kk)
        if (systemRetained(p->sys, rn((unsigned char)kk))) {
          const unsigned char *w2;

          if (!(w2 = systemWant(p->sys, rn((unsigned char)kk))))
            continue;
          for (jl = 0; jl < NACT; ++jl)
            if (SYSTEM_TST(w2, jl)) {
              if (r->byzProc >= 0 && (int)jl == r->byzProc)
                ++c2;
              else if (jl != p->self)
                ++ch;
            }
        }
      if (c2 > r->st.wFloodDuty)
        r->st.wFloodDuty = c2;
      if (ch > r->st.wHonestDuty)
        r->st.wHonestDuty = ch;
    }
#endif
    tot = (unsigned int)NACT * ROUNDS;
    cap = W_SERVECAP;
#ifdef W_SERVEWIRE
    /* THE ORDER ON A FINITE WIRE.  What the sequence spent this tick is
     * gone; recovery is granted the remainder, which is nothing at volume
     * and something whenever the sequence had only its tail to send. */
    {
      unsigned int left;

      left = p->missionActs >= (unsigned int)WIREBUDGET
           ? 0 : (unsigned int)WIREBUDGET - p->missionActs;
      if (cap > left)
        cap = left;
      if (!left)
        ++r->st.wireStarved;
      else
        ++r->st.wireFreed;
    }
#elif defined(W_SERVEPRIO)
    /* THE ORDER AS A PRESSURE PROXY: any emission at all yields the tick. */
    if (p->missionActs) {
#ifdef W_SERVE_YIELD
      cap = 0;                         /* the sequence first, without
                                        * remainder -- the spec's order */
#else
      if (cap > 1)
        cap = 1;                       /* a floor, kept only as the contrast
                                        * that localizes YIELD's failures */
#endif
    }
#endif
    granted = 0;
#ifdef W_SERVEPRIO
    memset(p->granted2, 0, sizeof (p->granted2));
#endif
    for (k = 0; k < tot && granted < cap; ++k) {
      const unsigned char *wnt;
      unsigned char rd2;
      unsigned int jj;

#ifdef W_SERVE_ROTDROP
      d = k;                           /* ROTATION WITHDRAWN: the front, always */
#else
      d = (p->serveDuty + k) % tot;
#endif
      jj = d / ROUNDS;
      rd2 = (unsigned char)(d % ROUNDS);
      if (jj == p->self || !p->closed[rd2] || !systemRetained(p->sys, rn(rd2)))
        continue;
      if (!(wnt = systemWant(p->sys, rn(rd2))) || !SYSTEM_TST(wnt, jj))
        continue;
#ifdef W_SERVE_NORESUME
      if (p->serveDropped[jj][rd2])
        continue;
#endif
      legBirthServer(r, p, (unsigned char)jj, rd2);
#ifdef W_SERVEPRIO
      p->granted2[jj][rd2] = 1;
      ++r->st.grantsMade;
#endif
      ++granted;
#ifndef W_SERVE_ROTDROP
      p->serveDuty = (d + 1) % tot;
#endif
    }
#ifdef W_SERVEPRIO
    /* THE ORACLE FOR "DEFERRED, NEVER RETIRED", and it is a direct reading
     * of the clause rather than a consequence of it.  The walk grants until
     * it hits the cap or runs out of owed duties, so ENDING WITH CAPACITY TO
     * SPARE means every want it could see was served.  A want still owed
     * beside an unspent slot is therefore not a deferral -- nothing deferred
     * it -- it is an obligation the caller has retired on its own account,
     * which M1 forbids.  Structurally zero for a glue that only defers;
     * W_SERVE_NORESUME is the glue that does not, and this is its red.
     * Independent of whether the heal completes, so it scores the clause
     * even in runs where the yield legitimately strands the returner. */
    /* TWO READINGS, ONE WALK, and they are not the same condition.  A want
     * left standing while the ORDER took the capacity (cap below the plain
     * cap) is a DEFERRAL, and the ledger records whose -- ground truth the
     * instrument can read and a deployment cannot.  A want left standing
     * while capacity was SPARE (granted below cap) is a RETIREMENT, since
     * nothing deferred it.  A zero cap satisfies the first and cannot
     * satisfy the second, which is why the guards are separate. */
    if (granted < cap || cap < (unsigned int)W_SERVECAP)
      for (k = 0; k < tot; ++k) {
        const unsigned char *wnt;
        unsigned char rd2;
        unsigned int jj;

        jj = k / ROUNDS;
        rd2 = (unsigned char)(k % ROUNDS);
        if (jj == p->self || !p->closed[rd2] || !systemRetained(p->sys, rn(rd2)))
          continue;
        if (!(wnt = systemWant(p->sys, rn(rd2))) || !SYSTEM_TST(wnt, jj))
          continue;
        if (p->granted2[jj][rd2])
          continue;                    /* served this tick; the want clears
                                        * when its owner evidences, not now */
        if (cap < (unsigned int)W_SERVECAP)
          r->st.yieldDenied[jj] = 1;
#ifdef W_SERVEWIRE
        if (granted < cap)
          ++r->st.wireRetired;
#endif
      }
#endif
#ifdef W_SERVE_NORESUME
    /* THE WITHDRAWAL: the yield made a RETIREMENT.  Every duty the wire had
     * no slot for is DROPPED rather than deferred, so a serve withheld under
     * pressure is never offered again.  That is M1's forbidden gate -- an
     * obligation retired by silence -- and it is the one thing the discharge
     * order is not allowed to do. */
    if (cap < W_SERVECAP)
      for (k = 0; k < tot; ++k) {
        const unsigned char *wnt;
        unsigned char rd2;
        unsigned int jj;

        jj = k / ROUNDS;
        rd2 = (unsigned char)(k % ROUNDS);
        if (jj == p->self || !p->closed[rd2] || !systemRetained(p->sys, rn(rd2)))
          continue;
        if (!(wnt = systemWant(p->sys, rn(rd2))) || !SYSTEM_TST(wnt, jj))
          continue;
        p->serveDropped[jj][rd2] = 1;
      }
#endif
  }
#endif

  /* the barren-sweep meter (system.md "The three states"): PROGRESS is a
   * fresh act cascade (a live Input that was not a duplicate), a frontier
   * advance (completion / adoption), possession-record growth, or a
   * release.  The first is read from activeThisTick; the last three are a
   * change to the (frontier, possession mass) fingerprint.  Duplicate
   * retries and idle ticks move none of them.  A tolerance climb resets
   * on each round's completion cascade, so its per-rung barren stays near
   * T_p; only an unbounded strand -- no cascade, no fingerprint change --
   * runs the count to SP and classifies PARTITIONED.  A finished process
   * (frontier at ROUNDS) is not stranded and is exempt.  A classified
   * process keeps stepping; the flag alters nothing below it. */
  {
    unsigned long f;
    unsigned int mass, rd, j;

    f = rv(systemFrontier(p->sys));
    mass = 0;
    for (rd = 0; rd < ROUNDS; ++rd)
      if (systemRetained(p->sys, rn((unsigned char)rd))) {
        const unsigned char *pos;

        pos = systemPossess(p->sys, rn((unsigned char)rd));
        for (j = 0; j < NACT; ++j)
          if (pos && SYSTEM_TST(pos, j))
            ++mass;
      }
    if (p->activeThisTick || f != p->lastFrontier || mass != p->lastMass) {
      p->lastFrontier = f;
      p->lastMass = mass;
      p->barren = 0;
    } else if (!p->partitioned && f < ROUNDS && ++p->barren >= SP) {
      p->partitioned = 1;
      ++r->st.classifications;
    }
    p->activeThisTick = 0;
#ifdef W_SERVEPRIO
    p->missionActs = 0;
#endif
  }
}

/* ------------------------------------------------------------------ */
/*  the invalid injector and the state-equivalence oracle             */
/*                                                                    */
/*  Crypto at this layer is verdict-shaped: the machine never sees a  */
/*  failed verdict.  An invalid item must be indistinguishable from   */
/*  absence -- INVALID REDUCES TO LOSS.  Each site below is a VERDICT  */
/*  GATE: it counts the injection, then (unless its red disables the  */
/*  gate) DROPS it with zero state touch and zero wire push.  Under   */
/*  M_INJ_<site> the gate is disabled and the flagged item is taken   */
/*  as if valid, so its state effect diverges the injector-on run     */
/*  from the injector-off run and the oracle fires. */
static void
injectStep(
  struct run *r
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  struct proc *p;
  unsigned char to;
  unsigned char forged;
  unsigned long f;
  unsigned char rret;
  unsigned int site;
  unsigned int rr;

  if (injNext() % 100 >= INJ_RATE)
    return;
  site = injNext() % INJ_SITES;
  to = (unsigned char)(injNext() % NACT);
  forged = (unsigned char)(injNext() % NACT);
  p = &r->p[to];
  f = rv(systemFrontier(p->sys));
  /* a retained round at the target makes the forged item effective under
   * the red (an inert call would falsely look like an absorbing site) */
  rret = f ? (unsigned char)(f - 1) : 0;
  for (rr = 0; rr < ROUNDS; ++rr)
    if (systemRetained(p->sys, rn((unsigned char)rr))) {
      rret = (unsigned char)rr;
      break;
    }
  /* used only inside the red blocks below; the gate-intact build drops */
  (void)sa;
  (void)forged;
  (void)rret;

  switch (site) {

  case INJ_ATTRIB:
    ++r->st.injSite[INJ_ATTRIB];
#ifdef M_INJ_ATTRIB
    /* gate disabled: a forged-sender act is attributed and recorded */
    (void)systemReceived(p->sys, rn(rret), forged, 0, 0, sa);
#endif
    break;

  case INJ_WITNESS:
    ++r->st.injSite[INJ_WITNESS];
#ifdef M_INJ_WITNESS
    /* gate disabled: an invalid served assertion counts as a witness */
    (void)systemWitness(p->sys, rn(f), forged, sa);
#endif
    break;

  case INJ_CANDIDATE:
    ++r->st.injSite[INJ_CANDIDATE];
#ifdef M_INJ_CANDIDATE
    /* gate disabled: a non-folding garbage composition is adopted as the
     * standing candidate (and clobbers the accumulated witnesses) */
    systemWitnessReset(p->sys);
    memset(p->cand, 0xC5, COMPLEN);
    p->candValid = 1;
#endif
    break;

  case INJ_LEG:
    ++r->st.injSite[INJ_LEG];
#ifdef M_INJ_LEG
    /* gate disabled: a forged-initiator leg's witness is delivered */
    (void)systemWitness(p->sys, rn(f), forged, sa);
#endif
    break;

  case INJ_EXCH:
    ++r->st.injSite[INJ_EXCH];
#ifdef M_INJ_EXCH
    /* gate disabled: a corrupted sidecar delivers wrong content (0x02 is
     * even, so never a real token -- exchTok is always odd) */
    p->haveTok[rret][forged] = 0x02;
#endif
    break;

  case INJ_POSSESS:
    ++r->st.injSite[INJ_POSSESS];
#ifdef M_INJ_POSSESS
    /* gate disabled: a forged possession indication is recorded */
    (void)systemPossessed(p->sys, rn(rret), forged, sa);
#endif
    break;

  default:
    break;
  }
}

/* the oracle's fingerprint: every byte the injector must not be able to
 * move.  (a) the seat's MACHINE-OWNED bytes, (b) THE RETAINED ROUNDS AND
 * THEIR RECORDS, which are caller storage now, (c) the glue-side
 * per-round artifacts the machine holds only as bitmaps
 * (adopted/completed composition, the standing candidate, content
 * tokens), (d) frontier + classification.  Injection counters and other
 * metrics are NOT hashed -- excluded by construction.
 *
 * (a) starts at the seat's 'rs' field, not at its first byte: the head
 * of struct system is the caller's own closure -- the comparator, the
 * four retention operations and the ctx -- and the ctx is a malloc
 * address, which differs between the injector-off and injector-on runs
 * of the same leaf for reasons that have nothing to do with the
 * machine.  Hashing it would make every comparison fail.
 * (b) is the reason this is not just a rename: the retained set is
 * state the machine DIRECTS but no longer HOLDS, so an oracle over the
 * seat alone would go blind to a gated item changing what is retained.
 * systemStoreState hands back the live entries in the comparator's
 * order, with no freed residue, so equal retained sets hash equal. */
static unsigned long
stateHash(
  struct run *r
){
  unsigned long h;
  unsigned long sz;
  unsigned long slen;
  unsigned int i;
  unsigned int k;

  h = 2166136261UL;
  sz = systemSz(NENC, RS)
   - ((const unsigned char *)&r->p[0].sys->rs
      - (const unsigned char *)r->p[0].sys);
  for (i = 0; i < NACT; ++i) {
    struct proc *p;
    const unsigned char *b;

    p = &r->p[i];
    b = (const unsigned char *)&p->sys->rs;
    for (k = 0; k < sz; ++k) {
      h ^= b[k];
      h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    b = systemStoreState(p->store, &slen);
    for (k = 0; k < slen; ++k) {
      h ^= b[k];
      h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    h ^= slen;
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
    b = &p->comp[0][0];
    for (k = 0; k < ROUNDS * COMPLEN; ++k) {
      h ^= b[k];
      h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    b = &p->haveTok[0][0];
    for (k = 0; k < ROUNDS * NACT; ++k) {
      h ^= b[k];
      h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    for (k = 0; k < COMPLEN; ++k) {
      h ^= p->cand[k];
      h = (h * 16777619UL) & 0xFFFFFFFFUL;
    }
    h ^= p->candValid;
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
    h ^= rv(systemFrontier(p->sys));
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
    h ^= p->partitioned;
    h = (h * 16777619UL) & 0xFFFFFFFFUL;
  }
  return (h);
}

/* ------------------------------------------------------------------ */
/*  one run                                                           */
/* ------------------------------------------------------------------ */

static void
runSeam(
  struct run *r
 ,unsigned long seed
 ,unsigned int dropPercent
 ,int lagProc
 ,int lagRound
 ,int indStarve
 ,int byzProc                        /* the Byzantine participant, -1 = none */
 ,unsigned int byzMode               /* BYZ_* mask */
 ,int inject                         /* 1 = injector on */
 ,unsigned long *hashArr             /* per-tick state fingerprint, or 0 */
 ,unsigned int *hashLen              /* out: fingerprints written */
){
  struct wire w;
  unsigned long sz;
  unsigned int i, k, tick, hlen;

  memset(r, 0, sizeof (*r));
  r->dropPercent = dropPercent;
  r->lagProc = lagProc;
  r->lagRound = lagRound;
  r->indStarve = indStarve;
  r->byzProc = byzProc;
  r->byzMode = byzMode;
  r->acsSz = bkr94acsSz(NENC, VLENENC, MAXPHASES);
  r->legSz = bracha87Fig1Sz(1, COMPLEN - 1);
  r->exchSz = bracha87Fig1Sz(NENC, 0);

  qReset();
  rngSeed(seed);
#ifdef SCHED_ENUM
  injSeed(seed ^ 0x9e3779b9UL ^ EnumInjSalt);
#else
  injSeed(seed ^ 0x9e3779b9UL);      /* separate injector stream */
#endif
  hlen = 0;
#ifdef SCHED_ENUM
  EnumPos = 0;                       /* the choice string is replayed from
                                      * the top; the injector-off and
                                      * injector-on runs of one leaf walk
                                      * the identical prefix */
#endif

  sz = systemSz(NENC, RS);
  for (i = 0; i < NACT; ++i) {
    if (!(r->p[i].sys = calloc(1, sz))
     || !(r->p[i].store = calloc(1, systemStoreSz(NENC, RS, REACH)))) {
      fprintf(stderr, "FATAL [%s]: out of memory\n", CurTest);
      abort();
    }
    /* the store IS the ctx systemInit hands to the comparator and to
     * the four operations, and systemStoreCmp forwards to ordCmp
     * (systemStore.h, the closure carries the comparator); REACH is
     * where the deployment's reach binds, which is what makes the
     * W_REACH_WSHRINK arm a CONFIGURATION withdrawal */
    systemStoreInit(r->p[i].store, NENC, RS, REACH, ordCmp, 0);
    systemInit(r->p[i].sys, NENC, TVAL, (unsigned char)i, RS, rn(0),
               systemStoreCmp, systemStoreRecords, systemStoreRetain,
               systemStoreRelease, systemStoreAfter, r->p[i].store);
    r->p[i].self = (unsigned char)i;
  }

  for (tick = 0; tick < MAXTICKS; ++tick) {
    unsigned int done;

    for (k = 0; k < DRAIN && qPopRandom(&w); ++k)
      deliverWire(r, &w);

    /* the injector fires inline at receipt, from its own RNG -- gated
     * injections touch nothing, so the schedule is unperturbed */
    if (inject)
      for (k = 0; k < INJ_PERTICK; ++k)
        injectStep(r);

    for (i = 0; i < NACT; ++i) {
      procTick(r, &r->p[i]);
    }

    if (hashArr) {
      hashArr[tick] = stateHash(r);
      hlen = tick + 1;
    }

    /* quiescence, posture-aware: every NON-classified process ran every
     * round and retains only rounds owed exclusively to classified
     * processes.  A classified (PARTITIONED) process is the abandonment
     * policy acting -- the run does not wait on it (system.md "The three
     * states": the policy firing is the process acting on a lapse that
     * already holds).  A round a non-classified process retains but owes
     * ONLY to classified processes can never reach all-n possession, so
     * holding it is not non-quiescence. */
    done = 1;
    for (i = 0; i < NACT && done; ++i) {
      unsigned int rd;

      /* the liar is not part of the cohort the run waits on, in either
       * direction: neither its own frontier nor a want owed to it */
      if (BYZSELF(r, i))
        continue;
      if (r->p[i].partitioned)
        continue;
      if (rv(systemFrontier(r->p[i].sys)) < ROUNDS) {
        done = 0;
        break;
      }
      for (rd = 0; rd + 1 < ROUNDS && done; ++rd) {
        const unsigned char *wnt;
        unsigned int j;

        /* content is part of the round's discharge: a finished process
         * holds every in-subset member's content for every round it
         * closed (the O2 grain must assemble, not just possession).  A
         * BYZANTINE MEMBER's content is exempt -- a member that mis-tags
         * or withholds its own grain leaves the per-member out-of-band
         * hole O2 prices, never the round's failure (L5's content
         * sentence read at the member axis). */
        if (r->p[i].closed[rd])
          for (j = 0; j < NACT; ++j)
            if (r->p[i].comp[rd][ANCHOR + j]
             && !r->p[i].haveTok[rd][j]
             && !BYZSELF(r, j)) {
              done = 0;
              break;
            }
        if (!done)
          break;
        if (!systemRetained(r->p[i].sys, rn((unsigned char)rd)))
          continue;
        wnt = systemWant(r->p[i].sys, rn((unsigned char)rd));
        for (j = 0; j < NACT; ++j)
          if (wnt && SYSTEM_TST(wnt, j) && !r->p[j].partitioned
           && !BYZSELF(r, j)) {
            done = 0;
            break;
          }
      }
    }
    if (done) {
      r->st.converged = 1;
      break;
    }
  }
  r->st.ticks = tick;
  if (hashLen)
    *hashLen = hlen;
#ifdef SCHED_ENUM
  EnumPops = EnumPos;
#endif

  /* capture the posture ground truth before the sys is freed (assertRun
   * runs after teardown).  STRAND SHAPE -- the ground-truth arm the
   * instrument can read and a deployment cannot: a classified process is
   * an ACCEPTED strand only when its duty is HELD and it POSSESSES the
   * round that duty is held on (frontier - 1), so the deficit is purely
   * evidential.  A classified process that LACKS that round is a starved
   * heal, stays in the quantifiers, and its checks fire. */
  for (i = 0; i < NACT; ++i) {
    unsigned long f;
    unsigned int rd;

    /* the liar's own posture is not the cohort's: it never counts as a
     * classification and never earns the strand exclusion */
    r->st.classified[i] = (unsigned char)(r->p[i].partitioned
                                       && !BYZSELF(r, i));
    f = rv(systemFrontier(r->p[i].sys));
    r->st.clFrontier[i] = f;
    for (rd = 0; rd < ROUNDS; ++rd) {
      unsigned int mm;

      r->st.pClosed[i][rd] = r->p[i].closed[rd];
      for (mm = 0; mm < NACT; ++mm) {
        r->st.pHaveTok[i][rd][mm] = r->p[i].haveTok[rd][mm];
        r->st.pHaveTold[i][rd][mm] = r->p[i].haveTold[rd][mm];
        r->st.pHaveToldReq[i][rd][mm] = r->p[i].haveToldReq[rd][mm];
      }
    }
    /* ARRIVED-BUT-UNBANKED is a GLUE DEFECT, not a strand.  A deficit
     * only counts as evidential if the evidence never reached this
     * process at all; if an indication arrived and the glue failed to
     * bank it, the classification is REJECTED, the process stays in the
     * every-process quantifiers, and its checks fire.  Without this arm
     * the posture absorbs exactly the defect M_SEAM_NOPEND injects. */
    {
      const unsigned char *poss;
      unsigned int mm;
      unsigned int unbanked;

      unbanked = 0;
      if (f > 0 && (unsigned int)(f - 1) < ROUNDS
       && (poss = systemPossess(r->p[i].sys, rn((unsigned char)(f - 1)))))
        for (mm = 0; mm < NACT; ++mm)
          if (mm != i && r->p[i].indArrived[f - 1][mm]
           && !SYSTEM_TST(poss, mm))
            ++unbanked;
      /* TWO LEGITIMATE DEFICITS, and the arm must tell them from a defect.
       * EVIDENTIAL: the process HOLDS the round its duty is held on and
       * only its evidence is missing -- the original ground, and it reads
       * the duty class because R4 is what strands it.  CAPACITY: the
       * serves this process was owed were WITHHELD BY THE DISCHARGE ORDER,
       * and it takes NEITHER conjunct of the first -- a starved returner
       * typically sits at duty MET, not HELD, because nothing about R4 is
       * blocking it: it simply cannot COMPLETE a round the cohort has left
       * behind, and the adoption that would close it is what the order
       * withheld.  Requiring HELD here was the first cut and it accepted
       * nothing.  system.md licenses the starvation -- the sequence goes first without
       * remainder, a returner starved under pressure is already one of the
       * t, and the sequence completes without it.  Both stay subject to
       * the arrived-but-unbanked veto: evidence that REACHED this process
       * and was dropped is a glue defect under either deficit, so
       * M_SEAM_NOPEND keeps its red.  yieldDenied is all-zero in every
       * build that compiles no order, so no other arm moves. */
      r->st.accepted[i] = (unsigned char)(r->p[i].partitioned
                          && !BYZSELF(r, i)
                          && ((systemDuty(r->p[i].sys) == SYSTEM_DUTY_HELD
                               && f > 0 && r->p[i].closed[f - 1])
                              || YIELDSTARVED(r, i))
                          && !unbanked);
    }
    if (r->st.classified[i])
      ++r->st.numClassified;
    if (r->st.accepted[i])
      ++r->st.numAccepted;
  }

  if (r->st.numClassified)
    for (i = 0; i < NACT; ++i)
      fprintf(stderr, "  CLASS p%u frontier %3u duty %u closed[f-1] %u"
                      " yieldDenied %u classified %u accepted %u\n",
              i, (unsigned int)r->st.clFrontier[i], systemDuty(r->p[i].sys),
              r->st.clFrontier[i] > 0
                ? (unsigned int)r->p[i].closed[r->st.clFrontier[i] - 1] : 0u,
              (unsigned int)YIELDSTARVED(r, i),
              (unsigned int)r->st.classified[i], (unsigned int)r->st.accepted[i]);

  /* a stall is the interesting outcome: say exactly where every
   * process stopped, so the schedule can be minimized by hand */
  if (!r->st.converged)
    for (i = 0; i < NACT; ++i) {
      unsigned int rd;

      fprintf(stderr, "  STALL p%u frontier %3u live %u owed %u duty %u"
                      " held %4u cand %u retained",
              i, (unsigned int)rv(systemFrontier(r->p[i].sys)),
              systemLive(r->p[i].sys), systemOwed(r->p[i].sys),
              systemDuty(r->p[i].sys), r->p[i].held,
              (unsigned int)r->p[i].candValid);
      for (rd = 0; rd < ROUNDS; ++rd) {
        const unsigned char *pos;
        const unsigned char *wnt;
        unsigned int j;

        if (!systemRetained(r->p[i].sys, rn((unsigned char)rd)))
          continue;
        pos = systemPossess(r->p[i].sys, rn((unsigned char)rd));
        wnt = systemWant(r->p[i].sys, rn((unsigned char)rd));
        fprintf(stderr, " r%u[pos", rd);
        for (j = 0; j < NACT; ++j)
          if (pos && SYSTEM_TST(pos, j))
            fprintf(stderr, " %u", j);
        fprintf(stderr, " want");
        for (j = 0; j < NACT; ++j)
          if (wnt && SYSTEM_TST(wnt, j))
            fprintf(stderr, " %u", j);
        fprintf(stderr, "]");
      }
      fprintf(stderr, "\n");
    }

  for (i = 0; i < NACT; ++i) {
    unsigned int rd;

    for (rd = 0; rd < ROUNDS; ++rd)
      if (r->p[i].acs[rd]) {
        free(r->p[i].acs[rd]);
        r->p[i].acs[rd] = 0;
      }
    for (rd = 0; rd < LEGCAP; ++rd)
      if (r->p[i].legs[rd].inUse) {
        free(r->p[i].legs[rd].f1);
        r->p[i].legs[rd].f1 = 0;
        r->p[i].legs[rd].inUse = 0;
      }
    for (rd = 0; rd < EXCHCAP; ++rd)
      if (r->p[i].exchs[rd].inUse) {
        free(r->p[i].exchs[rd].f1);
        r->p[i].exchs[rd].f1 = 0;
        r->p[i].exchs[rd].inUse = 0;
      }
    free(r->p[i].store);
    r->p[i].store = 0;
    free(r->p[i].sys);
    r->p[i].sys = 0;
  }
}

/* ------------------------------------------------------------------ */
/*  checks                                                            */
/* ------------------------------------------------------------------ */

/* sweep-level coverage: see the MIXED arm in assertRun */
static unsigned long ByzFabTotal = 0;
#ifdef W_SERVEPRIO
static unsigned long GrantsTotal  = 0;   /* serve grants the order allowed,
                                          * summed over the whole sweep */
#endif

#if defined(SCHED_ENUM) && TVAL == 0
/* the t=0 serve-floor arm, taken tree-wide instead of per-leaf: see the
 * scope note at its site in assertRun */
static unsigned long EnumServeLeaves = 0;
static unsigned long EnumLeafRuns = 0;
#endif
#if defined(SCHED_ENUM) && INJ_RATE > 0
/* the injector's per-site coverage, likewise tree-wide */
static unsigned long EnumInjSite[INJ_SITES] = { 0 };
#endif

/* SWEEP-LEVEL COVERAGE FOR THE WITHDRAWAL ARMS.  Whether a given seed
 * exhibits the predicted loss is schedule-dependent, so non-vacuity is a
 * property of the sweep -- the same granularity the seed-dependent mutant
 * reds are recorded at.  An arm whose counter stays ZERO is a FINDING:
 * the withdrawal changed nothing, which means the premise was not being
 * read where the proof says it is. */
#if defined(W_A5_NOINFER) || defined(W_R2C_SILENT) || defined(W_REACH_WSHRINK)
static unsigned long WLostTotal = 0;
#endif
#ifdef W_A9_SYBIL
static unsigned long WSybilTotal = 0;
#endif
#ifdef W_A6_PIN1
static unsigned long WUnearnedTotal = 0;
#endif
#ifdef W_REACH_WSHRINK
static unsigned long WEvictTotal = 0;
#endif
#ifdef W_SERVEFLOOD
static unsigned long WFloodTotal = 0;
static unsigned long WFloodDutyMax = 0;
static unsigned long WHonestDutyMax = 0;
#endif
#ifdef W_L2_NOBYTEMATCH
static unsigned long WNoMatchTotal = 0;
#endif
#ifdef W_L2_NOREARM
static unsigned long WNoRearmTotal = 0;
#endif
#ifdef W_L2_NOCLOSEVOID
static unsigned long WNoCloseVoidTotal = 0;
#endif
#ifdef W_I10_WRONGARTIFACT
static unsigned long WStaleTotal = 0;
#endif

static void
assertRun(
  struct run *r
 ,int laggard
){
  struct stats *s;
  unsigned int rd, i;
  unsigned long heldNonStrand;

  s = &r->st;

#ifdef W_A9_SYBIL
  WSybilTotal += s->wSybil;
#endif
#ifdef W_A6_PIN1
  WUnearnedTotal += s->tolUnearned;
#endif
#ifdef W_SERVEFLOOD
  WFloodTotal += s->wFlood;
  if (s->wFloodDuty > WFloodDutyMax)
    WFloodDutyMax = s->wFloodDuty;
  if (s->wHonestDuty > WHonestDutyMax)
    WHonestDutyMax = s->wHonestDuty;
#endif
#ifdef W_L2_NOBYTEMATCH
  WNoMatchTotal += s->wNoMatch;
#endif
#ifdef W_L2_NOREARM
  WNoRearmTotal += s->wNoRearm;
#endif
#ifdef W_L2_NOCLOSEVOID
  WNoCloseVoidTotal += s->wNoCloseVoid;
#endif
#ifdef W_I10_WRONGARTIFACT
  WStaleTotal += s->wStale;
#endif

  /* THE POSTURE, and its ground-truth guard.  PARTITIONED is the default
   * state; a classification is the abandonment policy acting on a lapse
   * that already holds.  The instrument sees ground truth a deployment
   * cannot -- whether a stranded process POSSESSES the round it is held
   * on -- and must use it, else the posture would absorb the starvation
   * reds: under those the laggard LACKS the round, which the strand-shape
   * arm rejects, so the original checks still fire.  An ACCEPTED strand
   * (s->accepted[i]) is excluded from the every-process / releases-
   * everywhere quantifiers; a REJECTED classification is not.  Zero
   * classifications in a run with no strand: every classification must be
   * an accepted strand. */
#if defined(W_A4_PARTITION)
  /* THE ARM'S OWN POSTURE ARM.  With A4 withdrawn for one process the
   * deficit is POSSESSIONAL, not evidential -- the victim holds no round
   * for a duty to be held on -- so the accepted-strand shape does not
   * apply and asserting it would only red on the prediction.  What the
   * arm asserts instead is the prediction itself: EXACTLY ONE process
   * classifies, and it is the one the transport abandoned. */
  CHECK(s->numClassified == 1,
        "W_A4: exactly one process classified PARTITIONED");
  CHECK(s->classified[WVICT],
        "W_A4: the classified process is the partitioned one");
  CHECK(s->numClassified <= TVAL,
        "posture: at most t processes classify (n-t stay participants)");
#else
  /* every arm that predicts no loss keeps the posture exactly as strict as
   * the baseline's, and W_A6_PIN1 notably does: the straggler shed its
   * brief predicted does NOT appear at this configuration -- see the
   * header.  Where an arm DOES predict the loss, the classification it
   * predicts is not strand-shaped and asserting the shape would only red
   * on the prediction. */
  if (!WSTARVED(r)) {
    CHECK(s->numClassified == s->numAccepted,
          "posture: every classification is an accepted strand");
    CHECK(s->numClassified <= TVAL,
          "posture: at most t processes classify (n-t stay participants)");
  }
#endif
#if defined(W_A6_PIN0)
  if (WWEDGED(r)) {
    /* A6 pinned SHUT, and the mute arm is where that bites.  All-n
     * possession is unreachable, the TOLERANCE class can never be
     * discharged, and the cohort therefore wedges one round in and runs
     * its barren budget out.  Abandoning IS the deployment behaving
     * correctly on a gate that lies to it, so the arm asserts the wedge
     * rather than reddening on it -- and the SAFETY arms below (D both
     * halves, E, F's unsafe arm, H) stay strict, which is what the
     * control is FOR. */
    CHECK(s->numClassified == NCORRECT(r),
          "W_A6_PIN0: the whole correct cohort ran its barren budget out");
    for (i = 0; i < NACT; ++i)
      if (!BYZSELF(r, i))
        CHECK(s->clFrontier[i] <= 1,
              "W_A6_PIN0: no correct process advanced past the first round");
    CHECK(s->blockedTicks > 0,
          "W_A6_PIN0: the advance signal withheld (the gate reads the input)");
  }
#endif

#ifdef W_A6_PIN1
  /* the withdrawal actually reached the machine.  A zero here would mean
   * the pin was compiled in and never consumed, and every green below it
   * would be vacuous. */
  if (r->byzMode & BYZ_SILENT)
    CHECK(s->tolUnearned > 0,
          "W_A6_PIN1: the pin was consumed -- an unearned tolerance advanced");
#endif
#ifdef W_SERVEWIRE
  /* NON-VACUITY, BOTH DIRECTIONS.  A run where the wire never bound proves
   * nothing about a yield, and a run where it never freed cannot exhibit the
   * relief the self-funding claim rests on -- so both tick classes must have
   * occurred before any verdict below is worth reading. */
  CHECK(s->wireStarved > 0,
        "wire: the sequence took the whole budget on some tick");
  CHECK(s->wireFreed > 0,
        "wire: the sequence left slots on some tick -- the relief exists");
  CHECK(s->wireRetired == 0,
        "wire: spare capacity never leaves a want unserved -- a yield defers");
#endif
#if defined(W_A5_NOINFER) || defined(W_R2C_SILENT) || defined(W_REACH_WSHRINK)
  /* Each of these withdrawals PREDICTS the liveness loss, so quiescence is
   * the prediction inverted and is banked as coverage instead of asserted.
   * The safety arms below stay strict -- that is what a control is FOR. */
  WLostTotal += (unsigned long)(s->converged ? 0 : 1) + s->numClassified;
#else
  CHECK(s->converged, "run reached quiescence");
#endif
#ifdef W_REACH_WSHRINK
  /* THE SIZING BOUNDARY, BANKED RATHER THAN ASSERTED.  At a reach of 1 the
   * holds one round, so a straggler more than one rung behind would be
   * served by nobody -- system.md's REACH proviso violated BY CONFIGURATION,
   * which is the "w and T_p are not independent" sizing obligation and NOT
   * an L1 red.  Whether a given schedule crosses that boundary is the
   * MEASUREMENT this arm exists to take, so the eviction and the strand are
   * counted at sweep level and reported; asserting either per run would be
   * asserting the prediction rather than testing it. */
  WEvictTotal += s->releasesStructural;
#endif
  heldNonStrand = 0;
  for (i = 0; i < NACT; ++i)
    if (!s->accepted[i] && !BYZSELF(r, i) && !WLOST(r, i))
      heldNonStrand += s->heldDroppedProc[i];
  CHECK(heldNonStrand == 0, "instrument: hold queue never overflowed");

  /* A -- tails classify.  The first two arms are MUTANT TRIPWIRES, not
   * checks: their counters move only inside the mutant blocks, so a
   * deployment glue that dropped or misrouted tails without
   * self-reporting would pass them.  What actually kills those two
   * mutants is B (the heal starves) and quiescence.  The third arm is
   * a real check -- it audits the machine's DELIVER contract -- and the
   * fourth is the vacuity guard. */
  CHECK(s->tailsDropped == 0, "A tripwire: behind-frontier traffic dropped by the glue");
  CHECK(s->tailsToInstance == 0, "A tripwire: behind-frontier traffic routed into an instance");
  CHECK(s->tailsDelivered == 0, "A: no DELIVER for a round behind the frontier");
  CHECK(s->tailsFed > 0, "A: behind-frontier traffic actually occurred");

  /* B -- laggard heal end to end.  The adoption arm is posture-aware: a
   * held process that classifies as an ACCEPTED strand legitimately never
   * heals (it is partitioned, not starved), so the arm is asserted only
   * when no strand was accepted -- under the starvation reds the laggard
   * is REJECTED (duty MET, or it LACKS the round), so the arm still
   * fires.  The re-feed arm is posture-aware for the SAME reason and it
   * is not the same reason as the adoption arm's: re-feeding requires
   * REACH TO WIDEN, and an accepted strand never advances its frontier,
   * so it never consumes what it held.  Serves born hold regardless --
   * want evidence is what a strand emits, not what it needs. */
  if (laggard) {
#ifndef W_A6_PIN1
    if (!s->numAccepted && !WSTARVED(r)) {
      CHECK(s->adopts > 0, "B: the held process closed by adoption");
      CHECK(s->heldConsumed > 0, "B: beyond-reach traffic was held and re-fed");
    }
#endif
    /* W_A6_PIN1 predicts the heal is OUTRUN -- a tolerance that never
     * funds the tail lets the cohort roll the reach off the round the
     * straggler is being served, so the adoption arm is the arm's own
     * prediction inverted and is not asserted there.  The serve arm
     * holds regardless: want evidence is born whether or not it lands --
     * EXCEPT under a discharge order that granted nothing all run, where
     * a serve is exactly what the spec says must not exist.  There the
     * arm INVERTS rather than lapsing: assert the yield really held, so
     * the regime is checked instead of excused. */
#ifdef W_SERVEPRIO
    if (!s->grantsMade)
      CHECK(s->serveMsgs == 0,
            "B: an order that granted nothing bore no serve -- the yield held");
    else
#endif
    CHECK(s->serveMsgs > 0, "B: serves were born from want evidence");
  }

  /* C -- no misread.  The I2 direction (possession retires want) is
   * NOT checked here: the machine maintains it by construction
   * (system.h, struct systemAct .want), so asserting it at the seam
   * is unfalsifiable.  It belongs to test_system.c.  The every-process
   * quantifier excludes accepted strands (they legitimately never close
   * the rounds past their frontier); rejected classifications stay in,
   * so a starved heal still fails here. */
  if (!WSTARVED(r))
    for (rd = 0; rd + 1 < ROUNDS; ++rd)
      for (i = 0; i < NACT; ++i)
        if (!s->accepted[i] && !BYZSELF(r, i) && !WLOST(r, i))
          CHECK(s->pClosed[i][rd], "C: every round closed at every non-strand process");
  /* The all-n release arm has a premise the MUTE liar voids outright: a
   * process that emits nothing never has its possession evidenced by
   * anyone, so all-n possession is unreachable and eviction is the only
   * release path left.  That is system.md's own eviction exception, not
   * a defect -- the same carve-out shape the accepted strand already
   * carries below at F. */
  /* W_A4_PARTITION voids the same premise at the transport instead of the
   * sender: a process no wire reaches evidences nothing, so all-n
   * possession is unreachable there too and eviction is the only release
   * path left. */
#ifndef W_A4_PARTITION
  if (!(r->byzMode & BYZ_SILENT))
    CHECK(s->releasesAllN > 0, "C: all-n releases occurred");
#endif

  /* D -- R4 over the composition.  The first arm is the real one: it
   * reads GROUND TRUTH (how many processes have actually closed the
   * prior round), independent of the signal the launch gated on.  The
   * second is a machine-consistency arm only. */
  CHECK(s->launchUnderShed == 0, "D: no advance outran n-t processes closing the prior round");
  CHECK(s->launchWhileBlocked == 0, "D: no launch taken while the advance signal withholds");
#ifdef M_SEAM_FREE
  /* THE MUTANT'S OWN TRIPWIRE, compiled nowhere else, so no baseline
   * moves.  It is labelled for what it is: M_SEAM_FREE is caught by its
   * own self-report and by nothing else -- neither D arm falls, and the
   * header says why (with n-t real closes behind it the advance is
   * permitted by R4; what the mutant bypasses is the tolerance BUDGET,
   * which only the signal sees).  An arm that fires because the defect
   * announced itself is worth having and worth NOT miscounting as the
   * check that would have caught a silent one. */
  CHECK(s->launchRogue == 0,
        "tripwire: the glue launched without the machine's answer");
#endif
  /* The vacuity arm is LAGGARD-only.  With premature indications held
   * and re-presented, a PLAIN run at zero loss fills its possession
   * records fast enough that R4 never has to withhold at all -- an
   * unconditional arm would then red on a perfectly healthy run. */
  if (laggard)
    CHECK(s->blockedTicks > 0, "D: the advance signal did withhold at some point");

  /* E -- sequence identity */
  CHECK(s->compMismatch == 0, "E: compositions byte-identical across processes");

  /* F -- release safety.  The unsafe arm (an all-n release for a round a
   * process had not closed) stays strict -- it is a safety property no
   * posture excuses.  The structural arm (no eviction / wrap) is the
   * "reach sized past it" claim, which holds only WITHOUT a strand:
   * once a correct process is stranded, the rounds it never possesses
   * can never reach all-n release, so the healthy processes MUST evict
   * them as the frontier advances.  With an accepted strand present, that
   * eviction is expected, not a defect. */
  CHECK(s->releaseUnsafe == 0, "F: no all-n release of a round a process had not closed");
  /* the same carve-out, third shape: a lost process's rounds can never
   * reach all-n either, so under W_A4_PARTITION and W_A6_PIN1 the healthy
   * cohort MUST evict.  The unsafe arm above stays strict in every build --
   * no posture and no withdrawal excuses a safety property. */
/* W_REACH_WSHRINK is the third: it turns the reach DOWN to one round, so
 * eviction is the configuration, not a defect -- asserted positively above. */
#if !defined(W_A4_PARTITION) && !defined(W_A6_PIN1) && !defined(W_REACH_WSHRINK)
  if (!s->numAccepted && r->byzProc < 0 && !WSTARVED(r))
    CHECK(s->releasesStructural == 0, "F: no eviction or wrap release (reach sized past it)");
#endif

  /* G -- round binding.  TRIPWIRE, not a check: the counter moves only
   * inside the M_SEAM_UNBOUND block.  That mutant's behavioral catch
   * was H. */
  CHECK(s->witnessUnbound == 0, "G tripwire: witness evidence from a non-frontier round");

  /* H -- the close speaks its round */
  CHECK(s->closeNoAdvance == 0, "H: every close advanced the frontier");

  /* I -- CONTENT COMPLETENESS (O2, the have grain).  Every non-strand
   * process that closed a round with a successor eventually holds every
   * in-subset member's content for it, via exchange accept (pre-close, in
   * the have bitmap the close carries) or late assembly (post-close,
   * systemAssembled).  Quantifiers posture-aware exactly like C: accepted
   * strands are excluded (they never close those rounds), rejected
   * classifications stay in so a starved exchange still fails.
   *   I-content (ground truth): the token is actually in hand.
   *   I-machine (the have-grain wiring): wherever ground truth holds
   *     content, the machine was told -- so a systemAssembled the glue
   *     skips shows up as a delivered-but-untold mismatch. */
  for (rd = 0; !WSTARVED(r) && rd + 1 < ROUNDS; ++rd) {
    unsigned int mm;

    for (mm = 0; mm < NACT; ++mm) {
      if (!s->compRefSet[rd] || !s->compRef[rd][ANCHOR + mm])
        continue;                      /* not an in-subset member of rd */
      if (BYZSELF(r, mm))
        continue;                      /* a Byzantine member's grain is the
                                        * per-member out-of-band hole (O2) */
      for (i = 0; i < NACT; ++i) {
        if (s->accepted[i] || !s->pClosed[i][rd] || BYZSELF(r, i)
         || WLOST(r, i))
          continue;                    /* excluded strand, the liar, the
                                        * arm's own victim, or did not
                                        * close rd */
        CHECK(s->pHaveTok[i][rd][mm] == exchTok((unsigned char)rd, (unsigned char)mm),
              "I: every closer holds every in-subset member's content");
        if (s->pHaveToldReq[i][rd][mm])
          CHECK(s->pHaveTold[i][rd][mm],
                "I: the machine was told of content delivered while retained");
      }
    }
  }

#if TVAL == 0
  /* THE t = 0 POINT (system.md's N-floor ruling, and the smallest
   * deployment the spec admits).  With n-t = n the prior round reads MET
   * or HELD and nothing else, so toleranceElapsed is a dead input and
   * L1's TOLERANCE half is VACUOUS rather than merely slack.  Asserting
   * the class is never READ is what makes that vacuity observable; L1's
   * whole weight at t = 0 rests on the HELD half, whose heal the serve
   * floor of one carries alone. */
  CHECK(s->dutyTolerance == 0, "t=0: the TOLERANCE duty class is never read");
  /* and the other half of the same ruling: with the cap at t equal to
   * zero, the FLOOR of one is the entire heal capacity the deployment
   * has, so an ordinary-loss run must still be seen to serve */
#ifndef SCHED_ENUM
  CHECK(s->serveMsgs > 0, "t=0: the serve floor of one carried the heal");
#else
  /* THE ENUMERATION'S SCOPE FOR THIS ONE ARM, and it is a scope note and
   * not a weakening.  "A run must be seen to serve" is a COVERAGE claim
   * about a sweep at ordinary loss, not a property of a schedule: the
   * enumeration runs at ZERO loss, where whether a process ever falls
   * behind is decided by the delivery order alone.  Most enumerated
   * orders DO put one behind -- that is itself the measurement, reordering
   * alone manufactures the heal -- but a handful deliver evenly enough
   * that nothing is ever owed, and reddening on those would be asserting
   * a prediction about the schedule rather than testing the layer.  The
   * arm is therefore accumulated and asserted ONCE over the whole tree.
   * The per-run form above is untouched in every other build. */
  EnumServeLeaves += s->serveMsgs > 0 ? 1 : 0;
  ++EnumLeafRuns;
#endif
#endif

  /* THE BYZANTINE ARM'S EXPECTED OUTCOMES.  Each arm below is a claim
   * system.md already makes about the fault it models, restated where a
   * live liar can falsify it.  A failure here is a FINDING about the
   * claim -- never a threshold to move. */
  if (r->byzProc >= 0) {
    unsigned int rd2;

    /* CONTAINMENT, common to every mode: a liar inside the fault budget
     * never strands a correct process.  The cohort runs the whole round
     * space with the liar in it, at the liar's expense and no one
     * else's. */
    for (i = 0; i < NACT && !WWEDGED(r); ++i)
      if (!BYZSELF(r, i) && !s->accepted[i])
        CHECK(s->clFrontier[i] >= ROUNDS,
              "BYZ: every correct process reached the last round");

    if (r->byzMode & BYZ_FORGE_POSSESS) {
      CHECK(s->byzForged > 0, "BYZ FORGE: possession was forged on egress");
      /* L5's STRICT ARM.  A forged bit is the liar's OWN bit to give
       * (A9 confines it), so an all-n release may fire one bit ahead of
       * the truth -- but never ahead of the CORRECT cohort's closes,
       * which is the whole of what L5 claims. */
      CHECK(s->releaseUnsafe == 0,
            "BYZ FORGE: L5 strict arm silent under a forging sender");
    }

    if (r->byzMode & BYZ_WITHHOLD)
      /* L1's corollary: a withheld bit costs the R4 hold, and the
       * tolerance escape -- not the withholder -- owns the pace.  The
       * containment arm above is where that is asserted. */
      CHECK(s->byzWithheld > 0, "BYZ WITHHOLD: possession was withheld on egress");

    if (r->byzMode & BYZ_MIXED_CANDIDATE) {
      /* whether a fabricated serving REACHES a witness path in a given
       * run is schedule-dependent (it needs a want, a leg to an even
       * destination, and that leg to accept), so non-vacuity is a
       * property of the SWEEP and is asserted once in main -- the same
       * granularity the seed-dependent mutant reds are recorded at.
       * Reaching it is not the failure: t forgers reach at most t. */
      ByzFabTotal += s->byzFabServed;
#ifdef W_SERVEPRIO
      GrantsTotal += s->grantsMade;
#endif
      /* L2's caller half: witnesses count only for an assertion
       * byte-identical to the standing candidate, so t forgers reach at
       * most t and no correct process ever closes on the variant. */
      CHECK(s->compMismatch == 0,
            "BYZ MIXED: no correct process closed on the fabricated variant");
    }

    if (r->byzMode & BYZ_EQUIVOCATE_VALUE) {
      CHECK(s->byzEquivocated > 0,
            "BYZ EQUIVOCATE: divergent A-Cast values were put on the wire");
      /* L6 through A1: bkr94acs inherits Bracha Lemma 2, so at most one
       * value is ever accepted for the equivocator's slot and every
       * correct process's sequence stays byte-identical. */
      CHECK(s->compMismatch == 0,
            "BYZ EQUIVOCATE: sequence identity holds across correct processes");
    }

    if (r->byzMode & BYZ_SILENT)
      for (rd2 = 0; rd2 + 1 < ROUNDS; ++rd2)
        if (s->compRefSet[rd2])
          CHECK(!s->compRef[rd2][ANCHOR + r->byzProc],
                "BYZ SILENT: the mute process is in no round's subset");

    if (r->byzMode & BYZ_WRONG_CONTENT)
      /* the tag turned wrong content into LOSS, which is the only shape
       * O2 admits for a corrupted grain */
      CHECK(s->byzWrongContent > 0,
            "BYZ WRONG_CONTENT: mis-tagged sidecars were caught and dropped");
  }
}

/* ------------------------------------------------------------------ */
/*  the run: injector-off then injector-on, the oracle across the pair */
/* ------------------------------------------------------------------ */

static unsigned long HashOff[MAXTICKS];
static unsigned long HashOn[MAXTICKS];

static void
runScenario(
  struct run *r
 ,const char *name
 ,unsigned long seed
 ,unsigned int drop
 ,int lagProc
 ,int lagRound
 ,int indStarve
 ,int byzProc
 ,unsigned int byzMode
 ,int laggard
){
  unsigned int lenOff, lenOn, minL, t, site;
  int firstDiv;

  CurTest = name;
  runSeam(r, seed, drop, lagProc, lagRound, indStarve, byzProc, byzMode,
          0, HashOff, &lenOff);
  runSeam(r, seed, drop, lagProc, lagRound, indStarve, byzProc, byzMode,
          1, HashOn, &lenOn);
  /* r now holds the injector-on run */
#ifndef SCHED_ENUM
  printf("%-18s ticks %6lu  tails %7lu  serves %6lu  adopts %4lu"
         "  rel %4lu  xDlv %5lu  inj %6lu  part %u\n",
         name, r->st.ticks, r->st.tailsFed, r->st.serveMsgs, r->st.adopts,
         r->st.releasesAllN, r->st.exchDelivers,
         r->st.injSite[0] + r->st.injSite[1] + r->st.injSite[2]
       + r->st.injSite[3] + r->st.injSite[4] + r->st.injSite[5],
         r->st.numAccepted);
#else
  /* one line per LEAF would be the whole output; the enumeration prints
   * its totals once, at the end */
#endif

  /* THE ORACLE -- state equivalence: with the verdict gates intact an
   * adversary the gates reject moves NO byte the machine or the glue
   * artifacts hold.  First diverging tick reported. */
  firstDiv = -1;
  minL = lenOff < lenOn ? lenOff : lenOn;
  for (t = 0; t < minL; ++t)
    if (HashOff[t] != HashOn[t]) {
      firstDiv = (int)t;
      break;
    }
  if (firstDiv < 0 && lenOff != lenOn)
    firstDiv = (int)minL;
  if (firstDiv >= 0)
    fprintf(stderr, "  ORACLE %s diverged at tick %d (lenOff %u lenOn %u)\n",
            name, firstDiv, lenOff, lenOn);
  CHECK(firstDiv < 0, "oracle: injector-on state byte-identical to injector-off");

#if INJ_RATE > 0
#ifndef SCHED_ENUM
  for (site = 0; site < INJ_SITES; ++site)
    CHECK(r->st.injSite[site] > 0, "injector: per-site coverage non-vacuous");
#else
  /* THE SAME SCOPE NOTE AS THE t=0 SERVE FLOOR, and for the same reason.
   * Per-site non-vacuity is a claim about a SWEEP: the injector fires
   * INJ_PERTICK steps a tick and picks its site uniformly, so whether all
   * six are reached in ONE run is a function of how many ticks that run
   * takes -- at ROUNDS=1 an enumerated leaf is short enough that a site
   * can be missed, and reddening on it would be measuring the leaf's
   * length rather than the layer.  The ORACLE itself -- the arm with the
   * teeth -- stays per-leaf and unchanged above.  Accumulated tree-wide
   * and asserted, with the counts printed, once at the end. */
  for (site = 0; site < INJ_SITES; ++site)
    EnumInjSite[site] += r->st.injSite[site];
#endif
#else
  (void)site;
#endif

  assertRun(r, laggard);
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

int
main(
  int argc
 ,char **argv
){
  static struct run R;
#ifndef SCHED_ENUM
  unsigned long seed;
#endif
  unsigned long seeds;
  int drop;

  /* argv[1] = seeds, argv[2] = loss percent (-1 = per-scenario default),
   * argv[3] = SCHED_STARVE1's victim process.  Under SCHED_ENUM argv[1] is
   * the TAIL seed instead (the enumeration walks the tree, not the seeds)
   * and argv[2] defaults to ZERO loss -- see the enumeration driver. */
  seeds = argc > 1 ? strtoul(argv[1], 0, 10) : 16;
  drop = argc > 2 ? atoi(argv[2]) : -1;
  if (!seeds)
    seeds = 1;
#if defined(SCHED_STARVE1)
  if (argc > 3)
    SchedVictim = (unsigned int)strtoul(argv[3], 0, 10);
  printf("SCHED_STARVE1 victim p%u (its inbound wires are delivered LAST;"
         " nothing is dropped)\n", SchedVictim);
#elif defined(SCHED_KINDFLIP)
  printf("SCHED_KINDFLIP: leg and exchange traffic ahead of ACS tails\n");
#elif defined(SCHED_LIFO)
  printf("SCHED_LIFO: newest queued wire first\n");
#elif defined(SCHED_FIFO)
  printf("SCHED_FIFO: oldest queued wire first\n");
#endif

#ifdef SCHED_ENUM
  /* THE ENUMERATION.  Depth-first over the choice of which queued wire to
   * deliver next, exhaustively to ENUMDEPTH, each leaf then completing
   * under the uniform policy.  Every leaf is a WHOLE RUN and gets the
   * whole oracle set -- assertRun's safety arms, the round-closure
   * quantifiers, quiescence, and the injector's state-equivalence oracle
   * across the leaf's own off/on pair.
   *
   * LOSS IS NOT ENUMERATED, and the honest reason is the arithmetic: at
   * this shape a run pushes thousands of wires, so a per-wire
   * delivered/lost binary choice multiplies the tree by 2^(that), which is
   * not a tractable state space at any depth worth having.  The default is
   * therefore ZERO loss, where pushWire takes no draw at all and the run
   * is a pure function of the choice string.  A non-zero argv[2] runs the
   * same enumeration with the loss SAMPLED from the tail seed -- still
   * reproducible, but the loss pattern is one sample and is reported as
   * such, never as enumerated. */
  {
    unsigned long leaves;
    unsigned long tailSeed;
    unsigned int drp;
    unsigned int i;
    int d;
    char name[64];

    tailSeed = seeds;                /* argv[1] is the tail seed here */
    drp = drop < 0 ? 0 : (unsigned int)drop;
    memset(EnumChoice, 0, sizeof (EnumChoice));
    memset(EnumBranch, 0, sizeof (EnumBranch));
    leaves = 0;
    for (;;) {
      sprintf(name, "ENUM leaf %lu", leaves);
      EnumInjSalt = leaves;
      runScenario(&R, name, tailSeed, drp, -1, -1, -1, -1, 0, 0);
      ++leaves;
      if (leaves == 1) {
        printf("ENUM n=%u t=%u w=%u rounds=%u drop=%u tailSeed=%lu"
               " depth=%u\n",
               (unsigned int)NACT, (unsigned int)TVAL,
               (unsigned int)REACH, (unsigned int)ROUNDS, drp,
               tailSeed, (unsigned int)ENUMDEPTH);
        printf("ENUM deliveries in the leftmost run %lu; branch factors",
               EnumPops);
        for (i = 0; i < (unsigned int)ENUMDEPTH && i < EnumPops; ++i)
          printf(" %u", EnumBranch[i]);
        printf("\n");
      }
      /* the odometer: increment the deepest position that has an
       * unexplored sibling, zero everything below it */
      d = (int)(EnumPops < (unsigned long)ENUMDEPTH
                  ? (unsigned int)EnumPops : (unsigned int)ENUMDEPTH) - 1;
      while (d >= 0) {
        if (EnumChoice[d] + 1 < EnumBranch[d]) {
          ++EnumChoice[d];
          for (i = (unsigned int)d + 1; i < (unsigned int)ENUMDEPTH; ++i)
            EnumChoice[i] = 0;
          break;
        }
        --d;
      }
      if (d < 0)
        break;
    }
    printf("ENUM leaves %lu, widest queue any pop chose from %u\n",
           leaves, EnumMaxQ);
#if TVAL == 0
    printf("ENUM serve-bearing runs %lu of %lu (zero loss: reordering alone"
           " puts a process behind)\n", EnumServeLeaves, EnumLeafRuns);
    CurTest = "ENUM t=0 serve floor";
    CHECK(EnumServeLeaves > 0,
          "t=0: the serve floor of one carried the heal somewhere in the tree");
#endif
#if INJ_RATE > 0
    printf("ENUM injections by site");
    for (i = 0; i < INJ_SITES; ++i)
      printf(" %lu", EnumInjSite[i]);
    printf("\n");
    CurTest = "ENUM injector coverage";
    for (i = 0; i < INJ_SITES; ++i)
      CHECK(EnumInjSite[i] > 0, "injector: per-site coverage non-vacuous");
#endif
#ifdef ENUMLEAVES
    CurTest = "ENUM leaf count";
    CHECK(leaves == ENUMLEAVES, "enumeration visited the asserted leaf count");
#endif
  }
#else

  for (seed = 1; seed <= seeds; ++seed) {
    char name[64];

#if SWEEP_PLAIN
    sprintf(name, "PLAIN seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 8 : (unsigned int)drop,
                -1, -1, -1, -1, 0, 0);
#endif
#if SWEEP_LAGGARD
    sprintf(name, "LAGGARD seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                LAGPROC, LAGROUND, -1, -1, 0, 1);
#endif
#if SWEEP_STARVE
    /* STARVE -- the accepted-strand POSITIVE CONTROL.  The laggard cut
     * kills the O1 carrier for round 2 (no round-3 act ever reaches the
     * victim) while indStarve kills the indication carrier for the same
     * round, so the victim HOLDS round 2 and can never learn anyone else
     * does.  That is a genuine two-carrier expiry with correct glue, and
     * the ACCEPT path must fire -- without it the strand-shape arm would
     * go dormant and defend nothing. */
    sprintf(name, "STARVE seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                LAGPROC, LAGROUND, INDSTARVE, -1, 0, 1);
#endif

#if SWEEP_BYZ == 1
    /* THE LIVE BYZANTINE ARMS, one per mode, at the default point.  A
     * liar and a cut process are TWO faults, so none of these carries a
     * laggard: at t = 1 the budget buys exactly one. */
    sprintf(name, "BYZ-FORGE seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_FORGE_POSSESS, 0);
    sprintf(name, "BYZ-WITHHOLD seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_WITHHOLD, 0);
    sprintf(name, "BYZ-MIXED seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_MIXED_CANDIDATE, 0);
    sprintf(name, "BYZ-EQUIV seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_EQUIVOCATE_VALUE, 0);
    sprintf(name, "BYZ-SILENT seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_SILENT, 0);
    sprintf(name, "BYZ-CONTENT seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_WRONG_CONTENT, 0);
#elif SWEEP_BYZ == 2
    /* THE LARGE POINT.  Three arms only, for runtime -- and the last is
     * the COMPOSED one the budget at t = 2 finally admits: a liar and a
     * cut process at once, two faults inside t. */
    sprintf(name, "BYZ-FORGE seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_FORGE_POSSESS, 0);
    sprintf(name, "BYZ-MIXED seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_MIXED_CANDIDATE, 0);
    sprintf(name, "BYZ-WITHHOLD+LAG seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                LAGPROC, LAGROUND, -1, BYZPROC, BYZ_WITHHOLD, 1);
#elif SWEEP_BYZ == 3
    /* THE A6 PINS' ONLY SHARP SCENARIO.  With a mute process present,
     * all-n possession is unreachable for every round, so MET can never
     * fire and the tolerance escape is the sole route past round 0.
     * Pinning it shut wedges the cohort there; pinning it open carries
     * the cohort at wire speed instead of one rung per T_p. */
    sprintf(name, "BYZ-SILENT seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                -1, -1, -1, BYZPROC, BYZ_SILENT, 0);
#elif SWEEP_BYZ == 4
    /* THE ROTATION CLAUSE'S ONLY SHARP SCENARIO.  A laggard that can close
     * its round by adoption alone, and a flooding solicitor whose forged
     * serve duties sit ahead of the laggard's in the caller's duty order.
     * Two faults, so this arm runs at n = 7, t = 2, where the budget buys
     * them; the cap is then t = 2 grants a tick, and the solicitor's own
     * duties -- one per retained round, w = 3 of them -- exceed it. */
    sprintf(name, "FLOOD+LAG seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                LAGPROC, LAGROUND, -1, BYZPROC, BYZ_WANT_FLOOD, 1);
#elif SWEEP_BYZ == 5
    /* THE ONE SCENARIO A SHRUNKEN REACH IS ABOUT.  The withholder keeps
     * all-n possession unreachable, so nothing releases by the all-n path
     * and every close must EVICT -- which is what makes the reach the
     * binding retention constraint instead of a bound nothing reaches.  The
     * laggard is then the straggler the reach rolls away from. */
    sprintf(name, "WITHHOLD+LAG seed %lu", seed);
    runScenario(&R, name, seed, drop < 0 ? 4 : (unsigned int)drop,
                LAGPROC, LAGROUND, -1, BYZPROC, BYZ_WITHHOLD, 1);
#endif
  }
#endif                               /* SCHED_ENUM */

#if SWEEP_BYZ == 1 || SWEEP_BYZ == 2
  /* A COVERAGE arm, and it has a subject only where serves flow: a
   * fabricated composition reaches a witness path by riding one.  Under a
   * discharge order that granted nothing all run there is no carrier and
   * the arm has nothing to observe -- it lapses HONESTLY, named here
   * rather than silently passing on an empty set. */
  CurTest = "BYZ MIXED coverage";
#ifdef W_SERVEPRIO
  if (GrantsTotal)
#endif
  CHECK(ByzFabTotal > 0,
        "BYZ MIXED: fabricated compositions reached correct witness paths");
#endif

#if defined(W_A6_PIN1)
  /* THE PIN WAS CONSUMED.  A zero here would mean the withdrawal never
   * reached the machine and every green above it is vacuous.  The SHED
   * the arm was briefed to find does not appear at this configuration --
   * see THE PINS' RESULT in the header; what the pin DOES buy is the
   * mute arm's tick cost, which is L1's tolerance half measured. */
  CurTest = "W_A6_PIN1 coverage";
  CHECK(WUnearnedTotal > 0,
        "W_A6_PIN1: an unearned tolerance advanced the frontier");
#endif
#if defined(W_A5_NOINFER)
  /* A5's carrier removed, A8's indication left standing.  A zero here
   * says the indication alone carries every case this sweep reaches --
   * a claim about coverage, not about A5. */
  CurTest = "W_A5_NOINFER coverage";
  CHECK(WLostTotal > 0,
        "W_A5_NOINFER: the indication alone left a stall or a strand");
#endif
#ifdef W_A9_SYBIL
  CurTest = "W_A9_SYBIL coverage";
  CHECK(WSybilTotal > 0,
        "W_A9_SYBIL: evidence was recorded under a false sender");
#endif
#if defined(W_R2C_SILENT)
  CurTest = "W_R2C_SILENT coverage";
  CHECK(WLostTotal > 0,
        "W_R2C_SILENT: post-decide silence left a stall or a strand");
#endif
#if defined(W_REACH_WSHRINK)
  /* NON-VACUITY IS THE EVICTION, NOT THE STRAND.  The reach at its floor
   * must actually BIND -- else the arm says nothing about REACH at all --
   * and with a withholder present nothing releases by the all-n path, so
   * every close evicts.  The strand the brief predicted is REPORTED, not
   * required: see the header for what the measurement says. */
  CurTest = "W_REACH_WSHRINK coverage";
  CHECK(WEvictTotal > 0,
        "W_REACH_WSHRINK: the floor reach bound (rounds left by eviction)");
  printf("W_REACH_WSHRINK: %lu evictions, %lu stalls-or-strands\n",
         WEvictTotal, WLostTotal);
#endif
#ifdef W_SERVEFLOOD
  CurTest = "serve flood coverage";
  CHECK(WFloodTotal > 0,
        "flood: blind re-offers landed as want evidence");
  printf("serve duties: solicitor max %lu, correct cohort max %lu"
         " (cap %u)\n", WFloodDutyMax, WHonestDutyMax,
         (unsigned int)W_SERVECAP);
#endif
#ifdef W_L2_NOBYTEMATCH
  /* the withdrawn clause was REACHED: an assertion was counted toward a
   * candidate it is not byte-identical to.  Zero here would mean no mixed
   * set ever reached one book and every green above is vacuous. */
  CurTest = "W_L2_NOBYTEMATCH coverage";
  CHECK(WNoMatchTotal > 0,
        "W_L2_NOBYTEMATCH: a non-matching assertion was counted");
#endif
#ifdef W_L2_NOREARM
  CurTest = "W_L2_NOREARM coverage";
  CHECK(WNoRearmTotal > 0,
        "W_L2_NOREARM: a candidate switch kept the accumulated book");
#endif
#ifdef W_L2_NOCLOSEVOID
  /* THE RACE ITSELF WAS REACHED.  Zero here is not a green: it says no
   * schedule in this sweep ever had an ADOPT standing unconsumed when a
   * process's own COMPLETE closed the round, so every green above is
   * VACUOUS and the arm covers nothing -- the register entry would then
   * read "not reachable in this glue shape", never that the clause is
   * unnecessary. */
  CurTest = "W_L2_NOCLOSEVOID coverage";
  CHECK(WNoCloseVoidTotal > 0,
        "W_L2_NOCLOSEVOID: an own COMPLETE closed over an unconsumed ADOPT");
  printf("W_L2_NOCLOSEVOID: %lu unvoided adopt debts survived an own close\n",
         WNoCloseVoidTotal);
#endif
#ifdef W_I10_WRONGARTIFACT
  /* REACHABILITY, REPORTED EITHER WAY (the register discipline).  A zero
   * here is not a green: it says no schedule at this configuration put a
   * candidate beside a close that named different bytes, so the withdrawal
   * is NOT COVERED here -- never that the caller half is unnecessary. */
  CurTest = "W_I10_WRONGARTIFACT coverage";
  CHECK(WStaleTotal > 0,
        "W_I10_WRONGARTIFACT: a close stored an artifact it did not name");
#endif

  printf("\n%d checks, %d failures\n", Checks, Failures);
  return (Failures ? 1 : 0);
}
