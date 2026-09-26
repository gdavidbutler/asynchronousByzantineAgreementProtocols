/*
 * test/test_predicates.c -- subset-enumeration correspondence tests for the
 * three algorithmic predicates that sit below the .dtc layer in
 * bracha87.c: fig4Nfn (the protocol function N), fig3IsValid (the
 * VALID^k predicate), and the cascade in bracha87Fig3Accept; and the
 * site independence of the two dispatches that more than one C site
 * reaches (bracha87Fig1Rules.c, bkr94acsRules.c).
 *
 * Audit chain:
 *   paper            <-> .dtc                   human, rule-by-rule comments
 *   .dtc              -> compiled dispatch      dtc, exhaustive/exclusive
 *   C wrapper boundary I/O                      human inspection
 *   fig3IsValid, fig4Nfn, cascade               THIS FILE -- exhaustive
 *                                               enumeration vs a subset-
 *                                               enumeration reference at
 *                                               bounded inputs
 *   one dispatch, many sites                    THIS FILE -- every input
 *                                               combination of each snippet,
 *                                               each site's reads constant
 *                                               over what it feeds by fiat
 *
 * Scope: n=4, t=1 (smallest interesting Bracha config); well under a
 * second.
 *
 * White-box: this file #include's bracha87.c and bkr94acs.c directly so
 * the file-local fig3IsValid and fig4Nfn and the dispatch snippets'
 * file-local constants are visible.  Built without the .o files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../bracha87.c"
#include "../bkr94acs.c"

#define NN 4
#define TT 1
#define NT (NN - TT)

static unsigned int Pass;
static unsigned int Fail;

static void
check(const char *what, int cond) {
  if (cond) ++Pass;
  else { ++Fail; printf("FAIL: %s\n", what); }
}

/* ----------------------------------------------------------------
 * fig4N reference, by explicit n-t subset enumeration.  It applies the
 * paper's per-subset rule under the library's two conventions -- the
 * step-1 tie broken to 0, and the D_FLAG permission encoding -- so it
 * is a second derivation, not the paper itself.
 *
 * Sub-round s of round k = 3i + s (0-based here; the paper's step
 * s+1 at its 1-based round 3i+s+1) applies one of three rules to
 * its n-t input messages:
 *
 *   s=0: result := majority of the n-t base values (tie -> 0).
 *   s=1: result := v|D_FLAG iff strictly more than half of n
 *                  inputs carry the same v; else "unchanged"
 *                  (each process keeps its prior value).
 *   s=2: result := dm iff strictly more than t inputs carry
 *                  (d, dm) for the same dm -- step 3 (i) decides dm
 *                  above 2t and (ii) adopts dm above t, and both
 *                  broadcast dm; else "coin".
 *
 * "Unchanged"/"coin" are non-deterministic across processes
 * and are reported as a "no exact result for this
 * subset" via a sentinel (rc < 0 from refSubset).
 *
 * fig4Nfn's contract:
 *   rc=0  : *result is the unique value any n-t subset would give.
 *   rc>0  : different n-t subsets could give different results;
 *           *result encodes D_FLAG-permission as the convention.
 *           - sub=1: bit set iff some subset can produce v|D_FLAG.
 *           - sub=2: bit clear (output never carries D_FLAG).
 *   rc<0  : input degenerate.
 *
 * The reference computes rc and *result by explicit subset
 * enumeration; fig4Nfn computes them analytically.  They must agree.
 * ---------------------------------------------------------------- */

/* Apply the per-subset rule.  Returns 0 + *r when the subset has
 * a deterministic paper output, or -1 when the rule is "unchanged"
 * (sub=1 no-majority) / "coin" (sub=2, no more than t (d, dm)). */
static int
refSubset(
  unsigned int sub
 ,unsigned int n_actual
 ,unsigned int t
 ,const unsigned char *vals
 ,unsigned int sz
 ,unsigned char *r
){
  unsigned int cnt[2];
  unsigned int dc[2];
  unsigned int i;
  unsigned int dm;

  cnt[0] = cnt[1] = 0;
  dc[0] = dc[1] = 0;
  for (i = 0; i < sz; ++i) {
    unsigned char v = vals[i] & (unsigned char)~BRACHA87_D_FLAG;
    if (v <= 1) {
      ++cnt[v];
      if (vals[i] & BRACHA87_D_FLAG)
        ++dc[v];
    }
  }

  switch (sub) {
  case 0:
    *r = (cnt[1] > cnt[0]) ? 1 : 0;
    return (0);
  case 1:
    if (cnt[0] * 2 > n_actual) { *r = 0 | BRACHA87_D_FLAG; return (0); }
    if (cnt[1] * 2 > n_actual) { *r = 1 | BRACHA87_D_FLAG; return (0); }
    return (-1); /* "unchanged" -- non-deterministic */
  case 2:
    dm = (dc[1] > dc[0]) ? 1 : 0;
    if (dc[dm] > t) { *r = (unsigned char)dm; return (0); }
    return (-1); /* "coin" -- non-deterministic */
  }
  return (-1);
}

/* Lex-next combination of size r from [0,n).  Returns 1 if generated,
 * 0 when exhausted. */
