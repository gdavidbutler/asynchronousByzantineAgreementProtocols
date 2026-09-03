/*
 * asynchronousByzantineAgreementProtocols - BKR94 Asynchronous Common Subset
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
 * BKR94 Asynchronous Common Subset
 *
 * Direct implementation of Ben-Or/Kelmer/Rabin 1994, "Asynchronous
 * Secure Computations with Optimal Resilience (Extended Abstract),"
 * PODC '94, pages 183-192, Section 4 Figure 3 (Protocol
 * Agreement[Q]).  See BKR94ACS.txt for the line-by-line paper
 * extract this header and its companion .c file are aligned to.
 *
 * Composes Bracha87 Figure 1 (reliable broadcast) with Bracha87
 * Figure 4 (binary BA) into multi-value agreement on a common
 * subset.  N reliable broadcasts distribute A-Casts; N binary
 * BAs decide inclusion.
 *
 * BKR94 parameterizes the protocol by a predicate Q(j).  Under the
 * two paper assumptions -- (1) Q eventually equals 1 for every honest
 * process, (2) every honest process eventually learns Q(j) for every j
 * -- Protocol Agreement[Q] produces a common subset of size >= n-t of
 * processes for whom Q(j) = 1.
 *
 *   This deployment: Q(j) = "Fig1 reliable broadcast for process j
 *   has ACCEPTED" (the BKR94 MPC-construction equivalent is "P_j has
 *   properly shared his input").  Reliable broadcast gives both Q
 *   assumptions for free: Bracha87 Fig1 eventually accepts every
 *   honest broadcast at every honest receiver (Lemma 4, assumption
 *   (1)), and any accept at one honest receiver eventually reaches
 *   every honest receiver (Lemma 3, assumption (2)).
 *
 * The three Figure 3 steps, per process P_i:
 *
 *   Step 1. For each j where you know Q(j)=1, enter 1 in BA_j.
 *   Step 2. When 2t+1 BAs have terminated with output 1, enter 0 in
 *           every BA where you have not yet entered a value.
 *   Step 3. Once all N BAs terminate, SubSet = { j : BA_j = 1 }.
 *
 * The "2t+1" in Step 2 is n-t in the paper's regime (n = 3t+1) and
 * is the implementation threshold for all supported (n, t).
 *
 * Step 2's "upon" is enabling evidence in the paper's asynchronous
 * model (unbounded finite delay, no clocks), not a moment: the
 * count makes the fanout SOUND; the caller decides WHEN, pacing it
 * from the BPR sweep (bkr94acsFanoutDuty / bkr94acsFanout).
 * Firing at the instant the count holds -- zero patience
 * -- closes SubSet against every honest process whose A-Cast is
 * still in flight, and under a persistent latency spread that
 * excludes the same honest processes every ACS instance.
 *
 * The same reading governs the BA round turn.  Bracha's Fig4 "wait
 * until validate n-t k-messages" is enabling evidence too, and the
 * turn computes the round -- majority, the decide thresholds --
 * over a validated sample that is still growing (n-t up to n).  So
 * the arrival path only BANKS evidence (bkr94acsBaInput stores,
 * validates, cascades); the turn fires from the BPR sweep
 * (bkr94acsTurnDuty / bkr94acsTurn), caller-paced under the same
 * patience discipline, and BA_DECIDED / COMPLETE / BA_EXHAUSTED
 * emerge from turns, never from inputs.
 *
 * Pure state machine: no I/O, no threads, no dynamic allocation.
 * Caller provides memory and delivers messages.
 *
 * Two message classes on the network:
 *   BKR94ACS_CLS_ACAST  -- Fig1 messages carrying A-Cast values
 *   BKR94ACS_CLS_BA -- Fig1 messages for per-process binary BA
 *
 * Operational limits:
 *   n:         unsigned char, encodes process count 1..256 (n + 1)
 *   t:         unsigned char, max 85 (n + 1 > 3t required)
 *   vLen:      unsigned char, encodes A-Cast length 1..256 (vLen + 1)
 *   maxPhases: unsigned char, for binary BA (per BKR94 BA instance)
 */

#ifndef BKR94ACS_H
#define BKR94ACS_H

#include "bracha87.h"

/*
 * Maximum processes for BKR94 ACS.
 * Bounded by the Bracha87 Fig1/Fig4 limits (unsigned char addressing).
 */
#define BKR94ACS_MAX_PROCESSES 256

/**************************************************************************/
/*  Message classes                                                       */
/*                                                                        */
/*  CANONICAL PACKED WIRE BYTE.  Every network message carries a Bracha87 */
/*  type (BRACHA87_INITIAL/ECHO/READY) and one of these two classes.  The */
/*  constant values are chosen bit-disjoint so an application that frames */
/*  its own wire format can pack the whole per-message discriminator into */
/*  ONE byte with no shifts.  The library never serializes -- it consumes */
/*  class structurally (which entry function the caller invokes), not as  */
/*  a stored value -- so these positions are a CONTRACT FOR PACKERS, not  */
/*  a format the library reads or writes.  The bundled example,           */
/*  example/bkr94acs.c, frame to this layout; new framers should too.     */
/*                                                                        */
/*    bit:  7      | 6     | 5         | 4        | 3  | 2   | 1 0       */
/*          D_FLAG   (app)   RECEIVED    ACCEPTED   cv   cls   type       */
/*                                                                        */
/*  Fixed by library constants:                                           */
/*    type     bits 0-1  (BRACHA87_TYPE_MASK = 0x03): INITIAL/ECHO/READY. */
/*    cls      bit  2    (BKR94ACS_CLS_MASK  = 0x04): ACAST=0x00,         */
/*                       BA=0x04.                                         */
/*    D_FLAG   bit  7    (BRACHA87_D_FLAG = 0x80): on a BA message,       */
/*                       the Fig4 decision-candidate flag.                */
/*    ACCEPTED bit  4    (BKR94ACS_ACCEPTED = 0x10): on a READY message,  */
/*                       the sender has accepted this Fig1 instance, so   */
/*                       the receiver retires its per-process READY retry */
/*                       to the sender (BPR; struct bkr94acsAct.accepted  */
/*                       on egress, the annot argument of                 */
/*                       bkr94acs*Input on ingress).                      */
/*                       Unlike D_FLAG it is class-independent -- valid   */
/*                       on an ACAST or BA READY (every Fig1 accepts).    */
/*    RECEIVED bit  5    (BKR94ACS_RECEIVED = 0x20): on a READY message,  */
/*                       the sender has already recorded THIS RECIPIENT'S */
/*                       accept -- "your accept was received here."       */
/*                       Class-independent like ACCEPTED, but decided     */
/*                       per recipient rather than per sender: the        */
/*                       framer sets it for recipient p iff bit p is set  */
/*                       in struct bkr94acsAct.received.  Its ABSENCE is  */
/*                       the live signal -- the receiver hands the byte   */
/*                       to bkr94acs*Input as annot, and the absence      */
/*                       un-suppresses the sender for one READY egress    */
/*                       so the announcement it is waiting for finally    */
/*                       goes out.                                        */
/*  Convention (not forced by a constant, but shared by all examples):    */
/*    cv       bit  3:   a BA message's binary value.  Placed             */
/*                       adjacent to cls.                                 */
/*    bit  6:            free for application message classes.            */
/*                                                                        */
/*  Compose / recover a library message:                                  */
/*    byte = cls | type [ | (cv << 3) | (value & BRACHA87_D_FLAG) ]       */
/*    type = byte & BRACHA87_TYPE_MASK                                    */
/*    cls  = byte & BKR94ACS_CLS_MASK                                     */
/*    BA value = ((byte >> 3) & 1) | (byte & BRACHA87_D_FLAG)             */
/*                                                                        */
/*  A BA message's entire payload is just those two live bits             */
/*  (value + D_FLAG), so folding them into this byte lets a BA            */
/*  message carry NO value bytes on the wire -- the dominant message      */
/*  class in ACS, so the saving compounds.  An ACAST message carries      */
/*  its vLen+1-byte value as the payload.                                 */
/**************************************************************************/

