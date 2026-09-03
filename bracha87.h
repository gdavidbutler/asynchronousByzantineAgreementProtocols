/*
 * asynchronousByzantineAgreementProtocols - Asynchronous Byzantine Agreement Protocols
 * Copyright (C) 2026 G. David Butler <gdb@dbSystems.com>
 *
 * This file is part of asynchronousByzantineAgreementProtocols
 *
 * asynchronousByzantineAgreementProtocols is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * asynchronousByzantineAgreementProtocols is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * INFORMATION AND COMPUTATION 75, 130-143 (1987)
 * Gabriel Bracha - Asynchronous Byzantine Agreement Protocols
 *
 * Four composable pure state machines, one per figure.
 * No I/O, no threads, no dynamic allocation.
 * Caller provides memory and executes output actions.
 *
 * Composition:
 *   message -> Fig1(n,t) -> accept -> Fig2/3(N) -> round complete -> Fig4(coin) -> decision
 *
 * Fig 2 defines the abstract protocol round (send, receive n-t, v := N(k,S)).
 * Fig 3 refines Fig 2: replaces receive with validate (VALID sets).
 *
 * Each module boundary matches the paper exactly.
 * Proofs apply per-module: Lemmas 1-4 and Theorems 1 and 4-5 to Fig1
 * (Theorem 5 names Fig1 the weak-termination Byzantine Generals
 * protocol: a faulty initiator's broadcast may never accept, and no
 * correct process can tell), Lemmas 5-7 to Fig2/3, Lemmas 8-10 and
 * Theorems 2-3 to Fig4.
 *
 * Operational limits:
 *   n:         unsigned char, encodes process count 1..256 (n + 1)
 *   t:         unsigned char, max 85 (n + 1 > 3t required)
 *   vLen:      unsigned char, encodes value length 1..256 (vLen + 1 bytes)
 *   maxPhases: unsigned char, max BRACHA87_MAX_PHASES (85)
 *   rounds:    unsigned char, 0-based, max 3 * BRACHA87_MAX_PHASES - 1 (254)
 */

#ifndef BRACHA87_H
#define BRACHA87_H

/*
 * Rounds per Figure 4 phase.  The paper writes Fig 4 as Phase(i) with
 * sub-actions at rounds 3i, 3i+1, 3i+2 (0-based here; the paper uses
 * 1-based 3i+1, 3i+2, 3i+3).  Fig 3 below is keyed by round; Fig 4
 * above by phase.  This constant names the conversion so callers do
 * not write the bare `* 3` / `/ 3`.
 */
#define BRACHA87_ROUNDS_PER_PHASE 3

/*
 * Maximum phases for Figure 4 consensus.
 * Each phase uses BRACHA87_ROUNDS_PER_PHASE rounds.  85 * 3 = 255
 * rounds, which is the maximum that fits in an unsigned char round
 * count.  Round indices range from 0 to
 * BRACHA87_ROUNDS_PER_PHASE * maxPhases - 1 (max 254).
 */
#define BRACHA87_MAX_PHASES 85

/*************************************************************************/
/*                                                                       */
/*  Figure 1 -- Reliable broadcast primitive                             */
/*                                                                       */
/*  One instance per (initiator, round) pair.                            */
/*  Caller maintains the set of instances.                               */
/*  Pure state machine: (state, input) -> (state', actions).             */
/*  n > 3t required.                                                     */
/*                                                                       */
/*  in                      condition                         out        */
/*  -------------------------------------------------------------------- */
/*  in(initial, v) from p  !echoed                            echo  all  */
/*  in(echo,    v) from j  !echoed && ecCnt[v]>(n+t)/2        echo  all  */
/*  in(ready,   v) from j  !echoed && rdCnt[v]>=t+1           echo  all  */
/*  in(echo,    v) from j   echoed && !rdSent                            */
/*                                 && ecCnt[v]>(n+t)/2        ready all  */
/*  in(ready,   v) from j   echoed && !rdSent                            */
/*                                 && rdCnt[v]>=t+1           ready all  */
/*  in(ready,   v) from j            rdCnt[v]>=2t+1          accept     */
/*                                                                       */
/*  Rule 6 reads no sent state: 2t+1 readys accept whether or not this   */
/*  process has sent its own.  One (ready, v) therefore fires Rules 5    */
/*  and 6 together, and Input returns ready THEN accept with             */
/*  BRACHA87_F1_ACCEPTED already set -- a READY output is never proof    */
/*  that the instance has not yet accepted.                              */
/*                                                                       */
/*  Paper typo: Fig. 1 says "(n+t)/2 (echo,v) messages" but the          */
/*  Lemma 1 proof says "more than (n+t)/2." The proof requires           */
/*  strict > for the pigeonhole argument. Code follows the proof.        */
/*                                                                       */
/*  Variable convention in the rule table above:                         */
/*    n   = actual process count (the struct field decoded; actual =     */
/*          fig1->n + 1).  This matches the paper.  Do NOT substitute    */
/*          the encoded byte from struct bracha87Fig1.                   */
/*    t   = max Byzantine, used as-is.                                   */
/*    Worked example: n_actual = 4, t = 1.                               */
/*      (n+t)/2 = 5/2 = 2 (C integer arithmetic).                        */
/*      Rule 2 fires on the 3rd distinct echo (strict >).                */
/*                                                                       */
/*  Per-sender dedup bounds Byzantine equivocation: at most one ECHO     */
/*  and one READY from each sender contribute to thresholds, regardless  */
/*  of how many duplicates or differing-value copies arrive.             */
/*                                                                       */
/*************************************************************************/

/*
 * Figure 1 message types (input)
 * The three types occupy the low two bits (0..2).  An application that
 * frames its wire format may pack this type into the low bits of a
 * single class+type byte and recover it with BRACHA87_TYPE_MASK; the
 * higher bits are free for a caller-chosen message class (see
 * BKR94ACS_CLS_MASK).  The library itself never serializes; this mask
 * only documents the value range so packers do not collide.
 */
#define BRACHA87_INITIAL   0x00
#define BRACHA87_ECHO      0x01
#define BRACHA87_READY     0x02
#define BRACHA87_TYPE_MASK 0x03

/* Figure 1 output actions */
#define BRACHA87_INITIAL_ALL 1  /* send (initial, v) to all processes (BPR) */
#define BRACHA87_ECHO_ALL    2  /* send echo(v) to all processes */
#define BRACHA87_READY_ALL   3  /* send ready(v) to all processes */
#define BRACHA87_ACCEPT      4  /* accept(v) */

/* Figure 1 state flags (bitmap) */
#define BRACHA87_F1_ECHOED   0x01
#define BRACHA87_F1_RDSENT   0x02
#define BRACHA87_F1_ACCEPTED 0x04
#define BRACHA87_F1_INITIATOR   0x08  /* this process is the broadcast initiator */

/*
 * Figure 1 state.
 *
 * Caller allocates bracha87Fig1Sz(n, vLen) bytes and calls
 * bracha87Fig1Init before use. No dynamic allocation.
 *
 * The value v is a fixed-length byte string of vLen + 1 bytes.
 * vLen encodes the value length: 0 = 1 byte, 255 = 256 bytes.
 * Per-process echo/ready values are stored so echo_count[v]
 * is computed correctly for any v, avoiding the liveness bug
 * where echoing the first value seen blocks the honest one.
 */
