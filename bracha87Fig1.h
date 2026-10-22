/*
 * asynchronousByzantineAgreementProtocols - Bracha 1987 Figure 1
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

#ifndef BRACHA87FIG1_H
#define BRACHA87FIG1_H

/*
 * Bracha 1987, "Asynchronous Byzantine Agreement Protocols", Information
 * and Computation 75, 130-143.  Extract: Bracha87.txt.
 *
 * Figure 1, the broadcast primitive, by process p, as a state machine.
 * The figure's steps are rows of bracha87Fig1.dtc, verbatim; the rows
 * that decide when a satisfied step is taken, and what is re-sent, are
 * the caller's and BPR's, in the same file, and BPR's arm row is in
 * bracha87Fig1Ready.dtc; dtc compiles each file into a dispatch this
 * library runs, the second after the first.  Read the .dtc files for
 * the rules.
 *
 * One instance per process per broadcast.  The instance holds what the
 * figure's "wait until the receipt of ... (including messages received
 * in step ...)" reads: the (initial, v) from the initiator, one (echo, v)
 * and one (ready, v) per sender, and the steps this process has taken.
 * The caller owns the wire: it feeds every arrival to bracha87Fig1Input
 * and sends what the returned actions name.  Nothing here waits on a
 * named process, reads absence as information, or keeps time.
 *
 * Two events drive it, both fed through bracha87Fig1Input: the arrival
 * of a message of this broadcast, and a sweep of the caller's retry.
 * The paper assumes a reliable message system; over a fair-loss network
 * the caller sweeps, at a cadence of its own, and each sweep returns the
 * messages still owed (BPR).  The sweep is the only unit anything here
 * counts in.
 *
 * The caller's message beside (initial, v).  A caller may send a
 * message of its own to each process beside the initiator's (initial,
 * v), and have it re-sent with it.  It says so at Init, by the beside
 * flag, because the instance cannot infer it: a caller that sends
 * nothing beside and one whose message has not arrived look the same
 * from here, and absence is not information.  BESIDE is that message's
 * arrival, fed like any other; the library never sees its content.
 * The two arrive in either order: the instance keeps the first
 * (initial, v) from p, the caller keeps what rode beside it, and the
 * caller feeds BESIDE once it has validated that against the v the
 * instance holds (bracha87Fig1Value, BRACHA87_INITIAL_ALL); fed ahead
 * of the (initial, v) it is refused, since the caller cannot have.
 * What the flag changes is one thing: step 1 on p's (initial, v)
 * waits for the BESIDE, so this process echoes nothing of p's on p's
 * word alone, without the caller's validation, which is the one thing
 * the library cannot do for it.  The figure's other two ways into step
 * 1, enough (echo, v) or (ready, v) from the others, are the others'
 * word: the first correct process to echo v did so on p's (initial,
 * v), since both counts need an earlier correct echo or ready, so where
 * the callers set the flag alike a correct process validated v before
 * any correct process could accept it.  They stand as printed, as do
 * steps 2 and 3.  So Theorem 1 holds as printed for a faulty p: one
 * that sends this process one value and the others another is accepted
 * here, on their readies, under the value they accepted, or nowhere.
 * For a correct p, Lemma 4 also needs what p sends beside to reach the
 * correct processes and pass their validation.  With the flag clear
 * nothing rides beside: BESIDE is refused, the instance holds the
 * caller's nothing from Init (BRACHA87_F1_BESIDE is set there), and the
 * figure runs as printed, which is how bkr94acs makes the agreements'
 * own broadcasts.
 * The initiator re-sends (initial, v), and whatever rides beside it, to
 * the processes not recorded as holding both, which is their own word
 * (the HOLDING annotation below) and nothing inferred, until it
 * abandons.
 *
 * Abandon is a sweep count the caller gives at Init, 0 for none.  A
 * process counts its own sweeps from Init, which is where it starts:
 * the initiator takes step 0 there, every other process begins its
 * wait at step 1 there.  The sweep that makes the count returns
 * ABANDON, as the pass that makes bkr94acs's does.  At abandon the
 * seat is done with this broadcast, for good: after that sweep,
 * bracha87Fig1Input records nothing and returns nothing, and the calls
 * that read the instance answer as it stood then.  Abandon is
 * the only exit.  A process that receives nothing that enables step 1
 * cannot tell a faulty p from a slow one; it waits there, and abandon
 * is its only exit.  So an instance that stands alone is given a
 * count, unless its caller has an end of its own that lets the
 * instance go.  An instance held inside a larger protocol (Figure 3
 * and up) is given none: the counter is that protocol's seat's, the
 * seat abandons the whole of what it holds at once, and it stops
 * feeding this instance.  Under none nothing here counts, bracha87Fig1Sweeps
 * stays 0, and the instance re-sends on every sweep until nothing is
 * owed.  To every other process an abandoned seat is silence, and
 * silence is what a faulty process looks like, so an abandon is paid
 * out of t: one silence per seat, never one per instance.
 *
 * Three annotations, the caller's, riding the messages BPR already
 * re-sends.  A re-send is retired only by the evidence of the process
 * it was sent to, about the thing sent: no message of the figure says a
 * process holds the initial or has accepted, and since a process may
 * echo on the others' echoes, its echo alone says nothing about the
 * initial either.  The caller frames them from its own instance's
 * state on every message it sends or re-sends: BRACHA87_ANN_HOLDING on
 * an (echo, v) or (ready, v) when bracha87Fig1State has both
 * BRACHA87_F1_INITIAL and BRACHA87_F1_BESIDE; BRACHA87_ANN_ACCEPTED on
 * a (ready, v) when it has BRACHA87_F1_ACCEPTED; and
 * BRACHA87_ANN_RECEIVED on a (ready, v) to each recipient in
 * bracha87Fig1Received, the processes whose (ready, v) carried ACCEPTED
 * here.  It feeds them off each arriving (echo, v) and (ready, v)
 * through bracha87Fig1Input (bracha87Fig1.dtc and bracha87Fig1Ready.dtc,
 * BPR's rows).  The initiator's re-send of (initial, v) goes to each
 * process not recorded as holding it.  The step-1 send goes to all,
 * and the echo is re-sent to all until this process accepts; past
 * that, p's (initial, v) or BESIDE arriving at a process that holds
 * both is p's word that it has not heard, and the echo is re-sent once
 * more, to p alone (bracha87Fig1EchoRecipient), carrying HOLDING.  The
 * step-2 send goes to all; a
 * re-send on a sweep goes to each bracha87Fig1Recipient: the processes
 * not recorded as having accepted, and the processes whose (ready, v)
 * arrived without RECEIVED since this process accepted, once each.
 * When none is owed the re-send retires: quiescence, reachable under
 * loss and not guaranteed, and abandon carries what is left.
 *
 * Self is a recipient like any other.  The paper sends every message to
 * all the processes and counts its own, and the instance records its
 * own (echo, v) and (ready, v) only when the caller feeds them with
 * from == self.  The hand-back to self is the caller's, and it is
 * local: loopback on an egress interface is not reliable, so a caller
 * feeds its own message straight back, and runs the actions that
 * hand-back returns as it runs any other.  The hand-back is an input
 * like any other, and the answers of bracha87Fig1Recipient and
 * bracha87Fig1EchoRecipient for a sweep's re-sends stand until the next
 * sweep, so the order of a caller's sends and hand-backs within a sweep
 * does not matter.
 *
 * Nothing here aborts.  A refused argument is a 0 or -1 return and no
 * state change.
 */