#define BKR94ACS_CLS_ACAST  0x00 /* Fig1 reliable broadcast of A-Casts */
#define BKR94ACS_CLS_BA 0x04 /* Fig1 messages for binary BA */
#define BKR94ACS_CLS_MASK      0x04 /* recover class from a packed byte */
#define BKR94ACS_ACCEPTED      0x10 /* on a READY: sender accepted this Fig1
                                     * instance (BPR per-process READY retire) */
#define BKR94ACS_RECEIVED      0x20 /* on a READY: sender has recorded the
                                     * RECIPIENT's accept of this Fig1 instance
                                     * -- "your accept was received here" */

/*************************************************************************/
/*  Output actions                                                       */
/*                                                                       */
/*  Returned in struct bkr94acsAct array from bkr94acsAcastInput,        */
/*  bkr94acsBaInput, bkr94acsRetryStep, and bkr94acsAcast calls.         */
/*  Caller sends the described messages on the network.                  */
/*************************************************************************/

#define BKR94ACS_ACT_ACAST_SEND    1  /* send A-Cast Fig1 msg: .type, .value, .process */
#define BKR94ACS_ACT_BA_SEND     2  /* send BA Fig1 msg: .type, .baValue, .process, .round, .initiator */
#define BKR94ACS_ACT_BA_DECIDED   3  /* BA for .process decided .baValue */
#define BKR94ACS_ACT_COMPLETE     4  /* all N BAs decided; common subset final */
#define BKR94ACS_ACT_BA_EXHAUSTED 5  /* BA for .process reached maxPhases with no decision; this ACS instance cannot complete */

/*
 * struct bkr94acsAct
 *
 * BA_DECIDED and COMPLETE are success signals -- a decision was
 * reached -- and are NOT stop conditions: post-decide continuation
 * requires the process to keep broadcasting past both.  BA_EXHAUSTED
 * reports that the BA can issue no new phase/round and so will never
 * decide -- COMPLETE is unreachable.  Stopping is unspecified by the
 * library under unbounded latency; it is the application's
 * abandonment policy.
 *
 * Field usage by act:
 *   ACAST_SEND     .process, .type (BRACHA87_INITIAL/ECHO/READY), .value (vLen+1 bytes)
 *   BA_SEND      .process, .round, .initiator, .type, .baValue (binary)
 *   BA_DECIDED    .process, .baValue (0=excluded, 1=included)
 *   COMPLETE      (no fields)
 *   BA_EXHAUSTED  .process (BA's Fig4 returned BRACHA87_EXHAUSTED;
 *                 BKR94 Lemma 2 Part B's "all BAs terminate"
 *                 assumption is violated for this instance, so
 *                 the local process cannot reach BKR94ACS_ACT_COMPLETE.
 *                 No safe in-protocol recovery: any unilateral
 *                 substitute decision could disagree with a remote
 *                 process's actual decision, breaking SubSet agreement
 *                 (Part C).  The application surfaces it as the run's
 *                 failure cause and exits through its abandonment
 *                 policy, optionally restarting with fresh state.
 *                 Output exactly once per BA per ACS instance.)
 *
 * .value is a borrowed pointer into library-owned storage (the
 * Fig1's echoed-value slot -- populated as soon as INITIATOR, Rule
 * 1, 2, or 3 echoes a value, i.e. while ECHOED is set, before
 * ACCEPT).  Valid until the next call into the library on the
 * same struct bkr94acs that mutates state.  Caller must copy if
 * persistence beyond that boundary is needed.
 *
 * Distinct from bkr94acsAcastValue, which queries the same
 * physical slot but is ACCEPT-gated for non-self processes so
 * application reads see only Bracha-Lemma-2-protected values.
 * ACAST_SEND outputs need the sent bytes pre-ACCEPT for the
 * protocol's ECHO/READY traffic to carry, so this field exposes
 * the broader gate.
 */
struct bkr94acsAct {
  const unsigned char *value; /* ACAST_SEND: vLen+1 bytes; otherwise 0 */
  const unsigned char *skip;  /* ACAST_SEND/BA_SEND: BPR per-process suppress
                               * mask (process p skipped iff bit p set), or 0 =
                               * broadcast to all; bracha87Fig1Skip.  Borrowed,
                               * valid until the next mutating library call. */
  const unsigned char *received;/* ACAST_SEND/BA_SEND READY: per-recipient
                                 * RECEIVED mask -- set BKR94ACS_RECEIVED for
                                 * recipient p iff bit p is set
                                 * (BRACHA87_SKIP_TST reads it); 0 on any other
                                 * act, meaning no recipient is marked.  Where
                                 * .accepted is one fact about the sender, this
                                 * is one bit per recipient; bracha87Fig1Received.
                                 * Borrowed, same lifetime as .skip. */
  unsigned char act;          /* BKR94ACS_ACT_* */
  unsigned char process;       /* which process this relates to */
  unsigned char round;        /* BA round (BA_SEND only) */
  unsigned char type;         /* BRACHA87_INITIAL/ECHO/READY (ACAST_SEND, BA_SEND) */
  unsigned char baValue;     /* binary value (BA_SEND, BA_DECIDED) */
  unsigned char initiator;  /* who initiated this Fig1 broadcast (BA_SEND) */
  unsigned char accepted;     /* ACAST_SEND/BA_SEND READY: 1 = set the
                               * BKR94ACS_ACCEPTED wire bit (this instance has
                               * accepted); 0 otherwise.  The receiving process
                               * feeds it back on the annot argument of
                               * bkr94acs*Input. */
};

