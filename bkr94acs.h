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
 * BAes decide inclusion.
 *
 * BKR94 parameterizes the protocol by a predicate Q(j).  Under the
 * two paper assumptions — (1) Q eventually equals 1 for every honest
 * process, (2) every honest process eventually learns Q(j) for every j
 * — Protocol Agreement[Q] produces a common subset of size >= n-t of
 * processes for whom Q(j) = 1.
 *
 *   This deployment: Q(j) = "Fig1 reliable broadcast for process j
 *   has ACCEPTED" (the BKR94 MPC-construction equivalent is "P_j has
 *   properly shared his input").  Reliable broadcast gives both Q
 *   assumptions for free: Bracha87 Fig1 eventually accepts every
 *   honest broadcast at every honest receiver (Lemma 4).
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
 * Pure state machine: no I/O, no threads, no dynamic allocation.
 * Caller provides memory and delivers messages.
 *
 * Two message classes on the network:
 *   BKR94ACS_CLS_ACAST  — Fig1 messages carrying A-Cast values
 *   BKR94ACS_CLS_BA — Fig1 messages for per-process binary BA
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
/*    bit:  7      | 6 5 | 4        | 3  | 2   | 1 0                       */
/*          D_FLAG   (app)  ACCEPTED   cv   cls   type                    */
/*                                                                        */
/*  Fixed by library constants:                                           */
/*    type   bits 0-1  (BRACHA87_TYPE_MASK = 0x03): INITIAL/ECHO/READY.   */
/*    cls    bit  2    (BKR94ACS_CLS_MASK  = 0x04): ACAST=0x00,        */
/*                      BA=0x04.                                   */
/*    D_FLAG bit  7    (BRACHA87_D_FLAG = 0x80): on a BA message,  */
/*                      the Fig4 decision-candidate flag.                 */
/*    ACCEPTED bit 4  (BKR94ACS_ACCEPTED = 0x10): on a READY message,    */
/*                      the sender has accepted this Fig1 instance, so    */
/*                      the receiver retires its per-process READY retry    */
/*                      to the sender (BPR; struct bkr94acsAct.accepted   */
/*                      on egress, bkr94acs*Accepted on ingress).  Unlike */
/*                      D_FLAG it is class-independent -- valid on a       */
/*                      ACAST or BA READY (every Fig1 accepts).  */
/*  Convention (not forced by a constant, but shared by all examples):    */
/*    cv     bit  3:   a BA message's binary value.  Placed        */
/*                      adjacent to cls.                                  */
/*    bits 5-6:        free for application message classes.              */
/*                                                                        */
/*  Compose / recover a library message:                                  */
/*    byte = cls | type [ | (cv << 3) | (value & BRACHA87_D_FLAG) ]       */
/*    type = byte & BRACHA87_TYPE_MASK                                    */
/*    cls  = byte & BKR94ACS_CLS_MASK                                     */
/*    BA value = ((byte >> 3) & 1) | (byte & BRACHA87_D_FLAG)      */
/*                                                                        */
/*  A BA message's entire payload is just those two live bits      */
/*  (value + D_FLAG), so folding them into this byte lets a BA     */
/*  message carry NO value bytes on the wire -- the dominant message      */
/*  class in ACS, so the saving compounds.  A ACAST message carries    */
/*  its vLen+1-byte value as the payload.                                 */
/**************************************************************************/

#define BKR94ACS_CLS_ACAST  0x00 /* Fig1 reliable broadcast of A-Casts */
#define BKR94ACS_CLS_BA 0x04 /* Fig1 messages for binary BA */
#define BKR94ACS_CLS_MASK      0x04 /* recover class from a packed byte */
#define BKR94ACS_ACCEPTED      0x10 /* on a READY: sender accepted this Fig1
                                     * instance (BPR per-process READY retire) */