struct bracha87Fig1 {
  unsigned short ecCnt[2];/* incremental echo counts for binary (vLen==0) */
  unsigned short rdCnt[2];/* incremental ready counts for binary (vLen==0) */
  unsigned char n;        /* process count encoding: actual = n + 1 */
  unsigned char t;        /* max Byzantine (n + 1 > 3t) */
  unsigned char vLen;     /* value length encoding: actual = vLen + 1 */
  unsigned char flags;    /* BRACHA87_F1_ECHOED/RDSENT/ACCEPTED/INITIATOR */
  unsigned char data[1];  /* variable: see bracha87Fig1Sz */
};

/* data[] is the variable tail; see bracha87.c for layout. */

/*
 * Size in bytes needed for a Fig1 instance, or 0 if the configuration
 * cannot be built.
 *
 * The parameters are WIDER than bracha87Fig1Init's unsigned char on
 * purpose, and the width is the refusal: an out-of-range n or vLen is
 * unrepresentable at Init, so Sz is the only entry that can see one,
 * and 0 is how it declines.  Every *Sz in this library and bkr94acsSz
 * answer the same way.
 */
unsigned long
bracha87Fig1Sz(
  unsigned int             /* n: actual process count = n + 1; > 255 refused */
 ,unsigned int             /* vLen: actual value length = vLen + 1; > 255 refused */
);

/* Initialize a Fig1 instance. Caller has allocated bracha87Fig1Sz bytes. */
void
bracha87Fig1Init(
  struct bracha87Fig1 *
 ,unsigned char            /* n: actual process count = n + 1 */
 ,unsigned char            /* t */
 ,unsigned char            /* vLen: actual value length = vLen + 1 */
);

/*
 * Mark this Fig1 instance as the broadcast initiator and store
 * the value to be broadcast.  Sets BRACHA87_F1_INITIATOR; copies
 * value into the echoed-value slot so bracha87Fig1Value
 * returns it (without setting BRACHA87_F1_ECHOED -- Rule 1 still
 * fires from the receipt of (initial, v) via bracha87Fig1Input).
 *
 * BPR retries BRACHA87_INITIAL_ALL on every bracha87Fig1Bpr
 * call while INITIATOR is set and the instance has neither ACCEPTED
 * nor observed an echo from every process.  Stopping at "we ECHOED
 * locally" would be wrong -- in the n = 3t + 1 regime the
 * (n+t)/2+1 echo threshold equals the count of honest processes, so
 * any honest process that missed the bootstrap INITIAL can leave the
 * cascade one echo short forever, and only the initiator can
 * break it.  The two retirements above are the sound stops:
 * echoSenders == n means there is no un-echoed process left to
 * induce; ACCEPTED means t+1 correct readys now circulate, so
 * ready-amplification carries every correct process to accept with
 * no INITIAL consumed.  Both are strictly stronger than the
 * forbidden ECHOED gate.
 *
 * Caller outputs BRACHA87_INITIAL_ALL once at initiator time and
 * relies on BPR thereafter.
 *
 * Idempotent: re-calling overwrites the stored value.  The
 * intended use is one call at A-Cast time.
 */
void
bracha87Fig1Initiator(
  struct bracha87Fig1 *
 ,const unsigned char *    /* value: vLen + 1 bytes */
);

/*
 * Process one incoming message. Returns number of actions (0..3).
 * Actions written to out[] in order (echo, ready, accept).
 * Caller provides out[] with room for 3 entries.
 *
 * On any action, the echoed value is available via bracha87Fig1Value.
 *
 * Deduplication: at most one echo and one ready per sender.
 *
 * Recording continues past ACCEPT: an arrival after this instance has
 * accepted still takes its per-sender echo/ready record -- feeding
 * bracha87Fig1AllEchoed, the suppress masks, and the acFrom-subset-of-
 * rdFrom ingress order -- and returns 0.  No action is output twice,
 * ACCEPT included.
 *
 * INITIAL sender obligation: this instance is keyed to ONE designated
 * initiator.  Only that initiator may send (initial, v); ECHO and
 * READY arrive legitimately from any process (and are sender-deduped).
 * This entry does NOT know its own initiator index, so it cannot
 * enforce the binding -- the CALLER must drop any INITIAL whose
 * authenticated sender is not the designated initiator before calling
 * here.  Authenticated channels alone do not close this: they bind the
 * sender identity but not the message's claimed initiator, and a
 * non-initiator INITIAL is a forged broadcast the echo cascade would
 * carry to a false ACCEPT.  (bkr94acsAcastInput / bkr94acsBa-
 * Input enforce from == initiator / initiator on the caller's behalf.)
 */
unsigned int
bracha87Fig1Input(
  struct bracha87Fig1 *
 ,unsigned char            /* type: BRACHA87_INITIAL/ECHO/READY */
 ,unsigned char            /* from: sender index */
 ,const unsigned char *    /* value: vLen + 1 bytes */
 ,unsigned char *          /* out: actions, room for 3 */
);

/*
 * Echoed value.
 *
 * Returns non-null when either:
 *   - this instance is the initiator (BRACHA87_F1_INITIATOR set via
 *     bracha87Fig1Initiator), or
 *   - Rule 1, 2, or 3 has fired (BRACHA87_F1_ECHOED set).
 *
 * Initiator-only state returns the value supplied at
 * bracha87Fig1Initiator so BPR can re-broadcast it before any
 * loopback or echo-cascade has set ECHOED.
 */
const unsigned char *
bracha87Fig1Value(
  const struct bracha87Fig1 *
);