static int
nextCombo(unsigned int *idx, unsigned int r, unsigned int n) {
  unsigned int i;

  i = r;
  while (i > 0) {
    --i;
    if (idx[i] < n - (r - i)) {
      ++idx[i];
      while (++i < r)
        idx[i] = idx[i - 1] + 1;
      return (1);
    }
  }
  return (0);
}

/* Reference fig4N: enumerate every n-t subset of input, apply paper
 * rule, aggregate.  Sets *result and returns rc per fig4Nfn's contract. */
static int
fig4NRef(
  unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *values
 ,unsigned int n_actual
 ,unsigned int t
 ,unsigned char *result
){
  unsigned int sub;
  unsigned int nt;
  unsigned int idx[NN];
  unsigned char sv[NN];
  unsigned int i;
  int seenDeterministic;
  int seenNonDeterministic;
  int allSameDet;
  unsigned char detR;

  if (!n_msgs) return (-1);

  sub = (unsigned int)(k % 3);
  nt = n_actual - t;
  if (n_msgs < nt) return (-1);

  for (i = 0; i < nt; ++i) idx[i] = i;

  seenDeterministic = 0;
  seenNonDeterministic = 0;
  allSameDet = 1;
  detR = 0;

  do {
    int rc;
    unsigned char r;
    for (i = 0; i < nt; ++i) sv[i] = values[idx[i]];
    rc = refSubset(sub, n_actual, t, sv, nt, &r);
    if (rc == 0) {
      if (!seenDeterministic) { detR = r; seenDeterministic = 1; }
      else if (r != detR) allSameDet = 0;
    } else {
      seenNonDeterministic = 1;
    }
  } while (nextCombo(idx, nt, n_msgs));

  /* Aggregation per fig4Nfn's convention. */
  if (seenDeterministic && !seenNonDeterministic && allSameDet) {
    *result = detR;
    return (0); /* exact */
  }

  /* Permissive: encode D_FLAG permission per sub-round convention. */
  if (sub == 1) {
    /* Some subset reachable D_FLAG with base detR (if any deterministic
     * fired); D_FLAG is legitimate iff some subset produces v|D_FLAG. */
    if (seenDeterministic) {
      *result = detR; /* carries D_FLAG bit if a v|D_FLAG was reachable */
      return (1);
    }
    *result = 0; /* no majority anywhere; D_FLAG not legitimate */
    return (1);
  }
  /* sub=0 permissive: both 0 and 1 reachable; fig4Nfn writes the
   * tie-break-to-0 result and returns 1.  *result has no D_FLAG. */
  if (sub == 0) {
    *result = (unsigned char)((seenDeterministic && detR == 1) ? 1 : 0);
    /* fig4Nfn writes tie-break-to-0 path's value: result = (cnt[1] > cnt[0]) */
    /* but we report what the reference saw deterministically; fig4Nfn
     * in the permissive sub=0 path writes the full-set majority too. */
    return (1);
  }
  /* sub=2 permissive: result=0, no D_FLAG. */
  *result = 0;
  return (1);
}

/* For sub=0, fig4Nfn writes *result = (cnt[1] > cnt[0]) ? 1 : 0
 * regardless of permissive vs exact.  Match that for compare. */
static unsigned char
fullSetMajorityZero(const unsigned char *values, unsigned int n_msgs) {
  unsigned int c0, c1, i;
  c0 = c1 = 0;
  for (i = 0; i < n_msgs; ++i) {
    unsigned char v = values[i] & (unsigned char)~BRACHA87_D_FLAG;
    if (v == 0) ++c0;
    else if (v == 1) ++c1;
  }
  return (c1 > c0) ? 1 : 0;
}

/* ----------------------------------------------------------------
 * fig4Nfn correspondence: enumerate every input combination of size
 * 1..NN over the value alphabet {0, 1, 0|D_FLAG, 1|D_FLAG}, for each
 * sub-round 0,1,2; compare fig4Nfn output to fig4NRef.
 * ---------------------------------------------------------------- */

static const unsigned char Alpha[4] = {
  0, 1,
  (unsigned char)(0 | BRACHA87_D_FLAG),
  (unsigned char)(1 | BRACHA87_D_FLAG)
};

/* Never drawn: this arm calls fig4Nfn directly and never reaches
 * Fig4Round's case (iii).  It exists because bracha87Fig4Init refuses
 * a null coin. */
static unsigned char
predCoin(
  void *closure
 ,unsigned char instance
 ,unsigned char phase
){
  (void)closure;
  (void)instance;
  (void)phase;
  return (0);
}

