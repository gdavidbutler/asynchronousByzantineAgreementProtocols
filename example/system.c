/*
 * asynchronousByzantineAgreementProtocols - Example system layer program
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
 * system.c -- Standalone demonstration of the system obligation layer
 * (system.md) composed with BKR94 ACS.
 *
 * What the system layer brings to the table:
 *
 *   One ACS instance agrees on ONE subset.  An application has a
 *   STREAM.  Turning a series of one-shot agreements into a single
 *   ordered sequence is not free: ACS legitimately excludes up to t
 *   honest contributions per round, so an excluded value must ride
 *   the next round -- without appearing twice if it did land, and
 *   without the sequence forking when a process falls behind.
 *
 * Three printed verdicts, in the order a reviewer should read them:
 *
 *   sequence identity  every process's agreed sequence is
 *                      byte-identical at every position both hold
 *                      (system.md L6).
 *   exactly-once       every presented value appears in the agreed
 *                      sequence exactly once -- never zero, never
 *                      twice (system.md R1).  R1 is a CONJUNCTION:
 *                      the machine half (only ADMIT consumes a value,
 *                      MAINTAIN outranks and never consumes, one
 *                      launch act per opportunity) is system.[hc]'s;
 *                      the caller half (stage the bytes, re-present
 *                      them byte-identically on every launch, retire
 *                      only on witnessing the value in an agreed
 *                      subset) is THIS FILE's, and is the half the
 *                      layer proves nothing about.
 *   laggard heals      a process cut off for one round does not fork
 *                      and is not stranded: the cohort advances under
 *                      the R4 tolerance, the cut process adopts from
 *                      served evidence, and its sequence still
 *                      matches.
 *   containment        with -B, one Byzantine process lying about
 *                      possession, want, and the served composition
 *                      changes nothing the correct processes agree on.
 *                      Every verdict quantifies over CORRECT processes
 *                      only; a Byzantine process's own state proves
 *                      nothing and is excluded.
 *
 * The Byzantine arm attacks the SYSTEM layer's own containment claims,
 * not Bracha's: an equivocating initiator is Lemma 2's business and is
 * demonstrated by example/bracha87Fig1.c.  The three lies and what each
 * tests --
 *
 *   forge possession  claim to hold every round.  The machine marks
 *                     only the authenticated sender, so this retires
 *                     only the serves owed to the liar; release still
 *                     needs all n bits, so every correct process's TRUE
 *                     indication is still required.  No count-threshold
 *                     shortcut exists for exactly this reason.
 *   forge want        never admit holding anything, so every round
 *                     reads as wanted and soaks serve slots.  The cap
 *                     is t with a floor of one and grants rotate, so a
 *                     correct wanting process is still served.
 *   fake candidate    serve a fabricated composition on recovery legs,
 *                     for rounds this process need not even hold.  The
 *                     witness record marks only its sender, so t
 *                     forgers reach at most t -- one short of the t+1
 *                     the adopt gate needs.
 *
 * THE THREE CARRIER GEOMETRIES (system.md "Relation to a deployment")
 * are the reason this file has three message classes.  Every carrier is
 * an instance of a layer-below primitive, so transport reliability is
 * INHERITED rather than re-implemented -- BPR comes free with the
 * instance, and every carrier retires on the primitive's own gates:
 *
 *   round instance  ACS at (n, t).  Agrees the composition on its value
 *                   plane, and carries each process's BANK -- what that
 *                   process commits to the round.
 *   exchange        a Bracha87 Fig 1 instance at (n, t), one per
 *                   (round, member), initiator = the member, value =
 *                   its bank.  This is the CONTENT carrier, and it is
 *                   born at the member's own close of the round.
 *   recovery        a Bracha87 Fig 1 instance at TWO processes and
 *                   t = 0 -- the primitive degenerated to an
 *                   acknowledged, retried pair channel.  This is a
 *                   SERVE act's discharge, and it is why a serve here
 *                   is a real Fig 1 leg and not a bare message: the leg
 *                   retires on the other end's accept, never on local
 *                   send.  It serves the composition always, and
 *                   REPLAYS the bank only to a process genuinely
 *                   behind -- see legEmit for where that line is drawn
 *                   and why it is the spec's line rather than a tuned
 *                   one.
 *
 * THE REPLAY NEEDS CARRIER EXPIRY TO FIRE, and the run reports whether
 * it did rather than being tuned until it does.  The reason is
 * structural and worth knowing before copying this file: an exchange
 * quiesces only when ALL n have accepted, so while any process still
 * lacks a bank its carrier is still retrying to exactly that process.
 * The exchange heals its own stragglers, so the replay fires only
 * where the exchange plane itself cannot deliver.  Two triggers exist
 * here: -L, which cuts the cut round's ACS and exchange wires both
 * (recovery legs excepted -- they ARE the heal), so that round's
 * content can reach the cut process by replay alone; and the two
 * deadlines below -- the key window's expiry (O4/O5, the inner) and
 * the byte-reuse guard (the outer) -- which runs crossing the wrap
 * exercise.  A run whose replay count is zero still says so.
 * The retention floor (O4, simulated below) rides BESIDE those
 * deadlines rather than among them: it bounds what can be MASKED, not
 * how long a carrier lives -- and because the release handler already
 * frees a round's legs at both ends, no carrier here ever outlives
 * the pruning that moves it, so its refusal arms stand as tripwires
 * behind those gates rather than as a third deadline.
 *
 * THE CADENCE -- how the two (n, t) geometries sequence.  The exchange
 * for a round FOLLOWS that round's agreement and runs alongside the
 * NEXT round's: a one-stage pipeline, exchange(R) beside round(R+1).
 * The order is not an optimization, it is the censorship resistance.
 * A member that released its content while its round was still being
 * agreed would let the cohort decide inclusion on what it had seen; the
 * round exists to take that decision blind, so nothing may leave until
 * the composition is fixed.  The two consequences worth naming:
 *
 *   exclusion costs participation, never the value.  An excluded member
 *   released nothing, so its bank rides the next round unspent and
 *   exchanges after THAT round closes.  Nothing is re-broadcast.
 *   the grains' lifetimes are independent.  A round is RELEASED on
 *   all-n possession of its composition -- a decision its own tails
 *   reach quickly -- while its content is still crossing.  So the
 *   exchanges survive their round's release (the ACS instance does
 *   not), and a sequence position waits on CONTENT rather than on
 *   retention.  Gating that wait on retention instead reports content
 *   that is merely in flight as an out-of-band hole, which is how the
 *   two grains come to be nominally separate and actually welded.
 *
 * THE CLOCK, and where its two hands are read.  The round instance's
 * COMPLETE is the TICK -- it fixes a composition and advances the
 * frontier.  The content landing is the TOCK: here that is every
 * in-subset member's bank, in a deployment that disperses it is t+1
 * shards, and either way it is one beat behind its tick.  Both hands
 * reach systemLaunch, and they reach it through DIFFERENT inputs, which
 * is the whole of the answer to "when does the next round start":
 *
 *   the tick   drives R4's advance -- systemDuty reads possession of
 *              the prior round's COMPOSITION, and nothing else.
 *   the tock   drives M2's capacity gate -- backlogDrained, which asks
 *              whether prior rounds' content has reached this process's
 *              decision stream.
 *
 * And the two gate different acts.  A JOIN is never held by the
 * backlog (system.h: "capacity may defer only chosen work"), so
 * PARTICIPATION never waits on the content plane and a process whose
 * content is slow is never pushed toward the fault budget.  Only an
 * ADMIT waits -- PRESENTATION is the chosen work.  So the tock pushes
 * back on how fast new values enter, never on how fast the sequence
 * advances, and the loop closes itself: a process that stops admitting
 * stops adding exchanges, its content drains, and it admits again.
 *
 * What is deliberately NOT a gate is any carrier's own state, and the
 * reason is stronger than bracha87.h's "best-effort under loss".
 * NOTHING AT ALL-n IS REACHABLE AT t >= 1.  Up to t processes may
 * never answer anything, so every all-n predicate is a t = 0 property
 * wearing general clothes.  Both of this layer's carrier retirements
 * are all-n -- the round instance at RELEASE (all n possess the
 * composition) and each exchange at all-n-accepted (acFrom == n) -- so
 * with one genuinely silent process neither can ever fire.  The run
 * reports the birthed/retired counts for exactly this reason, and the
 * trend is visible with NO silent process present at all: 8 of 8 at
 * n=2 t=0, 31 of 48 at n=4 t=1, 47 of 147 at n=7 t=2, 14 of 48 at 50%
 * loss.  All-n degrades the moment there is a t to spend.
 *
 * So ABANDONMENT IS THE STEADY STATE of carrier retirement at t >= 1,
 * not the exception, and the thing that actually bounds a carrier's
 * cost meanwhile is PER-PROCESS suppression (bracha87Fig1Skip): the
 * recipient set decays to the processes that have not answered, at
 * most t of them.  A carrier does not stop, it SHRINKS TO THE FAULTS
 * and stays there until the application's policy ends it.  Gating an
 * advance on carriers falling quiet would therefore not merely be
 * fragile -- it would deadlock at every t >= 1.
 *
 * Which settles the round-byte wrap, and settles it on the TAIL rather
 * than on the frontier.  The tempting guard -- do not reuse a byte
 * while a carrier still holds it -- is the same all-n mistake wearing
 * a different hat: that condition never clears at t >= 1, so it would
 * hold the first launch forever.  The frontier is not the thing to
 * stop.  Every advance to here honored t, so a process still dragging
 * the tail a whole round space later either IS one of the t or is
 * being counted as one now; the distance is the evidence.  So the
 * launch that reuses a byte proceeds unconditionally and ABANDONS
 * everything still outstanding under the byte's previous incarnation.
 * Serving stops; advancing does not.
 *
 * That is what gives carrier abandonment a DERIVED deadline instead of
 * a tuned one -- the byte's reuse is the last moment an obligation can
 * be honored, since past it the name is ambiguous -- and it is what
 * makes the fleet bounded by construction rather than by a cap: one
 * live incarnation per byte, at most.
 *
 * What makes the guard SOUND is the spec's three round names (system.md,
 * the Model), kept apart in this file: POSITION, the unbounded monotone
 * calculus R, keys every record and the agreed sequence; BYTE, the
 * wire/API unsigned char, ROUTES and only routes, sound within the
 * reach; IDENTITY, the chain anchor, PROVES.  At the byte's reuse
 * routing alone would read a wire from the previous incarnation as
 * frontier traffic -- a stalled process a whole round space behind
 * would inject its stale round into the live one wearing the same
 * byte.  So every wire asserts its round's PREVIOUS anchor and ingress
 * refuses a lineage that does not match its own chain at the mapped
 * position (struct wire, idAnchor); the run counts the refusals.
 *
 * The identity plane has a SECOND conjunct beside the anchor's, and the
 * spec names all three: an act is OF a round by its identity, FROM its
 * author by attribution, and AUTHORED WITHIN A FINITE BUDGET (O5).
 * The budget is the signature/offset chain, simulated here as a
 * per-process monotone counter: authoring consumes an offset, offsets
 * group into KEYS of -o budget signatures each (a one-time signature
 * scheme's 2^s per key), maintenance
 * goes due as a key nears exhaustion, and the rotation installs on the
 * maintenance round's own close.  Verification is a WINDOW -- current
 * or next key, read from the newest key witnessed per author -- and
 * the window is what gives carriers their INNER deadline: a carrier
 * whose signing key has fallen two behind can never verify anywhere
 * again (O4: the material to re-derive it is gone), so its holder
 * retires it, receiver and sender alike.  The two conjuncts are
 * independent by construction: a STALLED author's chain never
 * advances, so only the anchor refuses its traffic at the wrap, while
 * a LINGERING carrier's author has moved on, so the window refuses it
 * long before the byte would.  In a wrap run the window empties the
 * fleet first and the guard -- the OUTER, naming deadline -- reaps
 * what is left; the run reports both.
 *
 * Without crypto the same bytes ride all three carriers, and that
 * redundancy is deliberate: with crypto the bank would be validated
 * shards worth nothing until the exchange releases what completes them,
 * and recovery would replay exactly that release.  The topology is what
 * is being demonstrated, not the payload.
 *
 * SCOPE -- what this demonstration deliberately does NOT carry:
 *
 *   No crypto.  O1's chain fold is the demo mixer below, adequate for
 *   demonstration only, exactly as this repo's example coin is; O3's
 *   verification seam is a byte comparison; O5's signature chain is a
 *   bare monotone counter -- offsets, keys and windows with no key
 *   material under them, so an overdraft is COUNTED where a deployment
 *   would find signing physically impossible.  Ingress carries O5's
 *   offset floor in its reference three-outcome form at the authored
 *   high-water: a higher offset accepts and records its identity, the
 *   same offset re-presented under the same identity is a retry, and
 *   the same offset under a DIFFERENT identity is a detected
 *   re-authoring of a one-time slot, refused -- and, nothing here
 *   re-authoring, counted as a defect.  A9 (sender-authenticated
 *   ingress) is SIMULATED: every wire carries a pairwise-keyed tag
 *   (Psk, provisioned out of band with the genesis bundle -- A11),
 *   verified before anything else at ingress.  Nothing here forges, so
 *   the tag never refuses -- the coin's discipline: the SHAPE of the
 *   recipe with none of its strength, and a refusal is reported as the
 *   harness defect it would be.  The identity plane is the same demo
 *   grade: idAnchor folds with the demo mixer, so the wrap
 *   classification carries the SHAPE of O1's refusal and none of its
 *   strength.
 *   O4's forward-only derivation is the mixer once more, as a MASK
 *   chain: each process retains one pairwise key row AT the retention
 *   floor and derives every later round's step by folding the chain
 *   heads forward -- base plus recompute, never per-round snapshots.
 *   The floor follows the machine's pruning (wipe on prune, the
 *   bounded forward secrecy); each round's step is pinned at the
 *   close (the frozen step, what keeps in-flight content openable
 *   after the floor passes its round); an exchange INITIAL carries
 *   each destination's masked copy of the bank, and a recovery leg
 *   re-masks the banks it replays FROM THE FLOOR -- the serving form,
 *   which must reproduce the pinned exchange form byte for byte,
 *   the one comparison that tells a reproduction from a fresh
 *   masking.  The floor's refusal arms -- content unservable behind
 *   a server's floor (O2's out-of-band price), an arrival the
 *   receiving end can no longer open (loss) -- are TRIPWIRES here:
 *   the release gates already scope every serving carrier inside the
 *   floor, and the run report says so.
 *   Content across a RELAY is no longer fiat, twice over: the
 *   composition carries O2's per-member digest (fixed by the
 *   agreement's own value plane), and every bank travels with its
 *   member's ORIGIN TAG -- minted by the member, forwarded verbatim
 *   by relays, verified at every landing including pre-close, the
 *   demo's form of the member signature that makes a fragment
 *   self-validating.  Detect-don't-correct on both: a bad piece is
 *   refused on one look, never repaired, so a relaying server asserts
 *   nothing the receiver cannot check -- recovery is a REPLAY of the
 *   exchange, not a new trust relationship, and with nothing left on
 *   fiat the replay serves a bank on EVERY serve rather than behind a
 *   distance gate.
 *   The two-grain SIZE saving is visible in shape but not in bytes:
 *   the composition's digest region is what a deployment's value plane
 *   would shrink to, while this demo's A-Cast still carries the full
 *   bank -- one artifact serving as both the digest carrier and, once
 *   exchanged, the content.
 *   No threads, no transport, no wall clock.  The tick is a loop
 *   iteration.  A4's eventual delivery is supplied by the in-memory
 *   queue rather than inherited from BPR, and L1's "keeps taking
 *   steps" is likewise supplied by the loop.
 *
 * This process keeps its own content record -- it must, since the
 * agreed sequence outlives the rounds that carried it -- and ALSO feeds
 * systemAssembled so the machine's have grain stays current while a
 * round is retained.  That is the fully-fed shape; a caller serving
 * from its own record may decline to feed it and leave the grain at its
 * close-time value, and a caller with no record of its own must feed it
 * (system.h, systemAssembled).
 *
 * It is a GROUPING caller, and therefore never a switching one
 * (system.md, C6): served assertions are grouped by their LINK --
 * anchor and member count -- in this file's own book, and the machine
 * hears nothing until one link reaches t+1 distinct servers; the
 * membership and digests under that link are consumed from ONE
 * server (O3's two planes -- see legAccept).  Under t Byzantine any
 * t+1 group contains an honest server asserting the agreed link,
 * so at most one group can ever reach t+1 and a candidate switch
 * cannot arise -- systemWitnessReset stays uncalled here.  A caller
 * that cannot group must latch and re-arm on a switch instead; what
 * fails is first-latch WITHOUT the reset -- a Byzantine first server
 * latches a fabrication no honest assertion then byte-matches, and
 * adoption stalls forever (measured at the witness site below:
 * weakening the group test to one assertion strands the laggard at
 * zero adoptions while containment still reads clean).
 *
 * Build:
 *   (from project root) make example_system
 *
 * Usage:
 *   ./example_system [-v] [-s seed] [-l loss] [-L proc:round]
 *                    [-M proc] [-B proc:mode] [-m every] [-T Tp]
 *                    [-S sweeps] n t r msgs
 *
 * Examples -- the notable runs from building this file.  Add -v to any
 * of them to trace launches, closes and adoptions, and -s to move the
 * delivery order and loss draw:
 *
 *   ./example_system 4 1 3 3
 *     Baseline: every correct process prints the same agreed sequence,
 *     each staged value appearing in it exactly once.
 *
 *   ./example_system -v -m 2 4 1 3 3
 *     O5: a maintenance round rides "~maintenance" while the staged
 *     value stays staged and wins a later round -- MAINTAIN outranks
 *     ADMIT and never consumes it.
 *
 *   ./example_system -L 3:1 4 1 3 3
 *     R4 and the heal: process 3 is cut off from EVERY round-1 wire
 *     except recovery legs -- agreement and content planes both --
 *     the cohort advances under the bounded tolerance rather than
 *     stalling, and process 3 heals whole over real 2/0 Fig 1 legs:
 *     the composition by adoption (the link witnessed at t+1, the
 *     membership and digests consumed from ONE server -- the fold
 *     ground), and the content by the replay's re-masked banks riding
 *     the same legs (the serving form).  The run's "replayed by
 *     recovery" count is the content half actually landing.
 *
 *   ./example_system -L 3:2 4 1 3 1
 *     The vacuity guard: with one message each the cut round is never
 *     reached, and the heal verdict reports that instead of passing.
 *
 *   ./example_system -l 50 4 1 3 3
 *     Half of every inter-process wire dropped; BPR comes free with
 *     each of the three carriers and still carries the run to agreement
 *     with no content lost.  Every process self-classifies PARTITIONED
 *     on the way: at this loss rate progress is genuinely sparse enough
 *     to lapse the default budget, which is the posture working, not
 *     failing -- a classified process keeps stepping, and -T 300 buys
 *     enough budget to cross it.  That budgets are counted in SWEEPS is
 *     what makes them sensitive to how many carriers are in flight.
 *
 *   ./example_system 2 0 3 2
 *     n = 2, t = 0: the serve cap's floor of one IS the whole serving
 *     capacity here, and without it SERVE would retire by silence.
 *
 *   ./example_system 4 1 3 300
 *     THE WRAP: 300 staged values need 300-plus rounds (rotation
 *     rounds ride along), so the round byte genuinely recurs and every
 *     launch past position 255 fires the reuse guard.  The key window
 *     expires the lingering carriers first -- thousands per run, the
 *     inner deadline doing the work -- so the guard, the outer one,
 *     typically reaps nothing and says so; the recovery replay has the
 *     carrier expiry it needed to fire.  All verdicts hold across the
 *     crossing; the same is true at 7 2 3 300 and 2 0 3 300, and with
 *     -L or -B riding along.
 *
 *   ./example_system -o 16 4 1 3 300
 *     A budget too small for the tempo, kept honest: rotations churn,
 *     the margin fails (overdrafts reported), and carriers expire
 *     mid-delivery -- the lost content is priced as out-of-band holes
 *     (O2, per member), never as a jam.  The capacity gate's
 *     authorship-expiry arm is what keeps it a price rather than a
 *     deadlock: content whose author moved two keys on can never
 *     arrive, so it reads as reached.
 *
 *   ./example_system 7 2 4 3
 *     n = 7, t = 2: a subset of n-t = 5 legitimately leaves two
 *     contributions out of every round, so they ride the next one.
 *
 *   ./example_system -B 1:1 4 1 3 3
 *     Forged possession, and its containment: the liar's own forged bit
 *     completes the all-n record, the round releases, and the carrier
 *     the liar still needed dies.  It strands only itself -- the layer
 *     owes nothing to a process that says it is satisfied.  With the
 *     content plane wired the self-stranding has a visible price: the
 *     liar is a MEMBER of round 0 but never closes it, so it never
 *     births its exchange and its content never leaves.  Every correct
 *     process therefore holds the same composition (containment ok) and
 *     the same single <out-of-band> at that position.  A member can
 *     always withhold its own content; O2 prices that per member, and
 *     the round is unharmed -- which is the whole reason the two grains
 *     are separated in the first place.
 *
 *   ./example_system -B 1:2 4 1 3 3
 *     Forged want: soaks serve slots without ever displacing a correct
 *     wanting process, because grants rotate.
 *
 *   ./example_system -B 1:8 -L 5:1 7 2 4 3
 *     The fold ground earning its keep: a fabrication that does not
 *     chain to the asserted anchor is refused on one look, no book.
 *
 *   ./example_system -B 1:5 -L 5:1 7 2 4 3
 *     A re-folded fabrication instead, which the fold ground CANNOT
 *     refuse -- the t+1 grouping is what discriminates it.  Needs
 *     t >= 2: a Byzantine process and a partitioned one are two faults.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "systemStore.h"
#include "bkr94acs.h"

/*--------------------------------------------------------------------------*/
/*  The machine's round names -- the ordinal instantiation                  */
/*                                                                          */
/*  The machine takes rounds as opaque rs-byte NAMES under one caller       */
/*  comparator (system.h, the operations on a round).  This deployment's   */
/*  name is its resolved POSITION's ordinal bytes; ordCmp is the header's   */
/*  reference displacement comparator, and every close mints ordinal        */
/*  succession.  rn()/rv() convert at the machine boundary, so the rest     */
/*  of the file keeps speaking positions.                                   */
/*--------------------------------------------------------------------------*/

