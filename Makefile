CFLAGS = -I. -Os -g
DTC = ../decisionTableCompiler/dtc
AWK = awk

all: example_bracha87Fig1 example_bkr94acs example_system

bracha87.o: bracha87.c bracha87.h bracha87Fig1Rules.c bracha87Fig3Rules.c bracha87Fig4Rules.c
	$(CC) $(CFLAGS) -c -o $@ bracha87.c

bkr94acs.o: bkr94acs.c bkr94acs.h bracha87.h bkr94acsRules.c
	$(CC) $(CFLAGS) -c -o $@ bkr94acs.c

system.o: system.c system.h systemRules.c
	$(CC) $(CFLAGS) -c -o $@ system.c

# The *Rules.c dispatch snippets are TRACKED SOURCE, not build output.
#
# Each one is generated: dtc co-compiles a .dtc rule table with its
# *ToC.dtc bridge to a .psu dispatch, and psu.awk translates that to the
# snippet an entry point #includes.  They are committed anyway, and no
# rule here remakes them -- nothing in the ordinary graph runs dtc, and
# neither clean nor clobber removes them.
#
# What forces that is the cost of the search.  dtc searches for a
# depth-minimal dispatch, and the search grows with the table: system
# alone is 2 min 34 s on an Apple M2 against 0.14 s for the four paper
# tables together, and slower hardware pays proportionally more.  Whoever
# edits system.dtc should pay that once; a person building the tree
# should not pay it at all.  Committing also makes the tree stand alone,
# since dtc lives in a SECOND repository.
#
# Regeneration is the deliberate act below, after editing a .dtc.  The
# diff it produces is what gets reviewed and committed, and `make rules`
# followed by `git diff --exit-code` is the check that no .dtc edit went
# unbuilt (dtc is deterministic, so a nonempty diff means a real one).
# The per-table targets exist for the same asymmetry: one table's edit
# must not re-run the full search for the other four.
#
# The .psu stays untracked: a machine trace whose only consumer is
# psu.awk.  Read the .dtc for the rules, the snippet for the C.

rules: rules-bracha87Fig1 rules-bracha87Fig3 rules-bracha87Fig4 rules-bkr94acs rules-system

rules-bracha87Fig1:
	$(DTC) bracha87Fig1.dtc bracha87Fig1ToC.dtc > bracha87Fig1.psu
	$(AWK) -f psu.awk bracha87Fig1.psu > bracha87Fig1Rules.c

rules-bracha87Fig3:
	$(DTC) bracha87Fig3.dtc bracha87Fig3ToC.dtc > bracha87Fig3.psu
	$(AWK) -f psu.awk bracha87Fig3.psu > bracha87Fig3Rules.c

rules-bracha87Fig4:
	$(DTC) bracha87Fig4.dtc bracha87Fig4ToC.dtc > bracha87Fig4.psu
	$(AWK) -f psu.awk bracha87Fig4.psu > bracha87Fig4Rules.c

rules-bkr94acs:
	$(DTC) bkr94acs.dtc bkr94acsToC.dtc > bkr94acs.psu
	$(AWK) -f psu.awk bkr94acs.psu > bkr94acsRules.c

rules-system:
	$(DTC) system.dtc systemToC.dtc > system.psu
	$(AWK) -f psu.awk system.psu > systemRules.c

example_bracha87Fig1: example/bracha87Fig1.c bracha87.o bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ example/bracha87Fig1.c bracha87.o

example_bkr94acs: example/bkr94acs.c bkr94acs.o bracha87.o bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ example/bkr94acs.c bkr94acs.o bracha87.o

example_system: example/system.c system.o bkr94acs.o bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ example/system.c system.o bkr94acs.o bracha87.o

test_bracha87: test/test_bracha87.c bracha87.o bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_bracha87.c bracha87.o

test_bkr94acs: test/test_bkr94acs.c bkr94acs.o bracha87.o bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_bkr94acs.c bkr94acs.o bracha87.o

test_predicates: test/test_predicates.c bracha87.c bracha87.h bracha87Fig1Rules.c bracha87Fig3Rules.c bracha87Fig4Rules.c
	$(CC) $(CFLAGS) -I. -o $@ test/test_predicates.c

test_bracha87_blackbox: test/test_bracha87_blackbox.c bracha87.o bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_bracha87_blackbox.c bracha87.o