static void
testFig4NfnCorrespondence(void) {
  struct bracha87Fig4 *b;
  unsigned long sz;
  unsigned int n_msgs;
  unsigned int sub;
  unsigned int idx[NN];
  unsigned char vals[NN];
  unsigned char senders[NN];
  unsigned int i;
  unsigned int total;
  unsigned int agreed;

  printf("\nfig4Nfn correspondence (n=%d, t=%d, all sub-rounds):\n", NN, TT);

  sz = bracha87Fig4Sz(NN - 1, 10);
  b = calloc(1, sz);
  bracha87Fig4Init(b, NN - 1, TT, 10, 0, 0, predCoin, 0);
  for (i = 0; i < NN; ++i) senders[i] = (unsigned char)i;

  total = agreed = 0;
  for (sub = 0; sub < 3; ++sub) {
    unsigned char k = (unsigned char)sub;
    for (n_msgs = NT; n_msgs <= NN; ++n_msgs) {
      /* Enumerate all 4^n_msgs value tuples. */
      unsigned int total_tuples = 1;
      unsigned int t;
      for (i = 0; i < n_msgs; ++i) total_tuples *= 4;
      for (t = 0; t < total_tuples; ++t) {
        unsigned int x = t;
        unsigned char r_actual;
        unsigned char r_ref;
        int rc_actual;
        int rc_ref;
        unsigned char canon_actual;
        unsigned char canon_ref;

        for (i = 0; i < n_msgs; ++i) {
          idx[i] = x & 3;
          vals[i] = Alpha[idx[i]];
          x >>= 2;
        }

        rc_actual = fig4Nfn(b, k, n_msgs, senders, vals, &r_actual);
        rc_ref    = fig4NRef(k, n_msgs, vals, NN, TT, &r_ref);
        ++total;

        /* Normalize permissive return: both must say "exact" or both
         * "permissive".  Negative rc -> degenerate, both must agree. */
        if ((rc_actual == 0) != (rc_ref == 0)) continue; /* mismatch */
        if ((rc_actual < 0) != (rc_ref < 0))   continue;

        /* For exact (rc=0), results must equal byte-for-byte. */
        if (rc_actual == 0) {
          if (r_actual == r_ref) ++agreed;
          continue;
        }

        /* For permissive (rc>0), the convention is sub-round dependent:
         *   sub=0: both write tie-break-to-0 majority of FULL set.
         *   sub=1: both encode v|D_FLAG iff some subset reached strict
         *          majority for v; the v identity must match.
         *   sub=2: both write 0, no D_FLAG.
         */
        if (sub == 0) {
          canon_actual = r_actual;
          canon_ref    = fullSetMajorityZero(vals, n_msgs);
          if (canon_actual == canon_ref) ++agreed;
          continue;
        }
        if (sub == 2) {
          if (r_actual == 0 && r_ref == 0) ++agreed;
          continue;
        }
        /* sub == 1 */
        canon_actual = r_actual;
        canon_ref    = r_ref;
        /* Both should have D_FLAG iff D_FLAG legitimate; base v should
         * match if both have D_FLAG. */
        if ((canon_actual & BRACHA87_D_FLAG) !=
            (canon_ref    & BRACHA87_D_FLAG)) continue;
        if (canon_actual & BRACHA87_D_FLAG) {
          if ((canon_actual & 1) == (canon_ref & 1)) ++agreed;
          continue;
        }
        ++agreed; /* both no D_FLAG, both permissive */
      }
    }
  }

  printf("  enumerated %u inputs, agreed on %u\n", total, agreed);
  check("fig4Nfn matches the subset-enumeration reference at all bounded inputs",
        agreed == total);
  free(b);
}

/* ----------------------------------------------------------------
 * fig3IsValid correspondence at k=0 and k=1.
 *
 * VALID^0 is paper-trivial: v in {0, 1}.
 * VALID^1 is the existential ?S subset-of-VALID^0, |S|=n-t,
 *         such that v = N_test(0, S).  We use a deterministic
 *         test-N (majority-tie-to-0) so the existential reduces
 *         to "v = some-subset-majority of validated round-0 values."
 *
 * We pre-populate VALID^0 by accepting round-0 messages, then for
 * every (sender, value) candidate at round 1 compare the observable
 * outcome of bracha87Fig3Accept (validCount delta) to a subset-enumeration
 * subset-enumeration reference.
 * ---------------------------------------------------------------- */

/* Paper-direct test N: enumerate every n-t subset, compute majority-
 * tie-to-0 per subset, aggregate.  rc=0 if all subsets agree (exact);
 * rc>0 if subsets disagree (permissive, no D_FLAG since this test N
 * doesn't model d-flagged inputs).
 *
 * fig3IsValid invokes N on the full validated set; this N then does
 * the existential subset enumeration that the paper definition of
 * VALID^k requires.  A non-existential N (e.g. just the full-set
 * majority) wouldn't expose the existential and fig3IsValid couldn't
 * see VALID^k members that are reachable only via a subset. */
static int
testN(
  void *closure
 ,unsigned char k
 ,unsigned int n_msgs
 ,const unsigned char *senders
 ,const unsigned char *values
 ,unsigned char *result
){
  unsigned int idx[NN];
  unsigned int i;
  unsigned char first;
  int seen;
  int allSame;

  (void)closure; (void)senders; (void)k;

  if (n_msgs < NT) return (-1);

  for (i = 0; i < NT; ++i) idx[i] = i;
  seen = 0;
  allSame = 1;
  first = 0;

  do {
    unsigned int c0, c1;
    unsigned char m;
    c0 = c1 = 0;
    for (i = 0; i < NT; ++i) {
      if (values[idx[i]] == 0) ++c0;
      else if (values[idx[i]] == 1) ++c1;
    }
    m = (c1 > c0) ? 1 : 0;
    if (!seen) { first = m; seen = 1; }
    else if (m != first) allSame = 0;
  } while (nextCombo(idx, NT, n_msgs));

  if (allSame) {
    *result = first;
    return (0);
  }
  *result = 0;
  return (1);
}