/*************************************************************************/
/*  BKR94 ACS state                                                      */
/*************************************************************************/

struct bkr94acs {
  unsigned char n;          /* process count encoding: actual = n + 1 */
  unsigned char t;          /* max Byzantine (n + 1 > 3t) */
  unsigned char vLen;       /* A-Cast value length encoding: actual = vLen + 1 */
  unsigned char maxPhases;  /* per binary BA instance */
  unsigned char self;       /* this process's index (needed for BA routing) */
  unsigned char complete;   /* boolean: all N BAs decided (Step 3); set
                             * once, never cleared.  A stand-alone byte,
                             * not a flag bitmap: it is the struct's one
                             * boolean, and completion is the caller's
                             * primary query -- read a->complete
                             * directly. */
  /*
   * The BKR94 step-2 / step-3 decision counts are not stored: a
   * stored counter is a denormalization of baDecision[] (and, as the
   * unsigned char it once was, wrapped on the 256th decision so
   * BKR94ACS_ACT_COMPLETE could never fire at 256 processes).  They
   * are derived by scanning baDecision[] on demand: the step-3 count
   * once per BA decision (bkr94acsTurn's DECIDE branch, a rare
   * event), the step-2 count at every bkr94acsFanoutDuty query --
   * once per sweep for a paced caller, an O(N) walk that is the
   * price of not storing a wrappable counter.
   *
   * pad puts data[] at offset 8 -- a multiple of sizeof (void *) on
   * all common 32- and 64-bit ABIs -- so data[] starts at the
   * alignment required by the function-pointer fields in the
   * Fig1/Fig4 instances carved out of it.
   */
  unsigned char pad[2];
  unsigned char data[1];    /* variable: see bkr94acsSz */
};

/* data[] is the variable tail; see bkr94acs.c for layout. */

/*
 * Size in bytes needed for a BKR94 ACS instance, or 0 if the
 * configuration cannot be built -- the same refusal every bracha87*Sz
 * makes.  n and vLen are refused for the reason stated at
 * bracha87Fig1Sz: they are WIDER here than bkr94acsInit's unsigned
 * char, so Sz is the only entry that can see one out of range.
 * maxPhases is refused rather than clamped (bracha87Fig4Sz clamps it)
 * because bkr94acsInit refuses the same values by returning
 * uninitialized -- the allocation and the machine decline together.
 */
unsigned long
bkr94acsSz(
  unsigned int             /* n: actual process count = n + 1; > 255 refused */
 ,unsigned int             /* vLen: actual A-Cast length = vLen + 1;
                             * > 255 refused */
 ,unsigned int             /* maxPhases: per binary BA instance;
                             * 0 or > BRACHA87_MAX_PHASES (85) refused */
);

/*
 * Initialize a BKR94 ACS instance. Caller has allocated bkr94acsSz bytes.
 *
 * The N binary BAs share the single (coin, coin closure) supplied here.
 * Each is given the process index it decides on as its bracha87CoinFn
 * instance argument, so the coin can name them apart -- without that a
 * common coin would hand every BA in a phase the identical value.  The
 * index is the BA's identity, not this process's, so it is the same
 * name at every process, which is what a common coin must agree on.  A
 * local coin ignores it and draws fresh entropy per call.
 */
void
bkr94acsInit(
  struct bkr94acs *
 ,unsigned char            /* n: actual process count = n + 1 */
 ,unsigned char            /* t */
 ,unsigned char            /* vLen: actual A-Cast length = vLen + 1 */
 ,unsigned char            /* maxPhases */
 ,unsigned char            /* self: this process's index */
 ,bracha87CoinFn           /* coin function, must be non-null */
 ,void *                   /* coin closure */
);

/*
 * Maximum output actions from a single library call.
 *
 * A-Cast input (BKR94ACS_CLS_ACAST):
 *   up to 2 ACAST_SEND (echo/ready) + 1 BA_SEND (enter-1 from BKR94 Step 1
 *   on accept).  Step 2's enter-0 fanout lives on the BPR sweep
 *   (bkr94acsFanout), not here.  Bound: 3.
 *
 * BA input (BKR94ACS_CLS_BA):
 *   up to 2 (echo/ready) from the Fig1 input.  The input only BANKS
 *   evidence (Fig3 store/validate/cascade); round turns and their
 *   acts live on the BPR sweep (bkr94acsTurn), not here.  Bound: 2.
 *
 * Turn (bkr94acsTurn, one BA round fired from the sweep):
 *   1 BA_SEND (next-round INITIAL) + 1 of BA_DECIDED or BA_EXHAUSTED
 *   + 1 COMPLETE.  Bound: 3.
 *
 * Fanout (bkr94acsFanout, BKR94 Step 2 fired from the sweep):
 *   one BA_SEND per BA unentered at the call.  Bound: N = n + 1.
 *
 * BKR94ACS_MAX_ACTS is the umbrella -- the exact maximum over every
 * entry above, so one out[] sized by it serves a whole caller loop.
 * Fanout is the only entry that is not bounded by 3, which is why the
 * umbrella scales with n at all; the max() floor matters only where
 * n + 1 falls below 3, at the one- and two-process configurations t=0
 * admits.
 *
 * Where that array lives is the caller's -- the library never
 * allocates.  It is not the memory that matters here: struct bkr94acs
 * is O(N^2 * maxRounds) Fig 1 instances, so at every configuration
 * that can actually be allocated the act array is negligible beside
 * the state it reports on.
 */
#define BKR94ACS_MAX_ACTS(n) \
  ((unsigned int)(n) + 1 > 3u ? (unsigned int)(n) + 1 : 3u)

/*
 * Maximum output actions from a single bkr94acsRetryStep call.
 *
 * The cursor visits one Fig1 instance per retry call, and a Fig 1 has
 * three retryable actions (INITIAL_ALL + ECHO_ALL + READY_ALL), which
 * is what BRACHA87_FIG1_RETRY_MAX_ACTS already names.  This entry only
 * re-tags them as struct bkr94acsAct (ACAST_SEND or BA_SEND, with
 * process / round / initiator / type filled from the cursor position),
 * so the bound is that one, spelled at this layer rather than
 * duplicated as a second literal.
 */
#define BKR94ACS_RETRY_MAX_ACTS  BRACHA87_FIG1_RETRY_MAX_ACTS