/*************************************************************************/
/*  Output actions                                                       */
/*                                                                       */
/*  Returned in struct bkr94acsAct array from bkr94acsAcastInput,     */
/*  bkr94acsBaInput, bkr94acsRetry, and bkr94acsAcast calls.     */
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
 * BA_DECIDED and COMPLETE are success signals — a decision was
 * reached — and are NOT stop conditions: post-decide continuation
 * requires the process to keep broadcasting past both.  BA_EXHAUSTED
 * is the library's one stop condition, and it is a failure (the BA
 * cannot decide).  Successful stopping is unspecified by the library
 * under unbounded latency; it is the application's policy.
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
 *                 (Part C).  Application must abort and (optionally)
 *                 restart with fresh state.  Output exactly once per
 *                 BA per ACS instance.)
 *
 * .value is a borrowed pointer into library-owned storage (the
 * Fig1's echoed-value slot — populated as soon as INITIATOR, Rule
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
  unsigned char act;          /* BKR94ACS_ACT_* */
  unsigned char process;       /* which process this relates to */
  unsigned char round;        /* BA round (BA_SEND only) */
  unsigned char type;         /* BRACHA87_INITIAL/ECHO/READY (ACAST_SEND, BA_SEND) */
  unsigned char baValue;     /* binary value (BA_SEND, BA_DECIDED) */
  unsigned char initiator;  /* who initiated this Fig1 broadcast (BA_SEND) */
  unsigned char accepted;     /* ACAST_SEND/BA_SEND READY: 1 = set the
                               * BKR94ACS_ACCEPTED wire bit (this instance has
                               * accepted); 0 otherwise.  The receiving process
                               * feeds it back via bkr94acs*Accepted. */
};

/*************************************************************************/
/*  BKR94 ACS state                                                      */
/*************************************************************************/

/* State flags (bitmap; same idiom as BRACHA87_F1_* / BRACHA87_F4_*) */
#define BKR94ACS_F_THRESHOLD 0x01 /* BKR94 Step 2 has fired (enter-0 fanout done) */
#define BKR94ACS_F_COMPLETE  0x02 /* all N BAs decided (Step 3 complete) */

struct bkr94acs {
  unsigned char n;          /* process count encoding: actual = n + 1 */
  unsigned char t;          /* max Byzantine (n + 1 > 3t) */
  unsigned char vLen;       /* A-Cast value length encoding: actual = vLen + 1 */
  unsigned char maxPhases;  /* per binary BA instance */
  unsigned char self;       /* this process's index (needed for BA routing) */
  unsigned char flags;      /* BKR94ACS_F_THRESHOLD / BKR94ACS_F_COMPLETE */
  /*
   * The BKR94 step-2 / step-3 decision counts are not stored: a
   * stored counter is a denormalization of baDecision[] (and, as the
   * unsigned char it once was, wrapped on the 256th decision so
   * BKR94ACS_ACT_COMPLETE could never fire at 256 processes).  They are
   * derived by scanning baDecision[] at each BA decision — a rare
   * event (see bkr94acsBaInput).
   *
   * pad puts data[] at offset 8 — a multiple of sizeof (void *) on
   * all common 32- and 64-bit ABIs — so data[] starts at the
   * alignment required by the function-pointer fields in the
   * Fig1/Fig4 instances carved out of it.
   */
  unsigned char pad[2];
  unsigned char data[1];    /* variable: see bkr94acsSz */
};

/* data[] is the variable tail; see bkr94acs.c for layout. */

/* Size in bytes needed for a BKR94 ACS instance */
unsigned long
bkr94acsSz(
  unsigned int             /* n: actual process count = n + 1 */
 ,unsigned int             /* vLen: actual A-Cast length = vLen + 1 */
 ,unsigned int             /* maxPhases: per binary BA instance */
);

/* Initialize a BKR94 ACS instance. Caller has allocated bkr94acsSz bytes. */
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
 * Maximum output actions from a single input call.
 *
 * A-Cast input (BKR94ACS_CLS_ACAST):
 *   up to 2 ACAST_SEND (echo/ready) + 1 BA_SEND (enter-1 from BKR94 Step 1
 *   on accept).  Step 2's enter-0 fanout lives in BA input, not here.
 *   Bound: 3.
 *
 * BA input (BKR94ACS_CLS_BA):
 *   up to 2 (echo/ready) from the Fig1 input, plus a cascade over
 *   newly-validated rounds.  Adversarial delivery can make Fig3's
 *   forward cascade validate many rounds in a single Fig3Accept call
 *   (round k+1 unlocks when round k crosses n-t, etc.), so the
 *   per-call ceiling is:
 *     2 (echo/ready) + M (BA_SEND per round advanced)
 *       + 1 (BA_DECIDED, fires at most once per BA)
 *       + N (BKR94 Step 2 enter-0 fanout, fires at most once per ACS)
 *       + 1 (COMPLETE, fires at most once per ACS instance)
 *   where M = maxPhases * BRACHA87_ROUNDS_PER_PHASE is the BA's
 *   round bound and N = n + 1.
 *   Bound: M + N + 4.
 *
 * BA case strictly dominates, so the unified bound is
 * M + N + 4.  BKR94ACS_MAX_ACTS takes maxPhases so the cascade bound
 * is exact for the configured BA, not the 85-phase ceiling.
 */
