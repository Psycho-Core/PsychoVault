# What I build next

This file always names the current stage of my build order (section 0 of
`docs/Psycho_PLAN.md`), so nobody has to guess what the next exe is for.

## Current stage: Stage A - make my browser real online

In flight, in this order:

1. Bundled extensions inside the one exe (appended payload, unpacked to my
   profile on first run) so Proton VPN is present without the ZIP.
2. `psychovault://nettest` - DNS, TCP, TLS and HTTPS checks that each name
   their own failure, with the results copyable.
3. A CI step that loads a real page from the live internet each build and saves
   the capture as proof.

After Stage A: Stage C - my engine rendering my own settings UI against the 23
pinned reference screenshots. No GitHub Actions runs happen until a change is
actually ready to build.

## How the diagnostics answer "why doesn't a page load"

Type `psychovault://nettest` in the address bar, press "Run my connection
checks", press "Copy the results", and paste that text into the report. Each
line is one stage; the first FAIL is the stage I fix first.