/*
 * BPR - Bracha Phase Retry.
 *
 * End-to-end argument applied to Bracha (Saltzer/Reed/Clark
 * 1984; see SRC84.txt and BPR.md, the governing statement): the
 * "still owed" predicate lives at the Bracha endpoint, so
 * retransmission is placed here.
 *
 * Retry the broadcast actions this instance is still owed under
 * fair-loss -- offered to recover eventual delivery without an
 * application-layer retry bookkeeping.  Returns the number of actions (0..3) to broadcast.
 *
 * Reactive: rules fire only when called.  No wall-clock predicate
 * appears anywhere; the application's retry tick IS the event, so
 * asynchrony is preserved.  The rate of retry calls bounds retry
 * volume.
 *
 * Minimal retry -- each action retires at the soonest point
 * it is provably no longer owed to ANY correct process:
 *   INITIAL (initiator only): retires at ACCEPTED, or once an echo has
 *     been observed from every process (echoSenders == n).  INITIAL
 *     only induces echoes, so all-echoed leaves nothing to induce;
 *     ACCEPTED witnesses t+1 correct readys, after which ready-
 *     amplification needs no initial.
 *   ECHO (echoed): retires at ACCEPTED, for the same amplification
 *     reason -- past t+1 correct readys no process consumes an echo.
 *   READY (rdSent): never retires on LOCAL state.  It is exactly
 *     what the amplification tail consumes; an accepted process still
 *     owes its (ready, v) to processes below 2t+1, and retry is the
 *     only delivery mechanism under loss.  It DOES retire on the
 *     REMOTE pair of facts that every process has accepted AND every
 *     process holds THIS instance's accept -- the count of
 *     bracha87Fig1Skip(READY_ALL) reaching n -- after which no process
 *     consumes a ready anywhere and nobody is owed an announcement:
 *     genuine quiescence, where the application's termination policy
 *     would otherwise carry the never-retired tail.  Below that, the
 *     same mask drops READY per process.
 *
 *     Both halves are load-bearing, and the second is why the mask is
 *     not simply the accepted set.  "q has accepted" and "q holds my
 *     accept" are different facts, and only the READY this process is
 *     already retrying carries the second one.  Suppressing on the
 *     first alone silences the announcement q is waiting for -- q's own
 *     gate then stands one bit short forever, which a cursor phase
 *     offset reaches with no loss at all.  A (ready, v) arriving
 *     WITHOUT the RECEIVED annotation is q showing so; the caller routes
 *     it to bracha87Fig1ProcessResend and the next tick re-sends with the
 *     annotation set (bracha87Fig1Received).  A marked READY re-arms
 *     nothing, so the exchange converges in one tick per direction and
 *     stops.
 *
 *     Under fair loss quiescence is REACHABLE, not guaranteed: a lost
 *     marked READY leaves its target's evidence unchanged, so its
 *     unmarked re-sends persist at its own tick rate, each re-arms, each
 *     draws the marked re-send again, and fair loss delivers one
 *     eventually.  What still blocks the gate forever is an honest
 *     residue no annotation can reach: a process that abandons early, or
 *     one that never announces at all (Byzantine-silent), keeps every
 *     other process's count below n.  That is safe -- it can only
 *     persist once every correct process has accepted, and the
 *     un-quiesced process merely retries a READY no correct process
 *     consumes.  An outstanding arm costs one masked READY per tick,
 *     aimed only at the process it un-suppressed; a Byzantine process
 *     that announced and then keeps re-arming can hold the gate open at
 *     that price, forfeiting nothing owed to any correct process.  The
 *     application's abandonment policy remains the backstop.
 *
 * Per-process suppression: every retry action carries a suppress mask
 * (bracha87Fig1Skip; on the array path, struct bracha87Fig1Act.skip)
 * naming processes that provably no longer consume it -- echoed processes for
 * INITIAL, readied processes for ECHO, accepted processes for READY.  The
 * caller's broadcast skips them.  This is the individual-process refinement
 * of the all-or-nothing retires above: a fast process is dropped from the
 * recipient set the moment IT crosses, not when the last process does.
 * INITIAL and ECHO are bootstrap-only; ACCEPTED is the local
 * witness (>= t+1 of 2t+1 readys are correct) that the bootstrap
 * is complete.  These stops are strictly stronger than the
 * "stop once locally echoed/accepted-so-stop-ready" gates that
 * would strand slow processes -- those remain forbidden.
 *
 * Returns 0 when there is nothing sent to retry (a non-initiator
 * instance that has not echoed -- the natural "idle" signal at the
 * Fig1 level), or when every sent action has hit its retire
 * gate above: INITIAL and ECHO at ACCEPTED (INITIAL also at
 * all-echoed), READY at full suppress coverage.  An RDSENT instance
 * therefore outputs at least READY until that gate closes; past that
 * it is quiescent and returns 0, and an unmarked READY arriving
 * afterward re-opens it for exactly the tick that re-sends marked.
 *
 * Out actions reuse BRACHA87_INITIAL_ALL / BRACHA87_ECHO_ALL /
 * BRACHA87_READY_ALL; the echoed value is read via
 * bracha87Fig1Value, same as after Input.  Order of actions in
 * out[]: initial, echo, ready.
 */
unsigned int
bracha87Fig1Bpr(
  struct bracha87Fig1 *
 ,unsigned char *          /* out: actions, room for 3 */
);

/*
 * Returns 1 iff this instance has recorded an echo from every one of
 * the n processes (distinct echo senders == n), else 0 (and 0 for a null
 * pointer).
 *
 * This is the same monotone quantity that retires INITIAL retry (see
 * bracha87Fig1Bpr): once every process has echoed there is nothing left
 * for (initial, v) to induce.  It is exposed because an application
 * that pairs a side-channel payload with the broadcast -- and gates
 * its own ECHO on having validated that payload -- needs all-echoed,
 * NOT the A-Cast's ACCEPTED, as the retirement point for the side
 * channel: all-echoed implies every process validated the payload, which
 * ACCEPTED (2t+1 readys, of which up to t may be byzantine and t the
 * un-validated tail above the n=3t+1 boundary) does not.  Under <= t
 * silent processes this never reaches 1, so such a side channel retries
 * until the application abandons -- the conservative, correct default.
 */
unsigned int
bracha87Fig1AllEchoed(
  const struct bracha87Fig1 *
);

/*
 * Record that process 'from' has ACCEPTED this instance (idempotent,
 * value-agnostic).  Drives the READY suppress mask and, with
 * bracha87Fig1ProcessResend, the READY quiescence gate.  'from' is the
 * announcing process -- the sender of
 * a (ready, v) carrying the ACCEPTED annotation, already fed through
 * bracha87Fig1Input -- or the local index, supplied by the caller for
 * self-accept (a Fig1 instance does not know its own process index).
 * Out-of-range 'from' and a null instance are ignored.
 *
 * THIS STAYS A SEPARATE ENTRY, and the composition above it is not the
 * precedent.  bkr94acs{Acast,Ba}Input take the same two annotations as
 * an `annot` argument, on the argument that both are facts about one
 * message so both belong to the call that carries it.  That argument
 * does not reach down here: SELF-ACCEPT IS A FACT ABOUT NO MESSAGE.
 * There is no Input to hang it on, and a bare-Fig1 caller has no other
 * way to record it -- which is why bkr94acs itself calls this entry
 * internally at each of its own ACCEPT points.  Folding these two into
 * bracha87Fig1Input would take self-accept away from every caller that
 * runs its own Fig 1 and leave the quiescence gate unable to reach n.
 */
void
bracha87Fig1ProcessAccepted(
  struct bracha87Fig1 *
 ,unsigned char            /* from: announcing process, or self */
);

/*
 * Record that process 'from' does NOT hold this instance's accept, so the
 * READY suppress mask must not drop it and the READY retire gate must not
 * close over it.  Mirror of bracha87Fig1ProcessAccepted, and read off the
 * same message: a (ready, v) that arrives WITHOUT the RECEIVED annotation
 * is its sender showing this process's accept has not been recorded there
 * -- had it recorded one, it would have suppressed this process instead of
 * sending.  Call AFTER the matching bracha87Fig1Input for that (ready, v),
 * the same discipline ProcessAccepted takes; the two are order-independent
 * with respect to each other, and one message can legitimately carry both
 * facts (its sender has accepted, and it lacks ours).
 *
 * The arm cannot ride Input itself: an unmarked re-send is a DUPLICATE
 * (ready, v), which Input dedups and returns 0 for before reaching any
 * post-accept region.  Caller-fed is how the arm escapes that dedup --
 * exactly as ProcessAccepted already does for the ACCEPTED annotation.
 *
 * Records nothing until this instance has BRACHA87_F1_ACCEPTED: there is
 * no accept to announce, and the post-accept retries reach an
 * unannounced-to process anyway, since it is not yet in acFrom.  Consumed
 * by the next READY egress, which goes out marked; a lost marked re-send
 * is re-armed by its target's next unmarked one, and that re-arming is the
 * loss recovery.  Out-of-range 'from' and a null instance are ignored.
 *
 * CALLER OBLIGATION.  A caller that stops ticking an instance on the
 * quiescent 0 return (leaving it out of its retry rotation) must put it
 * BACK on this call.  The library re-opens the READY retire here, but only
 * a tick can re-send, and an unmarked READY is exactly the evidence that
 * something is still owed.  Skipping the re-entry costs nothing while
 * nothing is lost and forfeits the whole fair-loss recovery the moment a
 * marked re-send is dropped: its target re-sends unmarked forever into a
 * process that has stopped listening for its own work.
 *
 * Byzantine note, the mirror of ProcessAccepted's: an arm only ever
 * un-suppresses its own sender, so a forged unmarked READY buys the forger
 * one masked READY per tick, aimed at the forger.  It can hold this
 * instance's READY retire open -- the same standing an announcement it
 * never sends already has -- and can neither delay nor displace anything
 * owed to a correct process, since every other bit of the mask still
 * suppresses.
 */
