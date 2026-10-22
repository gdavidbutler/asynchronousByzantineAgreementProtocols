/*
 * asynchronousByzantineAgreementProtocols - BKR94 Agreement on a Common Subset
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

#ifndef BKR94ACS_H
#define BKR94ACS_H

/*
 * Ben-Or, Kelmer, Rabin 1994, "Asynchronous Secure Computations with
 * Optimal Resilience", PODC '94.  Extract: BKR94ACS.txt.  Section 4,
 * Protocol Agreement[Q], Figure 3, with Bracha 1987 Figure 4 as its BA
 * and Bracha's Figures 3 and 1 beneath that.  Extract: Bracha87.txt.
 *
 * One seat.  Everything here is one player, Pi, holding what the four
 * figures say a process holds and nothing about any other process:
 *
 *   bkr94acs.dtc       Agreement[Q]: enter 1 on Q(j), enter 0 past
 *                      n - t decisions of 1 and the patience, output
 *                      the subset at n, with the value of every j in it
 *   bracha87Fig4.dtc   each BA_j: N at a round's turn, the decision,
 *                      and the witness VALID reads
 *   bracha87Fig3.dtc   each round of a BA: validate, and the turn
 *   bracha87Fig1.dtc   each broadcast of a round: the six rows, the
 *                      caller's wait at step 1, the re-sends; with bracha87Fig1Ready.dtc, the
 *                      arm a (ready, v) sets
 *
 * Five dispatches, one per file, each compiled alone by dtc with its
 * own bridge and run in that order on every event; Figure 1's two run
 * inside bracha87Fig1Input, the instance's own library, and the other
 * three are included here.  The names one file produces and the next
 * reads are C locals between them.  Read the five .dtc files for the
 * rules.  Nothing here waits on a named process, reads absence as
 * information, or keeps time.
 *
 * What is agreed is values.  A player's message is one value, of vLen
 * bytes, broadcast by Figure 1, and Q(j) = 1 at a seat is that seat
 * having accepted j's value.  So the paper's two assumptions on Q are
 * Bracha's Lemmas 4 and 3, and a faulty P_j is held to one value at
 * every correct seat that accepts it or to none (Theorem 1): the
 * subset's j each have one value, the same at every correct seat.  That
 * is agreement, not truth: a faulty P_j that sends every seat the same
 * value has that value agreed exactly as a correct player's is.  So a
 * caller trusts what a value says only when t + 1 distinct j have it,
 * byte-identical, as bkr94acsValue gives them at this seat: at most t
 * players are faulty, so one of the t + 1 is a correct player's, and t
 * are not enough.  That holds with the subset or without it; every
 * correct seat that outputs the subset counts over the same values.
 * The value broadcast is the only way a player's message enters;
 * nothing a player sends outside it is agreed.
 *
 * The seat's instance space is n value broadcasts and n BAs, each BA
 * of up to maxRounds rounds, each round n Figure 1 instances, one per
 * broadcaster, carrying a one-byte value: 0, 1, (d, 0), (d, 1).
 * Rounds are the paper's, 1-based, 3i+1, 3i+2, 3i+3 for phase i, and
 * fit a byte: 85 phases are rounds 1..255.  Round 0 of (j, j) is j's
 * value broadcast, before BA_j.  A BA decides only at a phase's third
 * round, so maxRounds is taken up to the next multiple of 3: 1..3 is
 * 3, 4..6 is 6.  A BA that would need a round past
 * maxRounds is not run further: a decided one stops short of its last
 * phase, an undecided one needs what the seat cannot hold and the seat
 * abandons.
 *
 * Two events, as in Figure 1: an arrival, tagged by the BA, the
 * broadcaster q and the round k of the Figure 1 instance it belongs
 * to, and the sweep.  The sweep is a pass of a
 * cursor over every live instance, one instance per call, because a
 * pass may owe more re-sends than any one return holds; the pass
 * completing is what advances the seat's counters.
 *
 * Patience is a wait's: a round's, counted from the round's start and
 * read by the round's turn; and Agreement[Q]'s step 2, counted from the
 * n - t'th decision of 1.  Abandon is the seat's, counted from Init,
 * and every instance the seat holds stops running with it: after the
 * call that returns ABANDON, a sweep step or an arrival, bkr94acsInput,
 * bkr94acsBroadcast and bkr94acsSweep record nothing and return
 * nothing.  The calls that read the seat still answer, as it stood at
 * abandon, whether SUBSET was returned or not: bkr94acsValue gives the
 * value of every j accepted here, and bkr94acsInstance every Figure 1
 * instance's state and accepted value.  An instance's own
 * BRACHA87_F1_ABANDONED stays clear: the seat's abandon is
 * bkr94acsAbandoned.  To the other players an abandoned seat is one of
 * the t.  An abandon of 0 is none: the seat runs until the caller lets
 * it go, and the round ceiling is its one exit before that.
 *
 * At t = 0 the subset is all players, or this seat outputs none: it
 * abandons, or runs until its caller lets it go.  Step 2's count is
 * the honest count n - t, which is all n.
 *
 * The caller's message beside a value broadcast.  As in Figure 1
 * (bracha87Fig1.h), a caller may send a message of its own to each
 * player beside its value's (initial, v); with the beside flag set at
 * Init, a seat echoes a player's (initial, v) only holding the BESIDE
 * for it, the caller's validation, and runs the figure as printed
 * otherwise: on the others' echoes and readies, and at steps 2 and 3.
 * So Theorem 1 holds as printed for a faulty player; for a correct
 * one, Lemma 4 and so Q's first assumption also need what it sends
 * beside to reach the correct seats and pass their validation.  What
 * rides beside is not agreed and the library never sees it.  The
 * caller validates it against the value the instance holds, so with
 * the flag set a BESIDE ahead of its (initial, v) is refused.  Nothing
 * rides beside a BA's broadcast: those instances are made with Figure
 * 1's flag clear, as a value broadcast's are when this seat's flag is
 * clear, and a BESIDE for either is refused.
 *
 * The coin at Figure 4's step 3 (iii) is the caller's: a callback and
 * its context, given at Init, answering 0 or 1.
 *
 * Nothing here aborts.  A refused argument is a 0 or -1 return and no
 * state change.
 */

