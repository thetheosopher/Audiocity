# Real-time Rules (Hard Constraints)

## Forbidden on audio thread
- Heap allocation (`new`, `malloc`, growing `std::vector`)
- Locks/mutexes/condition variables
- File I/O
- Logging
- Plugin scanning/instantiation

## Required patterns
- Pre-allocate voices/buffers in `prepareToPlay()`.
- Use lock-free queues for events/state.
- Keep `processBlock()` deterministic and bounded.