void
bracha87Fig1ProcessResend(
  struct bracha87Fig1 *
 ,unsigned char            /* from: process lacking our announcement */
);

/*
 * BPR per-process suppress mask for one retry action: a bitmap of processes
 * that provably no longer consume it, so the caller's broadcast skips
 * them (process p skipped iff bit p set).  INITIAL_ALL -> echoed processes,
 * ECHO_ALL -> readied processes, READY_ALL -> accepted processes MINUS
 * the ones with an outstanding arm (bracha87Fig1ProcessResend) --
 * suppressing an armed process is what strands it; 0 for a null
 * instance or non-retry act (broadcast to all).  Borrowed pointer into
 * library state, valid until the next mutating call on this instance.
 * The array Retry fills struct bracha87Fig1Act.skip from this.
 *
 * The READY mask is also the retire gate: bracha87Fig1Bpr stops outputting
 * READY_ALL when all n of its bits are set, which is the same statement as
 * "every process has accepted and none is owed an announcement."  Its
 * outstanding arms are consumed by each READY_ALL egress, so a mask read
 * between that egress and the next arm or tick can still show a process
 * un-suppressed whose marked re-send just went out -- one duplicate
 * (ready, v) at worst, never a missed one.
 */
const unsigned char *
bracha87Fig1Skip(
  const struct bracha87Fig1 *
 ,unsigned char            /* act: BRACHA87_INITIAL_ALL/ECHO_ALL/READY_ALL */
);

/*
 * The RECEIVED mask: a bitmap of the processes whose accept this instance
 * has recorded (bracha87Fig1ProcessAccepted), so a (ready, v) sent to
 * process p can say so iff bit p is set -- test with BRACHA87_SKIP_TST,
 * same bit convention.  0 for a null instance.  Borrowed pointer into
 * library state, valid until the next mutating call on this instance.  The
 * array Retry fills struct bracha87Fig1Act.received from this.
 *
 * The annotation is the discriminator that keeps the annotation exchange
 * from ping-ponging: a (ready, v) carrying it must NOT be fed to
 * bracha87Fig1ProcessResend, while one arriving without it re-arms.  It is
 * the raw accepted set, not the suppressed set -- an armed process is
 * precisely a process in this mask that must still be sent to -- so a
 * caller reads .skip to decide WHETHER to send and this to decide WHAT the
 * sent (ready, v) claims.  Never set the annotation for a process outside
 * this mask: it would show a correct process its accept as received here,
 * silencing the re-sends carrying the announcement this instance still
 * needs.
 */
const unsigned char *
bracha87Fig1Received(
  const struct bracha87Fig1 *
);

/*
 * Test a BPR suppress mask -- the bitmap returned by bracha87Fig1Skip
 * (or carried on struct bracha87Fig1Act.skip / bkr94acsAct.skip):
 * non-zero iff process 'p' is to be skipped.  This names the mask's bit
 * convention (a little-endian per-process bitmap, BIT_SZ(n) bytes) so a
 * broadcast loop reads it without re-deriving the layout:
 *
 *   for (p = 0; p < n; ++p)
 *     if (!(skip && BRACHA87_SKIP_TST(skip, p)))
 *       deliver_to(p, ...);
 *
 * SELF IS IN THE LOOP, and the protocol does not converge without it.
 * A Fig 1 instance counts a process's own echo and ready only when they
 * arrive through bracha87Fig1Input with from == self; sending them sets
 * the ECHOED / RDSENT flags and nothing else.  At n = 3t+1 the echo
 * threshold (n+t)/2 + 1 equals the count of correct processes, so a
 * process that omits itself is one short of its own threshold forever
 * and no ready is ever sent anywhere.  Delivering to self is a LOCAL
 * hand-back, not a packet: a hairpin through the network is both
 * wasteful and, behind NAT, unreliable.
 *
 * Macro, not a function, so it inlines in the per-process loop; 'mask' is
 * evaluated once, 'p' twice (pass a simple expression).  A null mask
 * means "deliver to all" -- guard with `skip &&` as above.
 */
#define BRACHA87_SKIP_TST(mask, p) \
  ((mask)[(unsigned int)(p) >> 3] & (1 << ((unsigned int)(p) & 7)))

/*************************************************************************/
/*                                                                       */
/*  Figure 2 -- Abstract protocol round                                  */
/*                                                                       */
/*  The generic form of any asynchronous protocol round.                 */
/*  Figure 3 refines this by replacing receive with validate.            */
/*  Rounds are 0-based.                                                  */
/*                                                                       */
/*  round(k) by process p                                                */
/*    Send (p, k, v) to all processes                                    */
/*    Wait until a set S of n - t k-messages received                    */
/*    v := N(k, S)                                                       */
/*                                                                       */
/*************************************************************************/

/*
 * Figure 2 output action.  It reports that the round's threshold is
 * CROSSED, which enables the round; it is not an instruction to compute
 * it now.  The received set keeps growing past n-t, and any sample of
 * at least n-t is proof-covered -- see WHEN TO CALL IT at
 * bracha87Fig4Round.  Reported once, on the crossing arrival only.
 */
#define BRACHA87_ROUND_COMPLETE 2  /* n-t messages received for round k */

/*
 * Figure 2 state.
 *
 * Tracks received messages per round for up to n processes, maxRounds rounds.
 * Caller allocates bracha87Fig2Sz(n, maxRounds) bytes and calls
 * bracha87Fig2Init. No dynamic allocation.
 *
 * Pure accumulation: any received message counts toward n-t.
 * For validated accumulation, use Figure 3 which adds VALID checking.
 */
struct bracha87Fig2 {
  unsigned char n;        /* process count encoding: actual = n + 1 */
  unsigned char t;        /* max Byzantine (n + 1 > 3t) */
  unsigned char maxRounds;
  unsigned short data[1]; /* variable: see bracha87Fig2Sz */
};

/*
 * data[] is the variable tail; see bracha87.c for layout.  It is
 * typed unsigned short because it begins with the per-round received
 * counts, which must hold 256 (an unsigned char would wrap when all
 * 256 senders of a full house are received); the member type also
 * carries the alignment those counts require.
 */

/* Size in bytes needed for a Fig2 instance, or 0 if out of range
 * (same refusal contract as bracha87Fig1Sz). */
unsigned long
bracha87Fig2Sz(
  unsigned int             /* n: actual process count = n + 1; > 255 refused */
 ,unsigned int             /* maxRounds; > 255 refused */
);

