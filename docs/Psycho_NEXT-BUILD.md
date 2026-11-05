# What I build next

This file always names the current stage of my build order (section 0 of
`docs/Psycho_PLAN.md`), so nobody has to guess what the next exe is for.

## Current stage: Stage A - live-internet truth, then my own Electron

In flight, in this order. Proton VPN and extensions moved to the end of my
build order (Stage F): they arrive once the browser underneath them is real.

1. `psychovault://nettest` - DNS, TCP, TLS and HTTPS checks that each name
   their own failure, with the results copyable.
2. A CI step that loads a real page from the live internet each build and saves
   the capture as proof.
3. Then Stage B: PsychoEngine climbs layout, painting and JavaScript against
   pinned conformance with published counts, followed by Stage C where my own
   UI pages are rendered by PsychoEngine against my reference screenshots.

No GitHub Actions runs happen until a change is actually ready to build.

## How the diagnostics answer "why doesn't a page load"

Type `psychovault://nettest` in the address bar, press "Run my connection
checks", press "Copy the results", and paste that text into the report. Each
line is one stage; the first FAIL is the stage I fix first.