test_bkr94acs_blackbox: test/test_bkr94acs_blackbox.c bkr94acs.o bracha87.o bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_bkr94acs_blackbox.c bkr94acs.o bracha87.o

test_system: test/test_system.c system.o system.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_system.c system.o

# invariant falsifier -- exploratory, NOT part of check (see the header
# comment: a clean run is evidence a conjunct is worth proving, never a
# substitute for the proof).  Override EN/ET/EW/HORIZON/MAXSTATES/TBLBITS
# on the command line for other configurations.
test_system_invariant: test/test_system_invariant.c system.o system.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_system_invariant.c system.o

# L7 second half -- the twin drive proves the held-members grain gates
# nothing, instead of assuming it the way the default enumeration does.
# Twin A is driven exactly as the default build, so EXPECTSTATES asserts
# that twin A's reachable set is unperturbed.  Exploratory, NOT part of
# check; slower than the default build by the second drive.
test_system_invariant_hrtwin: test/test_system_invariant.c system.o system.h
	$(CC) $(CFLAGS) -DHRTWIN -DEXPECTSTATES=621094 $(CPPFLAGS) -I. -o $@ \
	  test/test_system_invariant.c system.o

# composed-seam falsifier -- exercises the glue between one bkr94acs
# instance per round and one struct system per process.  Exploratory,
# NOT part of check (see the header comment).  Build a glue mutant with
# e.g. CPPFLAGS=-DM_SEAM_DROP; each mutant must fire its check.
test_system_seam: test/test_system_seam.c system.o bkr94acs.o bracha87.o \
                  system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -I. -o $@ test/test_system_seam.c \
	  system.o bkr94acs.o bracha87.o

# THE CONFIG SWEEP.  Every shape constant in the seam is -D overridable
# and everything else derives from it, so one source drives three
# deployment points.  Which scenarios each point runs is the source's own
# decision (SWEEP_LAGGARD / SWEEP_STARVE / SWEEP_BYZ) -- a fault a
# configuration has no budget for is not run there.
#
#   default  n=4 t=1 w=3  PLAIN + LAGGARD + STARVE + six Byzantine arms
#   t0       n=2 t=0      PLAIN only (the spec's smallest deployment;
#                         at n-t = n a manufactured fault wedges the
#                         round everywhere instead of making one process
#                         lag, so LAGGARD/STARVE/Byzantine are out of
#                         model -- the heal rides ordinary loss instead)
#   big      n=7 t=2      PLAIN + LAGGARD + FORGE + MIXED +
#                         WITHHOLD-with-a-laggard, the composed arm two
#                         faults inside t finally admits
test_system_seam_t0: test/test_system_seam.c system.o bkr94acs.o bracha87.o \
                     system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=1 -DTVAL=0 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_big: test/test_system_seam.c system.o bkr94acs.o bracha87.o \
                      system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 -DQCAP=262144u -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

seam-configs: test_system_seam test_system_seam_t0 test_system_seam_big
	./test_system_seam_t0 16
	./test_system_seam_big 16
	./test_system_seam 16

# THE PREMISE-WITHDRAWAL MATRIX.  One build per arm, the M_* pattern: each
# withdraws exactly ONE premise a system.md proof consumes and carries a
# PREDICTED outcome.  A NEGATIVE control predicts NON-failure -- building
# it, running it, and seeing the safety arms stay green IS its result --
# so unlike a mutant its expected exit status is 0.  Expected outcomes are
# recorded in the source header; run them by hand, not from `check`.
#
#   W_A4_PARTITION  control.  A4 (eventual delivery) withdrawn for one
#                   process, whole run.  4048/0 at 16 seeds.
#   W_A6_PIN0       control.  A6 pinned SHUT (toleranceElapsed = 0); the
#                   mute arm's cohort wedges and abandons, which the arm
#                   asserts.  12643/0 at 16 seeds.
#   W_A6_PIN1       control.  A6 pinned OPEN (toleranceElapsed = 1).
#                   16095/0 at 16 seeds.
#   W_A5_NOINFER    control.  A5 (the O1 inference) withdrawn, the
#                   indication left standing; liveness is lost nearly
#                   everywhere, which the arm asserts.  641/0 at 16
#                   seeds, and slow (MAXTICKS stalls).
#   W_A9_SYBIL      FALSIFYING.  A9 (ingress attribution) withdrawn for
#                   the evidence this layer records.  464 FAILURES at 16
#                   seeds -- L5's release safety and R4's floor.
test_system_seam_W_A4_PARTITION: test/test_system_seam.c system.o bkr94acs.o \
                                 bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_A4_PARTITION -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_A6_PIN0: test/test_system_seam.c system.o bkr94acs.o \
                            bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_A6_PIN0 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_A6_PIN1: test/test_system_seam.c system.o bkr94acs.o \
                            bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_A6_PIN1 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_A5_NOINFER: test/test_system_seam.c system.o bkr94acs.o \
                               bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_A5_NOINFER -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_A9_SYBIL: test/test_system_seam.c system.o bkr94acs.o \
                             bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_A9_SYBIL -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

