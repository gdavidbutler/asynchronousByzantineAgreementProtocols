/*
 * asynchronousByzantineAgreementProtocols - system obligation layer
 * Copyright (C) 2026 G. David Butler <gdb@dbSystems.com>
 *
 * This file is part of asynchronousByzantineAgreementProtocols
 *
 * asynchronousByzantineAgreementProtocols is free software: you can
 * redistribute it and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation, either
 * version 3 of the License, or (at your option) any later version.
 *
 * asynchronousByzantineAgreementProtocols is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * System -- the obligation layer above ACS
 *
 * Implementation of the obligation calculus specified in system.md
 * and system.dtc -- the composition layer that runs an unbounded
 * sequence of ACS instances (rounds) over an application input
 * stream.  There is no governing paper for this layer; system.md is
 * the governing specification, and this header and its companion .c
 * are aligned to it and to the system.dtc decision tables the same
 * way bkr94acs.[hc] aligns to BKR94ACS.txt and bkr94acs.dtc.
 *
 * The five obligations (system.md "The calculus"):
 *
 *   PARTICIPATE  born from verified existence evidence of a round
 *                this process has not reached; discharged by running
 *                the round's instance; retired by own COMPLETE or by
 *                adoption (O3: witnessed at t+1 distinct servers,
 *                closing through the same consume region).
 *   PRESENT      an accepted value owed until witnessed in an agreed
 *                subset (R1); the caller stages the bytes and rides
 *                them on launched rounds -- this machine sees it as
 *                the valuePending/maintenance launch inputs and the
 *                caller retires it at subset witness.
 *   ADMIT        the only obligation assumed by choice; gated on
 *                self-local capacity and the R4 advance rule; born
 *                from a pending value or from identity maintenance
 *                (O5), which outranks and never consumes the value.
 *   SERVE        born from want evidence for a retained round, two
 *                grains (O2): composition always, held members'
 *                content beside it; retired per process on
 *                possession evidence, per round on release.
 *   RETAIN       born at own COMPLETE or adoption; released on all-n
 *                possession or on eviction, oldest retained round
 *                first.
 *
 * Still caller-side per system.md "Relation to a deployment": the
 * serve walk's concurrency cap and rotation (pacing mechanics), the
 * cross-kind byte budget (artifact sizes are application domain;
 * systemEvict is its actuator), and the R2b resume constraint (it
 * binds the ACS instance state, which the caller owns).
 *
 * Pure state machine: no I/O, no threads, no dynamic allocation, no
 * wall-clock reasoning.  Caller provides memory, delivers evidence,
 * and executes output actions.  The ACS instances themselves are the
 * caller's (one bkr94acs per round); this machine never touches
 * them -- it decides WHEN one runs, WHO is served, and WHAT is
 * retained.  It composes with bkr94acs at the caller, not at
 * compile time, so this header stands alone.
 *
 * Rounds are the wrapping unsigned char space 0..255 (the same
 * discipline as Bracha87 BA rounds).  The caller presents only
 * traffic it has authenticated for rounds within its chain reach
 * (system.md "Model"): rounds at or behind the frontier, where the
 * frontier is the next round this process runs.  Traffic for rounds
 * beyond reach cannot be verified and must be held by the caller;
 * defensively, such rounds are treated as inert here.
 *
 * Operational limits:
 *   n:  unsigned char, encodes process count 1..256 (n + 1)
 *   t:  unsigned char, max Byzantine (n + 1 >= 3t + 1)
 *   w:  unsigned char, encodes retained window 1..256 rounds (w + 1)
 */

#ifndef SYSTEM_H
#define SYSTEM_H

/*************************************************************************/
/*  Output actions                                                       */
/*                                                                       */
/*  Returned in struct systemAct arrays.  Caller executes them:          */
/*  launching instances, serving artifacts, freeing retained state.      */
/*************************************************************************/

