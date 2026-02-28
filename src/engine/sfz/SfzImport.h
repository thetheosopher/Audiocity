#pragma once

#include "SfzModel.h"

namespace audiocity::engine::sfz
{
class Importer
{
public:
    [[nodiscard]] Program importFromFile(const juce::File& sfzFile) const;
};
}
