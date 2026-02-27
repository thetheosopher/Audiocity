# Prompt: Implement v1 SFZ import + mapping editor

---
Read `docs/07-sfz-preprocessor.md`, `docs/08-sfz-import.md`, `docs/06-roadmap.md` (Epic E), and `docs/03-ui-sampler-style.md`.

Implement SFZ import v1:
- Preprocessor with `#include` + cycle detection + `#define` expansion
- Parser for the core opcode subset
- Convert to Program/Group/Zone model
- Emit diagnostics (missing samples, unsupported opcodes, include errors)

Update Mapping UI:
- Display zones after import
- Zone list + RR group column

Add tests using SFZ fixtures:
- nested includes
- default_path switching
- velocity layers
- loops
---
