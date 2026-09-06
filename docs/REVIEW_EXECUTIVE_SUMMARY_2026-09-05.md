# Audiocity — Review Executive Summary

```text
Review conducted: 2026-09-05
Reviewer: Codex (OpenAI)
Perspectives: Software Optimization Engineering + Product Management
Codebase: Audiocity 1.3.2.0 (JUCE/C++20 hybrid sampler — Standalone + VST3)
```

> Reviewer identity note: the supplied template named a different model. The identity above records the reviewer that actually performed this review.

## What the software does

Audiocity is a Windows software sampler delivered as a standalone instrument and VST3 plugin. It combines a 64-preset factory bank, sample capture/generation and slicing, modulation/effects, disk streaming, program mapping, broad third-party instrument import, and SFZ/DecentSampler export. Its strongest assets are import breadth, explicit real-time design rules, detailed diagnostics, and a large deterministic offline test suite.

The reviewed non-UI tests passed, but Audiocity is not ready for a confidence-based release until two concurrency defects and its delivery gates are fixed.

## Top 3 performance and engineering risks

1. **Unsafe cross-thread engine mutation — Critical.** UI controls directly change ordinary `EngineCore` settings and live voice objects while the audio thread can read or mutate them. This is a C++ data race that can produce intermittent audio corruption or host crashes. The documented immutable-snapshot model is not consistently enforced.
2. **Detached import worker lifetime — Critical.** A detached thread captures a raw processor pointer. Closing a plugin or DAW session during an import can let that worker access a destroyed processor. “Cancel” only discards the final result, so repeated replacements can also leave multiple expensive jobs running.
3. **Scale and release signals are unreliable — High.** Every instance reserves about 27.5 MiB for unused capture/preview arrays; library scans read every uncached sample end-to-end and repeatedly sort the accumulated list; programs above 256 assets/128 groups/512 zones silently truncate. Meanwhile, the public CI release run fails and all 19 local UI goldens differ, so regressions are not reliably blocked.

## Top 3 product opportunities

1. **Portable projects and guided relinking.** Imported instruments restore from their original path only. A versioned asset manifest, missing-sample resolver, and explicit Reference/Collect/Embed workflow would make collaborator and cross-machine sessions dependable while controlling oversized embedded state.
2. **Cross-platform reach.** Audiocity currently ships Windows VST3/Standalone only. Once correctness and CI are stable, macOS VST3/AU with Apple Silicon support is the clearest audience expansion; Linux/CLAP should follow measured demand. Free Decent Sampler already spans Windows, macOS, Linux, iOS, and multiple plugin formats ([official product page](https://www.decentsamples.com/product/decent-sampler-plugin/)).
3. **Make existing breadth usable and scalable.** The library recognizes many formats that the primary chooser and drag/drop reject. One format registry, lazy large-library indexing, truthful cancellable progress, and loss-preflight for exports will create more user value than another importer.

## Highest-leverage first action

**Make the audio thread the sole owner of mutable engine/DSP state.** Route UI changes through APVTS or bounded commands, capture controls once per block, and apply only changed groups on the audio thread. This removes undefined behavior and simultaneously reduces per-block work. In parallel only with low-risk delivery work, repair the CI preset and visually approve the UI baselines so this change has a trustworthy safety net.

Full evidence is in [CODE_REVIEW_2026-09-05.md](CODE_REVIEW_2026-09-05.md); estimates, sequencing, and implementation prompts are in [ENHANCEMENT_ROADMAP_2026-09-05.md](ENHANCEMENT_ROADMAP_2026-09-05.md).
