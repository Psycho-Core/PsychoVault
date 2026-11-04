# My Test262 measurement

I wanted a number I could not talk my way out of, so I wrote a runner that walks the Test262 tree and executes every test through my own parser and my own interpreter. Nothing from another engine is involved: the tests are inputs, and all the code that runs them is mine.

## The run

- Corpus: `tc39/test262` at commit `831359a4192ef83b8c50f91363d2ff739fe550f6`.
- Runner: `tools/Psycho_conformance.cpp`, built by `tools/Psycho_build.py` as `pv-conformance`.
- Command: `pv-conformance --test262 corpus/test262 --out test262-results.tsv`.
- Machine: this Linux development machine, one process per test.

| Outcome | Tests |
| --- | ---: |
| Passed | 5,009 |
| Failed an assertion the test made | 1,062 |
| Threw something the test did not expect | 22,672 |
| Crashed my interpreter | 16 |
| Skipped, because the test needs a module, a strictness flag or a harness I do not run | 3,454 |
| Skipped, because the test names a feature my engine does not have | 21,845 |
| Ran in total | 54,058 |

Two ways of reading that, and I want both said:

- **17.4 percent of the tests my engine can attempt pass** - 5,009 of 28,759.
- **9.3 percent of the whole corpus passes** - 5,009 of 54,058.

The skip-unsupported bucket is not a way of hiding failures. It is tests whose front matter names a feature I genuinely do not implement, and the four biggest are Temporal (6,536), generators (3,859), async iteration (3,842) and BigInt (1,306). A test that only *uses* one of those features indirectly is not skipped, so the bucket is narrower than it could have been.

## What the run found, and what I fixed because of it

I ran the corpus and then read the failures rather than the total. Six things came out of it.

**My parser had no `switch` statement.** Not a stub, not a broken one: absent. Any script with a `switch` was a syntax error, and a lot of ordinary page code uses one. I added the statement with fallthrough, a `default` that can sit anywhere, one block scope across the cases, and a `break` that ends the switch and nothing else.

**There was no comma expression.** `(0, eval)` was a parse error, and so was `for (i = 0, j = 3; ...)`. The comma is now its own node, evaluated left to right with the last value as its result, allowed only where the grammar wants a full Expression, so argument lists and array literals still parse as lists.

**There was no `Function` constructor.** It now builds the body and runs it through the same parser that compiles a page's scripts, which is the only way I want two paths to the same language.

**A thrown error was a string, so `instanceof` could not answer for it.** A page that catches an engine error now gets an object with `name` and `message`, and `e instanceof TypeError` is true for the right type and false for the rest. The conversion happens at the one boundary where a value enters JavaScript, so a hundred throw sites did not have to change.

**Running out of stack was a C++ exception.** A runaway recursion escaped my interpreter and reached the shell instead of being something a page could catch. It is a `RangeError` now, and `try { recurse() } catch (e) { e instanceof RangeError }` works.

**`Object.assign(null, ...)` dereferenced a null target.** That was not a wrong answer, it was a segfault, one of the sixteen crashes the run recorded. A null or undefined target is a `TypeError` now, a null source is skipped, and the other crashes are listed per test in the report so I can work through them.

One more defect fell out of the same work: the headless build's four-argument `fetch_url` forwarded a timeout of zero and a size limit of zero, so every request made that way refused its own body with "that response was larger than I will hold in memory". I found it because my new tunnel test asked for a page through a loopback proxy and was told an eleven byte body was too big.

The stack limit needed a second pass. Counting script frames was enough on the 8 MiB thread my Linux tests get and not enough on the 1 MiB thread a Windows exe starts with, so a runtime now asks the platform for the thread's stack and stops at three quarters of it - one rule for Linux, Windows and a sanitizer build whose frames are much fatter. The same hosted run found a use-after-move: a nested `Function` call reallocated the vector of compiled programs while the outer call was still walking it. Both were real, and neither was visible on my machine.

## Before and after

The same corpus, before those six fixes and after them:

- Before: 4,783 passed, 23 crashes, 22,896 unexpected throws.
- After: 5,009 passed, 16 crashes, 22,672 unexpected throws.

That is a 226 test gain and seven fewer crashes. It is also a reminder of the size of what is left: the largest single remaining group is 4,631 tests that die with `TypeError: value is not callable`, which is mostly my `$262.createRealm` being undefined for cross-realm tests plus built-ins I have not written. The next largest is a parser gap around destructuring in parameter lists (2,761 tests hit `identifier expected`).

## What this number is not

- It is not a claim about the web. Test262 measures a language, not a browser, and my engine deliberately lacks Temporal, BigInt, generators, typed arrays, modules and most of `Intl`.
- It is not complete coverage of the corpus semantics. I skip module tests and the strictness flags; my runner cannot force a test to run in strict mode when it did not ask. So the real strict-mode number is not measured yet.
- The sixteen crashes are honest records of my interpreter falling over. On this platform each test runs in its own child process, which is how a crash becomes a line in the report instead of the end of the run.

## Web platform tests

I did not run a WPT subset in this build, and I am not going to quote a number I did not measure.

The reason is concrete rather than a matter of time. WPT's tests drive a page through `testharness.js`, which needs modules, async test registration and a promise queue behaving the way the specification describes. My engine has none of the three yet, so a WPT run today would measure my harness rather than my browser. When my JavaScript work reaches modules and async iteration, the same runner shape here can drive WPT files through my own testharness implementation, written by me, and the number will mean something.

## How to reproduce it

```
python3 tools/Psycho_build.py --compiler gcc --outdir build
curl -sSL https://codeload.github.com/tc39/test262/tar.gz/831359a4192ef83b8c50f91363d2ff739fe550f6 -o test262.tar.gz
tar xzf test262.tar.gz
./build/pv-conformance --test262 test262-831359a4192ef83b8c50f91363d2ff739fe550f6 --out results.tsv
```

The `.github/workflows/Psycho_conformance.yml` job does exactly that on every change to my parser or interpreter and keeps the per-test record as an artifact, so the number in this page can always be re-derived from a run rather than from this sentence.
