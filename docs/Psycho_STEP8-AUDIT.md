# My audit of step 8, security and ad blocking

I built step 8 in two halves. The first half is the HTTPS policy: what a page is allowed to load over plain HTTP, which hosts I have promised to keep on TLS, and how the form body in front of me is protected. The second half is the blocker: my own list syntax, my own matcher, and the counts and reasons that let me see it working.

This page is the audit of both, written after the code compiled and the tests ran, because a green build is not the same thing as a rule that does what it says.

## How I audited it

I asked the questions a person would ask of a browser, not the questions that make my own code look finished:

- Can a site talk me out of HTTPS after it asked for it, and can anyone on the path add that promise?
- What happens to an HTTPS page's plain image, not just its plain script.
- Does a form post on an HTTPS page get refused when the target is plain, and is the refusal visible?
- Which host does a per-site exception actually name?
- Does a rule I cannot evaluate get refused, or quietly dropped?
- Can a pattern with a host anchor match something that merely mentions the host?
- Does the number in my window come from the same decision the network made?
- Does my containment claim match what the operating system is actually doing?

## What was wrong

**A per-site exception compared the wrong host.** My request policy compared the exception list against the host being fetched. An exception for `shop.test` therefore only applied when the request itself went to `shop.test`, which is almost never what an exception is for: the requests I want to excuse are the third-party ones the site makes. My own test caught this after I wrote it as an assertion about the page I was on. The exception now compares the page host, and uses the request host only for a top-level request where the two are the same.

**The always-HTTPS upgrade did not run at all.** I added the setting and a two-argument `should_upgrade`, then a patch to the shared request check failed to apply and nothing noticed, because my tests exercised the policy class and not the fetch. The check that read the returned address found it. The upgrade now lives in the single function every request passes through, and a test asserts the address my fetch returns starts with `https://` for a host with a promise on record.

**Changing the memory cap would have killed the browser.** My first job object used `KILL_ON_JOB_CLOSE`, which terminates every process in the job when the last handle to it closes. My settings path closed the handle to rebuild the job, so changing a number in the settings page would have ended the process. I removed the flag, keep one job for the life of the browser, and rewrite its limits in place. The cap can now go on, off and on again without a restart.

**My totals were written by a tab thread and read by the window thread.** A plain `uint64_t` shared between a tab's worker and my painting code is a data race whatever the number is for. The window's totals are atomics now, and the per-tab counters belong to the tab that owns them, so the delta I fold into the total is exact.

## What I built, in the order I built it

**The HTTPS store.** A host, an expiry in milliseconds and an `includeSubDomains` flag. `record_header` refuses a header that did not arrive over HTTPS, caps the lifetime at a year, and a `max-age=0` removes the entry instead of storing a promise that has already been taken back. The store serialises to my own JSON, is pruned when the profile loads, and `should_upgrade` is what turns a plain URL into a TLS one before any socket opens.

**Mixed content and insecure forms.** An HTTPS page loading an HTTP subresource is refused for every resource type, and loopback is exempt. An HTTPS page posting a form to an HTTP address is refused, counted, and the reason names the rule. Both decisions sit in the same function as the blocking decision, so there is one order and one explanation string.

**The filter engine.** I parse comments, ignore a header line, and refuse cosmetic rules with the line number and the reason. A rule is a pattern with its anchors, an optional set of types, an optional third-party requirement, and optional site restrictions. A rule naming `sitekey` or `redirect` is refused rather than dropped, because a rule I cannot honour is a rule that would silently do less than the list says. Matching builds the subject from the lowercased host and the path and query, and a host anchor is bounded on the left by a label boundary, so `||ads.example` cannot match `ads.example.evil.test`.

**The wiring.** A fetch now carries the kind of resource it is for. That type decides which rules apply, and it is also what the refusal message names, so a blocked image says image and a blocked script says script. The page host travels with the request so third-party rules and per-site exceptions have something real to compare. My shell owns one HTTPS store, one filter list and one counter block for the window, and each tab holds a share of them, which means a closing tab cannot leave a worker pointing at freed memory.

**The containment.** A job object with kill-on-unhandled-exception, a memory cap and a user-time cap when I set them. The panel prints whether a cap is in force and states in the same breath that page code is not isolated from my process. There is no sentence anywhere in this build that calls that a sandbox.

**The settings.** An always-HTTPS toggle, a memory cap field, a filter list file path, and three notes: the rule count with the exception count, the blocked total with the upgrade and refusal totals, and the honest memory sentence.

## What I checked, and how

- My engine test suite: **718 checks passed**, 70 more than the previous build.
- The HTTPS store: a header over HTTPS, the same header over HTTP refused, a capped lifetime, `includeSubDomains`, `max-age=0`, pruning an expired entry, and a JSON round trip that survives a restart.
- The decisions through the request path, not only through the class: a filtered script refused with the rule named, a per-site exception letting it through, mixed content refused with the blocker switched off, the upgrade visible in the returned address, and exactly one counted fallback for a host without a promise.
- The matcher: a host anchor and a subdomain, a query string that mentions the host and does not match, a type option that excludes the wrong type, a third-party rule on the page that is not the site, an exception beating a block, and a `domain=` rule with an exclusion.
- My source audit: **250 checks**. It now proves that the HTTPS store, the filter parser and the matcher are my own code, that I bundle no third-party list, that every request goes through one shared check, that a refusal is tagged so it can never be retried in the clear, that an HTTPS promise never falls back, and that the isolation sentence exists in my source and is printed by my panel.
- My naming audit: **21 checks**. My plan audit: **377 checks**.

## What I could not test here

- **No real mixed content over a real TLS connection.** My decisions are tested against my own policy with URLs I construct. A live HTTPS page with an HTTP subresource on my own machine is the check that settles the wiring end to end.
- **No isolation test, because there is no isolation.** A job object cannot be tested for an escape it is not designed to prevent.
- **No large filter list.** My starter list is 33 rules and my parser is tested on a list of eight lines. A subscription-sized list is a performance question I have not answered.
- **No page that breaks because of my blocking.** I have not browsed real sites with the rules on and counted what I broke. That is the honest limit of a list I wrote myself.

## What is still on my plan for this section

CSP enforcement, an AppContainer profile for page code, extension permission enforcement at the dispatcher, a signed update manifest, and fuzzing. None of those are claimed by this build, and the release notes say so where a person would read it.
