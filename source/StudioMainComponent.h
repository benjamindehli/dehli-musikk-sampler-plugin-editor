#pragma once

// The Studio's main view. Milestone 2: model tree (left) + property inspector
// (right) around the hosted plugin preview (centre), with Save / Undo / Redo,
// dirty tracking, and hot reload — every committed edit rebuilds the hosted
// processor from the in-memory model with its state carried over.

#include "PluginProject.h"
#include "ModelTree.h"
#include "Inspector.h"
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
    bool keyPressed (const juce::KeyPress&) override;

    /** Unsaved-changes guard for the window close button. */
    bool isDirty() const { return project != nullptr && project->isDirty(); }

private:
    void chooseAndOpen();
    void openRepo (const juce::File& repoDir);
    void closeProject();
    void layoutEditor();
    void attachProcessor();          // player + editor onto the current processor
    void detachProcessor();
    void applyCommittedEdit();       // hot reload + refresh panels
    void importSamples (NodeRef target, const juce::StringArray& files);
    dm::Group* resolveGroup (NodeRef ref);
    void refreshProblems();
    void updateToolbar();
    void confirmDiscardThen (std::function<void()> proceed);

    // Top strip
    juce::TextButton openButton { "Open..." };
    juce::TextButton saveButton { "Save" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton reloadButton { "Reload" };
    juce::TextButton audioButton { "Audio..." };
    juce::Label pathLabel;

    // Panels
    ModelTree modelTree;
    Inspector inspector;

    struct EditorHolder : juce::Component, juce::ComponentListener
    {
        std::function<void()> onChildResized;
        void componentMovedOrResized (juce::Component&, bool, bool wasResized) override
        {
            if (wasResized && onChildResized) onChildResized();
        }
    };
    EditorHolder editorHolder;

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
