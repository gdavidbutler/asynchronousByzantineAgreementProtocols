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
 * THE RETAINED ROUNDS ARE THE CALLER'S.  Every rule about them is
 * decided here -- RETAIN is born at the close, RELEASE fires at
 * all-n or at the reach, SERVE is born from want evidence, the
 * advance signal reads the round below the frontier -- but none of
 * them is STORED here.  The rounds this process holds, and the
 * records it keeps per held round, live in caller storage reached
 * through four operations fixed at init (systemInit), which are
 * the whole of what this machine asks of retention:
 *
 *   records   what do I hold for round R (0 = R is not retained)
 *   retain    begin holding R (0 = the reach has no room)
 *   release   stop holding R
 *   after     what do I hold after R -- and, asked of no round,
 *             the OLDEST held.  The two enumerating rules consume
 *             their order here: the cursor birth walks the rounds
 *             after a sender's cursor, eviction takes the oldest
 *
 * A round's RECORDS are three process bitmaps -- possession, still
 * owed, held members -- laid out by this machine and sized by
 * systemRecordsSz; the caller supplies those bytes and keys them
 * by the round's name.  That is the seam: the caller answers WHAT
 * IS HELD, this machine answers WHAT TO DO ABOUT IT.  How deep to
 * retain, and in what, is the caller's -- the declared RECOVERY
 * REACH (system.md O6) is its store's own bound, reported by
 * refusing to retain, not a window sized into this machine.
 *
 * Rounds are NAMES -- places in the round chain (system.md
 * "Model"; the calculus consumes identity and order only, never an
 * arithmetic quantity).  A round argument is the caller's CANONICAL
 * NAME for a resolved place: rs bytes, opaque here, fixed at init.
 * The machine performs no arithmetic on a name -- it asks exactly
 * the questions the calculus asks (system.md, Mechanization
 * status: the operations on a round):
 *
 *   equal      is this the same place (binding possession, want,
 *              have, witness records; classifying a presented
 *              round against the frontier and the retained set)
 *   order      does this place precede that one (the cursor birth,
 *              eviction's oldest-first, the serve rotation, the
 *              ahead classification)
 *   mint       succession is never computed: closing round R is
 *              the event that names R+1, and the close HANDS the
 *              machine the new frontier name (systemComplete's
 *              next argument).  The minted name must FOLLOW,
 *              under cmp, the frontier it succeeds and every
 *              retained round -- distinctness alone is not enough
 *              (system.md, Mechanization status: succession)
 *   genesis    provisioning names the first frontier (systemInit)
 *
 * equal and order are answered by ONE caller-supplied comparator
 * (systemInit's cmp) so the two cannot diverge; predecessor is not
 * an operation (the round below the frontier is the last-closed
 * round -- history the machine holds); magnitude is not an
 * operation (order is consumed, never measured -- no rule reads a
 * distance or a width).  A name may be as simple as an ordinal's
 * bytes or as complex as a deployment's composite chain construct;
 * the machine cannot tell, which is the point.
 * The caller presents only traffic it has authenticated for rounds
 * within its chain reach: it RESOLVES each wire's asserted round
 * identity against the identities it retains, and the resolved
 * place's name is the round argument (resolution, not mapping).
 * Reach is bounded on BOTH sides: ahead, by the frontier -- a round
 * this process has not run cannot be verified; behind, by what it
 * still retains, since verifying an act as being of round R
 * consumes R's identity.  Traffic outside reach cannot be verified
 * and must be held or dropped by the caller; defensively, rounds
 * ordering after the frontier are treated as inert here.
 *
 * A BYTE IS NOT A ROUND.  A deployment's wire may carry a bounded
 * routing name beside the identity -- a demux hint, and a
 * cross-check that refuses a wire whose hint disagrees with the
 * resolved place -- but nothing is believed on it and no
 * argument below speaks it.  Every 'round' argument below is the
 * name of the place the traffic's IDENTITY proves, never a round
 * some bounded routing name suggests -- see the entry points below for what
 * banking on a bounded name would cost.  A byte routes; an identity proves.
 *
 * Operational limits:
 *   n:  unsigned char, encodes process count 1..256 (n + 1)
 *   t:  unsigned char, max Byzantine (n + 1 >= 3t + 1)
 *   rs: unsigned int, the round-name size in bytes (caller-fixed
 *       at init; every round argument and every name the store
 *       hands back is exactly rs bytes -- deployment sizing)
 *
 * Retention has no limit HERE.  How many rounds the store will
 * hold is the deployment's declared RECOVERY REACH (system.md
 * O6), and it is not the retention REQUIREMENT, which is derived
 * from evidence and stays unbounded while any process withholds
 * (system.md R4, absence); the gap between what must be retained
 * and what a store can hold is not a defect to be closed but the
 * very thing R4 names -- fault budget spent on whoever still
 * lacks the round.  Two finite resources bound it independently
 * and either may bind first: the store's own room (reported by
 * refusing to retain, which this machine answers by evicting the
 * oldest) and the caller's artifact bytes (systemEvict).  Both
 * are the reach seen from one side or the other.
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
#define SYSTEM_ACT_RELEASE 5 /* round .round left retention: the machine
                              * has ALREADY dropped it through the caller's
                              * own release operation, so this act says free
                              * that round's ARTIFACT and serve state -- not
                              * drop the round again; the derivation
                              * floor for round-keyed state advances with it
                              * (system.md O4: the two floors are coupled).
                              * ONE act, TWO causes the caller must not
                              * conflate: an all-n RELEASE, where nothing
                              * can still be owed; or a WITHDRAWAL, where
                              * this process merely ceased to hold
                              * (system.md RETAIN).  The act does not say
                              * which; the entry point does -- systemEvict
                              * and systemComplete's no-room path are
                              * withdrawals, every other site is all-n */
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
 * .want and .have (SERVE only) are borrowed pointers INTO THE
 * CALLER'S OWN RECORDS for the retained round -- its still-owed
 * bitmap (want minus possession; possession recording clears want
 * bits) and its held-members bitmap (system.md O2 -- which members'
 * content this process can serve; composition is served always,
 * held members' content beside it, and a member held nowhere
 * reachable is a per-member out-of-band hole, never the round's
 * failure).  Process p is owed iff SYSTEM_TST(want, p); member m
 * is servable iff SYSTEM_TST(have, m).  The have grain is
 * CALLER-FED surface (the machine never sees content and no rule
 * reads the bitmap): it holds exactly what systemComplete and
 * systemAssembled reported, so it is as-of-last-report.  A caller
 * keeping its own per-member content record may serve from that
 * record and leave the grain at its close-time value.
 * .round is a borrowed pointer to the rs-byte name of the round
 * the act relates to, and it names one of three places: the
 * frontier's name, which this machine holds; the name the caller
 * presented to the call, which it must keep valid until it has
 * consumed the acts; or the name the store handed back.  A
 * RELEASE act is the exception and is always this machine's own
 * copy -- the round is no longer in the store to point at, which
 * is exactly why the copy exists.
 * All three pointers are valid until the next call into the
 * library on the same struct system, and .want/.have no longer
 * than the caller's own records for that round.
 */