#define SYS_RS ((unsigned int)sizeof (unsigned long))

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

/*--------------------------------------------------------------------------*/
/*  Constants                                                               */
/*--------------------------------------------------------------------------*/

#define MAX_PROCESSES 8
/*
 * The machine's round space is the wrapping byte, and this file now runs
 * IN it rather than stopping short: a long enough run genuinely reuses
 * round bytes, which is what makes the tail-abandonment guard testable.
 * RSPACE is that byte space; the POSITION space (below) is unbounded.
 */
#define RSPACE      256
#define MAX_REACH    16   /* recovery-reach cap in rounds, NOT a round cap */
#define MAX_STAGED 10000  /* application messages staged per process */
#define VLEN         16   /* contributed value bytes (vLen encoding VLEN-1) */
#define MAX_PHASES    8

/*
 * The composition: the chain anchor (O1) followed by one membership
 * byte per process.  Byte-identical at every correct process, which is
 * what makes it comparable at a witness and foldable at an adopter.
 */
#define ANCHOR   4
/*
 * O2's per-member digest, in the COMPOSITION where the spec puts it
 * ("the agreed subset plus a per-member digest, fixed by agreement
 * itself"): the anchor, one membership byte per process, then one
 * DGLEN-byte digest of each member's bank -- derived at COMPLETE from
 * the value plane the agreement fixed.  Content validates against it
 * wherever it lands (recordBank, and the close for content that landed
 * early): detect-don't-correct, a bad piece is refused, never repaired.
 */
#define DGLEN    ANCHOR
#define COMPLEN  (ANCHOR + MAX_PROCESSES + MAX_PROCESSES * DGLEN)
#define COMPDG(c, m) ((c) + ANCHOR + MAX_PROCESSES + (m) * DGLEN)

#define LEGCAP      64
/*
 * The exchange fleet is bounded BY CONSTRUCTION, not by a cap: one slot
 * per (round byte, member), directly indexed, so at most one live
 * incarnation per byte ever exists -- the reuse guard reclaims the old
 * incarnation at the launch that reuses its byte.  A retired exchange
 * keeps its slot (with its key) until that reclaim so a late wire cannot
 * re-birth it.  The array is RSPACE * n entries on the heap.
 */
/*
 * The in-memory network's MEMORY LIMITS -- the only pacing there is
 * (the round space bounds the fleet; nothing else is owed a bound).
 * DRAIN is sized to what a tick can structurally push: every live
 * carrier's BPR fanout across all n processes.  A full queue drops,
 * and a drop is loss, which BPR carries -- but a DRAIN persistently
 * below the push rate starves the run outright, so it is sized above
 * the structural worst, not tuned to a measurement.
 */
#define HOLDCAP   2048
#define QCAP    262144
#define DRAIN    16384
#define MAX_TICKS 400000
#define IDLE_STOP   64

#define WK_ACS   0  /* traffic of a round's ACS instance */
#define WK_SERVE 1  /* traffic of a recovery leg (Fig 1 at 2 processes, t=0) */
#define WK_EXCH  2  /* traffic of an exchange (Fig 1 at (n, t)) */

#define NO_MSG 0xFFFFFFFFu /* rode: this round carried no application value */
#define NO_BANK 0xFF /* wire[].bankOf: no bank rides this wire */

/*--------------------------------------------------------------------------*/
/*  Wire                                                                    */
/*--------------------------------------------------------------------------*/

struct wire {
  /*
   * The O5 conjunct of the identity plane: the SIGNING OFFSET of the
   * authorship this wire rides under -- a per-process monotone counter
   * standing in for a budgeted one-time signature chain (SCOPE).
   * Identities are
   * the unbounded names (system.md, the Model), so an unbounded field
   * is licensed here where the round byte is not.  A retry re-stamps
   * the SAME offset (re-presentation is byte-identical at the same
   * offset; a re-signed duplicate would be detectably distinct), and
   * ingress verifies the offset against the current-or-next KEY window
   * -- see the key gate in deliverWire.
   */
  unsigned long sigOff;
  unsigned char kind;      /* WK_ACS / WK_SERVE */
  unsigned char sysRound;  /* the system round this traffic belongs to */
  unsigned char from;      /* authenticated sender (A9, by fiat) */
  unsigned char to;
  unsigned char possesses; /* possession indication riding this wire */
  unsigned char cls;       /* WK_ACS: BKR94ACS_CLS_ACAST / _BA */
  unsigned char type;      /* BRACHA87_INITIAL / ECHO / READY */
  unsigned char accepted;  /* BKR94ACS_ACCEPTED annotation */
  unsigned char process;   /* WK_ACS: whose A-Cast / which BA */
  unsigned char baRound;
  unsigned char initiator;
  unsigned char baValue;
  unsigned char legServer; /* WK_SERVE: the serving process */
  unsigned char legServed; /* WK_SERVE: the wanting process */
  /*
   * Whose bank rides in value[].  WK_EXCH: the exchange's own initiator,
   * so (sysRound, bankOf) IS the exchange's identity.  WK_SERVE: the
   * member whose bank this leg wire replays, or NO_BANK.  A bank never
   * rides WK_ACS traffic -- there it is still the A-Cast's own value,
   * which is the whole point of the split.
   */
  unsigned char bankOf;
  /*
   * The IDENTITY plane: the chain anchor of the round BELOW this wire's
   * round.  A byte ROUTES; an identity PROVES (system.md, the three
   * round names): sysRound is sound only within the reach, and at the
   * byte's reuse a wire from the previous incarnation reads as frontier
   * traffic on the byte alone.  Ingress therefore refuses any at-or-
   * behind wire whose asserted lineage does not match the receiver's
   * own chain at the mapped position.  Demo-grade like every anchor
   * here (SCOPE): the fold supplies the shape of the argument, none of
   * its strength -- a deployment keys traffic on real chain anchors and
   * gets this refusal from O1 itself.
   */
  unsigned char idAnchor[ANCHOR];
  /*
   * A9, simulated: the pairwise-keyed sender tag -- the demo's form of
   * the per-fragment authentication a deployment's transport supplies.
   * Computed over the whole wire (tag zeroed) under Psk[from][to] at
   * the one egress chokepoint, verified before anything at ingress.
   * Non-adversarial like the coin: nothing here forges a sender, so a
   * refusal is a harness defect and is reported as one; what the tag
   * carries is the SHAPE -- attribution is a keyed check, not a field
   * read on faith.
   */
  unsigned char tag[ANCHOR];
  unsigned char value[VLEN];
  /*
   * O4's serving form, simulated: the per-destination MASKED copy of a
   * bank.  On a WK_EXCH INITIAL, side[] is value[] under the pair's
   * mask key at this round's step and sideHave marks it (echo/ready
   * carry none: the sidecar rides INITIAL alone, per destination).  On
   * a WK_SERVE wire the replayed bank in value[] is ITSELF the masked
   * form -- a serve re-masks from the floor, and the masked bytes are
   * the wire's only content grain there, so the mask is load-bearing
   * on that path rather than decoration.  Nothing key-like rides the
   * wire: both ends derive the step from held state alone.
   */
  unsigned char side[VLEN];
  unsigned char sideHave;
  /*
   * O2's self-validation, carried WITH the bank wherever the bank
   * travels: the member's origin tag over the clear content, minted by
   * the member alone and FORWARDED VERBATIM by a relay -- the demo's
   * form of the member signature that makes a fragment self-validating
   * across a relay (a deployment relay cannot mint a signature, so
   * neither does this one).  Verified at recordBank on every landing,
   * pre-close included.
   */
  unsigned char bankTag[ANCHOR];
  unsigned char comp[COMPLEN];
};

/*--------------------------------------------------------------------------*/
/*  Recovery leg -- one Bracha87 Fig 1 at two processes, t = 0              */
/*                                                                          */
/*  Within the leg the server is index 0 (its designated initiator) and     */
/*  the served process is index 1.  The leg retires only when both ends     */
/*  have accepted -- remote evidence, never local send.                     */
/*--------------------------------------------------------------------------*/

struct leg {
  struct bracha87Fig1 *f1;
  unsigned long pos;       /* the served round's POSITION */
  unsigned long off;       /* the signing offset this leg's wires ride:
                            * the server's authored assertion (consumed),
                            * or the served end's chain head at birth
                            * (attribution only, nothing authored) */
  unsigned char inUse;
  unsigned char server;
  unsigned char served;
  unsigned char retired;
  unsigned char selfAcc;
  unsigned char otherAcc;
  unsigned char cursor;    /* which member's bank rides the next leg wire */
  unsigned char prevAnchor[ANCHOR]; /* identity of pos - 1, for the wires */
};

/*--------------------------------------------------------------------------*/
/*  Exchange -- one Bracha87 Fig 1 at (n, t) per (round, member)            */
/*                                                                          */
/*  The member is the instance's designated initiator and its bank is the   */
/*  broadcast value, so Lemma 2 does the work no crypto is here to do: an   */
/*  equivocating member cannot make two correct processes accept different  */
/*  content for one round.  Born at the member's OWN close of the round --  */
/*  never before, which is the whole of the censorship-resistance argument  */
/*  -- and freed at all-accepted quiescence, never at release: content may   */
/*  assemble past the composition it belongs to (O2).  That quiescence is    */
/*  a t = 0 certainty and no more than a hope above it (see the header's     */
/*  clock section): at t >= 1 the terminal state is the application's        */
/*  abandonment, and until then per-process suppression shrinks the          */
/*  carrier to the processes that have not answered.                         */
/*--------------------------------------------------------------------------*/

struct exch {
  struct bracha87Fig1 *f1;
  unsigned long pos;       /* the carried round's POSITION */
  unsigned long off;       /* the signing offset this instance's wires
                            * ride: for the member's own exchange the
                            * SAME offset its round contribution consumed
                            * -- the bank re-presented byte-identically
                            * on a second carrier (O5) -- else this
                            * process's chain head at local birth */
  unsigned long artOff;    /* the ARTIFACT's own offset (the member's),
                            * learned from any wire the member itself
                            * sent; what authorship expiry reads */
  unsigned char artKnown;
  unsigned char inUse;
  unsigned char member;    /* the bank's author, and this instance's initiator */
  unsigned char retired;
  unsigned char selfAcc;
  unsigned char sideHave;  /* side[] holds this process's masked sidecar */
  unsigned char prevAnchor[ANCHOR]; /* identity of pos - 1, for the wires */
  unsigned char side[VLEN]; /* the per-destination masked bank copy (O4),
                             * stored at INITIAL ingress, opened at ACCEPT */
  unsigned char stag[ANCHOR]; /* the member's origin tag riding side[] */
};

/*--------------------------------------------------------------------------*/
/*  Round slot -- one round's whole record, keyed by POSITION               */
/*                                                                          */
/*  THE THREE ROUND NAMES (system.md, the Model): POSITION is the calculus  */
/*  R -- unbounded, monotone, what these slots and the agreed sequence are  */
/*  keyed by.  BYTE is the wire/API unsigned char -- ROUTING ONLY, sound    */
/*  within the reach, and it is the slot INDEX (pos & 0xFF), so at most     */
/*  one incarnation of a byte is ever live.  IDENTITY is the chain anchor   */
/*  -- what PROVES, carried on every wire and checked at ingress.  The      */
/*  launch that reuses a byte reclaims the previous incarnation's slot      */
/*  and ABANDONS everything still outstanding under it (the guard); the     */
/*  byte's reuse is the last moment an obligation under the old name can    */
/*  be honored, so the deadline is DERIVED, never tuned.                    */
/*--------------------------------------------------------------------------*/

struct rslot {
  struct bkr94acs *acs;
  unsigned long pos;
  unsigned long off;                  /* the offset authoring this round's
                                       * contribution consumed (O5) */
  unsigned long srvOff;               /* the offset authoring this round's
                                       * SERVE assertion consumed -- once,
                                       * at the first leg; every later leg
                                       * re-presents the same assertion at
                                       * the same offset (O5) */
  struct bracha87Retry cur;
  unsigned int rode;                  /* staged msg this round carried */
  unsigned char inUse;
  unsigned char closed;
  unsigned char srvSet;
  unsigned char maint;                /* the contribution was the
                                       * maintenance form: this round's
                                       * close installs the rotation */
  unsigned char comp[COMPLEN];
  unsigned char prevAnchor[ANCHOR];   /* anchor of pos - 1 (Genesis at 0) */
  /*
   * The pairwise mask row at THIS round's step, pinned at the close --
   * the returner's frozen step: content still crossing when the floor
   * later passes this round stays openable HERE, because the receiving
   * side's key was frozen when its assembly began, while nothing FRESH
   * can be masked for the round any more (maskWalk refuses behind the
   * floor).  That split is exactly O4's bounded forward secrecy.
   */
  unsigned char exchKey[MAX_PROCESSES][ANCHOR];
  unsigned char bank[VLEN];           /* what THIS process banked here */
  unsigned char told[MAX_PROCESSES];
  unsigned char has[MAX_PROCESSES];
  unsigned char content[MAX_PROCESSES][VLEN];
  unsigned char ctag[MAX_PROCESSES][ANCHOR]; /* each bank's origin tag,
                                              * kept to FORWARD on serves */
};

/*--------------------------------------------------------------------------*/
/*  One process's seat                                                      */
/*--------------------------------------------------------------------------*/

struct proc {
  struct system *sys;
  struct systemStore *store;          /* the rounds this process RETAINS and
                                       * the records the machine keeps in
                                       * them: caller storage, reached through
                                       * the four retention operations, sized
                                       * to the declared reach */
  struct rslot *slot;                 /* RSPACE slots, index = pos & 0xFF */
  struct exch *exch;                  /* RSPACE * n, index = byte * n + m */
  struct wire *hold;                  /* beyond-reach traffic, re-fed later */
  unsigned char (*msg)[VLEN];         /* staged application values */
  unsigned char (*seq)[VLEN];         /* the agreed sequence, grown on demand */
  unsigned char *seqHole;
  unsigned long fpos;                 /* POSITION frontier; its byte is
                                       * always systemFrontier */
  unsigned long seqNext;              /* next position to emit, in order */
  unsigned long pendPos;              /* the position pendF[] speaks for */
  unsigned long candPos;
  /*
   * The signature/offset chain (O5), demo grade: sigOff is the next
   * offset to consume -- monotone, never rewound -- and rotKey is the
   * newest KEY this process has installed by winning a maintenance
   * round.  Key index = offset / SigBudget; consuming past the
   * current-or-next window's reach -- two keys beyond the last
   * install -- is an OVERDRAFT (counted, never blocked: SCOPE
   * concedes the crypto that would make it impossible).  hiOff
   * is the receiver half: the highest offset verified per author,
   * from which the current-or-next window is read.
   */
  unsigned long sigOff;
  unsigned long rotKey;
  unsigned long hiOff[MAX_PROCESSES];
  /*
   * The offset floor's anchor record (O5, the three-outcome gate): the
   * highest AUTHORED offset witnessed per author, with the identity
   * that offset asserted.  A second artifact at the same offset with a
   * different identity is a detected re-authoring -- a one-time
   * scheme's slots are spent by signing, so two anchors at one offset
   * would leak the slot -- and
   * is refused.  Only the high-water is recorded, exactly the bound
   * the reference design keeps; anything below it faces the key
   * window alone.  Authored offsets only: a carrier relaying another
   * author's artifact stamps its own chain head without consuming,
   * and a stamp is attribution, not authorship.
   */
  unsigned long authOff[MAX_PROCESSES];
  unsigned char authSeen[MAX_PROCESSES];
  unsigned char authAnchor[MAX_PROCESSES][ANCHOR];
  /*
   * O4's forward-only mask derivation, demo grade: ONE pairwise key row
   * retained at the retention floor -- maskKey[j] is the key masking
   * round maskFloor's content between this process and j, and every
   * later round's key is derived by folding the retained chain heads
   * forward from it (maskWalk).  Base plus recompute, never per-round
   * snapshots.  The floor advances over rounds the machine has pruned
   * (maskAdvance), folding each departed round's anchor into the row
   * and thereby DESTROYING the step it leaves behind -- wipe on prune
   * is the bounded forward secrecy, and it is what gives a serve its
   * floor: content behind it can no longer be masked for anyone.
   */
  unsigned long maskFloor;
  unsigned char maskKey[MAX_PROCESSES][ANCHOR];
  struct leg leg[LEGCAP];
  unsigned int holdCnt;
  unsigned int tolCount;              /* T_p: own sweeps under TOLERANCE */
  unsigned int barren;                /* S: own sweeps without progress */
  unsigned int adopts;
  unsigned int seqCnt;
  unsigned int seqCap;
  unsigned int maxRetained;
  unsigned int msgHead;               /* PRESENT: the staged value */
  unsigned int msgCnt;
  unsigned int retryCursor;
  unsigned char self;
  unsigned char serveCursor[SYS_RS + 1]; /* systemCursorSz(SYS_RS): the
                                          * round NAME last served, plus its
                                          * in-use byte */
  unsigned char adoptPending;
  unsigned char candValid;
  unsigned char tolElapsed;
  unsigned char active;               /* progress observed this tick */
  unsigned char partitioned;
  /*
   * Possession indications for the frontier position, held until its
   * close (the arrived-indication obligation, system.h systemReceived).
   * Keyed by POSITION, not byte: pendPos names the position these speak
   * for, so a stale entry structurally cannot resurface at the next
   * incarnation of the byte.
   */
  unsigned char pendF[MAX_PROCESSES];
  unsigned char cand[COMPLEN];
  /*
   * O3's verification seam, caller-side: assertions grouped by their
   * BYTES before the machine is told anything.  See legAccept.
   */
  unsigned char candCnt;
  unsigned char candSrv[MAX_PROCESSES][(MAX_PROCESSES + 7) / 8];
  /*
   * The book's grouping key is the LINK, not the whole candidate --
   * O3's two witness planes realized: LINK = [chain_R | member count]
   * (the chain_{R-1} half is discharged at ingress, where every leg
   * wire's asserted lineage already matched our own chain), witnessed
   * at t+1 distinct servers; the MESSAGE plane -- membership and
   * per-member digests -- is stored from the FIRST server whose bytes
   * fold to their link and consumed from that ONE server at adoption.
   */
  unsigned char candLink[MAX_PROCESSES][ANCHOR + 1];
  unsigned char candBuf[MAX_PROCESSES][COMPLEN];
};

/*--------------------------------------------------------------------------*/
/*  Globals                                                                 */
/*--------------------------------------------------------------------------*/

static struct proc Proc[MAX_PROCESSES];
static struct wire *Queue;
static unsigned int Qcount;
static unsigned int Rng;
static unsigned int N;
static unsigned int T;
static unsigned int R;          /* the recovery reach, in rounds */
static unsigned int Verbose;
static unsigned int DropPct;
static unsigned int Tp;
static unsigned int Sp;
static unsigned int MaintEvery;
static int LagProc = -1;
static int LagRound = -1;
static int MuteProc = -1;
static int ByzProc = -1;
static unsigned int ByzMode;
static unsigned int ByzAsserts;   /* fabrications actually served */
/*
 * Which carrier each delivered bank arrived on.  Reported so the two
 * content routes are visible rather than assumed: a run whose recovery
 * count is zero never exercised the replay, and saying so is the
 * difference between a demonstration and a claim.
 */
static unsigned int ByExchange;
static unsigned int ByRecovery;
/*
 * The serving form (O4), measured.  ByPlane counts banks recorded from
 * the exchange's clear value plane because the masked sidecar never
 * arrived (a process can echo its way into an exchange without ever
 * seeing the INITIAL that carries its copy) -- the demo's stand-in for
 * reconstructing from other recipients' shards, and the count keeps
 * the fallback honest rather than silent.  ServeMasked counts banks
 * re-masked from the floor onto recovery legs; ServeFloorHole counts
 * serves refused because their round fell behind the server's floor
 * (the wipe working -- content past it is unservable, priced per
 * member as O2's out-of-band hole); MaskFloorDrop counts arrivals this
 * end could no longer open (its own floor passed the round before its
 * frozen step existed) -- read as loss, which BPR carries.
 *
 * HONESTY: the two refusal arms are structurally DORMANT in this glue
 * and the report says so per run.  The release handler frees a round's
 * legs at both ends (the zombie-mirror gate), so no leg outlives the
 * pruning that moves the floor, and a slot's pinned row outlives the
 * floor by the whole byte space -- the arms can fire only if one of
 * those gates regresses, which is exactly what a tripwire is for.
 * What IS reachable is the split they defend: fresh masking bounded by
 * the floor (every serve recomputes from it and must reproduce the
 * pinned form -- MaskMismatch is the validated check) and opening
 * bounded by the frozen step (in-flight content stays openable after
 * its round is pruned).
 */