/*
 * Process an A-Cast broadcast message (BKR94ACS_CLS_ACAST).
 *
 * These are Fig1 messages carrying A-Cast values.
 * Returns number of actions written to out[].
 * Caller provides out[] with room for BKR94ACS_MAX_ACTS(n) entries.
 *
 * On BKR94ACS_ACT_ACAST_SEND:
 *   Caller broadcasts an A-Cast Fig1 message of .type
 *   (BRACHA87_INITIAL/ECHO/READY) for .process.  Bytes to send:
 *   .value (vLen+1 bytes, borrowed pointer into the library's
 *   echoed-value slot -- see struct bkr94acsAct.value).
 *
 * On BKR94ACS_ACT_BA_SEND:
 *   Caller broadcasts a BA Fig1 message.
 *   Fields: .process, .round, .initiator, .type, .baValue.
 *
 * annot carries the READY annotations this message arrived with --
 * BKR94ACS_ACCEPTED and BKR94ACS_RECEIVED.  Only those two bits are
 * read, and only when type is BRACHA87_READY, so a caller may pass the
 * whole packed discriminator byte unmasked and every other bit is
 * ignored.
 *
 * 0 IS NOT A NEUTRAL ANNOT.  On a READY the ABSENCE of RECEIVED is
 * itself a claim -- "I do not hold your accept" -- and it arms the
 * re-send described below.  A driver that does not model the exchange
 * at all passes BKR94ACS_RECEIVED, which arms nothing; passing 0
 * instead re-arms every post-accept READY, so the suppress mask never
 * holds and the READY retire never converges.
 *
 * THE ANNOTATIONS ARE ROUTED HERE, and the routing is not a courtesy --
 * it is the only way to get the order right.  ACCEPTED must reach the
 * Fig1 after that READY's own sender record (so acFrom stays a subset of
 * rdFrom), and the re-send arm must be taken from the ABSENCE of
 * RECEIVED, after any accept this same message caused.  Both are facts
 * about one message, so both are arguments to the one call that carries
 * it, and a caller cannot get the sequence wrong by writing it in the
 * wrong order or by arming on a READY that was already marked.
 *
 * The arm still escapes Fig 1's duplicate dedup, which is what made a
 * separate entry look necessary: an unmarked re-send is a DUPLICATE
 * (ready, v) that bracha87Fig1Input returns 0 for, so the routing runs
 * on the message whether or not the Fig 1 produced any action.
 *
 * Byzantine-safe both ways.  A forged ACCEPTED marks only its own
 * sender, so it retires this process's retry to the liar alone and can
 * never strand a correct laggard.  A forged missing RECEIVED
 * un-suppresses only its own sender, buying the forger one masked READY
 * per sweep aimed at itself.
 *
 * CALLER OBLIGATION, the one the library cannot absorb: a caller that
 * parks a process on bkr94acsRetryStep's quiescent 0 return must
 * un-park it when an unmarked READY arrives -- type == BRACHA87_READY
 * with no BKR94ACS_RECEIVED, the same two bits handed in as annot.
 * The library re-opens the READY retire here, but only a tick can
 * re-send, and a parked process never ticks.  Skipping the re-entry
 * costs nothing while nothing is lost, and forfeits the whole
 * fair-loss recovery the moment a marked re-send is dropped: its
 * target re-sends unmarked forever into a process that has stopped
 * listening.
 */
unsigned int
bkr94acsAcastInput(
  struct bkr94acs *
 ,unsigned char            /* process: whose A-Cast */
 ,unsigned char            /* type: BRACHA87_INITIAL/ECHO/READY */
 ,unsigned char            /* annot: READY annotations as received --
                             * BKR94ACS_ACCEPTED / BKR94ACS_RECEIVED;
                             * the raw wire byte is accepted */
 ,unsigned char            /* from: sender of this message */
 ,const unsigned char *    /* value: vLen + 1 bytes */
 ,struct bkr94acsAct *     /* out: actions, room for BKR94ACS_MAX_ACTS(n) */
);

/*
 * Process a BA message (BKR94ACS_CLS_BA).
 *
 * These are Fig1 messages for the binary BA on process's inclusion.
 * Returns number of actions written to out[].
 * Caller provides out[] with room for BKR94ACS_MAX_ACTS(n) entries.
 *
 * The BA for each process is a full Fig1+Fig3+Fig4 pipeline; this
 * entry BANKS evidence only -- the Fig1 input's echo/ready traffic
 * (at most 2 BKR94ACS_ACT_BA_SEND acts, which the caller sends to
 * all processes) and, on a Fig1 ACCEPT, the Fig3 store/validate/
 * cascade.  It never turns a round: BA_DECIDED / COMPLETE /
 * BA_EXHAUSTED emerge from bkr94acsTurn on the BPR sweep, where the
 * caller paces the sample each round is computed over.
 *
 * annot is the same argument bkr94acsAcastInput takes, read the same
 * way and routed at the same point -- see it there, including why 0 is
 * not a neutral value and the parking obligation an unmarked READY
 * carries.  The only difference is which Fig1 the annotations reach:
 * the (process, round, initiator) instance this call names.
 */
unsigned int
bkr94acsBaInput(
  struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
 ,unsigned char            /* round: BA round (0-based) */
 ,unsigned char            /* initiator: who initiated this Fig1 broadcast */
 ,unsigned char            /* type: BRACHA87_INITIAL/ECHO/READY */
 ,unsigned char            /* annot: READY annotations as received --
                             * BKR94ACS_ACCEPTED / BKR94ACS_RECEIVED;
                             * the raw wire byte is accepted */
 ,unsigned char            /* from: sender of this message */
 ,unsigned char            /* value: binary BA value */
 ,struct bkr94acsAct *     /* out: actions, room for BKR94ACS_MAX_ACTS(n) */
);

/*
 * Query: get the decided common subset.
 * Returns count of included processes.
 * Fills processes[] with the included process indices (caller provides n + 1 entries).
 * Only valid after a->complete is non-zero.
 */
unsigned int
bkr94acsSubset(
  const struct bkr94acs *
 ,unsigned char *          /* processes out, n + 1 entries */
);

/*
 * Query: get the accepted A-Cast value for a process.
 * Returns pointer to the vLen + 1 byte value, or 0 if not yet
 * accepted (or, for self-process, not yet A-Cast).
 *
 * Reads the same physical slot as struct bkr94acsAct.value, but
 * ACCEPT-gated for non-self processes so callers see only values
 * Bracha 1987 Lemma 2 protects against Byzantine equivocation.
 * Pre-ACCEPT echoed values can disagree across honest processes and
 * are intentionally hidden here.
 */
const unsigned char *
bkr94acsAcastValue(
  const struct bkr94acs *
 ,unsigned char            /* process */
);