/* Message kinds fed to bracha87Fig1Input. */
#define BRACHA87_INITIAL 0  /* (initial, v) from the initiator */
#define BRACHA87_ECHO    1  /* (echo, v) from any process */
#define BRACHA87_READY   2  /* (ready, v) from any process */
#define BRACHA87_BESIDE  3  /* the caller's message beside (initial, v) */
#define BRACHA87_SWEEP   4  /* a sweep of the caller's retry, no message */

/* Annotations an (echo, v) or (ready, v) carries, the caller's: the
 * annot argument to bracha87Fig1Input on an ECHO or a READY, and what
 * the caller sets on one it sends. */
#define BRACHA87_ANN_ACCEPTED 0x01  /* READY: the sender has accepted */
#define BRACHA87_ANN_RECEIVED 0x02  /* READY: the sender holds the recipient's accept */
#define BRACHA87_ANN_HOLDING  0x04  /* ECHO, READY: the sender holds p's (initial, v)
                                     * and what rode beside it */

/* Actions returned by bracha87Fig1Input, each at most once per call. */
#define BRACHA87_ECHO_ALL    0  /* send (echo, v) to all the processes;
                                 * on a sweep, re-send it to each
                                 * bracha87Fig1EchoRecipient */
#define BRACHA87_READY_ALL   1  /* send (ready, v) to all the processes;
                                 * on a sweep, re-send it to each
                                 * bracha87Fig1Recipient */