struct systemAct {
  const unsigned char *want;  /* SERVE: owed-process bitmap; otherwise 0 */
  const unsigned char *have;  /* SERVE: held-members bitmap; otherwise 0 */
  const unsigned char *round; /* which round this relates to (rs bytes) */
  unsigned char act;          /* SYSTEM_ACT_* */
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
 *                          cannot block it (n-t correct suffice),
 *                          but carrier loss can.  Both carriers
 *                          expire -- the indication rides the
 *                          round's own traffic, which the layer
 *                          below retires at quiescence, and the
 *                          inference rides later rounds, which
 *                          stand beyond the reach of a process that
 *                          cannot advance -- but a stranded
 *                          process's own acts at its authored
 *                          high-water are its cursor (system.md
 *                          Model, want): wherever a correct
 *                          process retains rounds after that
 *                          cursor, the cursor want they birth
 *                          draws serves whose traffic carries the
 *                          record back within reach -- the heal's
 *                          re-entry route.  The heal serves a
 *                          process that genuinely lacks a round;
 *                          where nothing is retained beyond the
 *                          cursor, nothing is owed, and the same
 *                          barren sweeps that drive abandon are
 *                          the caller's governing exit -- no
 *                          machine event is.  Duty stays bounded
 *                          by retention.
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
 * systemComplete:  1 RELEASE -- eviction of the oldest retained
 *                  round, the store having no room.
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
  int (*cmp)(void *, const unsigned char *, const unsigned char *);
                          /* the round comparator: one authority for
                           * equal AND order (see systemInit) */
  unsigned char *(*records)(void *, const unsigned char *);
  unsigned char *(*retain)(void *, const unsigned char *);
  void (*release)(void *, const unsigned char *);
  unsigned char *(*after)(void *, const unsigned char *
   ,const unsigned char **);
                          /* the retention operations (see systemInit) */
  void *ctx;              /* opaque caller context, handed to the
                           * comparator and to every operation */
  unsigned int rs;        /* round-name size in bytes */
  unsigned char n;        /* process count encoding: actual = n + 1 */
  unsigned char t;        /* max Byzantine (n + 1 >= 3t + 1) */
  unsigned char self;     /* this process's index */
  unsigned char flags;    /* SYSTEM_F_LIVE / SYSTEM_F_OWED / SYSTEM_F_ADOPT */
  /*
   * No count of retained rounds is stored, and none is asked of
   * the store: a count is a denormalization, and no rule reads
   * one.  Oldest is asked ('after' with no round); the
   * advance-signal class is derived from the last-closed round's
   * records (systemDuty).
   */
  unsigned char data[1];  /* variable: see systemSz */
};