#define SYSTEM_ACT_DELIVER 1 /* feed the received traffic to the live
                              * instance (.round is the frontier) */
#define SYSTEM_ACT_SERVE   2 /* serve retained round .round; .want is the
                              * bitmap of processes owed it */
#define SYSTEM_ACT_JOIN    3 /* launch the instance for round .round without
                              * REQUIRING an application value (participation
                              * discharge).  A pending value rides the joined
                              * round exactly as it would an admitted one --
                              * participation is contribution (system.md,
                              * PARTICIPATE discharge); with none pending the
                              * instance runs with the deployment's
                              * empty/rotation value.  JOIN vs ADMIT
                              * distinguishes the obligation's cause and
                              * gating, never value consumption. */
#define SYSTEM_ACT_ADMIT   4 /* launch the instance for round .round,
                              * consuming the next application value */
#define SYSTEM_ACT_RELEASE 5 /* round .round released: free its artifact and
                              * serve state; the derivation floor for
                              * round-keyed state advances with it (system.md
                              * O4: the two floors are coupled -- release
                              * advances both together) */
#define SYSTEM_ACT_ADOPT   6 /* the frontier round's witnesses reached t+1
                              * distinct servers (system.md O3): the caller
                              * validates its candidate composition (fold
                              * reproduction and content are its crypto) and,
                              * on success, closes through systemComplete --
                              * the one consume region.  Output exactly once
                              * per frontier round (re-armed by
                              * systemWitnessReset or by advancing). */
#define SYSTEM_ACT_MAINTAIN 7 /* launch the instance for round .round as an
                              * identity-maintenance round (system.md O5):
                              * the contribution is the deployment's
                              * maintenance form, NEVER a pending application
                              * value -- a maintenance win is not the value's
                              * win, so PRESENT stays outstanding. */

/*
 * struct systemAct
 *
 * .want and .have (SERVE only) are borrowed pointers into
 * library-owned storage: the retained round's still-owed bitmap
 * (want minus possession; possession recording clears want bits)
 * and its held-members bitmap (system.md O2 -- which members'
 * content this process can serve; composition is served always,
 * held members' content beside it, and a member held nowhere
 * reachable is a per-member out-of-band hole, never the round's
 * failure).  Process p is owed iff SYSTEM_TST(want, p); member m
 * is servable iff SYSTEM_TST(have, m).  The have grain is
 * CALLER-FED surface (the machine never sees content and no rule
 * reads the bitmap): it holds exactly what systemComplete and
 * systemAssembled reported, so it is as-of-last-report.  A caller
 * keeping its own per-member content record may serve from that
 * record and leave the grain at its close-time value.  Valid until
 * the next call into the library on the same struct system.
 */
struct systemAct {
  const unsigned char *want; /* SERVE: owed-process bitmap; otherwise 0 */
  const unsigned char *have; /* SERVE: held-members bitmap; otherwise 0 */
  unsigned char act;         /* SYSTEM_ACT_* */
  unsigned char round;       /* which round this relates to */
};

/* Test process p's bit in a possession / want bitmap. */
#define SYSTEM_TST(map, p) ((map)[(p) >> 3] & (1 << ((p) & 7)))

