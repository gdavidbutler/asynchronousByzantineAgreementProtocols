# System -- the obligation layer above ACS

## Status

This layer has no governing paper.  The layers below are governed by
Bracha87.txt (reliable broadcast, binary agreement) and BKR94ACS.txt
(agreement on a common subset); the reliability discipline by SRC84.txt
(end-to-end argument).  This file is the governing specification for
the composition layer -- the component that runs an unbounded sequence
of ACS instances over an application input stream -- and is constructed
from those three extracts, the repository's Bracha Phase Retry
mechanism (BPR -- in-tree code and tables with no paper of their own;
the message-level calculus this file lifts), and the REQUIREMENTS
below.  The companion decision tables are system.dtc; the state
machine is system.[hc] (see Mechanization status at the end for the
declarative/structural split and what is deliberately caller-side).

This repository stands alone.  Nothing here names an external design:
every capability the calculus depends on that this repository does not
supply is pinned in IMPLEMENTER OBLIGATIONS as a property the deployer
must provide -- the same pattern as the transport, identity, coin, and
abandonment obligations README.md pins for the layers below.  The
calculus depends on the stated properties, never on any construction.

Rules are marked DERIVED (forced by the requirements and obligations;
the derivation is recorded at the rule).  What remains configurable
is sizing only (T_p, S, the window, the retention span), which is
tuning.

## Requirements

The layer sits between two fixed jaws.  Below: the ACS and reliable
broadcast libraries, whose correctness proofs CONSUME premises that
therefore bind every deployment.  Above: the application's purpose and
the implementer-provided capabilities.  Worked through, the jaws force
nearly every rule; a "question" at this layer is almost always an
unstated requirement, not a choice.

R1  PURPOSE.  An accepted application value is owed exactly-once
    appearance in the agreed sequence.  Acceptance happens when the
    application presents the value and the layer accepts it
    (refusing is backpressure); from that point the value is an
    obligation, not a preference.

R2  CONFORMANCE.  The papers' premises are obligations on every
    deployment, because the proofs consume them:
    (a) Participation.  Agreement[Q] assumes every honest process
        eventually enters, per invocation, and Lemma 2 rides on it
        (BKR94ACS.txt, problem setup).  A process aware of a round
        that fails to participate is violating the proof's
        precondition -- one more silent honest process among t silent
        faults leaves at most 2t instances able to decide inclusion,
        below the n-t threshold, and the round cannot complete.
        Participation is conformance, not politeness.
    (b) Single entry.  BA semantics demand a single input per
        (process, instance phase) (BKR94ACS.txt, remarks for
        implementers).  A second execution of a possibly-decided
        instance can legitimately enter differently (arrival order
        feeds the n-t gate), and receivers latch first-arrival per
        sender -- so re-execution is wire-indistinguishable from
        equivocation.  An interrupted discharge must RESUME with its
        instance state intact, never re-execute.
    (c) Post-decide continuation.  Success signals never stop
        anything at any layer; a decided process keeps broadcasting.
    (d) Exclusion is final at the agreement layer.  The paper
        substitutes for excluded inputs and never re-enters them
        (BKR94ACS.txt, usage facts); anything owed beyond the round
        lives above the agreement layer, i.e., here.

R3  PROVIDED CAPABILITIES.  The deployment supplies the implementer
    obligations O1-O5 below with the stated properties.

R4  FAULT BUDGET.  t is reserved for faults.
    Offered load may not spend the fault budget on an honest
    process, and the layer promises so WITHOUT any synchrony
    assumption -- the model admits none, so no mechanism here may
    lean on a stability period.  The mechanism is PARTICIPANT
    TOLERANCE, the frontier-advance rule:

      Having completed or adopted round R, a process advances its
      cursor to R+1 -- by admitting OR by joining an evidenced R+1 --
      only when:
        (1) for CHOSEN advances (admit, maintain) only: its own
            backlog has drained -- every result its prior advances
            accepted has reached its decision stream (M2).  A join
            is never capacity-gated -- owed work is paced by the
            tick alone (M1), else an adversary delaying our
            deliveries could defer participation;
        (2) n-t processes evidence composition possession of R.
            Withholding cannot block this -- t withholders leave n-t
            honest -- but no fault count alone assures it: the
            threshold counts evidence RECEIVED, and evidence needs
            a carrier still running to bear it (SERVE, RETAIN); and
        (3) all n evidence possession of R, or T_p sweeps have
            elapsed since (2) -- the tolerance.
      During the hold, post-decide tails and serves keep running
      (R2c):
      the hold is funded catch-up time for the tail, not idleness.
      A round no longer retained reads as duty met -- duty is
      bounded by retention (RETAIN); an evicted round is already
      out-of-band territory.

    The rule is BPR's own two-threshold discipline lifted one
    level: proceed at threshold, discharge duty toward all-n, and
    retire the duty only on remote evidence (all-n) or an explicit
    bounded budget (T_p) -- never implicitly on own progress.  One
    level down that is exactly ACCEPT-at-2t+1 beside
    ready-retries-until-all-accepted; the deployment defect this
    rule prevents is the round-level violation of the same
    discipline (the duty window quiescing as a side effect of the
    progress budget while the frontier ran free).

    The budget split, quantified: t withholding processes cost at
    most T_p sweeps per round -- a bounded constant factor on pace,
    never ownership of it.  Every honest straggler is guaranteed
    T_p sweeps per round of warm-window catch-up.  A process sheds
    only when its per-round deficit exceeds T_p -- a genuine
    capacity shortfall or a real fault, never latency spread.  The
    tolerance applies to JOINS as well as admissions because a
    Byzantine process that launches R+1 immediately would otherwise
    void every honest process's hold: an evidenced R+1 is joined
    only under (2)+(3), which is safe -- bounded deferral preserves
    R2a's eventual participation, and the early round simply waits.

    Two parameters result, deliberately separate: T_p (the DUTY
    budget: per-advance patience) and S (the PROGRESS budget:
    barren sweeps to abandonment), with S > T_p.  Sizing both, the
    window, and the retention span is tuning.

## Implementer obligations

The pattern of README.md one level down: a complete deployment
supplies the transport (pairwise; confidential; eventually delivering
via BPR retry), sender-authenticated ingress, the coin, the tick, and
the abandonment policy.  This layer adds five obligations.  Each is a
PROPERTY the calculus consumes; the construction, formats, and
algorithms are the implementer's.

These obligations read finer-grained than the model assumptions of
Bracha87.txt and BKR94ACS.txt; the difference is the problem's, not
the style's.  A single instance of broadcast or agreement consumes
nothing more than a connected asynchronous network and a fault
bound, so those papers assume coarsely.  An unbounded sequence
consumes more -- joining late, serving across a retained span,
re-presenting byte-identically, releasing only on evidence -- and
each obligation states one of those capabilities at exactly the
grain the calculus consumes it.  They are DERIVED like the rules,
not assumed for convenience: each records, at "Consumed for", what
rests on it, and weakening any one leaves a requirement
undischargeable.  What the papers assume coarsely survives
unchanged in the preamble above; the numbered obligations are the
residue the composition problem itself forces.