/*
 * Submit this process's A-Cast value.
 *
 * Marks the local A-Cast Fig1 (process = self) as the broadcast
 * initiator and stores the value to be broadcast.  Returns one
 * action (BKR94ACS_ACT_ACAST_SEND with .process = self,
 * .type = BRACHA87_INITIAL) for the caller to broadcast
 * immediately.  Thereafter bkr94acsRetryStep keeps outputting the same
 * ACAST_SEND/INITIAL on every sweep until that INITIAL retires; once
 * the local loopback or process echoes set F1_ECHOED, ACAST_SEND/ECHO
 * is output alongside it, and once F1_RDSENT is set, ACAST_SEND/READY
 * joins too.  The three streams retire INDEPENDENTLY, each at the
 * point it is provably owed to no correct process (Implementation
 * Note 11, and the retry banner in bracha87.h, which states the gates):
 *   INITIAL  at ACCEPTED, or once every process has echoed
 *   ECHO     at ACCEPTED
 *   READY    never on local state -- only on the remote fact that
 *            every process has accepted AND holds this instance's
 *            accept
 * The sent flags themselves are never cleared, so "the flag is set" is
 * NOT the retry condition; the gates above are.  What the gates have in
 * common is that each is strictly stronger than local saturation:
 * BPR's purpose is to help OTHER processes progress, so stopping a
 * stream because this process is done with it would strand them.
 *
 * Caller reads the value back via bkr94acsAcastValue(self).
 *
 * Returns 0 if a is null.  Idempotent on the value pointer:
 * re-calling overwrites the stored value.
 */
unsigned int
bkr94acsAcast(
  struct bkr94acs *
 ,const unsigned char *    /* value: vLen + 1 bytes */
 ,struct bkr94acsAct *     /* out: room for 1 entry */
);

/*
 * Bracha Phase Retry retry tick.
 *
 * End-to-end argument applied to BKR94 ACS (Saltzer/Reed/Clark
 * 1984; see SRC84.txt and BPR.md, the governing statement): the
 * "still owed" predicate combines Bracha's sent flags with
 * this layer's per-process BA-decided state, all of which live
 * at the BKR endpoint.
 *
 * Same one-call-per-tick semantic as bracha87Fig1RetryStep -- see
 * the network flood warning in bracha87.h.  The cursor (struct
 * bracha87Retry) lives in caller storage, initialized with
 * bracha87RetryInit; it walks the (A-Cast, then BA by
 * process x round x initiator) Fig1 instance space, finds the
 * next sent instance, and outputs its actions as struct
 * bkr94acsAct entries.
 *
 * Returns 0 only when a full sweep finds nothing to output: no sent
 * instance yet (pre-broadcast / shutdown state), or every sent
 * instance has retired all its retries -- quiescence, which under
 * fair loss is REACHABLE rather than guaranteed, since the honest
 * residue bracha87.h's retry banner names (a process that abandons
 * early, or never announces) holds the READY gate open for good.
 * Neither is a per-tick termination signal, and an unmarked READY
 * arriving later re-opens one instance's READY retry for the tick
 * whose re-send goes out marked -- so a caller that leaves the
 * rotation on the 0 return must re-enter it when an unmarked READY
 * arrives (the obligation at bkr94acs{Acast,Ba}Input, which is where
 * the library takes the arm).  Termination is an application choice;
 * the library prescribes no policy (see BPR.md).
 *
 * Replaces the application-layer retry bookkeeping entirely.  Per-instance
 * destination masks, per-process evidence tracking, and retry
 * scheduling over an external instance list are not needed; the retry
 * state is entirely intrinsic -- Bracha's own per-instance state (the
 * sent flags, and the announcement bitmaps behind the READY retire)
 * plus this layer's per-process BA decision byte.
 *
 * THE VERDICT GATE, this layer's one addition to Bracha's retires.
 * Before walking an A-Cast Fig1 the sweep consults bkr94acsBaDecision
 * for that process:
 *   undecided (0xFF) -> retry.  Other processes may still learn
 *     Q(j) = 1, and their Fig1 needs these echoes and readys.
 *   decided 1         -> retry.  Post-decide continuation: a process
 *     that has not yet observed the accept still needs the traffic, or
 *     its own BA can reach 0 through step 2 and SubSet agreement breaks.
 *   exhausted (0xFE)  -> retry.  No decision was made; earlier-round
 *     traffic may still help others.
 *   decided 0         -> SKIP.  The process is excluded from SubSet and
 *     step 2 has already conveyed this process's entry.
 * The BA Fig1 walk is ungated; Bpr returns 0 on unsent instances.
 *
 * The decided-0 skip is itself a retire, and it OUTRANKS the annotation
 * exchange: a gated instance stops being served whether or not its
 * announcement evidence ever completed.  So its evidence can stay
 * permanently short while its value is accepted everywhere.  That is
 * intended.  It also bounds the quiescence claim below -- an instrument
 * that reads the const accessors to CHECK that every sent instance
 * retired READY on full coverage must exempt the gated ones, or it
 * reports a defect against a correct machine.
 *
 * Out actions:
 *   BKR94ACS_ACT_ACAST_SEND for A-Cast Fig1 retries
 *     (.process = which A-Cast, .type = INITIAL/ECHO/READY,
 *      .value = vLen+1 bytes).
 *   BKR94ACS_ACT_BA_SEND for BA Fig1 retries
 *     (.process = which BA, .round, .initiator, .type =
 *      INITIAL/ECHO/READY, .baValue read from Fig1Value).
 *
 * Caller provides out[] with room for BKR94ACS_RETRY_MAX_ACTS
 * entries.
 */
unsigned int
bkr94acsRetryStep(
  struct bkr94acs *
 ,struct bracha87Retry *    /* cursor; init with bracha87RetryInit */
 ,struct bkr94acsAct *     /* out: room for BKR94ACS_RETRY_MAX_ACTS */
);

