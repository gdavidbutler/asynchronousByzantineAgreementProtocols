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

/*
 * A reference retention store for system.h
 *
 * system.[hc] decides everything about a retained round and stores
 * none of it: the rounds a process holds, and the records kept per
 * held round, are the caller's, reached through four operations
 * fixed at systemInit (system.h, THE RETENTION OPERATIONS).  This
 * is one implementation of those four -- a capacity-bounded array
 * kept in the round order, sufficient for any deployment whose
 * reach fits in memory it can size up front.
 *
 * It is a REFERENCE, not a required part of the layer.  A
 * deployment is free to answer the four operations from a tree, an
 * index over its artifact store, or a database; the machine cannot
 * tell, which is the point of the seam.  What this file is for:
 *
 *   - it DISCHARGES the retention requirements (system.md,
 *     Mechanization status) so a caller can read one worked
 *     example of each term rather than infer them;
 *   - it gives the instruments a store that is not each one's own,
 *     so a divergence between two instruments is a finding about
 *     the machine and not about two hand-rolled stores;
 *   - it is the artifact the store-mutant tier mutates: a term is
 *     shown load-bearing by breaking it HERE and watching a
 *     designated oracle fire.
 *
 * IT ENCODES NO MACHINE KNOWLEDGE, and that is a correctness
 * property rather than a style note.  It knows the four operations,
 * systemRecordsSz, and the caller's comparator; it knows no rule,
 * no invariant, no act, and no obligation.  A store that "helps" --
 * skipping a retain it judges redundant, clearing a want bit,
 * answering an order question its own way -- can absorb a defect in
 * the machine, and then every instrument sharing it is wrong
 * identically and nothing catches it.  Keep it dumb.
 *
 * THE CLOSURE CARRIES THE COMPARATOR.  systemInit takes ONE context
 * and hands it to both the comparator and the four operations, so a
 * store used as that context must be able to answer the comparator
 * too: pass systemStoreCmp as systemInit's cmp and the store as its
 * ctx, and the store forwards to the comparator it was initialized
 * with.  A deployment whose context is its own object does the same
 * thing in its own terms.
 */

#ifndef SYSTEMSTORE_H
#define SYSTEMSTORE_H

/*
 * One process's retention store.  Entries are held in the
 * comparator's order, so 'after' is a position and the lookups are
 * a binary search -- the order is the caller's authority (system.h
 * systemInit), never a byte comparison of the names.
 */
struct systemStore {
  int (*cmp)(void *, const unsigned char *, const unsigned char *);
  void *ctx;              /* the comparator's own context */
  unsigned long cap;      /* rounds this store will hold: THE REACH */
  unsigned long cnt;      /* rounds held now */
  unsigned int rs;        /* round-name size in bytes */
  unsigned int n;         /* process count encoding: actual = n + 1 */
  unsigned char data[1];  /* variable: cap entries of name + records */
};

/*
 * Size in bytes for a store of 'cap' rounds.  'cap' IS the declared
 * RECOVERY REACH (system.md O6) seen from the storage side: when it
 * is reached, retain refuses, and the machine answers a refusal by
 * releasing the oldest round and asking once more.  Sizing it is
 * tuning; what it approximates is not.
 */
unsigned long
systemStoreSz(
  unsigned int              /* n: actual process count = n + 1 */
 ,unsigned int              /* rs: round-name size in bytes */
 ,unsigned long             /* cap: rounds to hold */
);

/*
 * Initialize an EMPTY store -- the state systemInit requires of it
 * (system.h, the retention requirements).  'cmp' and 'ctx' are the
 * caller's round comparator and its context: the SAME comparator
 * the machine is given, so the order the store keeps and the order
 * the machine consumes cannot diverge.
 * Rejects a null pointer, rs = 0, cap = 0, and a null cmp; a
 * rejected store holds nothing and refuses every retain.
 */
void
systemStoreInit(
  struct systemStore *
 ,unsigned int              /* n: actual process count = n + 1 */
 ,unsigned int              /* rs: round-name size in bytes */
 ,unsigned long             /* cap: rounds to hold */
 ,int (*)(void *, const unsigned char *, const unsigned char *)
                            /* cmp: the round comparator */
 ,void *                    /* ctx: the comparator's context */
);

/*
 * The comparator, forwarded -- pass THIS as systemInit's cmp and
 * the store as systemInit's ctx (see the header note above).
 */
int
systemStoreCmp(
  void *                    /* the store */
 ,const unsigned char *
 ,const unsigned char *
);

/*
 * The four retention operations, in systemInit's argument order.
 * Their contracts are system.h's; the terms they discharge here:
 * retain refuses only when full and a refusal changes nothing
 * (retaining a round already held is not a refusal: it answers
 * that round's records); nothing is added or dropped except
 * through retain and release; after is strictly forward in the
 * comparator's order, each held round once, 0 at the end; and a
 * record or name handed back stays valid until the set next
 * changes through retain or release -- this store keeps its
 * entries in order, so a retain or release BELOW a round
 * relocates it, contents intact, and a re-ask reaches it.
 */
unsigned char *
systemStoreRecords(
  void *                    /* the store */
 ,const unsigned char *     /* round */
);

unsigned char *
systemStoreRetain(
  void *                    /* the store */
 ,const unsigned char *     /* round */
);

void
systemStoreRelease(
  void *                    /* the store */
 ,const unsigned char *     /* round */
);

unsigned char *
systemStoreAfter(
  void *                    /* the store */
 ,const unsigned char *     /* round, or 0 for the oldest held */
 ,const unsigned char **    /* out: the store's own name for it */
);

/*
 * Rounds held now -- for instruments and reports.  No rule of the
 * layer reads a count (system.md: counting is not an operation on
 * retention); this is not part of the seam.
 */
unsigned long
systemStoreCount(
  const struct systemStore *
);

/*
 * The live entries as one contiguous, canonical byte range, with
 * its length -- for a falsifier that must key the retained set into
 * its visited state, which it must, since the set is reachable
 * state the machine directs but does not hold.  Entries are in the
 * comparator's order and compacted on release, so equal stores give
 * equal bytes and the range never carries a freed tail.  Valid
 * until the next call that changes the store.
 *
 * The WHOLE ALLOCATION is canonical too, not just this range: a
 * release clears the entry it vacates, so two stores holding the
 * same rounds with the same records are byte-identical however they
 * got there.  That is a correctness property of a store, not
 * hygiene -- a freed name left in place tells anything comparing
 * the bytes that two identical retained sets are different states,
 * and splits states no operation can tell apart.
 */
const unsigned char *
systemStoreState(
  const struct systemStore *
 ,unsigned long *           /* out: length in bytes */
);

#endif /* SYSTEMSTORE_H */