/* data[] is the variable tail (the frontier and last-closed round
 * names live there beside the witness book); see system.c for
 * layout. */

/* Size in bytes needed for a system instance */
unsigned long
systemSz(
  unsigned int              /* n: actual process count = n + 1 */
 ,unsigned int              /* rs: round-name size in bytes */
);

/*
 * Size in bytes of ONE retained round's records -- three process
 * bitmaps laid end to end: possession, still owed, held members,
 * each (n + 8) / 8 bytes.  The caller allocates this much per
 * round it holds and hands the bytes back from 'records', 'retain'
 * and 'after'; this machine lays them out and is their SOLE
 * WRITER (system.md, the retention seam: the invariant's record
 * clauses read a record and conclude who wrote it).  A caller
 * reads them freely -- SYSTEM_TST against systemPossess /
 * systemWant / systemHave, which name the three slices.
 */
unsigned long
systemRecordsSz(
  unsigned int              /* n: actual process count = n + 1 */
);

/*
 * Size in bytes of a serve-walk cursor (systemServe): a round name
 * and the byte distinguishing "no round served yet" from a served
 * round whose name happens to be all zeroes -- names are opaque,
 * so no name value can serve as that sentinel.
 */
unsigned long
systemCursorSz(
  unsigned int              /* rs: round-name size in bytes */
);