/*
 * The sweep-side decisions and their shared trichotomy.
 *
 * In the papers' asynchronous model (unbounded finite delay, no
 * clocks) a rule's "upon" / "wait until" names the evidence that
 * ENABLES an action, never the moment it is taken -- the model has
 * no moments.  Two of this layer's decisions consume evidence that
 * is still growing when it first suffices, so their firing is paced
 * by the caller from the BPR sweep tick, the deployment's only
 * time-like notion:
 *
 *   the enter-0 fanout  (BKR94 step 2)      bkr94acsFanoutDuty/Fanout
 *   the BA round turn   (Bracha Fig4 round)  bkr94acsTurnDuty/Turn
 *
 * Both duty queries classify with the same trichotomy:
 *
 *   BKR94ACS_DUTY_HELD       not enabled: firing now would be
 *                            unsound or is impossible; elapsed
 *                            time is irrelevant -- wait.
 *   BKR94ACS_DUTY_TOLERANCE  enabled, and waiting could still
 *                            improve the outcome; the caller
 *                            counts sweeps against its patience
 *                            and fires when it elapses.
 *   BKR94ACS_DUTY_MET        enabled with nothing left to wait
 *                            for; firing is free.
 *
 * Caller discipline: per decision, count completed sweeps while
 * duty is TOLERANCE; fire when the count exceeds the deployment's
 * patience.  Zero patience recovers the eager schedule an earlier
 * revision hardwired into the arrival paths -- provided the caller
 * evaluates the verdict on EVERY attempt, since a clock that only
 * advances at a sweep boundary fires one boundary late even at zero.
 *
 * THE UNIT IS THE FULL SWEEP -- one complete pass of the Retry
 * cursor over every sent Fig 1 instance, read off the cursor's own
 * wrap count (below), the same unit an abandonment policy counts
 * barren sweeps in.  That is what makes patience worth anything: a pass re-sends
 * each sent instance exactly once, so a delayed A-Cast's INITIAL
 * goes back on the wire once per sweep.  While TOLERANCE holds, the
 * same sweeps are re-carrying that traffic, so the wait is spent on
 * the recovery that can improve what the firing consumes -- but at
 * the CURSOR'S RATE, one instance per call: a budget denominated in
 * calls rather than passes re-sends only its own count out of
 * bkr94acsFig1SentCount and can buy nothing at all.
 *
 * CLOSING A SWEEP: read the cursor's `sweeps` counter, which the
 * library advances on every wrap and is the pass boundary EXACTLY.
 * Save it, and close a pass whenever it differs from the saved value:
 *
 *   if (retry.sweeps != lastSweeps) { lastSweeps = retry.sweeps;
 *                                     sweepDone = 1; }
 *
 * COMPARE, never assume +1 -- one call can complete two passes (one
 * to finish a partial pass that had actions, one more to establish
 * the next is empty and return 0).
 *
 * Do NOT count calls against bkr94acsFig1SentCount.  That count is an
 * UPPER bound on the calls a pass costs, never the count itself: the
 * cursor walks past an instance whose retries have all retired without
 * spending a call on it, while the count still includes it (sent flags
 * are never cleared).  Since a live pass always has some instance with
 * output, the 0 return does not fire, and a call-counting caller
 * closes late by the retired count -- which GROWS as a run matures,
 * because INITIAL and ECHO retire at ACCEPTED.  The patience unit
 * would silently stretch over the run.  The counter has none of that,
 * and costs no walk.
 *
 * The one term that remains the caller's: a process parked on an
 * earlier 0 return makes no call at all, so no library counter can
 * advance for it.  Such a caller counts one idle pass per tick, since
 * the pass it would have made owes nothing.
 *
 * PARKING IS AN OPTIMIZATION, NOT A REQUIREMENT, and skipping it is
 * what removes that term: a caller that keeps calling every tick
 * closes its passes on the counter alone, the whole rule in one
 * comparison.  What it pays is a full walk of the Fig 1 instance space
 * per tick, since a quiescent process finds nothing and returns 0 only
 * at the end of the pass -- bounded by that space, and far below a
 * tick at any size this state can be allocated at, because a tick is a
 * wire rate limit and the walk is a few nanoseconds per position.
 * Park to save that, or to report quiescence as the bundled example
 * does; do not park believing the boundary requires it.
 *
 * Count the closings, not the calls.  Both the duty patience above
 * and an abandonment policy's barren counter advance on this
 * boundary, which is the premise the fanout's ordering rests on.
 *
 * Two sizings follow, neither independent of the patience:
 *   the cursor length -- bkr94acsFig1SentCount grows as the BAs
 *     advance, so a budget priced in calls shrinks in real terms
 *     over a run while one priced in passes does not;
 *   the caller's own abandonment gate -- waiting is not progress,
 *     so a patience clock and a barren clock advance on the same
 *     boundaries, and patience that does not expire strictly
 *     before the abandon gate fires the decision into a caller that
 *     is already leaving.  Size that gate above the patience and
 *     the FANOUT's ordering is structural: its window opens on a
 *     BA output of 1, an act the caller counts as progress, so the
 *     barren count stands at zero on the boundary that first
 *     charges the patience.  The ROUND TURN's window opens with no
 *     acts at all, so there the ordering stays a caller obligation.
 *     BPR.md (The Abandon Boundary) carries the sizing: one knob,
 *     the patience, with the gate derived from it.
 */
#define BKR94ACS_DUTY_HELD      0
#define BKR94ACS_DUTY_TOLERANCE 1
#define BKR94ACS_DUTY_MET       2

/*
 * BKR94 Step 2 -- the enter-0 fanout.
 *
 * The paper's rule: "Upon completing 2t+1 BA protocols with output
 * 1, enter input 0 to every BA protocol for which you have not yet
 * entered a value."  Fired at the instant the local count crosses,
 * the fastest n-t processes close SubSet against every honest
 * process whose A-Cast is still in flight, and under a persistent
 * latency spread that excludes the same honest processes every ACS
 * instance.
 *
 * bkr94acsFanoutDuty (derived by scanning baDecision[] and the
 * entered set; nothing stored):
 *   HELD       BA-output-1 count < n-t: entering 0 is unsound
 *              (Lemma 2 Part A).
 *   TOLERANCE  count holds and unentered BAs remain: each delayed
 *              A-Cast that completes inside the patience window
 *              enters 1 by Step 1 and leaves the unentered set.
 *   MET        nothing unentered: moot.
 *
 * bkr94acsFanout(a, patienceElapsed, out) enters 0 into every BA
 * still unentered at the call, outputting one BKR94ACS_ACT_BA_SEND
 * per entry.  It fires iff duty is TOLERANCE and patienceElapsed is
 * nonzero, and returns 0 having changed nothing otherwise -- so an
 * unconditional call per sweep is safe, and passing a literal 1 is
 * the simplest zero-patience caller.  Firing empties the unentered
 * set, so once-per-instance is structural.
 *
 * Same shape as bkr94acsTurn, deliberately: both seams take the
 * caller's verdict and gate on it internally.  They differ only in
 * what MET means -- Turn's MET is a full sample and fires free, this
 * one's is an empty unentered set and is moot, so MET here needs no
 * elapsed signal because there is nothing left to enter.
 */
unsigned char
bkr94acsFanoutDuty(
  const struct bkr94acs *
);

unsigned int
bkr94acsFanout(
  struct bkr94acs *
 ,unsigned char            /* patienceElapsed: caller's patience verdict */
 ,struct bkr94acsAct *     /* out: room for n + 1 entries (BKR94ACS_MAX_ACTS covers it) */
);