/*
 * The R4 advance signal (system.md, participant tolerance): the
 * composition-possession class of the round below the frontier.
 * Returned by systemDuty and consumed by systemLaunch's advance
 * gate:
 *
 *   SYSTEM_DUTY_MET        all n possess it, or it is no longer
 *                          retained (duty is bounded by retention);
 *                          the frontier is free to advance.
 *   SYSTEM_DUTY_TOLERANCE  n-t possess it but not all n: the
 *                          bounded hold.  The caller counts T_p of
 *                          its own sweeps while this class holds --
 *                          the same discipline as the barren sweeps
 *                          that drive abandon -- and passes the
 *                          result as systemLaunch's toleranceElapsed.
 *   SYSTEM_DUTY_HELD       short of n-t: the frontier holds
 *                          regardless of tolerance.  The class
 *                          counts evidence RECEIVED, not possession
 *                          held, so it is not guaranteed transient
 *                          on a fault count alone: t withholders
 *                          cannot block it (n-t honest suffice),
 *                          but carrier loss can.  Both carriers
 *                          expire -- the indication rides the
 *                          round's own traffic, which the layer
 *                          below retires at quiescence, and the
 *                          inference rides later rounds, which
 *                          stand beyond the reach of a process that
 *                          cannot advance.  The heal serves a
 *                          process that genuinely lacks the round;
 *                          a caller this class strands reads no
 *                          progress, and the same barren sweeps
 *                          that drive abandon are its governing
 *                          exit -- no machine event is.  Duty stays
 *                          bounded by retention.
 */
#define SYSTEM_DUTY_MET       0
#define SYSTEM_DUTY_TOLERANCE 1
#define SYSTEM_DUTY_HELD      2

/*
 * Maximum output actions from a single call, any entry point.
 *
 * systemReceived:  1 DELIVER, or 1 SERVE, or 1 RELEASE (a possession
 *                  indication completing all-n), pairwise exclusive
 *                  by the dispatch rows -- but bounded 2 defensively.
 * systemComplete:  1 RELEASE -- window-full eviction of the oldest
 *                  retained round, or the round-space wrap boundary
 *                  (an entry 256 rounds behind the new frontier must
 *                  release before its round byte recurs).  The two
 *                  cannot fire together: the wrap release frees the
 *                  slot the insert reuses.
 * systemLaunch, systemPossessed, systemEvict, systemServe,
 * systemWitness: 1.
 */
#define SYSTEM_MAX_ACTS 2

/*************************************************************************/
/*  System state                                                         */
/*************************************************************************/

/* State flags (bitmap; same idiom as BKR94ACS_F_*) */
#define SYSTEM_F_LIVE  0x01 /* an instance is live for the frontier round */
#define SYSTEM_F_OWED  0x02 /* participation owed: existence evidence for the
                             * frontier round arrived with no live instance
                             * (system.dtc "record participation owed for R";
                             * DERIVED -- system.md R2a) */
#define SYSTEM_F_ADOPT 0x04 /* SYSTEM_ACT_ADOPT already output for the
                             * frontier round (single-fire latch; cleared by
                             * systemComplete and systemWitnessReset) */

/*
 * One process's seat in the system (system.md Model, the term pin):
 * per-process obligation state, n seats per system, exactly as
 * struct bkr94acs is one process's seat in one ACS instance.
 */
struct system {
  unsigned char n;        /* process count encoding: actual = n + 1 */
  unsigned char t;        /* max Byzantine (n + 1 >= 3t + 1) */
  unsigned char w;        /* retained window encoding: actual = w + 1 */
  unsigned char self;     /* this process's index */
  unsigned char flags;    /* SYSTEM_F_LIVE / SYSTEM_F_OWED / SYSTEM_F_ADOPT */
  unsigned char frontier; /* the next round this process runs (wraps) */
  /*
   * Retained-round and oldest-retained counts are not stored: a
   * stored count is a denormalization of the window entries (and
   * at the 256-round window it would need widening past unsigned
   * char).  Both are derived by scanning the window -- bounded 256
   * entries, at per-round events.  The advance-signal class is
   * likewise derived (systemDuty scans the prior round's bitmap).
   */
  unsigned char pad[2];
  unsigned char data[1];  /* variable: see systemSz */
};

/* data[] is the variable tail; see system.c for layout. */

/* Size in bytes needed for a system instance */
unsigned long
systemSz(
  unsigned int              /* n: actual process count = n + 1 */
 ,unsigned int              /* w: actual retained window = w + 1 */
);