/* Reference: value v is in VALID^1 iff exists a subset of size n-t
 * of validated round-0 values whose majority-tie-to-0 == v. */
static int
isInValid1Ref(unsigned char v, const unsigned char *valid0, unsigned int cnt) {
  unsigned int idx[NN];
  unsigned int i;

  if (cnt < NT) return (0);
  if (v > 1) return (0);
  for (i = 0; i < NT; ++i) idx[i] = i;
  do {
    unsigned int c0, c1;
    unsigned char m;
    c0 = c1 = 0;
    for (i = 0; i < NT; ++i) {
      if (valid0[idx[i]] == 0) ++c0;
      else if (valid0[idx[i]] == 1) ++c1;
    }
    m = (c1 > c0) ? 1 : 0;
    if (v == m) return (1);
  } while (nextCombo(idx, NT, cnt));
  return (0);
}

static void
testFig3IsValidCorrespondence(void) {
  struct bracha87Fig3 *b;
  unsigned long sz;
  unsigned int total, agreed;
  unsigned int v0_combo;
  unsigned char v;

  printf("\nfig3IsValid correspondence (n=%d, t=%d, k in {0, 1}):\n",
         NN, TT);

  sz = bracha87Fig3Sz(NN - 1, 5);
  total = agreed = 0;

  /* k=0: trivially v in {0,1}.  Three test points: v=0, v=1, v=2. */
  for (v = 0; v <= 2; ++v) {
    int actual, ref;
    b = calloc(1, sz);
    bracha87Fig3Init(b, NN - 1, TT, 5, testN, 0);
    actual = fig3IsValid(b, 0, v);
    ref    = (v <= 1);
    ++total;
    if (!actual == !ref) ++agreed;
    free(b);
  }

  /* k=1: enumerate every assignment of round-0 values to senders 0..n-1.
   * Each sender is assigned a value in {0, 1, "absent"}.  Then probe
   * each candidate v in {0, 1} via fig3IsValid at k=1 and compare. */
  for (v0_combo = 0; v0_combo < 81; ++v0_combo) {
    unsigned char r0_assign[NN];
    unsigned char valid0[NN];
    unsigned int valid0Cnt;
    unsigned int x;
    unsigned int s;

    /* 3^4 = 81 assignments: each sender's slot is 0, 1, or 2 ("absent"). */
    x = v0_combo;
    for (s = 0; s < NN; ++s) { r0_assign[s] = (unsigned char)(x % 3); x /= 3; }

    /* Build Fig3 state: accept all non-absent r0_assign values at round 0. */
    b = calloc(1, sz);
    bracha87Fig3Init(b, NN - 1, TT, 5, testN, 0);
    valid0Cnt = 0;
    for (s = 0; s < NN; ++s) {
      if (r0_assign[s] < 2) {
        unsigned int vc;
        bracha87Fig3Accept(b, 0, (unsigned char)s, r0_assign[s], &vc);
        if (vc > valid0Cnt) {
          valid0[valid0Cnt++] = r0_assign[s];
        }
      }
    }

    /* Probe v=0 and v=1 at round 1. */
    for (v = 0; v <= 1; ++v) {
      int actual, ref;
      actual = fig3IsValid(b, 1, v);
      ref    = isInValid1Ref(v, valid0, valid0Cnt);
      ++total;
      if (!actual == !ref) ++agreed;
      else printf("  k=1 v=%u combo=%u valid0=%u: actual=%d ref=%d\n",
                  v, v0_combo, valid0Cnt, actual, ref);
    }
    free(b);
  }

  printf("  enumerated %u predicate evaluations, agreed on %u\n",
         total, agreed);
  check("fig3IsValid matches the subset-enumeration reference at k in {0, 1}",
        agreed == total);
}

/* ----------------------------------------------------------------
 * Cascade correspondence: drive bracha87Fig3Accept through bounded
 * sequences with varying delivery order; final per-round VALID
 * bitmaps must match a subset-enumeration reference that re-validates
 * from scratch on each step.
 * ---------------------------------------------------------------- */

/* Reference: given the multiset of accepted (round, sender, value)
 * messages so far, compute per-round VALID bitmaps from scratch. */