/* Initialize a Fig2 instance. Caller has allocated bracha87Fig2Sz bytes. */
void
bracha87Fig2Init(
  struct bracha87Fig2 *
 ,unsigned char            /* n: actual process count = n + 1 */
 ,unsigned char            /* t */
 ,unsigned char            /* maxRounds */
);

/*
 * Record a received message for round k.
 * Returns BRACHA87_ROUND_COMPLETE if this causes n-t received, 0 otherwise.
 * Deduplication: one message per sender per round.
 */
unsigned int
bracha87Fig2Receive(
  struct bracha87Fig2 *
 ,unsigned char            /* round k (0-based) */
 ,unsigned char            /* sender */
 ,unsigned char            /* value */
);

/* Query received count for round k */
unsigned int
bracha87Fig2RecvCount(
  const struct bracha87Fig2 *
 ,unsigned char            /* round k (0-based) */
);

/*
 * Retrieve received messages for round k.
 * Returns count, fills senders[] and values[] (caller-provided, n + 1 entries).
 */
unsigned int
bracha87Fig2GetReceived(
  const struct bracha87Fig2 *
 ,unsigned char            /* round k (0-based) */
 ,unsigned char *          /* senders out, n + 1 entries */
 ,unsigned char *          /* values out, n + 1 entries */
);

/*************************************************************************/
/*                                                                       */
/*  Figure 3 -- Correctness enforcement (VALID sets)                     */
/*                                                                       */
/*  Refines Figure 2: replaces receive with validate (VALID sets).       */
/*  Wraps Figure 1 Accept with a recursive conformance check.            */
/*  Parameterized by N: the protocol function.                           */
/*  Rounds are 0-based.                                                  */
/*                                                                       */
/*  When n-t messages validate for round k (Fig 2 threshold), stored     */
/*  messages from round k+1 are re-evaluated, cascading as needed.       */
/*                                                                       */
/*  round(k) by process p                                                */
/*    Broadcast(p, k, v)                                                 */
/*    Wait till n-t k-messages validated                                 */
/*    v := N(k, S)                                                       */
/*                                                                       */
/*  VALID^0_p = {(q,0,v) | accepted, v in {0,1}}                         */
/*  VALID^k_p = {(q,k,v) | accepted, exists n-t in VALID^{k-1}           */
/*               s.t. v = N(k-1, {m1..m_{n-t}})}                         */
/*                                                                       */
/*************************************************************************/

/*
 * Protocol function N.
 * Given round k and a set of n-t validated messages from round k,
 * compute the output value.
 *
 *   closure: caller context
 *   k: round number (0-based)
 *   n_msgs: number of messages (at least n-t, may exceed n-t)
 *   senders: n_msgs sender IDs
 *   values: n_msgs values, each 1 byte (binary consensus)
 *   result: output value written here (1 byte)
 *
 * Returns 0: result is set, exact match required for VALID.
 * Returns >0: any binary value is valid (non-deterministic path).
 *   When n_msgs > n-t, N should return >0 if different n-t subsets
 *   could produce different results (paper's existential quantifier).
 * Returns <0: error, message is invalid.
 *
 * WRITE result ON EVERY NON-ERROR RETURN, the permissive one included.
 * The permissive return does NOT mean "result is unused": the caller
 * reads it to police BRACHA87_D_FLAG, which the free choice of a binary
 * value does not extend to.  On a >0 return set BRACHA87_D_FLAG in
 * result iff a d-message is legitimate at this round, and when it is,
 * set result's low bit to the one base value it is legitimate for (at
 * most one of (0|D_FLAG), (1|D_FLAG) can be, per Fig 4 step 2).  An
 * incoming d-message is rejected unless result marks D_FLAG legitimate
 * and its base matches.  Leaving result untouched on this path is
 * undefined behavior -- it is an ordinary uninitialized local in the
 * caller's frame, and the D_FLAG gate would read whatever it held.
 * Only the <0 error return may leave result alone.
 */
typedef int (*bracha87Nfn)(
  void *                   /* closure */
 ,unsigned char            /* k */
 ,unsigned int             /* n_msgs */
 ,const unsigned char *    /* senders */
 ,const unsigned char *    /* values */
 ,unsigned char *          /* result */
);

/* Figure 3 output action */
#define BRACHA87_VALIDATED 1  /* message is in VALID^k */

/*
 * Figure 3 state.
 *
 * Manages VALID sets for up to maxRounds rounds, n processes.
 * Caller allocates bracha87Fig3Sz(n, maxRounds) bytes and
 * calls bracha87Fig3Init.
 */
struct bracha87Fig3 {
  bracha87Nfn N;
  void *Nclosure;
  unsigned char n;
  unsigned char t;
  unsigned char maxRounds;
  unsigned short data[1];  /* variable: see bracha87Fig3Sz */
};

/*
 * data[] is the variable tail; see bracha87.c for layout.  It is
 * typed unsigned short because it begins with the per-round VALID^k
 * counts, which must hold 256 (an unsigned char would wrap when all
 * 256 senders of a full house validate in one round, permanently
 * stalling validation of the next round); the member type also
 * carries the alignment those counts require.
 */

/* Size in bytes needed for a Fig3 instance, or 0 if out of range
 * (same refusal contract as bracha87Fig1Sz). */
unsigned long
bracha87Fig3Sz(
  unsigned int             /* n: actual process count = n + 1; > 255 refused */
 ,unsigned int             /* maxRounds; > 255 refused */
);

void
bracha87Fig3Init(
  struct bracha87Fig3 *
 ,unsigned char            /* n: actual process count = n + 1 */
 ,unsigned char            /* t */
 ,unsigned char            /* maxRounds */
 ,bracha87Nfn              /* N */
 ,void *                   /* Nclosure */
);

/*
 * Submit an accepted message (from Fig1) for validation.
 *
 * Returns:
 *   BRACHA87_VALIDATED if message is in VALID^k
 *   0 if not valid, round out of range, or this sender was already
 *     stored for round k (one message per sender per round; the
 *     duplicate is dropped before validation is attempted)
 *
 * validCount receives the number of validated messages for round k
 * (so caller can check for n-t completion).
 */
unsigned int
bracha87Fig3Accept(
  struct bracha87Fig3 *
 ,unsigned char            /* round k (0-based) */
 ,unsigned char            /* sender */
 ,unsigned char            /* value */
 ,unsigned int *           /* validCount out, 0 to skip */
);

/*
 * Query VALID^k count for a specific round.
 */
unsigned int
bracha87Fig3ValidCount(
  const struct bracha87Fig3 *
 ,unsigned char            /* round k (0-based) */
);

/*
 * Retrieve validated messages for round k.
 * Returns count, fills senders[] and values[] (caller-provided, n + 1 entries).
 */
unsigned int
bracha87Fig3GetValid(
  const struct bracha87Fig3 *
 ,unsigned char            /* round k (0-based) */
 ,unsigned char *          /* senders out, n + 1 entries */
 ,unsigned char *          /* values out, n + 1 entries */
);

/*
 * Returns 1 iff round k has reached n-t validated (Fig 2 round
 * completion), else 0 (and 0 for a null pointer or a round out of
 * range).  Includes rounds completed by cascaded re-evaluation.
 */
unsigned int
bracha87Fig3RoundComplete(
  const struct bracha87Fig3 *
 ,unsigned char            /* round k (0-based) */
);