static unsigned int ByPlane;
static unsigned int ServeMasked;
static unsigned int ServeFloorHole;
static unsigned int MaskFloorDrop;
/*
 * Sweeps on which M2's capacity gate withheld an admission -- the tock
 * pushing back on the tick.  Reported because a gate that never fires
 * is a claim and not a demonstration.
 */
static unsigned int BacklogHolds;
/*
 * The wrap, measured.  GuardFires counts launches that reused a round
 * byte (each reclaims the previous incarnation's slot); Abandoned*
 * count the carriers still outstanding under the old name at that
 * moment -- the derived deadline actually cutting something off, not
 * just passing.  IdRejected counts at-or-behind wires refused because
 * their asserted lineage did not match the receiver's chain at the
 * mapped position: the byte routed them to a live name, the identity
 * refused them.  All reported; a run that never wraps says so.
 */
static unsigned int GuardFires;
static unsigned int AbandonedExch;
static unsigned int AbandonedLeg;
static unsigned int IdRejected;
/*
 * Exchange lifecycle, measured (the header's clock section): birthed is
 * every local instance across every process, retired is the all-accepted
 * quiescence -- an all-n fact, so the gap between the two IS the tail
 * the byte-reuse guard exists to reap.
 */
static unsigned int ExchBirthed;
static unsigned int ExchRetired;
/*
 * The signature/offset chain, measured.  SigBudget is the offsets one
 * key signs (a one-time scheme's 2^s); Rotations counts
 * maintenance-round wins that
 * installed a next key; Overdrafts counts key boundaries crossed TWO
 * keys past the last install -- past the current-or-next window's
 * reach, where a deployment could not sign at all (must be zero at a
 * sane budget).  KeyRejected counts wires refused at
 * ingress for riding a key two or more behind the author's newest
 * witnessed key; KeyExpired* count carriers this process itself
 * retired because their signing key fell two behind its own head --
 * O4's forward-only derivation seen from the sender: the material to
 * re-derive that signature is gone, so the retry can never verify
 * anywhere again.  This is the INNER deadline, and it is what actually
 * empties the lingering fleet; the byte-reuse guard remains the OUTER
 * (naming) deadline and reaps what is left.
 */
static unsigned long SigBudget = 64;
static unsigned long SigMargin;
static unsigned int Rotations;
static unsigned int Overdrafts;   /* boundary crossings TWO keys past the
                                   * last install -- past what
                                   * current-or-next could ever verify */
static unsigned int KeyRejected;
static unsigned int KeyExpiredExch;
static unsigned int KeyExpiredLeg;
static unsigned int CandConflicts; /* times a correct process's book held
                                    * two distinct compositions at once */
static unsigned int CandRejected;  /* assertions the fold ground refused
                                    * on their own, no book consulted */

/*
 * Byzantine behaviors (-B proc:mode).  Each attacks one of the system
 * layer's OWN containment claims; equivocating an A-Cast is Bracha
 * Lemma 2's business and is demonstrated by example/bracha87Fig1.c.
 */
#define BYZ_POSSESS 1  /* claim possession of every round, held or not */
#define BYZ_WANT    2  /* never claim possession, so every round reads
                        * as wanted -- soaks serve slots */
#define BYZ_CAND    4  /* serve a fabricated composition on recovery legs,
                        * re-folded so it is internally consistent */
#define BYZ_LINK    8  /* fabricate WITHOUT re-folding: the membership no
                        * longer chains to the asserted anchor, which is
                        * what O1's linkage check catches on one look */
static unsigned long AcsSz;
static unsigned long LegSz;
static unsigned long ExchSz;

/* O1's chain base (A11: the genesis bundle).  Demo bytes. */
static unsigned char Genesis[] = {0x5b, 0xa5, 0x11, 0x00};

/*
 * A9's pairwise keys, provisioned out of band alongside the genesis
 * bundle (A11: the same delivery that seeds the chain base seeds the
 * pair keys).  Symmetric demo bytes.
 */
static unsigned char Psk[MAX_PROCESSES][MAX_PROCESSES][ANCHOR];
/*
 * Each member's PUBLIC origin key (O2's self-validating fragments):
 * the verification key for the member's origin tag over its bank --
 * the demo's form of a signature's public half, so it is derivable by
 * everyone from the genesis bundle where a real deployment ships its
 * signature pubkeys in it (A11).  Minting under it is likewise open
 * here; that
 * is the SHAPE-not-strength concession every keyed fold in this file
 * makes, and nothing forges.
 */
static unsigned char BankKey[MAX_PROCESSES][ANCHOR];
/*
 * Refusals that must stay ZERO in this non-adversarial demo: a sender
 * tag that fails its pairwise check, or content that fails its agreed
 * digest.  Either nonzero is a defect in the harness, not a defense
 * working, and the report says so -- the fold-ground soundness line's
 * precedent.
 */
static unsigned int TagRejected;
static unsigned int DigestRejected;
/*
 * Same discipline for the serving form: a serve's re-mask from the
 * floor must reproduce the pinned exchange-step form byte for byte --
 * the reference design's near-miss is a re-mask anchored one step off,
 * which still PASSES between consenting ends while silently being a
 * fresh masking rather than a reproduction.  Only this comparison
 * catches it, so a nonzero count is a harness defect.
 */
static unsigned int MaskMismatch;
/*
 * Two more defect-class refusals (nothing here forges or re-authors):
 * an authored offset re-presented under a DIFFERENT identity (O5's
 * three-outcome floor -- a detected re-authoring), and a bank whose
 * origin tag does not verify (O2's self-validation).  LinkOnly is
 * informational, not a refusal: servers whose assertion joined a
 * link group AFTER its candidate was stored -- direct evidence the
 * message plane closed from ONE server while the link accrued its
 * t+1 (the fold ground's acceptance half, exercised).
 */
static unsigned int ReAuthorRejected;
static unsigned int OriginRejected;
static unsigned int LinkOnly;

/* The deployment's maintenance form (O5) and its empty-join form. */
static unsigned char Maint[] = "~maintenance";
static unsigned char Empty[] = "~empty";

/*--------------------------------------------------------------------------*/
/*  Deterministic scheduling                                                */
/*--------------------------------------------------------------------------*/

static unsigned int
rngNext(
  void
){
  Rng = Rng * 1103515245u + 12345u;
  return ((Rng >> 16) & 0x7FFFu);
}

/*
 * The coin -- deterministic alternating, adequate for demonstration
 * only, exactly as example/bkr94acs.c's.
 */
static unsigned char
demoCoin(
  void *closure
 ,unsigned char phase
){
  (void)closure;
  return (phase % 2);
}

/*
 * O1's chain fold, demo grade: anchor_R = mix(anchor_{R-1}, membership).
 * A10 asks for collision resistance so that a candidate folding to a
 * verified anchor IS the agreed result; this mixer supplies the SHAPE
 * of that argument and none of its strength.  A deployment folds with
 * a real hash.
 */
static void
foldAnchor(
  unsigned char *out            /* ANCHOR bytes */
 ,const unsigned char *prev     /* ANCHOR bytes */
 ,const unsigned char *mem      /* the membership AND the per-member
                                 * digests (COMPLEN - ANCHOR bytes) --
                                 * the fold covers member digests, which
                                 * is what O1's linkage always assumed */
){
  unsigned int i;
  unsigned int acc;

  acc = 0;
  for (i = 0; i < ANCHOR; ++i)
    acc = acc * 131u + prev[i];
  for (i = 0; i < COMPLEN - ANCHOR; ++i)
    acc = acc * 131u + mem[i];
  for (i = 0; i < ANCHOR; ++i) {
    out[i] = (unsigned char)(acc & 0xFF);
    acc >>= 5;
    acc = acc * 31u + 7u;
  }
}

/*
 * The demo mixer again, keyed: A9's pairwise tag and O2's per-member
 * digest are both this fold -- the shape of a MAC and of a digest,
 * none of the strength (SCOPE), exactly as foldAnchor supplies the
 * shape of O1's binding.
 */
static void
mixTag(
  unsigned char *out            /* ANCHOR bytes */
 ,const unsigned char *key      /* ANCHOR bytes */
 ,const unsigned char *b
 ,unsigned int len
){
  unsigned int i;
  unsigned int acc;

  acc = 0;
  for (i = 0; i < ANCHOR; ++i)
    acc = acc * 131u + key[i];
  for (i = 0; i < len; ++i)
    acc = acc * 131u + b[i];
  for (i = 0; i < ANCHOR; ++i) {
    out[i] = (unsigned char)(acc & 0xFF);
    acc >>= 5;
    acc = acc * 31u + 7u;
  }
}

/*
 * XOR a bank under the keystream the demo mixer expands from an
 * ANCHOR-sized mask key -- the shape of O4's masking with none of its
 * strength, like every keyed fold here.  XOR is its own inverse, so
 * one routine masks and opens.
 */
static void
maskBytes(
  unsigned char *out            /* VLEN bytes */
 ,const unsigned char *in       /* VLEN bytes */
 ,const unsigned char *key      /* ANCHOR bytes */
){
  unsigned char blk[ANCHOR];
  unsigned int i;

  mixTag(blk, key, key, ANCHOR);
  for (i = 0; i < VLEN; ++i) {
    if (i && !(i % ANCHOR))
      mixTag(blk, key, blk, ANCHOR);
    out[i] = (unsigned char)(in[i] ^ blk[i % ANCHOR]);
  }
}

/*
 * Wrapping distance from 'base' to 'round' in the unsigned char round
 * space: 0 is 'base' itself, 1..127 ahead of it, 128..255 behind it.
 * Chain reach (system.h) is at-or-behind the frontier.
 */
static unsigned char
aheadBy(
  unsigned char round
 ,unsigned char base
){
  return ((unsigned char)(round - base));
}

/*
 * BYTE -> POSITION, the routing half of the split: map a wire's round
 * byte into the receiver's own position space around its frontier --
 * 0..127 ahead of it, 1..128 behind it.  Sound within the reach and
 * ONLY there: at the byte's reuse a wire from the previous incarnation
 * maps to the live position, which is exactly what the identity check
 * at ingress exists to refuse.  Returns -1 for a byte that maps before
 * genesis -- no honest wire does.
 */
static long
posOf(
  const struct proc *p
 ,unsigned char round
){
  unsigned char ab;

  ab = aheadBy(round, (unsigned char)(p->fpos % RSPACE));
  if (ab < 128)
    return ((long)(p->fpos + ab));
  if (p->fpos < (unsigned long)(RSPACE - ab))
    return (-1);
  return ((long)(p->fpos - (RSPACE - ab)));
}

/*
 * Consume one signing offset (O5): authoring an authenticated value --
 * a round contribution at launch, a server's leg assertion at birth --
 * spends it.  Nothing else does: retries re-present at the offset
 * already spent, and a carrier merely relaying another author's
 * artifact stamps its own chain head without consuming.  Crossing
 * beyond the current-or-next window's reach is an overdraft, counted
 * rather than blocked (SCOPE: no real keys here to run out of).
 */
static unsigned long
sigConsume(
  struct proc *p
){
  /*
   * The overdraft line sits at TWO keys past the last install, not
   * one: verification is current-or-next, so signing into the next
   * key while its rotation round is still winning is the allowance
   * working -- exclusion can cost a rotation several retries at
   * t >= 1, and the spill absorbs the streak.  Only crossing into a
   * key the window could never cover is the failure.
   */
  if (p->sigOff && !(p->sigOff % SigBudget)
   && p->sigOff / SigBudget > p->rotKey + 1)
    ++Overdrafts;
  return (p->sigOff++);
}

/* The slot holding 'pos', or 0 if that incarnation is gone or unborn. */
static struct rslot *
slotOf(
  struct proc *p
 ,unsigned long pos
){
  struct rslot *s;

  s = p->slot + pos % RSPACE;
  if (s->inUse && s->pos == pos)
    return (s);
  return (0);
}

/*
 * IDENTITY at 'pos': the chain anchor of the position below it, which
 * is what every wire of 'pos' asserts as its lineage.  0 when this
 * process cannot know it -- and for any position at or behind the
 * frontier it always can: positions below the frontier are closed, and
 * their slots survive until their byte's reuse, which is strictly
 * later than any at-or-behind mapping can reach.
 */
static const unsigned char *
prevAnchorAt(
  struct proc *p
 ,unsigned long pos
){
  struct rslot *s;

  if (!pos)
    return (Genesis);
  if ((s = slotOf(p, pos - 1)) && s->closed)
    return (s->comp);
  return (0);
}

/*
 * The pair (self, other) mask key at round 'pos' -- O4's forward-only
 * derivation: the floor row folded forward over the chain heads of the
 * rounds between the floor and 'pos'.  THE STEP IS KEYED ON THE LINEAGE
 * THE WIRE ITSELF ASSERTS: round pos's key folds anchors through
 * pos - 1 and NOT pos's own -- the same off-by-one the reference design
 * pins deliberately.  Anchoring one step later would also agree between
 * the two ends of an exchange (which is why nothing but the pinned-form
 * comparison would catch it) but it would strand the pre-close heal: a
 * process being served round pos has not closed it, so pos's own anchor
 * is exactly what it cannot yet hold.  Fails behind the floor -- the
 * material is destroyed (bounded forward secrecy) -- and the caller
 * prices the refusal, never works around it.
 */
static int
maskWalk(
  struct proc *p
 ,unsigned long pos
 ,unsigned char other
 ,unsigned char *key            /* ANCHOR out */
){
  struct rslot *s;
  unsigned long q;

  if (pos < p->maskFloor)
    return (0);
  memcpy(key, p->maskKey[other], ANCHOR);
  for (q = p->maskFloor; q < pos; ++q) {
    if (!(s = slotOf(p, q)) || !s->closed)
      return (0);
    mixTag(key, key, s->comp, ANCHOR);
  }
  return (1);
}

/*
 * The key that opens round 'pos' content from 'other' at this end:
 * the FROZEN step pinned at this process's own close of the round
 * when there is one -- assembly begun, the key survives the floor --
 * else the live walk (the pre-close heal: a wanter being served its
 * frontier round can walk to it, since the walk needs only anchors
 * BELOW the round).
 */
static int
maskKeyAt(
  struct proc *p
 ,unsigned long pos
 ,unsigned char other
 ,unsigned char *key            /* ANCHOR out */
){
  struct rslot *s;

  if ((s = slotOf(p, pos)) && s->closed) {
    memcpy(key, s->exchKey[other], ANCHOR);
    return (1);
  }
  return (maskWalk(p, pos, other, key));
}

/*
 * O4's floor coupling: advance the retained row over every round the
 * machine has pruned, in round order, folding each departed round's
 * anchor into the row -- the fold overwrites the old base, which is
 * the demo's wipe.  The walk stops at the first round still retained
 * (its serving form is still owed) and never crosses the frontier.
 * The lag between a prune and this advance is bounded by the REACH
 * itself -- eviction is oldest-first, so no released round waits more
 * than the reach spans -- while slots outlive the reach by the whole
 * byte space, so the anchors the fold needs are always still held.
 */
static void
maskAdvance(
  struct proc *p
){
  struct rslot *s;
  unsigned int j;

  while (p->maskFloor < p->fpos
   && !systemRetained(p->sys, rn(p->maskFloor))
   && (s = slotOf(p, p->maskFloor)) && s->closed) {
    for (j = 0; j < N; ++j)
      mixTag(p->maskKey[j], p->maskKey[j], s->comp, ANCHOR);
    ++p->maskFloor;
  }
}

/*--------------------------------------------------------------------------*/
/*  Queue                                                                   */
/*--------------------------------------------------------------------------*/

static void
qPush(
  const struct wire *w
){
  if (Qcount >= QCAP)
    return;
  Queue[Qcount++] = *w;
}

/* Pop a uniformly random pending wire (swap-with-last). */
static int
qPopRandom(
  struct wire *w
){
  unsigned int i;

  if (!Qcount)
    return (0);
  i = rngNext() % Qcount;
  *w = Queue[i];
  Queue[i] = Queue[--Qcount];
  return (1);
}

/*
 * Push onto the network.  Self-addressed traffic is not a network path
 * and is never dropped; everything else draws against the loss rate,
 * then against the laggard cut.
 */
static void
pushWire(
  const struct wire *w
){
  if (w->to != w->from) {
    if (DropPct && (rngNext() % 100u) < DropPct)
      return;
    /*
     * The cut is a POSITION, not a byte -- a byte cut would re-cut the
     * next incarnation.  Harness-side inference from the sender's own
     * frontier is sound here because every carrier lives well inside
     * the window (the two key deadlines retire it long before the
     * half-space).  The cut spans the agreement AND content planes --
     * ACS and exchange wires both -- so the cut round's content can
     * only ever reach the cut process by recovery replay, which is
     * exactly what the heal must then demonstrate.  Recovery legs are
     * NOT cut: they are the heal itself, and cutting them turns the
     * scenario into a permanent one-round partition no protocol can
     * heal -- a strand demonstration, not a heal demonstration.
     */
    if (w->kind != WK_SERVE
     && LagProc >= 0
     && (int)w->to == LagProc
     && posOf(&Proc[w->from], w->sysRound) == (long)LagRound)
      return;
    /*
     * The mute: process M's outbound is never delivered -- every round,
     * every kind, recovery legs included.  Unlike -L this is a FAULT
     * demonstration, not a heal demonstration: the muted process is the
     * permanently silent process the fault budget t exists to price, so
     * nothing it sends is spared.  It still hears everything (its
     * inbound is a network path like any other), and its self-addressed
     * traffic is above this gate with the rest.
     */
    if (MuteProc >= 0 && (int)w->from == MuteProc)
      return;
  }
  /*
   * A9 at egress: one chokepoint, every wire tagged under the pair key
   * for (from, to) with the tag field zeroed for the computation.
   */
  {
    struct wire t;

    t = *w;
    memset(t.tag, 0, sizeof (t.tag));
    mixTag(t.tag, Psk[t.from][t.to], (const unsigned char *)&t,
           sizeof (t));
    qPush(&t);
  }
}

static void
holdWire(
  struct proc *p
 ,const struct wire *w
){
  if (p->holdCnt >= HOLDCAP)
    return;
  p->hold[p->holdCnt++] = *w;
}

/*--------------------------------------------------------------------------*/
/*  Forward declarations -- the glue is mutually recursive: a received      */
/*  wire produces machine acts, and applying a machine act (DELIVER,        */
/*  SERVE, a launch) produces more wires.                                   */
/*--------------------------------------------------------------------------*/

static void applySysActs(struct proc *, struct systemAct *, unsigned int,
                         const struct wire *);
static void sysClose(struct proc *, struct rslot *, const unsigned char *);
static void sysTryComplete(struct proc *, struct rslot *);
static void deliverWire(const struct wire *);
static unsigned int recordBank(struct proc *, unsigned long, unsigned char,
                               const unsigned char *, const unsigned char *);
static void seqEmit(struct proc *, unsigned long, unsigned int);

/*--------------------------------------------------------------------------*/
/*  ACS egress                                                              */
/*                                                                          */
/*  Every wire of round R carries the possession indication for R           */
/*  (system.h systemReceived, 'possesses').  THIS IS LOAD-BEARING and is    */
/*  not obvious from the header: the indication rides a CLOSED round's own  */
/*  tails.  Without it, a set of processes that all close R and wait learn  */
/*  nothing about each other, the advance gate never opens, and the run     */
/*  deadlocks rather than merely slowing.                                   */
/*--------------------------------------------------------------------------*/

static void
emitAcs(
  struct proc *p
 ,struct rslot *s
 ,struct bkr94acsAct *acts
 ,unsigned int nacts
){
  struct wire w;
  unsigned char round;
  unsigned int i;
  unsigned int j;

  round = (unsigned char)(s->pos % RSPACE);
  for (i = 0; i < nacts; ++i) {
    switch (acts[i].act) {

    case BKR94ACS_ACT_ACAST_SEND:
    case BKR94ACS_ACT_BA_SEND:
      if (acts[i].act == BKR94ACS_ACT_ACAST_SEND && !acts[i].value)
        break;
      memset(&w, 0, sizeof (w));
      w.kind = WK_ACS;
      w.sysRound = round;
      w.from = p->self;
      w.sigOff = s->off;
      memcpy(w.idAnchor, s->prevAnchor, ANCHOR);
      w.possesses = s->closed;
      /*
       * The two possession lies.  Both are contained by the machine
       * marking only the AUTHENTICATED SENDER: a forged claim retires
       * only the serves owed to the forger, and release still needs all
       * n bits, so with at most t liars every correct process's TRUE
       * indication is still required.  A forged WANT is the mirror --
       * it can soak a serve slot but cannot displace a correct wanting
       * process, because the cap rotates.
       */
      if ((int)p->self == ByzProc) {
        if (ByzMode & BYZ_POSSESS)
          w.possesses = 1;
        else if (ByzMode & BYZ_WANT)
          w.possesses = 0;
      }
      w.type = acts[i].type;
      w.accepted = acts[i].accepted;
      w.process = acts[i].process;
      if (acts[i].act == BKR94ACS_ACT_ACAST_SEND) {
        w.cls = BKR94ACS_CLS_ACAST;
        memcpy(w.value, acts[i].value, VLEN);
      } else {
        w.cls = BKR94ACS_CLS_BA;
        w.baRound = acts[i].round;
        w.initiator = acts[i].initiator;
        w.baValue = acts[i].baValue;
      }
      for (j = 0; j < N; ++j) {
        if (acts[i].skip && BRACHA87_SKIP_TST(acts[i].skip, j))
          continue;
        w.to = (unsigned char)j;
        pushWire(&w);
      }
      break;

    case BKR94ACS_ACT_COMPLETE:
      sysTryComplete(p, s);
      break;

    case BKR94ACS_ACT_BA_EXHAUSTED:
      /*
       * The library's one stop, and a failure stop: this BA can issue
       * no further phase, so COMPLETE is unreachable for the round.
       * Nothing here adds a stop -- the process is simply behind, and
       * the wanting side heals it (system.md, Relation to a deployment).
       */
      printf("process %u: BA[%u] EXHAUSTED at round %lu -- healing by adoption\n",
             (unsigned)p->self, (unsigned)acts[i].process, s->pos);
      break;

    default:
      break;
    }
  }
}