static void
referenceValidate(
  const unsigned char *acceptedRound
 ,const unsigned char *acceptedSender
 ,const unsigned char *acceptedValue
 ,unsigned int nAccepted
 ,unsigned int maxRounds
 ,unsigned char *validBmp /* maxRounds bytes; bit s set if (sender s) valid at round r */
){
  unsigned int r;
  unsigned int j;
  int progressed;

  memset(validBmp, 0, maxRounds);

  /* VALID^0: every accepted with value <= 1 is valid. */
  for (j = 0; j < nAccepted; ++j) {
    if (acceptedRound[j] == 0 && acceptedValue[j] <= 1)
      validBmp[0] |= (unsigned char)(1u << acceptedSender[j]);
  }

  /* Iterate higher rounds until fixed point. */
  do {
    progressed = 0;
    for (r = 1; r < maxRounds; ++r) {
      /* Collect VALID^{r-1} senders/values. */
      unsigned char prevS[NN], prevV[NN];
      unsigned int prevCnt = 0;
      unsigned int s;
      for (s = 0; s < NN; ++s) {
        if (validBmp[r - 1] & (1u << s)) {
          /* Look up the value of (round=r-1, sender=s). */
          unsigned int q;
          for (q = 0; q < nAccepted; ++q) {
            if (acceptedRound[q] == r - 1 && acceptedSender[q] == s) {
              prevS[prevCnt] = (unsigned char)s;
              prevV[prevCnt] = acceptedValue[q];
              ++prevCnt;
              break;
            }
          }
        }
      }
      if (prevCnt < NT) continue;

      /* For each accepted (round=r, sender, value) not already valid,
       * test against testN over every n-t subset of prevS. */
      for (j = 0; j < nAccepted; ++j) {
        if (acceptedRound[j] != r) continue;
        if (validBmp[r] & (1u << acceptedSender[j])) continue;

        /* isInValidR: does v match testN-of-some-subset? */
        {
          unsigned int idx[NN];
          unsigned int i;
          int found = 0;
          for (i = 0; i < NT; ++i) idx[i] = i;
          do {
            unsigned char sv[NN];
            unsigned char rr;
            for (i = 0; i < NT; ++i) sv[i] = prevV[idx[i]];
            testN(0, (unsigned char)(r - 1), NT, prevS, sv, &rr);
            if (rr == acceptedValue[j]) { found = 1; break; }
          } while (nextCombo(idx, NT, prevCnt));
          if (found) {
            validBmp[r] |= (unsigned char)(1u << acceptedSender[j]);
            progressed = 1;
          }
        }
      }
    }
  } while (progressed);
}

static void
testCascadeCorrespondence(void) {
  static const unsigned char rounds[]  = {0, 0, 0, 0, 1, 1, 1, 1};
  static const unsigned char senders[] = {0, 1, 2, 3, 0, 1, 2, 3};
  static const unsigned char values[]  = {0, 0, 0, 1, 0, 0, 0, 0};
  unsigned int nMsg = sizeof rounds / sizeof rounds[0];
  unsigned long sz;
  unsigned int perm;
  unsigned int agreed = 0;
  unsigned int total = 0;
  unsigned int order[8];
  unsigned int i;

  printf("\nCascade correspondence (n=%d, t=%d, 4 round-0 + 4 round-1 deliveries):\n",
         NN, TT);

  sz = bracha87Fig3Sz(NN - 1, 5);

  /* Try several delivery orders.  Full 8! = 40320 is overkill;
   * sample a deterministic mix: identity, reverse, round-1-first,
   * interleaved.  Each must reach the same fixed-point bitmaps. */
  for (perm = 0; perm < 4; ++perm) {
    struct bracha87Fig3 *b;
    unsigned char actualBmp[5] = {0};
    unsigned char refBmp[5] = {0};
    unsigned char accR[8], accS[8], accV[8];

    switch (perm) {
    case 0: for (i = 0; i < nMsg; ++i) order[i] = i; break;
    case 1: for (i = 0; i < nMsg; ++i) order[i] = nMsg - 1 - i; break;
    case 2: order[0]=4; order[1]=5; order[2]=6; order[3]=7;
            order[4]=0; order[5]=1; order[6]=2; order[7]=3; break;
    case 3: order[0]=0; order[1]=4; order[2]=1; order[3]=5;
            order[4]=2; order[5]=6; order[6]=3; order[7]=7; break;
    }

    b = calloc(1, sz);
    bracha87Fig3Init(b, NN - 1, TT, 5, testN, 0);
    for (i = 0; i < nMsg; ++i) {
      unsigned int j = order[i];
      unsigned int vc;
      bracha87Fig3Accept(b, rounds[j], senders[j], values[j], &vc);
      accR[i] = rounds[j]; accS[i] = senders[j]; accV[i] = values[j];
    }

    /* Read actual final VALID bitmaps for rounds 0..1. */
    {
      unsigned char ss[NN], vv[NN];
      unsigned int j, c;
      c = bracha87Fig3GetValid(b, 0, ss, vv);
      for (j = 0; j < c; ++j) actualBmp[0] |= (unsigned char)(1u << ss[j]);
      c = bracha87Fig3GetValid(b, 1, ss, vv);
      for (j = 0; j < c; ++j) actualBmp[1] |= (unsigned char)(1u << ss[j]);
    }

    referenceValidate(accR, accS, accV, nMsg, 5, refBmp);

    ++total;
    if (actualBmp[0] == refBmp[0] && actualBmp[1] == refBmp[1])
      ++agreed;
    else
      printf("  perm %u: actual r0=0x%02x r1=0x%02x; ref r0=0x%02x r1=0x%02x\n",
             perm, actualBmp[0], actualBmp[1], refBmp[0], refBmp[1]);

    free(b);
  }

  printf("  %u delivery permutations, agreed on %u\n", total, agreed);
  check("Cascade reaches the reference's fixed point under shuffled delivery",
        agreed == total);
}

