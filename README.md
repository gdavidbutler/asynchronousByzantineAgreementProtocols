# asynchronousByzantineAgreementProtocols

Generated with Claude Code (https://claude.ai/code)

## What this is

Two C89 libraries, with no I/O of their own, for agreeing on messages in an asynchronous network of n processes of which up to t may be Byzantine, n > 3t. `bracha87Fig1.h` is Bracha's Figure 1: one seat shares a message with the others. `bkr94acs.h` is BKR94's Agreement[Q] over Bracha's Figures 4, 3 and 1: every seat shares a message with every other and all agree on a subset of them. Each library is a state machine for one seat. Your program feeds it every message that arrives, sends what the actions it returns name, and sweeps it on a clock of your choosing. The headers are the reference. This file says why things are the way they are.

A seat is one process's view: Bracha's "process p", BKR94's "player P_i". The caller is the program that owns the wire, the threads and the memory and feeds the seat. A sweep is one pass of the caller's retry over everything the seat has sent and not yet seen answered, and it is the only clock the libraries have.

Try it:

```
make
./example_bracha87Fig1 -s 7 -l 40 5 0 "the message"
./example_bkr94acs -s 7 -l 20 -b 3 -x 2 4 1 alpha bravo charlie liar
```

The first is five seats over a network losing 40 percent of deliveries, seat 0 sharing a message; it reports the message each seat holds at the end. The second is four seats, one of them Byzantine and sending two values, losing 20 percent; it reports the agreed subset and its messages as each honest seat's library hands them back. Both examples print sweep counts and delivery counts beside the messages, because the cost of agreement is visible on the wire and should be.

## The papers

This repository is written for a reader who has these five papers on their desk. Without them it will not make much sense: the code is organized by their figures and the terminology is theirs. The in-tree extracts (`*.txt`) carry the passages the code and this file rest on, but they are extracts, not substitutes -- a little light reading first is the way in, and for a reader new to the subject the order below is the reading order.

- M. J. Fischer, N. A. Lynch, M. S. Paterson, "Impossibility of Distributed Consensus with One Faulty Process," *Journal of the ACM* 32(2), 374-382 (1985). The impossibility everything here answers. Extract `FLP82.txt`, named for the technical report Ben-Or cites.
- Michael Ben-Or, "Another Advantage of Free Choice: Completely Asynchronous Agreement Protocols (Extended Abstract)," PODC '83, 27-30. The escape by randomization, and the protocol beneath Bracha's Figure 4 (Bracha names it with one other). Extract `BenOr83.txt`.
- Gabriel Bracha, "Asynchronous Byzantine Agreement Protocols," *Information and Computation* 75, 130-143 (1987). Extract `Bracha87.txt`.
- Michael Ben-Or, Boaz Kelmer, Tal Rabin, "Asynchronous Secure Computations with Optimal Resilience (Extended Abstract)," PODC '94, 183-192. Extract `BKR94ACS.txt`.
- J. H. Saltzer, D. P. Reed, D. D. Clark, "End-to-End Arguments in System Design," *ACM Transactions on Computer Systems* 2(4), 277-288 (1984). Decides where a correctness function lives. Extract `SRC84.txt`.

## One seat

Every figure in the papers is written from one process's seat: "(By process p)". So is every line here. A seat holds what the figure says a process holds, counts what arrives, and acts on counts. It never waits on a named process, never reads absence as information, and keeps no clock. That is the asynchronous model of FLP, and it is the one thing the code is never allowed to forget: a dead seat and a slow one look the same, forever. Sweeps are a local count, nothing more; no row that decides a value reads one.

Figure 1 is three message kinds: a broadcaster's `(initial, v)`, and every seat's `(echo, v)` and `(ready, v)` of it. A seat echoes when it has the initial, readies when enough have echoed, and accepts v when enough have readied. BKR94's Agreement[Q] runs one binary agreement per seat, deciding whether that seat is in the subset; each agreement is Bracha's Figure 4, phases of three rounds; each round is Figure 3, a broadcast by every seat and a wait for n - t of them to be validated; each broadcast is Figure 1. Each seat's message is one more Figure 1 broadcast, and Q(j) is a seat's accept of it.

## Decision tables, not code

The figures are short, and their difficulty is not in their steps. It is in the first `if` a programmer writes, which orders everything after it. So the figures are not coded. They are transcribed, line by line, into decision tables for `../decisionTableCompiler`, one `.dtc` file per figure and one more for a row whose inputs touch nothing else in its figure, and the compiler decides the order of the tests. Beside the figure's rows, written in the paper's words, sit rows in two other vocabularies: the caller's, which say when a satisfied step is taken, since a figure's "wait until" names what enables a step and the papers have no moments; and the retry's, which say what is re-sent. The compiler composes them, and a name one group produces and another reads costs no test at run time.

Each `.dtc` compiles alone, in a fraction of a second, into a dispatch the C runs at its own site; across files the tables share one or two names, which are C locals between those sites. A joint compile of all of them, with a bridge of their union, gives one dispatch of depth 54 and three million lines after minutes of compiling, and no site in the C has all of its inputs in hand at once, so it is not the build. The generated snippets are kept in the tree so the libraries build without the compiler; `make rules` regenerates them after a `.dtc` changes. Read the `.dtc` files for the rules. What the C adds around the rows is counting, copying, refusing, and two loops the rows cannot express: Figure 1's "for some v" over the values in hand, and Figure 3's revisiting of stored messages that a validation may have given a witness.

## What is added, and where the figures are departed from

The papers assume a reliable message system. A real network loses messages, so three things stand between the papers and the wire, and each rests on one sentence:

- **BPR, Bracha Phase Retry.** Re-send what the figure sent until the figure no longer owes it. The retry lives at the protocol endpoint, by the end-to-end argument (SRC84): only the endpoint knows what is still owed, and a lower layer could at most make it cheaper. A re-send to a seat retires on that seat's own word about the thing sent, and on nothing else: in an asynchronous system nothing another seat says, and nothing the sender infers, tells it a message arrived. No message of the figure carries such a word, since a seat may echo on the others' echoes without ever holding the initial, so three bits of the caller's ride the messages BPR already re-sends. A seat marks its echo and its ready with "I hold your initial and what came beside it", and the initiator re-sends its initial, and what rides beside, to the seats not so marked. A seat marks its ready with "I have accepted", and toward each seat whose marked ready it holds with "I hold your accept"; it re-sends its ready only to the seats not yet recorded as having accepted, and a ready arriving without that second mark re-opens one more re-send to its sender, which carries the mark. The echo alone retires on the seat's own accept, since past it the readies carry the rest. When every seat is recorded and none is owed that one more, the re-send retires, which is reachable under loss and not guaranteed; everything stops at abandon.
- **Patience.** Do not spend the Byzantine budget against an honest laggard. The papers go on at n - t, since t faulty processes can keep any count from reaching n. Patience is how many sweeps a seat first waits for all n before it goes on at n - t, because the one it would be writing off may only be late, and a seat written off is one the subset will not hold.
- **Abandon.** The paper's whole treatment of halting is one unproven clause: "However, this can be easily accomplished." Abandon is that clause made real. A seat counts its sweeps from its start and at the abandon count it is done, with everything it holds; it is the seat's only exit, and a caller that gives no count keeps the seat until it lets the seat go itself. To every other seat an abandoned one is silence, and silence is what a faulty process looks like, so an abandon is paid out of t. Patience spends sweeps, abandon spends a fault, which is why the one sits above the other.

Three places depart from the figures as printed, all on purpose, and a reader judging faithfulness should know them before the rows:

- **Step 1 on the `(initial, v)` waits for what the caller sent beside it.** The figure gives three ways into step 1: the initial, enough echoes, enough readies. A caller that sends a message of its own beside the initial, a signature over the value for one, has the first wait until that message is held and validated here: a seat echoes nothing on the broadcaster's word alone without the caller's validation, which is the one thing the library cannot do for it. The other two ways in are the others' word, and each of them validated before it echoed; they stand as printed, as do steps 2 and 3, and so does Theorem 1: a faulty broadcaster that sends one seat one value and the others another is accepted at that seat under the value the others accepted, on their readies, or nowhere. A caller with nothing to send beside says so at Init, by the beside flag, because the instance cannot infer it: a caller that sends nothing beside and one whose message is late look the same from inside, and absence is not information. With the flag clear the figure runs as printed, which is how the agreements' own broadcasts are made.
- **Agreement[Q]'s step 3 waits for the values.** The paper outputs the subset when all n agreements have completed. Here the output is the subset and the value of every seat in it, so the step waits until this seat has accepted each of them. An agreement that output 1 had an honest seat enter 1, which had accepted the value, and Lemma 3 brings that accept to every other seat.
- **Agreement[Q]'s step 2 enters its 0s at n - t decisions of 1, not at the paper's 2t + 1.** The paper bounds the subset by n - t >= 2t + 1, and the two agree only at n = 3t + 1; above it the paper's count admits a subset of 2t + 1 members, below the n - t the consumer of this library reports as its floor. The row reads the honest count, n - t, which is the most a seat may wait for, and waits its patience past it. At t = 0 that count is n, so a subset is every player or none, where 2t + 1 would let a seat enter 0 for players it had merely not yet heard from.

Four readings the figures do not give are marked in the rows where they land: a tie in "majority value" keeps the value; where N keeps value_p, a validator reads another process's value_p as that process's own validated message of the round before, since the paper gives N the round and the message set alone; the sample a round consumes is whatever is validated when it turns, at least n - t and up to n, which is what patience buys; and a decided agreement broadcasts one more phase, which the proofs need, and then stops. An agreement that reaches the caller's round ceiling undecided abandons its seat. The ceiling is the caller's bet: Bracha's Theorem 3 puts the expected number of phases at a constant for t near the square root of n and exponential for t a fraction of n.

## What the papers assume and this library does not provide

The papers run on assumptions that are not protocol and so are not here. They are the caller's, and a deployment that does not meet them has not deployed the papers. Bracha states his model in one sentence (Section 1, `Bracha87.txt`):

> We assume a reliable message system in which no messages are lost or generated. Each process can directly send messages to any other process, and can identify the sender of every message it receives.

Of its four clauses this library supplies one. "No messages are lost" is BPR, and BPR supplies it only over a fair-loss network: a message re-sent on every sweep must eventually arrive. A link that never delivers is not loss, it is a partition, and the only thing the library can do with it is abandon. The other three are the caller's wire:

- **"No messages are generated."** A message must come from the seat it names, unaltered. The seat trusts the sender byte on every arrival and the bytes beside it; a forged sender or a changed value is a message the model says cannot exist. Authenticated point-to-point channels are how a deployment makes that true, and nothing here checks it. The library does absorb its own duplicates: a re-sent message counts once.
- **"Each process can directly send messages to any other process."** Full connectivity. The seat names recipients and the caller reaches them; a seat it cannot reach is, to the protocol, silent.
- **"Can identify the sender of every message it receives."** The same authentication read from the receiving side. Every count in every figure is a count of distinct senders, so a sender a seat cannot identify is a count it cannot keep.

Of BKR94 only Section 4 is here, which is why its extract is `BKR94ACS.txt`: Agreement[Q] runs on the same reliable channel as Bracha, a message guaranteed to arrive after an arbitrary finite delay, and asks nothing more of it. The secure channel of the paper's Section 2 belongs to its secret sharing, which is not here. Agreement[Q] agrees on a set of players for whom Q is 1, and leaves Q to the application; that the agreed set carries messages is this tree's construction, not the paper's.

Q carries two assumptions of its own (Section 4): every honest player is eventually assigned Q = 1, and when a player is assigned, every player eventually learns it. Here Q(j) is one thing only, a seat's accept of the value seat j broadcast by Figure 1, and the two assumptions are then Bracha's Lemmas 4 and 3. It is also what makes the subset a subset of messages: Theorem 1 holds a faulty seat to one value at every seat that accepts it, so every correct seat that outputs the subset outputs the same value for each seat in it. A message that reaches the seats any other way, beside a broadcast or outside the library, is not covered by any of this: a Byzantine seat can tell each seat something different, and nothing agrees on it.

And the papers' bound, n > 3t, is the caller's choice of t. The library refuses the rest, and sizes everything else by bytes: 256 seats, 255 rounds per agreement, 256 bytes of a Figure 1 value, because more of any of them is prohibitive in a deployment and a byte carries each on the wire. The value's limit is the one that bites, and it is there because of volume: every seat sends a broadcast's value again in its echo and in its ready, to every seat, and an agreement is rounds of such broadcasts by every seat. Anything practical is larger than 256 bytes, and the beside flag below is how it is agreed all the same. A seat's memory is n squared times the rounds times a Figure 1 instance; the header gives the numbers, and at the limits they are gigabytes.

## Two parameters of a seat, and why

`bkr94acsInit` takes two things a caller might not expect to have to supply.

**The coin** is Bracha's. Figure 4 ends a phase it cannot decide with "coin_toss (0 or 1 with probability 1/2)", and Theorem 2's termination with probability 1 rests on the adversary, who schedules every message and knows every past toss, not knowing the next one. A library has no randomness of its own and no secret to make from, and whatever it hard-coded the adversary could read in the source. So the coin is a callback with the caller's context: the caller chooses the source, local to the seat, and owns its unpredictability. The tests and the examples pass deterministic coins, because a run must repeat, and that costs them only the paper's termination guarantee, which they do not test.

**The beside flag** is this tree's, and it is how a large message is agreed without being carried through the rounds. The message travels once, beside the `(initial, v)` of a small value that commits to it, a hash for one. A seat does not echo the value on the broadcaster's initial until it has validated what came beside it against the value, and on the others' echoes or readies it echoes as printed, since each of them validated first, so the agreement on the small value is an agreement on the large message, at the cost of one send of it and not of every echo and ready. What is beside, to each seat its own if the caller likes, and how it is validated are the caller's; the library holds the initial, in whichever order the two arrive, and takes the caller's word that the validation passed. But it is a wait, and a caller with nothing to send beside would have every seat wait forever. The flag says which caller this is, at both libraries' Init, since nothing an instance sees can tell them apart. With it clear, a value's `(initial, v)` is enough and a BESIDE is refused; with it set, step 1 on the initial waits for the BESIDE, and a BESIDE ahead of the initial is refused, since the caller cannot yet have validated it. The agreements' own broadcasts carry nothing beside them and never wait.

## What is proven, and what is not

The papers are forty years mature and are not tested here. What is tested is that the rows were implemented as written, the caller's rows included, which is a different thing from the figures and is why the departures above are stated. The test of record for each library is a shadow: one seat, from its own view, fed everything an adversary can send it, with a record of what was fed and every return checked against the rows read as statements about that record. `test/shadow.c` is Figure 1's. `test/acsShadow.c` is the seat's; it mirrors the Figure 1 layer, the value broadcasts included, through `bracha87Fig1.h` itself and reads the rows above it from its own counts and validated sets, with scripted scenes for the states a random feed seldom reaches. `test/bracha87Fig1.c` and `test/bkr94acs.c` run several honest seats over a queue with seeded loss, in order, to reach what one seat cannot, at t = 0 and above; and `test/bkr94acs.c` can starve some seats of the readies of one seat's value until they have entered its agreement, so that honest inputs split on that one agreement and a tie, a kept value and the coin are reached across seats, which no all-ones run does.

## Building

```
make          # the two libraries, as objects, and the two examples
make check    # the tests
make rules    # regenerate the dispatch snippets; needs ../decisionTableCompiler
```

The libraries are LGPLv3; see the headers.