# TRANCHE 2 (2026-07-25).  Same discipline, and three of the eight carry a
# configuration beside their -D because the premise IS a configuration or the
# scenario costs two faults:
#
#   W_SERVE_CAP0    FALSIFYING.  The SERVE floor withdrawn -- the cap read to
#                   zero, so no recovery leg is ever born.  211 FAILURES at
#                   16 seeds: B, C, the posture, F's structural arm.  The
#                   safety arms stay silent, which is half the statement.
#   W_SERVE_ROTDROP FALSIFYING as briefed; ABSORBED as measured.  The serve
#                   walk's rotation withdrawn (the cursor pinned to the front
#                   of the duty order) under a forged-want flood, n=7 t=2.
#                   14049/0 -- see the header: O1's linkage bounds a
#                   solicitor to ONE duty, so a flood cannot crowd a cap of t.
#   W_SERVE_ROTOK   the same flood with the rotation INTACT: the arm's
#                   negative control.  14049/0, the heal completing.
#   W_R2C_SILENT    control.  R2c withdrawn (a decided process goes
#                   send-silent for the round it closed).  12999/0 at 16
#                   seeds, two ACCEPTED strands -- the predicted expiry.
#   W_REACH_WSHRINK control, a SIZING report.  The REACH proviso withdrawn by
#                   configuration (w = 1) with the window made to BIND by a
#                   withholder, n=7 t=2.  14073/0, 14 evictions; the strand
#                   the brief predicted does NOT appear -- the heal outruns
#                   the window, exactly as the A6 pins found from the other
#                   side.  Read it as a SIZING report, never as an L1 red.
#   W_L2_NOBYTEMATCH FALSIFYING.  C6's byte-matching clause withdrawn.  4
#                   FAILURES at 16 seeds -- E and the fabrication arm, at
#                   BYZ-MIXED seeds 6 and 16.
#   W_L2_NOREARM    FALSIFYING.  C6's re-arm clause ALONE withdrawn (not the
#                   void clause -- that is M_SEAM_NOVOID).  2 FAILURES at 16
#                   seeds, at BYZ-MIXED seed 5.
#   W_I10_WRONGARTIFACT FALSIFYING.  I10's caller half withdrawn (the close
#                   stores the standing candidate, not what it consumed).
#                   16 FAILURES at 16 seeds, every machine conjunct clean.
test_system_seam_W_SERVE_CAP0: test/test_system_seam.c system.o bkr94acs.o \
                               bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_SERVE_CAP0 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_SERVE_ROTDROP: test/test_system_seam.c system.o bkr94acs.o \
                                  bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 -DQCAP=262144u \
	  -DW_SERVE_ROTDROP -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_SERVE_ROTOK: test/test_system_seam.c system.o bkr94acs.o \
                                bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 -DQCAP=262144u \
	  -DW_SERVE_ROTOK -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_SERVE_YIELD: test/test_system_seam.c system.o bkr94acs.o \
                                bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 \
	  -DW_SERVE_YIELD -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_SERVE_YIELDFLOOR: test/test_system_seam.c system.o bkr94acs.o \
                                 bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 \
	  -DW_SERVE_YIELDFLOOR -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_SERVE_WIRE: test/test_system_seam.c system.o bkr94acs.o \
                               bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 \
	  -DW_SERVE_WIRE -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_SERVE_NORESUME: test/test_system_seam.c system.o bkr94acs.o \
                                   bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 \
	  -DW_SERVE_NORESUME -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_R2C_SILENT: test/test_system_seam.c system.o bkr94acs.o \
                               bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_R2C_SILENT -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_REACH_WSHRINK: test/test_system_seam.c system.o bkr94acs.o \
                                  bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=6 -DTVAL=2 -DQCAP=262144u -DWENC=0 \
	  -DW_REACH_WSHRINK -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_L2_NOBYTEMATCH: test/test_system_seam.c system.o bkr94acs.o \
                                   bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_L2_NOBYTEMATCH -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_L2_NOREARM: test/test_system_seam.c system.o bkr94acs.o \
                               bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_L2_NOREARM -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_W_I10_WRONGARTIFACT: test/test_system_seam.c system.o \
                                      bkr94acs.o bracha87.o system.h \
                                      bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_I10_WRONGARTIFACT -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