#define BRACHA87_ACCEPT      2  /* accept v */
#define BRACHA87_ABANDON     3  /* the instance stops re-sending */
#define BRACHA87_INITIAL_ALL 4  /* re-send (initial, v), and what rides
                                 * beside it, to every process that is
                                 * not a bracha87Fig1Holder; never
                                 * returned once every process is one */

/* Room the caller gives bracha87Fig1Input for actions: the three steps
 * on an arrival; on a sweep abandon alone, or the three re-sends. */
#define BRACHA87_FIG1_OUT 3

/* State bits returned by bracha87Fig1State. */
#define BRACHA87_F1_INITIATOR 0x01  /* this process is p, and holds v */
#define BRACHA87_F1_INITIAL   0x02  /* holds (initial, v) from p */
#define BRACHA87_F1_BESIDE    0x04  /* holds the caller's message from p;
                                     * from Init with the flag clear */
#define BRACHA87_F1_ECHOED    0x08  /* step 1 taken */
#define BRACHA87_F1_RDSENT    0x10  /* step 2 taken */
#define BRACHA87_F1_ACCEPTED  0x20  /* step 3 taken */
#define BRACHA87_F1_ABANDONED 0x40  /* re-sending stopped */

/* Test bit 'p' of a bitmap from bracha87Fig1Received. */
#define BRACHA87_BIT_TST(mask, p) \
  ((mask)[(p) >> 3] & (1 << ((p) & 7)))

struct bracha87Fig1;

/*
 * n and vLen are carried as n - 1 and vLen - 1, one byte each: there is
 * always at least one process and at least one byte of value, so 0..255
 * is 1..256, and more of either is prohibitive in any deployment.  A
 * byte carries each on the wire.  Process indices are bytes too.
 */

/*
 * An instance counts (echo, v) and (ready, v) per distinct v, and keeps
 * at most K distinct values of each kind: the first K that senders
 * send.  A sender of any other value is recorded as a sender, and so
 * never counted twice, and counts toward no value.  A count is never
 * more than the figure's, so Lemmas 1 and 2 hold as they stand.  K is
 * at most t + 1, and that is what the proofs need:
 *
 *   (ready, v): by Lemma 1 the correct processes send (ready, v) for
 *   one v, and the faulty add at most t others, so the ready values
 *   kept always include the one that t + 1 or 2t + 1 of them carry.
 *   Lemma 3 holds: a correct accept means t + 1 correct (ready, v),
 *   which take every other correct process through steps 1 and 2 on v
 *   from its ready values, no (initial, v) needed.
 *
 *   (echo, v): under a correct initiator the correct processes echo
 *   its one v, the faulty add at most t, and Lemma 4 holds.  Under a
 *   faulty initiator the correct processes may echo different values,
 *   its echo values can fill before a v the figure would have counted
 *   past (n + t)/2 arrives, and the process then sends no (ready, v)
 *   on echoes.  That needs n >= 3t + 3 (filling takes t + 1 senders of
 *   other values, leaving at most n - t - 1 for v, which exceeds
 *   (n + t)/2 only there) and costs a faulty initiator an accept the
 *   figure might have given it: Theorem 1's "or none", never a split.
 *
 * A caller whose values are drawn from fewer than t + 1 possibilities,
 * and who refuses the rest at its own door, gives that number instead.
 */

