/*
 * asynchronousByzantineAgreementProtocols - reference retention store
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

#include <string.h>
#include "systemStore.h"

/*
 * data[] is cap entries, each the round's name followed by its
 * records:
 *
 *   offset 0    the round's name (rs bytes, a copy -- the caller's
 *               presented name is not retained here)
 *   offset rs   the records, systemRecordsSz(n) bytes, laid out and
 *               written by system.[hc] alone
 *
 * Entries are held ASCENDING in the comparator's order and
 * compacted on release, so index IS position: 'after' is a
 * successor index, the oldest is index 0, and equal stores hold
 * equal bytes (which systemStoreState relies on).  Order comes from
 * the caller's comparator, never from the name bytes -- a round
 * name is a chain construct here, and memcmp would be a different
 * order silently.
 *
 * Nothing in this file reads a record.  It moves them when an
 * insertion shifts, and hands them back; every bit in them is the
 * machine's.
 */

static unsigned int
stBs(
  const struct systemStore *s
){
  return (((unsigned int)s->n + 8) / 8);
}

/* Entry size: the name, then the records. */
static unsigned long
stEsz(
  const struct systemStore *s
){
  return ((unsigned long)s->rs + 3 * stBs(s));
}

static unsigned char *
stEnt(
  struct systemStore *s
 ,unsigned long i
){
  return (s->data + i * stEsz(s));
}

static int
stCmp(
  const struct systemStore *s
 ,const unsigned char *a
 ,const unsigned char *b
){
  return ((*s->cmp)(s->ctx, a, b));
}

/*
 * The first index whose round does not precede 'round', with
 * *found set when that index IS 'round'.  A binary search because
 * the entries are ordered; the comparator answers every step, so
 * the search is sound for any name shape the caller can order.
 */
static unsigned long
stFind(
  struct systemStore *s
 ,const unsigned char *round
 ,int *found
){
  unsigned long lo;
  unsigned long hi;
  unsigned long mid;
  int c;

  *found = 0;
  lo = 0;
  hi = s->cnt;
  while (lo < hi) {
    mid = lo + (hi - lo) / 2;
    if (!(c = stCmp(s, stEnt(s, mid), round))) {
      *found = 1;
      return (mid);
    }
    if (c < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  return (lo);
}

unsigned long
systemStoreSz(
  unsigned int n
 ,unsigned int rs
 ,unsigned long cap
){
  return (sizeof (struct systemStore) - 1
   + cap * ((unsigned long)rs + 3 * (((unsigned long)n + 8) / 8)));
}

void
systemStoreInit(
  struct systemStore *s
 ,unsigned int n
 ,unsigned int rs
 ,unsigned long cap
 ,int (*cmp)(void *, const unsigned char *, const unsigned char *)
 ,void *ctx
){
  if (!s)
    return;
  memset(s, 0, systemStoreSz(n, rs, cap));
  if (!rs || !cap || !cmp)
    return; /* rejected: cmp stays 0, so every operation refuses */
  s->cmp = cmp;
  s->ctx = ctx;
  s->cap = cap;
  s->rs = rs;
  s->n = n;
}

int
systemStoreCmp(
  void *v
 ,const unsigned char *a
 ,const unsigned char *b
){
  struct systemStore *s;

  s = v;
  return (stCmp(s, a, b));
}

unsigned char *
systemStoreRecords(
  void *v
 ,const unsigned char *round
){
  struct systemStore *s;
  unsigned long i;
  int found;

  s = v;
  if (!s || !s->cmp || !round)
    return (0);
  i = stFind(s, round, &found);
  if (!found)
    return (0);
  return (stEnt(s, i) + s->rs);
}

unsigned char *
systemStoreRetain(
  void *v
 ,const unsigned char *round
){
  struct systemStore *s;
  unsigned long i;
  unsigned long j;
  int found;

  s = v;
  if (!s || !s->cmp || !round)
    return (0);
  i = stFind(s, round, &found);
  if (found)
    return (stEnt(s, i) + s->rs);
  if (s->cnt >= s->cap)
    return (0); /* the reach binds -- and nothing has changed */
  for (j = s->cnt; j > i; --j)
    memcpy(stEnt(s, j), stEnt(s, j - 1), stEsz(s));
  memcpy(stEnt(s, i), round, s->rs);
  memset(stEnt(s, i) + s->rs, 0, 3 * stBs(s));
  ++s->cnt;
  return (stEnt(s, i) + s->rs);
}

void
systemStoreRelease(
  void *v
 ,const unsigned char *round
){
  struct systemStore *s;
  unsigned long i;
  unsigned long j;
  int found;

  s = v;
  if (!s || !s->cmp || !round)
    return;
  i = stFind(s, round, &found);
  if (!found)
    return;
  for (j = i; j + 1 < s->cnt; ++j)
    memcpy(stEnt(s, j), stEnt(s, j + 1), stEsz(s));
  --s->cnt;
  /*
   * The vacated entry is cleared, so a released round leaves NO
   * residue anywhere in the allocation.  This is canonicality, not
   * hygiene: two stores holding the same rounds with the same
   * records are the same store, and a freed name left in place says
   * otherwise to anything that compares the bytes.  A state
   * enumeration must compare them -- the retained set is reachable
   * state the machine directs but does not hold, so it belongs in
   * the key -- and a residue splits states no operation can tell
   * apart.  Found by test_system_invariant: at a deep start the
   * residue was a distinct name, at a fresh start it was the
   * all-zero name of round 0 and indistinguishable from never
   * written, so the two runs' reachable sets differed by exactly
   * the states the residue split.
   */
  memset(stEnt(s, s->cnt), 0, stEsz(s));
}

unsigned char *
systemStoreAfter(
  void *v
 ,const unsigned char *round
 ,const unsigned char **name
){
  struct systemStore *s;
  unsigned long i;
  int found;

  s = v;
  if (!s || !s->cmp || !name)
    return (0);
  if (!round)
    i = 0;
  else if ((i = stFind(s, round, &found)), found)
    ++i;
  if (i >= s->cnt)
    return (0);
  *name = stEnt(s, i);
  return (stEnt(s, i) + s->rs);
}

unsigned long
systemStoreCount(
  const struct systemStore *s
){
  return (s && s->cmp ? s->cnt : 0);
}

const unsigned char *
systemStoreState(
  const struct systemStore *s
 ,unsigned long *len
){
  if (!s || !len)
    return (0);
  *len = s->cmp ? s->cnt * stEsz(s) : 0;
  return (s->data);
}
