# PSYCHO.md

This is my project guide for anyone or any coding agent working on PsychoVault. I want you to read it before making changes, then check the current files and test evidence. This guide describes what I want; it is not proof that a feature exists or works.

Tools that automatically read `CLAUDE.md` or `AGENTS.md` will not necessarily read this filename. I want this project's guide to be called `PSYCHO.md`. Tell the tool to read it, or point that tool's own configuration at it.

## What I am building

I am building my own browser, not putting my name on somebody else's browser.

The previous Electron application was responsive and ran well. I want that level of responsiveness, but I do not want Electron, Chromium, Gecko, WebView, an embedded browser runtime, or another browser's engine underneath the new application. I do not want a renamed browser, a wrapper around a browser component, or a collection of pretend controls.

Every line of PsychoVault application and engine source must be written for this project. That includes the browser shell, HTML and CSS engines, JavaScript engine, layout, painting, networking, storage, process isolation, and extension engine. I do not accept copying another browser's implementation and changing the names.

The exception is extensions. Proton VPN is my standard bundled VPN extension, and I want a general extension system rather than a Proton-only hack. Extension code remains separately identified, licensed and sandboxed. An extension exception is not permission to import a third-party browser engine.

Using an operating-system API, compiler, standard language library, font service, GPU API or build tool is not the same as embedding another browser. Any such boundary must be explicit in the architecture and dependency audit. I do not want a dependency that quietly supplies the parts I asked to have written.

## My non-negotiable requirements

1. I want the new browser to feel as fast and responsive as the previous application. Measure it on comparable hardware and workloads; do not promise that an unmeasured engine is faster.
2. I want the same interface: the dark theme, tab strip, address bar, sidebar, workspace controls, new-tab/home page, dialogs and settings layout. The screenshots are reference material, not an invitation to invent a different design.
3. I want all of my previous features and settings, including profiles, workspaces, vault, passwords, downloads, privacy controls, spelling, accessibility, appearance, ad blocking, extensions, Performance, updates and recovery.
4. I want Proton VPN shipped as the standard extension. It must authenticate, connect, proxy browser traffic and fail closed when required. A popup, a loaded extension or a connected label is not proof of a VPN.
5. I want the implementation written, not represented by empty functions, fabricated results, test-only browser APIs or controls that only look finished.
6. I want every project document and release note written in my plain first-person voice.
7. I want research and a written plan before major implementation. I want the plan audited against the actual requirements, specifications, extension code and available evidence.
8. I want GitHub Actions to build the browser. I do not want to be told to compile it myself. Run builds one at a time, fix the first real failure and publish only verified outputs.

## The interface is a contract

The reference screenshots are in `docs/reference/legacy-ui/`. They cover the home/new-tab page and all ten settings categories:

- General
- Privacy & security
- Downloads
- Extensions
- Performance
- Appearance
- Spelling
- Passwords
- Accessibility & language
- About

The archive also contains browser-window captures. These were made with a throwaway profile, not my personal profile. A viewport capture is not proof that an entire long settings page was recorded. I want a complete control-by-control inventory and additional captures where the archive is incomplete.

Keep the ad blocker above Proton and both above Settings. Call the resource-control panel Performance. Preserve the established visual hierarchy, spacing, colours, typography, icons, focus states and interactions unless I explicitly approve a change.

I want visual regression tests at the reference viewport sizes and checks for keyboard navigation, focus, zoom, text scaling and different window sizes. Matching one screenshot does not establish complete UI parity.

## Features, data and honesty

I want an inventory that maps every old feature, setting, default, persistence rule, shortcut, context menu and error state to an implementation and acceptance test. A settings page with the right labels is not enough if its controls do nothing.

Preserve my profiles, workspaces, bookmarks, history, passwords, vault contents and other user data. Design and test migration before replacing storage. Keep backups and recovery paths. Never delete untracked installers, profiles or personal files as part of a reset or build.

Keep private browsing separate from ordinary browsing. Do not let extensions, scripts, internal pages or websites gain privileged access by accident. Keep update signature and hash verification, safe installer handoff, backup and rollback.

Do not claim that an extension, VPN, ad blocker, sandbox, privacy protection or performance target works because the code compiles or a status indicator changes. Test the behaviour. Report passed, failed, blocked and not run separately. A source audit is not a device test, and a successful build is not a working browser.

Until the complete acceptance gates pass, describe the project as in development. Do not publish a partial engine as a finished replacement for my old browser.

## How I want engineering work done

- Read this file, the current architecture plan, the requirement inventory and the relevant verification evidence first.
- Keep changes focused. Answer multi-part requests in order.
- For a bug, start with the installed version and the first actual error in the transcript. Do not diagnose from a generic notification alone.
- Use specifications and behaviour tests to guide original implementation. Reading another implementation for API research does not permit copying its source.
- Keep privileged browser code separate from website and extension code. Use default-deny permissions and validate every message crossing a trust boundary.
- Do not invent my results, hardware measurements, compatibility percentages, security guarantees or release status.
- Do not weaken a requirement silently. If a requirement conflicts with a platform limit or the evidence, record the conflict, explain it and resolve it explicitly.
- Keep third-party extension licences, notices, versions, checksums and local patches accurate. Do not mix extension code with authored engine code.
- Do not commit credentials, signing keys, personal profiles, private logs, downloaded runtimes or generated binaries. Never ask me to paste a password or token into chat. Scrub diagnostics before sharing them.

## Builds and releases

I want free GitHub-hosted builds, with one active heavy build at a time. Cache only correct, versioned inputs and never reuse an output built from different source as though it were current. Keep build logs and failure notes useful without exposing private information.

I want a Windows x64 release as the first complete desktop target. Android must use the same owned engine and requirements, not a separate third-party browser wearing the same name. Plan Android only after the shared core has stable platform boundaries; do not claim parity from a source port.

My Windows checkout stays at `D:\Blakes_SHIT\#Psycho-core_REPOS\PsychoVault`. Do not tell me to move it. When the new build system exists, I still want a double-click `PsychoVault_easy_build.bat` entry point that delegates to the supported build path rather than asking me to assemble a toolchain manually.

Every release needs accurate release notes and checksums. A failed build also needs an honest failure record. Do not overwrite a published release or bypass signing and verification to make a workflow green.

## Writing and reporting

Write as me: direct, plain, first-person language. Use ` - ` instead of em dashes. Avoid marketing claims and inflated technical language.

Keep the public README short and suitable for the general public, with the important details and the approved pictures. Keep the new-tab picture near the top when the new browser has its own screenshot. Do not use a legacy screenshot to pretend the new engine is finished. Retain applicable notices and no-warranty information.

Give me evidence and links. Verify a PR, merge, build, release or test result before saying it succeeded. If something was not run, say so. If a plan is feasible but large, give me the real stages and gates rather than a fake completion date.

My definition of done is a working, tested browser with my interface, my features, my own engine and extension system, and Proton working inside it. An empty repository, a design document or a successful compile is only a step toward that result.