/*************************************************************************/
/*                                                                       */
/*  Figure 4 -- Consensus protocol                                       */
/*                                                                       */
/*  Instantiates Figure 3 with three specific N functions.               */
/*  Three rounds per phase. Parameterized by coin.                       */
/*                                                                       */
/*  maxPhases <= BRACHA87_MAX_PHASES (85).                               */
/*  85 * 3 = 255 rounds fits in unsigned char round count (0..254).      */
/*  If all phases are exhausted without decision, Fig4Round returns      */
/*  BRACHA87_EXHAUSTED.                                                  */
/*                                                                       */
/*  Phase(i) by process p:                                               */
/*                                                                       */
/*  1. Broadcast(p, 3i, value_p).                                        */
/*     Wait n-t validated. value_p := majority.                          */
/*                                                                       */
/*  2. Broadcast(p, 3i+1, value_p).                                      */
/*     If >n/2 same v: value_p := (d,v). Else unchanged.                 */
/*                                                                       */
/*  3. Broadcast(p, 3i+2, value_p).                                      */
/*     If >2t (d,v): decide v.                                           */
/*     Else if >t (d,v): value_p := v.                                   */
/*     Else: value_p := coin.                                            */
/*                                                                       */
/*************************************************************************/

/*
 * Coin function: return 0 or 1 for the coin named by (instance, phase).
 *
 * The coin is an ORACLE, not a protocol.  Randomization is how Bracha
 * meets FLP: deterministic asynchronous consensus is impossible, and
 * the paper does not evade that -- it changes the termination
 * requirement, buying probabilistic termination with a source of
 * randomness taken as given.  That source must come from OUTSIDE the
 * asynchronous system.  So this call returns a value, always and
 * immediately: it cannot fail, cannot decline, and must not block or
 * perform I/O.  An interactive construction -- one that broadcasts and
 * waits for peers before it can answer -- does not belong behind this
 * callback; it would relocate the impossibility into the coin rather
 * than escape it, and make the protocol's termination rest on a
 * liveness condition no paper here proves.  See README (Coin Choice).
 *
 * instance names WHICH state machine is asking; phase names WHICH coin
 * within it.  Together with whatever the closure carries (a session or
 * epoch identity), they are the coin's NAME, and the name is what a
 * common coin must agree on across processes.  A caller running one
 * Fig 4 can pass anything for instance and ignore it; bkr94acs sets it
 * to the BA's process index, so the N concurrent BAs of one ACS draw
 * distinct coins rather than sharing a phase's value.  A local coin
 * (independent randomness per call) ignores both parameters -- it needs
 * entropy, not a name.
 */
typedef unsigned char (*bracha87CoinFn)(
  void *                   /* closure */
 ,unsigned char            /* instance */
 ,unsigned char            /* phase */
);

/* Figure 4 output actions (bitmask) */
#define BRACHA87_BROADCAST 1  /* broadcast value_p for current round */
#define BRACHA87_DECIDE    2  /* decided: value is final */
#define BRACHA87_EXHAUSTED 4  /* all phases exhausted without decision */

/*
 * Figure 4 decision-candidate flag.
 * Paper's "(d, v)" encoding: high bit marks that
 * the sender saw >n/2 agreement in step 2.
 * value & 1 = the binary value, value & D_FLAG = decision candidate.
 * Present only in round 3i+2 broadcasts.
 */
#define BRACHA87_D_FLAG    0x80

/* Figure 4 state flags (bitmap; same idiom as BRACHA87_F1_*) */
#define BRACHA87_F4_DECIDED   0x01
#define BRACHA87_F4_EXHAUSTED 0x02

/*
 * Figure 4 state.
 *
 * Caller allocates bracha87Fig4Sz(n, maxPhases) bytes and calls
 * bracha87Fig4Init.  Embeds a Fig3 instance as the trailing fig3
 * field; its variable tail extends past sizeof (struct bracha87Fig4)
 * into the bytes Sz() reserves for it.  Caller reads the embedded
 * Fig3 directly as &fig4->fig3 -- no cast.
 *
 * maxPhases must be >= 1 and <= BRACHA87_MAX_PHASES (85).
 * Fig 4 instantiates Fig 3 with maxRounds = maxPhases * 3.
 */
struct bracha87Fig4 {
  bracha87CoinFn coin;
  void *coinClosure;
  unsigned char n;        /* process count encoding: actual = n + 1 */
  unsigned char t;        /* max Byzantine (n + 1 > 3t) */
  unsigned char maxPhases;
  unsigned char instance;  /* passed to coin as the caller's own name
                            * for this state machine; never interpreted */
  unsigned char phase;     /* current phase (0-based) */
  unsigned char subRound;  /* 0, 1, or 2 within phase */
  unsigned char value;     /* current estimate */
  unsigned char decision;
  unsigned char flags;     /* BRACHA87_F4_DECIDED / BRACHA87_F4_EXHAUSTED */
  struct bracha87Fig3 fig3;/* embedded Fig 3; variable tail extends past */
};

/*
 * Size in bytes needed for a Fig4 instance, or 0 if n is out of range
 * (same refusal contract as bracha87Fig1Sz).
 *
 * maxPhases is CLAMPED, not refused, because bracha87Fig4Init clamps it
 * identically -- the allocation and the initialized machine agree on the
 * ceiling.  n has no such counterpart: Init takes it as an unsigned char
 * and cannot see an out-of-range value at all, so Sz refuses it.
 */
unsigned long
bracha87Fig4Sz(
  unsigned int             /* n: actual process count = n + 1; > 255 refused */
 ,unsigned int             /* maxPhases, clamped to BRACHA87_MAX_PHASES (85) */
);

/*
 * Initialize a Fig 4 instance.
 *
 * coin must NOT be 0.  Step 3 case (iii) (no decision-candidate
 * majority) invokes coin(coinClosure, instance, phase) to derive
 * value_p.  Even on input traces where case (iii) never fires, callers
 * must supply a valid coin: the library does not branch on a null coin
 * pointer, and supplying 0 is undefined behavior.
 *
 * instance is stored and handed back to the coin unexamined -- the
 * library never interprets it.  It exists so a caller running several
 * Fig 4 machines can tell them apart when naming a coin; a caller
 * running one can pass 0.
 */
void
bracha87Fig4Init(
  struct bracha87Fig4 *
 ,unsigned char            /* n: actual process count = n + 1 */
 ,unsigned char            /* t */
 ,unsigned char            /* maxPhases, <= BRACHA87_MAX_PHASES (85) */
 ,unsigned char            /* initialValue: 0 or 1 */
 ,unsigned char            /* instance: opaque, passed to coin */
 ,bracha87CoinFn           /* coin, must not be 0 */
 ,void *                   /* coinClosure */
);