/* Kinds fed to bkr94acsInput, each tagging a Figure 1 instance
 * (ba, q, k). */
#define BKR94ACS_INITIAL 0  /* (initial, v) from q, the broadcaster */
#define BKR94ACS_ECHO    1  /* (echo, v) from any process */
#define BKR94ACS_READY   2  /* (ready, v) from any process */
#define BKR94ACS_BESIDE  3  /* the caller's message beside a value's
                             * (initial, v): round 0 only, under the
                             * beside flag */

/* Values a BA's message carries. */
#define BKR94ACS_V0  0
#define BKR94ACS_V1  1
#define BKR94ACS_D0  2  /* (d, 0) */
#define BKR94ACS_D1  3  /* (d, 1) */

/* Actions, in struct bkr94acsAct.act. */
#define BKR94ACS_SEND_INITIAL 0  /* (initial, value) of (ba, self, k) to every
                                  * process that is not a bkr94acsHolder; what
                                  * the caller sends beside it is its own */
#define BKR94ACS_SEND_ECHO    1  /* (echo, value) of (ba, q, k) to all;
                                  * from bkr94acsSweep, to each
                                  * bracha87Fig1EchoRecipient of the
                                  * instance (bkr94acsInstance); either
                                  * way carrying the annotation the
                                  * instance says (bracha87Fig1.h) */
#define BKR94ACS_SEND_READY   2  /* (ready, value) of (ba, q, k) to all;
                                  * from bkr94acsSweep, to each
                                  * bracha87Fig1Recipient of the
                                  * instance (bkr94acsInstance); either
                                  * way carrying the annotations the
                                  * instance says (bracha87Fig1.h) */
#define BKR94ACS_DECIDED      3  /* BA_ba decided value, 0 or 1, at round k:
                                  * the paper's "decides v at round 3r + 3",
                                  * a phase's third round */
#define BKR94ACS_SUBSET       4  /* the subset is output, the value of every
                                  * j in it accepted here: bkr94acsSubSet,
                                  * bkr94acsValue */
#define BKR94ACS_ABANDON      5  /* the seat stops running; its reads
                                  * still answer */

/*
 * A send at round 0 is of ba's value broadcast, and its vLen bytes do
 * not fit here: they are bracha87Fig1Value of bkr94acsInstance(a, ba,
 * ba, 0), for BRACHA87_INITIAL_ALL, BRACHA87_ECHO_ALL or
 * BRACHA87_READY_ALL as the send is SEND_INITIAL, SEND_ECHO or
 * SEND_READY.  The value echoed or readied can differ from the one
 * accepted, so a send carries its own.
 */
struct bkr94acsAct {
  unsigned char act;
  unsigned char ba;
  unsigned char q;      /* the broadcaster; self for SEND_INITIAL; 0 for
                         * DECIDED, which has none */
  unsigned char k;      /* the round, 1..maxRounds; 0 for ba's value;
                         * the deciding round for DECIDED */
  unsigned char value;  /* BKR94ACS_V0 .. BKR94ACS_D1; 0 at round 0 */
};

/* Test bit 'p' of the bitmap from bkr94acsSubSet. */
#define BKR94ACS_BIT_TST(mask, p) \
  ((mask)[(p) >> 3] & (1 << ((p) & 7)))

struct bkr94acs;