/*
 * Bytes to allocate for an instance of n processes carrying values of
 * vLen bytes, keeping K distinct values per kind, K as it will be given
 * to Init.  The count is not rounded: a caller that packs instances at
 * this stride aligns each to an unsigned int.  0 if K is 0.
 */
unsigned int
bracha87Fig1Sz(
  unsigned char  /* n - 1 */
 ,unsigned char  /* vLen - 1 */
 ,unsigned char  /* K: distinct values kept per kind */
);

/*
 * Initialize an instance: n processes, t faulty (n > 3t), values of vLen
 * bytes, K distinct values kept per kind (1..t + 1), the designated
 * initiator and this process (both < n), the abandon count in sweeps,
 * 0 for none, and whether the caller sends something beside (initial,
 * v) that the steps wait for (non-zero) or nothing (0).  0, or -1 with
 * the instance untouched.
 */
int
bracha87Fig1Init(
  struct bracha87Fig1 *
 ,unsigned char        /* n - 1 */
 ,unsigned char        /* t */
 ,unsigned char        /* vLen - 1 */
 ,unsigned char        /* K: distinct values kept per kind, 1..t + 1 */
 ,unsigned char        /* initiator */
 ,unsigned char        /* self */
 ,unsigned int         /* abandon: sweeps, 0 for none */
 ,unsigned char        /* beside: non-zero iff something rides beside (initial, v) */
);

/*
 * Step 0, by the initiator: take v as this broadcast's value.  The send
 * of (initial, v) to all the processes is the caller's, self included,
 * as is whatever the caller sends beside it.  0, or -1 if this process
 * is not the initiator, has already taken step 0 or been fed an
 * (initial, v), or value is null.
 */
int
bracha87Fig1Initiator(
  struct bracha87Fig1 *
 ,const unsigned char *  /* value: vLen bytes */
);

/*
 * Feed an event and take the actions it fires.
 *
 *   BRACHA87_INITIAL  from the initiator: recorded once; from anyone
 *                     else it is not this broadcast and is dropped
 *   BRACHA87_ECHO     recorded once per sender, counted under its value
 *                     while the K values kept have room for it;
 *                     carrying BRACHA87_ANN_HOLDING, its sender is
 *                     recorded as holding the initial, on every arrival
 *   BRACHA87_READY    recorded once per sender, counted likewise;
 *                     carrying BRACHA87_ANN_HOLDING or
 *                     BRACHA87_ANN_ACCEPTED, its sender is recorded as
 *                     holding the initial or having accepted, on every
 *                     arrival
 *   BRACHA87_BESIDE   from the initiator, with the beside flag set and
 *                     p's (initial, v) held, or this process p:
 *                     recorded; value ignored.  Refused otherwise
 *   BRACHA87_SWEEP    no sender, no value: the re-sends still owed
 *
 * A duplicate records nothing but the annotations and runs the
 * dispatch all the same; an abandoned instance records nothing and
 * runs nothing.  The
 * figure's "for some v" is taken by running the dispatch once per
 * distinct value in hand, so a step taken on one value and the next on
 * another are both the figure's; each step is taken once.
 *
 * Writes up to BRACHA87_FIG1_OUT actions to out, in the order ECHO_ALL,
 * READY_ALL, ACCEPT, ABANDON, then on a sweep INITIAL_ALL, ECHO_ALL,
 * READY_ALL for the re-sends, and returns how many.  A re-send is never
 * returned beside a step of the same message in one call.  0 for a null
 * instance or out, a kind outside the five, a sender outside 0..n-1, a
 * null value where one is read, or a BESIDE with the flag clear or
 * ahead of the (initial, v).
 */
