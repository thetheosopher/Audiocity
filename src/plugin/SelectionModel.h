#pragma once

#include <functional>

class SelectionModel
{
public:
    using SelectionChanged = std::function<void(int)>;

    void setSelectedZoneIndex(const int zoneIndex)
    {
        if (selectedZoneIndex_ == zoneIndex)
            return;

        selectedZoneIndex_ = zoneIndex;
        if (onSelectionChanged)
            onSelectionChanged(selectedZoneIndex_);
    }

    void clear()
    {
        setSelectedZoneIndex(-1);
    }

    [[nodiscard]] int getSelectedZoneIndex() const noexcept
    {
        return selectedZoneIndex_;
    }

    SelectionChanged onSelectionChanged;

private:
    int selectedZoneIndex_ = -1;
};