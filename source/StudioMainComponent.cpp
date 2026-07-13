#include "StudioMainComponent.h"

namespace dmse_studio
{

namespace
{
constexpr int kTopStrip = 34;
constexpr int kProblemsHeight = 96;
constexpr int kTreeWidth = 260;
constexpr int kInspectorWidth = 300;
}

StudioMainComponent::StudioMainComponent()
{
    openButton.onClick = [this] { confirmDiscardThen ([this] { chooseAndOpen(); }); };
    addAndMakeVisible (openButton);

    saveButton.onClick = [this]
    {
        if (project == nullptr) return;
        juce::String error;
        if (! project->save (error))
            problems.setText ("SAVE FAILED: " + error);
        updateToolbar();
    };
    addAndMakeVisible (saveButton);

    undoButton.onClick = [this]
    {
        if (project == nullptr || ! project->canUndo()) return;
        detachProcessor();
        project->undo();
        attachProcessor();
        modelTree.rebuild (project->getModel());
        inspector.refresh();
        refreshProblems();
        updateToolbar();
    };
    addAndMakeVisible (undoButton);

    redoButton.onClick = [this]
    {
        if (project == nullptr || ! project->canRedo()) return;
        detachProcessor();
        project->redo();
        attachProcessor();
        modelTree.rebuild (project->getModel());
        inspector.refresh();
        refreshProblems();
        updateToolbar();
    };
    addAndMakeVisible (redoButton);

    reloadButton.onClick = [this]
    {
        if (project != nullptr)
            confirmDiscardThen ([this, dir = project->getRepoRoot()] { openRepo (dir); });
    };
    addAndMakeVisible (reloadButton);

    audioButton.onClick = [this]
    {
        auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
            deviceManager, 0, 0, 2, 2, true, false, true, false);
        selector->setSize (480, 380);
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (selector.release());
        o.dialogTitle = "Audio settings";
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = juce::Colour (0xff202122);
        o.launchAsync();
    };
    addAndMakeVisible (audioButton);

    pathLabel.setText ("No plugin open", juce::dontSendNotification);
    pathLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b1b2));
    addAndMakeVisible (pathLabel);

    modelTree.onSelect = [this] (NodeRef ref) { inspector.show (ref); };
    addAndMakeVisible (modelTree);

    inspector.getModel   = [this] () -> dm::PresetLibrary& { return project->getModel(); };
    inspector.beginEdit  = [this] { if (project != nullptr) project->beginEdit(); };
    inspector.commitEdit = [this] { applyCommittedEdit(); };
    addAndMakeVisible (inspector);

    editorHolder.onChildResized = [this] { layoutEditor(); };
    addAndMakeVisible (editorHolder);

    problems.setMultiLine (true);
    problems.setReadOnly (true);
    problems.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, 0)));
    problems.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141516));
    problems.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8d9da));
    addAndMakeVisible (problems);

    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&player);
    for (const auto& mi : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (mi.identifier, true);
        deviceManager.addMidiInputDeviceCallback (mi.identifier, &player);
    }

    setWantsKeyboardFocus (true);
    updateToolbar();
    setSize (1400, 800);
}

StudioMainComponent::~StudioMainComponent()
{
    closeProject();
    deviceManager.removeAudioCallback (&player);
}

bool StudioMainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress ('z', cmd, 0))                                  { undoButton.triggerClick(); return true; }
    if (key == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)) { redoButton.triggerClick(); return true; }
    if (key == juce::KeyPress ('s', cmd, 0))                                  { saveButton.triggerClick(); return true; }
    return false;
}

void StudioMainComponent::confirmDiscardThen (std::function<void()> proceed)
{
    if (! isDirty())
    {
        proceed();
        return;
    }
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon, "Unsaved changes",
        "The project has unsaved changes. Discard them?", "Discard", "Cancel", this,
        juce::ModalCallbackFunction::create ([proceed] (int result)
        {
            if (result == 1)
                proceed();
        }));
}