/*--------------------------------------------------------------------------*/
/*  Recovery legs                                                           */
/*--------------------------------------------------------------------------*/

static struct leg *
legFind(
  struct proc *p
 ,unsigned char server
 ,unsigned char served
 ,unsigned long pos
){
  unsigned int i;

  for (i = 0; i < LEGCAP; ++i)
    if (p->leg[i].inUse
     && p->leg[i].server == server
     && p->leg[i].served == served
     && p->leg[i].pos == pos)
      return (&p->leg[i]);
  return (0);
}

static struct leg *
legAlloc(
  struct proc *p
 ,unsigned char server
 ,unsigned char served
 ,unsigned long pos
 ,const unsigned char *prevAnchor
 ,unsigned long off
){
  struct leg *lg;
  unsigned int i;

  for (i = 0; i < LEGCAP; ++i)
    if (!p->leg[i].inUse)
      break;
  if (i == LEGCAP)
    return (0);
  lg = &p->leg[i];
  if (!(lg->f1 = calloc(1, LegSz)))
    return (0);
  bracha87Fig1Init(lg->f1, 1, 0, COMPLEN - 1);
  lg->inUse = 1;
  lg->server = server;
  lg->served = served;
  lg->pos = pos;
  lg->retired = 0;
  lg->selfAcc = 0;
  lg->otherAcc = 0;
  lg->cursor = 0;
  memcpy(lg->prevAnchor, prevAnchor, ANCHOR);
  lg->off = off;
  return (lg);
}

static void
legFree(
  struct leg *lg
){
  free(lg->f1);
  lg->f1 = 0;
  lg->inUse = 0;
}

/*
 * Emit one leg action to BOTH ends of the pair.  A Bracha Fig 1 is a
 * broadcast primitive and its sender is one of the recipients: at two
 * processes the echo threshold is (n+t)/2 + 1 = 2, so an end that does
 * not feed its own action to itself can never reach it.  Self-addressed
 * wires ride the same delivery path (pushWire exempts them from loss),
 * which keeps one ingress path rather than a recursive local feed.
 */
static void
legEmit(
  struct proc *p
 ,struct leg *lg
 ,unsigned char act
){
  struct rslot *s;
  struct wire w;
  const unsigned char *v;
  const unsigned char *skip;
  unsigned int e;
  unsigned int mi;

  if (!(v = bracha87Fig1Value(lg->f1)))
    return;
  skip = bracha87Fig1Skip(lg->f1, act);
  s = slotOf(p, lg->pos);
  memset(&w, 0, sizeof (w));
  w.kind = WK_SERVE;
  w.sysRound = (unsigned char)(lg->pos % RSPACE);
  w.from = p->self;
  w.legServer = lg->server;
  w.legServed = lg->served;
  w.sigOff = lg->off;
  memcpy(w.idAnchor, lg->prevAnchor, ANCHOR);
  w.type = (unsigned char)(act == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                         : act == BRACHA87_ECHO_ALL    ? BRACHA87_ECHO
                         :                               BRACHA87_READY);
  w.accepted = (unsigned char)(act == BRACHA87_READY_ALL && lg->selfAcc);
  /*
   * A leg wire is traffic of its round, so it carries the same
   * possession indication an ACS wire of that round would: the server
   * holds the round by construction, and the served process starts
   * carrying it the moment it adopts -- which is what retires the
   * serve per-process at the server end.
   */
  w.possesses = (unsigned char)(s && s->closed);
  if ((int)p->self == ByzProc) {
    if (ByzMode & BYZ_POSSESS)
      w.possesses = 1;
    else if (ByzMode & BYZ_WANT)
      w.possesses = 0;
  }
  memcpy(w.comp, v, COMPLEN);
  /*
   * Recovery REPLAYS the exchange.  Beside the composition on the
   * leg's value plane, one member's bank rides each leg wire, cycling
   * through the members this server can serve -- SERVE's .have grain.
   * One bank per wire, and deliberately NOT on the value plane: two
   * honest servers hold different content at different times, and
   * their compositions must still be byte-identical or the t+1 link
   * grouping could never form.
   *
   * The bank rides EVERY serve.  An earlier revision gated it on
   * DISTANCE (server two or more rounds ahead), on two premises that
   * are both gone: that relayed content rested on fiat -- it is now
   * self-validating (the member's origin tag travels with it) and is
   * the exchange's own bytes re-masked (the serving form) -- and that
   * a wanter one rung behind has content "its own exchange is still
   * delivering", which stopped being a premise the day -L was ruled
   * to cut the content plane too: the process being healed may be
   * owed content that will never arrive by itself at ANY distance.
   * A serve discharges want; the content rides the discharge because
   * the relay asserts nothing the receiver cannot check.
   */
  w.bankOf = NO_BANK;
  if (s && s->closed) {
    unsigned char sk[ANCHOR];
    unsigned char other;

    /*
     * THE SERVING FORM (O4): the bank rides re-masked under the leg
     * pair's key at this round's step, recomputed FROM THE FLOOR on
     * every serve -- base plus recompute over the retained chain
     * heads, never a kept snapshot.  A round fallen behind the floor
     * can no longer be masked for anyone: the composition is still
     * served (the heal survives) but the bank is not, and the refusal
     * is counted -- the per-member price O2 names out-of-band, arrived
     * at through key destruction rather than through a policy.
     */
    other = (unsigned char)(p->self == lg->server ? lg->served : lg->server);
    for (mi = 0; mi < N; ++mi) {
      unsigned char m;

      m = (unsigned char)((lg->cursor + mi) % N);
      if (!s->comp[ANCHOR + m] || !s->has[m])
        continue;
      if (!maskWalk(p, lg->pos, other, sk)) {
        ++ServeFloorHole;          /* held content, no key: terminal for
                                    * this round's banks at this end */
        break;
      }
      /*
       * The recompute must reproduce the frozen exchange-step form
       * byte for byte -- the one comparison that distinguishes a
       * reproduction from a fresh masking (the reference design's
       * near-miss: an off-by-one step also agrees between consenting
       * ends).  Divergence is a harness defect.
       */
      if (memcmp(sk, s->exchKey[other], ANCHOR))
        ++MaskMismatch;
      w.bankOf = m;
      maskBytes(w.value, s->content[m], sk);
      memcpy(w.bankTag, s->ctag[m], ANCHOR); /* forwarded, never minted */
      lg->cursor = (unsigned char)((m + 1) % N);
      ++ServeMasked;
      break;
    }
  }
  for (e = 0; e < 2; ++e) {
    if (skip && BRACHA87_SKIP_TST(skip, e))
      continue;
    w.to = (unsigned char)(e ? lg->served : lg->server);
    pushWire(&w);
  }
}

/*
 * A leg reached ACCEPT at this end.  The served end has now obtained a
 * byte-identical assertion of the round's composition from one server:
 * feed it to the machine's witness book (O3) if it speaks the frontier,
 * and evidence the server's own possession either way.
 *
 * The two calls are ordered deliberately: systemPossessed comes LAST
 * because a release it provokes must not free state this function is
 * still walking.
 */
static void
legAccept(
  struct proc *p
 ,struct leg *lg
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  const unsigned char *v;
  unsigned long f;
  unsigned int n;

  lg->selfAcc = 1;
  bracha87Fig1ProcessAccepted(lg->f1, (unsigned char)(p->self == lg->server ? 0 : 1));
  if (!(v = bracha87Fig1Value(lg->f1)))
    return;
  p->active = 1;

  if (p->self == lg->served) {
    f = rv(systemFrontier(p->sys));
    if (lg->pos == p->fpos) {
      struct rslot *sf;
      unsigned char expect[ANCHOR];
      unsigned int i;
      unsigned int cnt;
      unsigned int bad;

      /*
       * O3's FOLD GROUND, caller-side (system.md: "the fold ground is
       * realized wholly caller-side").  Two checks, each decided on ONE
       * assertion with no book consulted:
       *
       *  (1) LINKAGE.  The asserted anchor must be exactly what folding
       *      our own previous anchor over the asserted membership
       *      produces (O1: chain_R = H(chain_{R-1} || the members)).
       *      An assertion that does not chain from the history WE hold
       *      cannot be this round's result.
       *  (2) OUR OWN DECISIONS.  Any BA our own round-R instance has
       *      already decided pins that member: BKR94 agreement says
       *      every correct process decides a BA the same way, so an
       *      assertion disagreeing with a decision we hold is provably
       *      false.  No crypto and no counting.
       *
       * These gate the MESSAGE plane.  The ACCEPTANCE half -- the half
       * that consumes A10 -- is realized below at the grain the spec
       * gives it: what needs t+1 distinct servers is only the LINK,
       * [chain_{R-1} | chain_R | member count], while membership and
       * per-member digests are accepted from ONE server because they
       * fold to that witnessed anchor -- collision resistance making at
       * most one message plane derive it (per-member attestation at
       * threshold is unmeetable anyway: only n-2 servers can attest a
       * non-self member).  The fold here is the demo mixer, so the
       * binding has A10's SHAPE and none of its strength -- a
       * fabricated membership folding to the SAME anchor is what the
       * real hash excludes and nothing in this file can produce.  What
       * keeps a fabricated DIFFERENT anchor out is the link plane's
       * t+1: with at most t liars, no fabricated link ever accrues it.
       */
      bad = 0;
      foldAnchor(expect, prevAnchorAt(p, p->fpos), v + ANCHOR);
      if (memcmp(expect, v, ANCHOR))
        bad = 1;
      if (!bad && (sf = slotOf(p, p->fpos)) && sf->acs)
        for (i = 0; i < N; ++i) {
          unsigned char d;

          d = bkr94acsBaDecision(sf->acs, (unsigned char)i);
          if (d > 1)
            continue;              /* undecided (0xFF) or exhausted (0xFE) */
          if ((unsigned int)(d ? 1 : 0)
           != (unsigned int)(v[ANCHOR + i] ? 1 : 0)) {
            bad = 1;
            break;
          }
        }
      if (bad) {
        ++CandRejected;
        goto possession;           /* never enters the book */
      }

      /*
       * O3's verification seam, caller-side -- the NEVER-SWITCH
       * discipline (system.md, C6's seam pin) at the LINK grain: group
       * assertions by their LINK bytes in our own book and tell the
       * machine nothing until one link reaches t+1 distinct servers.
       * The MESSAGE plane is stored once per link, from the FIRST
       * server whose bytes folded to it, and that one server's bytes
       * are what an adoption consumes -- the fold ground's acceptance
       * half.  Later servers of the same link witness the link ONLY;
       * their message bytes are never compared (counted, so the run
       * can show the acceptance half really carried the load).
       *
       * Note WHICH property the grouping protects.  SAFETY -- a
       * fabrication is never adopted -- is the MACHINE's: its witness
       * record marks only the authenticated sender, so t forgers reach
       * at most t, one short of the adopt gate.  What the grouping
       * protects is LIVENESS.  Latch the FIRST link instead and a
       * Byzantine first server latches a fabricated one that no honest
       * assertion ever matches, so adoption stalls forever -- confirmed
       * by mutating this test to `cnt >= 1`, which strands the laggard
       * at zero adoptions in every seed tried while containment still
       * reads ok.
       *
       * That stall is precisely what systemWitnessReset exists to
       * break, and grouping is the stronger answer: under t Byzantine
       * any t+1 group contains an honest server asserting the agreed
       * link, so at most one link can ever reach t+1 and a switch
       * cannot arise.  Hence the reset stays uncalled HERE; a caller
       * that cannot group must call it.
       */
      if (p->candPos != p->fpos || (!p->candValid && !p->candCnt)) {
        p->candPos = p->fpos;
        p->candCnt = 0;
        memset(p->candSrv, 0, sizeof (p->candSrv));
      }
      {
        unsigned char link[ANCHOR + 1];
        unsigned int mc;

        memcpy(link, v, ANCHOR);
        mc = 0;
        for (i = 0; i < N; ++i)
          if (v[ANCHOR + i])
            ++mc;
        link[ANCHOR] = (unsigned char)mc;
        for (i = 0; i < p->candCnt; ++i)
          if (!memcmp(p->candLink[i], link, sizeof (link)))
            break;
        if (i == p->candCnt && p->candCnt < MAX_PROCESSES) {
          memcpy(p->candLink[i], link, sizeof (link));
          memcpy(p->candBuf[i], v, COMPLEN);
          ++p->candCnt;
          if (p->candCnt == 2 && (int)p->self != ByzProc)
            ++CandConflicts;       /* the grouping had real work to do */
        } else if (i < p->candCnt)
          ++LinkOnly;              /* link witnessed; message plane unread */
      }
      if (i < p->candCnt) {
        p->candSrv[i][lg->server >> 3] |=
          (unsigned char)(1 << (lg->server & 7));
        cnt = 0;
        for (n = 0; n < N; ++n)
          if (SYSTEM_TST(p->candSrv[i], n))
            ++cnt;
        if (cnt >= T + 1 && !p->candValid) {
          memcpy(p->cand, p->candBuf[i], COMPLEN);
          p->candValid = 1;
          for (n = 0; n < N; ++n) {
            unsigned int wn;

            if (!SYSTEM_TST(p->candSrv[i], n))
              continue;
            wn = systemWitness(p->sys, rn(f), (unsigned char)n, sa);
            applySysActs(p, sa, wn, 0);
          }
        }
      }
    }
  }

possession:
  n = systemPossessed(p->sys, rn(lg->pos), lg->server, sa);
  applySysActs(p, sa, n, 0);
}

/*
 * Birth a leg as the SERVER of 'round' to wanting process 'to'.  The
 * composition is the leg's broadcast value; the leg is a Fig 1, so BPR
 * carries it from here and it retires only on the other end's accept.
 */
static void
legBirthServer(
  struct proc *p
 ,unsigned char to
 ,struct rslot *s
){
  struct leg *lg;
  unsigned char fake[COMPLEN];
  const unsigned char *assertion;

  /*
   * A Byzantine server asserts a round it need not hold -- the honest
   * gate is possession, and it is exactly the gate an attacker skips.
   */
  if (!s->closed && (int)p->self != ByzProc)
    return;
  if (!s->srvSet) {
    /*
     * The assertion is authored ONCE for the round; every leg serving
     * it -- to however many wanters, over however many retries -- is
     * a byte-identical re-presentation at the same offset (O5).  A
     * per-leg consumption here is the mistake that melted the budget:
     * serve pressure burned keys so fast that carriers expired
     * mid-delivery and the capacity gate jammed on content that could
     * no longer arrive.
     */
    s->srvOff = sigConsume(p);
    s->srvSet = 1;
  }
  if ((lg = legFind(p, p->self, to, s->pos))) {
    if (lg->retired)
      return;
  } else if (!(lg = legAlloc(p, p->self, to, s->pos, s->prevAnchor,
                             s->srvOff)))
    return;
  assertion = s->comp;
  if ((int)p->self == ByzProc && (ByzMode & (BYZ_CAND | BYZ_LINK))) {
    /*
     * The fabricated composition: a plausible one, internally
     * consistent (membership flipped, then re-folded), so nothing but
     * the t+1 grouping can tell it from the agreed one.  Containment is
     * the machine's: the witness record marks only this sender, so t
     * forgers reach at most t, one short of the t+1 the adopt gate
     * needs.
     */
    memcpy(fake, s->comp, COMPLEN);
    fake[ANCHOR + ((p->self + 1) % N)] ^= 1;
    if (ByzMode & BYZ_CAND)
      foldAnchor(fake, s->prevAnchor, fake + ANCHOR);
    assertion = fake;
    if (!bracha87Fig1Value(lg->f1))
      ++ByzAsserts;
  }
  if (!bracha87Fig1Value(lg->f1)) {
    bracha87Fig1Initiator(lg->f1, assertion);
    legEmit(p, lg, BRACHA87_INITIAL_ALL);
  }
}

/*--------------------------------------------------------------------------*/
/*  Exchange                                                                */
/*--------------------------------------------------------------------------*/

/*
 * Take delivery of member 'member''s bank for 'round'.  This is the ONLY
 * way content enters this process besides its own authorship: the round
 * instance carries the bank, but a banked artifact is not content until
 * it is exchanged, so nothing here reads bkr94acsAcastValue.
 *
 * The machine is told only about a member of a composition we hold --
 * the have grain is a grain OF the round's membership -- and only while
 * the round is retained.  Content that lands later is still recorded and
 * still fills its sequence position; it simply cannot be advertised on a
 * SERVE, which is the O2 out-of-band tail seen from the serving side.
 */
static unsigned int
recordBank(
  struct proc *p
 ,unsigned long pos
 ,unsigned char member
 ,const unsigned char *bytes
 ,const unsigned char *tag      /* the member's origin tag, or 0 when the
                                 * bytes came by reconstruction (the value
                                 * plane) and the digest alone vouches */
){
  struct rslot *s;

  if (member >= MAX_PROCESSES || !(s = slotOf(p, pos)) || s->has[member])
    return (0);
  /*
   * O2's self-validation, two independent checks.  The ORIGIN tag
   * binds the bytes to their author and travels with them, so it
   * validates on ARRIVAL -- pre-close included, which is what removes
   * the old relay fiat window (content replayed before the close used
   * to be believed until the close's digest).  The DIGEST binds the
   * bytes to the agreed composition and exists only once the round is
   * closed here.  Detect-don't-correct on both: a bad piece is refused
   * on one look and the carrier goes on retrying; nothing is repaired.
   */
  if (tag) {
    unsigned char ot[ANCHOR];

    mixTag(ot, BankKey[member], bytes, VLEN);
    if (memcmp(ot, tag, ANCHOR)) {
      ++OriginRejected;
      return (0);
    }
  }
  if (s->closed && s->comp[ANCHOR + member]) {
    unsigned char dg[DGLEN];

    mixTag(dg, Genesis, bytes, VLEN);
    if (memcmp(dg, COMPDG(s->comp, member), DGLEN)) {
      ++DigestRejected;
      return (0);
    }
  }
  memcpy(s->content[member], bytes, VLEN);
  /*
   * Keep the tag beside the content: a later serve FORWARDS it, since
   * a relay cannot mint one.  Bytes accepted by reconstruction carry
   * none, so the tag is re-derived here -- the demo's stand-in for a
   * reconstruction whose proofs travel with it (threshold re-encode +
   * Merkle in a deployment), open arithmetic here (SCOPE).
   */
  if (tag)
    memcpy(s->ctag[member], tag, ANCHOR);
  else
    mixTag(s->ctag[member], BankKey[member], bytes, VLEN);
  s->has[member] = 1;
  p->active = 1;
  if (!s->told[member]
   && s->closed && s->comp[ANCHOR + member]
   && systemRetained(p->sys, rn(pos))) {
    systemAssembled(p->sys, rn(pos), member);
    s->told[member] = 1;
  }
  return (1);
}

/*
 * The exchange slot for (pos, member).  Directly indexed by the byte,
 * which IS the one-live-incarnation-per-byte bound: a slot found holding
 * a different position is a stale incarnation the reuse guard has not
 * reclaimed yet, and every caller runs strictly behind the launches that
 * fire the guard, so it cannot arise -- refuse it rather than reuse it.
 */
static struct exch *
exchFind(
  struct proc *p
 ,unsigned long pos
 ,unsigned char member
){
  struct exch *ex;

  ex = p->exch + (pos % RSPACE) * N + member;
  if (ex->inUse && ex->pos == pos)
    return (ex);
  return (0);
}

static struct exch *
exchAlloc(
  struct proc *p
 ,unsigned long pos
 ,unsigned char member
 ,const unsigned char *prevAnchor
 ,unsigned long off
){
  struct exch *ex;

  ex = p->exch + (pos % RSPACE) * N + member;
  if (ex->inUse)
    return (0);                    /* a stale incarnation: never reuse */
  if (!(ex->f1 = calloc(1, ExchSz)))
    return (0);
  bracha87Fig1Init(ex->f1, (unsigned char)(N - 1), (unsigned char)T, VLEN - 1);
  ex->inUse = 1;
  ex->pos = pos;
  ex->member = member;
  ex->retired = 0;
  ex->selfAcc = 0;
  memcpy(ex->prevAnchor, prevAnchor, ANCHOR);
  ex->off = off;
  /*
   * Every field re-initialized: this slot is REUSED across incarnations
   * (the guard clears inUse and nothing else), and a stale artifact
   * offset inherited here once expired fresh mirrors at birth --
   * killing content that was still arriving and pricing it as a hole.
   */
  ex->artOff = 0;
  ex->artKnown = 0;
  ex->sideHave = 0;
  ++ExchBirthed;
  return (ex);
}