/*
 * Initialize a system instance.  Caller has allocated systemSz bytes.
 * The frontier starts at round 0.  Rejects a null pointer, self > n
 * (an out-of-range self would corrupt every possession record),
 * n + 1 < 3t + 1 (the threshold arithmetic the advance rule rests
 * on), and the single-process encoding n = 0 (retention's
 * release-on-evidence discipline needs a roster beyond self: a lone
 * process's possession record is born already covering all n, and
 * the completion path takes no release decision); a rejected
 * instance is inert -- every entry point returns 0 on it.
 */
void
systemInit(
  struct system *
 ,unsigned char             /* n: actual process count = n + 1 */
 ,unsigned char             /* t: max Byzantine (n + 1 >= 3t + 1) */
 ,unsigned char             /* w: actual retained window = w + 1 */
 ,unsigned char             /* self: this process's index */
);

/*
 * Authenticated traffic for round 'round' arrived from process
 * 'from'.  'possesses' is 1 if the traffic carried a possession
 * indication for that round (the deployment's analog of the
 * BKR94ACS_ACCEPTED bit riding a READY -- see system.md "Model",
 * possession evidence).  The possession record is updated FIRST,
 * then the dispatch classifies (reads following writes), so a
 * process serving or re-broadcasting a round it holds is never
 * misread as wanting it.  Only RETAINED rounds have a record: an
 * indication for a round not yet retained -- one still live, or one
 * awaiting the join that will run it -- is dropped, not banked.  The
 * drop is safe in itself -- nothing false is ever recorded -- but it
 * is NOT self-correcting, and unlike the owed traffic below (whose
 * loss costs a retry cycle, not correctness) what is lost here is
 * unrecoverable.  The caller MUST hold such an indication and
 * re-present it through systemPossessed once the round is retained.
 * The evidence was authenticated when it arrived and marks only its
 * own sender, so re-presenting it later is exactly as contained as
 * banking it would have been; it was merely premature.
 * Hold the FRONTIER round's indications only -- that is the sole
 * round a close can retain -- and discard the hold on release.  A
 * hold keyed on "not retained" also catches released rounds, and in
 * the wrapping round space such an entry outlives its round and
 * resurfaces at the next incarnation of that byte.
 * The O1 inference recovers a dropped indication from the sender's
 * later-round traffic ONLY WHERE SUCH TRAFFIC COMES TO EXIST.  It is
 * not a general fallback: a cohort with nothing further to contribute
 * launches no later round, and there the drop is not a delay -- the
 * evidence is gone for good, and a correct process that closed the
 * round late can be left at SYSTEM_DUTY_HELD with neither carrier
 * remaining to open its next frontier.
 *
 * Out actions:
 *   SYSTEM_ACT_DELIVER  the round is the live instance's; feed the
 *                       traffic to it (then translate its outputs).
 *   SYSTEM_ACT_SERVE    want evidence for a retained round; serve
 *                       the artifact to the processes in .want (the
 *                       walk in systemServe re-outputs it each tick
 *                       until retired).
 *   SYSTEM_ACT_RELEASE  the possession record reached all n.
 *
 * A round beyond chain reach (ahead of the frontier) is inert:
 * the caller could not have verified it, so it creates no
 * obligation and returns 0 acts.  Existence evidence for the
 * frontier round itself, with no live instance, records
 * participation owed (SYSTEM_F_OWED; no act -- the discharge fires
 * at the next systemLaunch).  The caller should hold the traffic
 * that recorded owed and re-present it after the join (the layer
 * below's retry re-delivers regardless, so dropping it costs a
 * retry cycle, not correctness).
 *
 * Returns number of actions written to out[].
 * Caller provides out[] with room for SYSTEM_MAX_ACTS entries.
 */