/*
 * Initialize a system instance.  Caller has allocated systemSz bytes.
 * 'genesis' names the first frontier (rs bytes, copied) -- the
 * provisioning-delivered base of the round chain.
 *
 * 'cmp' is the round comparator, the ONE authority for both round
 * operations the calculus consumes: called as cmp(ctx, a, b) on two
 * rs-byte names, it returns 0 iff a and b name the same place,
 * negative if a's place precedes b's, positive if it follows.
 * Identity and order come from the same function so they cannot
 * diverge.  It must be a pure total order over the names the caller
 * can present plus the names the machine holds (the caller's own
 * reach -- resolution precedes presentation, so the caller can
 * always answer), and it must not call back into this machine.
 *
 * The ordinal instantiation (rs = sizeof (unsigned long), the name
 * is the ordinal's bytes) uses DISPLACEMENT order, sound at any
 * width and across the ordinal's wrap -- the wrap is the
 * comparator's to absorb, never the machine's:
 *
 *   unsigned long a, b, d;
 *   memcpy(&a, aName, sizeof (a));
 *   memcpy(&b, bName, sizeof (b));
 *   if (!(d = b - a)) return (0);
 *   return (d <= ((unsigned long)-1 >> 1) ? -1 : 1);
 *
 * (a precedes b iff b - a, taken modulo the width, falls in the
 * half-space; the caller's simultaneous reach must stay inside it.)
 *
 * THE RETENTION OPERATIONS.  The four below are how this machine
 * reaches the rounds the caller holds; each takes ctx first and
 * an rs-byte name where it takes one.
 *
 *   records(ctx, round)
 *     the records held for 'round', or 0 if it is not retained.
 *   retain(ctx, round)
 *     begin holding 'round' -- key it by a COPY of the name and
 *     return its records, whose contents this machine then
 *     initializes; or return 0 when the reach has no room.
 *   release(ctx, round)
 *     stop holding 'round'.  This runs while the round's records
 *     still exist, which is where a withdrawal can name the
 *     processes it leaves unserved -- the complement of the
 *     possession record (system.md R4, absence).
 *   after(ctx, round, name)
 *     the earliest round retained AFTER 'round', or -- with
 *     'round' 0 -- the OLDEST retained round: its records, with
 *     *name set to the store's own copy of its name; 0 when none
 *     follows.  'round' need not itself be retained.
 *
 * THE RETENTION REQUIREMENTS (system.md, Mechanization status,
 * states these canonically; this list mirrors it).  They are
 * correctness terms, not advice -- each is consumed by a named
 * invariant or requirement, and breaking one does not make this
 * machine answer wrongly, it makes the answers be about a
 * different machine's state:
 *
 *   The SET CHANGES ONLY THROUGH retain AND release.  A round
 *   added behind the machine is retained with no birth; a round
 *   dropped behind it departs with no RELEASE act, so its
 *   artifact is never freed and its SERVE obligation vanishes --
 *   a correct process wanting it is shed with no event anywhere
 *   (system.md R4, absence; O6 admits only an eviction the
 *   process REPORTS).  In particular retain NEVER makes room by
 *   dropping a round: refusing is how the reach binds, and the
 *   machine answers a refusal with an eviction that is reported.
 *   A refusal must have CHANGED NOTHING.
 *   retain SUCCEEDS for the round a close names, once the
 *   machine has released the oldest to make room.  THE FRONTIER
 *   ADVANCES AT EVERY CLOSE -- not conditional on storage -- and
 *   a caller may release retention to keep that true, shedding
 *   announced rounds through systemEvict between calls.  A close
 *   whose round is not retained has shed it silently: nothing
 *   can serve it, and the advance signal reads its absence as
 *   duty met.
 *   The MACHINE IS THE SOLE WRITER of the records.  The caller
 *   allocates and keys them and reads them freely; every bit is
 *   set here.
 *   after ENUMERATES IN THE COMPARATOR'S ORDER, strictly
 *   forward, each retained round once, answering 0 at the end.
 *   A store that answers otherwise is NOT DEFENDED AGAINST and
 *   will not terminate -- the one place this layer is not inert
 *   on bad input, named here rather than discovered.
 *   A RECORD AND A NAME STAY VALID until the set NEXT CHANGES
 *   through retain or release, and a retained round's record
 *   keeps its CONTENTS through every change short of its own
 *   release: a store may relocate storage to keep its order when
 *   the set changes, but what it moves, it moves intact, and a
 *   re-ask reaches it.  Until-return is not enough -- the acts
 *   hand records and names back to the caller after the call
 *   returns (the serve act's round is the store's own name); the
 *   set changes only inside calls into this machine, which is
 *   why a caller that consumes its acts before its next call is
 *   safe.
 *   release IS THE DROP: the machine has already called it for
 *   the round a SYSTEM_ACT_RELEASE names.  The act says free
 *   that round's ARTIFACT and serve state, never drop the round
 *   again.
 *   The STORE IS EMPTY at systemInit.
 *
 * Rejects a null pointer, self > n (an out-of-range self would
 * corrupt every possession record), n + 1 < 3t + 1 (the threshold
 * arithmetic the advance rule rests on), the single-process
 * encoding n = 0 (retention's release-on-evidence discipline needs
 * a roster beyond self: a lone process's possession record is born
 * already covering all n, and the completion path takes no release
 * decision), rs = 0, a null genesis or cmp, and a missing
 * retention operation; a rejected instance is inert -- every entry
 * point returns 0 on it.
 */