/*
 * Quiescence, not release: every process has accepted, so no further
 * traffic of this instance is owed anywhere.  The slot is KEPT (with its
 * key) so a straggling wire cannot re-birth the instance and start the
 * broadcast over.
 */
static void
exchRetire(
  struct exch *ex
){
  free(ex->f1);
  ex->f1 = 0;
  ex->retired = 1;
  ++ExchRetired;
}

static void
exchEmit(
  struct proc *p
 ,struct exch *ex
 ,unsigned char act
){
  struct rslot *s;
  struct wire w;
  const unsigned char *v;
  const unsigned char *skip;
  unsigned int j;

  if (!ex->f1 || !(v = bracha87Fig1Value(ex->f1)))
    return;
  skip = bracha87Fig1Skip(ex->f1, act);
  s = slotOf(p, ex->pos);
  memset(&w, 0, sizeof (w));
  w.kind = WK_EXCH;
  w.sysRound = (unsigned char)(ex->pos % RSPACE);
  w.from = p->self;
  w.bankOf = ex->member;
  w.sigOff = ex->off;
  memcpy(w.idAnchor, ex->prevAnchor, ANCHOR);
  w.type = (unsigned char)(act == BRACHA87_INITIAL_ALL ? BRACHA87_INITIAL
                         : act == BRACHA87_ECHO_ALL    ? BRACHA87_ECHO
                         :                               BRACHA87_READY);
  w.accepted = (unsigned char)(act == BRACHA87_READY_ALL && ex->selfAcc);
  /* An exchange wire is traffic of its round, like every other carrier. */
  w.possesses = (unsigned char)(s && s->closed);
  if ((int)p->self == ByzProc) {
    if (ByzMode & BYZ_POSSESS)
      w.possesses = 1;
    else if (ByzMode & BYZ_WANT)
      w.possesses = 0;
  }
  memcpy(w.value, v, VLEN);
  /*
   * The per-destination sidecar (O4's serving form at its birth): the
   * member's INITIAL -- and only the INITIAL, only the member sends
   * one -- carries each destination's own masked copy of the bank,
   * under the pair's key at this round's step, pinned at the close.
   * A BPR re-INITIAL re-masks under the same pinned row, so the
   * re-presentation is byte-identical (O5's discipline extended to
   * the masked grain).  Echoes and readys carry none: the value plane
   * is the agreement grain (Lemma 2), the sidecar is the content
   * grain, and the recovery leg's re-mask from the floor must later
   * reproduce exactly these bytes.
   */
  for (j = 0; j < N; ++j) {
    if (skip && BRACHA87_SKIP_TST(skip, j))
      continue;
    w.to = (unsigned char)j;
    w.sideHave = 0;
    if (act == BRACHA87_INITIAL_ALL && ex->member == p->self
     && s && s->closed) {
      maskBytes(w.side, v, s->exchKey[j]);
      /* the member mints its origin tag over the CLEAR bytes (O2) */
      mixTag(w.bankTag, BankKey[p->self], v, VLEN);
      w.sideHave = 1;
    }
    pushWire(&w);
  }
}

/*
 * Birth this process's OWN exchange for a round it has just closed into
 * a composition that includes it.  The gate is the close, and that gate
 * IS the censorship resistance: a member that released its content while
 * the round was still being agreed would let the cohort decide inclusion
 * on what it saw, which is exactly the decision the round exists to take
 * blind.  A member excluded from the round has released nothing, so the
 * exclusion costs participation and never the value -- it rides the next
 * round and exchanges after THAT one closes.
 */
static void
exchBirthMember(
  struct proc *p
 ,struct rslot *s
){
  struct exch *ex;

  if (!s->comp[ANCHOR + p->self])
    return;
  if (!(ex = exchFind(p, s->pos, p->self))
   && !(ex = exchAlloc(p, s->pos, p->self, s->prevAnchor, s->off)))
    return;
  if (ex->retired || !ex->f1 || bracha87Fig1Value(ex->f1))
    return;
  ex->off = s->off;                /* the SAME offset the contribution
                                    * consumed: one authored artifact,
                                    * re-presented on a second carrier */
  ex->artOff = s->off;
  ex->artKnown = 1;
  bracha87Fig1Initiator(ex->f1, s->bank);
  exchEmit(p, ex, BRACHA87_INITIAL_ALL);
}

/*
 * M2's capacity gate, DERIVED (system.h systemLaunch, 'backlogDrained').
 *
 * The contract is precise about what may be read: every result this
 * process's prior advances accepted has reached its decision stream,
 * taken from the caller's OWN BOOKS, and "no emission state is
 * consulted" -- a put on a carrier attests nothing beyond the attempt.
 * So this asks the TOCK, never the carriers: is each closed round's
 * content in hand?  How many exchanges that took is the layer below's
 * business -- all in-subset members here, t+1 shards in a deployment
 * that disperses -- and either way the gate is the same question.
 *
 * The second clause is what keeps the gate from becoming a liveness
 * hold.  A result that can no longer arrive HAS reached the stream, as
 * an out-of-band hole; if it did not count, one member withholding its
 * own content would stop every correct process from ever presenting
 * again, which hands a single fault a cohort-wide stall.  Unreachable
 * is read locally and conservatively: the round has been released (its
 * record is gone, so nothing further can be reported against it) AND no
 * carrier for the missing bank is alive here.
 *
 * The declaration governs the CAPACITY GATE ONLY.  seqEmit still waits
 * on content, so declaring early costs at most an admission one sweep
 * ahead of itself and can never put a hole in the sequence -- which is
 * why this needs no grace period against the race between a member's
 * first exchange wire and its first possession-bearing one.
 *
 * Not keyed on the emission cursor: the sequence is ordered, so one gap
 * holds every later round unemitted however complete its content is,
 * and a gate reading emission would inherit that block instead of
 * measuring what has actually arrived.
 */
static unsigned int
backlogDrained(
  struct proc *p
){
  struct rslot *s;
  struct exch *ex;
  unsigned int j;
  unsigned int k;

  for (j = 0; j < RSPACE; ++j) {
    s = p->slot + j;
    if (!s->inUse || !s->closed || s->pos >= p->fpos)
      continue;                    /* not strictly below the frontier */
    for (k = 0; k < N; ++k) {
      if (!s->comp[ANCHOR + k] || s->has[k])
        continue;
      if (systemRetained(p->sys, rn(s->pos)))
        return (0);                /* still assembling, still in reach */
      if ((ex = exchFind(p, s->pos, (unsigned char)k))
       && !ex->retired)
        return (0);                /* a carrier for it is still alive */
    }
  }
  return (1);
}

/*--------------------------------------------------------------------------*/
/*  Applying machine actions                                                */
/*--------------------------------------------------------------------------*/

static void
applySysActs(
  struct proc *p
 ,struct systemAct *sa
 ,unsigned int nacts
 ,const struct wire *w     /* the provoking wire, for DELIVER; else 0 */
){
  struct bkr94acsAct out[BKR94ACS_MAX_ACTS(MAX_PROCESSES, MAX_PHASES)];
  struct rslot *s;
  unsigned long rd;
  unsigned int i;
  unsigned int j;
  unsigned int n;

  for (i = 0; i < nacts; ++i) {
    rd = rv(sa[i].round);
    /*
     * A machine act's round is a POSITION; the slot is that position's
     * incarnation of its slot index, verified per act below (the
     * machine speaks only of live and retained rounds).
     */
    s = p->slot + rd % RSPACE;

    switch (sa[i].act) {

    case SYSTEM_ACT_DELIVER:
      /*
       * The machine is this caller's router (system.h: routing is the
       * caller's, and DELIVER is the offered surface for a caller whose
       * router IS the machine).  A deployment that routes by its own
       * demux discharges delivery there instead and ignores this act.
       */
      if (!w || w->kind != WK_ACS || !s->inUse || s->pos != rd || !s->acs)
        break;
      if (w->cls == BKR94ACS_CLS_ACAST) {
        n = bkr94acsAcastInput(s->acs, w->process, w->type, w->from,
                               w->value, out);
        if (w->type == BRACHA87_READY && w->accepted)
          bkr94acsAcastAccepted(s->acs, w->process, w->from);
      } else {
        n = bkr94acsBaInput(s->acs, w->process, w->baRound, w->initiator,
                            w->type, w->from, w->baValue, out);
        if (w->type == BRACHA87_READY && w->accepted)
          bkr94acsBaAccepted(s->acs, w->process, w->baRound, w->initiator,
                             w->from);
      }
      if (n)
        p->active = 1;              /* a fresh cascade; dedup returns 0 */
      emitAcs(p, s, out, n);
      /* The sweep-side decisions at zero tolerance budget -- this
       * caller deliberately keeps the eager tempo so instrument
       * baselines stay comparable; a WAN-grade budget belongs on the
       * sweep.  Turns first: only a turn produces the decisions the
       * fanout counts, and a fanout cannot make a round turnable (it
       * writes only entered[] and round-0 initiator state, which no
       * turn duty reads).  Turns drain per BA and over every process,
       * since cascade unlocks successive rounds of one BA and an
       * A-Cast accept opens round 0 of another; the instance pointer
       * is re-read each turn because a release inside emitAcs frees
       * it. */
      for (j = 0; j < N; ++j)
        while (s->acs
            && (n = bkr94acsTurn(s->acs, (unsigned char)j, 1, out)) > 0) {
          p->active = 1;
          emitAcs(p, s, out, n);
        }
      if (!s->acs)
        break;
      n = bkr94acsFanout(s->acs, out);
      if (n)
        p->active = 1;
      emitAcs(p, s, out, n);
      break;

    case SYSTEM_ACT_SERVE:
      /*
       * SERVE's pacing is entirely the caller's (system.md SERVE
       * bounds): concurrency capped at t in-flight wanting processes
       * AND NEVER FEWER THAN ONE -- at t = 0 the cap is 1, not 0, or
       * SERVE would be retired by silence, which M1 forbids.  Grants
       * beyond the cap queue for a later tick.
       *
       * The spec's rotation is oldest-WANT-first; this walk approximates
       * it with the serve cursor's own cyclic order, which is over SLOTS
       * and diverges from round age after an out-of-order release (a
       * younger round takes the freed older slot).  Eventual service
       * survives -- the rotation is cyclic, so every wanting process is
       * reached -- but a caller that owes strict oldest-first must sort
       * by round age itself.  Likewise 'live' counts legs rather than
       * distinct wanting processes, so a leg to a dead process holds a
       * slot until its round is released.
       */
      if (!s->inUse || s->pos != rd || !s->closed)
        break;
      {
        unsigned int cap;
        unsigned int live;

        cap = T ? T : 1;
        live = 0;
        for (j = 0; j < LEGCAP; ++j)
          if (p->leg[j].inUse
           && p->leg[j].server == p->self
           && !p->leg[j].retired)
            ++live;
        for (j = 0; j < N; ++j) {
          if (j == p->self || !SYSTEM_TST(sa[i].want, j))
            continue;
          if (legFind(p, p->self, (unsigned char)j, s->pos))
            continue;              /* already in flight: not a new grant */
          if (live >= cap)
            break;                 /* the rest queue for a later tick */
          legBirthServer(p, (unsigned char)j, s);
          ++live;
        }
      }
      break;

    case SYSTEM_ACT_RELEASE:
      if (!s->inUse || s->pos != rd)
        break;
      /*
       * Nothing is harvested from the instance: its A-Cast values are
       * BANKS, and a bank is not content until it is exchanged.  The
       * exchanges for this round therefore SURVIVE the release -- they
       * are the only carrier left, and content legitimately assembles
       * past the composition it belongs to (O2).  Legs do not: they
       * assert a composition this process no longer retains.
       */
      free(s->acs);
      s->acs = 0;
      for (j = 0; j < LEGCAP; ++j)
        if (p->leg[j].inUse && p->leg[j].pos == s->pos)
          legFree(&p->leg[j]);
      p->active = 1;
      break;

    case SYSTEM_ACT_ADOPT:
      /*
       * ADOPT does NOT close here.  system.h requires an instance to be
       * LIVE for the round before systemComplete does anything, and the
       * recovery traffic that provoked this only recorded participation
       * OWED -- the JOIN that discharges it fires at the next
       * systemLaunch.  Closing from here would be silently inert, and
       * ADOPT is single-fire, so the adoption would be lost.  Carry it
       * as a debt instead; the tick discharges it after its launch.
       */
      if (!p->candValid)
        break;
      p->adoptPending = 1;
      p->active = 1;
      break;

    case SYSTEM_ACT_JOIN:
    case SYSTEM_ACT_ADMIT:
    case SYSTEM_ACT_MAINTAIN: {
      struct bkr94acsAct one;
      unsigned char val[VLEN];

      /*
       * THE GUARD -- stop the tail, never the frontier.  A launch is
       * always at the frontier position, so a slot found holding a
       * DIFFERENT position is the byte's previous incarnation, exactly
       * one round space behind.  Proceed unconditionally and ABANDON
       * everything still outstanding under the old name: past this
       * moment the byte is ambiguous, so the reuse is the last moment
       * any obligation under it could have been honored -- a DERIVED
       * deadline, not a tuned one.  A process still dragging that tail
       * either IS one of the t or is counted as one now; the distance
       * is the evidence.  The rejected alternative -- hold the launch
       * while a carrier still holds the byte -- is an all-n condition
       * that never clears at t >= 1 (see the header's clock section).
       *
       * The sequence positions of the abandoned round are emitted
       * FIRST, from the slot about to be reclaimed: what is missing
       * now is missing for good, so it becomes an out-of-band hole at
       * the deadline rather than at teardown.
       */
      if (s->inUse && s->pos != p->fpos) {
        ++GuardFires;
        seqEmit(p, s->pos + 1, 1);
        for (j = 0; j < N; ++j) {
          struct exch *ex;

          ex = p->exch + (rd % RSPACE) * N + j;
          if (ex->inUse) {
            if (!ex->retired) {
              ++AbandonedExch;
              free(ex->f1);
              ex->f1 = 0;
            }
            ex->inUse = 0;
          }
        }
        for (j = 0; j < LEGCAP; ++j)
          if (p->leg[j].inUse && p->leg[j].pos == s->pos) {
            if (!p->leg[j].retired)
              ++AbandonedLeg;
            legFree(&p->leg[j]);
          }
        free(s->acs);
        memset(s, 0, sizeof (*s));
      }
      if (s->inUse && s->acs)
        break;                     /* R2b: a resumed round keeps its
                                    * instance state; never re-execute */
      if (!s->inUse) {
        memset(s, 0, sizeof (*s));
        s->inUse = 1;
        s->pos = p->fpos;
        s->rode = NO_MSG;
        memcpy(s->prevAnchor, prevAnchorAt(p, p->fpos), ANCHOR);
        /*
         * Authoring the round's contribution consumes one signing
         * offset (O5) -- consumed at the FIRST launch of this
         * position only: a resumed round re-presents at the offset
         * already spent, which is the byte-identity obligation.
         */
        s->off = sigConsume(p);
      }
      if (!(s->acs = calloc(1, AcsSz)))
        break;
      bkr94acsInit(s->acs, (unsigned char)(N - 1), (unsigned char)T,
                   VLEN - 1, MAX_PHASES, p->self, demoCoin, 0);
      bracha87RetryInit(&s->cur);

      memset(val, 0, sizeof (val));
      if (sa[i].act == SYSTEM_ACT_MAINTAIN) {
        /*
         * O5: the contribution is the maintenance form, NEVER the
         * pending value.  A maintenance win is not the value's win, so
         * PRESENT stays outstanding and the value rides a later round.
         * The close of this round, with this process in the subset,
         * INSTALLS the rotation (sysClose).
         */
        memcpy(val, Maint, sizeof (Maint));
        s->rode = NO_MSG;
        s->maint = 1;
      } else if (p->msgHead < p->msgCnt) {
        /*
         * PRESENT, caller half: re-present the staged bytes
         * byte-identically.  A JOIN carries a pending value exactly as
         * an ADMIT does -- participation is contribution.
         */
        memcpy(val, p->msg[p->msgHead], VLEN);
        s->rode = p->msgHead;
      } else {
        memcpy(val, Empty, sizeof (Empty));
        s->rode = NO_MSG;
      }
      /*
       * The BANK: what this process commits to the round.  It rides the
       * ACS value plane -- where, with crypto, it would be validated
       * shards worth nothing until the exchange releases what completes
       * them -- and it is kept here because the exchange that will carry
       * it as CONTENT is not born until this round closes.
       */
      memcpy(s->bank, val, VLEN);
      n = bkr94acsAcast(s->acs, val, &one);
      p->active = 1;
      if (Verbose)
        printf("process %u: %s round %lu with \"%s\"\n",
               (unsigned)p->self,
               sa[i].act == SYSTEM_ACT_JOIN ? "JOIN"
             : sa[i].act == SYSTEM_ACT_ADMIT ? "ADMIT" : "MAINTAIN",
               s->pos, (const char *)val);
      emitAcs(p, s, &one, n);
      break;
    }

    default:
      break;
    }
  }
}

/*
 * The close, PACED BY THE CALLER on its own evidence -- the machine's
 * COMPLETE is enabling evidence, not a moment (the reading every layer
 * below already honors).  A BA can decide 1 on BA traffic alone, so at
 * COMPLETE a subset member's A-Cast value can still be crossing; the
 * per-member digest (O2) derives from that value, and closing without
 * it would put byte-DIVERGENT compositions at honest processes -- the
 * witness grouping's t+1 could never form and every later digest check
 * would refuse honest content.  So the close waits for its own
 * instance's subset values: the AGREEMENT plane's artifact, which a
 * deployment's value plane carries by construction -- never the
 * content plane, so the exchange pipeline's one-stage offset stands.
 * Reliable broadcast delivers every accepted A-Cast everywhere
 * eventually, so the deferral always ends; the sweep re-attempts it.
 *
 * Two call sites with different enclosing state: the COMPLETE act as
 * it lands (the common case, no deferral), and the sweep (the
 * re-attempt when a value lagged its round's decisions).
 */
static void
sysTryComplete(
  struct proc *p
 ,struct rslot *s
){
  unsigned char comp[COMPLEN];
  unsigned char members[MAX_PROCESSES];
  unsigned int cnt;
  unsigned int k;

  if (s->closed || !s->acs || !s->acs->complete)
    return;
  memset(comp, 0, sizeof (comp));
  cnt = bkr94acsSubset(s->acs, members);
  for (k = 0; k < cnt; ++k) {
    const unsigned char *cv;

    if (members[k] >= MAX_PROCESSES)
      continue;
    if (!(cv = bkr94acsAcastValue(s->acs, members[k])))
      return;                      /* a member's value is still crossing */
    comp[ANCHOR + members[k]] = 1;
    mixTag(COMPDG(comp, members[k]), Genesis, cv, VLEN);
  }
  foldAnchor(comp, s->prevAnchor, comp + ANCHOR);
  sysClose(p, s, comp);
}

/*--------------------------------------------------------------------------*/
/*  The one consume region                                                  */
/*                                                                          */
/*  A live COMPLETE and an adoption both close HERE (system.md, the         */
/*  wanting side: one region -- two copies drift).  A racing own COMPLETE   */
/*  therefore structurally supersedes the witness book.                     */
/*--------------------------------------------------------------------------*/