/* ----------------------------------------------------------------
 * Site independence.  bracha87Fig1.dtc and bkr94acs.dtc each compile
 * to one snippet reached from more than one C site.  A site feeds
 * from state the inputs its own outputs read and a fixed value to the
 * rest -- what the .dtc text calls feeding by fiat -- and discards
 * the outputs it does not read, which may fire on the fiat values
 * (an un-echoed initiator's tick fires Rule 1; an undecided byte fed
 * by fiat walks).  That is sound only if nothing a site reads moves
 * with a value it fed by fiat.  Table structure alone does not give
 * that: the echo and ready retries chain on the send rules, which read the kind
 * of message Bpr feeds by fiat, and what keeps them constant is row
 * content (a retry is "no" whenever its send fires, and a send fires
 * only with the flag the retry also needs clear).  So it is checked,
 * not derived: this enumerates every input combination of each
 * snippet, requires every output assigned on every path, and for
 * each site requires the outputs it reads to equal those of the same
 * combination with the site's fiat inputs at the site's fiat values.
 * The sites and their feeds are read off bracha87.c and bkr94acs.c.
 */

/* bracha87Fig1Rules.c: the twelve inputs in bridge order, eight
 * outputs.  Evaluated by the snippet itself. */
#define F1_IN 12
#define F1_OUT 8
static const unsigned char F1Card[F1_IN] = {
  3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
};
static const char *F1InName[F1_IN] = {
  "type", "haveEchoed", "haveSentReady", "ecGtHalfNT", "rdGeTPlus1",
  "rdGe2TPlus1", "haveAccepted", "amInitiator", "allEchoed",
  "readyMaskFull", "annAccepted", "annReceived"
};
static const char *F1OutName[F1_OUT] = {
  "sendEcho", "sendReady", "acceptV", "retryInitial", "retryEcho",
  "retryReady", "recordSender", "armSender"
};

static void
fig1Eval(const unsigned char *in, unsigned char *o) {
  unsigned char type;
  unsigned char haveEchoed;
  unsigned char haveSentReady;
  unsigned char ecGtHalfNT;
  unsigned char rdGeTPlus1;
  unsigned char rdGe2TPlus1;
  unsigned char haveAccepted;
  unsigned char amInitiator;
  unsigned char allEchoed;
  unsigned char readyMaskFull;
  unsigned char annAccepted;
  unsigned char annReceived;
  unsigned char sendEcho;
  unsigned char sendReady;
  unsigned char acceptV;
  unsigned char retryInitial;
  unsigned char retryEcho;
  unsigned char retryReady;
  unsigned char recordSender;
  unsigned char armSender;

  type          = in[0] == 0 ? BRACHA87_INITIAL
                : in[0] == 1 ? BRACHA87_ECHO : BRACHA87_READY;
  haveEchoed    = in[1];
  haveSentReady = in[2];
  ecGtHalfNT    = in[3];
  rdGeTPlus1    = in[4];
  rdGe2TPlus1   = in[5];
  haveAccepted  = in[6];
  amInitiator   = in[7];
  allEchoed     = in[8];
  readyMaskFull = in[9];
  annAccepted   = in[10];
  annReceived   = in[11];
  sendEcho = sendReady = acceptV = 0xAA;
  retryInitial = retryEcho = retryReady = 0xAA;
  recordSender = armSender = 0xAA;
#include "../bracha87Fig1Rules.c"
  o[0] = sendEcho;
  o[1] = sendReady;
  o[2] = acceptV;
  o[3] = retryInitial;
  o[4] = retryEcho;
  o[5] = retryReady;
  o[6] = recordSender;
  o[7] = armSender;
}

/* bkr94acsRules.c: seven inputs, five outputs. */
#define ACS_IN 7
#define ACS_OUT 5
static const unsigned char AcsCard[ACS_IN] = { 3, 3, 2, 4, 2, 2, 2 };
static const char *AcsInName[ACS_IN] = {
  "acsEvent", "inputToBAj", "postCountAllN", "baDecision", "seam",
  "enabled", "nothingLeft"
};
static const char *AcsOutName[ACS_OUT] = {
  "doInput1", "doOutputSubset", "walk", "duty", "fire"
};

static void
acsEval(const unsigned char *in, unsigned char *o) {
  unsigned char acsEvent;
  unsigned char inputToBAj;
  unsigned char postCountAllN;
  unsigned char baDecision;
  unsigned char seam;
  unsigned char enabled;
  unsigned char nothingLeft;
  unsigned char doInput1;
  unsigned char doOutputSubset;
  unsigned char walk;
  unsigned char duty;
  unsigned char fire;

  acsEvent      = in[0] == 0 ? BKR94ACS_ACS_EVENT_Q
                : in[0] == 1 ? BKR94ACS_ACS_EVENT_BA0
                : BKR94ACS_ACS_EVENT_BA1;
  inputToBAj    = in[1] == 0 ? BKR94ACS_ENTER_NONE
                : in[1] == 1 ? BKR94ACS_ENTER_ZERO : BKR94ACS_ENTER_ONE;
  postCountAllN = in[2];
  baDecision    = in[3] == 0 ? 0xFF : in[3] == 1 ? 0
                : in[3] == 2 ? 1 : 0xFE;
  seam          = in[4] ? BKR94ACS_SEAM_TURN : BKR94ACS_SEAM_FANOUT;
  enabled       = in[5];
  nothingLeft   = in[6];
  doInput1 = doOutputSubset = walk = duty = fire = 0xAA;
#include "../bkr94acsRules.c"
  o[0] = doInput1;
  o[1] = doOutputSubset;
  o[2] = walk;
  o[3] = duty;
  o[4] = fire;
}