void
systemInit(
  struct system *
 ,unsigned char             /* n: actual process count = n + 1 */
 ,unsigned char             /* t: max Byzantine (n + 1 >= 3t + 1) */
 ,unsigned char             /* self: this process's index */
 ,unsigned int              /* rs: round-name size in bytes */
 ,const unsigned char *     /* genesis: the first frontier's name
                             * (rs bytes, copied) */
 ,int (*)(void *, const unsigned char *, const unsigned char *)
                            /* cmp: the round comparator (see above) */
 ,unsigned char *(*)(void *, const unsigned char *)
                            /* records: what is held for a round */
 ,unsigned char *(*)(void *, const unsigned char *)
                            /* retain: begin holding a round */
 ,void (*)(void *, const unsigned char *)
                            /* release: stop holding a round */
 ,unsigned char *(*)(void *, const unsigned char *
                    ,const unsigned char **)
                            /* after: what is held after a round */
 ,void *                    /* ctx: opaque caller context, handed to
                             * the comparator and every operation */
);

/*
 * Authenticated traffic for round 'round' arrived from process
 * 'from'.  'possesses' is 1 if the traffic carried a possession
 * indication for that round (the deployment's analog of the
 * BKR94ACS_ACCEPTED bit riding a READY -- see system.md "Model",
 * possession evidence).  'cursor' is 1 if the act stands at its
 * sender's authored HIGH-WATER -- the latest place in that sender's
 * authorship order this caller has observed (system.md "Model",
 * want; the caller owns the authorship order, so the caller
 * asserts this).  A high-water act locates the sender's cursor at
 * 'round', and the machine records want, for that sender, of
 * every retained round after it whose possession the sender has
 * not evidenced -- discharged by the serve walk, retired by
 * possession evidence like any want.  An act below the high-water
 * is carrier traffic (a retry at its original place; its author
 * may stand anywhere later): pass 0, and 0 is always safe -- it
 * costs the re-entry route, never correctness.  Same containment
 * as everything else here: only the given sender's bits, gated on
 * that sender's own possession record.
 * 'round' MUST name the place the traffic's identity proves,
 * verified within chain reach -- derived by resolving the asserted
 * identity against this process's retained history, never by
 * translating a bounded wire name (system.md, Mechanization
 * status: the round argument; a hint-keyed translation records a
 * CORRECT process's true indication against a round it does not
 * hold, a bit no fault budget bounds and no author disowns).  A
 * caller that cannot verify the traffic has no round to present it
 * for.  The possession record is updated FIRST, then the dispatch
 * classifies (reads following writes), so a process serving or
 * re-broadcasting a round it holds is never misread as wanting
 * it.  Only RETAINED rounds have a record: an
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
 * round a close can retain -- and discard the hold on release: a
 * hold keyed on "not retained" also catches released rounds,
 * holding evidence no close will ever bank.  Hold only what
 * identity proves is the frontier's.
 * The O1 inference recovers a dropped indication from the sender's
 * later-round traffic ONLY WHERE SUCH TRAFFIC COMES TO EXIST.  It is
 * not a general fallback: a cohort with nothing further to contribute
 * launches no later round, and there the drop is not a delay -- the
 * evidence is gone for good.  A correct process that closed the
 * round late once stranded at SYSTEM_DUTY_HELD with neither carrier
 * remaining; the cursor birth is its re-entry where later rounds DO
 * exist -- its own re-presents at its high-water birth cursor want
 * at every correct retainer of them, and the serves that answers
 * carry the record back within its reach.  Where nothing later is
 * retained anywhere -- the cohort together at one frontier -- the
 * hold-and-re-present above is the only carrier, which is why it is
 * an obligation and not an optimization.
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
 ,const unsigned char *     /* round: the resolved place's name (rs bytes) */
 ,unsigned char             /* from: authenticated sender */
 ,unsigned char             /* possesses: 1 = carried possession indication */
 ,unsigned char             /* cursor: 1 = the act stands at its sender's
                             * authored high-water (cursor evidence) */
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
 * 'next' is the new frontier's name (rs bytes, copied): succession
 * is MINTED at the close, never computed here -- closing round R is
 * the event that names R+1 (a chain deployment folds the new head
 * at exactly this moment; the ordinal instantiation supplies
 * R + 1).  The caller owns name distinctness (a name released
 * before recurrence) AND name order: the minted name must follow,
 * under cmp, the frontier it succeeds and every retained round --
 * the retained set stays strictly behind the frontier only
 * because every birth does (system.md, Mechanization status:
 * succession).
 *
 * Out actions:
 *   SYSTEM_ACT_RELEASE  at most once: the oldest retained round
 *                       evicted to make room, the store having
 *                       refused to retain another -- a
 *                       WITHDRAWAL, not an all-n release: this
 *                       process ceasing to hold, never a claim
 *                       the round is no longer needed.  It is
 *                       out-of-band territory (system.md RETAIN)
 *                       and it spends fault budget on whoever
 *                       still lacks the round (R4, absence).
 *
 * Returns number of actions written to out[]; 0 (and no state
 * change) if no instance is live or 'round' is not the frontier.
 */
