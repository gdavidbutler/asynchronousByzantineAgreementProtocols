/*
 * test_bracha87_blackbox.c
 *
 * Black-box test for the public C API in bracha87.h.
 *
 * Tests are derived ONLY from the documented contract in bracha87.h
 * and the Bracha 1987 paper notes (Bracha87.txt).  No part of this
 * file inspects bracha87.c or any other implementation source.
 *
 * Properties exercised:
 *   - Validity      (all honest propose v -> all honest decide v)
 *   - Agreement     (no two honest decide differently for same broadcast)
 *   - Totality      (one honest accepts/decides -> all honest do, modulo
 *                    delivery -- driven by completing message exchange)
 *   - API edges     (Sz/Init contracts, Origin idempotency, dedup,
 *                    BPR replay invariants documented in the header)
 *   - Byzantine     (t < n/3 faulty injecting equivocations / arbitrary
 *                    values cannot break validity or agreement)
 *
 * Header encoding convention (CRITICAL): n parameter is encoded;
 * actual process count = n + 1.  vLen parameter is encoded; actual
 * value length = vLen + 1.  We use:
 *   N_ENC = 3  -> actual cluster of 4 peers
 *   T     = 1  -> max Byzantine; n + 1 > 3t holds (4 > 3)
 *   VLEN_BIN = 0  -> 1-byte values (binary consensus)
 *
 * Style: C89, -pedantic -Wall -Wextra, K&R, 2-space indent.  One
 * monolithic main() per the project's inline-everything convention.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

/* Encoded parameters for a 4-peer cluster (actual N = N_ENC + 1) */
#define N_ENC      3      /* actual peer count = 4 */
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
/* Asynchronous wire-message queue used by all multi-peer simulators. */
/* ------------------------------------------------------------------ */

struct wire {
  unsigned char type;       /* INITIAL/ECHO/READY */
  unsigned char from;
  unsigned char to;
  unsigned char round;
  unsigned char origin;
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
    return 0;
  pick = rngNext() % sz;
  idx = (QHead + pick) % QCAP;
  *out = WireQ[idx];
  --QTail;
  if (idx != (QTail % QCAP))
    WireQ[idx] = WireQ[QTail % QCAP];
  return 1;
}

static int
qPopHead(struct wire *out) {
  if (qSize() == 0)
    return 0;
  *out = WireQ[QHead % QCAP];
  ++QHead;
  return 1;
}

/* Broadcast a Fig1 action from peer 'from' to all 'nAct' peers. */
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
testCoinAlt(void *closure, unsigned char phase)
{
  (void) closure;
  return (unsigned char) (phase & 1);
}

static int
testNAny(void *closure, unsigned char k, unsigned int n_msgs,
         const unsigned char *senders, const unsigned char *values,
         unsigned char *result)
{
  unsigned int c0 = 0, c1 = 0, i;
  (void) closure;
  (void) k;
  (void) senders;
  if (n_msgs == 0) return -1;
  for (i = 0; i < n_msgs; ++i) {
    if ((values[i] & 1) == 0) ++c0;
    else ++c1;
  }
  *result = (c1 > c0) ? 1 : 0;
  return 1; /* permissive: any binary value valid */
}

/* ================================================================== */
/* main                                                               */
/* ================================================================== */

