# Browser Index (SQLite)

## Goals
- Fast search across watched folders.
- Tags/favorites/recent.
- Cache waveform peaks for instant display.

## Tables (suggested)
- `files(path, mtime, size, duration, channels, sample_rate, hash)`
- `tags(path, tag)`
- `favorites(path)`
- `recent(path, last_opened)`
- `peaks(path, format_version, peak_blob)`

## Notes
- Indexing and peak generation must occur off the audio thread.