static void
sysClose(
  struct proc *p
 ,struct rslot *s
 ,const unsigned char *comp
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  unsigned char have[(MAX_PROCESSES + 7) / 8];
  unsigned long before;
  unsigned long round;
  unsigned int n;
  unsigned int j;

  if (!s || !s->inUse || s->pos != p->fpos)
    return;
  round = s->pos;
  before = rv(systemFrontier(p->sys));

  /*
   * A member holds its OWN bank by authorship -- it wrote it.  Every
   * other member's arrives on that member's exchange, and an exchange
   * cannot be born until its round closes, so at THIS instant the have
   * grain is self alone (or, for a healed process, whatever recovery has
   * already replayed).  That is what makes systemAssembled load-bearing
   * in this file rather than incidental: nearly all of the O2 grain
   * arrives after the composition it belongs to.
   */
  if (comp[ANCHOR + p->self] && !s->has[p->self]) {
    memcpy(s->content[p->self], s->bank, VLEN);
    mixTag(s->ctag[p->self], BankKey[p->self], s->bank, VLEN);
    s->has[p->self] = 1;
  }
  /*
   * Content that landed BEFORE the close (a recovery replay to a
   * process being healed) was recorded unvalidated -- there was no
   * agreed digest yet.  There is now: a piece that does not fold to
   * its member's digest is dropped, and its arrival re-owed (O2's
   * detect-don't-correct, at the close's edge).
   */
  for (j = 0; j < N; ++j)
    if (comp[ANCHOR + j] && s->has[j]) {
      unsigned char dg[DGLEN];

      mixTag(dg, Genesis, s->content[j], VLEN);
      if (memcmp(dg, COMPDG(comp, j), DGLEN)) {
        ++DigestRejected;
        s->has[j] = 0;
      }
    }
  memset(have, 0, sizeof (have));
  for (j = 0; j < N; ++j)
    if (comp[ANCHOR + j] && s->has[j])
      have[j >> 3] |= (unsigned char)(1 << (j & 7));

  n = systemComplete(p->sys, rn(round), rn(round + 1), have, sa);
  if (rv(systemFrontier(p->sys)) == before)
    return;                        /* refused: not the frontier, or no
                                    * live instance -- the machine's own
                                    * supersession guard */

  memcpy(s->comp, comp, COMPLEN);
  s->closed = 1;
  /*
   * Pin this round's mask row -- the frozen step, born with the
   * assembly (O4): derived ONCE from the floor at the close and kept
   * with the slot, so content still crossing when the floor later
   * passes this round can still be opened here, while nothing fresh
   * can be masked for it.  The walk cannot refuse: the floor never
   * crosses the frontier, and every round below it is closed.
   */
  for (j = 0; j < N; ++j)
    maskWalk(p, s->pos, (unsigned char)j, s->exchKey[j]);
  ++p->fpos;                       /* the POSITION advances with the byte */
  p->adoptPending = 0;
  p->candValid = 0;
  p->tolCount = 0;
  p->active = 1;

  /* Only now is the have grain a grain of a composition we hold. */
  for (j = 0; j < N; ++j)
    if (comp[ANCHOR + j] && s->has[j])
      s->told[j] = 1;

  /*
   * PRESENT retirement, caller half: the staged value retires ONLY on
   * being witnessed as a member of an agreed subset.  Exclusion does
   * not retire it (R2d) and a maintenance win does not retire it (O5) --
   * both of those leave it staged to ride the next round, which is the
   * at-least-once half of R1.  Retiring here, exactly once, is the
   * at-most-once half.
   */
  if (comp[ANCHOR + p->self] && s->rode != NO_MSG
   && s->rode == p->msgHead)
    ++p->msgHead;

  /*
   * ROTATION INSTALL, on the maintenance round's own close with this
   * process in the agreed subset -- the agreed position is the install
   * point (the layer's rotation discipline: rotate in code, install on
   * COMPLETE).  The next key starts at its own boundary; offsets the
   * old key never signed are discarded with it, which is why rotating
   * early costs budget and rotating late is the overdraft.  An
   * EXCLUDED maintenance round installs nothing: maintenance stays
   * due, and the next round is another maintenance round (O5's
   * precedence) -- rotation retries by the same rule as everything
   * else here, by riding the next position.
   */
  if (s->maint && comp[ANCHOR + p->self]) {
    p->rotKey = p->sigOff / SigBudget + 1;
    p->sigOff = p->rotKey * SigBudget;
    ++Rotations;
  }

  /*
   * The agreement is fixed, so the content may now go out: this is the
   * pipeline's one-stage offset, exchange(R) alongside the round for
   * R+1.
   */
  exchBirthMember(p, s);

  if (Verbose) {
    printf("process %u: closed round %lu, subset {", (unsigned)p->self,
           s->pos);
    for (j = 0; j < N; ++j)
      if (comp[ANCHOR + j])
        printf(" %u", j);
    printf(" }\n");
  }

  applySysActs(p, sa, n, 0);

  /* Re-present the indications that arrived while this round was live. */
  if (p->pendPos == s->pos)
    for (j = 0; j < N; ++j) {
      struct systemAct pa[SYSTEM_MAX_ACTS];
      unsigned int pn;

      if (j == p->self || !p->pendF[j])
        continue;
      p->pendF[j] = 0;
      if (!systemRetained(p->sys, rn(round)))
        break;                     /* released underneath us */
      pn = systemPossessed(p->sys, rn(round), (unsigned char)j, pa);
      applySysActs(p, pa, pn, 0);
    }
}

/*--------------------------------------------------------------------------*/
/*  Ingress                                                                 */
/*--------------------------------------------------------------------------*/

static void
deliverWire(
  const struct wire *w
){
  struct systemAct sa[SYSTEM_MAX_ACTS];
  struct proc *p;
  const unsigned char *pa;
  long pos;
  unsigned int n;
  unsigned int i;

  /*
   * A9 at ingress, FIRST: nothing about a wire is believed -- not even
   * its routing -- until the pairwise tag verifies.  The demo's coin
   * discipline: no forger exists here, so a refusal is a defect and is
   * reported as one.
   */
  {
    struct wire t;
    unsigned char want[ANCHOR];

    if (w->from >= N || w->to >= N)
      return;
    t = *w;
    memset(t.tag, 0, sizeof (t.tag));
    mixTag(want, Psk[t.from][t.to], (const unsigned char *)&t,
           sizeof (t));
    if (memcmp(want, w->tag, ANCHOR)) {
      ++TagRejected;
      return;
    }
  }

  p = &Proc[w->to];
  /*
   * The byte ROUTES: map it into this process's own position space.  A
   * wire mapping before genesis is a stale byte no live position wears
   * -- dropped.  Everything at or behind the frontier then faces the
   * IDENTITY check below; everything ahead is unverifiable here.
   */
  if ((pos = posOf(p, w->sysRound)) < 0)
    return;

  if (w->kind != WK_ACS) {
    struct leg *lg;
    struct exch *ex;
    unsigned char out[3];
    unsigned char legFrom;
    unsigned int k;

    if ((unsigned long)pos > p->fpos)
      return;                      /* beyond chain reach: unverifiable */
    /*
     * The identity PROVES: a wire of position P asserts the chain
     * anchor of P - 1, and this process holds every anchor at or
     * behind its frontier.  A mismatch is a wire from a byte's
     * PREVIOUS incarnation -- the byte routed it to a live name, the
     * lineage refuses it.  Without this gate a process a whole round
     * space behind (a stalled liar, a long partition) would inject its
     * stale round into the live one wearing the same byte.
     */
    if (!(pa = prevAnchorAt(p, (unsigned long)pos))
     || memcmp(w->idAnchor, pa, ANCHOR)) {
      ++IdRejected;
      return;
    }
    /*
     * The KEY window, the O5 conjunct beside the anchor's O1 conjunct
     * -- and the two are genuinely independent.  A STALLED author's
     * chain never advances, so its traffic stays current for ITS chain
     * forever and only the anchor refuses it at the wrap; a LINGERING
     * carrier's author has moved on, so its old offsets fall behind
     * the author's own chain and the window refuses them here long
     * before the byte would.  Current-or-next, read from the newest
     * key witnessed: one key behind covers in-flight traffic across a
     * rotation, two behind is unverifiable -- the author's key for it
     * is gone (O4).
     */
    if (w->sigOff / SigBudget + 1 < p->hiOff[w->from] / SigBudget) {
      ++KeyRejected;
      return;
    }
    if (w->sigOff > p->hiOff[w->from])
      p->hiOff[w->from] = w->sigOff;
    /*
     * O5's offset floor, three outcomes at the authored high-water:
     * above -- accept and record the asserted identity beside it;
     * equal with the same identity -- a re-presentation, BPR's normal
     * traffic; equal with a DIFFERENT identity -- a second authoring
     * of a one-time slot, refused (a defect here: nothing re-authors).
     * Below the high-water only the key window judges -- the record is
     * one deep, exactly the bound the reference design keeps.  Gated
     * on AUTHORED wires only: a mirror or a served end stamps its own
     * chain head without consuming, and a stamp asserts no authorship.
     */
    if (w->kind == WK_EXCH ? w->from == w->bankOf
                           : w->from == w->legServer) {
      if (!p->authSeen[w->from] || w->sigOff > p->authOff[w->from]) {
        p->authSeen[w->from] = 1;
        p->authOff[w->from] = w->sigOff;
        memcpy(p->authAnchor[w->from], w->idAnchor, ANCHOR);
      } else if (w->sigOff == p->authOff[w->from]
              && memcmp(w->idAnchor, p->authAnchor[w->from], ANCHOR)) {
        ++ReAuthorRejected;
        return;
      }
    }

    /*
     * Exchange and recovery traffic is TRAFFIC OF ITS ROUND (system.md,
     * carrier geometries): behind a receiver's frontier it classifies as
     * retained-round traffic and its tails carry possession evidence
     * like any other traffic of the round.  So both go through the
     * machine's evidence surfaces exactly as ACS traffic does -- a
     * server's leg wires and a member's exchange wires evidence their
     * possession, a served process's leg wires are want evidence, and
     * the same O1 inference applies.  One block for both carriers: the
     * classification is a property of the ROUND, not of the geometry.
     *
     * Done BEFORE either instance is touched: a release this provokes
     * frees every leg of the round, and taking the pointer first would
     * leave us walking freed storage.  DELIVER cannot misfire here
     * because its handler routes only WK_ACS wires.
     */
    /*
     * Cursor evidence (system.md Model, want): this caller owns the
     * authorship order -- the offset floor above IS the authored
     * high-water record -- so an AUTHORED wire riding the sender's
     * newest authored offset locates its cursor here.  A retry of an
     * older duty re-presents at its original offset, falls below the
     * high-water, and asserts nothing (the refuted stale-retry
     * inference, excluded by construction).
     */
    n = systemReceived(p->sys, rn((unsigned long)pos), w->from, w->possesses,
                       (w->kind == WK_EXCH ? w->from == w->bankOf
                                           : w->from == w->legServer)
                       && p->authSeen[w->from]
                       && w->sigOff == p->authOff[w->from] ? 1 : 0,
                       sa);
    applySysActs(p, sa, n, w);
    for (i = 0; i < RSPACE; ++i) {
      struct rslot *rs;

      rs = p->slot + i;
      if (!rs->inUse || rs->pos >= (unsigned long)pos)
        continue;                  /* not strictly behind the wire */
      if (!systemRetained(p->sys, rn(rs->pos)))
        continue;
      n = systemPossessed(p->sys, rn(rs->pos), w->from, sa);
      applySysActs(p, sa, n, 0);
    }
    if (w->possesses && w->from < MAX_PROCESSES) {
      if (systemRetained(p->sys, rn((unsigned long)pos))) {
        n = systemPossessed(p->sys, rn((unsigned long)pos), w->from, sa);
        applySysActs(p, sa, n, 0);
      } else if ((unsigned long)pos == p->fpos) {
        if (p->pendPos != p->fpos) {
          memset(p->pendF, 0, sizeof (p->pendF));
          p->pendPos = p->fpos;
        }
        p->pendF[w->from] = 1;
      }
    }

    if (w->kind == WK_EXCH) {
      if (w->bankOf >= N)
        return;
      if (!(ex = exchFind(p, (unsigned long)pos, w->bankOf))
       && !(ex = exchAlloc(p, (unsigned long)pos, w->bankOf, pa, p->sigOff)))
        return;
      if (ex->retired || !ex->f1)
        return;                    /* quiesced: nothing is owed anywhere */
      /*
       * The artifact's own offset, learnable from any wire the member
       * ITSELF sent (its exchange wires all ride the offset its round
       * contribution consumed).  Authorship expiry below reads it.
       */
      if (w->from == w->bankOf && !ex->artKnown) {
        ex->artOff = w->sigOff;
        ex->artKnown = 1;
      }
      /*
       * Pitfall 17: only the designated initiator may send INITIAL, and
       * an exchange's initiator is the member whose bank it carries.
       * Here the check is doing more than protecting the cascade -- it
       * is what binds content to its author, since without crypto the
       * authenticated sender is the only authorship evidence there is.
       */
      if (w->type == BRACHA87_INITIAL && w->from != w->bankOf)
        return;
      /* This process's masked copy rides the INITIAL alone (O4). */
      if (w->type == BRACHA87_INITIAL && w->sideHave && !ex->sideHave) {
        memcpy(ex->side, w->side, VLEN);
        memcpy(ex->stag, w->bankTag, ANCHOR);
        ex->sideHave = 1;
      }
      n = bracha87Fig1Input(ex->f1, w->type, w->from, w->value, out);
      if (n)
        p->active = 1;               /* a fresh cascade on a carrier of
                                      * this layer IS progress; a
                                      * duplicate retry returns 0 acts */
      for (k = 0; k < n; ++k) {
        if (out[k] == BRACHA87_ACCEPT) {
          const unsigned char *v;

          /*
           * Lemma 2 is the whole containment story for content: correct
           * processes that accept this instance accept the SAME bytes,
           * so an equivocating member cannot fork the round's content.
           */
          ex->selfAcc = 1;
          bracha87Fig1ProcessAccepted(ex->f1, p->self);
          if ((v = bracha87Fig1Value(ex->f1))) {
            unsigned char mk[ANCHOR];
            unsigned char clear[VLEN];

            /*
             * Content enters through the masked sidecar when one
             * arrived -- opened under the frozen step (or the live
             * walk pre-close) and then digest-validated like every
             * bank, so a mask drift is caught by the same O2 check
             * that catches corrupt content.  A process can echo its
             * way into an exchange without ever seeing the INITIAL
             * that carries its copy; the clear value plane then
             * stands in for reconstructing from other recipients'
             * shards, and the count keeps that fallback visible.
             */
            if (ex->sideHave
             && maskKeyAt(p, (unsigned long)pos, ex->member, mk)) {
              maskBytes(clear, ex->side, mk);
              if (recordBank(p, (unsigned long)pos, w->bankOf, clear,
                             ex->stag))
                ++ByExchange;
            } else {
              if (ex->sideHave)
                ++MaskFloorDrop;
              if (recordBank(p, (unsigned long)pos, w->bankOf, v, 0))
                ++ByPlane;
            }
          }
        } else
          exchEmit(p, ex, out[k]);
      }
      if (w->type == BRACHA87_READY && w->accepted)
        bracha87Fig1ProcessAccepted(ex->f1, w->from);
      return;
    }

    /* WK_SERVE */
    if (w->bankOf < N) {
      unsigned char mk[ANCHOR];
      unsigned char clear[VLEN];
      unsigned char other;

      /*
       * The bank a recovery leg replays.  Authorship is no longer
       * fiat: the member's origin tag travels with the bytes and
       * recordBank verifies it on this landing -- A9 attributes the
       * HOP, the tag attributes the ORIGIN across the relay, which is
       * exactly the split O2's self-validating fragments exist for.
       * With it, pre-close replayed content validates on ARRIVAL
       * instead of riding on trust until the close's digest.
       *
       * The bytes arrive in the SERVING FORM (O4): masked under the leg
       * pair's key at the round's step.  This end opens them with its
       * frozen step, or with the live walk while the round is still its
       * frontier -- the pre-close heal, which is exactly why the step
       * excludes the round's own anchor.  No key means this end's floor
       * passed the round before an assembly froze one: undecipherable,
       * priced as loss, which BPR carries until the server's own floor
       * retires the bank from the leg.
       */
      other = (unsigned char)(w->to == w->legServer ? w->legServed
                                                    : w->legServer);
      if (!maskKeyAt(p, (unsigned long)pos, other, mk))
        ++MaskFloorDrop;
      else {
        maskBytes(clear, w->value, mk);
        if (recordBank(p, (unsigned long)pos, w->bankOf, clear,
                       w->bankTag))
          ++ByRecovery;
      }
    }

    if (!(lg = legFind(p, w->legServer, w->legServed, (unsigned long)pos))) {
      struct rslot *ls;

      /*
       * Only the served end births its own leg on arrival, and only for
       * a round within reach; a server's leg is born by a SERVE act.
       *
       * And never for a round this process has already RELEASED: a
       * straggling leg wire arriving after the release would RE-BIRTH
       * the mirror into a released round -- a zombie no release will
       * ever free again (release fires once per incarnation).  Those
       * zombies accumulated two per round at n = 7 until LEGCAP starved
       * the legitimate heals and stranded a straggler out of reach.
       * The seam instrument's rule: a process that released the round
       * drops the INITIAL.  The line is RELEASE, not close -- a
       * closed-but-retained round still answers, because the server's
       * leg retires only on this end's accept, and a mirror refused
       * here would leave that leg holding one of the server's t cap
       * slots until the round releases everywhere, which under a
       * genuine straggler is never: two dead slots and the whole
       * cohort's serving starves.
       */
      if (w->to != w->legServed)
        return;
      if ((ls = slotOf(p, (unsigned long)pos)) && ls->closed
       && !systemRetained(p->sys, rn((unsigned long)pos)))
        return;
      if (!(lg = legAlloc(p, w->legServer, w->legServed, (unsigned long)pos,
                          pa, p->sigOff)))
        return;
    }
    if (lg->retired)
      return;
    legFrom = (unsigned char)(w->from == lg->server ? 0 : 1);
    /*
     * Pitfall 17, at the bare Fig 1 layer: only the designated
     * initiator may send INITIAL.  The leg's initiator is its server.
     */
    if (w->type == BRACHA87_INITIAL && legFrom != 0)
      return;
    n = bracha87Fig1Input(lg->f1, w->type, legFrom, w->comp, out);
    for (k = 0; k < n; ++k) {
      if (out[k] == BRACHA87_ACCEPT) {
        legAccept(p, lg);
        /*
         * legAccept banks possession, which can complete an all-n
         * record and drive a RELEASE whose handler frees every leg of
         * this round -- lg included.  Re-find before touching it again.
         */
        if (!(lg = legFind(p, w->legServer, w->legServed,
                           (unsigned long)pos)))
          return;
      } else
        legEmit(p, lg, out[k]);
    }
    if (w->type == BRACHA87_READY && w->accepted) {
      bracha87Fig1ProcessAccepted(lg->f1, legFrom);
      lg->otherAcc = 1;
    }
    if (lg->selfAcc && lg->otherAcc)
      lg->retired = 1;             /* remote evidence, never local send */
    return;
  }

  /* WK_ACS */
  if ((unsigned long)pos > p->fpos) {
    holdWire(p, w);                /* beyond reach: unverifiable, held */
    return;
  }
  /* The same identity and key gates the other carriers face. */
  if (!(pa = prevAnchorAt(p, (unsigned long)pos))
   || memcmp(w->idAnchor, pa, ANCHOR)) {
    ++IdRejected;
    return;
  }
  if (w->sigOff / SigBudget + 1 < p->hiOff[w->from] / SigBudget) {
    ++KeyRejected;
    return;
  }
  if (w->sigOff > p->hiOff[w->from])
    p->hiOff[w->from] = w->sigOff;
  /*
   * The offset floor again -- every ACS wire of a sender's round rides
   * the offset its contribution consumed, so all are authored.
   */
  if (!p->authSeen[w->from] || w->sigOff > p->authOff[w->from]) {
    p->authSeen[w->from] = 1;
    p->authOff[w->from] = w->sigOff;
    memcpy(p->authAnchor[w->from], w->idAnchor, ANCHOR);
  } else if (w->sigOff == p->authOff[w->from]
          && memcmp(w->idAnchor, p->authAnchor[w->from], ANCHOR)) {
    ++ReAuthorRejected;
    return;
  }

  /* Every ACS wire of a sender's round is authored (the offset floor
   * above), so cursor evidence is simply the high-water test. */
  n = systemReceived(p->sys, rn((unsigned long)pos), w->from, w->possesses,
                     p->authSeen[w->from]
                     && w->sigOff == p->authOff[w->from] ? 1 : 0,
                     sa);
  applySysActs(p, sa, n, w);

  /*
   * An indication riding LIVE-round traffic is dropped by the machine,
   * correctly: only retained rounds have a record.  system.h calls that
   * conservative because the O1 inference re-derives it from the
   * sender's later-round traffic once the round is retained -- but that
   * carrier only exists if a later round is ever launched.  When the
   * cohort runs out of application values, no later round is, and a
   * process that closes late never learns what its own live-round
   * traffic already told it.  The evidence is authenticated and true
   * when it arrives; it is only PREMATURE.  Hold it and re-present it
   * at the close.  Sound by the same containment as the indication
   * itself: it marks only its own sender.
   *
   * Held ONLY for the frontier POSITION, which is the only one that can
   * become retained at the next close -- and keyed by position
   * (pendPos), so a stale entry structurally cannot be re-presented at
   * the next incarnation of the byte.
   */
  if (w->possesses && w->from < MAX_PROCESSES) {
    if (systemRetained(p->sys, rn((unsigned long)pos))) {
      /* the round became retained while these acts were applied */
      n = systemPossessed(p->sys, rn((unsigned long)pos), w->from, sa);
      applySysActs(p, sa, n, 0);
    } else if ((unsigned long)pos == p->fpos) {
      if (p->pendPos != p->fpos) {
        memset(p->pendF, 0, sizeof (p->pendF));
        p->pendPos = p->fpos;
      }
      p->pendF[w->from] = 1;
    }
  }

  /*
   * O1's linkage inference: an authenticated act of round R+1 OR LATER
   * evidences its sender's possession of R's composition.  "R+1 alone"
   * is the reading that strands any round whose immediate successor
   * never arrives -- walk EVERY retained round behind this wire's.
   */
  for (i = 0; i < RSPACE; ++i) {
    struct rslot *rs;

    rs = p->slot + i;
    if (!rs->inUse || rs->pos >= (unsigned long)pos)
      continue;                    /* not strictly behind the wire */
    if (!systemRetained(p->sys, rn(rs->pos)))
      continue;
    n = systemPossessed(p->sys, rn(rs->pos), w->from, sa);
    applySysActs(p, sa, n, 0);
  }

  /*
   * Frontier traffic that produced no act found no live instance: it
   * recorded participation owed.  Hold it and re-present it after the
   * join, so the join's instance sees it without waiting a retry cycle.
   */
  if ((unsigned long)pos == p->fpos && !systemLive(p->sys))
    holdWire(p, w);
}