unsigned int
systemReceived(
  struct system *
 ,unsigned char             /* round */
 ,unsigned char             /* from: authenticated sender */
 ,unsigned char             /* possesses: 1 = carried possession indication */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * Launch opportunity -- the caller's tick permits starting an
 * instance.  The tick paces this call (a wire rate limit, never a
 * correctness clock).  'valuePending' and 'backlogDrained' are
 * self-local facts supplied by the caller: an application value is
 * waiting, and this process's own backlog has drained -- every
 * result its prior advances accepted has reached its decision
 * stream (the capacity gate; system.md M2 -- read from the
 * caller's own books; emission is a put on a carrier contracted
 * to eventually drain and attests nothing beyond the attempt, so
 * no emission state is consulted).  'toleranceElapsed' is the R4
 * duty budget: 1 when the caller has counted T_p of its own sweeps
 * since systemDuty first reported SYSTEM_DUTY_TOLERANCE for the
 * current frontier (self-funded re-offers, the barren-sweep
 * discipline; reset the count when the frontier advances).
 *
 * All chosen actions are gated by the R4 advance rule -- the prior
 * round's possession class (computed internally from the retained
 * record) with 'toleranceElapsed' as the bounded escape:
 * SYSTEM_DUTY_MET advances; SYSTEM_DUTY_TOLERANCE advances only
 * with toleranceElapsed; SYSTEM_DUTY_HELD holds regardless.
 * Tolerance governs JOINS exactly as admissions (an early launcher
 * must gain nothing); the join remains ungated on the backlog --
 * capacity may defer only chosen work.  'maintenanceDue' is the O5
 * birth: authorship-budget exhaustion forces an identity-
 * maintenance round, which outranks the pending value and must not
 * consume it.
 *
 * Out actions (at most one; owed before maintenance before value):
 *   SYSTEM_ACT_JOIN     participation owed to the frontier round;
 *                       launch its instance (a pending value rides).
 *   SYSTEM_ACT_MAINTAIN maintenance due; launch the frontier
 *                       round's instance with the deployment's
 *                       maintenance form (the value stays staged).
 *   SYSTEM_ACT_ADMIT    nothing owed, no maintenance due, a value
 *                       pending, backlog drained; launch the
 *                       frontier round's instance consuming the
 *                       next value.
 *
 * Any action marks the instance live; systemComplete clears it.
 * Returns number of actions written to out[].
 */
