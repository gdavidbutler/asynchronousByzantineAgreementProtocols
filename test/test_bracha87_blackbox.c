/*
 * test_bracha87_blackbox.c
 *
 * Black-box test for the public C API in bracha87.h.
 *
 * Tests are derived ONLY from the documented contract in bracha87.h
 * and the Bracha 1987 paper notes (Bracha87.txt).  No part of this
 * file inspects bracha87.c or any other implementation source.
 *
 * Properties exercised (Bracha87.txt lemma statements):
 *   - Validity      Lemma 4 -- if a correct process p broadcasts v, all
 *                   correct processes accept v
 *   - Agreement     Lemma 2 -- if two correct processes accept u and v,
 *                   then u = v
 *   - Totality      Lemma 3 -- if a correct process accepts v, every
 *                   correct process eventually accepts v (modulo delivery
 *                   -- driven here by completing the message exchange)
 *   - API edges     (Sz/Init contracts, Initiator idempotency, dedup,
 *                    BPR retry invariants documented in the header)
 *   - Byzantine     (t < n/3 faulty injecting equivocations / arbitrary
 *                    values cannot break validity or agreement)
 *
 * Header encoding convention (CRITICAL): n parameter is encoded;
 * actual process count = n + 1.  vLen parameter is encoded; actual
 * value length = vLen + 1.  We use:
 *   N_ENC = 3  -> actual cluster of 4 processes
 *   T     = 1  -> max Byzantine; n + 1 > 3t holds (4 > 3)
 *   VLEN_BIN = 0  -> 1-byte values (binary consensus)
 *
 * Style: C89, -pedantic -Wall -Wextra, K&R, 2-space indent.  One
 * monolithic main() per the project's inline-everything convention.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bracha87.h"

/* ------------------------------------------------------------------ */
/* test plumbing                                                      */
/* ------------------------------------------------------------------ */

static int Failures = 0;
static int Checks   = 0;
static const char *CurTest = "<none>";

#define CHECK(cond, msg) do { \
    ++Checks; \
    if (!(cond)) { \
      ++Failures; \
      fprintf(stderr, "FAIL [%s]: %s  (%s:%d)\n", \
              CurTest, (msg), __FILE__, __LINE__); \
    } \
  } while (0)

#define BANNER(name) do { CurTest = (name); } while (0)

/* Encoded parameters for a 4-process cluster (actual N = N_ENC + 1) */
#define N_ENC      3      /* actual process count = 4 */
#define N_ACT      4
#define T_VAL      1
#define VLEN_BIN   0      /* actual value length = 1 byte */

/* ------------------------------------------------------------------ */
/* Pseudo-random scheduler.  Repeatable so failures are debuggable.   */
/* ------------------------------------------------------------------ */

static unsigned long Rng = 0x9e3779b97f4a7c15UL;

static unsigned int
rngNext(void) {
  Rng = Rng * 6364136223846793005UL + 1442695040888963407UL;
  return (unsigned int)(Rng >> 33);
}

static void
rngSeed(unsigned long s) {
  Rng = s ? s : 1;
  (void) rngNext();
}

/* ------------------------------------------------------------------ */
/* Asynchronous wire-message queue used by all multi-process simulators. */
/* ------------------------------------------------------------------ */

struct wire {
  unsigned char type;       /* INITIAL/ECHO/READY */
  unsigned char from;
  unsigned char to;
  unsigned char round;
  unsigned char initiator;
  unsigned char value[8];
};

#define QCAP 65536
static struct wire WireQ[QCAP];
static unsigned int QHead = 0, QTail = 0;

static void
qReset(void) {
  QHead = QTail = 0;
}

static unsigned int
qSize(void) {
  return (QTail - QHead);
}

static void
qPush(const struct wire *w) {
  if (qSize() >= QCAP) {
    fprintf(stderr, "FATAL: wire queue overflow\n");
    abort();
  }
  WireQ[QTail % QCAP] = *w;
  ++QTail;
}

/* Pop a uniformly-random pending message (asynchronous scheduler). */
static int
qPopRandom(struct wire *out) {
  unsigned int sz, pick, idx;
  sz = qSize();
  if (sz == 0)
    return (0);
  pick = rngNext() % sz;
  idx = (QHead + pick) % QCAP;
  *out = WireQ[idx];
  --QTail;
  if (idx != (QTail % QCAP))
    WireQ[idx] = WireQ[QTail % QCAP];
  return (1);
}

/* Broadcast a Fig1 action from process 'from' to all 'nAct' processes. */
static void
broadcastFig1(unsigned char from, unsigned int nAct,
              unsigned char act, const unsigned char *v,
              unsigned int vBytes)
{
  struct wire w;
  unsigned int j;
  unsigned char type;
  if (act == BRACHA87_INITIAL_ALL) type = BRACHA87_INITIAL;
  else if (act == BRACHA87_ECHO_ALL) type = BRACHA87_ECHO;
  else if (act == BRACHA87_READY_ALL) type = BRACHA87_READY;
  else return;
  for (j = 0; j < nAct; ++j) {
    memset(&w, 0, sizeof (w));
    w.type = type;
    w.from = from;
    w.to = (unsigned char) j;
    if (v && vBytes <= sizeof (w.value))
      memcpy(w.value, v, vBytes);
    qPush(&w);
  }
}

/* ------------------------------------------------------------------ */
/* File-scope coin and N functions referenced from main().            */
/* ------------------------------------------------------------------ */

static unsigned char
testCoinAlt(void *closure, unsigned char instance, unsigned char phase)
{
  (void) closure;
  (void) instance;
  return (unsigned char) (phase & 1);
}

/* An N that answers "permissive, and 0|D_FLAG is the one legitimate
 * d-flagged value": every n-t subset could yield 0 or 0|D_FLAG.  It
 * drives the permissive branch of the VALID check and of the cascade. */
static int
testNPermissive0D(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  (void) closure;
  (void) k;
  (void) n_msgs;
  (void) senders;
  (void) values;
  *result = 0 | BRACHA87_D_FLAG;
  return (1);
}

/* An N that answers "permissive, and no d-flagged value is legitimate":
 * a d-flag arriving under it must be rejected on the cascade path too. */
static int
testNPermissiveNoD(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  (void) closure;
  (void) k;
  (void) n_msgs;
  (void) senders;
  (void) values;
  *result = 0;
  return (1);
}

