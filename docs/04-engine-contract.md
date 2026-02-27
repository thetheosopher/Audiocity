# Engine Contract

## Engine API (suggested)
- `prepare(sampleRate, maxBlockSize)`
- `noteOn(note, velocity, sampleOffsetInBlock)`
- `noteOff(note, sampleOffsetInBlock)`
- `render(float** outputs, int numChannels, int numSamples)`

## Determinism
- Offline render must be repeatable for the same inputs (within tolerance if HQ resampler changes).

## Voice allocation
- Fixed-size voice pool.
- Voice stealing policy is explicit and tested.
