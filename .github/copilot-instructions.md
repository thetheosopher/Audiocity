# Copilot Instructions — Audiocity

You are assisting with **Audiocity**, a JUCE/C++ hybrid sampler.

## Non-negotiable constraints
- Never allocate, lock, do file I/O, or log on the audio thread (`processBlock`).
- Prefer fixed-size pools and preallocation in `prepareToPlay`.
- All changes must include or update relevant tests (offline render + golden tests).

## How to work
- Read `docs/` files before coding.
- Implement milestone-by-milestone.
- Keep changes compileable after each step.

## Layout
- Code under `src/`
- Specs under `docs/`
- Tests under `tests/`
- Third-party deps under `third_party/` (prefer submodules)
