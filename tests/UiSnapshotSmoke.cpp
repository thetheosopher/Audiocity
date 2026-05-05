#include <cstring>

#include <juce_gui_basics/juce_gui_basics.h>

int main(int argc, char* argv[])
{
    if (argc > 1 && std::strcmp(argv[1], "--with-gui") == 0)
    {
        juce::ScopedJuceInitialiser_GUI gui;
    }

    return 0;
}