/*
 * Bytes to allocate for a seat of n players with values of vLen bytes,
 * holding maxRounds rounds per BA, taken up to a multiple of 3 as Init
 * takes it: n x maxRounds x n Figure 1 instances of a one-byte value,
 * bracha87Fig1Sz(n - 1, 0, K) bytes each rounded up to 8, K the lesser
 * of 4 and (n - 1) / 3 + 1, the most t + 1 can be; n more of vLen bytes
 * with K = (n - 1) / 3 + 1; and the seat's own state.  In bytes, with
 * one-byte values: 15176 at n = 7 with 6 rounds, 94677160 at n = 256
 * with 6, 4015825576 at n = 256 with 255.  The caller sizes the
 * rounds.  n - 1, vLen - 1 and maxRounds are bytes, 0..255 for
 * 1..256 players and bytes of value, and 0 refused for rounds.  The
 * total is the one quantity here that can pass 32 bits, so it alone is
 * an unsigned long.  0 if maxRounds is 0, or where an unsigned long
 * cannot hold the total, and bkr94acsInit refuses both.
 */
unsigned long
bkr94acsSz(
  unsigned char  /* n - 1 */
 ,unsigned char  /* vLen - 1 */
 ,unsigned char  /* maxRounds, 1..255 */
);

/*
 * Initialize a seat: n players, t faulty (n > 3t), values of vLen
 * bytes, this player, maxRounds per BA, the two sweep counts (abandon
 * > patience, or 0 for none), the coin and its context, and whether a
 * value's step 1 waits for the caller's BESIDE (non-zero) or not.  0,
 * or -1 with the seat untouched.
 */
int
bkr94acsInit(
  struct bkr94acs *
 ,unsigned char        /* n - 1 */
 ,unsigned char        /* t */
 ,unsigned char        /* vLen - 1 */
 ,unsigned char        /* self */
 ,unsigned char        /* maxRounds, 1..255, taken up to a multiple of 3 */
 ,unsigned int         /* patience, sweeps */
 ,unsigned int         /* abandon, sweeps; 0 for none */
 ,unsigned char (*)(void *)  /* coin: answers 0 or 1 */
 ,void *               /* the coin's context */
 ,unsigned char        /* beside: non-zero iff a value's step 1 waits for BESIDE */
);

/*
 * Actions one call can return at most: the room the caller gives
 * bkr94acsInput and bkr94acsSweep.  Derived from the seat: a broadcast
 * at every round of every BA, since an entry's turn chains through the
 * rounds already stored when patience is 0 and step 2 enters every BA
 * it can in one call; a decision per BA; the instance's own three
 * sends; the subset; abandon.  n x (maxRounds + 1) + 8.  0 for a null
 * seat.
 */
unsigned int
bkr94acsOut(
  const struct bkr94acs *
);

/*
 * Feed an arrival and take the actions it fires.  The instance is
 * (ba, q, k) and from is the sender.  At k = 0 it is ba's value
 * broadcast, q is ba, and value is vLen bytes, not read for BESIDE; at
 * k >= 1 it is a BA's broadcast, value is one byte, one of the four,
 * and BESIDE is refused.  annot is the BRACHA87_ANN_* bits an ECHO or
 * a READY carries, ignored otherwise.  A duplicate records nothing but
 * the annotations and runs the dispatches all the same; an abandoned
 * seat records nothing and runs nothing.  A message about this seat's
 * own broadcast before the seat has made it is dropped, as is an
 * INITIAL or BESIDE from other than q, a BESIDE with the beside flag
 * clear, and one with it set before ba's (initial, v) is held here;
 * none of these makes the instance.
 *
 * Writes up to bkr94acsOut actions and returns how many: the
 * instance's sends in Figure 1's order, then the sends and decisions
 * the validation chain and the turns fire, then SUBSET, then ABANDON.
 * The accept of a value broadcast returns no action of its own: it is
 * Q(ba) = 1, and what Agreement[Q] does with that is returned.  Only
 * an arrival at k = 0 can make it, and bkr94acsValue of ba reads it.
 * 0 for a null seat, out or value, a kind outside the four, ba, q,
 * from outside 0..n-1, k above maxRounds, q other than ba at k = 0, or
 * a BA's value outside the four.
 */
unsigned int
bkr94acsInput(
  struct bkr94acs *
 ,unsigned char          /* kind */
 ,unsigned char          /* ba */
 ,unsigned char          /* q: the broadcaster */
 ,unsigned char          /* k: the round; 0 for ba's value */
 ,unsigned char          /* from: the sender */
 ,const unsigned char *  /* value: vLen bytes at k = 0, one byte above */
 ,unsigned char          /* annot: BRACHA87_ANN_* on an ECHO or a READY */
 ,struct bkr94acsAct *   /* out: room for bkr94acsOut */
);