static void
refeedHeld(
  struct proc *p
){
  struct wire *snap;
  unsigned int cnt;
  unsigned int i;

  if (!(cnt = p->holdCnt))
    return;
  if (!(snap = calloc(cnt, sizeof (*snap))))
    return;
  memcpy(snap, p->hold, cnt * sizeof (*snap));
  p->holdCnt = 0;
  for (i = 0; i < cnt; ++i) {
    long hp;

    if ((hp = posOf(p, snap[i].sysRound)) < 0)
      continue;                    /* stale beyond the mapping: dropped */
    if ((unsigned long)hp > p->fpos
     || ((unsigned long)hp == p->fpos && !systemLive(p->sys)))
      holdWire(p, &snap[i]);       /* still unreachable / still no instance */
    else
      deliverWire(&snap[i]);
  }
  free(snap);
}

/*
 * Append this process's agreed-sequence positions, strictly in POSITION
 * order, for every closed round below 'through' whose content is in
 * hand.
 *
 * The gate is CONTENT, never retention.  Release is a decision about the
 * composition -- taken as soon as all n possess it, which the round's own
 * tails make fast -- while the content for that round is still crossing
 * on exchanges born at its close.  Keying the wait on retention therefore
 * emits holes for content that is merely in flight, which is what makes
 * the two grains' lifetimes genuinely independent (O2) rather than
 * nominally so.  'force' overrides the wait: at teardown the run is over,
 * and at the reuse guard the round's carriers are being abandoned --
 * either way what is still missing is missing for good, and the position
 * becomes an out-of-band hole.
 */
static void
seqEmit(
  struct proc *p
 ,unsigned long through
 ,unsigned int force
){
  struct rslot *s;
  unsigned int k;

  while (p->seqNext < through
      && (s = slotOf(p, p->seqNext)) && s->closed) {
    if (!force)
      for (k = 0; k < N; ++k)
        if (s->comp[ANCHOR + k] && !s->has[k])
          return;                  /* the sequence is ordered: stop here */
    for (k = 0; k < N; ++k) {
      if (!s->comp[ANCHOR + k])
        continue;
      if (p->seqCnt >= p->seqCap) {
        unsigned char (*ns)[VLEN];
        unsigned char *nh;
        unsigned int nc;

        nc = p->seqCap ? p->seqCap * 2 : 1024;
        if (!(ns = realloc(p->seq, nc * sizeof (*ns))))
          return;
        p->seq = ns;
        if (!(nh = realloc(p->seqHole, nc)))
          return;
        memset(nh + p->seqCap, 0, nc - p->seqCap);
        p->seqHole = nh;
        p->seqCap = nc;
      }
      if (s->has[k])
        memcpy(p->seq[p->seqCnt], s->content[k], VLEN);
      else
        p->seqHole[p->seqCnt] = 1;
      ++p->seqCnt;
    }
    ++p->seqNext;
  }
}

/*--------------------------------------------------------------------------*/
/*  Main                                                                    */
/*--------------------------------------------------------------------------*/