int
main(int argc, char **argv)
{
  unsigned long sz;
  unsigned int i, j, k, r;
  unsigned int act_count;
  unsigned char actions[3];
  unsigned char buf[16];

  static unsigned char fig1Storage[N_ACT][2048];
  struct bracha87Fig1 *peers[N_ACT];
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
                     | BRACHA87_F1_ACCEPTED | BRACHA87_F1_ORIGIN)) == 0,
          "Fig1Init.flags clear");
    CHECK(bracha87Fig1Value(b) == 0, "Fig1Value null pre-origin pre-echo");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1Origin idempotency and value visibility");
  /* ---------------------------------------------------------------- */
  /* Header: "Idempotent: re-calling overwrites the stored value."    */
  /* Origin sets BRACHA87_F1_ORIGIN and the value WITHOUT setting     */
  /* ECHOED.  bracha87Fig1Value returns non-null when ORIGIN ||       */
  /* ECHOED.                                                          */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v1[1] = { 1 };
    static const unsigned char v0[1] = { 0 };
    const unsigned char *got;
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    bracha87Fig1Origin(b, v1);
    CHECK((b->flags & BRACHA87_F1_ORIGIN) != 0, "ORIGIN flag set");
    CHECK((b->flags & BRACHA87_F1_ECHOED) == 0, "ECHOED clear after Origin");
    got = bracha87Fig1Value(b);
    CHECK(got != 0, "Value visible after Origin");
    CHECK(got && got[0] == 1, "Value matches Origin arg");
    bracha87Fig1Origin(b, v0);
    got = bracha87Fig1Value(b);
    CHECK(got && got[0] == 0, "Origin idempotent overwrite");
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
    CHECK(actions[0] == BRACHA87_ECHO_ALL, "Rule1 emits ECHO_ALL");
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
    /* feed echoes from all remaining peers (1..N_ACT-1) */
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
  /* Eventual property: feed enough distinct READYs from all peers,   */
  /* and at some point ACCEPT (Rule 6) fires AND F1_ACCEPTED is set.  */
  /* Action order per header: echo, ready, accept.                    */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 1 };
    int sawAccept = 0;
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    /* Send INITIAL first to fire Rule 1, getting ECHOED set. */
    (void) bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, actions);
    /* Now feed READYs from all distinct peers; at some point Rule 5  */
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
      peers[i] = (struct bracha87Fig1 *) fig1Storage[i];
      bracha87Fig1Init(peers[i], N_ENC, T_VAL, (unsigned char) vEnc);
      accepted[i] = 0;
      memset(acceptedV[i], 0, sizeof (acceptedV[i]));
    }
    bracha87Fig1Origin(peers[0], V);
    broadcastFig1(0, N_ACT, BRACHA87_INITIAL_ALL, V, vBytes);
    while (qPopRandom(&w) && safety++ < 200000) {
      struct bracha87Fig1 *b = peers[w.to];
      act_count = bracha87Fig1Input(b, w.type, w.from, w.value, actions);
      for (i = 0; i < act_count; ++i) {
        unsigned char a = actions[i];
        const unsigned char *cv = bracha87Fig1Value(b);
        if (a == BRACHA87_ECHO_ALL || a == BRACHA87_READY_ALL) {
          CHECK(cv != 0, "Value non-null on ECHO_ALL/READY_ALL");
          broadcastFig1(w.to, N_ACT, a, cv, vBytes);
        } else if (a == BRACHA87_ACCEPT) {
          accepted[w.to] = 1;
          if (cv) memcpy(acceptedV[w.to], cv, vBytes);
        }
      }
    }
    CHECK(safety < 200000, "Lemma4 sim bounded");
    for (i = 0; i < N_ACT; ++i) {
      CHECK(accepted[i], "Lemma4: every honest peer accepts");
      CHECK(memcmp(acceptedV[i], V, vBytes) == 0,
            "Lemma4: every honest peer accepts v_orig");
      CHECK((peers[i]->flags & BRACHA87_F1_ACCEPTED) != 0,
            "F1_ACCEPTED set after accept");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Lemma 3: one peer misses INITIAL");
  /* ---------------------------------------------------------------- */
  /* Originator broadcasts INITIAL to peers 0,1,2 (skipping peer 3).  */
  /* Peer 3 must still ACCEPT V via the echo cascade (Rules 2/3/4/5/6) */
  /* once enough echoes/readys reach it.                              */
  {
    static const unsigned char V[1] = { 0x42 };
    struct wire w;
    unsigned int safety = 0;
    qReset();
    for (i = 0; i < N_ACT; ++i) {
      peers[i] = (struct bracha87Fig1 *) fig1Storage[i];
      bracha87Fig1Init(peers[i], N_ENC, T_VAL, VLEN_BIN);
      accepted[i] = 0;
    }
    bracha87Fig1Origin(peers[0], V);
    /* INITIAL to peers 0,1,2 only (peer 3 misses it) */
    for (i = 0; i < N_ACT - 1; ++i) {
      memset(&w, 0, sizeof (w));
      w.type = BRACHA87_INITIAL;
      w.from = 0;
      w.to = (unsigned char) i;
      w.value[0] = V[0];
      qPush(&w);
    }
    while (qPopRandom(&w) && safety++ < 200000) {
      struct bracha87Fig1 *b = peers[w.to];
      act_count = bracha87Fig1Input(b, w.type, w.from, w.value, actions);
      for (i = 0; i < act_count; ++i) {
        unsigned char a = actions[i];
        const unsigned char *cv = bracha87Fig1Value(b);
        if (a == BRACHA87_ECHO_ALL || a == BRACHA87_READY_ALL)
          broadcastFig1(w.to, N_ACT, a, cv, 1);
        else if (a == BRACHA87_ACCEPT) {
          accepted[w.to] = 1;
          acceptedV[w.to][0] = cv ? cv[0] : 0xFF;
        }
      }
    }
    CHECK(safety < 200000, "Lemma3 sim bounded");
    for (i = 0; i < N_ACT; ++i) {
      CHECK(accepted[i], "Lemma3: every honest peer accepts");
      if (accepted[i])
        CHECK(acceptedV[i][0] == V[0], "Lemma3: accepts the original v");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 Lemma 2: Byzantine equivocation -> agreement holds");
  /* ---------------------------------------------------------------- */
  /* Byzantine peer 0 originates and equivocates: sends (initial,A)   */
  /* to peer 1 and (initial,B) to peer 2; sends mixed echoes/readys   */
  /* with arbitrary values.  Honest peers 1,2,3.  Lemma 1/2 guarantee */
  /* no two honest peers accept different values.  Liveness is not    */
  /* required (split delivery may strand honest peers; safety is      */
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
      peers[i] = (struct bracha87Fig1 *) fig1Storage[i];
      bracha87Fig1Init(peers[i], N_ENC, T_VAL, VLEN_BIN);
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
      b = peers[w.to];
      act_count = bracha87Fig1Input(b, w.type, w.from, w.value, actions);
      if (w.to == 0) continue; /* skip Byzantine peer's outputs */
      for (i = 0; i < act_count; ++i) {
        unsigned char a = actions[i];
        const unsigned char *cv = bracha87Fig1Value(b);
        if (a == BRACHA87_ECHO_ALL || a == BRACHA87_READY_ALL)
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
    /* Even if no honest peer reaches accept, that's still a contract- */
    /* consistent outcome because liveness needs eventual delivery,    */
    /* which equivocation can break for the missing-value direction.   */
    (void) anyAccept; (void) commonV;
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 BPR replay invariants");
  /* ---------------------------------------------------------------- */
  /* Header invariants:                                               */
  /*   ORIGIN       -> Bpr emits INITIAL_ALL                          */
  /*   ECHOED       -> Bpr emits ECHO_ALL                             */
  /*   RDSENT       -> Bpr emits READY_ALL (incl. post-ACCEPT)        */
  /*   no commits   -> Bpr returns 0                                  */
  /*   action order: initial, echo, ready                             */
  {
    struct bracha87Fig1 *b = (struct bracha87Fig1 *) fig1Storage[0];
    static const unsigned char v[1] = { 7 };
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(bracha87Fig1Bpr(b, actions) == 0, "Bpr 0 on fresh");

    bracha87Fig1Origin(b, v);
    act_count = bracha87Fig1Bpr(b, actions);
    CHECK(act_count == 1, "Bpr 1 act when ORIGIN only");
    CHECK(actions[0] == BRACHA87_INITIAL_ALL, "Bpr ORIGIN -> INITIAL_ALL");

    /* Set ECHOED via Rule 1 */
    (void) bracha87Fig1Input(b, BRACHA87_INITIAL, 0, v, buf);
    CHECK((b->flags & BRACHA87_F1_ECHOED) != 0, "ECHOED set");
    act_count = bracha87Fig1Bpr(b, actions);
    CHECK(act_count == 2, "Bpr 2 acts when ORIGIN + ECHOED");
    CHECK(actions[0] == BRACHA87_INITIAL_ALL, "Bpr order: initial first");
    CHECK(actions[1] == BRACHA87_ECHO_ALL,    "Bpr order: echo second");

    /* Drive to RDSENT by feeding all echoes */
    for (i = 1; i < N_ACT; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_ECHO, (unsigned char) i, v, buf);
    CHECK((b->flags & BRACHA87_F1_RDSENT) != 0, "RDSENT set");
    act_count = bracha87Fig1Bpr(b, actions);
    CHECK(act_count == 3, "Bpr 3 acts when ORIGIN+ECHOED+RDSENT");
    CHECK(actions[0] == BRACHA87_INITIAL_ALL, "Bpr order initial");
    CHECK(actions[1] == BRACHA87_ECHO_ALL,    "Bpr order echo");
    CHECK(actions[2] == BRACHA87_READY_ALL,   "Bpr order ready");

    /* Drive to ACCEPTED by feeding readys */
    for (i = 0; i < N_ACT; ++i)
      (void) bracha87Fig1Input(b, BRACHA87_READY, (unsigned char) i, v, buf);
    CHECK((b->flags & BRACHA87_F1_ACCEPTED) != 0, "ACCEPTED");
    /* BPR continues post-ACCEPT (header gap-3 rule). */
    act_count = bracha87Fig1Bpr(b, actions);
    {
      int saw_init = 0, saw_echo = 0, saw_ready = 0;
      for (i = 0; i < act_count; ++i) {
        if (actions[i] == BRACHA87_INITIAL_ALL) saw_init = 1;
        if (actions[i] == BRACHA87_ECHO_ALL)    saw_echo = 1;
        if (actions[i] == BRACHA87_READY_ALL)   saw_ready = 1;
      }
      CHECK(saw_init, "BPR INITIAL post-ACCEPT");
      CHECK(saw_echo, "BPR ECHO post-ACCEPT");
      CHECK(saw_ready, "BPR READY post-ACCEPT");
    }

    /* Non-origin, non-echoed instance: BPR returns 0 */
    bracha87Fig1Init(b, N_ENC, T_VAL, VLEN_BIN);
    CHECK(bracha87Fig1Bpr(b, actions) == 0, "Bpr 0 on fresh non-origin");
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
  BANNER("Fig4 high-level: Lemma 9 (all-honest same start -> decide)");
  /* ---------------------------------------------------------------- */
  /* Lemma 9: "If all correct processes start a phase with the same   */
  /* value v, they all decide v at that phase."  Test with 4 peers,   */
  /* all start at value 0; expect every peer to DECIDE 0.             */
  {
    static unsigned char fig4Buf[N_ACT][32 * 1024];
    /* Per peer, a Fig1 array of size maxRounds * (n+1).  We use   */
    /* maxPhases=4 to keep arrays small for Lemma 9.               */
#define MP_LEMMA9   4
#define MR_LEMMA9   (MP_LEMMA9 * BRACHA87_ROUNDS_PER_PHASE)
    static unsigned char fig1Pool[N_ACT][MR_LEMMA9 * N_ACT][512];
    struct bracha87Fig4 *fig4[N_ACT];
    struct bracha87Fig1 *fig1Arr[N_ACT][MR_LEMMA9 * N_ACT];
    struct bracha87Fig1 *const *fig1Const[N_ACT];
    int decided[N_ACT];
    unsigned char decision[N_ACT];
    int allDecided;
    qReset();
    sz = bracha87Fig4Sz(N_ENC, MP_LEMMA9);
    CHECK(sz <= sizeof (fig4Buf[0]), "fig4Buf size");
    {
      unsigned long sz1 = bracha87Fig1Sz(N_ENC, VLEN_BIN);
      CHECK(sz1 <= sizeof (fig1Pool[0][0]), "fig1Pool slot size");
    }
    for (i = 0; i < N_ACT; ++i) {
      fig4[i] = (struct bracha87Fig4 *) fig4Buf[i];
      bracha87Fig4Init(fig4[i], N_ENC, T_VAL, MP_LEMMA9, 0,
                       testCoinAlt, 0);
      decided[i] = 0;
      decision[i] = 0xFF;
      for (k = 0; k < MR_LEMMA9 * N_ACT; ++k) {
        bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[i][k],
                         N_ENC, T_VAL, VLEN_BIN);
        fig1Arr[i][k] = (struct bracha87Fig1 *) fig1Pool[i][k];
      }
      fig1Const[i] = (struct bracha87Fig1 *const *) fig1Arr[i];
    }
    /* All peers start */
    {
      struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
      for (i = 0; i < N_ACT; ++i) {
        unsigned int nact = bracha87Fig4Start(fig4[i], fig1Const[i],
                                              (unsigned char) i,
                                              actsBuf,
                                              BRACHA87_FIG4_MAX_ACTS);
        CHECK(nact >= 1, "Fig4Start emits act");
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig4Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < N_ACT; ++k) {
              struct wire w;
              memset(&w, 0, sizeof (w));
              w.type = a->type;
              w.from = (unsigned char) i;
              w.to = (unsigned char) k;
              w.round = a->round;
              w.origin = a->origin;
              w.value[0] = a->value;
              qPush(&w);
            }
          }
        }
      }
    }
    /* Drive */
    {
      struct wire w;
      struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
      unsigned int safety = 0;
      while (qPopRandom(&w) && safety++ < 1000000) {
        unsigned int nact;
        if (w.to >= N_ACT) continue;
        nact = bracha87Fig4Input(fig4[w.to], fig1Const[w.to],
                                 w.to, w.round, w.origin,
                                 w.type, w.from, w.value[0],
                                 actsBuf, BRACHA87_FIG4_MAX_ACTS);
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig4Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < N_ACT; ++k) {
              struct wire ww;
              memset(&ww, 0, sizeof (ww));
              ww.type = a->type;
              ww.from = w.to;
              ww.to = (unsigned char) k;
              ww.round = a->round;
              ww.origin = a->origin;
              ww.value[0] = a->value;
              qPush(&ww);
            }
          } else if (a->act == BRACHA87_FIG4_DECIDE) {
            decided[w.to] = 1;
            decision[w.to] = a->decision;
          } else if (a->act == BRACHA87_FIG4_EXHAUSTED) {
            CHECK(0, "Lemma9: EXHAUSTED unexpected");
          }
        }
        {
          int allDone = 1;
          for (i = 0; i < N_ACT; ++i)
            if (!decided[i]) { allDone = 0; break; }
          if (allDone) break;
        }
      }
      CHECK(safety < 1000000, "Lemma9 sim bounded");
    }
    allDecided = 1;
    for (i = 0; i < N_ACT; ++i) {
      if (!decided[i]) allDecided = 0;
      else CHECK(decision[i] == 0, "Lemma9: decide common start value 0");
    }
    CHECK(allDecided, "Lemma9: all honest decide");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4 high-level: agreement under split start values");
  /* ---------------------------------------------------------------- */
  /* Peers 0,1 start at 0; peers 2,3 start at 1.  Theorem 2 requires  */
  /* AGREEMENT (no two honest decide differently); validity is        */
  /* unconstrained because not all honest started with the same v.    */
  {
    static unsigned char fig4Buf[N_ACT][32 * 1024];
#define MP_SPLIT   BRACHA87_MAX_PHASES
#define MR_SPLIT   (MP_SPLIT * 3)
    static unsigned char fig1Pool[N_ACT][MR_SPLIT * N_ACT][512];
    struct bracha87Fig4 *fig4[N_ACT];
    struct bracha87Fig1 *fig1Arr[N_ACT][MR_SPLIT * N_ACT];
    struct bracha87Fig1 *const *fig1Const[N_ACT];
    int decided[N_ACT];
    unsigned char decision[N_ACT];
    unsigned int common = 0xFF;
    int agreed = 1;
    int decideCount = 0;
    qReset();
    for (i = 0; i < N_ACT; ++i) {
      unsigned char start = (i < 2) ? 0 : 1;
      fig4[i] = (struct bracha87Fig4 *) fig4Buf[i];
      bracha87Fig4Init(fig4[i], N_ENC, T_VAL, MP_SPLIT, start,
                       testCoinAlt, 0);
      decided[i] = 0;
      decision[i] = 0xFF;
      for (k = 0; k < MR_SPLIT * N_ACT; ++k) {
        bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[i][k],
                         N_ENC, T_VAL, VLEN_BIN);
        fig1Arr[i][k] = (struct bracha87Fig1 *) fig1Pool[i][k];
      }
      fig1Const[i] = (struct bracha87Fig1 *const *) fig1Arr[i];
    }
    {
      struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
      for (i = 0; i < N_ACT; ++i) {
        unsigned int nact = bracha87Fig4Start(fig4[i], fig1Const[i],
                                              (unsigned char) i,
                                              actsBuf,
                                              BRACHA87_FIG4_MAX_ACTS);
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig4Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < N_ACT; ++k) {
              struct wire w;
              memset(&w, 0, sizeof (w));
              w.type = a->type;
              w.from = (unsigned char) i;
              w.to = (unsigned char) k;
              w.round = a->round;
              w.origin = a->origin;
              w.value[0] = a->value;
              qPush(&w);
            }
          }
        }
      }
    }
    /* FIFO scheduling for split-start */
    {
      struct wire w;
      struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
      unsigned int safety = 0;
      while (qPopHead(&w) && safety++ < 2000000) {
        unsigned int nact;
        if (w.to >= N_ACT) continue;
        nact = bracha87Fig4Input(fig4[w.to], fig1Const[w.to],
                                 w.to, w.round, w.origin,
                                 w.type, w.from, w.value[0],
                                 actsBuf, BRACHA87_FIG4_MAX_ACTS);
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig4Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < N_ACT; ++k) {
              struct wire ww;
              memset(&ww, 0, sizeof (ww));
              ww.type = a->type;
              ww.from = w.to;
              ww.to = (unsigned char) k;
              ww.round = a->round;
              ww.origin = a->origin;
              ww.value[0] = a->value;
              qPush(&ww);
            }
          } else if (a->act == BRACHA87_FIG4_DECIDE) {
            if (decided[w.to] == 0) ++decideCount;
            decided[w.to] = 1;
            decision[w.to] = a->decision;
          } else if (a->act == BRACHA87_FIG4_EXHAUSTED) {
            decided[w.to] = -1;
          }
        }
        {
          int allDone = 1;
          for (i = 0; i < N_ACT; ++i)
            if (decided[i] == 0) { allDone = 0; break; }
          if (allDone) break;
        }
      }
      CHECK(safety < 2000000, "split-start sim bounded");
    }
    for (i = 0; i < N_ACT; ++i) {
      if (decided[i] == 1) {
        if (common == 0xFF) common = decision[i];
        else if (common != decision[i]) agreed = 0;
      }
    }
    CHECK(agreed, "Theorem 2: agreement under split-start");
    CHECK(decideCount >= 1, "at least one peer decides under split-start");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4 high-level: silent Byzantine peer (crash subset)");
  /* ---------------------------------------------------------------- */
  /* n=4 (actual), t=1.  Honest peers 0,1,2 start at 1.  Byzantine    */
  /* peer 3 is silent (a crash-failure is a strict subset of          */
  /* Byzantine behaviour and is the strongest case where Lemma 9     */
  /* applies cleanly: all CORRECT processes started with v=1, so     */
  /* they must all decide 1).                                          */
  /*                                                                  */
  /* We do NOT inject random Byzantine garbage here for two reasons: */
  /*   (1) the bracha87.h-level wire framing isn't promised to be     */
  /*       robust against malformed messages -- that's the chanBlb /  */
  /*       chanBlbChnRsec layer's job in deployment;                 */
  /*   (2) the test goal is decided-value safety + validity under     */
  /*       up to t < n/3 absent peers, which is the worst-case crash  */
  /*       subset of Byzantine.                                       */
  /*                                                                  */
  /* SPEC GAP (reported): bracha87.h doesn't specify whether          */
  /* application is responsible for filtering well-formedness of     */
  /* incoming messages (e.g., D_FLAG bit on a round != 3i+2).         */
  {
    static unsigned char fig4Buf[N_ACT][32 * 1024];
#define MP_BYZ   BRACHA87_MAX_PHASES
#define MR_BYZ   (MP_BYZ * 3)
    static unsigned char fig1Pool[N_ACT][MR_BYZ * N_ACT][512];
    struct bracha87Fig4 *fig4[N_ACT];
    struct bracha87Fig1 *fig1Arr[N_ACT][MR_BYZ * N_ACT];
    struct bracha87Fig1 *const *fig1Const[N_ACT];
    int decided[N_ACT];
    unsigned char decision[N_ACT];
    int honest = N_ACT - 1; /* peers 0..2 honest; peer 3 Byzantine */
    qReset();
    for (i = 0; i < N_ACT; ++i) {
      fig4[i] = (struct bracha87Fig4 *) fig4Buf[i];
      bracha87Fig4Init(fig4[i], N_ENC, T_VAL, MP_BYZ, 1,
                       testCoinAlt, 0);
      decided[i] = 0;
      decision[i] = 0xFF;
      for (k = 0; k < MR_BYZ * N_ACT; ++k) {
        bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[i][k],
                         N_ENC, T_VAL, VLEN_BIN);
        fig1Arr[i][k] = (struct bracha87Fig1 *) fig1Pool[i][k];
      }
      fig1Const[i] = (struct bracha87Fig1 *const *) fig1Arr[i];
    }
    /* Honest peers start (broadcast to ALL including the Byzantine  */
    /* peer 3 -- our simulator ignores 3's state since 3 sends only   */
    /* injected garbage and we don't simulate its Fig4 evolution).    */
    {
      struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
      for (i = 0; i < (unsigned int) honest; ++i) {
        unsigned int nact = bracha87Fig4Start(fig4[i], fig1Const[i],
                                              (unsigned char) i,
                                              actsBuf,
                                              BRACHA87_FIG4_MAX_ACTS);
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig4Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            /* Broadcast only to honest peers */
            for (k = 0; k < (unsigned int) honest; ++k) {
              struct wire w;
              memset(&w, 0, sizeof (w));
              w.type = a->type;
              w.from = (unsigned char) i;
              w.to = (unsigned char) k;
              w.round = a->round;
              w.origin = a->origin;
              w.value[0] = a->value;
              qPush(&w);
            }
          }
        }
      }
    }
    /* Byzantine peer 3 is silent: no garbage injection. */
    {
      struct wire w;
      struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
      unsigned int safety = 0;
      while (qPopRandom(&w) && safety++ < 2000000) {
        unsigned int nact;
        if ((unsigned int) w.to >= (unsigned int) honest) continue;
        nact = bracha87Fig4Input(fig4[w.to], fig1Const[w.to],
                                 w.to, w.round, w.origin,
                                 w.type, w.from, w.value[0],
                                 actsBuf, BRACHA87_FIG4_MAX_ACTS);
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig4Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < (unsigned int) honest; ++k) {
              struct wire ww;
              memset(&ww, 0, sizeof (ww));
              ww.type = a->type;
              ww.from = w.to;
              ww.to = (unsigned char) k;
              ww.round = a->round;
              ww.origin = a->origin;
              ww.value[0] = a->value;
              qPush(&ww);
            }
          } else if (a->act == BRACHA87_FIG4_DECIDE) {
            decided[w.to] = 1;
            decision[w.to] = a->decision;
          } else if (a->act == BRACHA87_FIG4_EXHAUSTED) {
            decided[w.to] = -1;
          }
        }
        {
          int allDone = 1;
          for (i = 0; i < (unsigned int) honest; ++i)
            if (decided[i] == 0) { allDone = 0; break; }
          if (allDone) break;
        }
      }
      CHECK(safety < 2000000, "Byz sim bounded");
    }
    for (i = 0; i < (unsigned int) honest; ++i) {
      CHECK(decided[i] == 1, "Byz: honest decided");
      if (decided[i] == 1)
        CHECK(decision[i] == 1, "Byz: validity (all-honest start=1)");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig1 high-level Pump cursor / CommittedCount / NULL slots");
  /* ---------------------------------------------------------------- */
  /* Header: PumpInit + Fig1PumpStep walk the array, returning one    */
  /* instance's BPR per call.  NULL entries skipped.  CommittedCount  */
  /* counts ORIGIN | ECHOED | RDSENT slots.  Idle full sweep returns  */
  /* 0.                                                               */
  {
    static unsigned char pool[8][512];
    struct bracha87Fig1 *arr[8];
    struct bracha87Pump pump;
    struct bracha87Fig1Act outActs[BRACHA87_FIG1_PUMP_MAX_ACTS];
    static const unsigned char v[1] = { 0x42 };
    unsigned int got;
    sz = bracha87Fig1Sz(N_ENC, VLEN_BIN);
    CHECK(sz <= sizeof (pool[0]), "pump pool slot ok");
    /* slots 0..3 used; 4..7 NULL */
    for (i = 0; i < 4; ++i) {
      arr[i] = (struct bracha87Fig1 *) pool[i];
      bracha87Fig1Init(arr[i], N_ENC, T_VAL, VLEN_BIN);
    }
    for (i = 4; i < 8; ++i) arr[i] = 0;
    bracha87Fig1Origin(arr[1], v);
    (void) bracha87Fig1Input(arr[2], BRACHA87_INITIAL, 0, v, buf);
    CHECK(bracha87Fig1CommittedCount((struct bracha87Fig1 *const *) arr, 8)
            == 2, "CommittedCount = 2 (origin + echoed)");
    bracha87PumpInit(&pump);
    {
      int saw_init = 0, saw_echo = 0;
      for (r = 0; r < 16; ++r) {
        got = bracha87Fig1PumpStep((struct bracha87Fig1 *const *) arr, 8,
                                   &pump, outActs,
                                   BRACHA87_FIG1_PUMP_MAX_ACTS);
        if (got == 0) continue;
        for (i = 0; i < got; ++i) {
          if (outActs[i].act == BRACHA87_INITIAL_ALL) saw_init = 1;
          if (outActs[i].act == BRACHA87_ECHO_ALL) saw_echo = 1;
          CHECK(outActs[i].idx < 8, "PumpStep idx in range");
          CHECK(arr[outActs[i].idx] != 0, "PumpStep skips NULL slots");
        }
      }
      CHECK(saw_init, "PumpStep emits INITIAL_ALL for ORIGIN slot");
      CHECK(saw_echo, "PumpStep emits ECHO_ALL for ECHOED slot");
    }
    /* Idle sweep: all instances fresh, no commits -> a full sweep    */
    /* returns 0 at least once.                                        */
    {
      struct bracha87Fig1 *idleArr[8];
      int sawZero = 0;
      for (i = 0; i < 8; ++i) {
        bracha87Fig1Init((struct bracha87Fig1 *) pool[i],
                         N_ENC, T_VAL, VLEN_BIN);
        idleArr[i] = (struct bracha87Fig1 *) pool[i];
      }
      bracha87PumpInit(&pump);
      for (r = 0; r < 32; ++r) {
        got = bracha87Fig1PumpStep((struct bracha87Fig1 *const *) idleArr, 8,
                                   &pump, outActs,
                                   BRACHA87_FIG1_PUMP_MAX_ACTS);
        if (got == 0) { sawZero = 1; break; }
      }
      CHECK(sawZero, "PumpStep returns 0 on idle full sweep");
      CHECK(bracha87Fig1CommittedCount((struct bracha87Fig1 *const *) idleArr,
                                       8) == 0,
            "CommittedCount = 0 when idle");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig3 high-level: Origin + Input ladder + ROUND_COMPLETE");
  /* ---------------------------------------------------------------- */
  /* Each peer Origins for (round=0, origin=self) with its own value, */
  /* drives the Fig 1 + Fig 3 ladder; expects ROUND_COMPLETE to       */
  /* surface for round 0 once n-t Fig 1 ACCEPTs have validated.       */
  {
    static unsigned char fig3Buf[N_ACT][16 * 1024];
    static unsigned char fig1Pool[N_ACT][N_ACT][512];
    struct bracha87Fig3 *fig3[N_ACT];
    struct bracha87Fig1 *fig1Arr[N_ACT][N_ACT];
    struct bracha87Fig1 *const *fig1Const[N_ACT];
    int sawRC[N_ACT];
    sz = bracha87Fig3Sz(N_ENC, 1);
    CHECK(sz <= sizeof (fig3Buf[0]), "fig3Buf big enough");
    qReset();
    for (i = 0; i < N_ACT; ++i) {
      fig3[i] = (struct bracha87Fig3 *) fig3Buf[i];
      bracha87Fig3Init(fig3[i], N_ENC, T_VAL, 1, testNAny, 0);
      sawRC[i] = 0;
      for (k = 0; k < N_ACT; ++k) {
        bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[i][k],
                         N_ENC, T_VAL, VLEN_BIN);
        fig1Arr[i][k] = (struct bracha87Fig1 *) fig1Pool[i][k];
      }
      fig1Const[i] = (struct bracha87Fig1 *const *) fig1Arr[i];
    }
    {
      struct bracha87Fig3Act actsBuf[BRACHA87_FIG3_MAX_ACTS];
      for (i = 0; i < N_ACT; ++i) {
        unsigned char vv = (unsigned char) (i & 1);
        unsigned int nact = bracha87Fig3Origin(fig3[i], fig1Const[i],
                                               0, (unsigned char) i,
                                               &vv, actsBuf,
                                               BRACHA87_FIG3_MAX_ACTS);
        CHECK(nact >= 1, "Fig3Origin emits >= 1 act");
        CHECK(actsBuf[0].act == BRACHA87_INITIAL_ALL,
              "Fig3Origin first act = INITIAL_ALL");
        CHECK(actsBuf[0].origin == (unsigned char) i, "origin tag = self");
        CHECK(actsBuf[0].round == 0, "round tag = 0");
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig3Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < N_ACT; ++k) {
              struct wire w;
              memset(&w, 0, sizeof (w));
              w.type = a->type;
              w.from = (unsigned char) i;
              w.to = (unsigned char) k;
              w.round = a->round;
              w.origin = a->origin;
              w.value[0] = a->value ? a->value[0] : 0;
              qPush(&w);
            }
          }
        }
      }
    }
    {
      struct wire w;
      struct bracha87Fig3Act actsBuf[BRACHA87_FIG3_MAX_ACTS];
      unsigned int safety = 0;
      while (qPopRandom(&w) && safety++ < 500000) {
        unsigned char vv = w.value[0];
        unsigned int nact;
        if (w.to >= N_ACT) continue;
        nact = bracha87Fig3Input(fig3[w.to], fig1Const[w.to],
                                 w.round, w.origin, w.type, w.from,
                                 &vv, actsBuf, BRACHA87_FIG3_MAX_ACTS);
        for (j = 0; j < nact; ++j) {
          struct bracha87Fig3Act *a = &actsBuf[j];
          if (a->act == BRACHA87_INITIAL_ALL || a->act == BRACHA87_ECHO_ALL
              || a->act == BRACHA87_READY_ALL) {
            for (k = 0; k < N_ACT; ++k) {
              struct wire ww;
              memset(&ww, 0, sizeof (ww));
              ww.type = a->type;
              ww.from = w.to;
              ww.to = (unsigned char) k;
              ww.round = a->round;
              ww.origin = a->origin;
              ww.value[0] = a->value ? a->value[0] : 0;
              qPush(&ww);
            }
          } else if (a->act == BRACHA87_FIG3_ROUND_COMPLETE) {
            sawRC[w.to] = 1;
            CHECK(a->round == 0, "Fig3 ROUND_COMPLETE round = 0");
          }
        }
      }
      CHECK(safety < 500000, "Fig3 sim bounded");
    }
    for (i = 0; i < N_ACT; ++i) {
      CHECK(sawRC[i], "Fig3: ROUND_COMPLETE surfaced");
      CHECK(bracha87Fig3RoundComplete(fig3[i], 0),
            "Fig3RoundComplete query reflects state");
      CHECK(bracha87Fig3ValidCount(fig3[i], 0) >= (N_ACT - T_VAL),
            "Fig3ValidCount >= n-t");
    }
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
  BANNER("Fig3Origin exact emission tuple");
  /* ---------------------------------------------------------------- */
  /* Header now pins:                                                 */
  /*   act = INITIAL_ALL, type = INITIAL, (origin, round) as supplied */
  /*   value points into Fig1 instance's committed-value slot         */
  /* Verify the value pointer aliases the Fig1's stored value (i.e.,  */
  /* the same byte read via bracha87Fig1Value on the underlying slot. */
  {
    static unsigned char fig3Buf[16 * 1024];
    static unsigned char fig1Pool[N_ACT][512];
    struct bracha87Fig3 *fig3 = (struct bracha87Fig3 *) fig3Buf;
    struct bracha87Fig1 *fig1Arr[N_ACT];
    struct bracha87Fig3Act actsBuf[BRACHA87_FIG3_MAX_ACTS];
    static const unsigned char vv[1] = { 0xCB };
    unsigned char origin = 2;
    unsigned char round = 0;
    unsigned int nact;
    sz = bracha87Fig3Sz(N_ENC, 1);
    CHECK(sz <= sizeof (fig3Buf), "fig3Buf size for emit tuple");
    bracha87Fig3Init(fig3, N_ENC, T_VAL, 1, testNAny, 0);
    for (k = 0; k < N_ACT; ++k) {
      bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[k],
                       N_ENC, T_VAL, VLEN_BIN);
      fig1Arr[k] = (struct bracha87Fig1 *) fig1Pool[k];
    }
    nact = bracha87Fig3Origin(fig3,
                              (struct bracha87Fig1 *const *) fig1Arr,
                              round, origin, vv, actsBuf,
                              BRACHA87_FIG3_MAX_ACTS);
    CHECK(nact == 1, "Fig3Origin emits exactly 1 act");
    CHECK(actsBuf[0].act == BRACHA87_INITIAL_ALL,
          "Fig3Origin act = INITIAL_ALL");
    CHECK(actsBuf[0].type == BRACHA87_INITIAL,
          "Fig3Origin type = INITIAL");
    CHECK(actsBuf[0].origin == origin, "Fig3Origin origin tag");
    CHECK(actsBuf[0].round == round, "Fig3Origin round tag");
    CHECK(actsBuf[0].value != 0, "Fig3Origin value pointer not null");
    CHECK(actsBuf[0].value && actsBuf[0].value[0] == 0xCB,
          "Fig3Origin value byte matches");
    /* Header: "value pointing into the Fig 1 instance's committed-   */
    /* value slot."  The Fig 1 indexed by round*(n+1)+origin should   */
    /* return the same byte via Fig1Value.                            */
    {
      unsigned int idx = (unsigned int) round * (N_ACT) + origin;
      const unsigned char *fv = bracha87Fig1Value(fig1Arr[idx]);
      CHECK(fv != 0, "underlying Fig1Value visible after Origin");
      CHECK(fv && fv[0] == 0xCB, "underlying Fig1Value matches");
      CHECK((fig1Arr[idx]->flags & BRACHA87_F1_ORIGIN) != 0,
            "underlying Fig1 has F1_ORIGIN set");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4Start exact emission tuple");
  /* ---------------------------------------------------------------- */
  /* Header pins: act = INITIAL_ALL, type = INITIAL, origin = self,    */
  /* round = 0, value = initialValue.                                 */
  {
    static unsigned char fig4Buf[32 * 1024];
    static unsigned char fig1Pool[3 * N_ACT][512];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    struct bracha87Fig1 *fig1Arr[3 * N_ACT];
    struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
    unsigned char self = 1;
    unsigned char initialValue = 1;
    unsigned int nact;
    sz = bracha87Fig4Sz(N_ENC, 1);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for Start emit");
    bracha87Fig4Init(fig4, N_ENC, T_VAL, 1, initialValue, testCoinAlt, 0);
    for (k = 0; k < 3 * N_ACT; ++k) {
      bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[k],
                       N_ENC, T_VAL, VLEN_BIN);
      fig1Arr[k] = (struct bracha87Fig1 *) fig1Pool[k];
    }
    nact = bracha87Fig4Start(fig4,
                             (struct bracha87Fig1 *const *) fig1Arr,
                             self, actsBuf, BRACHA87_FIG4_MAX_ACTS);
    CHECK(nact == 1, "Fig4Start emits exactly 1 act");
    CHECK(actsBuf[0].act == BRACHA87_INITIAL_ALL,
          "Fig4Start act = INITIAL_ALL");
    CHECK(actsBuf[0].type == BRACHA87_INITIAL,
          "Fig4Start type = INITIAL");
    CHECK(actsBuf[0].origin == self, "Fig4Start origin = self");
    CHECK(actsBuf[0].round == 0, "Fig4Start round = 0");
    CHECK(actsBuf[0].value == initialValue,
          "Fig4Start value = initialValue from Init");
    /* Underlying Fig1[0*(n+1)+self] is marked ORIGIN */
    {
      unsigned int idx = 0 * N_ACT + self;
      CHECK((fig1Arr[idx]->flags & BRACHA87_F1_ORIGIN) != 0,
            "Fig4Start sets F1_ORIGIN on (round=0, self)");
    }
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4 malformed-value filtering via high-level Input");
  /* ---------------------------------------------------------------- */
  /* Header (Fig4Round doc) now states:                               */
  /*   "malformed Byzantine values are filtered structurally:         */
  /*    fig3IsValid via fig4Nfn rejects any value outside             */
  /*    {0, 1, D_FLAG|0, D_FLAG|1} appropriate for the round, so they */
  /*    are non-events that do not advance phase."                    */
  /* The filtering is at the Fig 3 validation predicate.  The high-   */
  /* level bracha87Fig4Input drives Fig1Input -> Fig3Accept           */
  /* (validation) -> Fig4Round (only on round-complete).              */
  /*                                                                  */
  /* Setup: n=4 t=1, maxPhases=2.  Drive round-0 traffic where every  */
  /* peer's broadcast carries an illegal D_FLAG bit.  Header invariant:*/
  /* none of these can validate so no ROUND_COMPLETE for round 0,    */
  /* hence no Fig4Round call, hence phase/subRound stay at the start. */
  {
    static unsigned char fig4Buf[32 * 1024];
    static unsigned char fig1Pool[2 * 3 * N_ACT][512];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    struct bracha87Fig1 *fig1Arr[2 * 3 * N_ACT];
    struct bracha87Fig4Act actsBuf[BRACHA87_FIG4_MAX_ACTS];
    unsigned char self = 0;
    unsigned int nact;
    sz = bracha87Fig4Sz(N_ENC, 2);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for malformed test");
    bracha87Fig4Init(fig4, N_ENC, T_VAL, 2, 0, testCoinAlt, 0);
    for (k = 0; k < 2 * 3 * N_ACT; ++k) {
      bracha87Fig1Init((struct bracha87Fig1 *) fig1Pool[k],
                       N_ENC, T_VAL, VLEN_BIN);
      fig1Arr[k] = (struct bracha87Fig1 *) fig1Pool[k];
    }
    /* Drive: for every (origin, sender) pair, deliver an INITIAL on  */
    /* round 0 with an ILLEGAL value (D_FLAG bit set; only 0/1 are    */
    /* legal at sub=0).  We deliver enough copies to cause ACCEPT     */
    /* (Rule 1 -> Rule 6 cascade) at the Fig 1 layer but at the Fig 3 */
    /* validate step the value should be rejected.                    */
    for (i = 0; i < N_ACT; ++i) {       /* origin */
      /* feed (initial, BAD) to self from origin i */
      (void) bracha87Fig4Input(fig4,
                               (struct bracha87Fig1 *const *) fig1Arr,
                               self, 0, (unsigned char) i,
                               BRACHA87_INITIAL, (unsigned char) i,
                               BRACHA87_D_FLAG, /* illegal at round 0 */
                               actsBuf, BRACHA87_FIG4_MAX_ACTS);
      /* feed echoes from all other senders */
      for (j = 0; j < N_ACT; ++j) {
        if (j == i) continue;
        (void) bracha87Fig4Input(fig4,
                                 (struct bracha87Fig1 *const *) fig1Arr,
                                 self, 0, (unsigned char) i,
                                 BRACHA87_ECHO, (unsigned char) j,
                                 BRACHA87_D_FLAG,
                                 actsBuf, BRACHA87_FIG4_MAX_ACTS);
      }
      /* feed readys from all senders */
      for (j = 0; j < N_ACT; ++j) {
        nact = bracha87Fig4Input(fig4,
                                 (struct bracha87Fig1 *const *) fig1Arr,
                                 self, 0, (unsigned char) i,
                                 BRACHA87_READY, (unsigned char) j,
                                 BRACHA87_D_FLAG,
                                 actsBuf, BRACHA87_FIG4_MAX_ACTS);
        for (r = 0; r < nact; ++r)
          if (actsBuf[r].act == BRACHA87_FIG4_DECIDE) {
            CHECK(0, "malformed: must not DECIDE on illegal value");
          }
      }
    }
    /* After feeding all malformed traffic for round 0, phase/subRound */
    /* MUST still be at the initial state (phase 0, subRound 0).      */
    CHECK(fig4->phase == 0,
          "malformed-only: Fig4 phase remains 0");
    CHECK(fig4->subRound == 0,
          "malformed-only: Fig4 subRound remains 0");
    CHECK(!(fig4->flags & BRACHA87_F4_DECIDED), "malformed-only: not decided");
    /* No Fig 3 round 0 should be complete because validation rejected*/
    /* every malformed message.                                        */
    CHECK(!bracha87Fig3RoundComplete(&fig4->fig3, 0),
          "malformed-only: Fig 3 round 0 NOT complete");
    CHECK(bracha87Fig3ValidCount(&fig4->fig3, 0) == 0,
          "malformed-only: Fig 3 validCount(0) == 0");
  }

  /* ---------------------------------------------------------------- */
  BANNER("Fig4Round post-EXHAUSTED safety");
  /* ---------------------------------------------------------------- */
  /* Header: subsequent calls to bracha87Fig4Round on EXHAUSTED       */
  /* return 0 actions; the state machine remains in EXHAUSTED.        */
  /*                                                                  */
  /* Drive an instance to EXHAUSTED with maxPhases=1 so the schedule  */
  /* is short.  At sub=2 of the last phase, with no decision and no   */
  /* >t (d,v) majority, EXHAUSTED is emitted (header).                */
  /*                                                                  */
  /* Strategy: run 3 rounds (0, 1, 2) with split values that produce  */
  /* no >2t (d,v) and no >t (d,v) majority -- specifically, give each */
  /* round n-t messages with values lacking any D_FLAG, so the         */
  /* decideV / setVMajority paths cannot fire and the coin path runs. */
  /* At sub=2 of phase 0 (the only phase with maxPhases=1), no        */
  /* decision means EXHAUSTED.                                        */
  {
    static unsigned char fig4Buf[32 * 1024];
    struct bracha87Fig4 *fig4 = (struct bracha87Fig4 *) fig4Buf;
    unsigned char senders[N_ACT];
    unsigned char values[N_ACT];
    unsigned int nact;
    int sawExhausted = 0;
    sz = bracha87Fig4Sz(N_ENC, 1);
    CHECK(sz <= sizeof (fig4Buf), "fig4Buf size for EXHAUSTED test");
    bracha87Fig4Init(fig4, N_ENC, T_VAL, 1, 0, testCoinAlt, 0);
    /* Round 0: 3 messages (n-t=3), no D_FLAG legal here.             */
    senders[0] = 0; values[0] = 0;
    senders[1] = 1; values[1] = 1;
    senders[2] = 2; values[2] = 0;
    nact = bracha87Fig4Round(fig4, 0, 3, senders, values);
    /* Should advance to next sub-round; expect BROADCAST */
    {
      unsigned int hadBroadcast = 0;
      if (nact & BRACHA87_BROADCAST) hadBroadcast = 1;
      CHECK(hadBroadcast, "round 0 advances to broadcast");
    }
    /* Round 1: legal values are {0, 1, D_FLAG|0, D_FLAG|1}.  Use no- */
    /* D_FLAG mixed values so no >n/2 majority sets D_FLAG.            */
    senders[0] = 0; values[0] = 0;
    senders[1] = 1; values[1] = 1;
    senders[2] = 2; values[2] = 0;
    nact = bracha87Fig4Round(fig4, 1, 3, senders, values);
    {
      unsigned int hadBroadcast = 0;
      if (nact & BRACHA87_BROADCAST) hadBroadcast = 1;
      CHECK(hadBroadcast, "round 1 advances to broadcast");
    }
    /* Round 2 (sub=2 of phase 0): no D_FLAG -> no decideV / no >t    */
    /* majority -> coin, then EXHAUSTED because phase 0 was last.     */
    senders[0] = 0; values[0] = 0;
    senders[1] = 1; values[1] = 1;
    senders[2] = 2; values[2] = 0;
    nact = bracha87Fig4Round(fig4, 2, 3, senders, values);
    if (nact & BRACHA87_EXHAUSTED) sawExhausted = 1;
    CHECK(sawExhausted, "EXHAUSTED emitted at sub=2 of last phase");
    CHECK((nact & BRACHA87_DECIDE) == 0,
          "EXHAUSTED mutually exclusive with DECIDE");
    /* Subsequent calls must return 0 and remain EXHAUSTED -- header  */
    /* states: "Subsequent calls to bracha87Fig4Round on an EXHAUSTED */
    /* instance are safe and return 0 actions; the state machine      */
    /* remains in EXHAUSTED" AND "BRACHA87_EXHAUSTED is also returned */
    /* at most once."                                                 */
    nact = bracha87Fig4Round(fig4, 2, 3, senders, values);
    CHECK(nact == 0, "post-EXHAUSTED Round(2) returns 0");
    /* Try other rounds too -- still 0 */
    nact = bracha87Fig4Round(fig4, 0, 3, senders, values);
    CHECK(nact == 0, "post-EXHAUSTED Round(round=0) returns 0");
    nact = bracha87Fig4Round(fig4, 1, 3, senders, values);
    CHECK(nact == 0, "post-EXHAUSTED Round(round=1) returns 0");
    /* No DECIDE, no decision recorded */
    CHECK(!(fig4->flags & BRACHA87_F4_DECIDED), "post-EXHAUSTED decided flag still 0");
  }

  /* ---------------------------------------------------------------- */
  /* Summary                                                          */
  /* ---------------------------------------------------------------- */
  fprintf(stdout, "test_bracha87_blackbox: %d checks, %d failures\n",
          Checks, Failures);
  return Failures == 0 ? 0 : 1;
}