O1  LINKAGE.  Round identity derives from the predecessor's agreed
    result, such that traffic for round R can be authenticated only
    by a holder of round R-1's result, and such that the linkage
    makes agreed-result-hood DECIDABLE: a holder of round R-1's
    result can decide, for any candidate result for R, whether it
    is R's agreed result, and the derivation BINDS the decision
    exact -- no two distinct round results derive the same identity,
    so a candidate result that folds to a verified identity IS the
    round's agreed result.  The derivation's BASE -- the first
    round's predecessor result -- is provisioned commonly to every
    process before any round runs, out of band, alongside the
    identity material the deployment already delivers.
    Consumed for: chain reach (what a process can verify); possession
    monotonicity -- an authenticated round-R act is, by this property,
    evidence its sender holds R-1's composition (see possession
    evidence, below); the fold-proof ground of O3 (the binding is
    what makes the fold a PROOF -- L2); the induction base of the
    sequence (A11 -- L6, and L2's fold case at the first round).

O2  TWO-GRAIN ARTIFACT.  The artifact of a round has two grains:
    COMPOSITION -- the agreed subset plus a per-member digest, fixed
    by agreement itself -- and per-member CONTENT, reconstructible
    from any t+1 correct holders out of the pieces distributed at the
    round, each piece self-validating (the receiver detects a bad
    piece and discards it; it never corrects -- BKR94ACS.txt, the
    asynchronous reconstruction limits).
    Consumed for: two-grain possession; the per-member out-of-band
    boundary (an unrecoverable member costs its own content line,
    never the round).

O3  WITNESSABLE SERVED FACTS.  A fact served to a process that did
    not witness the round's agreement is acceptable on exactly two
    grounds: identical assertion by t+1 DISTINCT servers (at least
    one honest), or provability from the linkage fold given one
    honest assertion.  Nothing weaker admits a served fact.
    An assertion carries the identity of the round it speaks (O1),
    and assertions are identical only as (identity, content)
    pairs -- a threshold is met only by servers asserting the same
    content of the same round.
    Consumed for: adoption (the wanting side's close); the
    thresholded evidence class.

O4  FORWARD-ONLY DERIVATION WITH A FLOOR BASE.  Round-keyed
    derivation state advances forward-only -- an advanced-past step is
    unrecoverable (that destruction is a security property the layer
    must not undo).  The implementer retains, AT THE RETENTION FLOOR,
    a recomputation base sufficient to re-derive any retained round's
    per-recipient serving form by computing forward from the floor.
    Consumed for: serve dischargeability across the retained span
    (base plus recompute -- never per-round snapshots, never retention
    of advanced-past steps); the coupling of the derivation floor to
    the retention floor (release advances both together; forward
    secrecy is bounded by the floor, and that bound is the accepted
    cost of serving).

O5  BUDGETED AUTHORSHIP.  Authoring an authenticated value consumes a
    finite budget.  Re-presentation of an accepted value must be
    byte-identical to its first authored form (a re-authored
    duplicate is detectably distinct and rejectable).  Budget
    exhaustion forces an identity-maintenance round; such a round
    must not consume a pending application value (its win is not the
    value's win).
    Consumed for: PRESENT's retire condition and its byte-identity
    constraint; ADMIT's second birth cause and its precedence.

## Model

n processes, up to t Byzantine, n >= 3t+1 (inherited).  A CORRECT
process follows this specification and keeps taking steps -- its
sweeps and ticks keep coming for the run's duration.  A process
that stops stepping is within the t faults or has abandoned (the
application's exit); liveness claims (L1) consume the stepping the
way delivery consumes A4.  The deployment runs ACS instances in
sequence; a ROUND is a position in that unbounded sequence, and
"round R" means the R'th position.

One word could carry three senses here, so the senses are pinned
before anything consumes them.  A PROCESS is the inherited ACS
actor: one of the n, the thing that steps, launches instances,
authors acts, and owes duties.  The SYSTEM is the n processes
taken together -- BKR94's own sense of the word, the container the
title names and what this layer's obligations serve.  It is a set,
never an actor: no act is authored by it, no duty is owed by it,
and no lemma quantifies over it -- every statement below
quantifies over processes and over one process's retained state,
and a drafter reaching for a global claim must quantify over
processes explicitly.  A DEPLOYMENT is an operational embodiment
of the system: the n processes plus what IMPLEMENTER OBLIGATIONS
makes the deployer provide (transport, attribution, provisioning)
and the recorded sizing choices; the axioms cite it exactly there
(A9's ingress attribution), and anything "the deployment" does is
discharged by some process's implementation loop, never by the
ensemble acting as one.  In the mechanization, struct system
(system.[hc]) is one process's SEAT in the system -- per-process
obligation state, n seats per system, exactly as struct bkr94acs
is one process's seat in one ACS instance.  The inverted reading
-- "the system inside a process" -- is wrong by this paragraph.

Three names attach to a round, and the calculus is exact about
which is consumed where.  The POSITION is what the lemmas quantify
over -- unbounded, totally ordered (A7).  The round BYTE is the
mechanization's bounded name for a position -- the natural demux
key a deployment routes traffic by, at the width the layers below
already speak on the wire.  The byte wraps: it names a position
unambiguously only across the retained window and its immediate
reach, and that bounded soundness is exactly I1 and the wrap
release (L7).  The width is sufficient because the nameable span
is bounded by the calculus, not by the name: at most the retained
window, the live instance, and the verification reach are ever
distinguishable at once, and a round behind retention is
out-of-band territory at ANY width -- a wider name would only
label territory already priced unrecoverable.  A deployment whose
retention span outgrows the wrap space widens the name as a
mechanization change: nothing in the calculus or the lemmas
consumes the specific width, only bounded, and released before
recurrence.  The round IDENTITY (O1) is the unbounded name
-- derived from the predecessor's agreed result, unique by the
binding (A10), common from the provisioned base up (A11).  It is
the name the deployment's authentication speaks: an act is OF a
round by its identity (existence evidence, verified within chain
reach), FROM its author by attribution (A9), and authored within
a finite budget (O5) -- all three speak identities.
A byte ROUTES; an identity PROVES.

Each process runs at most one non-COMPLETE instance at a time and
takes rounds in order (a deployment choice of this specification,
not a library demand).  An instance's COMPLETE -- the library's
BKR94ACS_ACT_COMPLETE, all n BAs decided -- is the only
cross-process clock.  No wall-clock predicate appears anywhere
in this specification; wall time may pace actions (the tick) but
never decides anything.

Executions are CAUSALLY WELL-FOUNDED, and the proofs consume that
where they induct over histories.  An act is authored before it is
received; a received act is among the causal predecessors of every
later local step at its receiver; and an event of an execution has
finitely many causal predecessors.  The causal order is therefore
a strict partial order with no infinite descent: a chain that
steps to a strictly earlier event bottoms out, and no event
precedes itself through any chain.  Asynchrony weakens DELIVERY
(A4 is all that is promised of it), never this -- delay and
reordering cannot deliver an act before it is authored.  L2's
witness ground inducts over exactly this order.

EVIDENCE is what obligations are born from and retired on.  All of it
rides traffic that already flows -- this specification adds no message
kind and no manufactured control traffic:

  existence of R -- an authenticated act of round R, verified within
    chain reach (O1).  Traffic beyond reach is not evidence: its
    sender still owes it, so it re-arrives on the sender's own retry
    cadence once reach extends; a caller may hold it as a latency
    optimization, but no obligation reads the hold, and an unbounded
    hold would be the remote inflation lever the Byzantine notes
    exclude.  (The frontier round's possession indications are the
    opposite case -- their carrier retires at quiescence, so they
    MUST be held: C11.)
  want of R -- an act of R received from a process which, per
    retained state, has not evidenced possession of R's composition.
    Want is observed, never inferred from the observer's own needs.
  possession -- two grains (O2):
    COMPOSITION possession of R, per process: binary.  A process
    HOLDS a round's composition only through the close -- own
    COMPLETE or adoption (RETAIN's births); below O3's grounds a
    served assertion is accumulating evidence, not a holding.
    Sources:
    an indication riding traffic already being sent (the precedent
    one level down is the accepted indication riding an
    already-retried ready), and -- DERIVED from O1 -- any
    authenticated act of round R+1 or later from that process
    (it could not have authored the act without holding R's
    result).  The inference has the same Byzantine containment as
    the indication: it can only mark its own sender.
    CONTENT possession, per (round, member): reaching the t+1
    reconstruction threshold for that member's content (O2).
    Composition possession drives the clock, serving of links, and
    release; content possession may lag indefinitely, and its
    failure boundary is per member, out of band.
  witnessed facts (O3) -- a served assertion held pending until t+1
    distinct servers match (or the fold proves it); below threshold
    it is accumulating evidence, not a fact.

## The calculus

Two meta-rules govern every obligation.  Both are the Bracha Phase
Retry discipline lifted from message granularity to round
granularity.

M1 -- retirement (DERIVED).  An obligation, once born, retires only on
evidence that no correct process can still need its discharge, or
when its subject passes out of every correct process's reach (the
accept-loss boundary; resolution is then out of band).  Local
progress, appetite, or silence never retire anything.  This applies
to EVIDENCE the layer accumulates as much as to actions it owes:
witness accumulations on the wanting side survive local quiet, or
their threshold restarts on every gap.  Precedent one level down: a
sent ready is owed on local state forever and retires only on the
remote all-accepted fact.

M2 -- assumption (DERIVED).  ADMIT is the only obligation a process
assumes by choice.  The choice is gated on self-local capacity --
backpressure -- and on the R4 advance rule (bounded remote-evidence
coupling: n-t possession, then all-n or T_p), and on nothing else.
The backlog is the process's own undelivered results: it has
drained when every result its prior advances accepted has reached
its decision stream (the caller supplies the fact, read from its
own books).  Emission is a put on a carrier contracted to
eventually drain (A4) and attests nothing beyond the attempt; the
gate consults no emission state, because a blind transmitter has
none to consult.
Every other obligation is born from evidence.

The five obligations:

PARTICIPATE (DERIVED -- R2a)
  born:      existence evidence of a round this process has not
             reached (a live instance is discharge already in
             progress).  Not optional: R2a makes joining an
             evidenced round conformance to the proofs below.
  discharge: run the instance for R, in round order, at this
             process's cursor.  Advancing the cursor TO an evidenced
             round is subject to the R4 advance rule -- tolerance
             governs joins exactly as admissions, else an early
             launcher voids every hold.  A pending accepted value
             rides any joined or admitted round (see PRESENT; a
             maintenance round carries the deployment's maintenance
             form instead -- O5) -- participation is contribution.
             An interrupted discharge RESUMES with
             its instance state intact (R2b); a from-scratch second
             execution is forbidden.
  retire:    own COMPLETE of R, or ADOPTION of R (the wanting side,
             below) -- whichever lands first; the two enter one
             consume region, and own COMPLETE supersedes a partial
             adoption.  Or: R unrecoverable (out-of-band boundary,
             surfaced to the abandonment policy).

PRESENT (DERIVED -- R1, R2d, O5)
  born:      a presented application value is accepted (R1).
  discharge: the value rides every round this process launches --
             joined or admitted; never a maintenance round (O5) --
             byte-identical at every presentation (O5), until
             agreed.
  retire:    the value witnessed as a member of an agreed subset
             (exactly-once: R1).  Exclusion from a round does NOT
             retire (R2d -- exclusion is final below, so the
             obligation persists here); an identity-maintenance win
             does not retire it (O5).  Unrecoverable only with the
             process itself (abandonment).

ADMIT (DERIVED -- M2 + R4)
  born:      by choice at a launch opportunity: (a) an accepted
             value is pending, or (b) identity maintenance is due
             (O5).  Birth (b) outranks (a) and must not consume the
             value.
  gate:      self capacity -- the process's own backlog has drained:
             every result its prior advances accepted has reached
             its decision stream (self-local: read from its own
             books, so no remote can force it open; holding it shut
             is bounded delay, never denial -- discharge rides the
             serving side, O2/O3) -- AND the R4 advance rule: n-t
             composition possession of the prior round, then all-n
             or T_p sweeps of tolerance.
  discharge: the admitted round becomes the live instance.

SERVE (DERIVED -- O2, O3, O4; bounds derived below)
  born:      want evidence for a round this process holds and
             retains, per (round, wanting process).  Two grains:
             composition serves (the link and member digests, O3-
             witnessable) and per-member content serves (pieces
             toward the wanting process's t+1, O2).
  discharge: recompute the wanting process's serving form from the
             floor base forward (O4) and send, paced by the tick.
             Some content serves have exactly one correct
             discharger (a member's own content may exist nowhere
             else); if it fails, that member is out of band FOR
             THAT PROCESS -- a per-member cost, never the round's
             (O2).
  bounds:    concurrency is capped at t in-flight wanting
             processes, and never at fewer than one (DERIVED, the
             cap and the floor separately.  The cap: while the
             sequence advances, n-t are completing rounds, so more
             than t cannot be genuinely behind, and slots beyond t
             serve nobody honest.  The floor: the cap's derivation
             holds while the sequence advances -- during a hold it
             bounds nothing -- and a genuinely-behind honest
             process is possible at every t.  The usual one is
             the abandonment posture: the policy fired on a round
             that did not complete and the process re-offers
             blind at its stale cursor (the three states) --
             expected operation, not a fault.  The extreme one is
             an instance that exhausts: a fault-free outcome
             whose holder keeps stepping, never within the t
             faults (R4: the budget is not spent on honest
             processes), and whose round can close no other way --
             no premise about instances resuming could substitute
             for the serve.  A cap read to zero would retire
             SERVE by silence, which M1 forbids, and would make
             the rotation's own guarantee below unsatisfiable).
             Grants
             beyond the cap queue and are granted oldest-want-first
             in rotation (DERIVED from M1: a correct wanting
             process is eventually served; a flooding solicitor
             must not displace it).
  retire:    per process, on that process's composition possession
             of the round; per round, on release.

RETAIN (DERIVED -- O2, O4; budget rule below)
  born:      own COMPLETE or adoption of R.  Composition is in hand
             by definition of either; content is retained as
             assembled and may hole per member (normal; the
             out-of-band tail is per member).
  budget:    one retention budget spans BOTH the serving state of
             completed rounds AND the wanting side's pre-adoption
             witness accumulations (M1: evidence survives local
             quiet).  Eviction is oldest round first across both
             kinds; within a round, the process's own lifeline
             (what only it can re-derive) goes last.
  retire:    RELEASE -- all n processes have evidenced composition
             possession of R (the quiescence analog; no
             count-threshold shortcut), budget eviction, or the
             round-name wrap boundary (encoding-derived: an entry
             releases before its byte recurs; eviction-class).
             Release advances the derivation floor and the
             retention floor together (O4) -- the floors stand at
             the oldest retained round, so a release advances them
             only when it releases the oldest; a younger round's
             release moves nothing.  An evicted round is
             out-of-band territory.

## The three states (DERIVED -- R4)

Every process is, at all times, in exactly one SELF-classified state.
No process ever classifies another -- the states need no wire
representation and read no network condition.  PARTITIONED is the
DEFAULT: under unbounded latency no network condition is readable,
so no process can ever prove it is NOT partitioned -- it can only
prove, from self-local evidence, that it is presently participating
or recovering.  A process not provably in either is PARTITIONED.

  PARTICIPANT -- provably discharging PARTICIPATE through live
    instances, advancing under the R4 rule.  A participant with
    nothing to send is still a participant (R2a); a participant
    holding under tolerance is still a participant (the hold is
    discharge of duty, not absence).  The proof is the progress
    evidence itself, metered by the sweep counts: while evidence
    keeps arriving the state holds; when the budget exhausts
    without it, the proof has lapsed and the state is PARTITIONED
    by default.
  PARTITIONED -- not provably either other state.  The abandonment
    policy firing on a round that did not COMPLETE is the process
    ACTING on this classification -- the policy is how a deployment
    reads the lapse, its barren-sweep budget the proof's meter --
    not the classification itself.  The response is blind and
    two-pronged, expecting nothing: re-present the round and keep
    launching at the stale cursor -- those acts ARE the want
    evidence servers observe.
  RECOVERING -- provably discharging PARTICIPATE through adoption
    (the wanting side, below): served evidence is arriving and
    closing rounds.  A recovering process rejoins the participants
    when its cursor reaches the frontier; its heal is funded by
    other participants' tolerance holds each round, which is why
    R4's guarantee is stated per round: deficit beyond T_p per round
    is capacity or fault, not schedule.

## The wanting side (DERIVED -- R2b, O1, O2, O3)

A process behind at R does not COMPLETE R by running its instance to
threshold; it closes R from served evidence:

- It participates at its own cursor (PARTICIPATE; its stale acts ARE
  the want evidence servers observe -- nothing is announced, nothing
  solicited).
- Served assertions accumulate per O3: composition facts close on
  t+1 distinct matching servers or on fold-proof from one honest
  assertion; content closes per member at t+1 pieces (O2).
- ADOPTION is the close: composition proven, the round enters the
  same consume region a live COMPLETE enters (one region -- two
  copies drift), the cursor advances, and the superseded instance
  state is retired THROUGH that region, never beside it.  Own
  COMPLETE arriving first supersedes the accumulation.
- Accumulated witness evidence is M1-protected state: it survives
  local quiet and is released only by adoption, supersession,
  eviction (RETAIN's budget), or the process's own abandonment.

## The lift, explicitly

  message level (Bracha87 + BPR)          round level (this layer)
  --------------------------------        --------------------------------
  sent flags (initiator/echoed/rdsent)    obligations (participate/present/
                                          serve/retain)
  retire on accepted / all-echoed         retire on possession / all-n
  (both witness remote facts)             composition possession
  per-process suppress bitmaps            per-process possession record
  accepted indication riding a ready      possession indication riding
                                          traffic (+ the O1 inference)
  forbidden: ready-retire at local        forbidden: local-intent
  accept, initial-retire at merely-       participation, appetite
  echoed (the weak local gates)           admission, local-floor release,
                                          silence-retired serves or
                                          witness evidence
  quiescence (all n accepted)             release (all n possess)
  abandonment (application policy)        out-of-band boundary
                                          (eviction / per-member loss)

The end-to-end justification transfers unchanged (SRC84.txt): the
"still owed" predicate for rounds -- who has evidenced possession,
what is retained, what is within reach -- lives only at this
endpoint, so the participation/presentation/serve/release decisions
are made here and cannot be delegated below without folding them
back.  It also places adoption's close at the wanting process: only
it knows, from its own accumulated evidence, what it is still owed.

## Byzantine notes (every input)

  existence evidence -- beyond chain reach (O1) it cannot create
    obligations.  Verified existence authored by a Byzantine process
    is simply participation: the round advances the shared sequence
    and carries this process's pending value too; work is bounded by
    one live instance at a time.
  want evidence -- a forged want adds only the forger to a round's
    owed set; its cost is serves to the forger alone, bounded by the
    retention lifetime AND by the serve cap's rotation (it cannot
    displace a correct wanting process).
  possession evidence -- indication or O1 inference, either marks
    only its own sender: forged, it retires only serves owed TO the
    forger and can never strand a correct process.  Release requires
    all n composition-possession records; with at most t forgeries,
    reaching n requires every correct process.  No count-threshold
    shortcut.
  witnessed facts -- t+1 distinct servers include at least one
    honest, so Byzantine servers can withhold or delay a fact but
    never poison one; below threshold nothing is acted on.
  fold candidates -- O3's second ground admits on ONE assertion, so
    a Byzantine server can supply a candidate; by the O1 binding
    (A10) at most one result folds to a verified identity, so a
    forged candidate fails the fold and is discarded -- a server can
    impose bounded verification work, never a false adoption.
    Chain reach (O1) bounds which identities verify at all.
  own backlog drained (delivery debt: results not yet at the
    decision stream) -- the fact is self-local, read from own books;
    no remote lever on the reading.  Discharge is delay-bounded,
    never deniable: admitted members carry correct holder sets (O2,
    O3), so a remote can slow the drain, never own it -- bounded in
    both directions, like the advance rule's inputs below.
  the launch inputs (value pending, maintenance due) -- self-local
    application facts; no remote lever.
  the advance rule's remote inputs (R4) -- bounded in both
    directions.  Withholding possession: t withholders cannot block
    the n-t signal (n-t honest suffice) and cost at most T_p sweeps
    per round past it -- a constant factor, never ownership of the
    pace.  Forging possession: an indication or inference marks only
    its own sender, and t forgeries plus self cannot reach n-t
    without honest evidence, so early advance cannot be forced.
    Early launching: an evidenced R+1 is joined only under the same
    rule, so launching early gains its author nothing.
  tolerance elapsed -- a count of this process's own sweeps (its own
    funded re-offers); self-local, like the backlog gate.
  retention budget -- self-local.  Serving state grows only with own
    COMPLETEs; witness accumulations grow only for own misses and
    latch per (member, server); no remote inflation lever.

## Lemmas and the state invariant

The layers below are governed by papers that PROVE their properties
(Bracha87.txt Lemmas 1-4 and Theorem 1; BKR94ACS.txt Lemma 2 Parts
A-D); neither paper tests.  This layer has no paper, and everything
above is marked DERIVED -- which justifies a RULE (the rule is forced
by the requirements) but asserts nothing about a RUN.  This section
supplies the missing form: an inductive state invariant and the
lemmas it carries.

The state invariant is discharged below (the induction); the lemma
proofs build on it and are written against exactly these
statements.  The mechanization is sequential --
ten entry points, one at a time, no interleaving inside the machine
-- so every lemma's
proof obligation is ordinary induction: systemInit establishes I, and
each entry preserves it.  The decision tables discharge the case
analysis by construction (exclusive and exhaustive, dtc-compiled), so
the induction is over entry points, not over rule combinations.

### What the proofs consume

  A1  ACS agreement: two correct processes that COMPLETE round R hold
      the same composition (BKR94ACS.txt, Lemma 2 Part C).
  A2  At most t Byzantine; n >= 3t+1 (inherited).
  A3  A correct server asserts, for round R, only the composition it
      holds for R.
  A4  Eventual delivery between correct processes (the transport
      obligation, discharged by retry).
  A5  O1 linkage: an authenticated act of round R+1 or later
      evidences its sender's possession of R.
  A6  The self-local gates (backlog drained, tolerance elapsed) are
      computed honestly by the caller.
  A7  Rounds are taken in order and the frontier is monotone over
      the unbounded sequence (the round BYTE is not monotone; it
      wraps).  This is the deployment choice recorded in Model, and
      L6 consumes it exactly as it consumes A1.
  A8  A correct process evidences COMPOSITION possession of R -- by
      the indication riding its traffic, or by authoring an
      authenticated act of a later round -- only when it holds R's
      composition.  A5 supplies the unforgeability of the second
      source; this supplies the truthfulness of both, and is honest
      behavior, not a property of the linkage.  L5 does not close
      without it: all-n possession proves every correct process's
      bit is genuinely its own, which is worth nothing unless a
      genuine bit means the sender actually holds the round.
  A9  Ingress attribution: the deployment's sender-authenticated
      ingress (the pattern above, like the transport of A4) binds
      every act to its true author, and the caller passes that
      author as the sender of the evidence it records.  The machine
      marks only the sender it is given; this supplies what that
      sender ARGUMENT means.  With A2, at most t possession bits are
      forgeable, and none of them a correct process's.
  A10 O1 binding: at most one round result derives a given round
      identity, so a candidate that folds to a verified identity is
      the round's agreed result.  What makes O3's fold ground a
      proof, and the fold case of L2.
  A11 Genesis: every correct process starts from the same linkage
      base (O1's commonly provisioned base of the derivation).  The
      induction base of L6, and what makes the first round's
      identity verifiable at all -- without it O1's fold has nothing
      to bottom out on.

### The state invariant I

State per instance: the frontier F; the live / owed / adopt flags;
the frontier round's distinct-witness record W; and, per retained
round r, the possession record P_r, the still-owed record Q_r, and
the held-members record H_r.  The invariant speaks the machine's
name for a round -- F and every retained r are round BYTES (Model);
the lemmas speak positions, and A7 carries between the two.

  I1  Retained rounds are pairwise distinct, number at most the
      window, and each lies strictly behind F in wrapping order
      within 255.  The COUNT bound is not a proof obligation -- it is
      enforced by the allocation (the window is a fixed array of w+1
      slots, and insert evicts when no slot is free).  DISTINCTNESS
      and strictly-behind-within-255 ARE proof obligations, at every
      window size: sibling rounds releasing at all-n keep the window
      unfull, so an old entry can survive eviction indefinitely and
      is held off its own byte's recurrence solely by the frontier+1
      lookahead release -- the load-bearing guard at every window
      size, not only at the encoding's edge.
  I2  For every retained r: P_r and Q_r are disjoint.  (Possession
      recording clears want; this disjointness is what makes serve
      retirement sound.)
  I3  For every retained r: self is in P_r.
  I4  For every retained r: P_r is short of all n.  (Reaching all n
      releases the round in the same call, so an all-n retained
      entry never exists between calls.)
  I5  The adopt latch is set only with at least t+1 distinct
      witnesses recorded, and self is never among them.
  I6  W and the adopt latch pertain to F alone; both are clear
      whenever F has just advanced.
  I7  F is not retained.
  I8  RELEASE fires on the possession paths exactly when the
      post-record reaches all n -- not earlier, not later.  This one
      is a TRANSITION property, not a resident-state property: I1-I7
      constrain only resident state, so a release that fires early
      leaves a perfectly legal state behind and is invisible to them.
      Releasing late is I4's business; releasing early is this
      conjunct's, and L5 rests on it.  Eviction and the wrap boundary
      are excluded by construction -- RETAIN permits those below all-n
      as the out-of-band boundary.
  I9  A round leaves the retained set ONLY by an output RELEASE act,
      and every RELEASE names a round that was retained.  The other
      half of I8's question -- I8 fixes WHEN a release fires, this
      fixes that a departure is always ANNOUNCED.  Without it an
      overwritten window slot vanishes unnoticed: resident state
      stays legal, but the caller is never told to free the artifact
      and the round's serve obligation disappears.  Also a
      transition property.  Stated as two containments rather than
      an equality,
      because an eviction whose freed slot is immediately reused for
      the new frontier round legitimately leaves that byte retained.
  I10 A round ENTERS retention only at completion, as the round that
      completed.  Three parts:
        (a) no entry point other than systemComplete adds a round to
            the retained set;
        (b) a systemComplete that retains adds exactly one round,
            equal to the frontier BEFORE the advance;
        (c) at birth the round's possession record is exactly {self}
            and nothing is owed.
      I8 and I9 constrain only how rounds LEAVE; without it a
      machine that completes R and retains some other byte satisfies
      every other conjunct while serving the wrong round, releasing
      the wrong artifact, and making L6's "the entry for R" denote
      nothing.
      BOUNDED BY THE API: this binds the round BYTE only.  The
      machine never sees a composition -- completion carries its
      round byte and the held-members bitmap and nothing else --
      so "the entry holds
      round R's actual result" is not expressible here and is a
      caller obligation, in the same pattern as L4's caller half.
  I11 An eviction releases the OLDEST retained round -- the greatest
      wrapping distance behind the frontier, well-defined because
      retained rounds are distinct (I1).  Both eviction sites are
      bound: the window-full path at completion and the explicit
      eviction entry.  I8 fixes when the possession-path release
      fires and I9 that every departure is announced; nothing else
      constrained WHICH round an eviction takes, and a machine
      evicting the newest satisfies I1-I10 while discarding the
      round most likely to still be wanted -- RETAIN's budget rule
      (oldest round first) made mechanical.  The wrap-boundary
      release needs no exclusion here: the recurring byte is at
      distance 255, which is maximal, so it is the oldest.  Vacuous
      at a single-slot window, where the choice is forced.

  Discharge shapes.  I1-I7 are resident-state conjuncts.  I8, I9,
  I10, and I11 are transition properties, and are carried in the
  proof as PER-ENTRY-POINT obligations rather than as a separate
  history category -- the induction is already per entry point, so
  each entry gets its clause (nine of ten add no retained round;
  systemComplete adds exactly the pre-advance frontier; releases fire
  only where I8 permits and are always announced; an eviction takes
  the oldest retained round; a possession-path entry mutates, in any
  retained round's records, only the bits of the sender it was given
  -- the clause L5's attribution step consumes).

### The induction

THE INVARIANT HOLDS.  systemInit establishes I1-I11, and each of
the other nine entry points preserves them, its per-entry
transition clause included.  Every reachable state of an accepted
instance is initialization's state or some call's successor, so
I1-I7 hold in every resident state and I8-I11 at every call.

The case analysis inside an entry is the decision tables': their
rows are mutually exclusive and exhaustive at compile time (the
dtc discipline), so each entry contributes exactly one answer per
input and the induction is over entry points alone.  A mechanized
site presents the inputs its event cannot vary as constants and
applies only its own event's outputs, so each case below reads the
tables under that site's pinned inputs.  An entry that refuses its
call -- an inert instance, an out-of-range process, a round it does
not speak for -- writes nothing and preserves everything; the cases
treat the accepting paths.

Four structural facts of the machine are consumed repeatedly and
stated once:

  RECORD WRITES.  A retained round's records are written only by:
  possession recording (the given sender's possession bit set, the
  same sender's still-owed bit cleared), serve-owed recording (the
  given sender's still-owed bit set -- the rule answers yes only
  when that sender's post-record possession bit is clear),
  held-members recording (H_r alone), release (the entry leaves
  the retained set), and birth at completion (the entry is
  reinitialized whole).  Possession bits are never cleared short
  of release or rebirth, and no entry point rewrites a retained
  entry's round byte.

  DEPARTURES.  Every path that removes an entry from the retained
  set outputs a RELEASE act naming that entry's byte in the same
  call, and every RELEASE act names an entry retained when the
  call began; the completion insert takes only a free slot -- free
  before the call, or freed by that call's own release.

  ADVANCE.  The frontier moves only at completion, by one.

  BOOK WRITES.  W and the adopt latch are written only by: witness
  recording (the given server's bit set; the latch set exactly
  when the post-record distinct count reaches t+1 with the latch
  clear), witness reset (both cleared), and completion (both
  cleared).

BASE.  Initialization zeroes the state: the retained set and W
empty, the latch clear, the frontier at the first round.  I1-I5
and I7 hold over the empty retained set and the clear latch, I6
holds with W and the latch clear, and the transition conjuncts
speak of calls, none of which has run.  Initialization refuses an
out-of-range self, n < 3t+1, and a single-process deployment; a
refused instance is inert at every entry, so the induction
quantifies over accepted instances.

RECEIVED.  Traffic ahead of the frontier is refused before the
dispatch (beyond chain reach -- Model); the remaining classes --
retained, released, the frontier live, the frontier not yet
launched -- are one per round byte.
  Retained R: a carried possession indication is recorded before
  the dispatch classifies, the given sender's possession bit set
  and its still-owed bit cleared (RECORD WRITES), so the tables
  read the post-record state.  I2: the one still-owed bit this
  call can set is the sender's, and the serve-owed rule answers
  yes only when the sender's possession bit is clear.  I3:
  possession bits are never cleared.  I8, I9, I4: the release
  rule's eviction arm is pinned off at this site (the budget input
  presented unexceeded), so it answers yes exactly when the
  post-record possession covers all n; by I4 the record was short
  of all n when the call began, so the yes lands exactly at the
  call whose recording completes the record -- neither early nor
  late (I8) -- the departure is announced (DEPARTURES -- I9), and no
  all-n entry survives the call (I4).  On a no, the record is
  still short of all n.  I1, I7, I10, I11: nothing is added, no
  byte rewritten, no eviction taken; a departure only shrinks the
  set.  The attribution clause: every record bit this call writes
  is the given sender's (RECORD WRITES).
  The frontier live: the answer is deliver; nothing is written.
  The frontier not yet launched: participation owed is recorded --
  a flag no conjunct reads.
  Released R: nothing is written.

LAUNCH.  The three launch answers are exclusive by the tables --
join requires owed work, maintain requires none and maintenance
due, admit requires none, no maintenance, and a pending value -- so
at most one launch act fires per opportunity (the fact L4
consumes).  Any answer sets the live flag; a join retires the owed
flag.  No record, no W, no latch, no frontier movement: every
conjunct is preserved untouched.

COMPLETE.  The one entry that retains a round; every other case of
this induction notes it adds none (I10a).  In the call's order:
  (1) Without a live instance, or named any round but the
  frontier, the call refuses.
  (2) The wrap release.  If an entry carries the byte F+1, by I1
  it is unique and lies at wrapping distance 255 behind F, the
  maximum; it is released and announced (I9), and as the
  maximal-distance entry it is the oldest (I11).
  (3) The budget eviction.  The release rule's eviction arm
  answers yes exactly when no slot is free; the entry at the
  greatest wrapping distance behind F -- well defined by I1
  distinctness -- is released and announced (I9, I11).  The wrap
  release frees its slot before the allocation is read, so (2)
  and (3) never fire together and a free slot always remains for
  (4).
  (4) The birth.  The completed round, the pre-advance frontier F,
  enters a free slot (DEPARTURES): possession exactly {self},
  nothing owed, the held members as given (I10b, I10c).
  (5) The advance.  F moves to F+1 (ADVANCE), and W, the latch,
  and the live, owed, and adopt flags clear in the same call --
  I6's clear-on-advance.
  The resident state that results: I1 -- surviving entries keep
  their bytes and the entering byte F was retained nowhere (I7),
  so bytes stay distinct; every survivor lay at distance 1..254
  behind F (distance 255 is the byte F+1, released at (2)), so it
  lies at distance 2..255 behind the new frontier with the new
  entry at distance 1, strictly behind within 255; the count bound
  is the allocation's (w+1 slots, insert only into a free slot).
  I7 -- the byte F+1 was released at (2) if present, and the
  entering byte is F.  I4 -- the new record is {self}, short of all
  n because a deployment has more than one process (initialization
  refuses a single-process deployment, under which a record born
  at {self} would already cover the roster while the completion
  path takes no release decision).  I2, I3, I5 -- the new entry
  owes nothing and holds self; the latch is clear.  I8 is silent:
  both releases here are eviction-class, excluded by construction.

POSSESSED.  RECEIVED's retained-possession case without the
deliver and serve arms: the given sender's possession bit is set
and its still-owed bit cleared (I2, I3, the attribution clause),
and the release rule -- eviction arm pinned off -- answers yes
exactly at post-record all n (I8, I9, I4, by the argument above).
Nothing is added, no eviction taken (I10, I11).

WITNESS.  Writes only W and the latch (BOOK WRITES).  The entry
refuses self as a server and any round but the frontier, so W
speaks F alone (I6) and never contains self.  The latch is set
only at a post-record distinct count of t+1 or more, and until the
next clearing -- which clears both together -- W only grows, so the
resident implication I5 holds in every intervening state.  The
retained set is untouched.

WITNESS RESET.  Clears W and the latch together: I5 and I6 hold
with both clear; nothing else is written.

ASSEMBLED.  Writes one member bit of a retained round's
held-members record, which no conjunct reads (H_r's exclusion from
advance and release decisions is L7's second half).  Nothing is
added, none released.

EVICT.  With nothing retained the call refuses.  Otherwise the
release rule's eviction arm (the event presents the budget as
pressed) releases the entry at the greatest wrapping distance
behind F -- unique by I1 -- announced (I9), the oldest (I11).  Not a
possession path, so I8 is silent; the set only shrinks, and a
subset of a set satisfying I1-I4 and I7 satisfies them.

SERVE.  Writes none of the machine's state: the walk reads the
retained records and reports a still-owed round; its cursor is the
caller's.  Every conjunct is preserved untouched.

This closes the induction.  I1 earns the closing word: its count
arm is the allocation's, but distinctness and behind-within-255
rest entirely on the wrap release at step (2) -- at EVERY window
size, not only at a full window.  Sibling rounds releasing at all
n keep the window unfull, so eviction pressure never bounds how
far behind an entry lingers; the lookahead at the byte about to
recur is the only guard between a lingering entry and the
recurrence of its byte, and the induction consumes it exactly
once, where the frontier advances.

### The lemmas

  L1  BOUNDED HOLD (R4).  Under TOLERANCE the R4 hold ON LAUNCHING
      lasts at most T_p of this process's own sweeps (A6: the sweep
      count is computed honestly).  Under HELD, fewer than n-t
      processes have EVIDENCED the prior round here.  Where some
      CORRECT process does not yet possess it, the heal is
      SELF-FUNDED: HELD reads only while this process still RETAINS
      the prior round below all-n (a round no longer retained reads
      as met -- R4), so the straggler's stale acts arrive here as
      want evidence (A4), the serve obligation is born here, one
      correct server suffices on the fold ground (A10) for the
      straggler to adopt, and its possession evidence returns by A4
      and A8.  The heal fills POSSESSION, never records: where
      every correct process already holds the round and only its
      evidence is missing, no want is born and no serve carries
      it, and the class need not clear -- a process it strands
      reads no progress evidence, proves no participation, and is
      PARTITIONED by default (the three states); the posture's
      discipline, not this lemma, governs it from there.
      Corollary: when the deficit is imposed by withholders -- n-t
      possess, so the class reads TOLERANCE --
      the hold costs at most T_p sweeps per round, and t withholding
      processes cost a bounded constant factor, never ownership of
      the pace (the quantified claim R4 makes).  A process genuinely
      behind (HELD) exits by the self-funded heal -- a liveness fact,
      not a sweep bound.
      SCOPE -- the lemma bounds the HOLD, not the ADVANCE.
      toleranceElapsed opens the launch gate; the frontier advances
      only at completion, and completion is unbounded under
      asynchrony.  Only the TOLERANCE half is a sweep bound; HELD
      is not guaranteed transient -- the lemma speaks while the
      participation proof sustains, and a process it strands is
      PARTITIONED by default.

      Proof.  The TOLERANCE half is near-definitional.  The class
      reads with at least n-t possession of the prior round
      recorded; the launch gate then opens at all n or at
      toleranceElapsed, and the tolerance input counts this
      process's own sweeps, computed honestly (A6) -- self-local,
      no remote lever in either direction (Byzantine notes).  A
      hold under TOLERANCE therefore lasts at most T_p of this
      process's own sweeps.  At t = 0 the class is unreachable
      (n-t is n) and this half is vacuous.

      The HELD half is the self-funded heal, and its chain
      consumes no bound on any instance's completion -- that is
      the point of adoption.  One proviso runs through it, REACH:
      no round a correct straggler still needs has been evicted
      by every correct process that held it.  Reach fails only by
      eviction -- a needed round cannot release at all n (second
      fact, below) -- but eviction is not merely an accident of
      budget.  It is reached in ordinary operation, under no
      faults and no loss: the tolerance escape carries a frontier
      forward one rung per T_p sweeps while retention spans only
      w rounds, so a straggler more than w rungs behind the
      fastest correct processes loses reach to their ordinary
      advance.  The proviso is therefore a SIZING obligation --
      w and T_p are not independent, and w must span the rungs a
      straggler is expected to climb within them -- as well as the
      out-of-band boundary RETAIN prices (M1's accept-loss).  This
      process's own eviction of R clears its duty the same way --
      that boundary read locally (duty bounded by retention).

      Suppose HELD reads: this process retains R with possession
      recorded short of n-t, so more than t processes have not
      evidenced R here, and by A2 a correct process is among them.
      A correct process may fail to evidence R for either of two
      reasons, and only one is a deficit this heal addresses.  If
      it LACKS R, the chain below closes the round for it.  If it
      HOLDS R and its evidence has merely not arrived, it is no
      straggler: no want evidence for R is born anywhere, so no
      serve carries the missing bits, and its evidence rides
      either the round's own traffic -- which the layer below
      retires once that round quiesces -- or its acts of a later
      round, which stand beyond the reach of a process that
      cannot advance.  Where every correct process already holds
      R the class therefore need not clear at all; a process it
      strands accumulates no progress evidence, its participation
      proof lapses, and it is PARTITIONED by default (the three
      states) -- the posture's blind re-offers and its deployment's
      abandonment policy govern from there, not this chain.  The
      remainder treats a correct process that lacks R.
      Three facts assemble the heal, and one discipline funds all
      three: retry.  A correct process is never send-silent: a
      decided process keeps broadcasting -- success signals stop
      nothing at any layer (R2c).  The
      partitioned posture is a retrying posture -- blind re-offers
      expecting nothing (the three states); a duty, once born,
      retires only on remote evidence, never on silence or local
      progress (M1); and evidencing possession is itself such a
      duty, discharged toward all n and retired only on that
      remote fact or at the budget boundary (R4's lifted
      discipline: proceed at threshold, discharge duty toward
      all-n, retire only on remote evidence or an explicit budget
      -- never implicitly on own progress).  Eventual delivery
      (A4) is the delivery of exactly this retried traffic -- the
      transport obligation names retry as its mechanism -- so what
      a correct process evidences always has a carrier and always
      lands.

      First, a correct process holds a round's composition only
      through the close (Model: holding is born of own COMPLETE
      or adoption, RETAIN's two births, and nothing weaker), and
      the close moves
      its cursor past the round (rounds in order, A7).  So a
      correct process lacking R stands at some cursor C <= R,
      never held C, and keeps stepping (Model).  It participates
      at C -- its instance already live or its blind re-offers
      already flowing, or its advance gate opening by the retry
      discipline exactly as in the climb below -- and its acts of
      C arrive at every correct process (A4).

      Second, within reach some correct process retains C.  Every
      correct process whose cursor is past C closed C and so held
      it -- this process among them.  A holder releases C on the
      possession path only at all n (I8), and an all-n record for
      C is impossible while a correct process never held C: the
      record marks only attributed senders (A9), at most t bits
      are forged and none a correct process's (A2, A9), and a
      true bit means its sender holds (A8).  Every other release
      is eviction-class (I8's exclusion), which reach excludes.
      A correct retainer of C therefore stands, observes the
      straggler's acts as want evidence for C -- observed, never
      inferred -- and SERVE is born there per (C, straggler).

      Third, a born serve discharges and one server closes the
      round.  The cap never reads below one slot and rotation
      forbids displacement, so the grant eventually lands (SERVE
      bounds; M1) and the serves flow, paced by the tick.  The
      straggler's own chain stands at C-1 (it closed C-1), the
      served composition folds within its reach (O1), and by the
      binding (A10) the fold is proof: the straggler ADOPTS C
      from served evidence alone -- its instance decides nothing
      here.  An exhausted instance heals by exactly this route:
      EXHAUSTED stops only the instance's own deciding, the
      process keeps stepping, and adoption never consults the
      instance.  The usual straggler is the same case: a
      PARTITIONED process's blind re-offers at its stale cursor
      are exactly the acts of C this chain consumes (the three
      states).

      The heal composes by induction on R - C, and each rung has
      two steps.  Adoption of a rung C advances the straggler's
      cursor -- and the next rung's serve is born only from want
      evidence, the straggler's own acts of C+1, whose launch is
      gated by the same advance rule on the round just adopted
      (R4 governs joins exactly as admissions).  The gate opens
      by the retry discipline, and by its breadth: the third
      fact's serve heals every correct process short of C, not
      only this straggler -- each one's acts of C arrive at the
      retainers (A4), the rotation displaces none (SERVE
      bounds), and each adopts on the same fold ground -- so at
      least n-t correct processes (A2) come to hold C.  Every
      correct holder of C keeps evidencing possession on its
      retried traffic, and the serving retainers' assertions
      continue until the straggler's own possession is evidenced
      back (SERVE retires per process only on that), so the
      straggler's record of C reaches n-t on true bits (A9, A8)
      and the launch gate opens within its own tolerance (A6).
      The next rung is evidenced within its now-extended reach:
      every correct process past C+1 -- this process among them
      -- launched C+1 (the wanting side participates at its
      cursor while it accumulates), and an instance's sent
      duties toward a process that never evidenced the round are
      unretired (R2c, M1), so verifiable acts of C+1 arrive (A4).
      The straggler joins, its acts of C+1 flow as want evidence,
      and the rung closes as the third fact closed C (its own
      COMPLETE may supersede en route -- L3).  Reach covers every
      rung, so the straggler closes R.  Every correct process
      therefore eventually evidences R here: one that holds R
      evidences it on traffic the retry discipline keeps flowing
      (A4, A8, A5), and one that lacks it closes R by the heal,
      its evidence returning the same way, recorded to its true
      author (A9).  Correct processes number at least n-t (A2),
      so where a correct process lacked R the record reaches n-t
      on true bits alone and HELD exits -- to TOLERANCE, or to MET
      at all n or on this process's own eviction of R.  At t = 0
      the same chain runs unchanged:
      every process is correct and the floor keeps the one slot
      the rotation grants -- no separate case.

      Corollary.  When n-t possess R and only withholders are
      short, the class reads TOLERANCE and the first half bounds
      the hold at T_p of this process's own sweeps per round; t
      withholding processes cost that same bounded factor, never
      ownership of the pace -- the quantified claim R4 makes.  The
      HELD exit is a liveness fact and carries no sweep bound;
      the SCOPE clause stands: the lemma bounds the hold, never
      the advance.

  L2  ADOPTION AGREEMENT.  An adopted composition equals the
      completers' result.  Adoption closes on one of O3's two
      grounds, and the cases consume differently.
      FOLD ground: a candidate that folds to an identity verified
      within the adopter's chain reach (O1) is the agreed
      composition outright (A10) -- the server's honesty never
      enters; O3's one honest assertion is who SUPPLIES the
      candidate, not why it is true.
      WITNESS ground: from I5 the adoption carries t+1 distinct
      witnesses; by A2 at least one is correct; by A3 that one
      asserted the composition it HOLDS.  A correct holder of R,
      however, may hold it by adoption rather than completion (RETAIN
      is born at own COMPLETE *or* adoption), and A1 speaks only of
      completers -- so "what it holds is the agreed composition" is
      the very claim being proved.  The sketch is therefore well-
      founded induction over the order in which correct processes
      come to hold R: BASE -- a holder that completed R holds the
      agreed composition by A1, or fold-closed and holds it by A10;
      STEP -- an adopter's t+1 witnesses include a correct one that
      held R strictly earlier, correct by the induction hypothesis.
      The order is well-founded because adoption requires prior
      servers, so holdings bottom out at completers and fold-closers.
      (Pigeonhole in the shape of Bracha87.txt Lemma 1.)
      CALLER HALF -- a consumed premise in the L4 pattern, pinned at
      Mechanization status: the machine's witness book records only
      WHO served; the caller counts a witness only for an assertion
      byte-identical to its adoption candidate, and re-arms the book
      on a candidate switch.  I5's t+1 are witnesses TO THE
      CANDIDATE only under that discipline.

      Proof.  Both grounds are read within one simultaneous
      induction on the position: the claim at R may consume
      sequence identity strictly below R -- L6's hypothesis at
      R-1 -- while L6 at R consumes this lemma at R; one
      induction carries the pair, grounded at A11.  The caller
      half is consumed wherever a witness is counted: an
      assertion enters the book only byte-identical to the
      adoption candidate, and the book re-arms on a candidate
      switch (Mechanization status), so I5's t+1 distinct
      witnesses are witnesses to the candidate.

      The fold ground.  An adopter of R closed R-1 (rounds in
      order, A7), so its chain head is its own round-(R-1)
      result -- the agreed result at R-1 by the induction
      hypothesis, and at the first round the provisioned base
      itself, common to every correct process (A11).  Within
      that reach the candidate folds to a verified identity
      (O1), and the derivation binds: no two distinct round
      results derive one identity (A10), so the folding
      candidate IS the round's agreed result -- the one result
      the round's identity derives from.  The anchor is the
      binding, not a completer: where any correct completer
      exists it holds exactly this result (A1), and where none
      does -- every correct holder may hold by adoption -- the
      binding alone carries the identity of the name.  The
      server's honesty is never consumed: O3's one honest
      assertion is who supplies a candidate, not why it is
      true, and a forged candidate fails the fold at the cost
      of bounded verification work, never a false adoption
      (Byzantine notes).  Nothing in the ground reads t.

      The witness ground.  From I5 the adoption carries at
      least t+1 distinct witnesses, never self among them --
      distinct PROCESSES, not merely distinct book bits, because
      the book marks only the attributed author of each counted
      assertion (A9); by
      A2 at least one is correct; by A3 that witness asserted,
      for R, only the composition it holds; by the caller half
      the counted assertion is byte-identical to the candidate
      adopted.  What remains is that a correct holder's
      composition is the agreed result -- and a correct process
      may hold R by adoption (RETAIN's two births), so the
      remainder is proved by well-founded induction over the
      order in which correct processes first come to hold R.
      The order is causal, not temporal: a counted witness held
      R when it served (A3), and its assertion was received and
      counted before the close it fed (the book accumulates
      toward the latch), so the witness's first holding
      causally precedes the adopter's.  A descending chain of
      holdings therefore steps strictly earlier in the causal
      order of one execution, which is well-founded (Model:
      executions are causally well-founded) -- an event of an
      execution has finitely many causal predecessors, and a
      cycle would place each holding strictly before the other.
      Self-exclusion keeps the step grounded: I5 bars self from
      the book, so no close counts the holding it is itself
      creating -- a self-witness would found a chain on
      nothing.  BASE -- a first holding not born of a
      witness-ground adoption is own COMPLETE or a fold-ground
      close, and nothing else: RETAIN's births are exhaustive,
      and an adoption closes on exactly one of O3's two
      grounds.  A completer holds the agreed result (A1); a
      fold-closer holds it by the fold ground above.  STEP -- a
      witness-ground adopter counted a correct witness whose
      first holding is strictly earlier in the order; by the
      induction hypothesis that holding is the agreed result;
      by A3 and the caller half the adopted candidate is
      byte-identical to it.  Every chain bottoms at a completer
      or a fold-closer, so on either ground the adopted
      composition equals the completers' result.

      At t = 0 the witness ground degenerates soundly: t+1 is
      one witness and A2 leaves every server correct, so the
      one counted assertion is a correct holder's and the same
      induction runs; the fold ground never read t.  The
      mechanization carries the grounds' shape: the witness
      entry refuses self and every round but the frontier,
      records WHO served and nothing else, and fires ADOPT once
      at t+1 through the latch; the reset re-arms the book for
      a candidate switch; and both grounds close through the
      one consume region, where a racing own COMPLETE
      supersedes the accumulation (L3).

  L3  SUPERSESSION.  Completion is the sole consume region (I10a)
      and clears W, the adopt latch, live, and owed while advancing
      F (I6); witnesses arriving afterward pertain to a round that
      is no longer F and are ignored.  Therefore
      adoption-then-completion and completion-then-late-witnesses
      reach the identical state, in either order.

      Proof.  Three facts of the induction carry it.  First,
      completion's mutation reads nothing of W or the adopt latch:
      the entry it births is a function of the pre-advance frontier
      and the given held members alone -- the byte and the born
      records by I10b and I10c, the held-members grain being the
      call's own argument -- and it clears
      W, the latch, and the live, owed, and adopt flags
      unconditionally (I6).  Second, witness recording writes only
      W and the latch, and refuses every round but the frontier.
      Third, completion refuses without a live instance and
      refuses any round but the frontier -- the close speaks its
      round.  Now take
      the two orders from a common pre-state.  Witnesses then
      completion: the recordings touched only state completion then
      clears, so the post-state carries no trace of them -- a
      partial accumulation, a full one, and a fired adopt latch
      erase identically.  Completion then witnesses: the late
      arrivals name the completed round, no longer the frontier
      after the advance, and are refused whole.  Both orders
      therefore end in the state completion alone produces from the
      same inputs: the same entry born, the same flags and W clear,
      the same frontier.  And the region consumes once per round:
      completion clears the live flag, every later launch is for a
      later frontier (rounds in order, A7), so a second close for
      the same round refuses either way: before any later launch
      it finds no live instance, and after one it names a round
      that is no longer the frontier (the third fact) -- and the
      superseded accumulation can neither complete its round a
      second time nor leave a residue, and the latch sets again
      only by a fresh t+1 accumulation for the new frontier (I5).

  L4  PRESENTATION.  MACHINE HALF: no launch consumes a value except
      ADMIT; MAINTAIN outranks ADMIT and never consumes (O5); at
      most one launch act per opportunity; a pending value rides a
      JOIN exactly as an ADMIT.  CALLER HALF -- a consumed premise,
      in the pattern of the implementer obligations above and
      already pinned at PRESENT and in Mechanization status: the
      caller stages the accepted value, re-presents it byte-
      identically on every launch until agreed, and retires it only
      on witnessing it in an agreed subset.  Exactly-once (R1) is
      the conjunction; this layer proves only the machine half, and
      the premise is where the other half lives.

      Proof (the machine half).  The launch answers are the
      tables', exclusive and exhaustive at compile time, and their
      guards are pairwise disjoint in the birth inputs: join fires
      only with participation owed; maintain only with nothing
      owed, maintenance due, and its capacity and advance gates;
      admit only with nothing owed, no maintenance due, a value
      pending, and the same gates.  At most one launch act
      therefore fires per opportunity (the induction's LAUNCH
      case).  The precedence is the guard structure itself: owed
      work forecloses both chosen acts, and maintenance due
      forecloses admit, so MAINTAIN outranks ADMIT wherever both
      births hold (O5).  ADMIT is the only rule that reads the
      value-pending input, so it is the only launch born from the
      value; the maintain rule decides and shapes its launch
      without reference to the value -- its act instructs the
      deployment's maintenance form, and the value stays staged (a
      maintenance win is not the value's win).  A pending value
      rides a JOIN exactly as an ADMIT because the ride is
      PRESENT's discharge, not the launch's cause: the join rule
      neither reads nor forecloses the value, both acts launch the
      same frontier round, and the machine records in JOIN versus
      ADMIT the obligation's cause and gating, never value
      consumption -- no value waits longer by being joined than by
      being admitted.  The caller half is consumed as stated;
      exactly-once (R1) is the conjunction of the two halves.

  L5  RELEASE SAFETY.  A round releases on the all-n path only with
      every process's possession recorded (I8); possession recording
      marks only its attributed sender (A9), so with at most t
      forgeries reaching n requires every correct process's true
      evidence, and a true bit means the round is held (A8).
      Hence no correct process still needs the released round's
      COMPOSITION.  Content is not covered: possession of it may lag
      indefinitely (Model), so a correct process mid-assembly can
      still need pieces of a round released at all-n composition --
      priced by O2 as the per-member out-of-band boundary, never the
      round's failure.
      Eviction -- including the encoding's wrap-boundary release
      (Mechanization status: an entry releases before its round
      byte recurs), which is eviction-class -- is the explicit
      exception and is out-of-band territory by definition (RETAIN).

      Proof.  The lemma speaks the all-n path; the release-cause
      enumeration is closed at three (Mechanization status), and
      the other two -- budget eviction and the wrap-boundary
      release -- are eviction-class, the out-of-band boundary
      RETAIN and M1's accept-loss clause price explicitly.  On
      the possession paths the release fires exactly when the
      post-record covers all n (I8; the induction's RECEIVED and
      POSSESSED cases -- the eviction arm pinned off at those
      sites, and by I4 the record short of all n when the call
      began).

      What an all-n record asserts, bit by bit.  A possession-
      path entry mutates only the bits of the sender it was
      given (the induction's attribution clause), and that
      sender argument is bound to the act's true author by the
      deployment's sender-authenticated ingress (A9).  A bit can
      therefore be forged only by its own author: with at most t
      Byzantine (A2), at most t bits are forgeries, and none of
      them a correct process's.  A record covering all n thus
      contains every correct process's bit as true evidence, and
      a true bit means its sender held the round's composition
      when it evidenced (A8).  Forgeries vouch only for
      Byzantine senders themselves; they can never substitute
      for a correct process's bit.

      Held once is needed never again.  A correct process holds
      a round's composition only through the close (RETAIN's two
      births), the close is once per round (L3: completion
      clears the live instance, and every later launch is a
      later frontier, A7), and a closed round is resumed, never
      re-entered (R2b).  Want of a composition exists only below
      the close (the wanting side), so a correct process that
      ever held R is past R and is never again a wanter of R's
      composition.  At the release every correct process has
      held R; hence no correct process still needs the released
      round's COMPOSITION.  Nor does the release strand a serve:
      possession recording clears the same sender's still-owed
      bit and the two records are disjoint (I2), so an all-n
      record leaves nothing owed -- SERVE's per-round retirement
      at release retires an obligation set already empty.

      Content is deliberately outside the claim.  Composition
      possession drives the clock and the release; content
      possession may lag indefinitely (Model, O2), so a correct
      process mid-assembly can still need pieces of a round
      whose composition released at all n -- priced by O2 as that
      member's out-of-band content line, never the round's
      failure.  At t = 0 every bit of the record is true (A2)
      and the same argument runs on both processes of the
      smallest deployment -- no separate case.

  L6  SEQUENCE IDENTITY.  Every correct process's agreed sequence is
      byte-identical at every position both hold -- a safety lemma;
      that every position is eventually held is the wanting side and
      L1, not this claim.  By induction on R: the entry for R
      arrives either by own COMPLETE (identical across completers by
      A1) or by adoption (identical to the completers' result by
      L2), and both enter the one consume region (L3); rounds are
      taken in order and the frontier is monotone (A7), so positions
      agree as well.  That "the entry for R" holds round R's actual
      result is I10's caller half, consumed here.  The induction
      base is A11 (the first round's identity has a common ground to
      fold to).  This is the property the layer exists for, and it
      rests on exactly L2, L3, A1, A7, A11, and I10's binding.

      Proof.  This is the other half of the simultaneous
      induction L2's proof declares: one induction on the
      position carries the pair, the hypothesis at R being this
      lemma's claim at every position strictly below R.  L2 at R
      consumes that hypothesis (its fold case verifies against
      the adopter's round-(R-1) head); this step consumes L2 at
      R.  The descent is strict, so the pair is grounded, and
      the ground is A11: below the first position stands the
      provisioned base itself, common to every correct process
      before any round runs -- the first round's completers agree
      by A1 alone, and its fold-closers verify against that
      common base.

      The step, at position R.  A correct process holds exactly
      one result for R: the close is once per round -- completion
      clears the live instance and every later launch is a later
      frontier (L3, A7) -- and both arrival routes enter the one
      consume region, where a racing own COMPLETE supersedes a
      partial adoption (L3), so no second close lands and no two
      copies drift.  Take two correct processes both holding R.
      Each holds by own COMPLETE or by adoption, RETAIN's two
      births.  Two completers hold the same composition (A1).
      An adopter holds the completers' result (L2 at R, its
      hypothesis supplied by this induction).  Either way both
      hold the round's agreed result -- the completers'
      composition where any correct completer exists (A1), the
      binding's unique fold anchor regardless (L2) --
      byte-identical.

      That the comparison is per POSITION is A7: rounds are
      taken in order and the frontier is monotone over the
      unbounded sequence, so each correct process's agreed
      sequence is indexed by the positions it closed, one close
      per position, and a common position names the same round
      at both processes -- the round BYTE's wrap never enters the
      claim (a byte routes; the lemmas speak positions, and A7
      carries between).  That "the entry for R" holds round R's
      actual result is I10's caller half, consumed as stated:
      the machine binds the byte and the birth, the caller binds
      the artifact.

      Nothing in the argument reads t: A1 and L2 carry the
      Byzantine weight, and at t = 0 both hold unchanged, so the
      smallest deployment inherits the claim as is.  The scope
      stands as stated -- identity at positions both hold; that
      every position is eventually held is the wanting side and
      L1, not this lemma.

  L7  WRAP AND TWO-GRAIN SOUNDNESS.  At most one retained entry per
      round byte, every retained round within 255 of F -- from I1
      together with the wrap release firing before a byte recurs.
      Separately: H_r appears in no advance and no release decision,
      so a per-member content hole can never gate the frontier or
      strand a round (the per-member out-of-band boundary of O2).
      The EXHAUSTED-behind heal is a consequence of L1 and the
      wanting side, not a separate lemma.

      Proof.  The first half is the induction's I1 read as a
      corollary: in every resident state retained bytes are
      pairwise distinct -- at most one entry per byte -- and every
      retained round lies strictly behind F within 255; and the
      completion case consumes the wrap release exactly at the
      advance, so an entry is released before the position that
      would reuse its byte can complete -- it has not yet launched.
      Within the nameable span -- the retained window, the live
      instance, and the verification reach -- a byte therefore
      denotes one position, the bounded soundness the Model prices
      for the round byte, and A7 carries the correspondence to the
      positions the lemmas quantify over.  The second half is
      syntactic.  The dispatch's inputs are the round class, the
      sender's possession, the live and owed flags, the launch
      inputs, the all-n test, the budget and oldest-round tests,
      and the prior round's possession class -- H_r is among them
      at none of the five mechanized sites; the advance signal is
      derived from the possession record alone; and the two
      structural releases, the wrap lookahead and the eviction
      scans, read round bytes and wrapping distances alone.  H_r
      is written at completion and by late assembly, and read only
      where serving carries it.  A per-member content hole
      therefore cannot gate the frontier, hold a release, or
      strand a round: its entire cost is that member's content
      line, out of band (O2).

## Mechanization status

system.dtc and system.[hc] mechanize the calculus: PARTICIPATE birth
(DERIVED), the launch decision with the R4 advance rule and the O5
precedence (join, then maintain, then admit -- all under the prior
round's possession class and the tolerance input), two-grain
possession (composition bitmaps plus the per-member have grain and
its late-assembly ingress; SERVE acts carry .want and .have), the
wanting side's witness book (t+1 distinct servers, single-fire
ADOPT, reset, and completion-as-the-one-consume-region clearing it),
and RETAIN/RELEASE at all-n or eviction -- the release-cause
enumeration is closed at three: all-n, budget eviction, and the
round-byte wrap boundary (encoding-derived, a structural guard like
the 254-round cap: an entry releases before its byte recurs), the
last being eviction-class for L5 and I8.  The adopt rule lives as a
C guard on the bkr94acs BPR-gate precedent -- an independent rule set
whose inputs appear in no other rule (its statement is in
system.dtc's witness-event section).  Deliberately CALLER-SIDE, per
"Relation to a deployment": the serve walk's concurrency cap and
rotation (pacing mechanics), the cross-kind byte budget (artifact
sizes are application domain; systemEvict actuates), PRESENT's byte
staging and subset-witness retirement (the machine sees the
valuePending/maintenanceDue inputs), the have grain's currency (the
grain is caller-fed -- the machine never sees content -- and no rule
reads it; a caller keeping its own per-member content record serves
from that record and may leave the machine's grain at its close-time
report, feeding late assembly only when it relies on .have as its
record), the R2b resume constraint
(it binds the ACS instance state, which the caller owns), and
O3's verification seam: the witness book records WHO served and
nothing else -- the caller counts an assertion toward t+1 only
byte-identical to its adoption candidate and verified as
speaking the frontier round by its identity (O3: assertions are
(identity, content) pairs), re-arms the book on a candidate
switch, closes a witness-ground adoption only on the ADOPT
signal, treats a reset as voiding any unconsumed ADOPT (a
caller that groups byte-identical assertions and reaches the
threshold in its own book before feeding the machine never
presents a switch -- the reset clause is vacuous for it), and
treats a superseding COMPLETE the same way: a close consuming
the round voids any unconsumed ADOPT for it, because L3
commutes the MACHINE state only -- an ADOPT already output is
caller state the machine cannot void, and acting on it after
the close would adopt over a completed round (no grouping
discipline makes THIS clause vacuous: the race between an
output ADOPT and a process's own COMPLETE is a schedule's to
manufacture, not a caller's to prevent) --
and the fold ground is realized wholly
caller-side (a candidate folding to a verified identity within
the adopter's chain reach -- O1, A10 -- closes on one assertion, no
book consulted), both grounds closing through the one consume
region a live COMPLETE enters, which speaks its round.

Three more caller halves complete this list, stated here because
lemma proofs consume them.  The retained entry's CONTENT (I10's
caller half): the machine binds the round BYTE -- the entry born
at completion is exactly the pre-advance frontier -- and "the
entry holds round R's actual result" is the caller's: the
artifact stored for R is the composition the close consumed,
indexed by the round the close named, wrap discipline included.
L6's byte-identical-at-every-position rests on it, and the
machine cannot check it -- it never sees a composition.  And the
possession-evidence RETURN LEG: a possession indication for the
FRONTIER round arriving before that round has a record is held
and re-presented through systemPossessed once the close retains
it (the machine drops it -- only retained rounds have a record --
and the drop is not self-correcting; the hold is frontier-only
and discarded on release, since a hold keyed on "not retained"
catches released rounds, whose entries outlive their round in
the wrapping byte space); and the O1 inference is likewise a
caller translation -- an authenticated act of round R+1 or later
is presented as possession evidence for R (Model, possession
sources).  L1's return leg and L5's all-n record consume that
the evidence LANDS: a caller that discards either carrier
starves the heal and strands the possession record at n-1.
And the ROUND ARGUMENT itself: evidence is presented for the
round its identity PROVES, verified within chain reach, never
for the round its byte names.  A9 pins the sender coordinate of
a recorded bit; this is the same obligation on the other
coordinate, and no caller half carried it -- the Model states
the fact (an act is OF a round by its identity) but nothing
bound the ingress argument to it.  The byte names a position
unambiguously only across the nameable span (Model), so a
sender 256 or more positions behind speaks a byte that has
recurred within that span -- the frontier's own at exactly 256,
a retained round's beyond -- and a byte-keyed translation
banks that sender's TRUE indication against a round it does not
hold.  Such a bit is outside the Byzantine notes' containment in
both directions -- it is no forgery, so the fault budget does
not bound it, and no author disowns it -- and L5's all-n record
and R4's advance evidence both consume that every bit was
recorded for the round it was authored for.  The damage is
bounded and it is not a new strand: a colliding sender is by
arithmetic already past every retention window, hence already
in the out-of-band territory RETAIN prices.  What it costs is a
round released without its last true bit, and an advance whose
evidence stands one honest process short.

## Relation to a deployment

The tables specify obligations, not mechanics.  Selecting the oldest
owed round, enumerating unserved processes, walking retained rounds,
the optional ahead-of-reach hold (a latency optimization -- retry
re-arrival makes the drop legal), and the budget ledger are
implementation loops that the go-signal outputs drive -- the same
division as the input-0 fanout and SubSet enumeration in
bkr94acs.dtc.  The tick that paces launch opportunities and serves is
a wire rate limit, never a correctness clock.  EXHAUSTED handling is
unchanged from the library: it is the only library stop, it feeds the
application's abandonment policy, and nothing here adds a stop -- a
process whose instance exhausts while the roster completes is simply
behind, and the wanting side heals it.

Every carrier of the layer's traffic is an instance of a layer-below
primitive, chosen so that transport reliability is INHERITED, never
re-implemented: BPR comes free with the instance, so A4's eventual
delivery is discharged by construction, and every carrier retires on
the primitive's own gates -- remote evidence or the abandonment
policy, never local progress.  Three geometries carry everything:

  - The round instance: ACS at (n, t).  The agreed composition
    rides its value plane; the per-member content grain (O2) rides
    payloads unique to each process, paired with the same
    instance's traffic.
  - Exchange: a reliable-broadcast instance (Bracha87.txt, Figure
    1) at (n, t) -- one initiator's artifact spanning all n on the
    value plane, with the per-member grain again as payloads unique
    to each process.
  - Recovery: a reliable-broadcast instance at two processes and
    t = 0 -- a targeted payload from one server to one wanting
    process, the primitive degenerated to an acknowledged, retried
    pair channel: a SERVE act's discharge.  The carrier guarantees
    an assertion arrives; O3's grounds decide what arrivals mean --
    one assertion suffices on the fold ground, t+1 distinct
    servers' on the witness ground.

Traffic of the exchange and recovery geometries is traffic of its
round: behind a receiver's frontier it classifies as retained-round
traffic, and its tails carry possession evidence like any other
traffic of the round.