unsigned int
systemComplete(
  struct system *
 ,const unsigned char *     /* round: the round being closed (rs bytes);
                             * must name the frontier (the live
                             * instance's) */
 ,const unsigned char *     /* next: the new frontier's name (rs bytes,
                             * copied) -- minted by this close */
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
 * 'round' MUST be the round the evidence's identity proves -- the
 * same obligation systemReceived carries, and it binds the linkage
 * inference too: an act evidences possession of the round its own
 * identity places it after, never of a round some bounded name
 * suggests.
 *
 * Byzantine-safe by construction: the record marks only 'from'
 * itself, so a forged indication retires only serves owed TO the
 * forger and can never strand a correct process.  Release requires
 * all n bits -- with at most t forgeries, reaching n still requires
 * every correct process's true indication.  No count-threshold
 * shortcut (system.md, Byzantine notes).
 *
 * That argument prices FORGERIES, and prices them by their author.
 * It does not reach a bit the CALLER misplaced: a correct process's
 * true indication, translated onto the wrong round, is a false bit
 * no fault budget bounds and no author disowns.  The precondition
 * above is what keeps the containment whole.
 *
 * Out actions:
 *   SYSTEM_ACT_RELEASE  the record reached all n.
 *
 * Returns number of actions written to out[].
 */
unsigned int
systemPossessed(
  struct system *
 ,const unsigned char *     /* round: the evidenced place's name (rs bytes) */
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
 *                     correct): the candidate composition is
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
 ,const unsigned char *     /* round: must name the frontier (rs bytes) */
 ,unsigned char             /* from: the serving process */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*
 * Discard the frontier round's accumulated witnesses and re-arm
 * the adopt signal -- for a caller switching candidate compositions
 * (a Byzantine server asserted a conflicting candidate that
 * accumulated; correct servers all assert the agreed one, so the
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
 ,const unsigned char *     /* round: the retained place's name (rs bytes) */
 ,unsigned char             /* member */
);

/*
 * Recovery-reach eviction -- a WITHDRAWAL, never an all-n release.
 * The artifact byte accounting is the caller's (artifact sizes are
 * application domain); when its declared reach (system.md O6) is
 * exhausted it calls this until back under.  Drops the oldest
 * retained round: this process ceasing to hold, never a claim that
 * the round is no longer needed.  It spends fault budget on every
 * process still lacking that round (system.md R4, absence), and
 * the round becomes out-of-band territory.
 *
 * Oldest-first is load-bearing, not a convenience: a returner
 * climbs rung by rung, and a rung dropped from the middle stalls a
 * climb nothing afterward can observe (system.md L1).
 *
 * R4 requires a withdrawal to NAME the processes it leaves
 * unserved -- the complement of the possession record, strictly
 * larger than the still-owed record.  The ACT does not carry that
 * set (it carries the round alone; .want is a SERVE field, 0
 * here).  The caller's own 'release' operation is where it is
 * named: this machine calls it while the round's records still
 * exist, so the possession record is in hand there and the
 * complement is one pass over it.
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
 * one-call-per-tick semantic and flood warning.  Each call serves
 * the first round still owed to some process AFTER the one the
 * last call served, wrapping to the oldest owed round at the end
 * of the retained set, and outputs one SERVE act for it (.want =
 * the still-owed bitmap).  Per-process retirement is possession
 * recording (which clears want bits); per-round retirement is
 * release.
 *
 * The cursor is caller storage of systemCursorSz(rs) bytes, ZEROED
 * before the first call: it holds the NAME of the round last
 * served, so the rotation is positioned in the round order ITSELF
 * and not by a count into a set that changes between calls -- a
 * name still positions the walk exactly when rounds are released
 * beneath it, where a count silently skips.
 * The walk advances one owed round per call and wraps at the end,
 * so a round continuously owed is revisited within one turn of the
 * retained set WHILE closes do not outpace serves.  That proviso
 * is R4's own throttle seen from the serving side, not a separate
 * assumption: a frontier below all-n advances at most once per T_p
 * sweeps while this walk runs every tick, and a returner is behind
 * exactly when that gate is holding -- the rotation is slowest to
 * wrap precisely when nothing needs it to.  A caller that closes
 * as fast as it serves has no returner to starve.
 * A cursor naming a round since released still positions the walk;
 * nothing requires it be retained.
 *
 * Returns 0 only when a full sweep finds nothing owed -- the serve
 * quiescence signal, not a termination signal.
 */
unsigned int
systemServe(
  struct system *
 ,unsigned char *           /* cursor: caller storage of
                             * systemCursorSz(rs) bytes, zeroed before
                             * the first call */
 ,struct systemAct *        /* out: room for SYSTEM_MAX_ACTS */
);

/*************************************************************************/
/*  Queries (read-only; the bkr94acsBaDecision idiom)                    */
/*************************************************************************/

/* Borrowed pointer to the name of the next round this process runs
 * (rs bytes); 0 on a null/rejected state. */
const unsigned char *
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
 ,const unsigned char *     /* round: the place's name (rs bytes) */
);

/*
 * The three slices of a retained round's records, in layout order
 * -- possession, still owed, held members.  Each is a borrowed
 * pointer into the caller's own records for that round (test with
 * SYSTEM_TST); 0 for a null/unretained argument.  A caller that
 * would rather index its records directly may: these name the
 * layout this machine writes, they do not hide it.
 */
const unsigned char *
systemPossess(
  const struct system *
 ,const unsigned char *     /* round: the place's name (rs bytes) */
);

const unsigned char *
systemWant(
  const struct system *
 ,const unsigned char *     /* round: the place's name (rs bytes) */
);

/* The held-members slice (O2). */
const unsigned char *
systemHave(
  const struct system *
 ,const unsigned char *     /* round: the place's name (rs bytes) */
);

/* Borrowed pointer to the frontier round's distinct-witness bitmap
 * (O3); 0 on a null/rejected state. */
const unsigned char *
systemWitnesses(
  const struct system *
);

#endif /* SYSTEM_H */
