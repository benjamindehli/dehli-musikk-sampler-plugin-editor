#include "StudioMainComponent.h"

namespace dmse_studio
{

namespace
{
constexpr int kTopStrip = 34;
constexpr int kProblemsHeight = 110;
}

StudioMainComponent::StudioMainComponent()
{
    openButton.onClick = [this] { chooseAndOpen(); };
    addAndMakeVisible (openButton);

    reloadButton.onClick = [this]
    {
        if (project != nullptr)
            openRepo (project->getRepoRoot());
    };
    reloadButton.setEnabled (false);
    addAndMakeVisible (reloadButton);

    audioButton.onClick = [this]
    {
        // Standard JUCE device selector in a dialog window.
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

    editorHolder.onChildResized = [this] { layoutEditor(); };
    addAndMakeVisible (editorHolder);

    problems.setMultiLine (true);
    problems.setReadOnly (true);
    problems.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, 0)));
    problems.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff141516));
    problems.setColour (juce::TextEditor::textColourId, juce::Colour (0xffd8d9da));
    addAndMakeVisible (problems);

    // Audio out + every MIDI input straight into the hosted processor.
    deviceManager.initialiseWithDefaultDevices (0, 2);
    deviceManager.addAudioCallback (&player);
    for (const auto& mi : juce::MidiInput::getAvailableDevices())
    {
        deviceManager.setMidiInputDeviceEnabled (mi.identifier, true);
        deviceManager.addMidiInputDeviceCallback (mi.identifier, &player);
    }

    setSize (1000, 700);
}

StudioMainComponent::~StudioMainComponent()
{
    closeProject();
    deviceManager.removeAudioCallback (&player);
}

void StudioMainComponent::closeProject()
{
    player.setProcessor (nullptr);
    if (pluginEditor != nullptr)
        pluginEditor->removeComponentListener (&editorHolder);
    pluginEditor.reset();   // editor must die before its processor
    project.reset();
    reloadButton.setEnabled (false);
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
        return;
    }

    auto& proc = project->getProcessor();
    player.setProcessor (&proc);

    pluginEditor.reset (proc.createEditor());
    if (pluginEditor != nullptr)
    {
        editorHolder.addAndMakeVisible (*pluginEditor);
        pluginEditor->addComponentListener (&editorHolder);
    }

    pathLabel.setText (project->getRepoRoot().getFullPathName(), juce::dontSendNotification);
    reloadButton.setEnabled (true);

    // Problems panel: loader warnings first (typos, dangling references), then a
    // short OK line so an empty panel is clearly "no findings", not "not run".
    const auto& lint = project->getLint();
    juce::StringArray lines;
    for (const auto& e : lint.errors)   lines.add ("ERROR: " + e);
    for (const auto& w : lint.warnings) lines.add ("warning: " + w);
    if (lines.isEmpty())
        lines.add ("Manifest OK — no lint findings. Modes: "
                   + juce::String (lint.library.modes.size()));
    problems.setText (lines.joinIntoString ("\n"));

    layoutEditor();
}

void StudioMainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff191a1b));
}

void StudioMainComponent::layoutEditor()
{
    if (pluginEditor == nullptr)
        return;
    // The plugin editor sizes ITSELF (mode switches can change its height) — keep
    // it centred in the holder at its own size.
    pluginEditor->setTopLeftPosition (
        juce::jmax (0, (editorHolder.getWidth()  - pluginEditor->getWidth())  / 2),
        juce::jmax (0, (editorHolder.getHeight() - pluginEditor->getHeight()) / 2));
}

void StudioMainComponent::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (kTopStrip).reduced (6, 5);
    openButton.setBounds (top.removeFromLeft (150));
    top.removeFromLeft (6);
    reloadButton.setBounds (top.removeFromLeft (70));
    top.removeFromLeft (6);
    audioButton.setBounds (top.removeFromLeft (120));
    top.removeFromLeft (10);
    pathLabel.setBounds (top);

    problems.setBounds (r.removeFromBottom (kProblemsHeight));
    editorHolder.setBounds (r);
    layoutEditor();
}

} // namespace dmse_studio
