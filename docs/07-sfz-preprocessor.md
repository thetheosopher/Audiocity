# SFZ Preprocessor (v1)

## Supported directives
- `#include "path"` (nested, cycle detection)
- `#define $NAME value` (variable expansion)

## Include resolution
- Resolve include paths relative to the **root SFZ file directory**.
- Included content is pasted at directive location.
- Detect cycles using an include stack.

## Variable expansion
- Expand `$NAME` in include paths, `default_path`, and opcode string values.

## Diagnostics
- Missing include file: error with file/line.
- Include cycle: error and skip include.
- Unknown variables: warning; leave literal text.
