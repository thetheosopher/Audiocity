#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace audiocity::engine::sfz
{
enum class DiagnosticSeverity
{
    warning,
    error
};

struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::warning;
    juce::String filePath;
    int line = 0;
    juce::String message;
};

struct Zone
{
    juce::String sourceSample;
    juce::String resolvedSamplePath;
    int lowKey = 0;
    int highKey = 127;
    int key = -1;
    int pitchKeycenter = 60;
    int lowVelocity = 0;
    int highVelocity = 127;
    int transpose = 0;
    int tuneCents = 0;
    int offset = 0;
    int loopStart = 0;
    int loopEnd = 0;
    juce::String loopMode = "no_loop";
    int rrGroup = 0;
};

struct Group
{
    int rrGroup = 0;
    std::vector<int> zoneIndices;
};

struct Program
{
    std::vector<Group> groups;
    std::vector<Zone> zones;
    std::vector<Diagnostic> diagnostics;
};
}