void StudioMainComponent::detachProcessor()
{
    player.setProcessor (nullptr);
    if (pluginEditor != nullptr)
        pluginEditor->removeComponentListener (&editorHolder);
    pluginEditor.reset();   // editor must die before its processor
}

void StudioMainComponent::attachProcessor()
{
    if (project == nullptr)
        return;
    auto& proc = project->getProcessor();
    player.setProcessor (&proc);
    pluginEditor.reset (proc.createEditor());
    if (pluginEditor != nullptr)
    {
        editorHolder.addAndMakeVisible (*pluginEditor);
        pluginEditor->addComponentListener (&editorHolder);
    }
    layoutEditor();
}

void StudioMainComponent::applyCommittedEdit()
{
    if (project == nullptr)
        return;
    detachProcessor();
    project->commitEdit();
    attachProcessor();
    modelTree.rebuild (project->getModel());   // names/labels may have changed
    inspector.refresh();
    refreshProblems();
    updateToolbar();
}

void StudioMainComponent::closeProject()
{
    detachProcessor();
    project.reset();
    updateToolbar();
}

void StudioMainComponent::chooseAndOpen()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Open a plugin repo (folder with assets/manifest/index.json)",
        lastDir.exists() ? lastDir : juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().isDirectory())
            openRepo (fc.getResult());
    });
}

void StudioMainComponent::openRepo (const juce::File& repoDir)
{
    closeProject();
    lastDir = repoDir.getParentDirectory();

    juce::String error;
    project = PluginProject::open (repoDir, error);
    if (project == nullptr)
    {
        pathLabel.setText ("No plugin open", juce::dontSendNotification);
        problems.setText ("ERROR: " + error);
        updateToolbar();
        return;
    }

    attachProcessor();
    modelTree.rebuild (project->getModel());
    inspector.show ({});
    pathLabel.setText (project->getRepoRoot().getFullPathName(), juce::dontSendNotification);
    refreshProblems();
    updateToolbar();
}

void StudioMainComponent::refreshProblems()
{
    if (project == nullptr)
        return;
    const auto& lint = project->getLint();
    juce::StringArray lines;
    for (const auto& e : lint.errors)   lines.add ("ERROR: " + e);
    for (const auto& w : lint.warnings) lines.add ("warning: " + w);
    if (lines.isEmpty())
        lines.add ("Manifest OK — no lint findings. Modes: "
                   + juce::String (lint.library.modes.size()));
    problems.setText (lines.joinIntoString ("\n"));
}

void StudioMainComponent::updateToolbar()
{
    const bool open = project != nullptr;
    saveButton.setEnabled (open && project->isDirty());
    undoButton.setEnabled (open && project->canUndo());
    redoButton.setEnabled (open && project->canRedo());
    reloadButton.setEnabled (open);
    if (open)
        pathLabel.setText (project->getRepoRoot().getFullPathName()
                               + (project->isDirty() ? "  •  (unsaved changes)" : ""),
                           juce::dontSendNotification);
}

void StudioMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff191a1b));
}

void StudioMainComponent::layoutEditor()
{
    if (pluginEditor == nullptr)
        return;
    pluginEditor->setTopLeftPosition (
        juce::jmax (0, (editorHolder.getWidth()  - pluginEditor->getWidth())  / 2),
        juce::jmax (0, (editorHolder.getHeight() - pluginEditor->getHeight()) / 2));
}

void StudioMainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kTopStrip).reduced (6, 5);
    for (auto* b : { &openButton, &saveButton, &undoButton, &redoButton, &reloadButton, &audioButton })
    {
        b->setBounds (top.removeFromLeft (b == &openButton ? 80 : 66));
        top.removeFromLeft (6);
    }
    top.removeFromLeft (8);
    pathLabel.setBounds (top);

    problems.setBounds (r.removeFromBottom (kProblemsHeight));
    modelTree.setBounds (r.removeFromLeft (kTreeWidth));
    inspector.setBounds (r.removeFromRight (kInspectorWidth));
    editorHolder.setBounds (r);
    layoutEditor();
}

} // namespace dmse_studio
