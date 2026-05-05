#include <cstring>

#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PluginProcessor.h"

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--with-gui") == 0)
    {
        juce::ScopedJuceInitialiser_GUI gui;
    }

    return 0;
}