/* One site: the inputs it feeds by fiat (value, or 0xFF for a
 * state-fed input), the subspace it runs in (an input the site
 * reaches only at one value and feeds from state there -- the kind
 * of message at Input's three branches, the event class at the two
 * ACS event sites; 0xFF for unconstrained), and the outputs it
 * reads.  An input fed a fixed value by the site, the kind of
 * message at Bpr among them, is fiat, not pin. */
struct site {
  const char *name;
  const unsigned char *fiat;
  const unsigned char *pin;
  const unsigned char *reads;
};

static void
siteIndependence(const char *what, unsigned int nIn, unsigned int nOut,
                 const unsigned char *card, const char *const *inName,
                 const char *const *outName,
                 void (*eval)(const unsigned char *, unsigned char *),
                 const struct site *sites, unsigned int nSites) {
  unsigned char in[F1_IN];       /* the wider of the two tables */
  unsigned char alt[F1_IN];
  unsigned char o[F1_OUT];
  unsigned char oAlt[F1_OUT];
  unsigned int combos;
  unsigned int unassigned;
  unsigned int i, j, k, x;
  int ok;

  combos = 0;
  unassigned = 0;
  memset(in, 0, sizeof (in));
  for (;;) {
    ++combos;
    eval(in, o);
    for (j = 0; j < nOut; ++j)
      if (o[j] == 0xAA) {
        if (!unassigned) {
          printf("    %s: %s unassigned at", what, outName[j]);
          for (x = 0; x < nIn; ++x)
            printf(" %s=%u", inName[x], in[x]);
          printf("\n");
        }
        ++unassigned;
      }
    /* advance the mixed-radix counter */
    for (i = 0; i < nIn && ++in[i] == card[i]; ++i)
      in[i] = 0;
    if (i == nIn)
      break;
  }
  printf("  %s: %u input combinations enumerated\n", what, combos);
  check("every output assigned on every path", unassigned == 0);

  for (k = 0; k < nSites; ++k) {
    const struct site *st;
    unsigned int checked;
    unsigned int moved;

    st = &sites[k];
    checked = 0;
    moved = 0;
    memset(in, 0, sizeof (in));
    for (;;) {
      ok = 1;
      for (i = 0; i < nIn; ++i)
        if (st->pin[i] != 0xFF && in[i] != st->pin[i])
          ok = 0;
      if (ok) {
        for (i = 0; i < nIn; ++i)
          alt[i] = st->fiat[i] == 0xFF ? in[i] : st->fiat[i];
        eval(in, o);
        eval(alt, oAlt);
        ++checked;
        for (j = 0; j < nOut; ++j)
          if (st->reads[j] && o[j] != oAlt[j]) {
            if (!moved) {
              printf("    %s site %s: %s moves with a fiat input at",
                     what, st->name, outName[j]);
              for (x = 0; x < nIn; ++x)
                printf(" %s=%u", inName[x], in[x]);
              printf("\n");
            }
            ++moved;
          }
      }
      for (i = 0; i < nIn && ++in[i] == card[i]; ++i)
        in[i] = 0;
      if (i == nIn)
        break;
    }
    printf("    site %s: %u combinations, reads constant over its fiat"
           " inputs: %s\n", st->name, checked, moved ? "NO" : "yes");
    check("a site's reads are a function of what it feeds from state",
          moved == 0 && checked > 0);
  }
}