/* An N the Fig3Init contract arm can hand in; never invoked there. */
static int
testN(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  (void) closure;
  (void) k;
  (void) n_msgs;
  (void) senders;
  (void) values;
  *result = 0;
  return (0);
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */

int
main(int argc, char **argv)
{
  unsigned long sz;
  unsigned int i, j, r;
  unsigned int act_count;
  unsigned char actions[3];
  unsigned char buf[16];

  static unsigned char fig1Storage[N_ACT][2048];
  struct bracha87Fig1 *processes[N_ACT];
  int accepted[N_ACT];
  unsigned char acceptedV[N_ACT][8];

  (void) argc;
  (void) argv;
  rngSeed(0xC0FFEE);

  /* ---------------------------------------------------------------- */
  BANNER("Sz/Init contract");
  /* ---------------------------------------------------------------- */
  /* Sz must return >= sizeof (struct) and grow with n / vLen / phases. */
  {
    unsigned long s_small, s_large, s_long;
    s_small = bracha87Fig1Sz(N_ENC, VLEN_BIN);
    s_large = bracha87Fig1Sz(15, VLEN_BIN);
    s_long  = bracha87Fig1Sz(N_ENC, 31);
    CHECK(s_small >= sizeof (struct bracha87Fig1), "Fig1Sz >= sizeof");
    CHECK(s_large > s_small, "Fig1Sz grows with n");
    CHECK(s_long  > s_small, "Fig1Sz grows with vLen");

    s_small = bracha87Fig2Sz(N_ENC, 4);
    s_large = bracha87Fig2Sz(15, 4);
    CHECK(s_small >= sizeof (struct bracha87Fig2), "Fig2Sz >= sizeof");
    CHECK(s_large > s_small, "Fig2Sz grows with n");

    s_small = bracha87Fig3Sz(N_ENC, 4);
    s_large = bracha87Fig3Sz(15, 4);
    CHECK(s_small >= sizeof (struct bracha87Fig3), "Fig3Sz >= sizeof");
    CHECK(s_large > s_small, "Fig3Sz grows with n");

    s_small = bracha87Fig4Sz(N_ENC, 1);
    s_large = bracha87Fig4Sz(N_ENC, BRACHA87_MAX_PHASES);
    CHECK(s_small >= sizeof (struct bracha87Fig4), "Fig4Sz >= sizeof");
    CHECK(s_large > s_small, "Fig4Sz grows with maxPhases");
  }

  /* ---------------------------------------------------------------- */
  /* Header: "Size in bytes needed for a Fig1 instance, or 0 if the    */
  /* configuration cannot be built ... The parameters are WIDER than   */
  /* bracha87Fig1Init's unsigned char on purpose, and the width is the */
  /* refusal ... 0 is how it declines."                                */
  /*                                                                   */
  /* 255 is the last representable encoded value (actual count 256);   */
  /* 256 is the first one Init could not carry, so the pair brackets   */
  /* the boundary rather than probing one side of it.                  */
  /* ---------------------------------------------------------------- */
  {
    CHECK(bracha87Fig1Sz(255, VLEN_BIN) != 0, "Fig1Sz takes n 255");
    CHECK(bracha87Fig1Sz(256, VLEN_BIN) == 0, "Fig1Sz refuses n 256");
    CHECK(bracha87Fig1Sz(N_ENC, 255) != 0, "Fig1Sz takes vLen 255");
    CHECK(bracha87Fig1Sz(N_ENC, 256) == 0, "Fig1Sz refuses vLen 256");

    CHECK(bracha87Fig2Sz(255, 4) != 0, "Fig2Sz takes n 255");
    CHECK(bracha87Fig2Sz(256, 4) == 0, "Fig2Sz refuses n 256");
    CHECK(bracha87Fig2Sz(N_ENC, 255) != 0, "Fig2Sz takes maxRounds 255");
    CHECK(bracha87Fig2Sz(N_ENC, 256) == 0, "Fig2Sz refuses maxRounds 256");

    CHECK(bracha87Fig3Sz(255, 4) != 0, "Fig3Sz takes n 255");
    CHECK(bracha87Fig3Sz(256, 4) == 0, "Fig3Sz refuses n 256");
    CHECK(bracha87Fig3Sz(N_ENC, 255) != 0, "Fig3Sz takes maxRounds 255");
    CHECK(bracha87Fig3Sz(N_ENC, 256) == 0, "Fig3Sz refuses maxRounds 256");

    CHECK(bracha87Fig4Sz(255, 1) != 0, "Fig4Sz takes n 255");
    CHECK(bracha87Fig4Sz(256, 1) == 0, "Fig4Sz refuses n 256");

    /* Both ends of maxPhases are refused, never substituted.  Header:
     * "maxPhases is refused at BOTH ends, never substituted."  The
     * pair below the ceiling is the boundary -- the last value Sz can
     * carry, then the first it cannot -- and the far-past value shows
     * the refusal is not a wrap that happens to land on 0. */
    CHECK(bracha87Fig4Sz(N_ENC, BRACHA87_MAX_PHASES) != 0,
          "Fig4Sz takes maxPhases at the ceiling");
    CHECK(bracha87Fig4Sz(N_ENC, BRACHA87_MAX_PHASES + 1) == 0,
          "Fig4Sz refuses maxPhases one past the ceiling");
    CHECK(bracha87Fig4Sz(N_ENC, 60000) == 0,
          "Fig4Sz refuses a far-past maxPhases");

    /* At 0 a Fig 4 contradicts itself, its round path answering
     * BROADCAST for rounds its Fig 3 can never validate. */
    CHECK(bracha87Fig4Sz(N_ENC, 1) != 0, "Fig4Sz takes maxPhases 1");
    CHECK(bracha87Fig4Sz(N_ENC, 0) == 0, "Fig4Sz refuses maxPhases 0");
  }

  /* ---------------------------------------------------------------- */
  /* Header: "Returns 1 initialized, 0 REFUSED -- a null instance, or */
  /* a configuration Bracha's model does not admit (n + 1 > 3t is     */
  /* required).  A refusal writes NOTHING ... no library entry aborts */
  /* a binding application over bad input."                           */
  /*                                                                  */
  /* The bound is N > 3t with N = n + 1, so N == 3t is the LAST       */
  /* refused configuration and N == 3t+1 the first admitted one.      */
  /* Each pair below is exactly that boundary (N=3,t=1 refused /      */
  /* N=4,t=1 admitted): a pair further out, say N=4 with t=2, is      */
  /* refused by both `<=` and `<` and so cannot see an off-by-one in  */
  /* the guard.  An Init that ABORTED on these would take the process */
  /* down here instead of reporting.                                  */
  /* ---------------------------------------------------------------- */
  {
    struct bracha87Fig1 *b1 = (struct bracha87Fig1 *) fig1Storage[0];
    unsigned char *b2, *b3, *b4;
    unsigned int intact;

    CHECK(bracha87Fig1Init(b1, N_ENC, T_VAL, VLEN_BIN) == 1,
          "Fig1Init returns 1 on a config the model admits");
    CHECK(bracha87Fig1Init(b1, 2, 1, VLEN_BIN) == 0,
          "Fig1Init refuses N = 3 with t = 1 (N == 3t)");
    CHECK(bracha87Fig1Init(0, N_ENC, T_VAL, VLEN_BIN) == 0,
          "Fig1Init refuses a null instance");

    if ((b2 = malloc(bracha87Fig2Sz(N_ENC, 4))) != 0) {
      CHECK(bracha87Fig2Init((struct bracha87Fig2 *) b2, N_ENC, T_VAL, 4) == 1,
            "Fig2Init returns 1 on a config the model admits");
      CHECK(bracha87Fig2Init((struct bracha87Fig2 *) b2, 2, 1, 4) == 0,
            "Fig2Init refuses N = 3 with t = 1 (N == 3t)");
      free(b2);
    }

    if ((b3 = malloc(bracha87Fig3Sz(N_ENC, 4))) != 0) {
      CHECK(bracha87Fig3Init((struct bracha87Fig3 *) b3,
                             N_ENC, T_VAL, 4, testN, 0) == 1,
            "Fig3Init returns 1 on a config the model admits");
      CHECK(bracha87Fig3Init((struct bracha87Fig3 *) b3,
                             2, 1, 4, testN, 0) == 0,
            "Fig3Init refuses N = 3 with t = 1 (N == 3t)");
      CHECK(bracha87Fig3Init((struct bracha87Fig3 *) b3,
                             N_ENC, T_VAL, 4, 0, 0) == 0,
            "Fig3Init refuses a null N");
      free(b3);
    }

    if ((b4 = malloc(bracha87Fig4Sz(N_ENC, 2))) != 0) {
      CHECK(bracha87Fig4Init((struct bracha87Fig4 *) b4,
                             N_ENC, T_VAL, 2, 0, 0, testCoinAlt, 0) == 1,
            "Fig4Init returns 1 on a config the model admits");
      CHECK(bracha87Fig4Init((struct bracha87Fig4 *) b4,
                             2, 1, 2, 0, 0, testCoinAlt, 0) == 0,
            "Fig4Init refuses N = 3 with t = 1 (N == 3t)");
      CHECK(bracha87Fig4Init((struct bracha87Fig4 *) b4,
                             N_ENC, T_VAL, 2, 0, 0, 0, 0) == 0,
            "Fig4Init refuses a null coin");
      CHECK(bracha87Fig4Init((struct bracha87Fig4 *) b4,
                             N_ENC, T_VAL, 0, 0, 0, testCoinAlt, 0) == 0,
            "Fig4Init refuses maxPhases 0, as Sz does");
      CHECK(bracha87Fig4Init((struct bracha87Fig4 *) b4, N_ENC, T_VAL,
                             BRACHA87_MAX_PHASES + 1, 0, 0,
                             testCoinAlt, 0) == 0,
            "Fig4Init refuses maxPhases past the ceiling, as Sz does");
      free(b4);
    }

    /* Header: "initialValue is 0 or 1, and anything else is refused."
     * The boundary is 1 admitted / 2 refused.  The other value tried
     * is BRACHA87_D_FLAG, whose base (& ~D_FLAG) is 0: a mask would
     * have admitted it as 0 where a refusal declines it, so it is the
     * value that tells the two apart.  A refused Init writes nothing,
     * so the probe stays 0xAA. */
    sz = bracha87Fig4Sz(N_ENC, 2);
    if ((b4 = malloc(sz)) != 0) {
      struct bracha87Fig4 *f4 = (struct bracha87Fig4 *) b4;

      CHECK(bracha87Fig4Init(f4, N_ENC, T_VAL, 2, 1, 0, testCoinAlt, 0) == 1,
            "Fig4Init takes initialValue 1");
      CHECK(f4->value == 1, "Fig4Init keeps initialValue 1");
      memset(b4, 0xAA, sz);
      CHECK(bracha87Fig4Init(f4, N_ENC, T_VAL, 2, 2, 0, testCoinAlt, 0) == 0,
            "Fig4Init refuses initialValue 2");
      CHECK(bracha87Fig4Init(f4, N_ENC, T_VAL, 2, BRACHA87_D_FLAG, 0,
                             testCoinAlt, 0) == 0,
            "Fig4Init refuses BRACHA87_D_FLAG as an initialValue");
      intact = 1;
      for (j = 0; j < sz; ++j)
        if (b4[j] != 0xAA)
          intact = 0;
      CHECK(intact, "a refused Fig4Init leaves the caller's memory alone");
      free(b4);
    }

    /* A refusal writes nothing: the caller still holds its allocation. */
    memset(fig1Storage[0], 0xAA, sizeof (fig1Storage[0]));
    bracha87Fig1Init((struct bracha87Fig1 *) fig1Storage[0], 2, 1, VLEN_BIN);
    intact = 1;
    for (j = 0; j < sizeof (fig1Storage[0]); ++j)
      if (fig1Storage[0][j] != 0xAA)
        intact = 0;
    CHECK(intact, "a refused Fig1Init leaves the caller's memory alone");
  }

  /* Init clears stored fields exposed by struct definition. */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    sz = bracha87Fig1Sz(N_ENC, VLEN_BIN);
    CHECK(sz <= sizeof (fig1Storage[0]), "fig1Storage large enough");
    memset(fig1Storage[0], 0xAA, sizeof (fig1Storage[0]));
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(b->n == N_ENC, "Fig1Init.n preserved");
    CHECK(b->t == T_VAL, "Fig1Init.t preserved");
    CHECK(b->vLen == VLEN_BIN, "Fig1Init.vLen preserved");
    CHECK((b->flags & (BRACHA87_F1_ECHOED | BRACHA87_F1_RDSENT
                     | BRACHA87_F1_ACCEPTED | BRACHA87_F1_INITIATOR)) == 0,
          "Fig1Init.flags clear");
    CHECK(bracha87Fig1Value(b) == 0, "Fig1Value null pre-initiator pre-echo");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1Initiator idempotency and value visibility");
  /* ---------------------------------------------------------------- */
  /* Header: "Idempotent: re-calling overwrites the stored value."    */
  /* Initiator sets BRACHA87_F1_INITIATOR and the value WITHOUT setting     */
  /* ECHOED.  bracha87Fig1Value returns non-null when INITIATOR ||       */
  /* ECHOED.                                                          */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v1[1] = { 1 };
    static const unsigned char v0[1] = { 0 };
    const unsigned char *got;
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    bracha87Fig1Initiator(b, v1);
    CHECK((b->flags & BRACHA87_F1_INITIATOR) != 0, "INITIATOR flag set");
    CHECK((b->flags & BRACHA87_F1_ECHOED) == 0, "ECHOED clear after Initiator");
    got = bracha87Fig1Value(b);
    CHECK(got != 0, "Value visible after Initiator");
    CHECK(got && got[0] == 1, "Value matches Initiator arg");
    bracha87Fig1Initiator(b, v0);
    got = bracha87Fig1Value(b);
    CHECK(got && got[0] == 0, "Initiator idempotent overwrite");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Rule 1: (initial,v) -> (echo,v) all + dedup");
  /* ---------------------------------------------------------------- */
  /* Header: "in(initial, v) from p && !echoed -> echo all"           */
  /* Once ECHOED is set, Rule 1 cannot re-fire.                       */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    act_count = bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, actions);
    CHECK(act_count >= 1, "Rule1 fires on initial");
    CHECK(actions[0] == BRACHA87_ECHO_ALL, "Rule1 outputs ECHO_ALL");
    CHECK((b->flags & BRACHA87_F1_ECHOED) != 0, "ECHOED set after Rule1");
    /* Re-deliver same initial: ECHOED guard fails Rule 1 */
    act_count = bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, actions);
    CHECK(act_count == 0, "Rule1 cannot re-fire once ECHOED");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 echo dedup per sender + Rule 2 fires");
  /* ---------------------------------------------------------------- */
  /* Header: "at most one echo and one ready per sender."             */
  /* We don't depend on the exact threshold formula -- we verify      */
  /*   (a) duplicate echoes from one sender don't advance the count,  */
  /*   (b) enough distinct echoes (we feed all N) eventually fires    */
  /*       Rule 2 (echo all).                                         */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    int sawRule2 = 0;
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    /* duplicate echoes from sender 0 -- must not fire Rule 2 */
    for (i = 0; i < 8; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_ECHO, 0, v, actions);
    CHECK((b->flags & BRACHA87_F1_ECHOED) == 0, "echo dedup per sender");
    /* feed echoes from all remaining processes (1..N_ACT-1) */
    for (i = 1; i < N_ACT; ++i) {
      act_count = bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i,
                                    v, actions);
      for (j = 0; j < act_count; ++j)
        if (actions[j] == BRACHA87_ECHO_ALL) sawRule2 = 1;
    }
    CHECK(sawRule2, "Rule2 fires at sufficient distinct echoes");
    CHECK((b->flags & BRACHA87_F1_ECHOED) != 0, "ECHOED set after Rule2");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 ready dedup per sender");
  /* ---------------------------------------------------------------- */
  /* Same shape as echo dedup, for the READY path. */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    for (i = 0; i < 8; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_READY, 0, v, actions);
    /* dedup only -- without crossing t+1 distinct senders Rule 3      */
    /* must not have fired ECHOED via the READY path.                  */
    CHECK((b->flags & BRACHA87_F1_ECHOED) == 0,
          "ready dedup pre-rule3");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 paths to ACCEPT");
  /* ---------------------------------------------------------------- */
  /* Eventual property: feed enough distinct READYs from all processes,   */
  /* and at some point ACCEPT (Rule 6) fires AND F1_ACCEPTED is set.  */
  /* Action order per header: echo, ready, accept.                    */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    int sawAccept = 0;
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    /* Send INITIAL first to fire Rule 1, getting ECHOED set. */
    (void) bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, actions);
    /* Now feed READYs from all distinct processes; at some point Rule 5  */
    /* fires (sets RDSENT) and eventually Rule 6 fires (accept).      */
    for (i = 0; i < N_ACT; ++i) {
      act_count = bracha87Fig1Input(b, BRACHA87_READY, (unsigned char) i,
                                    v, actions);
      /* Verify documented order: actions in order echo, ready, accept */
      {
        int posEcho = -1, posReady = -1, posAccept = -1;
        for (j = 0; j < act_count; ++j) {
          if (actions[j] == BRACHA87_ECHO_ALL) posEcho = (int) j;
          if (actions[j] == BRACHA87_READY_ALL) posReady = (int) j;
          if (actions[j] == BRACHA87_ACCEPT) posAccept = (int) j;
        }
        if (posEcho >= 0 && posReady >= 0)
          CHECK(posEcho < posReady, "out[] order: echo before ready");
        if (posReady >= 0 && posAccept >= 0)
          CHECK(posReady < posAccept, "out[] order: ready before accept");
        if (posAccept >= 0) sawAccept = 1;
      }
    }
    CHECK(sawAccept, "ACCEPT eventually fires given enough readys");
    CHECK((b->flags & BRACHA87_F1_ACCEPTED) != 0, "F1_ACCEPTED set");
    CHECK((b->flags & BRACHA87_F1_RDSENT) != 0, "F1_RDSENT set");
    /* ACCEPT does not re-fire on subsequent inputs */
    {
      int reAccept = 0;
      static const unsigned char v2[1] = { 1 };
      act_count = bracha87Fig1Input(b, BRACHA87_READY, 0, v2, actions);
      for (j = 0; j < act_count; ++j)
        if (actions[j] == BRACHA87_ACCEPT) reAccept = 1;
      CHECK(!reAccept, "ACCEPT does not re-fire (dedup retains ACCEPTED)");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Lemma 4: all-honest reliable broadcast (asynch sched)");
  /* ---------------------------------------------------------------- */
  /* Theorem 1 / Lemma 4 of Bracha 87: if a correct process broadcasts  */
  /* v, all correct processes accept v.  We use 4-byte payload to       */
  /* exercise vLen != 0.                                                */
  {
    static const unsigned char V[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    struct wire w;
    unsigned int vEnc = 3; /* actual length 4 bytes */
    unsigned int vBytes = 4;
    unsigned int safety = 0;
    qReset();
    sz = bracha87Fig1Sz(N_ENC, vEnc);
    CHECK(sz <= sizeof (fig1Storage[0]), "Lemma4: storage size");
    for (i = 0; i < N_ACT; ++i) {
      processes[i] = (struct bracha87Fig1 *) fig1Storage[i];
      bracha87Fig1Init(processes[i], N_ENC, T_VAL, (unsigned char) vEnc);
      accepted[i] = 0;
      memset(acceptedV[i], 0, sizeof (acceptedV[i]));
    }
    bracha87Fig1Initiator(processes[0], V);
    broadcastFig1(0, N_ACT, BRACHA87_INITIAL_ALL, V, vBytes);
    while (qPopRandom(&w) && safety++ < 200000) {
      struct bracha87Fig1 *b = processes[w.to];
      act_count = bracha87Fig1Input(b, w.type, w.from, w.value, actions);
      for (i = 0; i < act_count; ++i) {
        unsigned char a = actions[i];
        const unsigned char *cv = bracha87Fig1Value(b);
        if (a == BRACHA87_ECHO_ALL || a == BRACHA87_READY_ALL) {
          CHECK(cv != 0, "Value non-null on ECHO_ALL/READY_ALL");
          if (cv)
            broadcastFig1(w.to, N_ACT, a, cv, vBytes);
        } else if (a == BRACHA87_ACCEPT) {
          accepted[w.to] = 1;
          if (cv) memcpy(acceptedV[w.to], cv, vBytes);
        }
      }
    }
    CHECK(safety < 200000, "Lemma4 sim bounded");
    for (i = 0; i < N_ACT; ++i) {
      CHECK(accepted[i], "Lemma4: every honest process accepts");
      CHECK(memcmp(acceptedV[i], V, vBytes) == 0,
            "Lemma4: every honest process accepts v_orig");
      CHECK((processes[i]->flags & BRACHA87_F1_ACCEPTED) != 0,
            "F1_ACCEPTED set after accept");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Lemma 3: one process misses INITIAL");
  /* ---------------------------------------------------------------- */
  /* Initiator broadcasts INITIAL to processes 0,1,2 (skipping process 3).  */
  /* Process 3 must still ACCEPT V via the echo cascade (Rules 2/3/4/5/6) */
  /* once enough echoes/readys reach it.                              */
  {
    static const unsigned char V[1] = { 0x42 };
    struct wire w;
    unsigned int safety = 0;
    qReset();
    for (i = 0; i < N_ACT; ++i) {
      processes[i] = (struct bracha87Fig1 *) fig1Storage[i];
      bracha87Fig1Init(processes[i], N_ENC, T_VAL, VLEN_BIN);
      accepted[i] = 0;
    }
    bracha87Fig1Initiator(processes[0], V);
    /* INITIAL to processes 0,1,2 only (process 3 misses it) */
    for (i = 0; i < N_ACT - 1; ++i) {
      memset(&w, 0, sizeof (w));
      w.type = BRACHA87_INITIAL;
      w.from = 0;
      w.to = (unsigned char) i;
      w.value[0] = V[0];
      qPush(&w);
    }
    while (qPopRandom(&w) && safety++ < 200000) {
      struct bracha87Fig1 *b = processes[w.to];
      act_count = bracha87Fig1Input(b, w.type, w.from, w.value, actions);
      for (i = 0; i < act_count; ++i) {
        unsigned char a = actions[i];
        const unsigned char *cv = bracha87Fig1Value(b);
        if ((a == BRACHA87_ECHO_ALL || a == BRACHA87_READY_ALL) && cv)
          broadcastFig1(w.to, N_ACT, a, cv, 1);
        else if (a == BRACHA87_ACCEPT) {
          accepted[w.to] = 1;
          acceptedV[w.to][0] = cv ? cv[0] : 0xFF;
        }
      }
    }
    CHECK(safety < 200000, "Lemma3 sim bounded");
    for (i = 0; i < N_ACT; ++i) {
      CHECK(accepted[i], "Lemma3: every honest process accepts");
      if (accepted[i])
        CHECK(acceptedV[i][0] == V[0], "Lemma3: accepts the original v");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Lemma 2: Byzantine equivocation -> agreement holds");
  /* ---------------------------------------------------------------- */
  /* Byzantine process 0 initiates and equivocates: sends (initial,A)   */
  /* to process 1 and (initial,B) to process 2; sends mixed echoes/readys   */
  /* with arbitrary values.  Honest processes 1,2,3.  Lemma 1/2 guarantee */
  /* no two honest processes accept different values.  Liveness is not    */
  /* required (split delivery may strand honest processes; safety is      */
  /* the property we insist on here).                                  */
  {
    static const unsigned char A[1] = { 0x55 };
    static const unsigned char B[1] = { 0xAA };
    struct wire w0;
    struct wire w;
    int anyAccept = 0;
    unsigned char commonV = 0;
    unsigned int safety = 0;
    memset(&w0, 0, sizeof (w0));
    memset(&w, 0, sizeof (w));
    qReset();
    for (i = 0; i < N_ACT; ++i) {
      processes[i] = (struct bracha87Fig1 *) fig1Storage[i];
      bracha87Fig1Init(processes[i], N_ENC, T_VAL, VLEN_BIN);
      accepted[i] = 0;
    }
    /* Equivocating initials */
    w0.type = BRACHA87_INITIAL; w0.from = 0;
    w0.to = 1; w0.value[0] = A[0]; qPush(&w0);
    w0.to = 2; w0.value[0] = B[0]; qPush(&w0);
    /* Byzantine arbitrary echoes */
    w0.type = BRACHA87_ECHO; w0.from = 0;
    for (i = 1; i < N_ACT; ++i) {
      w0.to = (unsigned char) i;
      w0.value[0] = (i & 1) ? A[0] : B[0];
      qPush(&w0);
    }
    while (qPopRandom(&w) && safety++ < 200000) {
      struct bracha87Fig1 *b;
      if (w.to >= N_ACT) continue;
      b = processes[w.to];
      act_count = bracha87Fig1Input(b, w.type, w.from, w.value, actions);
      if (w.to == 0) continue; /* skip Byzantine process's outputs */
      for (i = 0; i < act_count; ++i) {
        unsigned char a = actions[i];
        const unsigned char *cv = bracha87Fig1Value(b);
        if ((a == BRACHA87_ECHO_ALL || a == BRACHA87_READY_ALL) && cv)
          broadcastFig1(w.to, N_ACT, a, cv, 1);
        else if (a == BRACHA87_ACCEPT) {
          accepted[w.to] = 1;
          acceptedV[w.to][0] = cv ? cv[0] : 0xFF;
        }
      }
    }
    CHECK(safety < 200000, "Byz Lemma2 sim bounded");
    for (i = 1; i < N_ACT; ++i) {
      if (accepted[i]) {
        if (!anyAccept) { anyAccept = 1; commonV = acceptedV[i][0]; }
        else CHECK(acceptedV[i][0] == commonV,
                   "Lemma2: no two honest accept different values");
      }
    }
    /* Even if no honest process reaches accept, that's still a contract- */
    /* consistent outcome because liveness needs eventual delivery,    */
    /* which equivocation can break for the missing-value direction.   */
    (void) anyAccept; (void) commonV;
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 BPR retry invariants");
  /* ---------------------------------------------------------------- */
  /* Header invariants:                                               */
  /*   INITIATOR       -> Bpr outputs INITIAL_ALL                          */
  /*   ECHOED       -> Bpr outputs ECHO_ALL                             */
  /*   RDSENT       -> Bpr outputs READY_ALL (incl. post-ACCEPT)        */
  /*   nothing sent  -> Bpr returns 0                                  */
  /*   action order: initial, echo, ready                             */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 7 };
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(bracha87Fig1Bpr(b, actions) == 0, "Bpr 0 on fresh");

    bracha87Fig1Initiator(b, v);
    act_count = bracha87Fig1Bpr(b, actions);
    CHECK(act_count == 1, "Bpr 1 act when INITIATOR only");
    CHECK(actions[0] == BRACHA87_INITIAL_ALL, "Bpr INITIATOR -> INITIAL_ALL");

    /* Set ECHOED via Rule 1 */
    (void) bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, buf);
    CHECK((b->flags & BRACHA87_F1_ECHOED) != 0, "ECHOED set");
    act_count = bracha87Fig1Bpr(b, actions);
    CHECK(act_count == 2, "Bpr 2 acts when INITIATOR + ECHOED");
    CHECK(actions[0] == BRACHA87_INITIAL_ALL, "Bpr order: initial first");
    CHECK(actions[1] == BRACHA87_ECHO_ALL,    "Bpr order: echo second");

    /* Drive to RDSENT by feeding all echoes */
    for (i = 1; i < N_ACT; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i, v, buf);
    CHECK((b->flags & BRACHA87_F1_RDSENT) != 0, "RDSENT set");
    act_count = bracha87Fig1Bpr(b, actions);
    CHECK(act_count == 3, "Bpr 3 acts when INITIATOR+ECHOED+RDSENT");
    CHECK(actions[0] == BRACHA87_INITIAL_ALL, "Bpr order initial");
    CHECK(actions[1] == BRACHA87_ECHO_ALL,    "Bpr order echo");
    CHECK(actions[2] == BRACHA87_READY_ALL,   "Bpr order ready");

    /* Drive to ACCEPTED by feeding readys */
    for (i = 0; i < N_ACT; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_READY, (unsigned char) i, v, buf);
    CHECK((b->flags & BRACHA87_F1_ACCEPTED) != 0, "ACCEPTED");
    /* Post-ACCEPT contract: INITIAL and ECHO retire (bootstrap-only;
     * accept witnesses t+1 correct readys, so amplification completes
     * the protocol without them), READY continues (it is what the
     * amplification tail consumes -- Note 10). */
    act_count = bracha87Fig1Bpr(b, actions);
    {
      int saw_init = 0, saw_echo = 0, saw_ready = 0;
      for (i = 0; i < act_count; ++i) {
        if (actions[i] == BRACHA87_INITIAL_ALL) saw_init = 1;
        if (actions[i] == BRACHA87_ECHO_ALL)    saw_echo = 1;
        if (actions[i] == BRACHA87_READY_ALL)   saw_ready = 1;
      }
      CHECK(act_count == 1, "BPR post-ACCEPT: READY only");
      CHECK(!saw_init, "BPR INITIAL retired post-ACCEPT");
      CHECK(!saw_echo, "BPR ECHO retired post-ACCEPT");
      CHECK(saw_ready, "BPR READY post-ACCEPT");
    }

    /* Non-initiator, non-echoed instance: BPR returns 0 */
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(bracha87Fig1Bpr(b, actions) == 0, "Bpr 0 on fresh non-initiator");

    /* bracha87Fig1AllEchoed contract: 0 on null, 0 until an echo has
     * been recorded from every one of the n processes, 1 thereafter. */
    CHECK(bracha87Fig1AllEchoed(0) == 0, "AllEchoed: NULL -> 0");
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(bracha87Fig1AllEchoed(b) == 0, "AllEchoed: fresh -> 0");
    for (i = 0; i < N_ACT; ++i) {
      (void) bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i, v, buf);
      CHECK(bracha87Fig1AllEchoed(b) == (i + 1 == N_ACT),
            "AllEchoed: 1 exactly when echoSenders == n");
    }

    /* The all-echoed RETIRE, which is the accessor's reason to exist:
     * header, bracha87Fig1Bpr -- "INITIAL (initiator only): retires at
     * ACCEPTED, or once an echo has been observed from every process
     * (echoSenders == n).  INITIAL only induces echoes, so all-echoed
     * leaves nothing to induce."  The two gates are independent, so
     * this one must retire INITIAL with ACCEPTED still clear, while
     * ECHO and READY (which retire only at ACCEPTED) keep retrying.
     * Echoes alone reach that state: the echo threshold fires the echo
     * rule and then the ready rule, and ACCEPTED needs readys. */
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    bracha87Fig1Initiator(b, v);
    for (i = 0; i < N_ACT; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i, v, buf);
    CHECK(bracha87Fig1AllEchoed(b) == 1,
          "AllEchoed retire: every process has echoed");
    CHECK((b->flags & BRACHA87_F1_ACCEPTED) == 0,
          "AllEchoed retire: ACCEPTED still clear");
    act_count = bracha87Fig1Bpr(b, actions);
    {
      int saw_init = 0, saw_echo = 0, saw_ready = 0;
      for (i = 0; i < act_count; ++i) {
        if (actions[i] == BRACHA87_INITIAL_ALL) saw_init = 1;
        if (actions[i] == BRACHA87_ECHO_ALL)    saw_echo = 1;
        if (actions[i] == BRACHA87_READY_ALL)   saw_ready = 1;
      }
      CHECK(!saw_init, "BPR INITIAL retired at all-echoed without ACCEPTED");
      CHECK(saw_echo, "BPR ECHO still retried at all-echoed");
      CHECK(saw_ready, "BPR READY still retried at all-echoed");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 BPR per-process skip / accept");
  /* ---------------------------------------------------------------- */
  /* Header: bracha87Fig1Skip returns the suppress bitmap (process p       */
  /* skipped iff bit p set) -- INITIAL_ALL=echoed, ECHO_ALL=readied,    */
  /* READY_ALL=accepted, 0 for null/non-retry.  bracha87Fig1ProcessAccepted */
  /* sets the accepted bit; all-n-accepted retires READY retry.        */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    const unsigned char *m;
    unsigned int saw_ready;

    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(bracha87Fig1Skip(0, BRACHA87_READY_ALL) == 0, "Skip: NULL -> 0");
    CHECK(bracha87Fig1Skip(b, BRACHA87_ACCEPT) == 0, "Skip: non-retry act -> 0");
    CHECK(bracha87Fig1Skip(b, BRACHA87_INITIAL_ALL) != 0, "Skip: INITIAL non-null");

    /* INITIAL skip = echoed processes; ECHO skip = readied (not merely echoed). */
    (void) bracha87Fig1Input(b, BRACHA87_ECHO, 0, v, buf);   /* process 0 echoes */
    (void) bracha87Fig1Input(b, BRACHA87_READY, 2, v, buf);  /* process 2 readies */
    m = bracha87Fig1Skip(b, BRACHA87_INITIAL_ALL);
    CHECK(BRACHA87_SKIP_TST(m, 0) && !BRACHA87_SKIP_TST(m, 1), "Skip INITIAL: echoed process only");
    m = bracha87Fig1Skip(b, BRACHA87_ECHO_ALL);
    CHECK(BRACHA87_SKIP_TST(m, 2) && !BRACHA87_SKIP_TST(m, 0), "Skip ECHO: readied (not echoed) process");

    /* READY skip = accepted processes, set only via ProcessAccepted; guards. */
    m = bracha87Fig1Skip(b, BRACHA87_READY_ALL);
    CHECK(!BRACHA87_SKIP_TST(m, 2), "Skip READY: readied != accepted");
    bracha87Fig1ProcessAccepted(0, 0);                 /* NULL: no crash */
    bracha87Fig1ProcessAccepted(b, (unsigned char)(N_ACT + 9));  /* range: ignored */
    bracha87Fig1ProcessAccepted(b, 2);
    m = bracha87Fig1Skip(b, BRACHA87_READY_ALL);
    CHECK(BRACHA87_SKIP_TST(m, 2), "Skip READY: accepted process set after ProcessAccepted");

    /* All-n-accepted READY quiescence. */
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    (void) bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, buf);
    (void) bracha87Fig1Input(b, BRACHA87_READY, 1, v, buf);
    (void) bracha87Fig1Input(b, BRACHA87_READY, 2, v, buf); /* RDSENT */
    act_count = bracha87Fig1Bpr(b, actions);
    saw_ready = 0;
    for (i = 0; i < act_count; ++i)
      if (actions[i] == BRACHA87_READY_ALL) saw_ready = 1;
    CHECK(saw_ready, "Quiescence: READY output before all accepted");
    for (i = 0; i < N_ACT; ++i)
      bracha87Fig1ProcessAccepted(b, (unsigned char) i);
    act_count = bracha87Fig1Bpr(b, actions);
    saw_ready = 0;
    for (i = 0; i < act_count; ++i)
      if (actions[i] == BRACHA87_READY_ALL) saw_ready = 1;
    CHECK(!saw_ready, "Quiescence: READY retired when all n accepted");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 resend / received -- the READY retire's second half");
  /* ---------------------------------------------------------------- */
  /* Header, bracha87Fig1Skip: READY_ALL is the accepted processes     */
  /* net of outstanding arms, and "the READY mask is also the retire   */
  /* gate ... when all n of its bits are set."  Header,                */
  /* bracha87Fig1ProcessResend: records nothing before ACCEPTED;       */
  /* consumed by the next READY egress; a null or out-of-range         */
  /* argument is ignored.  Header, bracha87Fig1Received: the raw       */
  /* accepted set, 0 for null, and an armed sender is "precisely a     */
  /* process in this mask that must still be sent to."                 */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    const unsigned char *m;
    unsigned int saw_ready;

    CHECK(bracha87Fig1Received(0) == 0, "Received: NULL -> 0");
    bracha87Fig1ProcessResend(0, 0);                 /* NULL: no crash */

    /* Pre-ACCEPTED an unmarked READY records nothing. */
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    (void) bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, buf);
    (void) bracha87Fig1Input(b, BRACHA87_READY, 1, v, buf);
    (void) bracha87Fig1Input(b, BRACHA87_READY, 2, v, buf); /* RDSENT */
    CHECK((b->flags & BRACHA87_F1_ACCEPTED) == 0, "Resend: setup not accepted");
    bracha87Fig1ProcessAccepted(b, 1);
    bracha87Fig1ProcessResend(b, 1);
    CHECK(BRACHA87_SKIP_TST(bracha87Fig1Skip(b, BRACHA87_READY_ALL), 1),
          "Resend: pre-ACCEPTED unmarked READY records nothing");

    /* Post-ACCEPTED it un-suppresses, and the RECEIVED mask keeps the
     * process the suppress mask drops -- the two masks are different
     * questions about the same recipient. */
    for (i = 0; i < N_ACT; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_READY, (unsigned char) i, v, buf);
    CHECK((b->flags & BRACHA87_F1_ACCEPTED) != 0, "Resend: setup accepted");
    for (i = 0; i < N_ACT; ++i)
      bracha87Fig1ProcessAccepted(b, (unsigned char) i);
    act_count = bracha87Fig1Bpr(b, actions);       /* drain any standing arm */
    m = bracha87Fig1Skip(b, BRACHA87_READY_ALL);
    saw_ready = 1;
    for (i = 0; i < N_ACT; ++i)
      if (!BRACHA87_SKIP_TST(m, i)) saw_ready = 0;
    CHECK(saw_ready, "Gate: full coverage at all n accepted");
    act_count = bracha87Fig1Bpr(b, actions);
    saw_ready = 0;
    for (i = 0; i < act_count; ++i)
      if (actions[i] == BRACHA87_READY_ALL) saw_ready = 1;
    CHECK(!saw_ready, "Gate: READY retired at full coverage");

    /* An arm re-opens exactly the tick that re-sends marked, and that
     * re-send is TICK-PACED: the arm itself outputs nothing, and the
     * mask it leaves still names every other process. */
    bracha87Fig1ProcessResend(b, 2);
    m = bracha87Fig1Skip(b, BRACHA87_READY_ALL);
    CHECK(!BRACHA87_SKIP_TST(m, 2), "Resend: post-ACCEPTED arm un-suppresses");
    CHECK(BRACHA87_SKIP_TST(m, 0) && BRACHA87_SKIP_TST(m, 1)
       && BRACHA87_SKIP_TST(m, 3),
          "Resend: un-suppresses the armed sender only");
    m = bracha87Fig1Received(b);
    CHECK(BRACHA87_SKIP_TST(m, 2), "Received: an armed sender stays in the RECEIVED mask");
    act_count = bracha87Fig1Bpr(b, actions);
    saw_ready = 0;
    for (i = 0; i < act_count; ++i)
      if (actions[i] == BRACHA87_READY_ALL) saw_ready = 1;
    CHECK(saw_ready, "Gate: an arm re-opens READY");
    act_count = bracha87Fig1Bpr(b, actions);
    saw_ready = 0;
    for (i = 0; i < act_count; ++i)
      if (actions[i] == BRACHA87_READY_ALL) saw_ready = 1;
    CHECK(!saw_ready, "Gate: the egress consumed it and READY retires again");

    /* Out-of-range is ignored: no other process loses its suppression. */
    bracha87Fig1ProcessResend(b, (unsigned char)(N_ACT + 9));
    m = bracha87Fig1Skip(b, BRACHA87_READY_ALL);
    saw_ready = 1;
    for (i = 0; i < N_ACT; ++i)
      if (!BRACHA87_SKIP_TST(m, i)) saw_ready = 0;
    CHECK(saw_ready, "Resend: out-of-range ignored");

    /* An un-announced process is never marked: the mark would claim its
     * accept was received here, telling a correct process to stop
     * re-sending an announcement this instance still needs. */
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    m = bracha87Fig1Received(b);
    saw_ready = 0;
    for (i = 0; i < N_ACT; ++i)
      if (BRACHA87_SKIP_TST(m, i)) saw_ready = 1;
    CHECK(!saw_ready, "Received: fresh instance marks nobody");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 post-accept observation");
  /* ---------------------------------------------------------------- */
  /* Header, bracha87Fig1AllEchoed: "1 iff this instance has recorded  */
  /* an echo from every one of the n processes", and "under <= t       */
  /* silent processes this never reaches 1" -- the only stated bar.    */
  /* Accept is reachable on readys alone (2t+1, Rule 6) without any    */
  /* echo recorded, so the two are independent and an echo arriving    */
  /* after accept must still be recorded.  Header, bracha87Fig1Input:  */
  /* "Returns number of actions (0..3)"; nothing after accept meets a  */
  /* rule, so the count is 0 and BRACHA87_ACCEPT never repeats.        */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    unsigned int saw_accept;
    unsigned int post_acts;

    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    bracha87Fig1Initiator(b, v);
    /* 2t+1 = 3 distinct readys accept, with no echo recorded. */
    for (i = 0; i < 2u * T_VAL + 1; ++i)
      act_count = bracha87Fig1Input(b, BRACHA87_READY, (unsigned char) i, v, actions);
    saw_accept = 0;
    for (i = 0; i < act_count; ++i)
      if (actions[i] == BRACHA87_ACCEPT) saw_accept = 1;
    CHECK(saw_accept, "PostAccept: 2t+1 readys accept");
    CHECK(bracha87Fig1AllEchoed(b) == 0, "PostAccept: no echo recorded at accept");

    /* n distinct echoes, all delivered after accept. */
    post_acts = 0;
    saw_accept = 0;
    for (i = 0; i < N_ACT; ++i) {
      act_count = bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i, v, actions);
      post_acts += act_count;
      for (j = 0; j < act_count; ++j)
        if (actions[j] == BRACHA87_ACCEPT) saw_accept = 1;
      CHECK(bracha87Fig1AllEchoed(b) == (i + 1 == N_ACT),
            "PostAccept: AllEchoed 1 exactly when echo senders == n");
    }
    /* Duplicates and an INITIAL round out the post-accept traffic. */
    post_acts += bracha87Fig1Input(b, BRACHA87_ECHO, 0, v, actions);
    post_acts += bracha87Fig1Input(b, BRACHA87_READY, 0, v, actions);
    post_acts += bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, actions);
    CHECK(post_acts == 0, "PostAccept: every post-accept Input returns 0 actions");
    CHECK(!saw_accept, "PostAccept: ACCEPT is not output a second time");
    CHECK(bracha87Fig1AllEchoed(b) == 1, "PostAccept: AllEchoed stays 1");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig2 receive / dedup / ROUND_COMPLETE / GetReceived");
  /* ---------------------------------------------------------------- */
  /* Header: "Returns BRACHA87_ROUND_COMPLETE if this causes n-t       */
  /* received, 0 otherwise.  Deduplication: one message per sender    */
  /* per round."                                                      */
  /* With encoded n=N_ENC=3, actual_n=4, t=1, n-t=3.                   */
  {
    static unsigned char fig2buf[2048];
    struct bracha87Fig2 *b = (struct bracha87Fig2 *) fig2buf;
    unsigned char senders[N_ACT], values[N_ACT];
    unsigned int got;
    unsigned int seen = 0;
    sz = bracha87Fig2Sz(N_ENC, 4);
    CHECK(sz <= sizeof (fig2buf), "fig2buf big enough");
    bracha87Fig2Init(b, N_ENC, T_VAL, 4);
    CHECK(bracha87Fig2RecvCount(b, 0) == 0, "fresh count = 0");
    /* Two distinct senders -> below n-t */
    CHECK(bracha87Fig2Receive(b, 0, 0, 1) == 0, "no complete at 1");
    CHECK(bracha87Fig2Receive(b, 0, 1, 1) == 0, "no complete at 2");
    /* dedup */
    CHECK(bracha87Fig2Receive(b, 0, 0, 1) == 0, "dedup");
    CHECK(bracha87Fig2RecvCount(b, 0) == 2, "RecvCount holds at 2");
    /* third distinct sender hits n-t = 3 -> ROUND_COMPLETE */
    CHECK(bracha87Fig2Receive(b, 0, 2, 1) == BRACHA87_ROUND_COMPLETE,
          "ROUND_COMPLETE at n-t");
    /* subsequent receives must NOT re-fire complete */
    CHECK(bracha87Fig2Receive(b, 0, 3, 1) == 0,
          "no re-fire of ROUND_COMPLETE");
    /* round 1 untouched */
    CHECK(bracha87Fig2RecvCount(b, 1) == 0, "round1 empty");
    /* GetReceived */
    got = bracha87Fig2GetReceived(b, 0, senders, values);
    CHECK(got == N_ACT, "GetReceived count = N_ACT");
    for (i = 0; i < got; ++i) {
      CHECK(values[i] == 1, "stored value preserved");
      if (senders[i] < 32) seen |= 1U << senders[i];
    }
    CHECK(seen == 0x0F, "all 4 senders present");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 array Retry cursor / SentCount / NULL slots");
  /* ---------------------------------------------------------------- */
  /* Header: RetryInit + Fig1RetryStep walk the array, returning one    */
  /* instance's BPR per call.  NULL entries skipped.  SentCount  */
  /* counts INITIATOR | ECHOED | RDSENT slots.  Idle full sweep returns  */
  /* 0.                                                               */
  {
    static unsigned char pool[8][512];
    struct bracha87Fig1 *arr[8];
    struct bracha87Retry retry;
    struct bracha87Fig1Act outActs[BRACHA87_FIG1_RETRY_MAX_ACTS];
    static const unsigned char v[1] = { 0x42 };
    unsigned int got;
    sz = bracha87Fig1Sz(N_ENC, VLEN_BIN);
    CHECK(sz <= sizeof (pool[0]), "retry pool slot ok");
    /* slots 0..3 used; 4..7 NULL */
    for (i = 0; i < 4; ++i) {
      arr[i] = (struct bracha87Fig1 *) pool[i];
      bracha87Fig1Init(arr[i], N_ENC, T_VAL, VLEN_BIN);
    }
    for (i = 4; i < 8; ++i) arr[i] = 0;
    bracha87Fig1Initiator(arr[1], v);
    (void) bracha87Fig1Input(arr[2], BRACHA87_INITIAL, 0, v, buf);
    CHECK(bracha87Fig1SentCount((struct bracha87Fig1 *const *) arr, 8)
            == 2, "SentCount = 2 (initiator + echoed)");
    bracha87RetryInit(&retry);
    {
      int saw_init = 0, saw_echo = 0;
      for (r = 0; r < 16; ++r) {
        got = bracha87Fig1RetryStep((struct bracha87Fig1 *const *) arr, 8,
                                   &retry, outActs);
        if (got == 0) continue;
        for (i = 0; i < got; ++i) {
          if (outActs[i].act == BRACHA87_INITIAL_ALL) saw_init = 1;
          if (outActs[i].act == BRACHA87_ECHO_ALL) saw_echo = 1;
          CHECK(outActs[i].idx < 8, "RetryStep idx in range");
          CHECK(arr[outActs[i].idx] != 0, "RetryStep skips NULL slots");
        }
      }
      CHECK(saw_init, "RetryStep outputs INITIAL_ALL for INITIATOR slot");
      CHECK(saw_echo, "RetryStep outputs ECHO_ALL for ECHOED slot");
    }
    /* Idle sweep: all instances fresh, nothing sent -> a full sweep    */
    /* returns 0 at least once.                                        */
    {
      struct bracha87Fig1 *idleArr[8];
      int sawZero = 0;
      for (i = 0; i < 8; ++i) {
        bracha87Fig1Init((struct bracha87Fig1 *) pool[i],
                         N_ENC, T_VAL, VLEN_BIN);
        idleArr[i] = (struct bracha87Fig1 *) pool[i];
      }
      bracha87RetryInit(&retry);
      for (r = 0; r < 32; ++r) {
        got = bracha87Fig1RetryStep((struct bracha87Fig1 *const *) idleArr, 8,
                                   &retry, outActs);
        if (got == 0) { sawZero = 1; break; }
      }
      CHECK(sawZero, "RetryStep returns 0 on idle full sweep");
      CHECK(bracha87Fig1SentCount((struct bracha87Fig1 *const *) idleArr,
                                       8) == 0,
            "SentCount = 0 when idle");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Retry cursor: the `sweeps` wrap count");
  /* ---------------------------------------------------------------- */
  /* Header (struct bracha87Retry): `sweeps` counts completed passes,   */
  /* is zeroed by RetryInit, and must be COMPARED -- one call can       */
  /* complete two passes, so a caller assuming +1 misses a boundary.    */
  /* That last claim is why the contract is worded as it is, so it      */
  /* gets a witness rather than a restatement.                          */
  {
    static unsigned char spool[2][512];
    struct bracha87Fig1 *sarr[2];
    struct bracha87Retry sretry;
    struct bracha87Fig1Act sacts[BRACHA87_FIG1_RETRY_MAX_ACTS];
    static const unsigned char sv[1] = { 0x5A };
    unsigned int before;
    unsigned int got2;

    bracha87RetryInit(&sretry);
    CHECK(sretry.sweeps == 0, "RetryInit zeroes sweeps");

    for (i = 0; i < 2; ++i) {
      sarr[i] = (struct bracha87Fig1 *) spool[i];
      bracha87Fig1Init(sarr[i], N_ENC, T_VAL, VLEN_BIN);
    }
    sarr[1] = 0;                      /* one live slot, one NULL */
    bracha87Fig1Initiator(sarr[0], sv);

    /* A pass over a 2-slot array with one act-producing instance:      */
    /* each call visits it, so each call completes exactly one pass.    */
    before = sretry.sweeps;
    (void) bracha87Fig1RetryStep((struct bracha87Fig1 *const *) sarr, 2,
                                 &sretry, sacts);
    CHECK(sretry.sweeps == before,
          "sweeps does not advance before the cursor wraps");
    (void) bracha87Fig1RetryStep((struct bracha87Fig1 *const *) sarr, 2,
                                 &sretry, sacts);
    CHECK(sretry.sweeps == before + 1, "sweeps advances once per pass");

    /*
     * THE DOUBLE ADVANCE.  Retire the instance's only retry between
     * calls: drive it to ACCEPTED so INITIAL and ECHO retire, and
     * record every process's accept so READY retires on the remote
     * all-accepted gate.  The next call then wraps once carrying the
     * previous pass's actions, finds the whole array empty, and wraps
     * again to return 0 -- two completed passes in one call.
     */
    for (i = 0; i < N_ACT; ++i)
      (void) bracha87Fig1Input(sarr[0], BRACHA87_READY,
                               (unsigned char) i, sv, buf);
    for (i = 0; i < N_ACT; ++i)
      bracha87Fig1ProcessAccepted(sarr[0], (unsigned char) i);
    before = sretry.sweeps;
    got2 = bracha87Fig1RetryStep((struct bracha87Fig1 *const *) sarr, 2,
                                 &sretry, sacts);
    CHECK(got2 == 0, "retired instance yields a 0 return");
    CHECK(sretry.sweeps >= before + 1, "sweeps advanced across the 0 return");
    CHECK(sretry.sweeps == before + 2,
          "one call completed TWO passes -- compare, never assume +1");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Rule 2 precise echo threshold (n=4, t=1)");
  /* ---------------------------------------------------------------- */
  /* Header rule-table convention now nailed down:                    */
  /*   n in the rule formulas = ACTUAL process count = struct.n + 1   */
  /*   t = struct.t                                                   */
  /* Rule 2 fires when !echoed && ecCnt[v] > (n+t)/2.                 */
  /* For n_actual=4, t=1: (4+1)/2 = 2 (C integer div).                */
  /* Strict > 2 means Rule 2 fires on the 3rd distinct echo, not the  */
  /* 2nd.  Per-sender dedup ensures duplicates from one sender don't  */
  /* contribute.                                                      */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    int firedAt[N_ACT];
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    for (i = 0; i < N_ACT; ++i) firedAt[i] = 0;
    /* Echoes from senders 0, 1, 2 (3rd echo = sender 2) */
    for (i = 0; i < 3; ++i) {
      act_count = bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i,
                                    v, actions);
      for (j = 0; j < act_count; ++j)
        if (actions[j] == BRACHA87_ECHO_ALL) firedAt[i] = 1;
    }
    CHECK(firedAt[0] == 0, "Rule2: 1st distinct echo does not fire");
    CHECK(firedAt[1] == 0, "Rule2: 2nd distinct echo does not fire");
    CHECK(firedAt[2] == 1, "Rule2: 3rd distinct echo fires (strict >)");
    CHECK((b->flags & BRACHA87_F1_ECHOED) != 0,
          "Rule2 precise: ECHOED set after 3rd distinct echo");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Rule 2 precise echo threshold (n=7, t=2)");
  /* ---------------------------------------------------------------- */
  /* For n_actual=7, t=2: (7+2)/2 = 4 (C integer div).                */
  /* Strict > 4 means Rule 2 fires on the 5th distinct echo.          */
  /* Use larger storage and an n=7 instance.                          */
  {
    static unsigned char fig1Big[4096];
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Big;
    static const unsigned char v[1] = { 1 };
    int firedAt[7];
    sz = bracha87Fig1Sz(6, VLEN_BIN); /* n_enc=6 -> actual=7 */
    CHECK(sz <= sizeof (fig1Big), "fig1Big big enough for n=7");
    bracha87Fig1Init(b, 6, 2, VLEN_BIN);
    for (i = 0; i < 7; ++i) firedAt[i] = 0;
    for (i = 0; i < 7; ++i) {
      act_count = bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i,
                                    v, actions);
      for (j = 0; j < act_count; ++j)
        if (actions[j] == BRACHA87_ECHO_ALL) firedAt[i] = 1;
      if ((b->flags & BRACHA87_F1_ECHOED) != 0) break;
    }
    CHECK(firedAt[0] == 0, "n=7,t=2: echo 1 does not fire");
    CHECK(firedAt[1] == 0, "n=7,t=2: echo 2 does not fire");
    CHECK(firedAt[2] == 0, "n=7,t=2: echo 3 does not fire");
    CHECK(firedAt[3] == 0, "n=7,t=2: echo 4 does not fire");
    CHECK(firedAt[4] == 1, "n=7,t=2: echo 5 fires (strict > 4)");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4Round post-EXHAUSTED safety");
  /* ---------------------------------------------------------------- */
  /* Header: subsequent calls to bracha87Fig4Round on EXHAUSTED       */
  /* return 0 actions; the state machine remains in EXHAUSTED.        */
  /*                                                                  */
  /* Drive an instance to EXHAUSTED with maxPhases=1 so the schedule  */
  /* is short.  At sub=2 of the last phase, with no decision and no   */
  /* >t (d,v) majority, EXHAUSTED is output (header).                */
  /*                                                                  */
  /* Strategy: run 3 rounds (0, 1, 2) with split values that produce  */
  /* no >2t (d,v) and no >t (d,v) majority -- specifically, give each */
  /* round n-t messages with values lacking any D_FLAG, so the         */
  /* decideV / setDMajority paths cannot fire and the coin path runs. */
  /* At sub=2 of phase 0 (the only phase with maxPhases=1), no        */
  /* decision means EXHAUSTED.                                        */
  {
    static unsigned char fig4Buf[32 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    unsigned char values[N_ACT];
    unsigned int nact;
    int sawExhausted = 0;
    sz = bracha87Fig4Sz(N_ENC, 1);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for EXHAUSTED test");
    bracha87Fig4Init(fig4, N_ENC, T_VAL, 1, 0, 0, testCoinAlt, 0);
    /* Round 0: 3 messages (n-t=3), no D_FLAG legal here.             */
    values[0] = 0;
    values[1] = 1;
    values[2] = 0;
    nact = bracha87Fig4Round(fig4, 0, 3, values);
    /* Should advance to next sub-round; expect BROADCAST */
    {
      unsigned int hadBroadcast = 0;
      if (nact & BRACHA87_BROADCAST) hadBroadcast = 1;
      CHECK(hadBroadcast, "round 0 advances to broadcast");
    }
    /* Round 1: legal values are {0, 1, D_FLAG|0, D_FLAG|1}.  Use no- */
    /* D_FLAG mixed values so no >n/2 majority sets D_FLAG.            */
    values[0] = 0;
    values[1] = 1;
    values[2] = 0;
    nact = bracha87Fig4Round(fig4, 1, 3, values);
    {
      unsigned int hadBroadcast = 0;
      if (nact & BRACHA87_BROADCAST) hadBroadcast = 1;
      CHECK(hadBroadcast, "round 1 advances to broadcast");
    }
    /* Round 2 (sub=2 of phase 0): no D_FLAG -> no decideV / no >t    */
    /* majority -> coin, then EXHAUSTED because phase 0 was last.     */
    values[0] = 0;
    values[1] = 1;
    values[2] = 0;
    nact = bracha87Fig4Round(fig4, 2, 3, values);
    if (nact & BRACHA87_EXHAUSTED) sawExhausted = 1;
    CHECK(sawExhausted, "EXHAUSTED output at sub=2 of last phase");
    CHECK((nact & BRACHA87_DECIDE) == 0,
          "EXHAUSTED mutually exclusive with DECIDE");
    /* Subsequent calls must return 0 and remain EXHAUSTED -- header  */
    /* states: "Subsequent calls to bracha87Fig4Round on an EXHAUSTED */
    /* instance are safe and return 0 actions; the state machine      */
    /* remains in EXHAUSTED" AND "BRACHA87_EXHAUSTED is also returned */
    /* at most once."                                                 */
    nact = bracha87Fig4Round(fig4, 2, 3, values);
    CHECK(nact == 0, "post-EXHAUSTED Round(2) returns 0");
    /* Try other rounds too -- still 0 */
    nact = bracha87Fig4Round(fig4, 0, 3, values);
    CHECK(nact == 0, "post-EXHAUSTED Round(round=0) returns 0");
    nact = bracha87Fig4Round(fig4, 1, 3, values);
    CHECK(nact == 0, "post-EXHAUSTED Round(round=1) returns 0");
    /* No DECIDE, no decision recorded */
    CHECK(!(fig4->flags & BRACHA87_F4_DECIDED), "post-EXHAUSTED decided flag still 0");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4Round refuses a null values with a nonzero n_msgs");
  /* ---------------------------------------------------------------- */
  /* Header: "Outside those four sit the null arguments -- a null     */
  /* instance, or a null values with a nonzero n_msgs ... refused the */
  /* way every other bad input is, 0 actions and no abort, rather     */
  /* than dereferenced."  Without the guard the counting loop         */
  /* dereferences it, so the arm's real assertion is that the process */
  /* is still alive to report the 0.                                  */
  /* ---------------------------------------------------------------- */
  {
    static unsigned char nvBuf[32 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) nvBuf;

    sz = bracha87Fig4Sz(N_ENC, 2);
    CHECK(sz <= sizeof (nvBuf), "nvBuf size for the null-values arm");
    CHECK(bracha87Fig4Init(fig4, N_ENC, T_VAL, 2, 0, 0, testCoinAlt, 0) == 1,
          "null-values arm: Init");
    CHECK(bracha87Fig4Round(fig4, 0, 3, 0) == 0,
          "Fig4Round returns 0 on a null values");
    CHECK(fig4->phase == 0 && fig4->subRound == 0,
          "a refused Fig4Round advanced nothing");
    CHECK(bracha87Fig4Round(fig4, 0, 0, 0) == 0,
          "Fig4Round returns 0 on an empty set");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4Round refuses a round that is not the machine's next");
  /* ---------------------------------------------------------------- */
  /* Header: "k NAMES THE ROUND THIS CALL COMPUTES, and it must be    */
  /* the machine's own next round ... Any other k is REFUSED (0        */
  /* actions, no state change) rather than retargeting the machine."   */
  /*                                                                  */
  /* The discriminating property is NO STATE CHANGE: a machine that    */
  /* silently retargeted would advance phase/subRound to follow the    */
  /* k it was handed, so the check reads those fields on both sides.  */
  {
    static unsigned char fig4Buf[32 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    unsigned char values[N_ACT];
    unsigned int nact;
    unsigned char ph;
    unsigned char sub;

    sz = bracha87Fig4Sz(N_ENC, 2);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for round-k test");
    bracha87Fig4Init(fig4, N_ENC, T_VAL, 2, 0, 0, testCoinAlt, 0);
    values[0] = 0;
    values[1] = 1;
    values[2] = 0;

    /* Fresh machine sits at round 0.  Every other k is refused. */
    ph = fig4->phase;
    sub = fig4->subRound;
    CHECK(ph == 0 && sub == 0, "fresh Fig4 is at round 0");
    nact = bracha87Fig4Round(fig4, 1, 3, values);
    CHECK(nact == 0, "round 1 on a machine at round 0 is refused");
    CHECK(fig4->phase == ph && fig4->subRound == sub,
          "refused round leaves phase/subRound untouched");
    nact = bracha87Fig4Round(fig4, 5, 3, values);
    CHECK(nact == 0, "round 5 on a machine at round 0 is refused");
    CHECK(fig4->phase == ph && fig4->subRound == sub,
          "far-ahead refused round leaves phase/subRound untouched");

    /* The machine's own next round is accepted and advances it. */
    nact = bracha87Fig4Round(fig4, 0, 3, values);
    CHECK(nact != 0, "round 0 on a machine at round 0 is taken");
    CHECK(fig4->phase == 0 && fig4->subRound == 1,
          "taken round advances to sub 1");

    /* A round already spent is refused on the same rule. */
    nact = bracha87Fig4Round(fig4, 0, 3, values);
    CHECK(nact == 0, "replay of a spent round is refused");
    CHECK(fig4->phase == 0 && fig4->subRound == 1,
          "replay leaves phase/subRound untouched");

    /* Crossing a phase boundary: after sub 2 the next k is 3. */
    nact = bracha87Fig4Round(fig4, 1, 3, values);
    CHECK(nact != 0, "round 1 taken after round 0");
    nact = bracha87Fig4Round(fig4, 2, 3, values);
    CHECK(nact != 0, "round 2 taken after round 1");
    CHECK(fig4->phase == 1 && fig4->subRound == 0,
          "phase advanced past sub 2");
    nact = bracha87Fig4Round(fig4, 2, 3, values);
    CHECK(nact == 0, "round 2 refused once the machine is at round 3");
    nact = bracha87Fig4Round(fig4, 3, 3, values);
    CHECK(nact != 0, "round 3 taken across the phase boundary");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 t=0: a READY that overtakes the INITIAL crosses both ready thresholds at once");
  /* ---------------------------------------------------------------- */
  /* At t = 0 the thresholds t+1 and 2t+1 are the same integer, so the */
  /* first READY at a process that has neither echoed nor sent ready   */
  /* fires the echo, ready and accept rules in one step.  This is the  */
  /* one dispatch leaf reachable only at t = 0.                        */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    int sawEcho = 0, sawReady = 0, sawAccept = 0;

    CHECK(bracha87Fig1Init(b, N_ENC, 0, VLEN_BIN) == 1, "Fig1Init at t = 0");
    act_count = bracha87Fig1Input(b, BRACHA87_READY, 1, v, actions);
    for (j = 0; j < act_count; ++j) {
      if (actions[j] == BRACHA87_ECHO_ALL) sawEcho = 1;
      if (actions[j] == BRACHA87_READY_ALL) sawReady = 1;
      if (actions[j] == BRACHA87_ACCEPT) sawAccept = 1;
    }
    CHECK(act_count == 3, "t=0 overtaking READY: three actions in one step");
    CHECK(sawEcho && sawReady && sawAccept,
          "t=0 overtaking READY: ECHO_ALL, READY_ALL and ACCEPT together");
    CHECK((b->flags & (BRACHA87_F1_ECHOED | BRACHA87_F1_RDSENT
                       | BRACHA87_F1_ACCEPTED))
          == (BRACHA87_F1_ECHOED | BRACHA87_F1_RDSENT | BRACHA87_F1_ACCEPTED),
          "t=0 overtaking READY: ECHOED, RDSENT and ACCEPTED all set");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1: an echo crossing the threshold at an un-echoed process echoes and readys at once");
  /* ---------------------------------------------------------------- */
  /* Rule 2 sends (echo, v) on the crossing, and the ready rules read  */
  /* that echo rather than the flag the message arrived on, so Rule 4  */
  /* fires on the same message.  A process is in that state only if    */
  /* the INITIAL had not arrived when the count crossed, since Rule 1  */
  /* echoes on arrival.                                                */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    int sawEcho = 0, sawReady = 0;

    CHECK(bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN) == 1,
          "Fig1Init for the un-echoed crossing");
    CHECK(bracha87Fig1Input(b, BRACHA87_ECHO, 0, v, actions) == 0,
          "un-echoed crossing: the first echo is below the threshold");
    CHECK(bracha87Fig1Input(b, BRACHA87_ECHO, 1, v, actions) == 0,
          "un-echoed crossing: the second echo is below the threshold");
    act_count = bracha87Fig1Input(b, BRACHA87_ECHO, 2, v, actions);
    for (j = 0; j < act_count; ++j) {
      if (actions[j] == BRACHA87_ECHO_ALL) sawEcho = 1;
      if (actions[j] == BRACHA87_READY_ALL) sawReady = 1;
    }
    CHECK(act_count == 2, "un-echoed crossing: two actions in one step");
    CHECK(sawEcho && sawReady,
          "un-echoed crossing: ECHO_ALL and READY_ALL together");
    CHECK((b->flags & (BRACHA87_F1_ECHOED | BRACHA87_F1_RDSENT))
          == (BRACHA87_F1_ECHOED | BRACHA87_F1_RDSENT),
          "un-echoed crossing: ECHOED and RDSENT both set");
    CHECK(bracha87Fig1Input(b, BRACHA87_ECHO, 3, v, actions) == 0,
          "un-echoed crossing: a later echo adds nothing");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig3: the permissive VALID check rejects a base above 1 and a d-flag on the wrong base");
  /* ---------------------------------------------------------------- */
  /* With an N that answers "permissive, 0|D_FLAG legitimate", a       */
  /* round-1 message validates only as 0, 1 or 0|D_FLAG: a base of 2   */
  /* is rejected and so is 1|D_FLAG.  Round 0 is filled to n-t first   */
  /* so round 1 is evaluated at all.  Then the same messages arrive    */
  /* BEFORE round 0 is full, so the cascade re-derives them and must   */
  /* reach the same verdicts.                                          */
  {
    unsigned char *b3;
    unsigned int vc;
    unsigned char s[N_ACT], vv[N_ACT];

    if ((b3 = malloc(bracha87Fig3Sz(N_ENC, 4))) != 0) {
      struct bracha87Fig3 *f3 = (struct bracha87Fig3 *) b3;

      CHECK(bracha87Fig3Init(f3, N_ENC, T_VAL, 4, testNPermissive0D, 0) == 1,
            "Fig3Init with the permissive N");
      for (i = 0; i < 3; ++i)
        (void) bracha87Fig3Accept(f3, 0, (unsigned char) i, 0, 0);
      CHECK(bracha87Fig3RoundComplete(f3, 0) == 1, "round 0 complete at n-t");
      CHECK(bracha87Fig3Accept(f3, 1, 0, 2, &vc) == 0,
            "permissive: a base of 2 is rejected");
      CHECK(bracha87Fig3Accept(f3, 1, 1, 1 | BRACHA87_D_FLAG, &vc) == 0,
            "permissive: 1|D_FLAG against a 0|D_FLAG permission is rejected");
      CHECK(bracha87Fig3Accept(f3, 1, 2, 0 | BRACHA87_D_FLAG, &vc)
            == BRACHA87_VALIDATED,
            "permissive: 0|D_FLAG is validated");
      CHECK(vc == 1, "permissive: one validated round-1 message");
      CHECK(bracha87Fig3Accept(f3, 1, 3, 0, &vc) == BRACHA87_VALIDATED
            && vc == 2, "permissive: a plain 0 is validated");

      CHECK(bracha87Fig3Init(f3, N_ENC, T_VAL, 4, testNPermissive0D, 0) == 1,
            "Fig3Init again for the cascade order");
      CHECK(bracha87Fig3Accept(f3, 1, 0, 2, &vc) == 0,
            "cascade: a base of 2 is stored invalid");
      CHECK(bracha87Fig3Accept(f3, 1, 1, 1 | BRACHA87_D_FLAG, &vc) == 0,
            "cascade: 1|D_FLAG is stored invalid");
      CHECK(bracha87Fig3Accept(f3, 1, 2, 0 | BRACHA87_D_FLAG, &vc) == 0,
            "cascade: 0|D_FLAG is stored invalid before round 0 is full");
      for (i = 0; i < 3; ++i)
        (void) bracha87Fig3Accept(f3, 0, (unsigned char) i, 0, 0);
      CHECK(bracha87Fig3GetValid(f3, 1, s, vv) == 1,
            "cascade: exactly one round-1 message is re-derived valid");
      CHECK(s[0] == 2 && vv[0] == (0 | BRACHA87_D_FLAG),
            "cascade: the re-derived message is sender 2's 0|D_FLAG");

      /* Same order under an N that permits no d-flag at all: the
       * cascade must reject 0|D_FLAG and keep the plain 0. */
      CHECK(bracha87Fig3Init(f3, N_ENC, T_VAL, 4, testNPermissiveNoD, 0) == 1,
            "Fig3Init with the no-d-flag permissive N");
      CHECK(bracha87Fig3Accept(f3, 1, 0, 0 | BRACHA87_D_FLAG, &vc) == 0,
            "cascade, no d-flag permitted: 0|D_FLAG is stored invalid");
      CHECK(bracha87Fig3Accept(f3, 1, 1, 0, &vc) == 0,
            "cascade, no d-flag permitted: a plain 0 is stored invalid before round 0 is full");
      for (i = 0; i < 3; ++i)
        (void) bracha87Fig3Accept(f3, 0, (unsigned char) i, 0, 0);
      CHECK(bracha87Fig3GetValid(f3, 1, s, vv) == 1 && s[0] == 1 && vv[0] == 0,
            "cascade, no d-flag permitted: only the plain 0 is re-derived valid");
      free(b3);
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4: a decided process keeps its decision under t < (d,v) <= 2t and ends its last phase without EXHAUSTED");
  /* ---------------------------------------------------------------- */
  /* Decide at phase 0 with > 2t (d,1), continue into phase 1, and at  */
  /* its step 3 validate exactly 2t (d,1): more than t, not more than  */
  /* 2t.  A decided process must neither adopt, nor toss, nor change   */
  /* its value, and when that is its last phase the round returns 0    */
  /* rather than EXHAUSTED -- it has decided.  Pass 0 leaves the       */
  /* sample without a base-value majority, pass 1 keeps one.           */
  {
    static unsigned char fig4Buf[32 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    unsigned char values[N_ACT];
    unsigned int nact;
    unsigned int pass;

    for (pass = 0; pass < 2; ++pass) {
      sz = bracha87Fig4Sz(N_ENC, 2);
      CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for the decided-process arm");
      bracha87Fig4Init(fig4, N_ENC, T_VAL, 2, 1, 0, testCoinAlt, 0);
      values[0] = 1;
      values[1] = 1;
      values[2] = 1;
      nact = bracha87Fig4Round(fig4, 0, 3, values);
      CHECK(nact == BRACHA87_BROADCAST, "decided arm: round 0 broadcasts");
      nact = bracha87Fig4Round(fig4, 1, 3, values);
      CHECK(nact == BRACHA87_BROADCAST, "decided arm: round 1 broadcasts");
      values[0] = 1 | BRACHA87_D_FLAG;
      values[1] = 1 | BRACHA87_D_FLAG;
      values[2] = 1 | BRACHA87_D_FLAG;
      nact = bracha87Fig4Round(fig4, 2, 3, values);
      CHECK(nact == (BRACHA87_DECIDE | BRACHA87_BROADCAST),
            "decided arm: > 2t (d,1) decides and continues");
      CHECK((fig4->flags & BRACHA87_F4_DECIDED) && fig4->decision == 1,
            "decided arm: the decision is 1");
      values[0] = 1;
      values[1] = 1;
      values[2] = 1;
      nact = bracha87Fig4Round(fig4, 3, 3, values);
      CHECK(nact == BRACHA87_BROADCAST, "decided arm: post-decide round 3 broadcasts");
      nact = bracha87Fig4Round(fig4, 4, 3, values);
      CHECK(nact == BRACHA87_BROADCAST, "decided arm: post-decide round 4 broadcasts");
      values[0] = 1 | BRACHA87_D_FLAG;
      values[1] = 1 | BRACHA87_D_FLAG;
      values[2] = pass ? 1 : 0;
      nact = bracha87Fig4Round(fig4, 5, 3, values);
      CHECK(nact == 0,
            "decided arm: the last phase of a decided process returns 0, not EXHAUSTED");
      CHECK((fig4->flags & BRACHA87_F4_EXHAUSTED) == 0,
            "decided arm: no EXHAUSTED flag on a decided process");
      /* dmax is 1 here, so an errant adopt would write the decision's
       * own value and the sub-round-2 tail restores it regardless:
       * this pins consistency, and the arm's discriminating check is
       * the return code above. */
      CHECK(fig4->value == 1 && fig4->decision == 1,
            "decided arm: t < (d,v) <= 2t leaves the decision in place");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4 n=6 t=1: > 2t (d,v) decides although the sample has no base-value majority");
  /* ---------------------------------------------------------------- */
  /* Below n = 4t+2 a sample with > 2t (d,v) always carries a strict  */
  /* majority of v among its base values; at n = 6, t = 1 three (d,1)  */
  /* and two 0 do not.  Step 3 decides on the (d,v) count alone.       */
  {
    static unsigned char fig4Buf6[64 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf6;
    unsigned char v6[6];
    unsigned int nact;

    sz = bracha87Fig4Sz(5, 1);
    CHECK(sz <= sizeof (fig4Buf6), "fig4Buf6 size for the n=6 arm");
    bracha87Fig4Init(fig4, 5, 1, 1, 0, 0, testCoinAlt, 0);
    v6[0] = 0; v6[1] = 0; v6[2] = 1; v6[3] = 1; v6[4] = 0;
    nact = bracha87Fig4Round(fig4, 0, 5, v6);
    CHECK(nact == BRACHA87_BROADCAST, "n=6 arm: round 0 broadcasts");
    nact = bracha87Fig4Round(fig4, 1, 5, v6);
    CHECK(nact == BRACHA87_BROADCAST, "n=6 arm: round 1 broadcasts");
    v6[0] = 1 | BRACHA87_D_FLAG;
    v6[1] = 1 | BRACHA87_D_FLAG;
    v6[2] = 1 | BRACHA87_D_FLAG;
    v6[3] = 0;
    v6[4] = 0;
    nact = bracha87Fig4Round(fig4, 2, 5, v6);
    CHECK(nact == BRACHA87_DECIDE,
          "n=6 arm: three (d,1) among five decide at the last phase");
    CHECK((fig4->flags & BRACHA87_F4_DECIDED) && fig4->decision == 1,
          "n=6 arm: the decision is 1");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4: a decided process holds its decision through samples with no >n/2 camp");
  /* ---------------------------------------------------------------- */
  /* Notes 1 and 9: a decided process keeps broadcasting and keeps its */
  /* decision as the broadcast value, whatever later samples show.     */
  /* Step 1 takes the sample's majority on no threshold at all, step 2 */
  /* flags a >n/2 camp, step 3 takes the >2t or >t (d,v) count or      */
  /* tosses; every one of them is suppressed once decided.  A sample   */
  /* only WITNESSES that suppression when what the rule would have     */
  /* written differs from the decision, so the sample below carries a  */
  /* majority of 0 against a decision of 1 and no camp above n/2 -- at */
  /* n = 4, t = 1 a 2-1 split of an n-t sample is no camp at all.  At  */
  /* n = 4t+2 a decided process can also meet a >2t (d,v) count that   */
  /* still carries no camp, which the second half of the arm drives.   */
  {
    static unsigned char fig4Buf[64 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    unsigned char values[6];
    unsigned int nact;
    unsigned int r;

    sz = bracha87Fig4Sz(N_ENC, 4);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for the split-sample arm");
    bracha87Fig4Init(fig4, N_ENC, T_VAL, 4, 0, 0, testCoinAlt, 0);
    values[0] = values[1] = values[2] = 1;
    (void) bracha87Fig4Round(fig4, 0, 3, values);
    (void) bracha87Fig4Round(fig4, 1, 3, values);
    values[0] = values[1] = values[2] = 1 | BRACHA87_D_FLAG;
    nact = bracha87Fig4Round(fig4, 2, 3, values);
    CHECK(nact == (BRACHA87_DECIDE | BRACHA87_BROADCAST)
          && fig4->decision == 1,
          "split-sample arm: decided 1 at the end of phase 0");

    /* Phases 1 and 2, every sub-round on a 2-1 split carrying a
     * majority of 0 against this decision of 1, and no d-flags at
     * all.  Nothing here may move the decision or the value the
     * process broadcasts, and every suppressed rule would write
     * something else: step 1 the majority 0, step 2 0|D_FLAG, step 3
     * the coin.  Both phases are played because the coin is the one
     * that agrees half the time -- testCoinAlt answers the phase
     * parity, so phase 1 would answer 1 and phase 2 0. */
    values[0] = 0;
    values[1] = 0;
    values[2] = 1;
    for (r = 3; r < 9; ++r) {
      nact = bracha87Fig4Round(fig4, (unsigned char) r, 3, values);
      CHECK(nact == BRACHA87_BROADCAST,
            "split-sample arm: a decided process keeps broadcasting");
      CHECK((fig4->flags & BRACHA87_F4_DECIDED)
            && (fig4->flags & BRACHA87_F4_EXHAUSTED) == 0,
            "split-sample arm: DECIDED holds and EXHAUSTED stays clear");
      CHECK(fig4->decision == 1 && fig4->value == 1,
            "split-sample arm: no camp cannot drift a decided value");
    }

    /* n = 6, t = 1 -- the n = 4t+2 boundary.  A decided process meets
     * a sample of five carrying three (d,1) and two 0: more than 2t
     * flagged, and still no camp above n/2. */
    sz = bracha87Fig4Sz(5, 4);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for the n=6 split arm");
    bracha87Fig4Init(fig4, 5, 1, 4, 0, 0, testCoinAlt, 0);
    values[0] = values[1] = values[2] = values[3] = values[4] = 1;
    (void) bracha87Fig4Round(fig4, 0, 5, values);
    (void) bracha87Fig4Round(fig4, 1, 5, values);
    values[0] = values[1] = values[2] = 1 | BRACHA87_D_FLAG;
    values[3] = values[4] = 0;
    nact = bracha87Fig4Round(fig4, 2, 5, values);
    CHECK(nact == (BRACHA87_DECIDE | BRACHA87_BROADCAST)
          && fig4->decision == 1,
          "n=6 split arm: decided 1 without a base-value majority");
    values[0] = values[1] = values[2] = values[3] = values[4] = 1;
    (void) bracha87Fig4Round(fig4, 3, 5, values);
    (void) bracha87Fig4Round(fig4, 4, 5, values);
    values[0] = values[1] = values[2] = 1 | BRACHA87_D_FLAG;
    values[3] = values[4] = 0;
    nact = bracha87Fig4Round(fig4, 5, 5, values);
    CHECK(nact == BRACHA87_BROADCAST,
          "n=6 split arm: a decided process broadcasts through a second >2t (d,v)");
    CHECK(fig4->decision == 1 && fig4->value == 1
          && (fig4->flags & BRACHA87_F4_EXHAUSTED) == 0,
          "n=6 split arm: the second >2t (d,v) leaves the decision in place");
  }

  /* ---------------------------------------------------------------- */
  BANNER("round-indexed getters refuse a null instance and a round past maxRounds");
  /* ---------------------------------------------------------------- */
  {
    unsigned char *b2, *b3;
    unsigned char s[N_ACT], vv[N_ACT];

    if ((b2 = malloc(bracha87Fig2Sz(N_ENC, 4))) != 0) {
      bracha87Fig2Init((struct bracha87Fig2 *) b2, N_ENC, T_VAL, 4);
      CHECK(bracha87Fig2GetReceived((struct bracha87Fig2 *) b2, 4, s, vv) == 0,
            "Fig2GetReceived refuses round == maxRounds");
      free(b2);
    }
    if ((b3 = malloc(bracha87Fig3Sz(N_ENC, 4))) != 0) {
      bracha87Fig3Init((struct bracha87Fig3 *) b3, N_ENC, T_VAL, 4, testN, 0);
      CHECK(bracha87Fig3GetValid((struct bracha87Fig3 *) b3, 4, s, vv) == 0,
            "Fig3GetValid refuses round == maxRounds");
      CHECK(bracha87Fig3RoundComplete((struct bracha87Fig3 *) b3, 4) == 0,
            "Fig3RoundComplete refuses round == maxRounds");
      CHECK(bracha87Fig3GetValid(0, 0, s, vv) == 0,
            "Fig3GetValid refuses a null instance");
      CHECK(bracha87Fig3RoundComplete(0, 0) == 0,
            "Fig3RoundComplete refuses a null instance");
      free(b3);
    }
  }

  /* ---------------------------------------------------------------- */
  /* Summary                                                          */
  /* ---------------------------------------------------------------- */
  fprintf(stdout, "test_bracha87_blackbox: %d checks, %d failures\n",
          Checks, Failures);
  return (Failures == 0 ? 0 : 1);
}
