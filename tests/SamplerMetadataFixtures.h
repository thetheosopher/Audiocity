#pragma once

#include <juce_core/juce_core.h>

#include <cstring>

namespace audiocity::test_fixtures
{
inline void appendBigEndianU16(juce::MemoryOutputStream& stream, const juce::uint16 value)
{
    const juce::uint8 bytes[2] = {
        static_cast<juce::uint8>((value >> 8) & 0xff),
        static_cast<juce::uint8>(value & 0xff)
    };
    stream.write(bytes, 2);
}

inline void appendBigEndianU32(juce::MemoryOutputStream& stream, const juce::uint32 value)
{
    const juce::uint8 bytes[4] = {
        static_cast<juce::uint8>((value >> 24) & 0xff),
        static_cast<juce::uint8>((value >> 16) & 0xff),
        static_cast<juce::uint8>((value >> 8) & 0xff),
        static_cast<juce::uint8>(value & 0xff)
    };
    stream.write(bytes, 4);
}

inline void appendFourcc(juce::MemoryOutputStream& stream, const char* tag)
{
    stream.write(tag, 4);
}

inline void appendIffChunk(juce::MemoryOutputStream& formBody,
                           const char* tag,
                           const juce::MemoryOutputStream& payload)
{
    appendFourcc(formBody, tag);
    appendBigEndianU32(formBody, static_cast<juce::uint32>(payload.getDataSize()));
    formBody.write(payload.getData(), payload.getDataSize());
    if ((payload.getDataSize() & 1u) != 0)
        formBody.writeByte(0);
}

inline bool writeAiffSamplerFixture(const juce::File& file,
                                    const int detuneCents = -15,
                                    const juce::uint16 sustainLoopMode = 1,
                                    const juce::uint16 sustainEndMarkerId = 77,
                                    const bool truncateMarkRecords = false,
                                    const juce::uint16 markerEndId = 77)
{
    constexpr int sampleCount = 32;
    constexpr int loopStart = 4;
    constexpr int loopEnd = 12;

    juce::MemoryOutputStream formBody;
    appendFourcc(formBody, "AIFF");

    juce::MemoryOutputStream common;
    appendBigEndianU16(common, 1);
    appendBigEndianU32(common, sampleCount);
    appendBigEndianU16(common, 16);
    constexpr juce::uint8 sampleRate44100[10] = {
        0x40, 0x0e, 0xac, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    common.write(sampleRate44100, sizeof(sampleRate44100));
    appendIffChunk(formBody, "COMM", common);

    juce::MemoryOutputStream markers;
    appendBigEndianU16(markers, 2);
    auto appendMarker = [&markers](const juce::uint16 id,
                                   const juce::uint32 frame,
                                   const char* name)
    {
        const auto nameLength = std::strlen(name);
        appendBigEndianU16(markers, id);
        appendBigEndianU32(markers, frame);
        markers.writeByte(static_cast<char>(nameLength));
        markers.write(name, nameLength);
        if (((7u + nameLength) & 1u) != 0)
            markers.writeByte(0);
    };
    appendMarker(19, loopStart, "loop");
    if (!truncateMarkRecords)
        appendMarker(markerEndId, loopEnd, "end");
    appendIffChunk(formBody, "MARK", markers);

    juce::MemoryOutputStream instrument;
    instrument.writeByte(48);
    const auto detuneByte = static_cast<juce::uint8>(
        detuneCents < 0 ? detuneCents + 256 : detuneCents);
    instrument.write(&detuneByte, 1);
    instrument.writeByte(0);
    instrument.writeByte(127);
    instrument.writeByte(0);
    instrument.writeByte(127);
    appendBigEndianU16(instrument, 0);
    appendBigEndianU16(instrument, sustainLoopMode);
    appendBigEndianU16(instrument, 19);
    appendBigEndianU16(instrument, sustainEndMarkerId);
    appendBigEndianU16(instrument, 0);
    appendBigEndianU16(instrument, 0);
    appendBigEndianU16(instrument, 0);
    appendIffChunk(formBody, "INST", instrument);

    juce::MemoryOutputStream sound;
    appendBigEndianU32(sound, 0);
    appendBigEndianU32(sound, 0);
    for (int frame = 0; frame < sampleCount; ++frame)
        appendBigEndianU16(sound, frame >= loopStart && frame < loopEnd ? 8192 : 0);
    appendIffChunk(formBody, "SSND", sound);

    juce::MemoryOutputStream form;
    appendFourcc(form, "FORM");
    appendBigEndianU32(form, static_cast<juce::uint32>(formBody.getDataSize()));
    form.write(formBody.getData(), formBody.getDataSize());
    return file.replaceWithData(form.getData(), form.getDataSize());
}
} // namespace audiocity::test_fixtures