static void
testSiteIndependence(void) {
  /* bracha87.c: Input feeds the three retire inputs 0 and, on each
   * kind of message, the thresholds of the counts that kind cannot
   * fire 0 (ec is computed on the echo branch, rd on the ready
   * branch, neither on initial); it reads the paper and annotation
   * outputs.  Bpr feeds kind (initial, v), the thresholds 0 and the
   * annotations (0, 1), and reads the three retries. */
  static const unsigned char InputFiat[F1_IN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, 0xFF
  };
  static const unsigned char InitialFiat[F1_IN] = {
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0xFF
  };
  static const unsigned char EchoFiat[F1_IN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0xFF, 0, 0, 0, 0xFF, 0xFF
  };
  static const unsigned char ReadyFiat[F1_IN] = {
    0xFF, 0xFF, 0xFF, 0, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0xFF, 0xFF
  };
  static const unsigned char InitialPin[F1_IN] = {
    0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char EchoPin[F1_IN] = {
    1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char ReadyPin[F1_IN] = {
    2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char NoPinF1[F1_IN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char InputReads[F1_OUT] = {
    1, 1, 1, 0, 0, 0, 1, 1
  };
  static const unsigned char BprFiat[F1_IN] = {
    0, 0xFF, 0xFF, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 1
  };
  static const unsigned char BprReads[F1_OUT] = {
    0, 0, 0, 1, 1, 1, 0, 0
  };
  static const struct site Fig1Sites[] = {
    { "bracha87Fig1Input",              InputFiat,   NoPinF1,    InputReads },
    { "bracha87Fig1Input (initial, v)", InitialFiat, InitialPin, InputReads },
    { "bracha87Fig1Input (echo, v)",    EchoFiat,    EchoPin,    InputReads },
    { "bracha87Fig1Input (ready, v)",   ReadyFiat,   ReadyPin,   InputReads },
    { "bracha87Fig1Bpr",                BprFiat,     NoPinF1,    BprReads }
  };
  /* bkr94acs.c: AcastInput on a Q event feeds the count 0 and the
   * BPR groups (undecided, fanout, 0, 0), reads doInput1; Turn on a
   * BA-output event feeds the same BPR groups, reads doOutputSubset;
   * the retry gate feeds (Q, one, 0) and the duty groups, reads
   * walk; the duty site feeds (Q, one, 0, undecided), reads duty
   * and fire. */
  static const unsigned char AcastFiat[ACS_IN] = {
    0xFF, 0xFF, 0, 0, 0, 0, 0
  };
  static const unsigned char AcastPin[ACS_IN] = {
    0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char AcastReads[ACS_OUT] = { 1, 0, 0, 0, 0 };
  static const unsigned char TurnFiat[ACS_IN] = {
    0xFF, 0xFF, 0xFF, 0, 0, 0, 0
  };
  static const unsigned char Turn0Pin[ACS_IN] = {
    1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char Turn1Pin[ACS_IN] = {
    2, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char TurnReads[ACS_OUT] = { 0, 1, 0, 0, 0 };
  static const unsigned char GateFiat[ACS_IN] = { 0, 2, 0, 0xFF, 0, 0, 0 };
  static const unsigned char GateReads[ACS_OUT] = { 0, 0, 1, 0, 0 };
  static const unsigned char DutyFiat[ACS_IN] = {
    0, 2, 0, 0, 0xFF, 0xFF, 0xFF
  };
  static const unsigned char DutyReads[ACS_OUT] = { 0, 0, 0, 1, 1 };
  static const unsigned char NoPin[ACS_IN] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  static const struct site AcsSites[] = {
    { "bkr94acsAcastInput",       AcastFiat, AcastPin, AcastReads },
    { "bkr94acsTurn (BA_j = 0)",  TurnFiat,  Turn0Pin, TurnReads },
    { "bkr94acsTurn (BA_j = 1)",  TurnFiat,  Turn1Pin, TurnReads },
    { "bkr94acsRetryStep (the verdict gate)", GateFiat, NoPin, GateReads },
    { "bkr94acsDuty",             DutyFiat,  NoPin,    DutyReads }
  };

  unsigned char in[F1_IN];
  unsigned char o[F1_OUT];
  unsigned int i;
  unsigned int cells;
  unsigned int armed;

  printf("\n[Site independence of the merged dispatches]\n");
  siteIndependence("bracha87Fig1Rules.c", F1_IN, F1_OUT, F1Card, F1InName,
                   F1OutName, fig1Eval, Fig1Sites,
                   sizeof (Fig1Sites) / sizeof (Fig1Sites[0]));
  siteIndependence("bkr94acsRules.c", ACS_IN, ACS_OUT, AcsCard, AcsInName,
                   AcsOutName, acsEval, AcsSites,
                   sizeof (AcsSites) / sizeof (AcsSites[0]));

  /* The arm rule's pre-accept cell (bracha87Fig1.dtc, "arm re-send
   * to sender"): a (ready, v) without RECEIVED at an instance that
   * has not accepted, and does not accept on it, arms nothing.
   * Through bracha87Fig1Input the cell is invisible -- the setter
   * refuses the same arm (BPR.md, Declared Invisible) -- so the row
   * is held here on the snippet, over every combination of the
   * other inputs. */
  cells = 0;
  armed = 0;
  memset(in, 0, sizeof (in));
  for (;;) {
    if (in[0] == 2 && in[11] == 0 && in[6] == 0) {
      fig1Eval(in, o);
      if (!o[2]) {
        ++cells;
        if (o[7])
          ++armed;
      }
    }
    for (i = 0; i < F1_IN && ++in[i] == F1Card[i]; ++i)
      in[i] = 0;
    if (i == F1_IN)
      break;
  }
  printf("  arm rule, pre-accept cell: %u combinations, %u armed\n", cells,
         armed);
  check("an unmarked (ready, v) before accept arms nothing",
        cells > 0 && armed == 0);
}

int
main(void) {
  printf("=================================================\n");
  printf("test_predicates: subset-enumeration correspondence\n");
  printf("=================================================\n");

  testFig4NfnCorrespondence();
  testFig3IsValidCorrespondence();
  testCascadeCorrespondence();
  testSiteIndependence();

  printf("\n=================================================\n");
  printf("Pass: %u  Fail: %u\n", Pass, Fail);
  printf("=================================================\n");
  return (Fail ? 1 : 0);
}