/*
 * Process a validated round message (from Fig3).
 * Returns a bitmask of actions: 0, BRACHA87_BROADCAST,
 * BRACHA87_DECIDE | BRACHA87_BROADCAST, BRACHA87_DECIDE alone, or
 * BRACHA87_EXHAUSTED.  TEST THE BITS, never the whole value: the
 * decision arrives with or without a companion BROADCAST depending on
 * where in the phase space it lands, so an equality comparison against
 * one of the combinations above drops decisions.
 *
 * On BRACHA87_BROADCAST: caller reads fig4->value for the broadcast value
 *   and fig4->phase/fig4->subRound for the round number.
 * On BRACHA87_DECIDE: caller reads fig4->decision.
 * On BRACHA87_EXHAUSTED: all phases consumed without decision -- the
 *   machine can issue no new phase/round, so it will never decide.
 *   See the success-vs-stop note below.
 *
 * BRACHA87_DECIDE is returned exactly once (the first time >2t d-messages
 * are seen).  It carries BRACHA87_BROADCAST whenever a next round exists
 * to broadcast into -- per the paper, a decided process continues
 * participating so others can reach consensus.  A decision taken on the
 * LAST phase has no next round, so there it is returned alone: the
 * decision stands, and the continuation the paper asks for is simply
 * over.  This is the one path that yields a bare BRACHA87_DECIDE.
 *
 * DECIDE is a success signal, NOT a stop condition.  A decided
 * process keeps broadcasting (post-decide continuation, above) for as
 * long as the phase space lasts, so the caller must never treat DECIDE
 * as "done, stop."  Past the ceiling the continuation rounds run out
 * and Round returns 0 without setting BRACHA87_F4_EXHAUSTED -- that
 * flag reports a phase space spent WITHOUT a decision, and a decided
 * instance never earns it.  The same
 * holds for Fig 1's BRACHA87_ACCEPT -- reliable broadcast has no stop
 * condition at all (no EXHAUSTED, no phase ceiling).  Under unbounded
 * latency no process can know that stopping is safe, so when to stop
 * after a decision is an application policy, not a library event.
 * BRACHA87_EXHAUSTED is not a stop either: it reports that no new
 * phase/round can be issued (surfaced upward as
 * BKR94ACS_ACT_BA_EXHAUSTED); the application factors that into the
 * same abandonment policy.
 *
 * BRACHA87_EXHAUSTED is also returned at most once: it is mutually
 * exclusive with BRACHA87_DECIDE (decideV requires !haveDecided;
 * EXHAUSTED requires sub=2 of the last phase with !haveDecided &&
 * !decideV) and the maxPhases ceiling makes single output structural.
 * Subsequent calls to bracha87Fig4Round on an EXHAUSTED instance are
 * safe and return 0 actions; the state machine remains in EXHAUSTED.
 * No unilateral substitute decision is produced -- exhaustion means
 * no new phase/round can be issued; the application folds that into
 * its abandonment policy.
 *
 * Inbound message integrity is the caller's responsibility (sender
 * authentication, well-formed framing).  Within those bounds, malformed
 * Byzantine values are filtered structurally: fig3IsValid via fig4Nfn
 * rejects any value outside {0, 1, D_FLAG|0, D_FLAG|1} appropriate for
 * the round, so they are non-events that do not advance phase.  An
 * adversarial schedule can still prevent a local decision by
 * preventing n-t honest validations -- this is the asynchronous
 * impossibility result, not a defect.
 *
 * k NAMES THE ROUND THIS CALL COMPUTES, and it must be the machine's
 * own next round -- phase * BRACHA87_ROUNDS_PER_PHASE + subRound, the
 * two fields this call then advances.  Any other k is REFUSED (0
 * actions, no state change) rather than retargeting the machine at the
 * round the caller named: a Fig 4 is a sequence, and a caller that has
 * lost its place would otherwise silently recompute a spent round or
 * skip an unspent one.  Convert with BRACHA87_ROUNDS_PER_PHASE rather
 * than a bare 3:
 *   phase = k / BRACHA87_ROUNDS_PER_PHASE
 *   subRound = k % BRACHA87_ROUNDS_PER_PHASE
 *
 * THE GUARD REACHES EVERY ROUND THE MACHINE CAN STILL ADVANCE PAST,
 * and that is not quite every round.  A machine that decides in its
 * LAST phase returns BRACHA87_DECIDE without advancing -- there is no
 * next round to name -- so phase/subRound still spell that round and
 * it is not refused a second time.  Re-calling it recomputes a spent
 * round: the dispatch runs again over the values handed in, and the
 * post-decide arm then rewrites value from decision, so the decision
 * itself cannot change -- but the call is not the refusal the rule
 * above promises.  The same holds for a post-decide continuation that
 * ran out of phase space.  Both are terminals: read
 * BRACHA87_F4_DECIDED in fig4->flags and stop calling, rather than
 * driving a terminal round a second time.  The third terminal needs
 * no such care: an EXHAUSTED instance is refused by its own guard.
 *
 * A 0 return is therefore one of four things.  Three are readable off
 * state the caller already holds: k did not match (fig4->phase /
 * fig4->subRound say so), the instance is EXHAUSTED
 * (BRACHA87_F4_EXHAUSTED), or the post-decide continuation ran out of
 * phase space (BRACHA87_F4_DECIDED set, phase at the ceiling).  The
 * fourth leaves no trace and is the caller's own argument: an empty
 * set, n_msgs == 0, which computes nothing and advances nothing, so
 * the same k stays due.  Only a k mismatch is a caller error; an empty
 * set is the ordinary state of a round whose sample has not arrived.
 *
 * SENDERS ARE NOT A PARAMETER.  Fig 4's computation is over values
 * alone -- the majority at 3i+1, the d-message counts at 3i+2.  Sender
 * identity belongs to Fig 3, which consumes it building VALID^k (one
 * message per sender per round); by the time a set reaches here it is
 * already deduped, so the round needs only the values.  Callers still
 * gather senders for bracha87Fig3GetValid; they simply stop passing
 * them on.
 *
 * WHAT BRACHA87_BROADCAST OBLIGES, and where BPR enters.  Fig 3's
 * round(k) reads "Broadcast(p, k, v)", and Broadcast there is Fig 1 --
 * the reliable broadcast this file opens with, not a bare send.  So the
 * caller owns one Fig 1 instance per (round, initiator), calls
 * bracha87Fig1Initiator on its own for round k, and feeds arriving
 * round-k traffic through that instance; a Fig 1 ACCEPT is then what
 * bracha87Fig3Accept consumes.  This matters beyond fidelity: BPR lives
 * entirely on Fig 1 (bracha87Fig1Bpr, bracha87Fig1RetryStep).  A caller
 * that broadcasts a round value directly instead of through a Fig 1
 * gets NO retransmission for it, and under fair loss the round simply
 * stalls.  bkr94acs.c does this wiring for the BA rounds it owns and is
 * the worked example.
 *
 * WHEN TO CALL IT is the caller's, and n-t is a floor, not a moment.
 * Fig 3's "wait till n-t k-messages validated" names the evidence that
 * ENABLES the round; the asynchronous model has no moments to fire at.
 * The validated set keeps growing past n-t -- to n, via cascades and
 * late arrivals -- and the proofs hold for ANY sample of at least n-t,
 * so the sample this call consumes is purely a function of when the
 * caller makes it.  Calling on the n-t'th validation takes the smallest
 * legal sample; waiting takes a superset.  At sub 2 the gain is
 * one-directional (decide and adopt are monotone thresholds, so a
 * fuller sample can only turn coin phases into deterministic decides);
 * at sub 1 the majority is a comparison a fuller sample can flip, and
 * both outcomes are proof-covered, so the flip trades between sound
 * broadcasts and never against safety.  Deferring an enabled round
 * therefore costs liveness only.  bkr94acs.h's bkr94acsTurnDuty /
 * bkr94acsTurn make this pacing an explicit caller decision; a
 * bare-layer caller that wants the eager schedule simply calls here on
 * the n-t'th validation, which is sound and is the smallest sample.
 */
