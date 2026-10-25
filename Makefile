CFLAGS = -I. -Os -g
DTC = ../decisionTableCompiler/dtc -q

all: bracha87Fig1.o bkr94acs.o example_bracha87Fig1 example_bkr94acs

clean:
	rm -f bracha87Fig1.o bkr94acs.o example_bracha87Fig1 example_bkr94acs test_bracha87Fig1 test_shadow test_bkr94acs test_acsShadow

clobber: clean
	rm -f bracha87Fig1.psu bracha87Fig1Ready.psu bracha87Fig3.psu bracha87Fig4.psu bkr94acs.psu

# The dispatch snippets are generated: dtc compiles each rule file alone
# with its bridge to a .psu, and psu.awk translates that to C. The
# snippets are kept so the library builds without dtc, and nothing in
# the ordinary graph runs dtc; regenerating them is this deliberate
# act, after editing a .dtc.
rules:
	$(DTC) bracha87Fig1.dtc bracha87Fig1ToC.dtc > bracha87Fig1.psu
	awk -f psu.awk bracha87Fig1.psu > bracha87Fig1Rules.c
	$(DTC) bracha87Fig1Ready.dtc bracha87Fig1ReadyToC.dtc > bracha87Fig1Ready.psu
	awk -v prefix=rd -f psu.awk bracha87Fig1Ready.psu > bracha87Fig1ReadyRules.c
	$(DTC) bracha87Fig3.dtc bracha87Fig3ToC.dtc > bracha87Fig3.psu
	awk -v prefix=f3 -f psu.awk bracha87Fig3.psu > bracha87Fig3Rules.c
	$(DTC) bracha87Fig4.dtc bracha87Fig4ToC.dtc > bracha87Fig4.psu
	awk -v prefix=f4 -f psu.awk bracha87Fig4.psu > bracha87Fig4Rules.c
	$(DTC) bkr94acs.dtc bkr94acsToC.dtc > bkr94acs.psu
	awk -v prefix=acs -f psu.awk bkr94acs.psu > bkr94acsRules.c

bracha87Fig1.o: bracha87Fig1.c bracha87Fig1.h bracha87Fig1Rules.c bracha87Fig1ReadyRules.c
	$(CC) $(CFLAGS) -c bracha87Fig1.c

bkr94acs.o: bkr94acs.c bkr94acs.h bracha87Fig1.h bracha87Fig3Rules.c bracha87Fig4Rules.c bkr94acsRules.c
	$(CC) $(CFLAGS) -c bkr94acs.c

example_bracha87Fig1: example/bracha87Fig1.c bracha87Fig1.h bracha87Fig1.o
	$(CC) $(CFLAGS) -o example_bracha87Fig1 example/bracha87Fig1.c bracha87Fig1.o

example_bkr94acs: example/bkr94acs.c bkr94acs.h bracha87Fig1.h bkr94acs.o bracha87Fig1.o
	$(CC) $(CFLAGS) -o example_bkr94acs example/bkr94acs.c bkr94acs.o bracha87Fig1.o

test_bracha87Fig1: test/bracha87Fig1.c bracha87Fig1.h bracha87Fig1.o
	$(CC) $(CFLAGS) -o test_bracha87Fig1 test/bracha87Fig1.c bracha87Fig1.o

test_shadow: test/shadow.c bracha87Fig1.h bracha87Fig1.o
	$(CC) $(CFLAGS) -o test_shadow test/shadow.c bracha87Fig1.o

test_bkr94acs: test/bkr94acs.c bkr94acs.h bracha87Fig1.h bkr94acs.o bracha87Fig1.o
	$(CC) $(CFLAGS) -o test_bkr94acs test/bkr94acs.c bkr94acs.o bracha87Fig1.o

test_acsShadow: test/acsShadow.c bkr94acs.h bracha87Fig1.h bkr94acs.o bracha87Fig1.o
	$(CC) $(CFLAGS) -o test_acsShadow test/acsShadow.c bkr94acs.o bracha87Fig1.o

check: test_bracha87Fig1 test_shadow test_bkr94acs test_acsShadow
	./test_bracha87Fig1 4 0 0
	./test_bracha87Fig1 4 0 30
	./test_bracha87Fig1 7 0 50
	./test_bracha87Fig1 1 0 0
	./test_bracha87Fig1 4 1 40
	./test_bracha87Fig1 7 2 50
	./test_bracha87Fig1 4 0 0 8
	./test_bracha87Fig1 7 2 50 8
	./test_shadow 20000
	./test_bkr94acs 4 0 0
	./test_bkr94acs 4 0 30
	./test_bkr94acs 7 0 50
	./test_bkr94acs 1 0 0
	./test_bkr94acs 4 1 40
	./test_bkr94acs 7 2 50
	./test_bkr94acs 1 0 0 3
	./test_bkr94acs 4 0 20 3
	./test_bkr94acs 4 1 0 6 2 0
	./test_bkr94acs 4 1 0 6 2 1
	./test_bkr94acs 6 1 0 6 3 1
	./test_bkr94acs 4 1 30 9 2 1
	./test_acsShadow 20000