# TRANCHE 4 (2026-07-25).  C6's COMPLETION-VOID clause, the register's one
# C6 clause without a matched red.
#
#   W_L2_NOCLOSEVOID FALSIFYING.  The glue's own successful close no longer
#                   voids the adopt debt, so an ADOPT output and unconsumed
#                   when this process's own COMPLETE lands closes the NEXT
#                   frontier with the STALE candidate.  DISTINCT from
#                   M_SEAM_NOVOID (the RESET analog, untouched here) and from
#                   W_L2_NOREARM.  PLAIN + LAGGARD + the six Byzantine arms;
#                   see the source header for the counts.
test_system_seam_W_L2_NOCLOSEVOID: test/test_system_seam.c system.o bkr94acs.o \
                                   bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DW_L2_NOCLOSEVOID -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

seam-premises: test_system_seam_W_A4_PARTITION test_system_seam_W_A6_PIN0 \
               test_system_seam_W_A6_PIN1 test_system_seam_W_A5_NOINFER \
               test_system_seam_W_A9_SYBIL test_system_seam_W_SERVE_CAP0 \
               test_system_seam_W_SERVE_ROTDROP test_system_seam_W_SERVE_ROTOK \
               test_system_seam_W_SERVE_YIELD test_system_seam_W_SERVE_YIELDFLOOR \
               test_system_seam_W_SERVE_WIRE test_system_seam_W_SERVE_NORESUME \
               test_system_seam_W_R2C_SILENT test_system_seam_W_REACH_WSHRINK \
               test_system_seam_W_L2_NOBYTEMATCH test_system_seam_W_L2_NOREARM \
               test_system_seam_W_I10_WRONGARTIFACT \
               test_system_seam_W_L2_NOCLOSEVOID
	./test_system_seam_W_A4_PARTITION 8
	./test_system_seam_W_A6_PIN0 8
	./test_system_seam_W_A6_PIN1 8
	./test_system_seam_W_A5_NOINFER 8
	-./test_system_seam_W_A9_SYBIL 8
	-./test_system_seam_W_SERVE_CAP0 8
	./test_system_seam_W_SERVE_ROTDROP 8
	./test_system_seam_W_SERVE_ROTOK 8
	-./test_system_seam_W_SERVE_YIELD 8
	./test_system_seam_W_SERVE_YIELDFLOOR 8
# 16 seeds, not 8: the BYZ-MIXED coverage arm needs them to find its
# subject, and a control that exits non-zero on a lapsed coverage arm
# reports a failure it does not have.
	./test_system_seam_W_SERVE_WIRE 16
	-./test_system_seam_W_SERVE_NORESUME 8
	./test_system_seam_W_R2C_SILENT 8
	./test_system_seam_W_REACH_WSHRINK 8
	-./test_system_seam_W_L2_NOBYTEMATCH 8
# 16 seeds at 8% LOSS, not the default sweep: this arm reds at ONE seed and
# the relocation described in its header moved that seed to 40, past any
# sample the matrix draws.  At 8% the same red reproduces ten times over,
# and the clean build is 0 failures at that rate -- so the loss is the
# reproduction, not the cause.
	-./test_system_seam_W_L2_NOREARM 16 8
	-./test_system_seam_W_I10_WRONGARTIFACT 8
	-./test_system_seam_W_L2_NOCLOSEVOID 8