/*
 * The BA round turn -- Bracha Fig4, one round of one process's BA.
 *
 * Bracha's Fig4 steps read "Wait until validate n-t k-messages"
 * and then compute over the validated set -- the majority at
 * (3i+1), the decide/adopt thresholds at (3i+3).  The set keeps
 * growing past n-t (cascades, late arrivals) and the proofs hold
 * for ANY >= n-t sample -- so the sample a turn consumes is purely
 * a function of WHEN the turn fires.  The old arrival-path turn
 * took the first n-t; a paced turn harvests more validated
 * messages.  At (3i+3) the gain is one-directional: the decide/
 * adopt counts are monotone thresholds (per-sender dedup: counts
 * only grow), so a fuller sample can only convert coin phases into
 * deterministic decides, never the reverse.  At (3i+1) the
 * majority is a comparison, not a threshold -- a fuller sample can
 * flip it -- but every >= n-t sample is proof-covered either way
 * (when all correct processes enter a phase agreed, correct-value
 * copies outnumber Byzantine ones in every such sample), so the
 * flip trades between sound broadcasts, never against safety.
 *
 * bkr94acsTurnDuty(a, process):
 *   HELD       the BA's next round is not complete (below n-t
 *              validated), or its round space is exhausted.
 *   TOLERANCE  complete at >= n-t but < n validated: turnable,
 *              and waiting could still enlarge the sample.
 *   MET        complete with all n validated: the full sample is
 *              in hand, waiting buys nothing.
 *
 * bkr94acsTurn(a, process, patienceElapsed, out) performs at most
 * ONE round turn: fires iff duty is MET, or duty is TOLERANCE and
 * patienceElapsed is nonzero (MET needs no elapsed signal --
 * firing is free).  Returns acts written, 0 when it did not fire.
 * Acts: BKR94ACS_ACT_BA_SEND (the next round's INITIAL, self as
 * initiator), BKR94ACS_ACT_BA_DECIDED or BKR94ACS_ACT_BA_EXHAUSTED,
 * and BKR94ACS_ACT_COMPLETE -- at most 3; these acts emerge ONLY
 * here, never from bkr94acsBaInput.  Post-decide continuation:
 * turns continue past DECIDE until the round space is exhausted.
 * A zero-patience caller drains: while (bkr94acsTurn(a, p, 1, out))
 * per process after banking new evidence.  Cascaded validation can
 * make several successive rounds turnable at once; each turn is
 * its own call, and nothing here re-arms the caller's clock when
 * one fires, so a paced caller spends ONE patience crossing a whole
 * cascade rather than one per round.  Re-arming per round is the
 * caller's to add and is not advised: it prices a catch-up the
 * cohort has already earned.
 * Scope patience to UNDECIDED BAs (bkr94acsBaDecision == 0xFF):
 * post-decide continuation rounds carry the pinned value and their
 * sample no longer chooses anything, while their turn feeds the
 * NEXT process's round -- holding them to the patience convoys the
 * cohort, since one process's stall holds everyone else at n-t.
 * The bundled example's sweep loop is the reference discipline.
 */
unsigned char
bkr94acsTurnDuty(
  const struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
);

unsigned int
bkr94acsTurn(
  struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
 ,unsigned char            /* patienceElapsed: caller's patience verdict */
 ,struct bkr94acsAct *     /* out: room for 3 entries (BKR94ACS_MAX_ACTS covers it) */
);

/*************************************************************************/
/*  Diagnostic accessors                                                 */
/*                                                                       */
/*  Read-only views into ACS state for monitoring, debugging, and        */
/*  cadence tuning.  None affect protocol semantics.                     */
/*************************************************************************/

/*
 * Decision state for a single BA (process's binary BA):
 *   0xFF -> undecided
 *   0xFE -> exhausted (Fig4 reached maxPhases with no decision;
 *           see BKR94ACS_ACT_BA_EXHAUSTED)
 *   0    -> excluded from common subset
 *   1    -> included in common subset
 *
 * Returns 0xFF on null state or out-of-range process.
 */
unsigned char
bkr94acsBaDecision(
  const struct bkr94acs *
 ,unsigned char            /* process */
);

/*
 * Returns 1 iff this process has entered a value into the BA for
 * 'process' -- 1 from BKR94 step 1 ("For each Pj for whom you (Pi)
 * know Q(j) = 1, participate in BA_j with input 1") or 0 from step
 * 2's fanout -- else 0, and 0 on null state or out-of-range process.
 *
 * The complement is step 2's own set: "enter input 0 to every BA
 * protocol for which you have not yet entered a value."
 * bkr94acsFanoutDuty reads it (MET is nothing unentered) and
 * bkr94acsFanout empties it, one BKR94ACS_ACT_BA_SEND per entry.
 *
 * Latched, per the paper's single-input rule: "Once a BA has received
 * an input from Pi (1 from step 1 or 0 from step 2), step 1 and step
 * 2 stop touching it -- BA semantics demand a single input per
 * player."  Set once, never cleared; no BA is entered twice.
 *
 * BA_self is included: step 1 makes no exception for j == self, so
 * this process enters BA_self on the same evidence (Q(self) = 1) as
 * any other BA.
 *
 * WHICH value was entered is not answered.  The entered value is the
 * BA's input and a BA past its first round no longer holds it
 * (struct bracha87Fig4.value is the current estimate); the decided
 * value is bkr94acsBaDecision.
 *
 * This is what THIS process did.  BKR94's entered facts are local --
 * Lemma 2 Part D reads a single honest player's entry -- so nothing
 * here licenses an inference about another process's entries, its
 * Q values, or its correctness.
 */
unsigned int
bkr94acsBaEntered(
  const struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
);

/*
 * The validated set for one BA's next round -- the messages Fig 3 has
 * placed in VALID^k for the round bkr94acsTurnDuty classifies and
 * bkr94acsTurn would compute over.  "Wait till a set S of n - t
 * k-messages have been validated" (Bracha87 Fig 3): the duty query
 * answers the size of that set, this answers the set.
 *
 * Returns the count and fills senders[] and values[] (caller provides
 * n + 1 entries each), forwarding bracha87Fig3GetValid for the BA's Fig 3
 * at its next round.  A VALID set's elements are the paper's
 * (q, k, v), so senders and values are answered together; k is fixed
 * by the round.  values are the BA's binary values, carrying
 * BRACHA87_D_FLAG in a 3i+2 round (Bracha's "(d, v)").
 *
 * The count is the one bkr94acsTurnDuty classifies from: >= n-t is
 * its TOLERANCE-or-MET boundary, == n its MET.
 *
 * The round is implicit and is the BA's next -- this surface names a
 * BA round nowhere else (bkr94acsTurnDuty and bkr94acsTurn take none;
 * a round reaches the caller on struct bkr94acsAct.round alone).  A
 * VALID set only grows within its round, but a turn advances the
 * round, so successive calls across a turn answer different sets and
 * the count is not monotone across one.  A BA whose round space is
 * exhausted (bkr94acsBaDecision 0xFE) has no next round: 0.
 *
 * A VALID set is one process's own -- the paper's VALID^k_p.  A
 * message validated here is validated at every correct process
 * EVENTUALLY (Bracha87 Lemma 6, "VALID sets are eventually equal"),
 * never at the instant of this call, and nothing about another
 * process's set is readable from this one.
 *
 * Returns 0 on null state, out-of-range process, or a null senders[]
 * or values[].
 */