unsigned int
bracha87Fig4Round(
  struct bracha87Fig4 *
 ,unsigned char            /* round k (0-based); must be this machine's
                             * next round or the call is refused */
 ,unsigned int             /* n_msgs */
 ,const unsigned char *    /* values */
);

/*************************************************************************/
/*                                                                       */
/*  Retry infrastructure (cursor type shared across the library)         */
/*                                                                       */
/*  The cursor lives in caller storage; the library does not own it      */
/*  (no library-internal cursor, no hidden mutation, parallel sweeps     */
/*  over the same state are permitted).  Initialize with                 */
/*  bracha87RetryInit before first use by any Retry consumer --          */
/*  bracha87Fig1RetryStep below, or bkr94acsRetryStep (bkr94acs.h).      */
/*                                                                       */
/*  NETWORK FLOOD WARNING.  Every Retry consumer is one-call-per-tick.   */
/*  Do NOT loop.  BPR retries persist until their retire gates close     */
/*  (ACCEPTED / all-echoed / full READY suppress coverage; sent flags    */
/*  live forever), so until convergence every sent instance has          */
/*  actions; a `while (Retry(...))` loop empties the cursor space onto   */
/*  the wire as fast as the CPU runs, burning through kernel buffers     */
/*  and causing the very drops the retry exists to recover from.  The    */
/*  application's tick rate is the rate limit.  In healthy operation a   */
/*  Retry consumer returns >0 on every call until quiescence.            */
/*                                                                       */
/*  The 0 return appears only when a full sweep across the whole cursor  */
/*  space found no actions: either no sent instance exists yet           */
/*  (pre-broadcast / fully-shutdown state) or every sent instance has    */
/*  retired all its retries (quiescence -- every process announced       */
/*  accepted and holds this process's own announcement).  Neither is a   */
/*  termination signal by itself, and a later unmarked READY re-opens    */
/*  the READY retry for the one tick that re-sends marked.               */
/*                                                                       */
/*  Termination is the application's policy, not the library's, which    */
/*  prescribes none -- see BPR.md.  A policy needing sweep coverage      */
/*  reads the cursor's `sweeps` wrap count, comparing it against a       */
/*  saved value; one sweep covers every currently-sent instance once.    */
/*  Do NOT count calls against bracha87Fig1SentCount -- that is an       */
/*  upper bound on the calls a pass costs, since a sent instance whose   */
/*  retries have all retired is walked past without spending one.        */
/*                                                                       */
/*************************************************************************/

struct bracha87Retry {
  unsigned int pos;        /* next index to visit */
  unsigned int sweepActs;  /* actions output in current sweep */
  unsigned int sweeps;     /* completed passes; COMPARE against a saved
                            * value, never assume +1 -- one call can
                            * complete two passes */
};

void
bracha87RetryInit(
  struct bracha87Retry *
);

/*--------------------------------------------------------------------------*/
/*  Fig 1 array Retry -- BPR sweep over a caller-owned Fig 1 array          */
/*                                                                          */
/*  Wraps the per-instance bracha87Fig1Bpr above with a cursor that         */
/*  walks an application-owned array of Fig 1 instances.                    */
/*                                                                          */
/*  out[] holds BRACHA87_FIG1_RETRY_MAX_ACTS entries.  The bound is a       */
/*  constant, not a function of the configuration -- one call advances to   */
/*  one instance and a Fig 1 has three retryable actions -- so the caller   */
/*  declares the array with that constant and passes no size.  There is no  */
/*  under-sizing to diagnose and therefore no refusal that could be         */
/*  mistaken for the quiescent 0 return below.                              */
/*--------------------------------------------------------------------------*/

#define BRACHA87_FIG1_RETRY_MAX_ACTS 3

/*
 * One BPR retry action for one Fig 1 instance.
 *
 *   act       BRACHA87_INITIAL_ALL / ECHO_ALL / READY_ALL
 *   accepted  READY_ALL retry only: 1 iff this instance has ACCEPTED,
 *             so the caller sets the wire ACCEPTED bit on the retried
 *             READY -- the announcement that drives processes' per-process READY
 *             retire and the quiescence gate (bracha87Fig1Process-
 *             Accepted on ingress).  0 otherwise.  Mirror of
 *             bkr94acsAct.accepted; a bare-layer caller that wires this
 *             back (plus bracha87Fig1ProcessAccepted) gets the same READY
 *             quiescence the bkr94acs layer has.  Without it READY simply
 *             retries until application abandonment -- safe, just not
 *             quiescent.
 *   idx       index in the caller's instances array
 *   value     borrowed pointer into the Fig 1 instance's
 *             echoed-value slot, vLen+1 bytes; valid until the
 *             next call into that instance.  Caller copies if
 *             persistence is required past that boundary.
 */
struct bracha87Fig1Act {
  const unsigned char *value;
  const unsigned char *skip;  /* BPR per-process suppress mask, or 0 = all;
                               * see bracha87Fig1Skip.  Borrowed, valid
                               * until the next mutating call. */
  const unsigned char *received;/* READY_ALL: per-recipient RECEIVED mask
                                 * (set the wire RECEIVED bit for recipient
                                 * p iff bit p is set), or 0 on any other
                                 * act.  Unlike .accepted, which is one fact
                                 * about the sender, this is one bit per
                                 * recipient; see
                                 * bracha87Fig1Received.  Borrowed, same
                                 * lifetime as .skip. */
  unsigned int  idx;
  unsigned char act;
  unsigned char accepted;     /* READY_ALL: 1 = set wire ACCEPTED bit */
};

/*
 * One Fig 1's retry actions per call.  Walks the cursor forward to
 * the next sent instance and returns its actions.
 *
 * Call ONCE per application tick.  Do NOT loop -- see the network
 * flood warning above.  0 means a full sweep found no actions:
 * nothing sent yet, or every sent instance has quiesced
 * (all retries retired -- see the warning block above).
 *
 * Null entries in instances[] are skipped (useful when the
 * application's array is sparse -- e.g. one slot per (initiator, round)
 * but only some pairs have been allocated).
 */
unsigned int
bracha87Fig1RetryStep(
  struct bracha87Fig1 *const *  /* instances */
 ,unsigned int                  /* count */
 ,struct bracha87Retry *         /* init with bracha87RetryInit */
 ,struct bracha87Fig1Act *      /* out, room for
                                 * BRACHA87_FIG1_RETRY_MAX_ACTS entries */
);

/*
 * Count of instances with any sent flag (INITIATOR, ECHOED, or
 * RDSENT).
 *
 * DIAGNOSTIC, and an UPPER BOUND on the calls a sweep costs -- never
 * the sweep's length.  Sent flags are never cleared, so an instance
 * whose retries have all retired still counts here while
 * bracha87Fig1RetryStep walks past it inside a call without spending
 * one; the gap is the retired count and it grows as a run matures.  To
 * close a pass, compare the cursor's `sweeps` wrap count.  This
 * remains sound wherever an over-estimate is what is wanted: telling a
 * quiescent 0 return from the pre-broadcast one (nonzero here means
 * something was sent), and bounding a worst-case time to an
 * abandonment gate from above.
 */
unsigned int
bracha87Fig1SentCount(
  struct bracha87Fig1 *const *  /* instances */
 ,unsigned int                  /* count */
);

#endif /* BRACHA87_H */
