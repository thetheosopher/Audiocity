# Audiocity — Executive Summary

```text
Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

## Implementation Status

Last updated: 2026-05-31. The first implementation pass has completed the highest-leverage quick wins: CI now runs the non-UI test suite, several low-risk render-path costs were removed, drag/drop debug logging is gated, import diagnostics can be copied and are persisted locally, ZIP importers now reject traversal-shaped sample references for Bitwig and Korg archive formats, NCW converter quoting is covered by a shell-metacharacter path regression, and malformed importer corpus coverage now exercises SF2, Bitwig, Korg, Ableton, KMP, EXS24, and NN-XT failure paths. The next prudent workstream is to continue MP-4 bounds audits or start MP-1 async import as a dedicated architectural slice.

## What the software does

Audiocity is a Windows hybrid sampler — a tool that plays and shapes recorded audio across a keyboard — delivered as both a standalone app and a VST3 plugin for music production software. Its standout strength is **import breadth**: it loads roughly fourteen instrument formats (SFZ, SF2, DecentSampler, Bitwig, MPC, EXS24, Korg, Ableton, and more), plus direct audio files, and ships a curated 64‑preset factory bank with automated quality checks. Under the hood it is a disciplined, real‑time audio engine with strong, lock‑free thread‑safety and a thorough offline test suite.

The codebase is mature and well‑documented. It is not at risk on correctness. The opportunities are about **trustworthy delivery, maintainability, and market reach.**

## Top 3 performance / engineering risks

1. **The automated test suite never runs in CI.** The only continuous‑integration job checks UI screenshots; the extensive engine, importer, and preset tests run only on a developer's machine. Real‑time and import regressions can ship undetected. *This is the highest‑priority fix and is roughly a day of work.*
2. **Three oversized files concentrate risk and slow everyone down.** The UI file alone is over 11,000 lines, with the processor and engine close behind. Every change forces a full recompile and a steep mental load, throttling development speed and raising the chance of mistakes.
3. **Large imports freeze the interface.** Loading a big library runs on the interface thread with no progress bar or cancel, so the app appears to hang. Importers also parse untrusted third‑party files and would benefit from security hardening (bounds checks, ZIP path‑traversal guards, fuzz testing).

## Top 3 product opportunities

1. **Go cross‑platform (macOS + Audio Unit, and CLAP).** Every direct competitor runs on macOS; Audiocity is Windows‑only and VST3‑only. The framework makes this primarily a build‑and‑sign effort, and it is by far the largest expansion of the potential audience.
2. **Add an export path (DecentSampler `.dspreset`).** Today the product imports many formats but exports only one, making it a one‑way street. A second exporter turns Audiocity from an importer into a content‑pipeline hub that fits how producers actually share instruments.
3. **Learn which formats users actually use.** There is rich engine telemetry but zero product insight into which imports succeed, fail, or matter. A strictly opt‑in, local‑first import log would let the roadmap invest in the formats users care about instead of guessing.

## The single highest‑leverage action to take first

**Put the existing offline test suite into continuous integration.** The tests already exist and run without audio hardware — they simply are not wired into the pull‑request pipeline. Turning them on costs about a day and immediately protects the real‑time engine, every importer, and the factory presets from silent regressions. It is the foundation that makes every subsequent change — refactoring the big files, going cross‑platform, hardening importers — safe to attempt.

*Full detail in `CODE_REVIEW_2026-05-31.md`; the prioritized plan with effort/impact and ready‑to‑use implementation prompts is in `ENHANCEMENT_ROADMAP_2026-05-31.md`.*