unsigned int
bracha87Fig1Input(
  struct bracha87Fig1 *
 ,unsigned char          /* kind */
 ,unsigned char          /* from: sender index; ignored for a sweep */
 ,const unsigned char *  /* value: vLen bytes; ignored for BESIDE, sweep */
 ,unsigned char          /* annot: BRACHA87_ANN_* on an ECHO or a READY; ignored otherwise */
 ,unsigned char *        /* out: room for BRACHA87_FIG1_OUT */
);

/*
 * The value an action carries: for INITIAL_ALL the initiator's v -- at
 * the initiator its own, at any other process the v of the (initial, v)
 * it holds from p, which is the one a caller validates what rode beside
 * it against before feeding BESIDE -- for ECHO_ALL the value echoed, for READY_ALL the value readied, for
 * ACCEPT the value accepted.  vLen bytes, borrowed from the instance
 * and written once, when the act is taken, so the pointer stays valid
 * for the life of the instance.  0 for any other act, one not yet
 * taken, or a null instance.
 */
const unsigned char *
bracha87Fig1Value(
  const struct bracha87Fig1 *
 ,unsigned char            /* act */
);

/*
 * Non-zero iff process p is recorded as holding p's (initial, v) and
 * what rode beside it: an (echo, v) or (ready, v) from it carried
 * BRACHA87_ANN_HOLDING.  INITIAL_ALL goes to the processes that are
 * not.  Once recorded, always, and the next bracha87Fig1Input may
 * record more.  0 for a null instance or p outside 0..n-1.
 */
int
bracha87Fig1Holder(
  const struct bracha87Fig1 *
 ,unsigned char  /* p */
);

/*
 * Non-zero iff an ECHO_ALL returned on a sweep goes to process p: every
 * p until this process accepts; past that, the initiator alone, while
 * its (initial, v) or BESIDE arriving here has armed one echo to it.
 * It answers for the last sweep's ECHO_ALL until the next sweep: the
 * arm that re-send served stays answered through the inputs between.
 * 0 for a null instance or p outside 0..n-1.
 */
int
bracha87Fig1EchoRecipient(
  const struct bracha87Fig1 *
 ,unsigned char  /* p */
);

/*
 * Non-zero iff a READY_ALL returned on a sweep goes to process p: p is
 * not recorded as having accepted, or was armed by a (ready, v)
 * arriving without BRACHA87_ANN_RECEIVED since this process accepted.
 * It answers for the last sweep's READY_ALL until the next sweep: the
 * arms that re-send served stay answered through the inputs between,
 * and the next sweep clears them.  0 for a null instance or p outside
 * 0..n-1.
 */
int
bracha87Fig1Recipient(
  const struct bracha87Fig1 *
 ,unsigned char  /* p */
);

/*
 * The processes recorded as having accepted: a (ready, v) from each
 * carried BRACHA87_ANN_ACCEPTED.  The caller sets BRACHA87_ANN_RECEIVED
 * on a (ready, v) to each process in it.  A bitmap of (n + 7) / 8
 * bytes, bit p for process p, borrowed from the instance; bits are
 * set, never cleared, and the next bracha87Fig1Input may set more.  0
 * for a null instance.
 */
const unsigned char *
bracha87Fig1Received(
  const struct bracha87Fig1 *
);

/* The BRACHA87_F1_* bits.  0 for a null instance. */
unsigned int
bracha87Fig1State(
  const struct bracha87Fig1 *
);

/* This process's sweeps since Init, counted up to its abandon count.
 * 0 for a null instance. */
unsigned int
bracha87Fig1Sweeps(
  const struct bracha87Fig1 *
);

#endif /* BRACHA87FIG1_H */