unsigned int
systemLaunch(
  struct system *
 ,unsigned char             /* valuePending: 1 = application value waiting */
 ,unsigned char             /* maintenanceDue: 1 = identity maintenance due
                             * (O5; outranks the value, never consumes it) */
 ,unsigned char             /* backlogDrained: 1 = own backlog drained (every
                             * accepted result delivered; system.md M2) */
 ,unsigned char             /* toleranceElapsed: 1 = T_p sweeps since the
                             * n-t signal (SYSTEM_DUTY_TOLERANCE) */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * The frontier round's result is held: the live instance reached
 * COMPLETE, or an equivalent adopted result arrived through the
 * deployment's recovery path.  Either way an instance must be LIVE
 * for the round -- an adopted result reaches here through the JOIN
 * the recovery traffic itself provoked (it recorded owed at
 * systemReceived); with no live instance the call is inert.
 * This is the ONE consume region (system.md,
 * the wanting side): a live COMPLETE and an adoption close both
 * enter here, so a racing own COMPLETE structurally supersedes the
 * witness book -- completion clears it either way.  The close
 * speaks its round: 'round' must name the frontier (the live
 * instance's round), so a superseded instance's stale result --
 * retired THROUGH this region, never beside it (system.md, the
 * wanting side) -- cannot be booked as a later frontier's
 * completion after a relaunch (the systemWitness precedent:
 * refuse any round but the frontier).  'have' is the
 * members whose content this process holds at completion (O2;
 * bitmap of at least the n bitmap size, or 0 = none -- assembly may
 * lag COMPLETE; a caller relying on the machine's have grain
 * records late assembly via systemAssembled, one serving from its
 * own content record may leave it).
 * Retains the round -- possession starts at self -- advances the
 * frontier, and clears the live, owed, and adopt-latch flags.
 *
 * Out actions:
 *   SYSTEM_ACT_RELEASE  at most once: the oldest retained round
 *                       evicted to make room (window full), or the
 *                       round-space wrap boundary (an entry 256
 *                       rounds behind must release before its round
 *                       byte recurs; releasing it frees the slot, so
 *                       both never fire together).  An evicted round
 *                       is out-of-band territory (system.md, RETAIN).
 *
 * Returns number of actions written to out[]; 0 (and no state
 * change) if no instance is live or 'round' is not the frontier.
 */
unsigned int
systemComplete(
  struct system *
 ,unsigned char             /* round: the round being closed; must be
                             * the frontier (the live instance's) */
 ,const unsigned char *     /* have: held-members bitmap at completion
                             * (copied), or 0 = none yet */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * Possession-evidence ingress for a retained round (the analog of
 * bkr94acs*Accepted): process 'from' holds round 'round''s
 * artifact.  The derived source is the indication riding traffic
 * (systemReceived's 'possesses'); the second derived source is the
 * linkage inference (system.md O1): an authenticated act of round
 * R+1 or later evidences its sender's possession of R's
 * composition, with the same containment as the indication (it
 * marks only its own sender).  The deployment feeds either source
 * through this entry -- including an indication systemReceived
 * dropped for want of a record, held by the caller and re-presented
 * here once the close retained the round.  Idempotent; unretained
 * rounds and out-of-range indices are ignored.
 *
 * Byzantine-safe by construction: the record marks only 'from'
 * itself, so a forged indication retires only serves owed TO the
 * forger and can never strand a correct process.  Release requires
 * all n bits -- with at most t forgeries, reaching n still requires
 * every correct process's true indication.  No count-threshold
 * shortcut (system.md, Byzantine notes).
 *
 * Out actions:
 *   SYSTEM_ACT_RELEASE  the record reached all n.
 *
 * Returns number of actions written to out[].
 */
unsigned int
systemPossessed(
  struct system *
 ,unsigned char             /* round */
 ,unsigned char             /* from: process that evidenced possession */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * Witness-evidence ingress for the frontier round (system.md O3,
 * the wanting side).  The caller has validated a served assertion
 * from process 'from' as matching its candidate composition for
 * the frontier round -- byte comparison and fold verification are
 * the caller's; this machine counts DISTINCT servers.  The record
 * marks only 'from' (Byzantine containment: t forgers reach at
 * most t < t+1), self is not a server of its own adoption, and
 * rounds other than the frontier are ignored (behind rounds are
 * served, not adopted; ahead rounds are beyond reach).
 * Idempotent per server.  Accumulated witness evidence is
 * M1-protected: it survives local quiet and is cleared only by
 * completion, reset, or teardown.
 *
 * Out actions:
 *   SYSTEM_ACT_ADOPT  distinct witnesses reached t+1 (>= 1
 *                     honest): the candidate composition is
 *                     acceptable.  Output exactly once; the caller
 *                     closes through systemComplete (JOINing first
 *                     if no instance is live -- the recovery
 *                     traffic recorded owed).
 *
 * The adopt rule is deliberately NOT in the merged dispatch: its
 * inputs appear in no other rule, so it is an independent rule
 * set, captured as this entry's C guard (see the witness-event
 * section of system.dtc -- the bkr94acs BPR-gate precedent).
 *
 * Returns number of actions written to out[].
 */
unsigned int
systemWitness(
  struct system *
 ,unsigned char             /* round: must be the frontier */
 ,unsigned char             /* from: the serving process */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * Discard the frontier round's accumulated witnesses and re-arm
 * the adopt signal -- for a caller switching candidate compositions
 * (a Byzantine server asserted a conflicting candidate that
 * accumulated; honest servers all assert the agreed one, so the
 * true candidate re-accumulates and a fake one never reaches t+1).
 */
void
systemWitnessReset(
  struct system *
);

/*
 * Record late assembly (O2): this process now holds member
 * 'member''s content for retained round 'round' (content
 * possession lags COMPLETE -- assembly is a separate t+1 gather).
 * Extends what SERVE acts carry in .have.  Idempotent; unretained
 * rounds and out-of-range members are ignored.
 * Feeding this entry is OPTIONAL: no machine rule reads the have
 * grain (it is carried surface), so only a caller that relies on
 * .have as its content-possession record need call it; a caller
 * with its own record may decline, leaving .have as-of-close.
 */
void
systemAssembled(
  struct system *
 ,unsigned char             /* round */
 ,unsigned char             /* member */
);

/*
 * Retention-budget eviction.  The artifact byte accounting is the
 * caller's (artifact sizes are application domain); when its budget
 * is exceeded it calls this until back under.  Releases the oldest
 * retained round (the caller frees the artifact; the round becomes
 * out-of-band territory).
 *
 * Out actions:
 *   SYSTEM_ACT_RELEASE  the oldest retained round.
 *
 * Returns number of actions written to out[]; 0 if nothing is
 * retained.
 */
unsigned int
systemEvict(
  struct system *
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * Serve walk -- the SERVE analog of bkr94acsRetry, with the same
 * one-call-per-tick semantic and flood warning.  The cursor lives
 * in caller storage (initialize to 0); each call advances it to the
 * next retained round still owed to some process and outputs one
 * SERVE act for it (.want = the still-owed bitmap).  Per-process
 * retirement is possession recording (which clears want bits);
 * per-round retirement is release.
 *
 * Returns 0 only when a full sweep finds nothing owed -- the serve
 * quiescence signal, not a termination signal.
 */
unsigned int
systemServe(
  struct system *
 ,unsigned char *           /* cursor: caller storage, init to 0 */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*************************************************************************/
/*  Queries (read-only; the bkr94acsBaDecision idiom)                    */
/*************************************************************************/

/* The next round this process runs.  0 on null state (indistinguishable
 * from a fresh instance -- check the pointer when it matters). */
unsigned char
systemFrontier(
  const struct system *
);

/* 1 iff an instance is live for the frontier round. */
unsigned int
systemLive(
  const struct system *
);

/* 1 iff participation is owed to the frontier round. */
unsigned int
systemOwed(
  const struct system *
);

/*
 * The R4 advance signal for the current frontier: the possession
 * class of the round below it (SYSTEM_DUTY_MET / _TOLERANCE /
 * _HELD; see the defines).  The caller starts its T_p sweep count
 * when this first returns SYSTEM_DUTY_TOLERANCE and resets it when
 * the frontier advances.  Returns SYSTEM_DUTY_MET on a null state
 * (inert instances hold nothing).
 */
unsigned int
systemDuty(
  const struct system *
);

/* 1 iff 'round' is retained (COMPLETE, not yet released). */
unsigned int
systemRetained(
  const struct system *
 ,unsigned char             /* round */
);

/*
 * Borrowed pointers to a retained round's possession and still-owed
 * bitmaps (test with SYSTEM_TST; same lifetime discipline as
 * bkr94acsAcastSkip).  0 for a null/unretained argument.
 */
const unsigned char *
systemPossess(
  const struct system *
 ,unsigned char             /* round */
);

const unsigned char *
systemWant(
  const struct system *
 ,unsigned char             /* round */
);

/* Borrowed pointer to a retained round's held-members bitmap (O2);
 * 0 for a null/unretained argument. */
const unsigned char *
systemHave(
  const struct system *
 ,unsigned char             /* round */
);

/* Borrowed pointer to the frontier round's distinct-witness bitmap
 * (O3); 0 on a null/rejected state. */
const unsigned char *
systemWitnesses(
  const struct system *
);

#endif /* SYSTEM_H */