# ------------------------------------------------------------------
# STAGE 2, TRANCHE 3 (2026-07-25): SCHEDULES.
# ------------------------------------------------------------------
#
# ADVERSARIAL SCHEDULER POLICIES.  Every sweep above delivers by ONE
# policy -- a uniform-random pop.  The Model says schedules are arbitrary
# and the safety lemmas quantify over all of them, so each -DSCHED_*
# build replaces the pop CHOICE and nothing else (same queue, same loss
# draw, same scenarios, same checks; one schedule draw per pop either way,
# so a seed names the same loss pattern under every policy).  Run over the
# default scenario sweep at 16 seeds.
#
#   SCHED_LIFO     newest queued wire first -- maximal reordering.
#                  37557/0 at 16 seeds, and the low count is a FINDING:
#                  LIFO shrinks the agreed subsets (3.156 members of 4
#                  against 3.692 elsewhere), spending R4's tolerance with
#                  the SCHEDULE instead of with a fault.  Adoptions rise
#                  from 521 to 2584 and everything still closes.
#   SCHED_FIFO     oldest first -- the degenerate in-order schedule, a
#                  coverage endpoint and not an adversary.  42677/0.
#   SCHED_STARVE1  one process's INBOUND wires always delivered LAST
#                  (victim = argv[3], default the laggard's index).
#                  Delay, not loss: nothing is dropped, A4 stands, and the
#                  run must still converge.  43193/0, and FASTER than the
#                  uniform policy.
#   SCHED_KINDFLIP leg and exchange traffic ahead of ACS tails --
#                  carrier-priority inversion.  42523/0, and it produces
#                  one LAGGARD accepted strand the uniform policy never
#                  reaches: the possession indication rides the ACS tails,
#                  so deferring them thins that carrier.
#
# ZERO failures, ZERO stalls, and every classification an ACCEPTED strand
# under all four; the safety arms never move.
test_system_seam_SCHED_LIFO: test/test_system_seam.c system.o bkr94acs.o \
                             bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSCHED_LIFO -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_SCHED_FIFO: test/test_system_seam.c system.o bkr94acs.o \
                             bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSCHED_FIFO -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_SCHED_STARVE1: test/test_system_seam.c system.o bkr94acs.o \
                                bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSCHED_STARVE1 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_SCHED_KINDFLIP: test/test_system_seam.c system.o bkr94acs.o \
                                 bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DSCHED_KINDFLIP -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

seam-sched: test_system_seam_SCHED_LIFO test_system_seam_SCHED_FIFO \
            test_system_seam_SCHED_STARVE1 test_system_seam_SCHED_KINDFLIP
	./test_system_seam_SCHED_LIFO 16
	./test_system_seam_SCHED_FIFO 16
	./test_system_seam_SCHED_STARVE1 16
	./test_system_seam_SCHED_KINDFLIP 16

# THE LOSS SWEEP.  The DEFAULT build, no new -D: the drop percentage is
# already argv[2], and it overrides the per-scenario default for EVERY
# scenario.  Safety holds at every level; liveness degrades by
# CLASSIFICATION (accepted strands), never by safety.
#
#    0%  42683/0   4%  42807/0   8%  42553/0
#   12%  42379/0  15%  42277/0  20%  42245/7  -- the edge
#
# The first accepted strand OUTSIDE the STARVE positive control appears
# at 12% (one LAGGARD seed).  The highest fully-green level is 15%; at
# 20% two STARVE seeds red on the POSTURE and C arms only, with D, E, F's
# unsafe arm and H silent -- a SIZING boundary (the abandonment budget S
# against the loss rate), not a safety one.  See the source header.
seam-loss: test_system_seam
	./test_system_seam 16 0
	./test_system_seam 16 4
	./test_system_seam 16 8
	./test_system_seam 16 12
	./test_system_seam 16 15