unsigned int
bkr94acsBaGetValid(
  const struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
 ,unsigned char *          /* senders out, n + 1 entries */
 ,unsigned char *          /* values out, n + 1 entries */
);

/*
 * Returns 1 iff A-Cast Fig1[process] has recorded an echo from all n
 * processes (distinct echo senders == n), else 0 (and 0 on null state or
 * out-of-range process).
 *
 * Application use: a process that pairs a side-channel payload (e.g. a
 * PSK or a signature) with its own A-Cast, and whose receivers gate
 * their ECHO of that A-Cast on validating the payload, must keep
 * retrying the payload until this returns 1 for process == self.
 * All-echoed implies every process validated the payload (the receiver
 * holds echo until it does), which is strictly stronger than the
 * A-Cast's own ACCEPTED -- ACCEPTED can be reached at 2t+1 readys
 * (up to t byzantine, t un-validated above the n=3t+1 boundary) while
 * correct processes still lack the payload.  Pinning the side channel to
 * ACCEPTED would strand them; pinning it here does not.  Under <= t
 * silent processes this never returns 1, so the payload retries until the
 * application abandons -- the conservative, correct default.
 */
unsigned int
bkr94acsAcastAllEchoed(
  const struct bkr94acs *
 ,unsigned char            /* process */
);

/*
 * Per-process suppress mask for a side channel paired with process's
 * A-Cast INITIAL -- the per-process refinement of bkr94acsAcastAllEchoed.
 * Returns the A-Cast Fig1's INITIAL skip mask (its echoed-process bitmap;
 * process p skipped iff bit p set, test with BRACHA87_SKIP_TST), or 0 for a
 * null/out-of-range argument.
 *
 * Where bkr94acsAcastAllEchoed is the all-or-nothing stop (retire the
 * side channel once EVERY process has echoed), this drops each process from the
 * side channel's recipient set the moment IT echoes.  An application that
 * gates its ECHO on validating the paired payload (signature / PSK) thereby
 * stops re-sending the payload to a process as soon as that process proves -- by
 * echoing -- it already validated and holds it.  Same borrowed-pointer
 * lifetime as bracha87Fig1Skip (valid until the next mutating library call).
 */
const unsigned char *
bkr94acsAcastSkip(
  const struct bkr94acs *
 ,unsigned char            /* process */
);

/*
 * Borrowed read-only access to an owned Fig1 instance: the A-Cast Fig1
 * for 'process', or the BA Fig1 for (process, round, initiator) -- the
 * same keys bkr94acsBaInput routes a message on.  Returns 0 for a
 * null or out-of-range argument.  Same borrowed lifetime as the mask
 * accessors: valid until the next mutating library call.
 *
 * READ-ONLY, structurally: the pointer is const, and inputs go through
 * the bkr94acs entries, which route wire facts to the right instance
 * and record the composition-level side effects (self-accept, step-1
 * enter) no bare Fig1 call knows to make.
 *
 * What this is for: the quiescence ending claim is PER-INSTANCE
 * evidence -- a sent Fig1 retires its READY only when its
 * accepted-process bitmap (bracha87Fig1Received) covers all n -- while
 * the retry's 0 return is the weaker derived fact: a machine that
 * retired READY on the forbidden LOCAL accept (Notes 10/16) also
 * returns 0, sooner.  A caller or instrument that CHECKS the
 * claim rather than infers it reads the instance: bracha87Fig1Value
 * non-null is the sent test (both ready paths require ECHOED, so
 * RDSENT implies it, and an INITIATOR carries its value), and a sent
 * instance whose RECEIVED mask covers all n retired its READY on the
 * remote all-accepted gate -- the distinction the 0 return cannot
 * show.  Every other bracha87Fig1 reader (Skip, AllEchoed) composes
 * the same way.
 *
 * SCOPE THE CHECK TO THE INSTANCES THE RETRY STILL SERVES.  An A-Cast
 * whose BA decided 0 is skipped by the verdict gate (see
 * bkr94acsRetryStep), and the gate outranks the annotation exchange, so its
 * evidence may never complete.  A checker that asserts full coverage
 * over every sent instance reds against the correct machine; filter on
 * bkr94acsBaDecision != 0 first.
 *
 * The paired side channel is NOT read this way -- it has its own two
 * entries above (bkr94acsAcastAllEchoed, bkr94acsAcastSkip), which say
 * what they are for in their names and keep a Fig 1 out of the caller.
 */
const struct bracha87Fig1 *
bkr94acsAcastFig1(
  const struct bkr94acs *
 ,unsigned char            /* process */
);

const struct bracha87Fig1 *
bkr94acsBaFig1(
  const struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
 ,unsigned char            /* round: BA round */
 ,unsigned char            /* initiator: who initiated this Fig1 broadcast */
);

/*
 * Number of Fig1 instances currently sent (any of F1_INITIATOR,
 * F1_ECHOED, F1_RDSENT set).  Walks the N A-Cast Fig1s plus the
 * full BA Fig1 space -- sent state is NOT bounded by
 * this process's own BA progress: a faster process's INITIAL for a round
 * this process's Fig4 has not yet entered fires Rule 1 here, leaving
 * that ahead-round Fig1 ECHOED (and retry-retried) while the local
 * baNextRound lags.
 *
 * DIAGNOSTIC, and an UPPER BOUND -- never the length of a sweep.  The
 * sent flags are never cleared, so this keeps counting an instance
 * whose retries have all retired, while bkr94acsRetryStep walks past that
 * instance inside a call without spending one on it.  The gap is the
 * retired count and it GROWS as a run matures.  Do not close a pass by
 * counting calls against this: read the cursor's `sweeps` wrap count,
 * which is the boundary exactly (see the sweep-side banner above).
 *
 * What it is still good for, both resting on the bound rather than on
 * an equality:
 *   - Disambiguating a 0 return.  Retry returns 0 both before anything
 *     has been sent and once every sent instance has retired; a
 *     nonzero count here separates quiescence from the pre-broadcast
 *     idle.
 *   - A worst-case time bound.  A pass costs AT MOST this many calls,
 *     so an abandonment gate of S sweeps fires within
 *     S * count * tick.  The over-estimate is what makes it sound for
 *     a worst case, and useless for a schedule.
 *   - Cadence sizing, read the same way: a sent instance is revisited
 *     at most every count calls, so tick * count bounds its retry
 *     interval from above.
 *
 * Returns 0 on null state.
 */
unsigned int
bkr94acsFig1SentCount(
  const struct bkr94acs *
);

#endif /* BKR94ACS_H */
