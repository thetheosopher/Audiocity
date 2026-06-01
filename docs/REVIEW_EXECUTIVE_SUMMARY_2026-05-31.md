# Audiocity — Executive Summary

```text
Review conducted: 2026-05-31
Reviewer: Claude Opus (via GitHub Copilot)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

## Implementation Status

Last updated: 2026-05-31. The first implementation pass has completed the highest-leverage quick wins, MP-4 importer hardening, and MP-1 async import, and has started MP-2 plus MP-3: CI now runs the non-UI test suite, several low-risk render-path costs were removed, drag/drop debug logging is gated, import diagnostics can be copied and are persisted locally, ZIP importers now reject traversal-shaped sample references for Bitwig and Korg archive formats, NCW converter quoting is covered by a shell-metacharacter path regression, malformed importer corpus coverage now exercises SF2, Bitwig, Korg, Ableton, KMP, EXS24, and NN-XT failure paths, binary importer bounds hardening caps KMP `RLP1` entries plus reports oversized EXS24 chunks through overflow-safe truncation checks, SF2 short LIST/table failures are rejected, ZIP/GZIP whole-entry reads are capped, sample files plus all supported imported-program formats now prepare on a background worker and publish on the message thread with clean stale-job cancellation, the About, Generate, and Capture tabs have been extracted into their own translation units, and the new DecentSampler exporter/save path preserves grouped volume/pan/release-trigger defaults plus documented round-robin and choke-group metadata across round trips.

## What the software does

Audiocity is a Windows hybrid sampler — a tool that plays and shapes recorded audio across a keyboard — delivered as both a standalone app and a VST3 plugin for music production software. Its standout strength is **import breadth**: it loads roughly fourteen instrument formats (SFZ, SF2, DecentSampler, Bitwig, MPC, EXS24, Korg, Ableton, and more), plus direct audio files, and ships a curated 64‑preset factory bank with automated quality checks. Under the hood it is a disciplined, real‑time audio engine with strong, lock‑free thread‑safety and a thorough offline test suite.

The codebase is mature and well‑documented. It is not at risk on correctness. The opportunities are about **trustworthy delivery, maintainability, and market reach.**

## Top 3 performance / engineering risks

1. **The automated test suite never runs in CI.** The only continuous‑integration job checks UI screenshots; the extensive engine, importer, and preset tests run only on a developer's machine. Real‑time and import regressions can ship undetected. *This is the highest‑priority fix and is roughly a day of work.*
2. **Three oversized files concentrate risk and slow everyone down.** The UI file alone is over 11,000 lines, with the processor and engine close behind. Every change forces a full recompile and a steep mental load, throttling development speed and raising the chance of mistakes.
3. **Import cancellation is still only publish-level, not decode-preemptive.** Large imports no longer freeze the interface, but once a decode/import job starts it still runs to completion and is only cancelled by dropping the stale result on publish. Importers also parse untrusted third‑party files, so continued hardening and corpus/fuzz coverage remain important.

## Top 3 product opportunities

1. **Go cross‑platform (macOS + Audio Unit, and CLAP).** Every direct competitor runs on macOS; Audiocity is Windows‑only and VST3‑only. The framework makes this primarily a build‑and‑sign effort, and it is by far the largest expansion of the potential audience.
2. **Finish the DecentSampler `.dspreset` exporter.** The current exporter slice now covers save routing, grouped volume/pan/release-trigger round-tripping, documented round-robin semantics, and choke-group mapping, but advanced mappings such as velocity fades, one-shot triggers, and sustain-loop differentiation still degrade with warnings. Finishing that compatibility envelope would turn Audiocity from a one-way importer into a content-pipeline hub that fits how producers actually share instruments.
3. **Learn which formats users actually use.** There is rich engine telemetry but zero product insight into which imports succeed, fail, or matter. A strictly opt‑in, local‑first import log would let the roadmap invest in the formats users care about instead of guessing.

## The single highest‑leverage action to take first

**Put the existing offline test suite into continuous integration.** The tests already exist and run without audio hardware — they simply are not wired into the pull‑request pipeline. Turning them on costs about a day and immediately protects the real‑time engine, every importer, and the factory presets from silent regressions. It is the foundation that makes every subsequent change — refactoring the big files, going cross‑platform, hardening importers — safe to attempt.

*Full detail in `CODE_REVIEW_2026-05-31.md`; the prioritized plan with effort/impact and ready‑to‑use implementation prompts is in `ENHANCEMENT_ROADMAP_2026-05-31.md`.*