/*
 * Step 0 of this seat's value broadcast: take value, vLen bytes, as
 * what this player contributes.  Returns the one action, SEND_INITIAL
 * of (self, self, 0), written to out; the send to all the players,
 * self included by hand-back, is the caller's, as is whatever it sends
 * beside it, and the sweep re-sends both.  0 for a null seat, value or
 * out, an abandoned seat, or a second call: a player broadcasts once.
 */
unsigned int
bkr94acsBroadcast(
  struct bkr94acs *
 ,const unsigned char *  /* value: vLen bytes */
 ,struct bkr94acsAct *   /* out: room for bkr94acsOut */
);

/*
 * One step of the sweep: the re-sends the next live instance in the
 * seat's rotation owes, written to out, their count returned in nout.
 * Returns 1 while the pass continues and 0 when it has completed,
 * which is when the seat's counters advance and the abandon count can
 * return ABANDON.  The completing step also returns what the turns the
 * patience now allows fire, their chains among them, and so can return
 * a ceiling ABANDON.  A caller sweeps by calling until 0.  nout may
 * be 0 on a step that owes nothing.  Returns 0 with nout 0 for a null
 * seat, out or nout, or an abandoned seat.
 */
int
bkr94acsSweep(
  struct bkr94acs *
 ,struct bkr94acsAct *   /* out: room for bkr94acsOut */
 ,unsigned int *         /* nout */
);

/*
 * Non-zero iff process p is recorded as holding q's (initial, v) of
 * (ba, q, k) and what rode beside it, an echo or ready from it carrying
 * BRACHA87_ANN_HOLDING: SEND_INITIAL goes to the processes that are
 * not.  0 for a null seat, an instance outside the space, one no
 * message has made yet, or p outside 0..n-1.  At k = 0, with q = ba,
 * ba's value broadcast.
 */
int
bkr94acsHolder(
  const struct bkr94acs *
 ,unsigned char  /* ba */
 ,unsigned char  /* q */
 ,unsigned char  /* k */
 ,unsigned char  /* p */
);

struct bracha87Fig1;

/*
 * The Figure 1 instance (ba, q, k), for what the caller frames on a
 * (ready, v) of it: bracha87Fig1State for BRACHA87_ANN_ACCEPTED,
 * bracha87Fig1Received for BRACHA87_ANN_RECEIVED per recipient, and
 * after a sweep step that returned SEND_READY, bracha87Fig1Recipient
 * for who it goes to, an answer that stands until the instance's next
 * sweep step; and at k = 0, with q = ba, ba's value broadcast,
 * for bracha87Fig1Value of what a send of it carries.  After ABANDON,
 * bracha87Fig1State and bracha87Fig1Value of BRACHA87_ACCEPT read what
 * each instance held then; its BRACHA87_F1_ABANDONED stays clear, the
 * seat's abandon being bkr94acsAbandoned.  Borrowed from the seat.  0
 * for a null seat, an instance outside the space, or one no message
 * has made yet.
 */
const struct bracha87Fig1 *
bkr94acsInstance(
  const struct bkr94acs *
 ,unsigned char  /* ba */
 ,unsigned char  /* q */
 ,unsigned char  /* k */
);

/*
 * The subset, once SUBSET has been returned: bit j for each BA_j that
 * decided 1, whose value is bkr94acsValue.  0 before that or for a
 * null seat.
 */
const unsigned char *
bkr94acsSubSet(
  const struct bkr94acs *
);

/*
 * The value of j's broadcast as accepted at this seat, vLen bytes
 * borrowed from the seat, or 0 before it is accepted: non-zero is
 * Q(j) = 1 here, the accept of j's value broadcast.  Every correct
 * seat that has one for j, running Agreement[Q] or Figure 1 alone, has
 * the same one, which is j's word and no more.  Once SUBSET has been
 * returned it is there for every j in the subset; with SUBSET or
 * without, it is there after ABANDON for every j accepted before it,
 * though an abandoned seat is one of the t and the claim above is the
 * correct seats'.  0 for a null seat or j outside 0..n-1.
 */
const unsigned char *
bkr94acsValue(
  const struct bkr94acs *
 ,unsigned char  /* j */
);

/*
 * BA_ba's decision: BKR94ACS_V0 or BKR94ACS_V1 once DECIDED has been
 * returned for it, else -1.  -1 for a null seat or ba outside 0..n-1.
 */
int
bkr94acsDecision(
  const struct bkr94acs *
 ,unsigned char  /* ba */
);

/* The seat's sweeps since Init.  0 for a null seat. */
unsigned int
bkr94acsSweeps(
  const struct bkr94acs *
);

/* Non-zero once the seat has abandoned.  0 for a null seat. */
int
bkr94acsAbandoned(
  const struct bkr94acs *
);

#endif /* BKR94ACS_H */
