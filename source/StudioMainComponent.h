#pragma once

// The Studio's main view (milestone 1): open a plugin repo, host the REAL plugin
// (editor + audio + MIDI in), and show the manifest lint in a problems panel.
// Editing layers (inspector, GUI designer, bindings, sample import, build/export)
// land in later milestones on top of this shell.

#include "PluginProject.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace dmse_studio
{

class StudioMainComponent : public juce::Component
{
public:
    StudioMainComponent();
    ~StudioMainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void chooseAndOpen();
    void openRepo (const juce::File& repoDir);
    void closeProject();
    void layoutEditor();

    // Top strip
    juce::TextButton openButton { "Open plugin repo..." };
    juce::TextButton reloadButton { "Reload" };
    juce::TextButton audioButton { "Audio settings..." };
    juce::Label pathLabel;

    // Centre: the hosted plugin editor (the real thing)
    struct EditorHolder : juce::Component, juce::ComponentListener
    {
        std::function<void()> onChildResized;
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (wasResized && onChildResized) onChildResized();
        }
    };
    EditorHolder editorHolder;

    // Bottom: manifest lint / problems panel
    juce::TextEditor problems;

    // Hosting
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<PluginProject> project;
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditor;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::File lastDir;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioMainComponent)
};

} // namespace dmse_studio