#define BKR94ACS_MAX_ACTS(n, maxPhases) \
  ((unsigned int)(maxPhases) * BRACHA87_ROUNDS_PER_PHASE \
   + (unsigned int)(n) + 5)

/*
 * Maximum output actions from a single bkr94acsRetry call.
 *
 * The cursor visits one Fig1 instance per retry call.  Per-Fig1
 * Bpr outputs at most 3 actions (INITIAL_ALL + ECHO_ALL +
 * READY_ALL).  Retry tags each as a struct bkr94acsAct
 * (ACAST_SEND or BA_SEND, with process / round / initiator /
 * type filled by the cursor position), so the per-call bound
 * is 3.
 */
#define BKR94ACS_RETRY_MAX_ACTS  3

/*
 * Process a A-Cast broadcast message (BKR94ACS_CLS_ACAST).
 *
 * These are Fig1 messages carrying A-Cast values.
 * Returns number of actions written to out[].
 * Caller provides out[] with room for BKR94ACS_MAX_ACTS(n, maxPhases) entries.
 *
 * On BKR94ACS_ACT_ACAST_SEND:
 *   Caller broadcasts a A-Cast Fig1 message of .type
 *   (BRACHA87_INITIAL/ECHO/READY) for .process.  Bytes to send:
 *   .value (vLen+1 bytes, borrowed pointer into the library's
 *   echoed-value slot — see struct bkr94acsAct.value).
 *
 * On BKR94ACS_ACT_BA_SEND:
 *   Caller broadcasts a BA Fig1 message.
 *   Fields: .process, .round, .initiator, .type, .baValue.
 */
unsigned int
bkr94acsAcastInput(
  struct bkr94acs *
 ,unsigned char            /* process: whose A-Cast */
 ,unsigned char            /* type: BRACHA87_INITIAL/ECHO/READY */
 ,unsigned char            /* from: sender of this message */
 ,const unsigned char *    /* value: vLen + 1 bytes */
 ,struct bkr94acsAct *     /* out: actions, room for BKR94ACS_MAX_ACTS(n, maxPhases) */
);

/*
 * Process a BA message (BKR94ACS_CLS_BA).
 *
 * These are Fig1 messages for the binary BA on process's inclusion.
 * Returns number of actions written to out[].
 * Caller provides out[] with room for BKR94ACS_MAX_ACTS(n, maxPhases) entries.
 *
 * The BA for each process is a full Fig1+Fig3+Fig4 pipeline
 * driven internally, deciding 0 or 1.
 *
 * On BKR94ACS_ACT_BA_SEND:
 *   Caller sends a BA message to all processes.
 *
 * On BKR94ACS_ACT_BA_DECIDED:
 *   BA for .process decided .baValue (0=exclude, 1=include).
 *
 * On BKR94ACS_ACT_COMPLETE:
 *   All N BAs decided. Common subset is final.
 *   Success signal, not a stop -- do not halt the loop here
 *   (post-decide continuation).
 *   Query with bkr94acsSubset().
 */
unsigned int
bkr94acsBaInput(
  struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
 ,unsigned char            /* round: BA round (0-based) */
 ,unsigned char            /* initiator: who initiated this Fig1 broadcast */
 ,unsigned char            /* type: BRACHA87_INITIAL/ECHO/READY */
 ,unsigned char            /* from: sender of this message */
 ,unsigned char            /* value: binary BA value */
 ,struct bkr94acsAct *     /* out: actions, room for BKR94ACS_MAX_ACTS(n, maxPhases) */
);

/*
 * ACCEPTED-annotation ingress (BPR per-process READY retire).
 *
 * When a received READY carries the BKR94ACS_ACCEPTED wire bit, its
 * sender 'from' has accepted the named Fig1 instance and consumes no
 * further (ready, v) from us.  These route that fact to the right Fig1's
 * acFrom, retiring our per-process READY retry to 'from' (and, once every
 * process has accepted, the instance's READY retry entirely).
 *
 * Call AFTER the matching bkr94acs{A-Cast,BA}Input for the same
 * READY (so rdFrom is recorded first; acFrom stays a subset of rdFrom).
 * Idempotent; out-of-range indices ignored.  No output actions.
 *
 * Byzantine-safe: a forged ACCEPTED only marks its own sender, so it can
 * only retire OUR retry to that liar -- never strand a correct laggard,
 * whose acFrom bit is set solely by its own true accept.
 */
