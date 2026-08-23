CFLAGS = -I. -Os -g
DTC = ../decisionTableCompiler/dtc
AWK = awk

all: example_bracha87Fig1 example_bkr94acs

bracha87.o: bracha87.c bracha87.h bracha87Fig1Rules.c bracha87Fig3Rules.c bracha87Fig4Rules.c
	$(CC) $(CFLAGS) -c -o $@ bracha87.c

bkr94acs.o: bkr94acs.c bkr94acs.h bracha87.h bkr94acsRules.c
	$(CC) $(CFLAGS) -c -o $@ bkr94acs.c

# The *Rules.c dispatch snippets are TRACKED SOURCE, not build output.
#
# Each one is generated: dtc co-compiles a .dtc rule table with its
# *ToC.dtc bridge to a .psu dispatch, and psu.awk translates that to the
# snippet an entry point #includes.  They are committed anyway, and no
# rule here remakes them -- nothing in the ordinary graph runs dtc, and
# neither clean nor clobber removes them.
#
# What forces that is the cost of the search.  dtc searches for a
# depth-minimal dispatch, and the search grows with the table; whoever
# edits a .dtc should pay that cost once, and a person building the
# tree should not pay it at all.  Committing also makes the tree stand
# alone, since dtc lives in a SECOND repository.
#
# Regeneration is the deliberate act below, after editing a .dtc.  The
# diff it produces is what gets reviewed and committed, and `make rules`
# followed by `git diff --exit-code` is the check that no .dtc edit went
# unbuilt (dtc is deterministic, so a nonempty diff means a real one).
# The per-table targets exist for the same asymmetry: one table's edit
# must not re-run the full search for the others.
#
# The .psu stays untracked: a machine trace whose only consumer is
# psu.awk.  Read the .dtc for the rules, the snippet for the C.

rules: rules-bracha87Fig1 rules-bracha87Fig3 rules-bracha87Fig4 rules-bkr94acs

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

example_bracha87Fig1: example/bracha87Fig1.c bracha87.o bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ example/bracha87Fig1.c bracha87.o

example_bkr94acs: example/bkr94acs.c bkr94acs.o bracha87.o bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ example/bkr94acs.c bkr94acs.o bracha87.o

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

test_schedules: test/test_schedules.c bkr94acs.o bracha87.o bkr94acs.h bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_schedules.c bkr94acs.o bracha87.o

check: test_bracha87 test_bkr94acs test_predicates test_bracha87_blackbox test_bkr94acs_blackbox test_schedules
	./test_bracha87
	./test_bkr94acs
	./test_predicates
	./test_bracha87_blackbox
	./test_bkr94acs_blackbox
	./test_schedules smoke

# The schedule explorer is a DELIBERATE act, like `rules` above, and for
# the same reason: cost.  `check` runs only its measured-subsecond smoke
# subset; the configs that walk a whole state graph run minutes and are
# asked for by name.  Their bounds are printed on every run -- exhaustive
# means exhaustive WITHIN those bounds and nothing more.
schedules: test_schedules
	./test_schedules all

# A suite that passes proves something only if it would FAIL on a
# machine that is wrong.  This applies single anchored defects to
# scratch copies of bracha87.c and bkr94acs.c and asks whether a NAMED
# check in the battery goes red -- or records, with the argument, that
# the defect is invisible to the battery's power.  It builds and runs a
# whole battery per defect, so it is a deliberate act like `rules` and
# `schedules` above, run by name, never from `check`.  It writes only
# under mutantWork and leaves the sources byte-identical, which it
# checksums and prints on both sides of the run.
mutants: test/mutants.sh
	sh test/mutants.sh

clean:
	rm -f bracha87.o bkr94acs.o
	rm -f example_bracha87Fig1 example_bkr94acs
	rm -f test_bracha87 test_bkr94acs test_predicates test_bracha87_blackbox test_bkr94acs_blackbox test_schedules
	rm -rf mutantWork

# the .psu are dtc's intermediate output, left behind by `make rules`;
# nothing reads them afterward, and removing them regenerates nothing
clobber: clean
	rm -f bracha87Fig1.psu bracha87Fig3.psu bracha87Fig4.psu bkr94acs.psu