int
main(
  int argc
 ,char *argv[]
){
  unsigned int msgs;
  unsigned int seed;
  unsigned int origSeed;
  unsigned int tick;
  unsigned int idle;
  unsigned int i;
  unsigned int j;
  unsigned int k;
  int arg;
  int exitCode;
  unsigned long sysSz;

  unsigned int seqOk;
  unsigned int onceOk;
  unsigned int holes;

  Verbose = 0;
  DropPct = 4;
  /*
   * The budgets are counted in SWEEPS, so they are sized against how
   * many sweeps a round costs -- and a round costs more sweeps with
   * three carriers in flight than with two.  This default is measured,
   * not chosen: at 30 the laggard heal lapses its progress budget in 24
   * of 32 (seed, process) observations while every verdict still passes,
   * which is a budget too tight for the tempo, not a strand.  Held at
   * 100 across the wrap: the 300-value runs classify nowhere at the
   * default loss.
   */
  Tp = 100;
  Sp = 0;
  MaintEvery = 0;
  seed = 1;
  exitCode = 0;

  arg = 1;
  while (arg < argc && argv[arg][0] == '-') {
    if (argv[arg][1] == 'v' && argv[arg][2] == '\0') {
      ++Verbose;
      ++arg;
    } else if (argv[arg][1] == 's' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      seed = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'l' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      DropPct = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'm' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      MaintEvery = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'o' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      SigBudget = (unsigned long)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'T' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      Tp = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'S' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      Sp = (unsigned int)atoi(argv[arg++]);
    } else if (argv[arg][1] == 'L' && argv[arg][2] == '\0') {
      char *c;

      if (++arg >= argc) goto usage;
      LagProc = atoi(argv[arg]);
      if (!(c = strchr(argv[arg], ':')))
        goto usage;
      LagRound = atoi(c + 1);
      ++arg;
    } else if (argv[arg][1] == 'M' && argv[arg][2] == '\0') {
      if (++arg >= argc) goto usage;
      MuteProc = atoi(argv[arg++]);
    } else if (argv[arg][1] == 'B' && argv[arg][2] == '\0') {
      char *c;

      if (++arg >= argc) goto usage;
      ByzProc = atoi(argv[arg]);
      ByzMode = (c = strchr(argv[arg], ':'))
              ? (unsigned int)atoi(c + 1)
              : (BYZ_POSSESS | BYZ_CAND);
      ++arg;
    } else
      goto usage;
  }

  if (argc - arg < 4) goto usage;
  N = (unsigned int)atoi(argv[arg++]);
  T = (unsigned int)atoi(argv[arg++]);
  R = (unsigned int)atoi(argv[arg++]);
  msgs = (unsigned int)atoi(argv[arg++]);

  if (N < 2 || N > MAX_PROCESSES) {
    fprintf(stderr, "n must be 2..%d\n", MAX_PROCESSES);
    return (1);
  }
  if (N < 3 * T + 1) {
    fprintf(stderr, "need n >= 3t + 1 (n=%u, t=%u)\n", N, T);
    return (1);
  }
  if (R < 1 || R > MAX_REACH) {
    fprintf(stderr, "reach must be 1..%d\n", MAX_REACH);
    return (1);
  }
  if (msgs < 1 || msgs > MAX_STAGED) {
    fprintf(stderr, "msgs must be 1..%d\n", MAX_STAGED);
    return (1);
  }
  if (DropPct > 90) {
    fprintf(stderr, "loss must be 0..90\n");
    return (1);
  }
  if (SigBudget < 16) {
    fprintf(stderr, "-o budget must be >= 16 (the rotation margin needs"
                    " room inside one key)\n");
    return (1);
  }
  /*
   * The rotation margin: maintenance goes due this many offsets before
   * the key exhausts, sized to cover the launches spent while the
   * rotation round itself wins -- exclusion can cost a few retries at
   * t >= 1.  Tuning, like every budget here.
   */
  SigMargin = SigBudget / 8;
  if (SigMargin < 2)
    SigMargin = 2;
  /*
   * S > T_p is required, not tuned (system.md R4): the progress budget
   * must outlast the duty budget, or a process would abandon a round it
   * is still legitimately holding for.
   */
  if (!Sp)
    Sp = 2 * Tp;
  if (Sp <= Tp) {
    fprintf(stderr, "need S > Tp (S=%u, Tp=%u)\n", Sp, Tp);
    return (1);
  }
  if (LagProc >= 0 && ((unsigned int)LagProc >= N || LagRound < 0)) {
    fprintf(stderr, "-L proc:round out of range\n");
    return (1);
  }
  if (MuteProc >= 0) {
    if ((unsigned int)MuteProc >= N) {
      fprintf(stderr, "-M proc out of range\n");
      return (1);
    }
    if (!T) {
      fprintf(stderr, "-M needs t >= 1 (a silent process is a fault)\n");
      return (1);
    }
    /* Same budget arithmetic as -B with -L: each is a fault. */
    if ((MuteProc == LagProc || MuteProc == ByzProc)
     || ((LagProc >= 0 || ByzProc >= 0) && T < 2)) {
      fprintf(stderr, "-M with -L or -B is two faults: needs t >= 2 and"
                      " distinct processes\n");
      return (1);
    }
  }
  if (ByzProc >= 0) {
    if ((unsigned int)ByzProc >= N) {
      fprintf(stderr, "-B proc out of range\n");
      return (1);
    }
    if (!T) {
      fprintf(stderr, "-B needs t >= 1 (one Byzantine process must fit"
                      " inside the fault budget)\n");
      return (1);
    }
    if (!ByzMode || (ByzMode & ~(unsigned int)(BYZ_POSSESS | BYZ_WANT
                                             | BYZ_CAND | BYZ_LINK))) {
      fprintf(stderr, "-B mode is a mask of 1 (forge possession),"
                      " 2 (forge want), 4 (fake candidate),"
                      " 8 (unchained candidate)\n");
      return (1);
    }
    if ((ByzMode & BYZ_POSSESS) && (ByzMode & BYZ_WANT)) {
      fprintf(stderr, "-B modes 1 and 2 are contradictory lies about the"
                      " same bit\n");
      return (1);
    }
    if (ByzProc == LagProc) {
      fprintf(stderr, "-B and -L must name different processes\n");
      return (1);
    }
    /*
     * A partitioned process is not distinguishable from a faulty one
     * under unbounded latency, so R4 charges it to the same budget the
     * Byzantine process is charged to.  Running both at t = 1 asks a
     * system to tolerate two faults; it will correctly refuse to
     * advance, which is the model, not a defect.
     */
    if (LagProc >= 0 && T < 2) {
      fprintf(stderr, "-B with -L is two faults: needs t >= 2"
                      " (a partitioned process spends the same budget)\n");
      return (1);
    }
  }

  origSeed = seed;
  Rng = seed ? seed : 1;

  /*
   * A11's out-of-band provisioning: the delivery that seeds the common
   * chain base also seeds the pairwise keys.  Symmetric demo bytes.
   */
  for (i = 0; i < MAX_PROCESSES; ++i)
    for (j = i; j < MAX_PROCESSES; ++j) {
      unsigned char pair[2];

      pair[0] = (unsigned char)i;
      pair[1] = (unsigned char)j;
      mixTag(Psk[i][j], Genesis, pair, sizeof (pair));
      memcpy(Psk[j][i], Psk[i][j], ANCHOR);
    }
  AcsSz = bkr94acsSz(N - 1, VLEN - 1, MAX_PHASES);
  LegSz = bracha87Fig1Sz(1, COMPLEN - 1);
  ExchSz = bracha87Fig1Sz(N - 1, VLEN - 1);
  sysSz = systemSz(N - 1, SYS_RS);

  if (!(Queue = calloc(QCAP, sizeof (*Queue)))) {
    fprintf(stderr, "queue allocation failed\n");
    return (1);
  }

  for (i = 0; i < N; ++i) {
    struct proc *p;

    p = &Proc[i];
    p->self = (unsigned char)i;
    if (!(p->sys = calloc(1, sysSz))
     || !(p->store = calloc(1, systemStoreSz(N - 1, SYS_RS, R)))
     || !(p->slot = calloc(RSPACE, sizeof (*p->slot)))
     || !(p->exch = calloc(RSPACE * N, sizeof (*p->exch)))
     || !(p->hold = calloc(HOLDCAP, sizeof (*p->hold)))
     || !(p->msg = calloc(msgs, sizeof (*p->msg)))) {
      fprintf(stderr, "allocation failed\n");
      exitCode = 1;
      goto cleanup;
    }
    /*
     * The retained rounds and their records are THIS FILE's now
     * (system.h, the retention operations), answered by the reference
     * store: -w is the deployment's declared reach, and the store
     * refusing at it is how the reach binds.  systemInit hands ONE ctx
     * to the comparator and to all four operations, so the store IS
     * the ctx and systemStoreCmp forwards to ordCmp.
     */
    systemStoreInit(p->store, (unsigned char)(N - 1), SYS_RS, R, ordCmp, 0);
    systemInit(p->sys, (unsigned char)(N - 1), (unsigned char)T,
               (unsigned char)i, SYS_RS, rn(0), systemStoreCmp,
               systemStoreRecords, systemStoreRetain, systemStoreRelease,
               systemStoreAfter, p->store);
    /*
     * Stage this process's application messages: the layer has ACCEPTED
     * them, so from here each is an obligation, not a preference (R1).
     * Crossing the round-byte wrap is a STAGING requirement, not a
     * tick-count one -- ADMIT consumes one value per round, so a run
     * only reuses a byte when more than RSPACE values push it past.
     */
    p->msgCnt = msgs;
    for (j = 0; j < msgs; ++j)
      sprintf((char *)p->msg[j], "p%um%u", i, j);
    /*
     * O4's mask-chain base rides the same delivery (A11): self's
     * pairwise row at the genesis floor, domain-separated from the A9
     * tag keys by the trailing byte -- a tag key authenticates a hop
     * forever, a mask row is the head of a forward-only chain and is
     * destroyed behind the floor.
     */
    for (j = 0; j < MAX_PROCESSES; ++j) {
      unsigned char pair[3];

      pair[0] = (unsigned char)(i < j ? i : j);
      pair[1] = (unsigned char)(i < j ? j : i);
      pair[2] = 'M';
      mixTag(p->maskKey[j], Genesis, pair, sizeof (pair));
    }
  }
  /* The members' public origin keys ride the same bundle (A11/O2). */
  for (i = 0; i < MAX_PROCESSES; ++i) {
    unsigned char who[2];

    who[0] = (unsigned char)i;
    who[1] = 'S';
    mixTag(BankKey[i], Genesis, who, sizeof (who));
  }

  printf("--- system layer over BKR94 ACS "
         "(n=%u, t=%u, r=%u, msgs/process=%u, loss=%u%%, seed=%u"
         ", Tp=%u, S=%u", N, T, R, msgs, DropPct, origSeed, Tp, Sp);
  if (MaintEvery)
    printf(", maintenance every %u", MaintEvery);
  if (LagProc >= 0)
    printf(", cut process %d at round %d", LagProc, LagRound);
  if (MuteProc >= 0)
    printf(", mute process %d", MuteProc);
  printf(") ---\n\n");

  /*----------------------------------------------------------------------*/
  /*  The tick loop                                                        */
  /*                                                                      */
  /*  The tick is a wire rate limit, never a correctness clock: nothing    */
  /*  below reads elapsed time, and every budget is counted in this        */
  /*  process's OWN sweeps.                                                */
  /*----------------------------------------------------------------------*/

  idle = 0;
  for (tick = 0; tick < MAX_TICKS; ++tick) {
    struct wire w;
    unsigned int done;
    unsigned int fresh;

    fresh = 0;

    for (k = 0; k < DRAIN && qPopRandom(&w); ++k)
      deliverWire(&w);

    for (i = 0; i < N; ++i) {
      struct proc *p;
      struct systemAct sa[SYSTEM_MAX_ACTS];
      struct bkr94acsAct out[BKR94ACS_RETRY_MAX_ACTS];
      unsigned int duty;
      unsigned int drained;
      unsigned int n;

      p = &Proc[i];
      fresh |= p->active;          /* the drain's deliveries, banked for
                                    * the exit gate before the sweep's
                                    * own meter resets the flag */
      p->active = 0;

      /*
       * The floor follows the machine's pruning (O4): fold the rows
       * forward over every round that has left the retained set since
       * the last sweep, destroying the steps behind it.
       */
      maskAdvance(p);

      refeedHeld(p);

      duty = systemDuty(p->sys);

      /*
       * R4, the caller's half: T_p is a count of THIS process's own
       * sweeps since the class first read TOLERANCE, reset when the
       * frontier advances.  It is self-local by construction -- no
       * adversary can hold it shut or force it open (axiom A6).
       */
      if (duty == SYSTEM_DUTY_TOLERANCE)
        ++p->tolCount;
      else
        p->tolCount = 0;
      p->tolElapsed = (unsigned char)(duty == SYSTEM_DUTY_TOLERANCE
                                   && p->tolCount >= Tp);

      /*
       * The clock's two hands reach systemLaunch here.  The TICK is the
       * round instance's COMPLETE, which is what advanced the frontier
       * and is what the R4 duty class above reads.  The TOCK is the
       * content landing, and it enters as M2's capacity gate -- so a
       * process whose content plane is behind stops ADMITTING while it
       * goes on JOINING, which is the split system.h draws ("the join
       * remains ungated on the backlog -- capacity may defer only
       * chosen work").  Participation never waits on a carrier; only
       * presentation does.  That is what keeps the two planes' liveness
       * uncoupled while still letting the slower one push back.
       */
      drained = backlogDrained(p);
      if (!drained && p->msgHead < p->msgCnt)
        ++BacklogHolds;            /* a value was pending and capacity was
                                    * closed.  Not the same as an admission
                                    * lost -- owed participation and
                                    * maintenance both outrank the value and
                                    * are ungated -- so this counts the gate
                                    * BINDING, not what it displaced */
      /*
       * Maintenance is due on the BUDGET (O5: exhaustion forces an
       * identity-maintenance round): inside the rotation margin of an
       * uninstalled next key, or -- urgently -- already signing on an
       * uninstalled key (the overdraft posture: every launch until the
       * rotation lands is another maintenance round).  The -m cadence
       * stays beside it as the demonstration knob it always was.
       */
      {
        unsigned int maintDue;

        maintDue = (unsigned int)(MaintEvery && p->fpos
                               && (p->fpos % MaintEvery) == 0);
        if (p->rotKey < p->sigOff / SigBudget
         || (p->rotKey == p->sigOff / SigBudget
          && SigBudget - p->sigOff % SigBudget <= SigMargin))
          maintDue = 1;
        n = systemLaunch(p->sys,
                         (unsigned char)(p->msgHead < p->msgCnt),
                         (unsigned char)maintDue,
                         (unsigned char)drained,
                         p->tolElapsed,
                         sa);
      }
      applySysActs(p, sa, n, 0);

      /*
       * Discharge an adoption debt AFTER the tick's own launch: the
       * close needs a live instance (systemComplete is inert without
       * one), so JOIN first, then re-read the frontier and close
       * through the one consume region.
       *
       * In THIS glue the instance is always already live: want evidence
       * is born from acts of round R, so a process being served R has
       * launched R.  The JOIN below is the path for a caller whose
       * recovery traffic reaches systemReceived and records
       * participation owed -- system.h's stated ADOPT shape, kept here
       * because a deployment copying this file needs it.
       */
      if (p->adoptPending && p->candValid) {
        if (!systemLive(p->sys)) {
          n = systemLaunch(p->sys, 0, 0, 1, p->tolElapsed, sa);
          applySysActs(p, sa, n, 0);
        }
        if (systemLive(p->sys)) {
          unsigned char cand[COMPLEN];
          unsigned char wit[(MAX_PROCESSES + 7) / 8];
          const unsigned char *wp;
          unsigned long adRound;
          unsigned long adPos;

          adPos = p->fpos;
          adRound = rv(systemFrontier(p->sys));
          memcpy(cand, p->cand, COMPLEN);
          /*
           * Snapshot the witness book BEFORE the close -- the close is
           * the one consume region and clears it.  Every witness SERVED
           * this composition, and a server has by construction closed
           * and retained the round, so each is the strongest possession
           * evidence there is.  Banking it is not optional bookkeeping:
           * an adopted round is born possessed by self alone, and until
           * the record reaches n-t the NEXT frontier reads HELD.  With
           * the cohort quiesced there is no later-round traffic for the
           * O1 inference to ride, so a healed process would stall one
           * round after healing.  At t = 1 the witnesses plus self reach
           * n-t exactly -- the TOLERANCE escape.  At t >= 2 they fall
           * one short (t+2 < 2t+1) and the remainder arrives on the
           * adopted round's OWN exchange, born at this close: its tails
           * are possession-bearing round-R traffic from every
           * responder, and the record climbs from there.
           */
          memset(wit, 0, sizeof (wit));
          if ((wp = systemWitnesses(p->sys)))
            memcpy(wit, wp, sizeof (wit));
          p->adoptPending = 0;
          ++p->adopts;
          if (Verbose)
            printf("process %u: ADOPT round %lu from served evidence\n",
                   (unsigned)p->self, adPos);
          sysClose(p, slotOf(p, adPos), cand);
          if (systemRetained(p->sys, rn(adRound)))
            for (j = 0; j < N; ++j) {
              if (j == p->self || !SYSTEM_TST(wit, j))
                continue;
              n = systemPossessed(p->sys, rn(adRound), (unsigned char)j, sa);
              applySysActs(p, sa, n, 0);
            }
        }
      }

      /* ACS tails: one instance per tick, round-robin (the flood rule). */
      for (j = 0; j < RSPACE; ++j) {
        struct rslot *s;

        s = p->slot + (p->retryCursor + j) % RSPACE;
        if (!s->inUse || !s->acs)
          continue;
        n = bkr94acsRetry(s->acs, &s->cur, out);
        emitAcs(p, s, out, n);
        p->retryCursor = (unsigned int)((s->pos + 1) % RSPACE);
        break;
      }

      /* The deferred close, re-attempted (see sysTryComplete). */
      {
        struct rslot *s;

        if ((s = slotOf(p, p->fpos)))
          sysTryComplete(p, s);
      }

      /*
       * Leg tails: BPR on every live leg -- unless the leg's signing
       * key has fallen two behind this process's own head.  That is
       * O4's forward-only derivation read from the sender: the
       * material to re-derive that signature is gone, so no retry of
       * it can ever verify anywhere again -- the INNER (verification)
       * deadline, arriving well before the byte-reuse guard's OUTER
       * (naming) one.
       */
      for (j = 0; j < LEGCAP; ++j) {
        struct leg *lg;
        unsigned char legOut[BRACHA87_FIG1_RETRY_MAX_ACTS];
        unsigned int m;

        lg = &p->leg[j];
        if (!lg->inUse || lg->retired || !lg->f1)
          continue;
        if (lg->off / SigBudget + 1 < p->sigOff / SigBudget) {
          ++KeyExpiredLeg;
          legFree(lg);
          continue;
        }
        n = bracha87Fig1Bpr(lg->f1, legOut);
        for (m = 0; m < n; ++m)
          legEmit(p, lg, legOut[m]);
      }

      /*
       * Exchange tails: BPR on every live exchange.  UNPACED, and the
       * reason it can be is the round space itself: the fleet is
       * bounded by the two deadlines -- the key window's expiry below
       * (the inner, which in practice empties it) and the guard's
       * abandonment at the byte's reuse (the outer: at most one live
       * incarnation per (byte, member), the slot array's own shape).
       * The 256-round space IS the pacing constant; anything beyond
       * that is a memory limit (QCAP), where a full queue drops and a
       * drop is loss, which BPR already carries.  An earlier revision
       * paced this walk by hand -- that was compensating for a fleet
       * the missing expiry let grow without bound, not a need of the
       * walk's.
       *
       * A zero return with this process already accepted is the
       * all-accepted quiescence -- nothing of this instance is owed
       * anywhere, so it retires.  Any other zero (neither initiated
       * nor echoed) is simply idle and must stay alive to be echoed
       * into later.  The retirement is an OPPORTUNITY, not a
       * lifecycle: all-n is unreachable at t >= 1.
       */
      for (j = 0; j < RSPACE * N; ++j) {
        struct exch *ex;
        unsigned char exOut[BRACHA87_FIG1_RETRY_MAX_ACTS];
        unsigned int m;

        ex = &p->exch[j];
        if (!ex->inUse || ex->retired || !ex->f1)
          continue;
        /*
         * Two expiry arms, one per chain.  The AUTHORSHIP arm: the
         * member's witnessed chain is two keys past the artifact, so
         * the member can no longer be carrying it -- this content can
         * never arrive again and the capacity gate must read it as
         * unreachable, or one mid-delivery expiry at the author jams
         * every gate downstream forever.  The ATTRIBUTION arm: this
         * instance is two keys stale in THIS process's own chain --
         * the sender-side O4 expiry, as for the legs.  Either way
         * the slot keeps its key (retired) so a straggling wire
         * cannot re-birth the instance; the byte-reuse guard
         * reclaims it.
         */
        if ((ex->artKnown
          && ex->artOff / SigBudget + 1 < p->hiOff[ex->member] / SigBudget)
         || ex->off / SigBudget + 1 < p->sigOff / SigBudget) {
          ++KeyExpiredExch;
          free(ex->f1);
          ex->f1 = 0;
          ex->retired = 1;
          continue;
        }
        n = bracha87Fig1Bpr(ex->f1, exOut);
        if (!n) {
          if (ex->selfAcc)
            exchRetire(ex);
          continue;
        }
        for (m = 0; m < n; ++m)
          exchEmit(p, ex, exOut[m]);
      }

      /* The serve walk -- one owed round per tick, cursor is ours. */
      n = systemServe(p->sys, p->serveCursor, sa);
      applySysActs(p, sa, n, 0);

      /*
       * Late assembly (O2) has no sweep of its own: content arrives on
       * an exchange accept or a recovery leg, and recordBank reports it
       * to the machine at that instant.  What a sweep WOULD have to do
       * is report content that landed before its round closed -- an
       * exchange for a round we have not closed cannot exist, but a
       * recovery leg can replay one, and the close's own have grain
       * picks that up.
       */

      /* Retention never exceeds the reach -- bounded memory, one column. */
      {
        unsigned int ret;

        ret = 0;
        for (j = 0; j < RSPACE; ++j)
          if (p->slot[j].inUse && systemRetained(p->sys, rn(p->slot[j].pos)))
            ++ret;
        if (ret > p->maxRetained)
          p->maxRetained = ret;
      }

      /*
       * The barren-sweep meter: the S budget, counted in this process's
       * own sweeps.  A process is PARTITIONED by DEFAULT -- it is the
       * state it cannot disprove -- and the lapse of this budget is the
       * proof of participation expiring, not a network condition being
       * read.  No process ever classifies another, and the classified
       * process keeps stepping: its blind re-offers at the stale cursor
       * ARE the want evidence servers observe.
       */
      if (p->active
       || duty == SYSTEM_DUTY_TOLERANCE
       || !(p->msgHead < p->msgCnt || systemLive(p->sys)
         || systemOwed(p->sys)))
        p->barren = 0;             /* Neither of these is barren.  A
                                    * process with nothing to send is a
                                    * participant, and so is one holding
                                    * under TOLERANCE -- that hold is
                                    * funded catch-up time for the tail,
                                    * not idleness (R4).  A HELD strand
                                    * is the case that does accrue: it
                                    * reads no progress evidence and
                                    * proves no participation. */
      else if (++p->barren >= Sp) {
        if (!p->partitioned && Verbose)
          printf("process %u: self-classified PARTITIONED at round %u"
                 " (barren sweeps exhausted)\n",
                 (unsigned)p->self, (unsigned)rv(systemFrontier(p->sys)));
        p->partitioned = 1;
        p->barren = 0;             /* re-arm: the budget meters each
                                    * lapse, it does not latch */
        /*
         * The lapse ENDS at the classification.  It is tempting to act
         * on a HELD frontier by evicting the round below -- duty is
         * bounded by retention, so a released round reads MET -- and
         * that is wrong twice over.  R4 gives HELD no budget escape by
         * design (only TOLERANCE has one, and T_p is it), and
         * systemEvict is the artifact BYTE budget's actuator, not a
         * lever for shedding duty.  Doing it anyway drops rounds that
         * correct processes are still being served: it frees the
         * retained record a heal is accumulating witnesses against, and
         * releasing below t+1 surviving retainers makes the witness
         * ground unreachable forever.  The classified process keeps
         * stepping instead -- its blind re-offers at the stale cursor
         * ARE the want evidence servers observe.
         */
      }
    }

    for (i = 0; i < N; ++i)
      seqEmit(&Proc[i], Proc[i].fpos, 0);

    /*
     * The harness's own abandonment policy, and it CANNOT be carrier
     * quiescence: retirement is all-n (the header's clock section), so
     * at t >= 1 a handful of exchange tails legitimately retry their
     * unanswered processes forever and the queue never falls silent.
     * The gate is the barren-sweep discipline instead -- every correct
     * process's staged values retired, then IDLE_STOP ticks in which no
     * process banked a fresh act (duplicate retries return 0 acts and
     * move nothing).  MAX_TICKS remains the hard backstop.
     */
    done = 1;
    for (i = 0; i < N; ++i)
      if ((int)i != ByzProc && (int)i != MuteProc
       && Proc[i].msgHead < Proc[i].msgCnt)
        done = 0;
    for (i = 0; i < N; ++i)
      fresh |= Proc[i].active;
    if (done && !fresh) {
      if (++idle >= IDLE_STOP)
        break;
    } else
      idle = 0;
  }

  /*----------------------------------------------------------------------*/
  /*  Results                                                             */
  /*----------------------------------------------------------------------*/

  for (i = 0; i < N; ++i)
    seqEmit(&Proc[i], Proc[i].fpos, 1);

  printf("\n--- Agreed sequences ---\n");
  for (i = 0; i < N; ++i) {
    struct proc *p;

    p = &Proc[i];
    printf("process %u%s (frontier %lu, duty %s, staged %u/%u,"
           " adoptions %u, max retained %u):\n",
           i, (int)i == ByzProc ? " [BYZANTINE]" : "",
           p->fpos,
           systemDuty(p->sys) == SYSTEM_DUTY_MET ? "MET"
         : systemDuty(p->sys) == SYSTEM_DUTY_TOLERANCE ? "TOLERANCE" : "HELD",
           (unsigned)p->msgHead, (unsigned)p->msgCnt,
           p->adopts, p->maxRetained);
    printf("  ");
    /* A long sequence is elided, not dumped -- the verdicts read it all. */
    for (j = 0; j < p->seqCnt; ++j) {
      if (p->seqCnt > 24 && j == 12) {
        printf(" ... %u positions ...", p->seqCnt - 24);
        j = p->seqCnt - 12 - 1;
        continue;
      }
      printf("%s%s", j ? " " : "",
             p->seqHole[j] ? "<out-of-band>" : (const char *)p->seq[j]);
    }
    printf("\n");
  }

  /*
   * Verdict 1 -- sequence identity at every position both hold (L6).
   * Compared PAIRWISE, not against one baseline: with a baseline, two
   * processes could disagree at a position the baseline happens to hold
   * as a hole and nothing would notice.
   */
  seqOk = 1;
  for (i = 0; i < N; ++i) {
    if ((int)i == ByzProc)
      continue;                    /* every guarantee is over CORRECT
                                    * processes; a Byzantine process's
                                    * own state proves nothing */
    for (j = i + 1; j < N; ++j) {
      unsigned int m;

      if ((int)j == ByzProc)
        continue;
      for (m = 0; m < Proc[i].seqCnt && m < Proc[j].seqCnt; ++m) {
        if (Proc[i].seqHole[m] || Proc[j].seqHole[m])
          continue;                /* a position one of them does not hold */
        if (memcmp(Proc[i].seq[m], Proc[j].seq[m], VLEN))
          seqOk = 0;
      }
    }
  }

  /* Verdict 2 -- exactly-once (R1), over correct processes. */
  onceOk = 1;
  for (i = 0; i < N; ++i) {
    struct proc *p;

    if ((int)i == ByzProc)
      continue;
    p = &Proc[i];
    for (j = 0; j < p->msgCnt; ++j) {
      unsigned int seen;

      seen = 0;
      for (k = 0; k < p->seqCnt; ++k)
        if (!p->seqHole[k] && !memcmp(p->seq[k], p->msg[j], VLEN))
          ++seen;
      if (seen != 1) {
        onceOk = 0;
        printf("process %u: \"%s\" appears %u times (want exactly 1)\n",
               i, (const char *)p->msg[j], seen);
      }
    }
  }

  holes = 0;
  for (i = 0; i < N; ++i)
    for (j = 0; j < Proc[i].seqCnt && (int)i != ByzProc; ++j)
      if (Proc[i].seqHole[j])
        ++holes;

  printf("\n--- Verdicts ---\n");
  printf("sequence identity (L6, positions both hold): %s\n",
         seqOk ? "ok" : "FAIL");
  printf("exactly-once presentation (R1):              %s\n",
         onceOk ? "ok" : "FAIL");
  if (ByzProc >= 0) {
    unsigned int compOk;

    /*
     * Byzantine containment.  The sharpest single check the system
     * layer's own notes license: no correct process ever HOLDS a
     * composition another correct process disagrees with at the same
     * round.  A fabricated assertion that reached adoption anywhere
     * would show up here as two correct processes holding different
     * bytes for one round, and it would then also fork the sequence.
     */
    compOk = 1;
    for (i = 0; i < N; ++i) {
      if ((int)i == ByzProc)
        continue;
      for (j = i + 1; j < N; ++j) {
        if ((int)j == ByzProc)
          continue;
        /*
         * Compared over the slots both still hold -- a slot survives
         * until its byte's reuse, so below RSPACE rounds this is every
         * round there was; past the wrap it is the last round space,
         * and history before that is covered by the sequence verdict.
         */
        for (k = 0; k < RSPACE; ++k) {
          struct rslot *sA;
          struct rslot *sB;

          sA = Proc[i].slot + k;
          sB = Proc[j].slot + k;
          if (sA->inUse && sB->inUse && sA->pos == sB->pos
           && sA->closed && sB->closed
           && memcmp(sA->comp, sB->comp, COMPLEN))
            compOk = 0;
        }
      }
    }
    printf("Byzantine containment (process %d, mode %u):   %s\n",
           ByzProc, ByzMode, compOk ? "ok" : "FAIL");
    if (!compOk)
      exitCode = 1;
    /*
     * Say whether the arm was EXERCISED.  A containment verdict that
     * passes because the attack never happened proves nothing, and
     * silence about that is how a demonstration comes to lie.
     */
    if (ByzMode & (BYZ_CAND | BYZ_LINK))
      printf("  fabricated compositions served: %u; refused outright by the"
             " fold ground: %u;\n  reaching the book, so needing the t+1"
             " grouping to discriminate: %u%s\n",
             ByzAsserts, CandRejected, CandConflicts,
             ByzAsserts ? "" : "  <- attack never reached a served leg");
  }
  if (LagProc >= 0) {
    unsigned long maxF;
    unsigned int healed;

    /*
     * "Rejoined" must be TESTED, not asserted.  Retiring the staged
     * values alone passes vacuously in any config whose values all land
     * before the cut round -- the cut is then never exercised and the
     * process was never behind.  Require that the cut round was
     * actually reached, that an adoption happened (the cut is total for
     * that round, so it can close no other way), and that the frontier
     * caught up with the cohort.
     */
    maxF = 0;
    for (i = 0; i < N; ++i)
      if (Proc[i].fpos > maxF)
        maxF = Proc[i].fpos;
    if ((unsigned long)LagRound >= maxF) {
      printf("cut process %d healed and rejoined:           "
             "not exercised (round %d never reached)\n", LagProc, LagRound);
    } else {
      healed = (unsigned int)(Proc[LagProc].msgHead == Proc[LagProc].msgCnt
                           && Proc[LagProc].adopts >= 1
                           && Proc[LagProc].fpos == maxF);
      printf("cut process %d healed and rejoined:           %s"
             " (%u adoption%s, frontier %lu of %lu)\n",
             LagProc, healed ? "ok" : "FAIL", Proc[LagProc].adopts,
             Proc[LagProc].adopts == 1 ? "" : "s",
             Proc[LagProc].fpos, maxF);
      if (!healed)
        exitCode = 1;
    }
  }
  {
    unsigned int boundOk;

    boundOk = 1;
    for (i = 0; i < N; ++i)
      if (Proc[i].maxRetained > R)
        boundOk = 0;
    printf("retention bounded by the reach (%u):          %s\n",
           R, boundOk ? "ok" : "FAIL");
    if (!boundOk)
      exitCode = 1;
  }
  printf("exchanges: %u birthed, %u retired at all-accepted quiescence"
         " (an all-n fact: certain only at t = 0)\n",
         ExchBirthed, ExchRetired);
  printf("banks delivered: %u by exchange sidecar, %u from the value"
         " plane (the reconstruction stand-in), %u replayed by recovery%s\n",
         ByExchange, ByPlane, ByRecovery,
         ByRecovery ? "" : "  <- the replay was never exercised");
  printf("sweeps with a value pending and capacity closed (M2): %u%s\n",
         BacklogHolds, BacklogHolds ? "" : "  <- the tock never pushed back");
  /*
   * The wrap, reported like every other exercised-arm note: a guard
   * that never fires is a claim, and a run that never reuses a byte
   * demonstrated nothing about the tail.
   */
  {
    unsigned long maxP;

    maxP = 0;
    for (i = 0; i < N; ++i)
      if (Proc[i].fpos > maxP)
        maxP = Proc[i].fpos;
    printf("round space: max position %lu in a byte space of %u%s\n",
           maxP, RSPACE,
           maxP > RSPACE ? " -- the round byte WRAPPED"
                         : "  <- the wrap was never crossed");
    printf("byte-reuse guard: %u launch(es) reused a byte, abandoning"
           " %u exchange(s) and %u leg(s)%s\n",
           GuardFires, AbandonedExch, AbandonedLeg,
           GuardFires ? "" : "  <- the guard never fired");
    printf("stale-incarnation wires refused by identity: %u"
           " (a byte routes, an identity proves)\n", IdRejected);
    printf("signature chain (budget %lu/key): %u rotation(s) installed,"
           " %u overdraft(s)%s\n",
           SigBudget, Rotations, Overdrafts,
           Overdrafts ? "  <- rotation missed the current-or-next window"
                      : "");
    printf("key expiry: %u exchange(s) and %u leg(s) retired two keys"
           " stale (sender side),\n  %u wire(s) refused outside the"
           " current-or-next window (receiver side)%s\n",
           KeyExpiredExch, KeyExpiredLeg, KeyRejected,
           KeyExpiredExch || KeyExpiredLeg || KeyRejected
             ? "" : "  <- expiry never exercised");
    printf("serving form (O4): %u bank(s) re-masked from the floor onto"
           " legs,\n  %u serve(s) refused behind the floor, %u arrival(s)"
           " undecipherable%s\n",
           ServeMasked, ServeFloorHole, MaskFloorDrop,
           ServeFloorHole || MaskFloorDrop
             ? "  <- a release gate no longer bounds the serving path"
             : "  (tripwires: dormant while the release gates hold)");
    printf("fold ground (O3/A10): %u link-only witness(es) -- servers"
           " whose message plane\n  was never read because one server's"
           " had already been accepted%s\n",
           LinkOnly,
           LinkOnly ? "" : "  <- no adoption accrued a second server");
    printf("run: %u ticks\n", tick);
  }
  if (holes)
    printf("out-of-band content holes: %u"
           " (per-member cost O2 prices, never a round's failure)\n", holes);
  /*
   * Soundness of the fold ground, reported unconditionally: it must
   * never refuse a TRUE assertion, so with no Byzantine process present
   * this count must be zero.  A non-zero value here without -B is a
   * defect in the checks, not a defence working.
   */
  if (ByzProc < 0 && (CandRejected || CandConflicts))
    printf("fold ground refused %u assertion(s) and saw %u candidate"
           " conflict(s) with NO Byzantine process -- this is a defect\n",
           CandRejected, CandConflicts);
  if (TagRejected || DigestRejected || MaskMismatch || OriginRejected
   || ReAuthorRejected)
    printf("A9/O2/O4/O5 refusals with nothing forging: %u sender tag(s),"
           " %u content digest(s), %u serve re-mask(s) diverging from"
           " the pinned form, %u origin tag(s), %u re-authored"
           " offset(s) -- this is a defect\n",
           TagRejected, DigestRejected, MaskMismatch, OriginRejected,
           ReAuthorRejected);
  for (i = 0; i < N; ++i)
    if (Proc[i].partitioned && (int)i != ByzProc)
      printf("process %u self-classified PARTITIONED\n", i);

  if (!seqOk || !onceOk)
    exitCode = 1;

  /*----------------------------------------------------------------------*/
  /*  Cleanup                                                             */
  /*----------------------------------------------------------------------*/

cleanup:
  for (i = 0; i < N; ++i) {
    if (Proc[i].slot)
      for (j = 0; j < RSPACE; ++j)
        free(Proc[i].slot[j].acs);
    for (j = 0; j < LEGCAP; ++j)
      free(Proc[i].leg[j].f1);
    if (Proc[i].exch)
      for (j = 0; j < RSPACE * N; ++j)
        free(Proc[i].exch[j].f1);
    free(Proc[i].slot);
    free(Proc[i].exch);
    free(Proc[i].hold);
    free(Proc[i].msg);
    free(Proc[i].seq);
    free(Proc[i].seqHole);
    free(Proc[i].sys);
    free(Proc[i].store);
  }
  free(Queue);

  return (exitCode);

usage:
  fprintf(stderr,
    "usage: example_system [-v] [-s seed] [-l loss] [-L proc:round]\n"
    "                      [-M proc] [-B proc:mode] [-m every] [-T Tp]\n"
    "                      [-S sweeps] n t r msgs\n"
    "  n            total processes (2-%d)\n"
    "  t            max Byzantine faults (n >= 3t + 1)\n"
    "  r            recovery reach in rounds retained (1-%d)\n",
    MAX_PROCESSES, MAX_REACH);
  fprintf(stderr,
    "  msgs         application messages staged per process (1-%d);\n"
    "               past %d the wire's routing byte reuses and the\n"
    "               slot-reuse guard is exercised (positions never\n"
    "               wrap; the identity is the name)\n",
    MAX_STAGED, RSPACE);
  fprintf(stderr,
    "  -v           verbose: trace launches, closes, adoptions\n"
    "  -s seed      delivery-order / loss seed\n"
    "  -l loss      percent of inter-process wires dropped (default 4)\n"
    "  -L proc:rnd  cut proc off from all round-rnd traffic except\n"
    "               recovery legs -- it must heal whole by recovery:\n"
    "               composition by adoption, content by replay\n");
  fprintf(stderr,
    "  -M proc      mute proc for the whole run (needs t >= 1): its\n"
    "               outbound is never delivered, every round, every kind\n"
    "               -- the permanently silent fault; it still hears\n");
  fprintf(stderr,
    "  -B proc:mode Byzantine process (needs t >= 1); mode is a mask of\n"
    "               1 forge possession, 2 forge want, 4 fake candidate,\n"
    "               8 unchained candidate (default 5); 1 and 2 are\n"
    "               contradictory\n"
    "  -m every     identity-maintenance round every 'every' rounds (O5)\n"
    "  -o budget    signing offsets per key (O5; default 64, >= 16) --\n"
    "               maintenance goes due as a key nears exhaustion, and\n"
    "               carriers two keys stale are expired\n");
  fprintf(stderr,
    "  -T Tp        duty budget: own sweeps under TOLERANCE (default 100)\n"
    "  -S sweeps    progress budget, must exceed Tp (default 2*Tp)\n");
  return (1);
}