void
bkr94acsAcastAccepted(
  struct bkr94acs *
 ,unsigned char            /* process: whose A-Cast */
 ,unsigned char            /* from: sender that announced accept */
);

void
bkr94acsBaAccepted(
  struct bkr94acs *
 ,unsigned char            /* process: which process's BA */
 ,unsigned char            /* round: BA round */
 ,unsigned char            /* initiator: who initiated this Fig1 broadcast */
 ,unsigned char            /* from: sender that announced accept */
);

/*
 * Query: get the decided common subset.
 * Returns count of included processes.
 * Fills processes[] with the included process indices (caller provides n entries).
 * Only valid after (a->flags & BKR94ACS_F_COMPLETE) is non-zero.
 */
unsigned int
bkr94acsSubset(
  const struct bkr94acs *
 ,unsigned char *          /* processes out, n entries */
);

/*
 * Query: get the accepted A-Cast value for an process.
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
 * immediately.  Thereafter bkr94acsRetry keeps outputting the same
 * ACAST_SEND/INITIAL on every sweep for as long as the F1_INITIATOR
 * flag is set on the A-Cast Fig1 (Implementation Note 11);
 * once the local loopback or process echoes set F1_ECHOED,
 * ACAST_SEND/ECHO is output alongside it; once F1_RDSENT is set,
 * ACAST_SEND/READY joins too.  All three streams retry
 * independently while their flags hold — BPR's purpose is to
 * help OTHER processes progress, so stopping any of the streams at
 * local saturation strands them.
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
 * 1984; see SRC84.txt and the BPR section of README.md): the
 * "still owed" predicate combines Bracha's sent flags with
 * this layer's per-process BA-decided state, all of which live
 * at the BKR endpoint.
 *
 * Same one-call-per-tick semantic as bracha87Fig1RetryStep — see
 * the network flood warning in bracha87.h.  The cursor (struct
 * bracha87Retry) lives in caller storage, initialized with
 * bracha87RetryInit; it walks the (A-Cast, then BA by
 * process × round × initiator) Fig1 instance space, finds the
 * next sent instance, and outputs its actions as struct
 * bkr94acsAct entries.
 *
 * Returns 0 only when a full sweep finds no sent instance —
 * pre-broadcast / shutdown state, not a per-tick termination
 * signal.  Termination is an application choice; the library
 * prescribes no policy (see README.md Deployment Notes).
 *
 * Replaces the application-layer retry bookkeeping entirely.  Per-record
 * destination masks, per-process evidence tracking, and retry
 * scheduling over an external record list are not needed; the
 * Fig1 sent-state flags plus the BA-decision gate (see
 * bkr94acs.dtc BPR section) are the entire retry state.
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
bkr94acsRetry(
  struct bkr94acs *
 ,struct bracha87Retry *    /* cursor; init with bracha87RetryInit */
 ,struct bkr94acsAct *     /* out: room for BKR94ACS_RETRY_MAX_ACTS */
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
 * Returns 1 iff A-Cast Fig1[process] has recorded an echo from all n
 * processes (distinct echo senders == n), else 0 (and 0 on null state or
 * out-of-range process).
 *
 * Application use: an process that pairs a side-channel payload (e.g. a
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
 * Number of Fig1 instances currently sent (any of F1_INITIATOR,
 * F1_ECHOED, F1_RDSENT set).  Walks the N A-Cast Fig1s plus the
 * full BA Fig1 space — sent state is NOT bounded by
 * this process's own BA progress: a faster process's INITIAL for a round
 * this process's Fig4 has not yet entered fires Rule 1 here, leaving
 * that ahead-round Fig1 ECHOED (and retry-retried) while the local
 * baNextRound lags.
 *
 * Useful for sizing tick cadence: at one Fig1 advance per Retry
 * call, the per-Fig1 retry rate is roughly tick / (count + 1).
 *
 * Returns 0 on null state.
 */
unsigned int
bkr94acsSentFig1Count(
  const struct bkr94acs *
);

#endif /* BKR94ACS_H */