# EXHAUSTIVE DELIVERY ENUMERATION at the smallest deployment the spec
# admits (n=2, t=0; the source #errors at any other shape).  Depth-first
# over which queued wire is delivered next, exhaustively to ENUMDEPTH,
# each leaf completing under the uniform policy; a node is reached by
# RE-EXECUTING its prefix (the harness is a pure function of (seed, choice
# string) at zero loss), so nothing forks and nothing leaks.  Loss is NOT
# enumerated -- a per-wire delivered/lost choice multiplies the tree by
# 2^(wires pushed).  Every leaf is a WHOLE RUN and gets assertRun entire
# plus the state-equivalence oracle.  Leaf counts are ASSERTED in-program.
#
#   ENUM1  ROUNDS=1 depth 6   29487680 leaves, 0 failures, ~23m
#   ENUM2  ROUNDS=2 depth 5    1593008 leaves, 0 failures, ~2m
test_system_seam_ENUM1: test/test_system_seam.c system.o bkr94acs.o \
                        bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=1 -DTVAL=0 -DROUNDS=1 \
	  -DSCHED_ENUM -DENUMDEPTH=6 -DENUMLEAVES=29487680 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

test_system_seam_ENUM2: test/test_system_seam.c system.o bkr94acs.o \
                        bracha87.o system.h bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -DNENC=1 -DTVAL=0 -DROUNDS=2 \
	  -DSCHED_ENUM -DENUMDEPTH=5 -DENUMLEAVES=1593008 -I. -o $@ \
	  test/test_system_seam.c system.o bkr94acs.o bracha87.o

seam-enum: test_system_seam_ENUM1 test_system_seam_ENUM2
	./test_system_seam_ENUM2 1
	./test_system_seam_ENUM1 1

# ------------------------------------------------------------------
# THE IN-TREE MACHINE-MUTANT TIER (2026-07-25).
# ------------------------------------------------------------------
#
# The seam's M_* mutants prove the composed instrument's checks have
# teeth.  This is the same currency one layer down: the single-seat
# instruments (test_system_invariant.c's I1-I11 falsifier, its -DHRTWIN
# twin drive, and test_system.c's contract sections) had never been
# shown to CATCH a broken machine, because every red they could produce
# needs a mutated system.c and instruments do not touch it.
#
# test/machineMutants.sh applies nine anchored mutations to a SCRATCH
# COPY of system.c (the anchor asserted to match exactly once first),
# rebuilds both instruments against each, and asserts the designated
# oracle fires.  system.c is never written -- the script checksums it
# before and after and aborts on any difference.  Runnable standalone
# (sh test/machineMutants.sh) and idempotent; artifacts and per-mutant
# logs land in machineMutants.d/, which clean removes.  The inventory
# and the expected oracle per mutant are in the script's header; the
# results are recorded in test_system_invariant.c's header.
machine-mutants:
	sh test/machineMutants.sh

check: test_bracha87 test_bkr94acs test_predicates test_bracha87_blackbox test_bkr94acs_blackbox test_system
	./test_bracha87
	./test_bkr94acs
	./test_predicates
	./test_bracha87_blackbox
	./test_bkr94acs_blackbox
	./test_system

clean:
	rm -f bracha87.o bkr94acs.o system.o
	rm -f example_bracha87Fig1 example_bkr94acs example_system
	rm -f test_bracha87 test_bkr94acs test_predicates test_bracha87_blackbox test_bkr94acs_blackbox test_system test_system_invariant test_system_invariant_hrtwin test_system_seam test_system_seam_t0 test_system_seam_big
	rm -f test_system_seam_W_A4_PARTITION test_system_seam_W_A6_PIN0 test_system_seam_W_A6_PIN1 test_system_seam_W_A5_NOINFER test_system_seam_W_A9_SYBIL
	rm -f test_system_seam_W_SERVE_CAP0 test_system_seam_W_SERVE_ROTDROP test_system_seam_W_SERVE_ROTOK test_system_seam_W_R2C_SILENT test_system_seam_W_REACH_WSHRINK test_system_seam_W_L2_NOBYTEMATCH test_system_seam_W_L2_NOREARM test_system_seam_W_I10_WRONGARTIFACT test_system_seam_W_L2_NOCLOSEVOID
	rm -f test_system_seam_SCHED_LIFO test_system_seam_SCHED_FIFO test_system_seam_SCHED_STARVE1 test_system_seam_SCHED_KINDFLIP test_system_seam_ENUM1 test_system_seam_ENUM2
	rm -rf machineMutants.d

# the .psu are dtc's intermediate output, left behind by `make rules`;
# nothing reads them afterward, and removing them regenerates nothing
clobber: clean
	rm -f bracha87Fig1.psu bracha87Fig3.psu bracha87Fig4.psu bkr94acs.psu system.psu
