# Audio Sample Library File Format Specification

**Purpose:** a language-neutral reference for reading, playing, converting, and writing audio assets and sampled instruments.

**Edition:** 1.0. This document defines explicit implementation profiles, not a new interchange format.

## Contents

- [1. Scope and coverage](#scope)
- [2. Binary conventions and shared data model](#conventions)
- [3. RIFF and IFF chunk traversal](#chunks)
- [4. WAVE, sampler metadata, BWF, RF64, and BW64](#wave)
- [5. AIFF and AIFF-C](#aiff)
- [6. Core Audio Format](#caf)
- [7. FLAC and Ogg audio assets](#compressed-assets)
- [8. SoundFont 2](#soundfont)
- [9. SFZ instruments](#sfz)
- [10. Downloadable Sounds](#dls)
- [11. Giga instruments](#giga)
- [12. Korg KMP and KSF](#korg)
- [13. EXS instruments](#exs)
- [14. Decent Sampler presets](#decent)
- [15. REX, Kontakt, and other proprietary families](#proprietary)
- [16. Cross-format conversion and playback](#conversion)
- [17. Reader, writer, and conformance checklist](#conformance)
- [18. Public references](#references)

<a id="scope"></a>
## 1. Scope and coverage

An audio file contains encoded sound and, sometimes, markers or loops. An instrument file adds mappings, pitch, articulation, and references to sound. A library container adds packaging, indexes, or a filesystem. These layers must be implemented separately: recognizing a library container does not decode its samples, and decoding a WAV does not reproduce the instrument that uses it.

The profiles below combine published format descriptions with independently documented implementation behavior. **Published rules**, **implementation-derived profiles**, and **unresolved fields** are distinguished explicitly. A recommendation in this guide is not automatically a requirement of an external standard. Public references appear as `[S1]`, `[S2]`, etc.; their links and version scopes are collected at the end.

### 1.1 Coverage levels

| Level | Meaning | What an implementer may rely on |
|---|---|---|
| **Structural profile** | Concrete framing, field tables, reference resolution, and a specified useful subset | Implement the stated subset without guessing its basic byte layout |
| **Version-scoped profile** | A concrete layout derived from a named public implementation | Implement that variant; reject or preserve unfamiliar variants |
| **Container/metadata profile** | Framing and metadata are described, but a separate codec or synthesis specification is required | Enumerate assets and metadata; do not claim complete decoding or playback |
| **Unresolved family** | No adequate layout is specified here | Identify the requirement, not a parser implementation |

| Format/profile | Coverage here | Principal limitation |
|---|---|---|
| RIFF/WAVE PCM and IEEE float | Structural profile | Compressed WAVE codecs need independent codec implementations |
| BWF `bext` | Structural profile | Broadcast metadata, not an instrument mapping |
| RF64/BW64 size handling | Structural profile | Full ADM/object-audio rendering is outside this guide |
| AIFF PCM and common AIFC encodings | Structural profile | Historical compressed AIFC codecs are not decoded here |
| CAF linear PCM | Structural profile | Compressed CAF requires packet and codec support |
| FLAC | Container/metadata profile | Full predictive/residual decoding belongs to RFC 9639 |
| Ogg/Vorbis and Ogg/Opus | Container/metadata profile | Ogg framing is not a codec |
| SoundFont 2.01/2.04 | Structural profile | Full synthesizer fidelity additionally requires modulation and DSP rules |
| SFZ | Structural text and portable playback profile | Dialects and extended opcodes vary by engine |
| DLS Level 1 core, selected Level 2 structures | Structural profile | Full Level 2 articulation and conditional-download rules are not exhaustively specified |
| Giga | Version-scoped container, dimensions, and sample-compression profile | Many synthesis-extension fields and newer variants remain outside the profile |
| Korg KMP/KSF | Version-scoped profile | Compressed KSF decoding and several playback-unit conventions remain unresolved |
| EXS | Version-scoped block, zone, sample, and group profile | Parameter blocks, consolidated assets, and endpoint conventions need variant-specific handling |
| Decent Sampler `.dspreset` | Structural XML and basic playback profile | UI, effects, scripting, and packaging are not a complete engine specification |
| REX2 | Bounded container-identification profile | Slice metadata and sample codec are not specified |
| Kontakt and other named proprietary families | Unresolved family entries | No universal binary specification is claimed |

**This is not a complete specification of every proprietary sampler format.** The unresolved-family catalog is deliberately retained so an implementation plan cannot confuse a familiar product name with an implemented parser. Where a profile is incomplete, the missing dependency is stated near the relevant layout, rather than hidden in a general disclaimer.

### 1.2 Recommended implementation order

Implement checked byte readers and chunk traversal first; then WAVE/AIFF/CAF PCM; then a shared sample-and-region model; then SFZ, SoundFont, and DLS reference resolution. Add EXS, Korg, and Giga through explicitly versioned adapters. Treat opaque library containers and licensed codecs as separate projects.

A useful first product can read common PCM assets and portable SFZ mappings without claiming compatibility with proprietary instrument banks.

<a id="conventions"></a>
## 2. Binary conventions and shared data model

### 2.1 Types and offsets

| Notation | Definition |
|---|---|
| `u8`, `i8` | Unsigned or two's-complement signed 8-bit integer |
| `u16le`, `i16le`, `u32le`, `u64le` | Explicitly little-endian integer of the indicated width |
| `u16be`, `u24be`, `u32be`, `u64be` | Explicitly big-endian integer; a 24-bit integer occupies three bytes |
| `i24le`, `i24be` | Three-byte signed integer, sign-extended after loading |
| `f32`, `f64` | IEEE binary floating point; byte order is specified separately |
| `bytes[N]`, `char[N]` | Exactly N stored bytes, with no host-language alignment |
| FOURCC | Four literal bytes, including spaces and letter case |
| Payload offset | Offset relative to the first byte after the chunk header |
| Record offset | Offset relative to the first byte of the record in its table |
| File offset | Absolute byte position from the beginning of the file |

**Every table uses payload or record offsets unless it explicitly says file offset.** Do not translate these tables into native structs without packed-layout control and independent size checks. A FOURCC is not an integer in the host's byte order.

Fixed-width text fields need not contain a NUL. Stop at the first NUL within the field, or at its declared width. Preserve undecodable raw bytes rather than replacing the name irreversibly. Legacy binary instrument names often require a legacy encoding; SFZ path encoding is engine-dependent; XML declares its own encoding.

Use at least 64-bit arithmetic for file positions, chunk lengths, frame counts, and products. In languages whose ordinary numbers cannot represent all 64-bit integers exactly, use an exact integer type for offsets and lengths. Never round a byte position through floating point.

### 2.2 Units that must not be conflated

- A **sample point** is one amplitude value in one channel.
- A **sample frame** contains one sample point for each channel at one time instant.
- A **PCM byte offset** is a position in encoded storage, not a sample-frame index.
- A **codec frame** is a compressed coding unit; it may decode to many sample frames.
- An **instrument region/zone** maps an event range to an asset and playback settings.
- A **marker ID** is an identifier; it is not necessarily an array index or frame position.

For uncompressed interleaved PCM:

```text
bytes_per_channel = ceil(container_bits / 8)
bytes_per_frame = channels * bytes_per_channel
frame_count = audio_bytes / bytes_per_frame
point_index(frame, channel) = frame * channels + channel
byte_offset(frame, channel) =
    audio_start + frame * bytes_per_frame + channel * bytes_per_channel
```

The division must be exact in a strict PCM reader. Do not apply these equations to ADPCM, FLAC, compressed Giga data, or other packet codecs.

### 2.3 Signed PCM and normalization

For a B-bit two's-complement integer, normalize to floating point using:

```text
normalized = signed_value / 2^(B - 1)
```

This maps the most negative value to -1 and the largest positive value to slightly less than +1. Eight-bit **WAVE PCM** is unsigned offset binary:

```text
normalized = (stored_u8 - 128) / 128
```

Eight-bit **AIFF PCM** is signed. These are not interchangeable without changing the bytes.

For a C-bit signed container with V valid bits stored most-significant-bit aligned, load the signed container, arithmetic-shift by `C - V`, then normalize by `2^(V - 1)`. Do not accidentally discard the sign bit or treat the unused low bits as additional signal precision.

An explicit three-byte sign extension is:

```text
u = b0 | (b1 << 8) | (b2 << 16)       # little-endian example
s = u if u < 0x800000 else u - 0x1000000
```

Floating-point audio may exceed nominal full scale. Preserve it during parsing; clipping, NaN rejection, or output limiting is a renderer policy and must be explicit.

### 2.4 Internal model

The following is a recommended implementation model, **not an on-disk schema**:

```text
AudioAsset:
    identifier, source_uri, format_profile, channel_layout
    sample_rate, channels, decoded_frame_count
    encoding, container_bits, valid_bits
    audio_ranges_or_packets
    root_pitch, pitch_correction_cents
    markers[], loops[], preserved_metadata[]

Instrument:
    identifier, name, bank, program
    groups[], regions[], modulation_routes[]
    asset_references[], preserved_extensions[]

Region:
    key_range_inclusive, velocity_range_inclusive
    controller_conditions[], trigger, sequence_or_round_robin
    asset_reference, channel_selection
    playback_start_frame, playback_end_frame_exclusive
    root_key, transpose_semitones, fine_tune_cents, key_tracking
    gain, pan, envelopes, filter
    loops[], choke_group, release_behavior
    source_profile, preserved_fields[]
```

Keep asset data and region overrides separate. Two zones may share the same waveform while using different root notes, trims, loops, gain, or articulation.

### 2.5 Canonical loop convention

Use a half-open interval internally:

```text
loop = [start_frame, end_frame_exclusive)
length = end_frame_exclusive - start_frame
```

| Source field convention | Internal conversion |
|---|---|
| WAVE `smpl` end, SFZ `loop_end` | Inclusive end: add 1 with overflow checking |
| SoundFont `end`, `endLoop` | Already exclusive |
| DLS loop start and length | `end = start + length` |
| AIFF marker-referenced loops | Resolve marker IDs first; retain the selected endpoint compatibility convention |
| EXS and KSF profiles here | Preserve raw endpoints; do not invent an inclusive/exclusive rule |

For unresolved endpoint conventions, expose an explicit compatibility setting or withhold playback of that loop. Silently adding 1 everywhere produces corrupted loop lengths.

<a id="chunks"></a>
## 3. RIFF and IFF chunk traversal

### 3.1 RIFF framing

RIFF uses little-endian size fields. A standard chunk is:

| Offset | Type | Field |
|---:|---|---|
| 0 | `bytes[4]` | Chunk ID |
| 4 | `u32le` | Payload byte count |
| 8 | `bytes[size]` | Payload |
| `8 + size` | Optional byte | Pad if size is odd; excluded from `size` |

For a chunk beginning at file position P:

```text
payload_start = P + 8
payload_end = payload_start + size
next_chunk = payload_end + (size & 1)
```

`RIFF` and `LIST` payloads begin with a four-byte form/list type. Their child area starts four bytes into the payload. The parent size includes the type and all child headers, child payloads, and child pads.

### 3.2 IFF/FORM framing

Classic AIFF uses the same eight-byte outer chunk layout and even-byte padding, but size fields are big-endian and the root is `FORM`. Its first four payload bytes identify `AIFF` or `AIFC`.

Do not infer chunk endianness from audio-data endianness. AIFC `sowt` has little-endian PCM inside big-endian IFF framing.

### 3.3 Checked traversal

```text
walk_chunks(start, limit, byte_order, size_resolver):
    cursor = start
    occurrences = empty map
    while cursor < limit:
        require limit - cursor >= 8
        id = read_four_bytes(cursor)
        size32 = read_u32(cursor + 4, byte_order)
        occurrence = occurrences.get(id, 0)
        size = size_resolver(id, size32, occurrence)
        require size <= limit - cursor - 8
        end = cursor + 8 + size
        require (size & 1) <= limit - end
        emit(id, cursor, cursor + 8, size)
        occurrences[id] = occurrence + 1
        cursor = end + (size & 1)
    require cursor == limit
```

A tolerant reader may accept a missing final pad at physical EOF only under a named compatibility policy. It must report the discrepancy. RF64/BW64 use a 64-bit size resolver; a raw `0xffffffff` length is not a seek distance.

Unknown chunks can be skipped structurally. A preserving writer should retain their bytes, IDs, placement context, and alignment unless an edit invalidates them. For example, changing audio duration may invalidate opaque slice indexes even when their bytes can be copied.

<a id="wave"></a>
## 4. WAVE, sampler metadata, BWF, RF64, and BW64

**Sources:** [S1], [S2], [S3], [S4], [S20].

### 4.1 Recognition and root

| File offset | Type | Expected value |
|---:|---|---|
| 0 | `bytes[4]` | `RIFF`, or extended-size `RF64`/`BW64` |
| 4 | `u32le` | Root size, normally file length minus 8 |
| 8 | `bytes[4]` | `WAVE` |
| 12 | Chunk sequence | `fmt `, `data`, and optional metadata |

The physical file may contain trailing material beyond the declared root. A strict parser does not interpret it as part of the WAVE. A compatibility reader may retain it separately.

`RIFX` is a big-endian RIFF-family variant, not ordinary little-endian WAVE. Support it explicitly or return an unsupported-variant result.

### 4.2 `fmt ` payload

| Offset | Type | Field | Meaning |
|---:|---|---|---|
| 0 | `u16le` | formatTag | Encoding tag |
| 2 | `u16le` | channels | Channel count |
| 4 | `u32le` | sampleRate | Frames per second |
| 8 | `u32le` | avgBytesPerSecond | Storage/transport rate |
| 12 | `u16le` | blockAlign | Bytes in a PCM frame, or codec block alignment |
| 14 | `u16le` | bitsPerSample | PCM container width or codec-specific value |
| 16 | `u16le`, when present | cbSize | Bytes of format extension following this field |
| 18 | `bytes[cbSize]` | extension | Format-specific data |

The basic PCM payload is 16 bytes. Non-PCM commonly uses at least 18. Require the actual payload to contain the claimed extension.

| Tag | Encoding | Reader action |
|---:|---|---|
| `0x0001` | Integer PCM | Decode according to channel count, width, and frame alignment |
| `0x0003` | IEEE float | Commonly 32- or 64-bit samples |
| `0x0006` | G.711 A-law | Decode each companded byte with a G.711 codec |
| `0x0007` | G.711 mu-law | Decode each companded byte with a G.711 codec |
| `0xfffe` | WAVEFORMATEXTENSIBLE | Parse the full extension and subtype GUID |
| Other | Codec-specific | Preserve metadata and require that codec; do not reinterpret as PCM |

For conventional PCM, require nonzero channels and sample rate, byte-aligned supported container width, and:

```text
blockAlign = channels * (bitsPerSample / 8)
avgBytesPerSecond = sampleRate * blockAlign
```

Some historical WAVE files misuse width fields or carry padded/nonstandard layouts. Handle those only as explicit variants; do not weaken the standard PCM calculation globally.

### 4.3 WAVEFORMATEXTENSIBLE

The payload is normally 40 bytes, with `cbSize >= 22`.

| Offset | Type | Field |
|---:|---|---|
| 18 | `u16le` | Valid bits per sample for PCM; union meaning differs for some codecs |
| 20 | `u32le` | Speaker/channel mask |
| 24 | `bytes[16]` | Subformat GUID |

The PCM GUID is `00000001-0000-0010-8000-00aa00389b71`; float is `00000003-0000-0010-8000-00aa00389b71`.

GUID serialization is mixed-endian: the first 32-bit and next two 16-bit components are little-endian; the final eight bytes retain their byte order. Compare the entire GUID, not just the first tag-sized portion.

The channel mask assigns speakers in increasing set-bit order. Common bits are front-left `0x1`, front-right `0x2`, front-center `0x4`, LFE `0x8`, back-left `0x10`, back-right `0x20`. A zero mask does not establish a surround layout. Preserve an unspecified/discrete layout instead of guessing.

### 4.4 `data` and `fact`

`data` contains encoded audio bytes. For PCM, frames are interleaved in channel order. Seek using `blockAlign`, not `channels * validBits / 8`.

`fact` begins with a `u32le` decoded sample-length value. It is normally the decoded frame count per channel, but historical compressed writers are inconsistent. Compare it with codec output and any 64-bit size metadata. Do not use it as an unchecked allocation length or multiply it by channels without establishing the convention.

A reader should scan the chunk directory before deciding where audio starts. **There is no universal 44-byte WAVE header.** Metadata may precede `fmt `, occur between `fmt ` and `data`, or follow `data`.

Multiple `data` chunks and `LIST/wavl` are separate profiles. A simple reader should report them as unsupported, rather than quietly play only the first chunk while claiming complete decoding.

### 4.5 `smpl`: pitch and loops

The fixed prefix is 36 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Manufacturer |
| 4 | `u32le` | Product |
| 8 | `u32le` | Sample period in nanoseconds |
| 12 | `u32le` | MIDI unity note |
| 16 | `u32le` | MIDI pitch fraction |
| 20 | `u32le` | SMPTE format |
| 24 | `u32le` | SMPTE offset |
| 28 | `u32le` | Loop count |
| 32 | `u32le` | Sampler-specific data byte count |
| 36 | Loop records | 24 bytes each |

Each loop record:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Cue/loop identifier |
| 4 | `u32le` | Type: 0 forward, 1 alternating, 2 backward |
| 8 | `u32le` | Start frame |
| 12 | `u32le` | **Inclusive** end frame |
| 16 | `u32le` | Fractional loop position |
| 20 | `u32le` | Play count; 0 means indefinitely |

Require `36 + loopCount * 24 + samplerDataBytes <= payloadSize`, using checked arithmetic. Convert an integer loop to `[start, end + 1)`. Preserve unsupported loop types and fractions instead of silently converting everything to a forward loop.

The MIDI pitch fraction is a fractional semitone in units of `2^-32`. An asset's nominal root is therefore `unityNote + pitchFraction / 2^32`. Keep the distinction between root-pitch metadata and a region's independent tuning correction.

### 4.6 `cue ` and labels

`cue ` begins with `u32le count`, followed by 24-byte records:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Cue ID |
| 4 | `u32le` | Position |
| 8 | `bytes[4]` | Referenced data chunk ID |
| 12 | `u32le` | Chunk start |
| 16 | `u32le` | Block start |
| 20 | `u32le` | Sample offset |

For the common single-`data` PCM profile, `sampleOffset` is a frame position. The other address fields matter for more complex WAVE organizations; do not universally replace them with a flat PCM interpretation.

`LIST/adtl` may contain `labl` and `note`: each begins with a cue ID followed by text. `ltxt` adds length and text-region information. Resolve associations by cue ID, not by record order. `LIST/INFO` contains descriptive metadata; `iXML` is XML metadata, not audio.

### 4.7 Broadcast Wave `bext`

BWF is WAVE plus broadcast metadata, not a different audio codec. The `bext` fixed prefix is 602 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `char[256]` | Description |
| 256 | `char[32]` | Originator |
| 288 | `char[32]` | Originator reference |
| 320 | `char[10]` | Origination date, normally `YYYY-MM-DD` |
| 330 | `char[8]` | Origination time, normally `HH:MM:SS` |
| 338 | `u32le` | Time-reference low word |
| 342 | `u32le` | Time-reference high word |
| 346 | `u16le` | BWF metadata version |
| 348 | `bytes[64]` | UMID area |
| 412 | `i16le` | Loudness value |
| 414 | `i16le` | Loudness range |
| 416 | `i16le` | Maximum true-peak level |
| 418 | `i16le` | Maximum momentary loudness |
| 420 | `i16le` | Maximum short-term loudness |
| 422 | `bytes[180]` | Reserved |
| 602 | Remaining bytes | Coding history |

Combine the time reference as `(high << 32) | low`. Its unit is sample frames since midnight at the file's sample rate, not milliseconds and not bytes. Do not derive a unique date/time without considering origination-date metadata and any external clock context.

Version 0 does not define the UMID as version 1 does; version 2 assigns the loudness fields. Interpret fields according to `version`. Loudness values use hundredths of the respective units; `0x7fff` means unspecified. True peak uses dBTP; integrated/momentary/short-term loudness use loudness units relative to full scale; range is in LU.

Treat coding history as length-bounded text. Updating sample rate or audio processing may require updating history and invalidating measured loudness values. Do not copy stale measurements as if newly measured.

### 4.8 RF64 and BW64 `ds64`

Both retain WAVE chunking and little-endian integers while allowing 64-bit sizes. The root ID is `RF64` or `BW64`, the form is `WAVE`, and a mandatory `ds64` chunk immediately follows the root header. The root's 32-bit size uses `0xffffffff` when the 64-bit size is authoritative.

`ds64` payload:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u64le` | Root size, still excluding the initial eight bytes |
| 8 | `u64le` | Primary `data` payload size |
| 16 | `u64le` | RF64 sample count; **reserved/dummy in BW64** |
| 24 | `u32le` | Size-table entry count |
| 28 | Entries | 12 bytes each: FOURCC followed by `u64le size` |

**Do not read BW64 offset 16 as an audio-frame count.** BS.2088-2 requires that field to be ignored when reading and zero when writing; it exists for RF64 compatibility.

Require `28 + 12 * tableLength <= ds64Size`. Replace a chunk's `0xffffffff` size with the corresponding 64-bit value. Non-sentinel 32-bit sizes remain ordinary sizes. Do not store the size table as a single value per FOURCC if repeated chunks can occur; preserve occurrence ordering and reject unresolved/ambiguous sentinel references.

BW64 adds metadata such as `axml` and `chna` for Audio Definition Model descriptions and track identifiers. Parsing these chunk bytes does not implement object-audio rendering. Full ADM semantics require the associated standards.

For recording, a writer may reserve a sufficiently large `JUNK` chunk in a short-form WAVE, then replace it with `ds64` and update the root ID if sizes outgrow 32 bits. Never wrap a 64-bit length into a 32-bit field. New broadcast workflows should consider BW64 rather than treating legacy RF64 guidance as the current broadcast standard.

### 4.9 Minimal reader and writer

Reader: validate root; resolve sizes; index all chunks; parse `fmt `; select a supported subtype; validate frame/block alignment; locate audio; parse optional loops/markers/metadata; report unsupported codec or layout explicitly.

Writer: choose encoding and channel layout; write consistent `fmt `; write audio in whole frames; calculate sizes excluding external pads; add metadata with valid references; finalize root size or `ds64`. Use extensible format when valid-bit width or channel layout requires it.

<a id="aiff"></a>
## 5. AIFF and AIFF-C

**Sources:** [S5], [S6].

### 5.1 Root and chunks

| File offset | Type | Field |
|---:|---|---|
| 0 | `bytes[4]` | `FORM` |
| 4 | `u32be` | Root payload size |
| 8 | `bytes[4]` | `AIFF` or `AIFC` |
| 12 | IFF chunks | Big-endian sizes, even-byte padding |

Required audio structures are `COMM` and `SSND`. The standard AIFC profile additionally requires `FVER`; its version timestamp is `0xa2805140`. A reader accepting an AIFC without `FVER` should identify that compatibility deviation.

### 5.2 `COMM`

AIFF `COMM` has an 18-byte payload:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u16be` | Channels |
| 2 | `u32be` | Number of decoded sample frames |
| 6 | `u16be` | Valid bits per sample |
| 8 | `bytes[10]` | 80-bit extended sample rate |

AIFC appends:

| Offset | Type | Field |
|---:|---|---|
| 18 | `bytes[4]` | Compression FOURCC |
| 22 | `u8` + bytes | Pascal compression name: length byte then that many bytes |

The Pascal string occupies an even total number of bytes, including its length byte and any string pad. That alignment is separate from the outer IFF chunk pad.

| AIFC code | Encoding |
|---|---|
| `NONE` | Big-endian signed integer PCM |
| `sowt` | Little-endian signed integer PCM |
| `fl32` | Big-endian IEEE 32-bit float |
| `fl64` | Big-endian IEEE 64-bit float |
| `alaw`, `ulaw` | G.711 companded data |
| `ALAW`, `ULAW`, `FL32` | Historical implementation variants; support deliberately |
| Other | Codec-specific, not automatically PCM |

For AIFF and integer AIFC, the storage container is rounded up to a whole number of bytes, with valid signal bits most-significant-bit aligned. Channels are interleaved. Eight-bit PCM is signed.

### 5.3 Extended-precision sample rate

The ten-byte number contains a big-endian sign/exponent word and a big-endian 64-bit significand, with an explicit integer bit.

For a normal finite value:

```text
sign = word16 >> 15
exponent = word16 & 0x7fff
significand = read_u64be(next_8_bytes)
rate = (-1)^sign * significand * 2^(exponent - 16383 - 63)
```

Handle zero, denormals, infinities, NaNs, and noncanonical extended encodings separately. A practical audio reader requires a positive finite rate. Do not load this field as a host `double`, reverse it into an integer rate, or assume all rates are exactly integral.

Reference bytes for 44100 Hz are `40 0e ac 44 00 00 00 00 00 00`.

### 5.4 `SSND`

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32be` | Offset to first sound-data byte after this prefix |
| 4 | `u32be` | Block size for aligned sound data |
| 8 | Bytes | Offset padding, then encoded audio |

```text
audio_start = SSND_payload_start + 8 + offset
available_audio_bytes = SSND_size - 8 - offset
```

Require `SSND_size >= 8` and `offset <= SSND_size - 8`. `blockSize` is not a per-frame width. For ordinary uncompressed PCM, use channel count and the rounded container width to compute frames, and compare with `COMM.numSampleFrames`.

An odd audio payload, nonzero `SSND.offset`, or metadata before `SSND` must not break seeking.

### 5.5 `MARK` and `INST`

`MARK` begins with a big-endian `u16 markerCount`. Each marker consists of:

| Relative offset | Type | Field |
|---:|---|---|
| 0 | `u16be` | Marker ID |
| 2 | `u32be` | Frame position |
| 6 | Pascal string | Name, with its own even-byte alignment |

Marker records are variable-length. Resolve by ID; validate IDs and positions independently.

`INST` payload is 20 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u8` | Base MIDI note |
| 1 | `i8` | Detune in cents |
| 2 | `u8` | Low note |
| 3 | `u8` | High note |
| 4 | `u8` | Low velocity |
| 5 | `u8` | High velocity |
| 6 | `i16be` | Gain in dB |
| 8 | `u16be` | Sustain loop mode |
| 10 | `u16be` | Sustain begin marker ID |
| 12 | `u16be` | Sustain end marker ID |
| 14 | `u16be` | Release loop mode |
| 16 | `u16be` | Release begin marker ID |
| 18 | `u16be` | Release end marker ID |

Modes: 0 disabled, 1 forward, 2 forward/backward. Marker IDs are not frame positions.

Endpoint handling is a compatibility concern: the libsndfile profile resolves the end directly to its marker position, whereas other sampler conventions may describe a final included frame. Retain original marker positions and record the selected convention. This guide does not impose WAVE's `end + 1` rule on AIFF.

Sustain and release loops are separate articulations, not two interchangeable entries. A parser should preserve both even if a simple player supports only the sustain loop.

### 5.6 Writer guidance

Write `COMM` using the actual decoded frame count and exact supported sample-rate encoding; write `SSND` with explicit offset and block-size fields; write stable marker IDs and verify loop references. Use `AIFC` when the encoding requires a compression code, even for `sowt` PCM. Preserve application-specific chunks only when their relationship to edited audio remains valid.

<a id="caf"></a>
## 6. Core Audio Format

**Source:** [S7]. CAF framing is neither RIFF nor AIFF.

### 6.1 File and chunk headers

| File offset | Type | Field |
|---:|---|---|
| 0 | `bytes[4]` | `caff` |
| 4 | `u16be` | Version, normally 1 |
| 6 | `u16be` | File flags, normally 0 |
| 8 | Chunk sequence | Twelve-byte chunk headers |

A CAF chunk header is FOURCC followed by **signed `i64be` payload size**. There is no RIFF-style implicit odd-byte pad. Negative sizes are invalid except for the permitted unknown-length final audio-data chunk.

The required `desc` chunk appears immediately after the file header.

### 6.2 `desc` audio description

Its payload is 32 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `f64be` | Sample rate |
| 8 | `bytes[4]` | Format ID, e.g. `lpcm` |
| 12 | `u32be` | Format flags |
| 16 | `u32be` | Bytes per packet |
| 20 | `u32be` | Frames per packet |
| 24 | `u32be` | Channels per frame |
| 28 | `u32be` | Bits per channel |

For CAF `lpcm`, the on-disk flags are:

| Bit | Meaning |
|---:|---|
| 0 | Samples are floating point rather than signed integer |
| 1 | Audio bytes are little-endian rather than big-endian |

**Do not substitute the bit definitions of a generic host AudioStreamBasicDescription.** CAF's serialized linear-PCM flag meanings are a distinct format rule.

For the simple interleaved LPCM profile, frames per packet is 1; bytes per packet gives the full frame width. Integer data is signed, including eight-bit data. Valid integer bits in a larger container are high-aligned. Floating-point width must match an implemented IEEE representation.

Compute container bytes per channel as `bytesPerPacket / channels`, requiring exact divisibility. For 24-bit stereo, `bytesPerPacket=6` denotes packed three-byte samples, while 8 denotes high-aligned 24-bit values in four-byte containers. Do not use `ceil(bitsPerChannel / 8)` alone to seek unpacked CAF audio.

### 6.3 `data`

The data payload starts with a big-endian `u32 editCount`, followed by encoded audio. Its declared size includes those four bytes.

A final `data` chunk may use size -1 to indicate that it extends to EOF. Reject a size less than -1, a -1 size on an inappropriate chunk, or any attempt to parse later chunks after an EOF-extending data chunk.

For fixed-width LPCM:

```text
audio_bytes = data_size - 4
frames = audio_bytes / bytes_per_packet
```

Require exact alignment and nonzero packet width.

### 6.4 Compressed CAF

Format IDs such as `aac ` and `alac` select codecs; they are not AIFF compression codes or aliases for PCM.

`kuki` holds codec configuration when required. `pakt` describes packet indexing and valid decoded frames:

| Offset | Type | Field |
|---:|---|---|
| 0 | `i64be` | Number of packets |
| 8 | `i64be` | Number of valid frames |
| 16 | `i32be` | Priming frames |
| 20 | `i32be` | Remainder frames |
| 24 | Variable packet descriptions | Present when sizes/durations are variable |

Packet-size and duration integers use variable-length seven-bit groups, most-significant group first, with the high bit indicating continuation. For each packet, read a byte count if `desc.bytesPerPacket` is zero, then a frame count if `desc.framesPerPacket` is zero. A nonzero description value supplies the corresponding constant instead. Bound continuation length, integer overflow, packet count, and cumulative byte lengths.

```text
read_packet_integer():
    value = 0
    repeat with a bounded byte count:
        byte = read_u8()
        require value <= maximum_integer >> 7
        value = (value << 7) | (byte & 0x7f)
        if (byte & 0x80) == 0:
            return value
    fail("overlong packet integer")
```

For example, `81 02` encodes 130. For constant-byte/constant-frame formats, `pakt` may contain only the 24-byte header with packet count 0, while still supplying priming/remainder information. Derive physical packet count from the audio length and constant byte width in that case; do not interpret zero as an empty recording.

Codec output may include priming and remainder frames that are not valid program audio. An asset reader must account for them before exposing sample-frame coordinates to an instrument.

This guide specifies the LPCM profile and the compressed-container dependencies; it does not include AAC or ALAC codec algorithms.

### 6.5 Channel layout

The optional `chan` payload has `u32be layoutTag`, `u32be channelBitmap`, and `u32be descriptionCount`, followed by 20-byte descriptions: label `u32be`, flags `u32be`, and three `f32be` coordinates. Require `12 + descriptionCount * 20 <= payloadSize`.

Tags, bitmap, and explicit descriptions are different layout representations. Their full label and coordinate conventions belong to the CAF/Core Audio definitions; preserve the selected representation rather than assign speakers solely from channel count.

### 6.6 Writer guidance

Write the required `desc` first. For LPCM, make description flags and actual data byte order agree, and include the edit count in `data` size. For compressed audio, use codec-produced configuration and packet descriptions; do not synthesize packet lengths from a PCM frame-width formula.

<a id="compressed-assets"></a>
## 7. FLAC and Ogg audio assets

**Sources:** [S8], [S9], [S10], [S11]. These are asset-level profiles, not complete codec implementations.

### 7.1 Native FLAC framing

A native FLAC stream starts with `fLaC`, followed by metadata blocks and then audio frames.

Each metadata header is four bytes:

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `u8` | Bit 7: last-metadata-block flag; low 7 bits: block type |
| 1 | `u24be` | Metadata payload length |
| 4 | Payload | No RIFF padding |

Types: 0 STREAMINFO, 1 padding, 2 application, 3 seek table, 4 Vorbis comment, 5 cuesheet, 6 picture; type 127 is forbidden. STREAMINFO must be first, exactly 34 bytes, and occur once.

### 7.2 FLAC STREAMINFO

| Payload offset | Width | Field |
|---:|---|---|
| 0 | `u16be` | Minimum block size in frames |
| 2 | `u16be` | Maximum block size in frames |
| 4 | `u24be` | Minimum encoded frame size, 0 if unknown |
| 7 | `u24be` | Maximum encoded frame size, 0 if unknown |
| 10 | Packed 64 bits | Rate, channel count, bit depth, total frames |
| 18 | `bytes[16]` | MD5 of decoded audio, or zero if unknown |

For the packed 64-bit big-endian value X:

```text
rate = X >> 44
channels = ((X >> 41) & 7) + 1
bits_per_sample = ((X >> 36) & 31) + 1
total_frames = X & ((1 << 36) - 1)
```

Zero total frames means unknown. The count is per-channel time frames, not all channel points combined.

A seek-table entry is 18 bytes: sample number `u64be`, stream-relative byte offset `u64be`, and frame sample count `u16be`. The byte offset is relative to the first audio frame, not the file start. The all-ones sample number denotes a placeholder.

Vorbis comments use little-endian length fields inside FLAC metadata. They do not inherit STREAMINFO's big-endian integer convention.

### 7.3 FLAC decoding boundary

Audio frames contain a header, per-channel subframes, predictive/residual coding, and checksums. Encoded subframes are not ordinary interleaved PCM bytes. A decoder must implement the current RFC's fixed/LPC prediction, Rice residuals, wasted bits, channel decorrelation, and checksum rules, or use a qualified codec library.

For sample playback, expose the original rate and exact decoded integer precision. FLAC is lossless; converting it to floating point or lossy audio should be an explicit output choice. Ordinary FLAC comments are not a standardized replacement for an instrument's region map or loop-mode semantics.

### 7.4 Ogg page framing

An Ogg page has a 27-byte fixed header, followed by a lacing table and body:

| Offset | Type | Field |
|---:|---|---|
| 0 | `bytes[4]` | `OggS` |
| 4 | `u8` | Version, normally 0 |
| 5 | `u8` | Flags: continuation 1, BOS 2, EOS 4 |
| 6 | `u64le` | Granule position; all ones denotes unavailable |
| 14 | `u32le` | Logical stream serial number |
| 18 | `u32le` | Page sequence number |
| 22 | `u32le` | Page CRC |
| 26 | `u8` | Number of lacing segments |
| 27 | `u8[segmentCount]` | Segment lengths |
| `27 + segmentCount` | Bytes | Body, length equal to sum of lacing values |

A lacing value of 255 continues the current packet; a value below 255 terminates it. A packet whose length is an exact multiple of 255 needs a terminating zero segment. Packets may span pages. Preserve separate reassembly state by serial number and honor the continuation flag.

Ogg CRC uses polynomial `0x04c11db7`, a zero initial register, and the checksum bytes set to zero during calculation. It is not interchangeable with an arbitrary reflected CRC-32 helper.

### 7.5 Codec mapping and timing

Vorbis identification begins with packet type 1 followed by `vorbis`; comment and setup headers follow before audio. Vorbis bitstream fields use their own bit-packing rules.

Opus identification begins with `OpusHead`, followed by `OpusTags`. Ogg Opus uses a 48 kHz granule clock irrespective of the informational original input-rate field; apply pre-skip and final trimming according to RFC 7845.

A granule position is codec-defined. Do not call every Ogg granule a frame number at the asset's nominal input sample rate. Chained streams may change rate or channels; a simple instrument asset profile should reject such changes or split them into separate assets.

MP3, AAC, and other perceptual codecs additionally require delay/padding handling for reliable sample coordinates. This document does not specify their complete frame syntax or codec algorithms.

<a id="soundfont"></a>
## 8. SoundFont 2

**Source:** [S12]. The structural profile covers ordinary RAM-based SoundFont 2.01 and the 2.04 24-bit extension. SoundFont 1, ROM-resident sample retrieval, and compressed SF3 audio are separate variants.

### 8.1 Container tree

```text
RIFF 'sfbk'
    LIST 'INFO'
        ifil, isng, INAM, optional descriptive chunks
    LIST 'sdta'
        smpl
        sm24                         # optional, version 2.04
    LIST 'pdta'
        phdr pbag pmod pgen inst ibag imod igen shdr
```

All binary table values are little-endian. The nine `pdta` tables have prescribed order and fixed record strides. Reject a payload whose size is not an exact multiple of its stride.

`ifil` is four bytes: major `u16le`, minor `u16le`. INFO string chunks are length-bounded, normally NUL-terminated ASCII with RIFF alignment.

### 8.2 Hydra records

| Table | Stride | Fields in record order |
|---|---:|---|
| `phdr` | 38 | Name[20], preset u16, bank u16, bagIndex u16, library u32, genre u32, morphology u32 |
| `pbag` | 4 | generatorIndex u16, modulatorIndex u16 |
| `pmod` | 10 | source u16, destination u16, amount i16, amountSource u16, transform u16 |
| `pgen` | 4 | generatorOperator u16, amount union[2] |
| `inst` | 22 | Name[20], bagIndex u16 |
| `ibag` | 4 | generatorIndex u16, modulatorIndex u16 |
| `imod` | 10 | Same layout as `pmod` |
| `igen` | 4 | Same layout as `pgen` |
| `shdr` | 46 | Sample header, detailed below |

`phdr` offsets: name 0, preset 20, bank 22, bag index 24, library 26, genre 30, morphology 34. `inst.bagIndex` is at 20.

The amount union may mean signed `i16`, unsigned index, or two `u8` range endpoints. Its interpretation depends on the generator operator. Do not parse all amounts as signed or all as unsigned.

### 8.3 Index chains and terminal records

The final preset/instrument/sample headers are conventionally named `EOP`, `EOI`, and `EOS`. Bag, generator, and modulator tables also contain terminal entries. They establish range boundaries and are not playable objects.

For preset P:

```text
bag_start = phdr[P].bagIndex
bag_end = phdr[P + 1].bagIndex
for b in [bag_start, bag_end):
    generators = pgen[pbag[b].generatorIndex :
                      pbag[b + 1].generatorIndex]
    modulators = pmod[pbag[b].modulatorIndex :
                     pbag[b + 1].modulatorIndex]
```

Instrument traversal substitutes `inst`, `ibag`, `igen`, and `imod`. Every index must be monotonic and in bounds; successor entries must exist. Check the numeric terminal relationships, not just the terminal names.

A first zone without the appropriate object-link generator may be a global zone. A local preset zone links to an instrument through generator 41. A local instrument zone links to a sample through generator 53. Global zones do not produce voices directly.

### 8.4 `shdr` sample headers

| Offset | Type | Field |
|---:|---|---|
| 0 | `char[20]` | Sample name |
| 20 | `u32le` | Start point |
| 24 | `u32le` | End point, exclusive |
| 28 | `u32le` | Loop start point |
| 32 | `u32le` | Loop end point, exclusive |
| 36 | `u32le` | Sample rate |
| 40 | `u8` | Original MIDI pitch |
| 41 | `i8` | Pitch correction in cents |
| 42 | `u16le` | Linked sample-header index |
| 44 | `u16le` | Sample type |

Addresses are **sample points relative to the `smpl` pool**, not bytes and not offsets relative to the individual sample. `end` and `endLoop` are exclusive.

Sample types: 1 mono, 2 right, 4 left, 8 linked; bit `0x8000` marks ROM data. A stereo pair consists of linked sample headers and separate mono sample streams. Do not reinterpret `smpl` as inherently interleaved stereo.

Validate mutual links, complementary left/right types, compatible rate and length, and usable loop alignment before pairing channels. Preserve an unmatched channel with a diagnostic rather than dereferencing an invalid link.

Original pitch 255 means unpitched; the specification supplies a middle-C fallback for invalid/unpitched cases. Pitch correction has a meaningful sign: a recording four cents sharp can carry -4 cents to correct it.

### 8.5 `smpl` and `sm24`

`smpl` stores signed 16-bit little-endian points. Each sample is followed by at least 46 zero-valued guard points outside its playable `end`. These are sample data, not RIFF pad bytes.

For valid 2.04 `sm24`, one low byte corresponds to each `smpl` point:

```text
value24 = signed16(smpl[i]) * 256 + unsigned8(sm24[i])
```

Do not sign-extend the low byte.

The 2.04 specification's declared `sm24` length is half the `smpl` byte length, rounded up to an even byte count. Only the corresponding point-count bytes are signal bytes; an additional alignment byte is not a new sample point. This declared internal alignment is distinct from the general RIFF external pad rule.

Ignore `sm24` with a reported diagnostic when the declared version is below 2.04, `smpl` is absent, or the length is incompatible. Retain ordinary 16-bit `smpl` decoding. Some writers use only an external odd-byte pad; accepting that variant must be a separate compatibility policy.

### 8.6 Important generators

All ordinary scalar amounts below are signed 16-bit unless stated otherwise. Reserved operators must not be assigned invented semantics.

| ID | Operator | Units / interpretation | Instrument default |
|---:|---|---|---|
| 0 | startAddrsOffset | Points added to sample start | 0 |
| 1 | endAddrsOffset | Points added to sample end | 0 |
| 2 | startloopAddrsOffset | Points added to loop start | 0 |
| 3 | endloopAddrsOffset | Points added to loop end | 0 |
| 4 | startAddrsCoarseOffset | 32768-point units | 0 |
| 5 | modLfoToPitch | Cents | 0 |
| 6 | vibLfoToPitch | Cents | 0 |
| 7 | modEnvToPitch | Cents | 0 |
| 8 | initialFilterFc | Absolute cents | 13500 |
| 9 | initialFilterQ | Centibels | 0 |
| 10 | modLfoToFilterFc | Cents | 0 |
| 11 | modEnvToFilterFc | Cents | 0 |
| 12 | endAddrsCoarseOffset | 32768-point units | 0 |
| 13 | modLfoToVolume | Centibels | 0 |
| 15 | chorusEffectsSend | Tenths of a percent | 0 |
| 16 | reverbEffectsSend | Tenths of a percent | 0 |
| 17 | pan | -500 left, 0 center, +500 right | 0 |
| 21 | delayModLFO | Timecents | -12000 |
| 22 | freqModLFO | Absolute cents | 0 |
| 23 | delayVibLFO | Timecents | -12000 |
| 24 | freqVibLFO | Absolute cents | 0 |
| 25-28 | Mod envelope delay/attack/hold/decay | Timecents | -12000 each |
| 29 | sustainModEnv | Reduction from peak in tenths of a percent | 0 |
| 30 | releaseModEnv | Timecents | -12000 |
| 31-32 | Key scaling of mod-envelope hold/decay | Timecents per key | 0 |
| 33-36 | Volume envelope delay/attack/hold/decay | Timecents | -12000 each |
| 37 | sustainVolEnv | Attenuation in centibels | 0 |
| 38 | releaseVolEnv | Timecents | -12000 |
| 39-40 | Key scaling of volume-envelope hold/decay | Timecents per key | 0 |
| 41 | instrument | Unsigned instrument index; preset local zones | No default |
| 43 | keyRange | Two bytes: inclusive low key, high key | 0..127 |
| 44 | velRange | Two bytes: inclusive low velocity, high velocity | 0..127 |
| 45 | startloopAddrsCoarseOffset | 32768-point units | 0 |
| 46 | keynum | Fixed key substitution; -1 disables | -1 |
| 47 | velocity | Fixed velocity substitution; -1 disables | -1 |
| 48 | initialAttenuation | Positive centibels reduce level | 0 |
| 50 | endloopAddrsCoarseOffset | 32768-point units | 0 |
| 51 | coarseTune | Semitones | 0 |
| 52 | fineTune | Cents | 0 |
| 53 | sampleID | Unsigned sample-header index; instrument local zones | No default |
| 54 | sampleModes | Unsigned mode flags | 0 |
| 56 | scaleTuning | Cents per key | 100 |
| 57 | exclusiveClass | Nonzero class chokes matching voices | 0 |
| 58 | overridingRootKey | MIDI root override; -1 disables | -1 |

Operators 14, 18-20, 42, 49, and 55 are unused/reserved in this profile; operator 59 is unused and 60 is the terminal enumeration value. Do not turn table padding into meaningful synthesis controls.

Effective sample addresses use signed arithmetic:

```text
start = shdr.start + gen[0] + 32768 * gen[4]
end = shdr.end + gen[1] + 32768 * gen[12]
loop_start = shdr.startLoop + gen[2] + 32768 * gen[45]
loop_end = shdr.endLoop + gen[3] + 32768 * gen[50]
```

Validate the final intervals against the sample pool. Offsets may be negative; unsigned addition is incorrect.

`sampleModes & 3`: 0 no loop, 1 continuous loop, 3 sustain loop; 2 is reserved. Continuous looping may continue through release; sustain looping exits when the voice enters release.

### 8.7 Generator precedence

1. Start instrument parameters from SoundFont defaults.
2. Apply global instrument generators as replacements.
3. Apply local instrument generators as replacements.
4. Form preset contributions from its global zone, then replace matching contributions with local preset values.
5. Add allowed preset **value** contributions to the instrument values.
6. Intersect key and velocity ranges across levels.
7. Resolve index, substitution, and sample generators according to their operator rules; never treat them as generic additive controls.

Address offsets, sample selection, root substitution, and several playback controls are instrument-only. Applying every preset generator as either blanket replacement or blanket addition is wrong.

### 8.8 Units and modulation

```text
frequency_hz = 8.176 * 2^(absolute_cents / 1200)
duration_seconds = 2^(timecents / 1200)
amplitude_gain = 10^(-attenuation_centibels / 200)
```

Handle specified instantaneous/zero-time sentinels separately; -12000 timecents is approximately one millisecond, not zero.

For envelope key scaling, the hold/decay timecent adjustment is `(60 - effectiveKey) * keynumToEnvelopeHoldOrDecay`. Volume-envelope sustain is attenuation, while modulation-envelope sustain is a percentage reduction:

```text
volume_sustain_gain = 10^(-sustainVolEnv / 200)
modulation_sustain_level = 1 - sustainModEnv / 1000
```

Apply source-specific useful ranges and sentinels before evaluating DSP.

A modulator source word has a seven-bit source index, MIDI-CC selector bit 7, direction bit 8, polarity bit 9, and curve type in bits 10-15. The amount-source word uses the same encoding. Curve types 0, 1, 2, and 3 denote linear, concave, convex, and switch. A destination is a generator operator; transforms 0 and 2 denote linear and absolute value. Non-CC source indexes include 2 note velocity, 3 key number, 10 polyphonic pressure, 13 channel pressure, 14 pitch wheel, and 16 pitch-wheel sensitivity. Source 0 is the no-controller/constant factor, not a request to multiply every route by zero.

Default modulators exist even when a file contains no explicit modulator records. Their replacement identity depends on source, destination, amount-source, and transform. A zero amount can disable an otherwise matching default. A player that ignores modulators can parse a bank but cannot claim equivalent velocity, controller, envelope, or pitch-wheel behavior.

### 8.9 Writer and player guidance

Construct table indexes from finalized zone order; append terminal records; ensure every local zone links to its object; write global zones only in the permitted position. Put key/velocity ranges and final link generators in the ordering required by the specification. Write complete sample guards and mutual stereo links.

A basic player selects preset, matches every eligible local zone, resolves its instrument zones, and may create multiple layered voices. Do not select only the first matching zone. Apply tuning, loop mode, envelope, pan, attenuation, and default modulation before claiming playback fidelity.

<a id="sfz"></a>
## 9. SFZ instruments

**Source:** [S13]. SFZ is a text mapping format with a common core and engine-specific extensions. The portable profile below is intentionally smaller than every published opcode.

### 9.1 Grammar and scanning

An SFZ document contains headers such as `<global>`, `<group>`, and `<region>`, and assignments `opcode=value`. It is not XML despite the angle-bracket header syntax.

Values can include spaces, notably sample paths. **Splitting the whole document on whitespace is not a valid lexer.** A scanner must distinguish a subsequent opcode assignment or header from whitespace inside a path, and must recognize supported comments and preprocessor directives.

The common ecosystem supports `//` comments, and many engines support C-style block comments, `#include`, and `#define` macros. These features are dialect-scoped. A portable writer should use a conservative tested subset and avoid filenames containing header/assignment-looking syntax. Quoting behavior is not uniform enough to assume a universal shell-like quoting grammar.

For includes: retain the source file and directory of each expansion; detect cycles; bound depth and expansion size; apply an explicit asset-root policy. Macro expansion must not turn malformed input into silently accepted assignments.

Unknown opcodes should remain in the parsed representation with scope and source location. A player should report unsupported behavior affecting sound, rather than interpret an ignored opcode as successful full compatibility.

### 9.2 Scopes

Typical hierarchy:

```text
global
    master                         # common extension
        group
            region
```

Assignments in a more specific scope override inherited assignments. New headers replace/reset their corresponding active scope according to the dialect; they are not ordinary nested XML elements.

`<group>` is a scope header. `group=7` is a numeric voice-group/choke opcode. They are different mechanisms.

`<control>` contains file/control settings such as `default_path`; these must not become ordinary region attributes. Multiple `<global>` sections and extended `<master>` behavior require an engine profile.

### 9.3 Portable mapping opcodes

| Opcode | Meaning | Typical portable interpretation |
|---|---|---|
| `sample` | Audio asset path | Relative to the instrument/control path policy |
| `lokey`, `hikey` | Inclusive MIDI key range | 0..127 |
| `lovel`, `hivel` | Inclusive MIDI velocity range | 0..127; ordinary note-on velocity is 1..127 |
| `pitch_keycenter` | Root key | Default 60 in the common profile |
| `key` | Set low key, high key, and root together | Use numeric MIDI keys for portability |
| `transpose` | Pitch offset | Semitones |
| `tune` | Pitch offset | Cents |
| `pitch_keytrack` | Pitch change per played key | Common default 100 cents per key |
| `volume` | Gain adjustment | dB |
| `pan` | Pan position | -100..100 |
| `offset` | Playback start | Decoded sample frames |
| `end` | Playback end | Common profile uses an inclusive frame position |
| `loop_start` | Loop start | Decoded sample frames |
| `loop_end` | Loop end | **Inclusive** decoded frame position |
| `loop_mode` | Loop/note-off behavior | Detailed below |
| `trigger` | Event condition | Attack/release/first/legato variants |
| `group`, `off_by` | Voice grouping and choke relationship | Preserve exact group values |
| `seq_length`, `seq_position` | Sequential/round-robin selection | Requires event-state implementation |
| `lorand`, `hirand` | Random-selection interval | Requires a consistent per-event random policy |
| `loccN`, `hiccN` | Controller-condition range | N identifies the controller |

`key` is shorthand, not an independent fourth pitch parameter. Since assignment-order behavior differs among engines, portable writers should emit `key` first and any intended explicit overrides after it.

Note names and octave numbering vary across software. Numeric MIDI keys avoid ambiguity over whether `C3`, `C4`, or `C5` means MIDI 60.

### 9.4 Sample paths

`default_path` normally establishes a prefix for sample paths. Absolute path handling, separators, reset behavior, and interaction with included files are implementation-dependent. A portable export should use relative asset paths, a tested directory separator convention, and a trailing separator in a directory prefix.

Maintain both the original path spelling and the resolved path. Case-insensitive resolution on one operating system must not silently select a different asset on a case-sensitive system. Resolve missing files explicitly; never create a silent placeholder waveform.

Built-in/generated sample names such as `*silence` or oscillator forms are dialect features, not filesystem paths.

### 9.5 Loops and release

| Mode | Meaning |
|---|---|
| `no_loop` | Play without looping; normal envelope/note-off behavior |
| `one_shot` | Play the sample without normal note-off interruption |
| `loop_continuous` | Loop throughout playback, including release |
| `loop_sustain` | Loop while sustained; leave the loop on release |

If `loop_mode` is absent, the common specification uses embedded sample loop metadata when present, otherwise no loop. An exporter that wants no loop should write `loop_mode=no_loop`, rather than relying on an omitted field.

Normalize `loop_end` to an exclusive bound with checked `+1`. Validate loop bounds after applying trim/offset rules for the selected engine profile. A stereo loop is measured in frames, not separate left/right point counts.

### 9.6 Envelopes, filters, and triggers

Common amplitude controls include `ampeg_attack`, `ampeg_decay`, `ampeg_sustain`, and `ampeg_release`; time values are in seconds and sustain is commonly a percentage. Extended envelope curves, controller modulation, and dynamic variants require their opcode definitions, not a guessed ADSR model.

Filter opcodes such as `fil_type`, `cutoff`, and `resonance` require the corresponding filter model and units. Merely extracting values is not equivalent rendering.

Release triggers depend on note and pedal state. `release`, `release_key`, `first`, and `legato` are not interchangeable. Some engines require a corresponding eligible attack region for release behavior; others do not. Maintain a profile-specific event model and record unsupported trigger variants.

### 9.7 Minimal original example

```sfz
<control>
default_path=Samples\

<global>
ampeg_attack=0.005
ampeg_release=0.2

<group>
lovel=1 hivel=127

<region>
sample=Piano C4.wav
lokey=48 hikey=65 pitch_keycenter=60
loop_mode=no_loop

<region>
sample=Piano A4.wav
lokey=66 hikey=84 pitch_keycenter=69
loop_mode=no_loop
```

The space in `Piano C4.wav` belongs to the path. The next header, not the first space, terminates that value.

### 9.8 Writer guidance

Export explicit numeric key/velocity limits, root, tuning, loop mode, and endpoints. Reduce inherited scopes only after computing effective settings. Do not flatten choke, round-robin, or release-trigger behavior into ordinary overlapping attack regions.

A portable SFZ export plus PCM/FLAC assets is often preferable to inventing undocumented binary instrument records.

<a id="dls"></a>
## 10. Downloadable Sounds

**Sources:** [S14], [S15]. This profile describes ordinary RIFF DLS core structures and selected extension points, not the entire MIDI DLS synthesis specification.

### 10.1 Container tree

```text
RIFF 'DLS '
    colh
    ptbl
    LIST 'wvpl'
        LIST 'wave'
            fmt , data, optional wsmp, LIST 'INFO'
        ...
    LIST 'lins'
        LIST 'ins '
            insh
            LIST 'lrgn'
                LIST 'rgn ' or 'rgn2'
                    rgnh, wlnk, optional wsmp
                    optional LIST 'lart'/'lar2'
            optional LIST 'lart'/'lar2'
            optional LIST 'INFO'
```

All spaces in FOURCCs matter. The wave pool contains embedded WAVE-style chunks, not necessarily standalone `RIFF/WAVE` files.

### 10.2 Collection and instrument headers

`colh` begins with `u32le instrumentCount`.

`insh` is 12 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | **Region count for this instrument** |
| 4 | `u32le` | Bank encoding |
| 8 | `u32le` | Program/instrument number |

Bank bits 0-6 hold the bank-LSB component, bits 8-14 the bank-MSB component, and bit 31 identifies percussion. Retain reserved bits. Convert serialized program numbering to a UI's 1-based display only at the presentation boundary.

Do not confuse `insh.regionCount` with `colh.instrumentCount`.

### 10.3 Region header and wave link

`rgnh` core payload:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u16le` | Low key |
| 2 | `u16le` | High key |
| 4 | `u16le` | Low velocity |
| 6 | `u16le` | High velocity |
| 8 | `u16le` | Region options |
| 10 | `u16le` | Key group |
| 12 | Optional `u16le` | Level-2 layer, when present in that profile |

Ranges are inclusive. Preserve options such as self-nonexclusive behavior; a key group is articulation information, not a sample index.

Core region option bit `0x0001` is self-nonexclusive. Wave-link option bit `0x0001` marks a phase master. `wsmp` option bits `0x00000001` and `0x00000002` request no sample-depth truncation and no compression respectively. These flags describe behavior/preservation constraints, not permission to reinterpret an already encoded audio payload.

`wlnk` payload:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u16le` | Wave-link options |
| 2 | `u16le` | Phase group |
| 4 | `u32le` | Channel assignment |
| 8 | `u32le` | Pool-table index |

The pool-table index addresses `ptbl`, not a byte position and not necessarily the physical order of `LIST/wave` chunks. Phase grouping and channel assignment must survive conversion even when the initial player does not render them.

### 10.4 Pool table

`ptbl`:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Header size, `cbSize` |
| 4 | `u32le` | Cue count |
| `cbSize` | `u32le[cueCount]` | Ordinary DLS wave offsets |

Require `cbSize >= 8` and `cbSize + cueCount * 4 <= payloadSize`. The offset base is the first child byte after the `wvpl` list type. Each offset points to a `LIST/wave` chunk header.

```text
pool_base = wvpl_chunk_start + 12
wave_chunk_start = pool_base + cue_offset
```

Check that the resulting position names an indexed child wave list and lies inside the pool. A search for a matching offset is safer than trusting an arbitrary seek target.

Some Giga/libgig variants use wider pool-table entries. They are not standard 32-bit DLS cues; select them only under their identified variant.

### 10.5 `wsmp` and loops

`wsmp` sample parameters:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Header size, `cbSize` |
| 4 | `u16le` | Unity note |
| 6 | `i16le` | Fine tune, cents |
| 8 | `i32le` | Attenuation |
| 12 | `u32le` | Sample options |
| 16 | `u32le` | Loop count |
| `cbSize` | Loop records | Size-qualified records |

The ordinary fixed header is 20 bytes. Do not begin loop reading at byte 20 if a larger `cbSize` is declared.

Attenuation in the libgig-compatible DLS/Giga profile uses signed 16.16 **tenths of a decibel**:

```text
attenuation_db = raw / 655360
gain = 10^(-attenuation_db / 20)
```

Positive values reduce level. Preserve the signed raw value and reject overflow during conversion. This field is not a linear gain multiplier and is not an SF2 integer-centibel amount.

The basic loop record is 16 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Record size, normally 16 |
| 4 | `u32le` | Loop type |
| 8 | `u32le` | Start frame |
| 12 | `u32le` | Length in frames |

Type 0 is a forward loop in the core profile. Level-2 release-loop behavior requires the corresponding type semantics.

Normalize with `end = start + length`, not `start + length + 1`. Reject zero/invalid active loops and bounds outside the decoded waveform. Region sample parameters may override wave-level defaults; retain both scopes.

### 10.6 Articulation connections

`LIST/lart` contains `art1`; Level 2 uses `LIST/lar2` and `art2`. Articulation payloads start with:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32le` | Header size |
| 4 | `u32le` | Connection count |
| `headerSize` | Connection records | 12 bytes each |

Connection record:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u16le` | Source |
| 2 | `u16le` | Control |
| 4 | `u16le` | Destination |
| 6 | `u16le` | Transform |
| 8 | `i32le` | Scale |

Source/control zero can represent a constant contribution, but destination-specific rules still determine its meaning. Destination identifiers select gain, pitch, pan, filter, envelope, LFO, and other parameters. Scale is destination-dependent fixed-point data; do not universally call it cents, seconds, or dB.

Selected Level-1 enumerations:

| Kind | Value | Meaning |
|---|---:|---|
| Source | `0x0000` | None/constant |
| Source | `0x0001` | LFO |
| Source | `0x0002` | Note-on velocity |
| Source | `0x0003` | Key number |
| Source | `0x0004`, `0x0005` | Envelope 1, envelope 2 |
| Source | `0x0006` | Pitch wheel |
| Source | `0x0081`, `0x0087`, `0x008a`, `0x008b` | MIDI CC 1, 7, 10, 11 |
| Destination | `0x0001` | Attenuation |
| Destination | `0x0003` | Pitch |
| Destination | `0x0004` | Pan |
| Destination | `0x0104`, `0x0105` | LFO frequency, start delay |
| Destination | `0x0206`, `0x0207`, `0x0209`, `0x020a` | Envelope 1 attack, decay, release, sustain |

Level-2 transform packing and connection precedence are not equivalent to SoundFont modulator words. Preserve the raw tuple and evaluate it with a qualified DLS articulation implementation.

### 10.7 Reader, player, and writer

Reader: enumerate wave lists and offsets; parse their `fmt `/`data`; read pool cues; enumerate instruments/regions; resolve `wlnk`; combine applicable `wsmp`; preserve articulation scope.

Player: select bank/program and eligible key/velocity regions; honor layering and key-group behavior; apply pitch and gain; process note-off and loops. Parsing only PCM from the pool is waveform extraction, not DLS instrument playback.

Writer: finalize wave-list byte positions before writing `ptbl`, or backpatch the table; serialize region links using table indexes; write accurate collection/region counts; keep `cbSize` and connection/loop counts consistent.

<a id="giga"></a>
## 11. Giga instruments

**Source:** [S16], libgig revision 4304. This is an implementation-derived profile, not a complete specification of every Giga version.

### 11.1 DLS foundation and extensions

Giga builds on DLS-family RIFF organization, with wave-pool assets, instruments, regions, and additional chunks. Common extensions include:

| Chunk/list | Role in this profile |
|---|---|
| Wave `3gix` | Sample-group index, beginning with a 16-bit value |
| Wave `smpl` | WAVE-style sample pitch/loop metadata |
| Wave `ewav` | Marks compressed waveform data and carries codec/version metadata |
| Region `3lnk` | Dimension definitions and dimension-region wave links |
| Region `LIST/3prg` | Dimension-region organization |
| `LIST/3ewl`, `3ewa` | Dimension-region sample/articulation extensions |

A `fmt ` indicating 16- or 24-bit output does **not** mean compressed `data` can be read as PCM. Presence of the compression extension changes the data decoder.

### 11.2 Dimensions and `3lnk`

`3lnk` starts with `u32le dimensionRegionCount`. It then contains dimension-definition slots, eight bytes each:

| Slot offset | Type | Field |
|---:|---|---|
| 0 | `u8` | Dimension kind |
| 1 | `u8` | Number of selector bits |
| 2 | `u8` | Stored bit position |
| 3 | `u8` | Stored selector-mask information |
| 4 | `u8` | Zone count; newer variants may use non-power-of-two counts |
| 5 | `bytes[3]` | Reserved/extension bytes |

The examined public profile reads five slots for major versions up to 2 and eight slots for versions above 2. Wave-pool indexes begin at payload offset 44 or 68 respectively, as `u32le[dimensionRegionCount]`.

Inactive dimension kind 0 consumes no selector bits. A zero stored zone count for an active dimension falls back to `1 << bits` in this profile. Bound bit counts, total selector width, and region count before shifting or allocating.

Dimension selections can represent velocity, controller position, sample channel, layer, release behavior, and other conditions. The dimension kinds and split rules are version-sensitive. Do not assume all dimensions are ordinary key ranges.

The selector index combines each dimension's selected zone in its allocated bit position. Unused combinations may exist when zone counts are not powers of two. Validate the mapping against actual dimension-region entries and retain custom split thresholds.

### 11.3 Compressed frame modes

Compressed audio is divided into coding frames. Each frame begins with one mode byte for mono or two mode bytes for stereo. The profile supports:

| Mode | Decoded depth | Points/channel in full frame | Parameter bytes/channel | Residual/raw bits per point | Body bytes/channel including parameters |
|---:|---:|---:|---:|---:|---:|
| 0 | 16 | 2048 | 0 | 16 raw | 4096 |
| 1 | 16 | 2048 | 4 | 8 residual | 2052 |
| 2 | 24 | 256 | 0 | 24 raw | 768 |
| 3 | 24 | 256 | 12 | 16 residual | 524 |
| 4 | 24 | 256 | 12 | 12 residual | 396 |
| 5 | 24 | 256 | 12 | 8 residual | 268 |

The final column excludes the initial mode byte(s). Validate that modes belong to the declared decoded depth.

Parameter headers for all channels precede residual/raw streams. For 16-bit stereo, the left/right per-point encoded values are interleaved, even if their widths differ. For 24-bit stereo, encoded channel streams are planar: left stream followed by right stream.

Do not apply the 24-bit planar rule to 16-bit stereo or assume that every frame has two mode bytes.

### 11.4 Mode 1 reconstruction

Read two signed little-endian 16-bit parameters, Y and DY. For each signed 8-bit residual R:

```text
DY = DY - R
Y = Y - DY
emit signed16(low_16_bits(Y))
```

The first residual produces the first output point; the stored parameters are predictor state, not two initial emitted samples. Seeking into a frame requires replaying predictor updates through the skipped points.

Use explicitly defined fixed-width/wrapping arithmetic where required, rather than relying on language-specific signed-overflow behavior.

### 11.5 Modes 3-5 reconstruction

Read four signed little-endian 24-bit parameters P0..P3:

```text
Y = P0
DY = P0 - P1
DDY = P2
DDDY = P3
```

For each signed residual R:

```text
DDDY = DDDY - R
DDY = DDY - DDDY
DY = -DY - DDY
Y = Y + DY
emit low_24_bits(Y << truncated_bits)
```

Modes 3 and 5 use signed 16-bit and signed 8-bit residuals respectively. Mode 4 packs two signed 12-bit residuals in three bytes:

```text
A = byte0 | ((byte1 & 0x0f) << 8)
B = (byte1 >> 4) | (byte2 << 4)
if A & 0x800: A -= 0x1000
if B & 0x800: B -= 0x1000
```

Mode 2 stores raw signed 24-bit points and applies the same truncation-bit restoration when that metadata is present. For the examined `ewav` version-above-2, 24-bit profile, truncation metadata is read at payload offset 64 for mono or 84 for stereo. Validate extension size and shift range; do not assume that offset for other versions.

### 11.6 Indexing, final frames, and limits

Scan mode bytes and checked body lengths to build a frame-to-byte index. For full frames the lengths above are sufficient. A shorter final frame requires exact residual packing rules, including mode-4 nibble packing and stereo channel placement.

The public implementation's final-frame count estimation is not a license to trust truncated input. Reject insufficient parameter bytes and inconsistent channel lengths. An implementer should use codec fixtures for partial and odd-point final frames before claiming full compressed-sample compatibility.

This profile provides the core reconstruction rules, but not every `3ewa` envelope/filter/LFO field, convolution feature, split library, pool-offset extension, or later Giga variant. Preserve unidentified extension bytes. A full-fidelity instrument writer should use a version-qualified implementation rather than synthesize missing articulation data.

<a id="korg"></a>
## 12. Korg KMP and KSF

**Source:** [S17], libgig revision 4304. This profile covers the observed chunk structures of KMP multisamples and KSF sample assets, not every Korg product file bearing a related extension.

### 12.1 Flat chunk framing

KMP/KSF use a flat sequence of:

```text
id[4], payload_size_u32be, payload[payload_size]
```

There is no `RIFF/WAVE` root. Integer fields in the structures below are big-endian. The public implementation uses a flat big-endian chunk reader whose `RIFF::List::LoadSubChunks` still advances to an even byte position after a chunk. For this implementation profile:

```text
next_chunk = chunk_start + 8 + payload_size + (payload_size & 1)
```

The external pad is not part of the declared payload size. This is a verified reader convention, not a claim that all Korg product containers use RIFF organization.

Known chunks include KSF `SMP1`, `SMD1`, optional `NAME`, and KMP `MSP1`, `RLP1`, optional `NAME`.

### 12.2 KSF `SMP1`

Minimum payload size is 32:

| Offset | Type | Field |
|---:|---|---|
| 0 | `char[16]` | Short sample name |
| 16 | `u8` | Bank |
| 17 | `u24be` | First start value |
| 20 | `u32be` | Second start value |
| 24 | `u32be` | Loop start |
| 28 | `u32be` | Loop end |

Do not load the three-byte start as a four-byte integer: doing so consumes a byte from the next field.

### 12.3 KSF `SMD1`

Minimum header is 12 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32be` | Sample rate |
| 4 | `u8` | Attributes |
| 5 | `i8` | Loop tune |
| 6 | `u8` | Channels |
| 7 | `u8` | Bit depth |
| 8 | `u32be` | Stored count; decoded frames in this implementation profile |
| 12 | Bytes | Encoded audio |

Attribute bit `0x10` marks compressed audio in the implementation. Bit `0x20` affects alternate-start use, with the examined implementation enabling its second-start behavior when the bit is clear. A compression selector involving bit `0x04` is exposed, but its complete codec semantics are not specified here.

The public implementation names the count `SamplePoints`, but seeks and reads using `FrameSize = (BitDepth / 8) * Channels`. Its count therefore represents time frames rather than all channel points combined. For an uncompressed byte-aligned asset, require enough audio for `storedCount * FrameSize`, and preserve/report extra bytes rather than consume them as additional declared frames.

Uncompressed supported integer samples are read with the file's big-endian profile. The implementation itself notes a channel-order/endian-correction concern for multichannel reads; cross-check stereo channel order with actual fixtures before claiming complete multichannel fidelity.

**Unresolved playback conventions:** this guide does not assign definitive loop-end inclusivity or physical units to every tuning/level/filter byte. Preserve raw fields and require a generation-specific playback profile. Do not fabricate compressed KSF decoding from the 12-byte header.

### 12.4 KMP `MSP1` and `RLP1`

`MSP1` minimum payload is 18:

| Offset | Type | Field |
|---:|---|---|
| 0 | `char[16]` | Short multisample name |
| 16 | `u8` | Region count |
| 17 | `u8` | Attributes |

KMP attribute bit 0 is inverted in the examined second-start selection: `Use2ndStart = (attributes & 1) == 0`. Preserve both the multisample-level and KSF-level start-selection flags.

`RLP1` contains 18-byte region records:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u8` | Original key in low 7 bits; transpose flag in bit 7 |
| 1 | `u8` | Top key in low 7 bits |
| 2 | `i8` | Tune |
| 3 | `i8` | Level |
| 4 | `u8` | Pan in low 7 bits |
| 5 | `i8` | Filter-cutoff adjustment |
| 6 | `char[12]` | Referenced sample filename |

The first region's low key is normally 0; subsequent low keys follow the preceding top key plus one in the examined mapping profile. Check monotonic top keys and cap at MIDI 127. Regions do not carry independent low-key bytes.

Optional `NAME` stores a 24-byte long name in the public profile.

### 12.5 Asset resolution and writing

A conventional sample location is:

```text
directory containing instrument.kmp
    instrument.kmp
    instrument\
        referenced sample filename
```

The subdirectory is based on the KMP basename without its extension. Preserve the original short filenames and name fields; resolving arbitrary same-named samples elsewhere may change the instrument.

A structural KMP reader can enumerate mappings and KSF references using these layouts. Full playback/writing requires resolving the documented endpoint, count, and parameter-unit gaps for the intended Korg generation. Unknown product formats such as PCG/KSC are not automatically covered by KMP/KSF.

<a id="exs"></a>
## 13. EXS instruments

**Source:** [S18], exstools4 commit `d886a5994b7a46de1fc49aec4eb37e8d83b72d55`. This section is implementation-derived and intentionally distinguishes stored blocks from waveform assets.

### 13.1 Block framing

Each block has an 84-byte header:

| Block offset | Type | Field |
|---:|---|---|
| 0 | `u8` | Byte-order selector: 0 big-endian, 1 little-endian in this profile |
| 1 | `bytes[2]` | Version bytes |
| 3 | `u8` | Raw block type and flags |
| 4 | `u32` in selected order | Content size |
| 8 | `u32` in selected order | Object index |
| 12 | `u32` in selected order | Flags |
| 16 | `bytes[4]` | Magic |
| 20 | `char[64]` | Object name |
| 84 | Bytes | Block content |

Little-endian magic variants include `TBOS`/`JBOS`; big-endian variants include `SOBT`/`SOBJ`. Select byte order from the actual header profile, not host architecture.

The public parser derives the type as `rawType & 0x3f`: 0 instrument, 1 zone, 2 group, 3 sample, 4 parameters. For its identified high-bit `0x80` variant, it masks the content size with `0x7fff`. This is variant-specific behavior, not a universal EXS size rule.

```text
next_block = block_start + 84 + decoded_content_size
```

There is no general RIFF-style pad between these blocks. Bound each block against EOF and preserve unrecognized types.

### 13.2 Coordinate convention

Some public parsers call the bytes beginning at block offset 8 the "legacy data." That span contains 76 bytes of header before the actual content. **The tables below use content-relative offsets**, so content offset 0 is file position `block_start + 84`. Do not add 76 twice.

### 13.3 Zone core

The examined core content is at least 104 bytes:

| Content offset | Type | Field |
|---:|---|---|
| 0 | `u8` | Options |
| 1 | `u8` | Root note |
| 2 | `i8` | Fine tune |
| 3 | `i8` | Pan |
| 4 | `i8` | Volume |
| 5 | `i8` | Volume scale |
| 6 | `u8` | Low key |
| 7 | `u8` | High key |
| 8 | Byte | Reserved/unmapped |
| 9 | `u8` | Low velocity |
| 10 | `u8` | High velocity |
| 11 | Byte | Reserved/unmapped |
| 12 | `u32` | Sample start |
| 16 | `u32` | Sample end |
| 20 | `u32` | Loop start |
| 24 | `u32` | Loop end |
| 28 | `u32` | Loop crossfade |
| 32 | `i8` | Loop detune |
| 33 | `u8` | Loop flags |
| 34 | `u8` | Loop mode |
| 35 | `bytes[42]` | Reserved/unmapped core area |
| 77 | `u8` | Flex flags |
| 78 | `i8` | Speed |
| 79 | `i8` | Tail tune |
| 80 | `i8` | Coarse tune |
| 81 | Byte | Reserved/unmapped |
| 82 | `u8` | Output |
| 83 | `bytes[5]` | Reserved/unmapped |
| 88 | `i32` | Group reference |
| 92 | `u32` | Sample reference |
| 96 | `bytes[8]` | Reserved/unmapped |

All multibyte integers use the block's selected order.

The examined newer profile also reads a signed flex-pitch byte at content offset 50 inside the previously unmapped area. Treat that as an identified field for this profile while retaining the remainder of the area unchanged.

Options in this profile: bit 0 one-shot; bit 1 disables pitch tracking; bit 2 reverse; bit 3 enables velocity-range filtering; bit 4 mute. Loop flag bit 0 enables the loop and bit 1 selects equal-power crossfade. Additional loop-flag meaning is not established here.

Key and velocity endpoints are inclusive. Group reference is signed and can carry a sentinel; do not reinterpret it as an unsigned array index. Resolve object references against the selected variant's index rules and preserve header indexes.

The structural profile does not conclusively establish every sample/loop endpoint or encoded gain/pan unit across generations. Keep raw values and choose a verified renderer profile rather than reusing WAVE or SFZ endpoint rules.

Appended zone fields in the public profile:

| Content offset | Type | Field | Minimum content length |
|---:|---|---|---:|
| 104 | `u32` | Fade-out frame count | 108 |
| 108 | `u32` | Synchronization frame offset | 128 |
| 112 | `i32` | Tail sample reference | 128 |
| 116 | `u32` | Tail start frame | 128 |
| 120 | `u32` | Tail end frame | 128 |
| 124 | `f32` | Precise zone volume, read directly from an otherwise padded field | 128 |
| 132 | `f32` | Tail volume | 136 |
| 136 | `u32` | Fade-in frame count | 140 |

Floats use the block's byte order. The content lengths 104, 108, 128, 132, 136, and 140 correspond to the public parser's growing zone variants; do not read a newer field from a shorter block.

### 13.4 Sample-reference core

Two common content sizes are 336 and 592 bytes, corresponding to a folder-only record and a record with an additional filename. Some variants append eight more bytes.

| Content offset | Type | Field |
|---:|---|---|
| 0 | `u32` | Data offset/reference information |
| 4 | `u32` | Frame count |
| 8 | `u32` | Sample rate |
| 12 | `u32` | Bit depth |
| 16 | `u32` | Channels |
| 20 | `u32` | Repeated channel field |
| 24 | `bytes[4]` | Reserved |
| 28 | `bytes[4]` | File type |
| 32 | `u32` | File size |
| 36 | `u32` | Compressed flag |
| 40 | `bytes[40]` | Reserved/unmapped |
| 80 | `char[256]` | Folder/path component |
| 336 | Optional `char[256]` | Filename |

The object name is in the block header, not repeated at content offset 0. When the filename is absent, the source profile derives path behavior from the name/folder representation.

An EXS file commonly references external WAV/AIFF assets. Newer consolidated variants can reference CAF with ALAC/AAC data and auxiliary editor/property-list blocks. The data-offset field is therefore not universally an offset into a standalone PCM file.

Validate stored rate/channel/frame metadata against the resolved asset, but do not use an old reference record to override a newly decoded asset silently.

### 13.5 Group core and extension boundaries

Group variants append fields; the public profile recognizes legacy span sizes 168, 172, 196, 200, and 208 bytes, equivalent to content sizes 92, 96, 120, 124, and 132. Select a layout that fits the actual declared content.

Useful core fields:

| Content offset | Type | Field |
|---:|---|---|
| 0 | `i8` | Volume |
| 1 | `i8` | Pan |
| 2 | `u8` | Voice limit |
| 3 | `u8` | Options |
| 4 | `u8` | Exclusive class |
| 5 | `u8` | Low velocity |
| 6 | `u8` | High velocity |
| 7 | `i8` | Sample-selection random offset |
| 16 | `u32` | Release-trigger decay-time engine field |
| 32 | `u8` | Velocity crossfade, interpreted as stored value minus 128 |
| 33 | `u8` | Velocity crossfade type |
| 34 | `u8` | Note crossfade type |
| 35 | `u8` | Note crossfade, interpreted as stored value minus 128 |
| 38 | `u8` | Low tempo selector |
| 39 | `u8` | High tempo selector |
| 41 | `i8` | Filter-cutoff offset |
| 43 | `i8` | Filter-resonance offset |
| 56 | `i32` | Envelope 1 attack offset |
| 60 | `i32` | Envelope 1 decay offset |
| 64 | `i32` | Envelope 1 sustain offset |
| 68 | `i32` | Envelope 1 release offset |
| 73 | `u8` | Release-trigger enable, low bit |
| 74 | `u8` | Output |
| 75 | `u8` | Note-selection value |
| 80 | `i32` | Group-cycle predecessor/reference |
| 84 | `u8` | Enable-by selector kind |
| 85 | `u8` | Controller selector number |
| 86 | `u8` | Low controller value |
| 87 | `u8` | High controller value |
| 88 | `u8` | Low key |
| 89 | `u8` | High key |
| 90 | `u8` | Zero-based MIDI channel selector |
| 91 | `u8` | Articulation selector |

Options in the examined public implementation include mute `0x10`, release-trigger decay `0x40`, and fixed sample selection `0x80`; additional options affect pitch-wheel, envelope curve, and round-robin behavior. Time and DSP-offset fields require an engine-unit mapping.

The enable-by kind selects note 1, group 2, controller 3, pitch 4, channel 5, articulation 6, or tempo 7 in this profile. Convert stored channel numbering to a 1-based presentation only when needed.

Group extensions:

| Content offset | Type | Field | Minimum content length |
|---:|---|---|---:|
| 94 | `i8` | Low pitch selector | 96 |
| 95 | `i8` | High pitch selector | 96 |
| 96 | `i32` | Envelope 1 hold offset | 120 |
| 100 | `i32` | Envelope 2 attack offset | 120 |
| 104 | `i32` | Envelope 2 decay offset | 120 |
| 108 | `i32` | Envelope 2 sustain offset | 120 |
| 112 | `i32` | Envelope 2 release offset | 120 |
| 116 | `i32` | Envelope 2 hold offset | 120 |
| 124 | `i32` | Envelope 1 delay offset | 132 |
| 128 | `i32` | Envelope 2 delay offset | 132 |

These are stored engine offsets, not self-describing seconds or dB. Preserve unknown gaps and appended tails.

Groups can constrain zones, route outputs, define release triggers, and implement cycling/round-robin. Losing group data while retaining zones changes the instrument.

### 13.6 Parsing and preservation

Scan blocks first, preserving type, index, name, flags, and content. Parse only fields available in the selected layout. Resolve zone sample/group references in a second pass. Resolve assets under an explicit search policy and compare decoded metadata.

Parameter blocks and extended group/zone tails are not fully specified here. Preserve them as opaque versioned data when editing unrelated fields, but do not claim a new instrument writer can reproduce the original synthesis without their semantics.

For broad-language portability, exporting resolved EXS zones into an explicit intermediate model or conservative SFZ is more defensible than guessing undocumented parameter encodings.

<a id="decent"></a>
## 14. Decent Sampler presets

**Source:** [S19], vendor developer documentation. This profile concerns unbundled `.dspreset` XML and basic sample mapping.

### 14.1 Document structure

```text
DecentSampler
    groups
        group
            sample
            ...
        ...
    optional ui, effects, midi, modulators, tags, buses, ...
```

There must be one top-level `<DecentSampler>` element and one `<groups>` element for the sample mapping. Group order matters because controls/bindings may refer to groups by zero-based index.

Use a real XML parser with external entity/network loading disabled. Attribute names are case-sensitive. Preserve unknown elements and attributes with namespace and scope where possible.

`minVersion` declares the intended minimum engine version. It is not a binary file version or evidence that every feature is supported by an alternate renderer.

### 14.2 Sample mapping

| Attribute | Meaning / units |
|---|---|
| `path` | Required relative asset path; vendor documents WAV, AIFF, FLAC |
| `rootNote` | Required root MIDI key |
| `loNote`, `hiNote` | Inclusive key bounds, common defaults 0 and 127 |
| `loVel`, `hiVel` | Inclusive velocity bounds, common defaults 0 and 127 |
| `start`, `end` | Playback frame positions; documented default end is frame count minus 1 |
| `tuning` | Semitones, including fractional semitones |
| `pitchKeyTrack` | 0 unpitched tracking, 1 normal semitone/key tracking |
| `volume` | Linear multiplier, or dB text with `dB` suffix |
| `pan` | -100 left to +100 right |
| `trigger` | Attack, release, first, legato, or documented continuous behavior |
| `loopStart`, `loopEnd` | Loop frame positions |
| `loopEnabled` | Boolean loop enable |
| `loopCrossfade` | Crossfade length in frames |
| `loopCrossfadeMode` | `linear` or `equal_power` |
| `loCCN`, `hiCCN` | Controller-condition pair for controller N |
| `onLoCCN`, `onHiCCN` | Controller-event trigger range, not ordinary note filtering |

The published sample `end` default describes the last included frame. Preserve the raw `loopEnd` and qualify its inclusive interpretation with the intended engine version; the public attribute description does not independently define every explicit endpoint edge case.

The vendor's root-note prose and default key-range values are not perfectly consistent at MIDI 0. Readers should preserve a supplied value and report out-of-profile cases; writers seeking maximum portability should use an explicitly tested range.

### 14.3 Inheritance and gain

Many attributes inherit from `<groups>` to `<group>` to `<sample>`, with the most specific setting winning. **Volume is an exception:** instrument, group, and sample volumes are separate multipliers.

```text
effective_gain = instrument_gain * group_gain * sample_gain
db_to_gain(db) = 10^(db / 20)
```

For example, two scope-level values of 0.5 produce 0.25, not 0.5.

Ordinary `pan` inherits by replacement. Newer documented `globalPan` and `groupPan` are additive offsets; clamp the final sum to the supported range. Their volume aliases `globalVolume`/`groupVolume` do not introduce new gain stages.

Group/global tuning, key tracking, and newer `tuningIgnoresKeyTrack` settings have version-sensitive interactions. Preserve the minimum-version requirement instead of flattening all tuning blindly.

### 14.4 Envelope and release

Basic amplitude settings include `attack`, `decay`, `sustain`, and `release`. The published example/profile uses seconds for time stages and a linear sustain level. Disabling `ampEnvEnabled` produces one-shot-like playback rather than ordinary note-off behavior.

Newer `loopDuringRelease=false` leaves a loop after note release so the recorded tail can play, while the release envelope still applies. The vendor associates this setting with a minimum engine version. Do not substitute this behavior for every loop-enabled preset.

UI bindings, effects, modulators, arpeggiation, slice stretching, and buses are separate schemas/DSP behaviors. Retain them or report them as unsupported. A `.dslibrary` package requires an independently implemented package profile; its extension does not establish that it is ordinary XML.

### 14.5 Minimal original example

```xml
<?xml version="1.0" encoding="UTF-8"?>
<DecentSampler minVersion="1.0.0">
  <groups attack="0.005" release="0.2" volume="0.8">
    <group>
      <sample path="Samples/Piano-C4.wav"
              rootNote="60" loNote="48" hiNote="72"
              loVel="1" hiVel="127" loopEnabled="false"/>
    </group>
  </groups>
</DecentSampler>
```

The path separators are serialized XML data, not a requirement on the parser's host filesystem API. Decode XML entities before resolving the relative path.

<a id="proprietary"></a>
## 15. REX, Kontakt, and other proprietary families

### 15.1 REX2 bounded identification profile

An observed REX2 container begins:

| File offset | Type | Observed field |
|---:|---|---|
| 0 | `bytes[4]` | `CAT ` |
| 4 | `u32be` | Container size |
| 8 | `bytes[4]` | `REX2` |
| 12 | `bytes[4]` | `HEAD` |
| 16 | `u32be` | Header payload size |
| 20 | `u32be` | Version/signature value; one observed profile uses `0x490cf18d` |

This is a **recognition observation**, not a complete normative specification. The four-byte signature is not established as invariant across every version, and these bytes do not define slice tables or audio decoding.

A REX-capable library generally needs decoded PCM, slice count/positions, tempo, and beat/bar duration. Full-loop rendering and slice extraction are distinct operations. Do not treat encoded sample payload as PCM or equate `CAT /REX2` recognition with a functional importer.

Use an authorized SDK/decoder where appropriate, or undertake a separately fixture-backed codec and slice-layout investigation. No licensing or redistribution permission is implied by successful parsing.

### 15.2 Kontakt

Kontakt is a family of versioned instrument, multi, sample, resource, and library containers, not a single byte layout:

| Common extension | General role | What this guide does not claim |
|---|---|---|
| `.nki` | Instrument | A universal zone/group/parameter layout |
| `.nkm` | Multi | Complete routing, instrument references, or state serialization |
| `.nkx`, `.nks`, related containers | Library/package data in some generations | Universal archive indexing or asset extraction |
| `.ncw` | Compressed waveform | Complete codec decoding |
| `.nkr` | Resources | Universal resource layout |
| `.nicnt` | Library registration/metadata | That it contains a complete sample map or audio |

Versioned binary serialization, compressed samples, resource references, scripting, and protected content are separate problems. A file-extension match or successful extraction of embedded names does not establish instrument playback.

For a portable reimplementation, require an identified version, a byte-level schema with verified reference resolution, decoded waveform fixtures, and a documented subset of group/script semantics. Protected-library access should follow authorized vendor interfaces; this guide does not specify protection bypass or key material.

If an authorized source can export mapping and ordinary audio, SFZ or another documented interchange profile is often a more practical migration boundary. Exported mappings may still lose scripts, effects, round-robin state, or release behavior; state those losses.

### 15.3 Family catalog and explicit specification gaps

The following catalog prevents gaps from being mistaken for covered formats. Related product names are grouped where the required work is similar. **"Unresolved" means no implementable binary layout is provided here**, not that no public information exists or that every version is inaccessible.

| Family | Status here | Required additional profile |
|---|---|---|
| Akai S900/S950 | Unresolved | Disk/sample/program layouts, packed sample encoding, compressed variants |
| Akai S1000/S3000 | Unresolved | Filesystem, directory, sample and keygroup records |
| Akai S5000/S6000 | Unresolved | Program records and external asset/reference rules |
| Akai MPC5000 | Unresolved | Generation-specific programs, samples, and projects |
| Akai MPC Renaissance/modern MPC | Unresolved | XML/binary package versions, program hierarchy, pad/layer semantics |
| Alchemy | Unresolved | Versioned preset, source, and sample-map structures |
| BeatMaker | Unresolved | Project/preset package schema and asset references |
| Bitwig | Unresolved | Sampler preset serialization and device-state versions |
| Blofeld | Unresolved | Sample/map/container layout and device constraints |
| Decent Sampler | Basic XML profile above | Package, UI, modulation, effects, and new-feature schemas |
| Deluge | Unresolved | Firmware-versioned song/kit/synth XML and sample behavior |
| DirectWave | Unresolved | Program records, embedded/external audio, modulation |
| DLS | Core profile above | Complete Level-2 articulation and conditional behavior |
| DR-008 | Unresolved | Drum-module/preset serialization and routing |
| Dream | Unresolved | Exact product/generation, bank layout, embedded sound encoding |
| Drumagog | Unresolved | GOG variants, layers, alternate hits, compressed assets |
| DrumCore | Unresolved | Library package/index and loop metadata |
| Dynacord | Unresolved | Device media, sample packing, program structures |
| E-mu Emax | Unresolved | Disk/bank layout, sample packing, preset and voice records |
| E-mu Emulator II/III/IV, ESI, EOS | Unresolved | Generation-specific media and bank structures |
| E-mu Emulator X | Unresolved | EXB bank records, sample references/compression, modulation |
| Ensoniq EPS/ASR | Unresolved | Media filesystem, instrument/layer/sample records |
| Ensoniq ASR-X | Unresolved | Distinct disk/program/sample profile |
| Ensoniq Mirage | Unresolved | Disk geometry, bank/parameter layout, sample encoding |
| EXS | Version-scoped profile above | Complete parameter/DSP blocks and consolidated variants |
| Fairlight | Unresolved | Exact generation, disk/sample/voice structure |
| Roland Fantom | Unresolved | Generation-specific multisamples, patches, and media |
| Alesis Fusion | Unresolved | Program/multisample serialization and sample metadata |
| Grace | Unresolved | Preset schema, inheritance, and asset resolution |
| GURU | Unresolved | Pattern/pad/library serialization and references |
| HALion/HALion XML | Unresolved | XML version schema and proprietary preset/container semantics |
| HISE | Unresolved | Sample-map XML variants, monoliths/codecs, scripts and processing |
| IDT | Unresolved identity/profile | Exact product and serialized structure; acronym is insufficient |
| Independence | Unresolved | Layer/program/library serialization and audio access |
| iRompler | Unresolved | Exact container and mapping schema |
| Isla | Unresolved | Exact product/generation and program/sample records |
| Kontakt | Versioned family, not specified | Identified instrument/container/codec versions |
| Korg DSS-1 | Unresolved | Disk/program/multisound/sample packing |
| Korg Triton/Kronos | KMP/KSF subset only | PCG/KSC, bank/program, and generation-specific extensions |
| Kurzweil | Unresolved | Object records, block references, ROM/RAM sample distinctions |
| LiveRack/LiveSampler | Unresolved | Exact product schemas and asset path rules |
| MachFive | Unresolved | Generation-specific preset and resource/container layouts |
| Nanostudio | Unresolved | Project/instrument package schemas |
| Nuance | Unresolved | Patch/map and sample reference schema |
| Omnisphere | Unresolved | Preset serialization, resource/sample access, synthesis layers |
| Peavey | Unresolved | Exact sampler generation, media and bank records |
| PLAY | Unresolved | Engine/library generation, instrument/container/codec layouts |
| Poise | Unresolved | Kit/pad schema, round-robin and velocity behavior |
| Prophet/Prophet X | Unresolved | Product-specific sample/map/container structures |
| Quantum | Unresolved identity/profile | Exact product and version; do not infer from a name alone |
| Reason/NN-XT/NN-19 | Unresolved | Patch serialization and ReFill/container/codec separation |
| Roland S-series | Unresolved | Disk filesystem, sample/partial/patch/performance records |
| Roland MV8000/MV8800 | Unresolved | Project, patch and audio reference structures |
| Roland XV | Unresolved | Device-specific bank/multisample/wave encoding |
| SampleCell | Unresolved | Instrument/map and sample reference records |
| SampleTank | Unresolved | Generation-specific instrument/header/archive/sample codec |
| SFZ | Portable text profile above | Engine-specific extended opcodes |
| SoundFont | Version 2 structural profile above | SF1, SF3 compression, complete synthesis behavior |
| Solaris | Unresolved | Sample-map/profile and device constraints |
| Spectralis | Unresolved | Product-specific sample and bank serialization |
| Structure | Unresolved | Patch/container versions, sample and modulation references |
| Synclavier | Unresolved | Media, sample-directory and voice/program layouts |
| TAL-Sampler | Unresolved | Preset schema and resampling/DAC-model behavior |
| TX16Wx | Unresolved | Program/performance/map schema and generation extensions |
| Yamaha A-series | Unresolved | Device-specific media, program and sample packing |
| Yamaha Motif | Unresolved | Generation-specific all/voice/waveform/sample containers |
| Yamaha Tyros | Unresolved | Voice expansion/container and waveform/reference structure |
| Unity | Unresolved | Bank/preset layout and embedded/external sound rules |
| UVI | Unresolved | Product/container generation, resources, instrument scripting |
| VSampler | Unresolved | Bank/program schema and sample references |
| WaveFrame | Unresolved | Media/sample/program structures |
| Westacott | Unresolved identity/profile | Exact spelling/product, fixtures and structural identification |
| Wusik | Unresolved | Soundset/preset generations and sample encoding |
| MOD/tracker modules | Unresolved in this edition | Distinct module signatures, pattern/effect/sample semantics |

Some names in historical inventories identify internal abstractions rather than public file formats. Do not publish an invented signature or extension for an unidentified family. In particular, template-resource chunk names are not evidence of input-file magic.

### 15.4 Minimum evidence for adding a proprietary profile

Record version/signature rules; root framing; record sizes and byte order; reference bases; sample encoding; count/length validation; key/velocity mapping; pitch and level units; loop endpoints; note-off semantics; writer requirements; and at least one ordinary and one boundary-case fixture.

A name-only entry should become a structural profile only after those facts are established. Keep observed fields separate from inferred semantics, and preserve unknown bytes until the semantics are resolved.

<a id="conversion"></a>
## 16. Cross-format conversion and playback

### 16.1 Pitch

For an ordinary pitched sample:

```text
pitch_delta_cents =
    (played_key - root_key) * cents_per_key
    + 100 * transpose_semitones
    + fine_tune_cents
    + asset_pitch_correction_cents

source_frames_per_output_frame =
    (source_rate / output_rate) * 2^(pitch_delta_cents / 1200)
```

The inclusion and sign of asset correction must match the source profile; do not count metadata tuning twice. A fractional root pitch can be represented either as a fractional key or as root plus a correction, but not both at once.

Use decoded source-frame coordinates for trims and loops. Resampling changes output-frame counts, not the meaning of original source-frame metadata.

### 16.2 Region selection

Process every eligible region, not just the first. Eligibility may depend on key, velocity, controller values, trigger, random value, sequence state, keyswitches, pedal state, articulation, and channel.

Maintain event state per required scope: instrument, group, note, or voice. Round-robin per note-on is not necessarily round-robin per matched region. A converter that erases these conditions turns alternation into layering.

Note-on with MIDI velocity zero conventionally acts as note-off. A velocity range including zero does not override that event interpretation.

### 16.3 Loop execution

For a forward loop `[L, E)`:

```text
if position >= E and looping_is_active:
    position = L + ((position - L) mod (E - L))
```

Preserve fractional position for resampling. Crossfades require reading valid pre-loop/tail ranges and applying the specified curve. Validate crossfade length against the loop and available audio.

For sustain looping, note-off changes loop-active state; the voice may continue into a recorded tail while its release envelope runs. For continuous looping, release may leave the loop active. For a one-shot, normal note-off may be ignored. These modes cannot be reduced to a single Boolean.

Ping-pong loops require direction state and an explicit endpoint policy to avoid repeating or skipping the endpoint sample. Preserve backward-loop and finite-play-count semantics where supported.

### 16.4 Stereo and channel routing

WAVE/AIFF/ordinary CAF PCM use interleaved frames; SF2 commonly uses linked mono streams; Giga compression changes channel storage by bit-depth profile. Pairing channels requires rate/length/loop consistency, not just similar filenames.

Preserve channel masks and discrete channel assignments. Downmixing is a rendering decision that should be explicit and may need a documented matrix; it is not a parser default.

### 16.5 Gain, pan, envelopes, and filters

Normalize stored gain to either linear amplitude or dB with a recorded unit. Positive attenuation and positive gain have opposite effects. SF2 centibels, DLS fixed-point attenuation, SFZ dB, and Decent linear/dB text are not interchangeable raw numbers.

Pan law, envelope curves, velocity response, filter topology, resonance scaling, effects, and limiter behavior are renderer-specific fidelity requirements. A structural conversion can preserve parameters without reproducing sound exactly; say which behavior is approximated.

### 16.6 Loss report

Every conversion should produce a machine-readable or human-readable loss report:

```text
source_profile
destination_profile
preserved_features[]
approximated_features[{feature, method}]
dropped_features[{feature, reason}]
unresolved_assets[]
unsupported_codecs[]
invalid_or_repaired_fields[]
```

Do not report success when required assets are missing, an audio codec was skipped, or a protected container was merely recognized. Metadata-only extraction is a distinct outcome.

### 16.7 Asset packaging

Use stable relative paths and a manifest associating source assets with destination assets. Preserve original filenames where feasible; resolve collisions deterministically. Hashing can identify identical assets, but deduplicating files must not erase distinct region-level metadata.

A writer must ensure references point to assets included in the package and remain valid after relocation. Avoid embedding absolute development-machine paths in a portable library.

<a id="conformance"></a>
## 17. Reader, writer, and conformance checklist

### 17.1 Error model

Distinguish:

| Result | Meaning |
|---|---|
| Recognized and decoded | Selected profile and codec were implemented; required content resolved |
| Recognized, metadata only | Container/schema known, audio or synthesis dependency unavailable |
| Unsupported variant | Family recognized, version/codec/layout not implemented |
| Invalid input | Required structural invariants fail |
| Missing asset | Reference could not be resolved under the declared policy |
| Compatibility repair | A named deviation was accepted with a diagnostic |

These are not interchangeable success values. Report the format, location, field, and expected invariant for malformed input.

### 17.2 General invariants

- Check every read range before access; guard addition and multiplication overflow.
- Bound nesting, record counts, packet lengths, text expansion, and decoded output.
- Do not trust advertised frame counts to allocate memory without a resource limit.
- Keep unknown metadata without executing scripts or loading external XML entities.
- Separate path resolution from parsing; prevent references from escaping an approved asset root unless explicitly allowed.
- Verify object indexes and byte offsets against indexed structures, not just file length.
- Do not silently substitute zero values for malformed headers or missing assets.
- Keep tolerant repairs visible and independently selectable.

### 17.3 Structural test vectors

These small values are useful for cross-language implementations:

| Test | Input | Expected result |
|---|---|---|
| RIFF odd chunk | Header 8 bytes, payload 3 bytes | Next chunk is 12 bytes after this chunk start |
| PCM stereo 16-bit | Channels 2, frames 3 | Six points, 12 audio bytes |
| Signed 24-bit LE minimum | `00 00 80` | -8388608 |
| Signed 24-bit LE maximum | `ff ff 7f` | 8388607 |
| Unsigned WAVE 8-bit silence | `80` | 0 |
| Signed AIFF 8-bit silence | `00` | 0 |
| AIFF rate | `40 0e ac 44 00 00 00 00 00 00` | 44100 Hz |
| Inclusive loop | Start 10, end 19 | Internal `[10,20)`, length 10 |
| DLS loop | Start 10, length 10 | Internal `[10,20)` |
| SF2 24-bit point | `smpl=-1`, `sm24=255` | -1 in signed 24-bit units |
| Giga mode-1 predictor | Y=100, DY=2, residuals 1,-1,0 | Output 99,97,95 |
| Giga mode-4 residual packing | `ff 0f 80` | Residuals -1 and -2048 |
| Ogg exact packet multiple | Laces 255,0 | One 255-byte packet, not a continuing packet |
| Decent gain scopes | Group 0.5, sample 0.5 | Effective multiplier 0.25 |

### 17.4 Real-fixture obligations

| Profile | Minimum useful fixtures |
|---|---|
| WAVE | PCM 8/16/24/32, float, odd chunk, metadata before/after data, extensible layout |
| BWF/RF64/BW64 | Versioned `bext`, 64-bit sentinel resolution, additional size-table entries |
| AIFF/AIFC | Nonzero SSND offset, signed 8-bit, non-byte valid depth, `sowt`, marker loops |
| CAF | Both PCM endiannesses, integer/float, EOF-extending data, compressed packet trimming if supported |
| SF2 | Global/local zones, stereo links, modulator defaults, offset generators, 2.04 `sm24` |
| SFZ | Paths with spaces, inheritance, includes/macros if supported, release/choke/sequence behavior |
| DLS | Nontrivial pool offsets, region overrides, loops, articulation scopes |
| Giga | All supported modes, mixed-mode stereo, partial frames, seek/replay, dimension selection |
| Korg | Actual generation-specific KMP/KSF pairs, loops, alternate starts, count semantics |
| EXS | Both byte orders, multiple block sizes, group/sample references, missing/relocated assets |
| Decent | XML escaping, scope-level volume multiplication, triggers, versioned loop behavior |

Round-trip structural tests must compare indexes, sizes, preserved metadata, and decoded audio, not just whether a file reopens. Playback comparisons additionally need timing, pitch, loops, note-off, stereo, and articulation checks.

### 17.5 Negative fixtures

Include truncated headers/payloads; excessive counts; overflowing offsets; invalid sample rates; impossible channel/bit-width combinations; index cycles; pool links to non-wave chunks; loop ends beyond decoded audio; unsupported mode/version values; missing external assets; cyclic includes; and malicious XML entities.

The parser should fail at the violated boundary with a useful diagnostic, without hanging, reading beyond EOF, or claiming a successful empty instrument.

### 17.6 Writer contract

Choose and advertise a destination profile. Recompute lengths and reference indexes from finalized output, include correct pads and terminal records, serialize byte order explicitly, and verify the written file with an independent reader where available.

Do not preserve stale checksums, duration metadata, packet indexes, loudness measurements, or opaque audio-coordinate metadata after edits unless they are recomputed or known to remain valid.

<a id="references"></a>
## 18. Public references

This guide is an original synthesis of factual layouts and parser guidance. The linked standards remain authoritative for their full conformance requirements. Public implementation links identify the scope of implementation-derived profiles; they are not vendor standards and their code licenses are separate from the use of format facts.

| ID | Reference | Scope |
|---|---|---|
| S1 | [McGill MMSP: WAVE audio format](https://mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html) | RIFF/WAVE, PCM/float, extensible format and data conventions |
| S2 | [EBU Tech 3285: Broadcast Wave Format](https://tech.ebu.ch/docs/tech/tech3285.pdf) | BWF `bext`, versions, timestamp and loudness fields |
| S3 | [EBU Tech 3306](https://tech.ebu.ch/docs/tech/tech3306.pdf) | Historical RF64 guidance and current replacement notice |
| S4 | [ITU-R BS.2088-2](https://www.itu.int/rec/R-REC-BS.2088-2-202511-I/en) | Current BW64 framing; `ds64` reserved fields and ADM-related chunks |
| S5 | [McGill MMSP: AIFF/AIFF-C](https://mmsp.ece.mcgill.ca/Documents/AudioFormats/AIFF/AIFF.html) | AIFF/AIFC encoding, extended rate, compression codes |
| S6 | [libsndfile 1.2.2 AIFF implementation](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/aiff.c) | Marker/loop compatibility behavior; not a replacement for Apple's full specification |
| S7 | [Apple Core Audio Format specification](https://developer.apple.com/library/archive/documentation/MusicAudio/Reference/CAFSpec/CAF_spec/CAF_spec.html) | CAF file/chunk headers, `desc`, `data`, packet tables and codec dependencies |
| S8 | [RFC 9639: FLAC](https://www.rfc-editor.org/rfc/rfc9639) | Native framing, metadata, complete lossless codec specification |
| S9 | [RFC 3533: Ogg encapsulation](https://www.rfc-editor.org/rfc/rfc3533) | Page framing, lacing, stream serials and checksums |
| S10 | [Vorbis I specification](https://xiph.org/vorbis/doc/Vorbis_I_spec.html) | Header packets, bit packing, codec and Ogg mapping |
| S11 | [RFC 7845: Ogg Opus](https://www.rfc-editor.org/rfc/rfc7845) | Identification, granule clock, pre-skip and trimming |
| S12 | [SoundFont 2.04 specification](https://www.synthfont.com/sfspec24.pdf) | Hydra records, sample pools, generators, modulators and 24-bit extension; some retained page footers say 2.01 |
| S13 | [SFZ format documentation](https://sfzformat.com/) | Header scopes and opcode definitions; consult individual dialect/compatibility notes |
| S14 | [libgig DLS.cpp, revision 4304](https://svn.linuxsampler.org/cgi-bin/viewvc.cgi/libgig/trunk/src/DLS.cpp?revision=4304&view=markup) | DLS chunk reading, wave-pool resolution, region/sample/articulation structures |
| S15 | [mingw-w64 DLS definitions](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/include/dls1.h) and [DLS2 definitions](https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/include/dls2.h) | Public struct/enumeration declarations; not complete playback semantics |
| S16 | [libgig gig.cpp, revision 4304](https://svn.linuxsampler.org/cgi-bin/viewvc.cgi/libgig/trunk/src/gig.cpp?revision=4304&view=markup) | Giga dimensions, compressed frame modes, predictor reconstruction and attenuation units |
| S17 | [libgig Korg.cpp, revision 4304](https://svn.linuxsampler.org/cgi-bin/viewvc.cgi/libgig/trunk/src/Korg.cpp?revision=4304&view=markup) and [RIFF.cpp, same revision](https://svn.linuxsampler.org/cgi-bin/viewvc.cgi/libgig/trunk/src/RIFF.cpp?revision=4304&view=markup) | KMP/KSF fields, sample-reference behavior, and flat-chunk padding |
| S18 | [exstools4 at commit d886a5994b7a46de1fc49aec4eb37e8d83b72d55](https://github.com/jonkubis/exstools4/tree/d886a5994b7a46de1fc49aec4eb37e8d83b72d55), particularly `exsblock.py`, `exsclasses.py`, and `exsfile.py` | Pinned EXS block/object profile; not an Apple normative standard |
| S19 | [Decent Sampler developer guide](https://decentsampler-developers-guide.readthedocs.io/en/latest/), [format overview](https://decentsampler-developers-guide.readthedocs.io/en/latest/introduction.html), and [groups/sample attributes](https://decentsampler-developers-guide.readthedocs.io/en/latest/the-groups-element.html) | Vendor XML and playback attribute definitions; new features carry version requirements |
| S20 | [libsndfile 1.2.2 RF64 implementation](https://github.com/libsndfile/libsndfile/blob/1.2.2/src/rf64.c) | RF64 `ds64` frame-count interpretation and header writing; implementation corroboration, not BW64 semantics |

For implementation-derived profiles, keep a local source/version snapshot with the parser implementation. For future revisions, expand the profile only when field semantics and test fixtures justify the additional compatibility claim.
