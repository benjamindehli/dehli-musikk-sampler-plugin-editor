// DMSE Studio — desktop authoring app for dehli-musikk-sampler-engine plugins.
// Milestone 1: open a plugin repo from disk, host the real plugin (UI + audio +
// MIDI), show manifest lint. See CLAUDE.md for the milestone roadmap.

#include "StudioMainComponent.h"

namespace dmse_studio
{

class StudioApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "DMSE Studio"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff191a1b), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new StudioMainComponent(), true);
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            // Unsaved-changes guard before quitting.
            if (auto* c = dynamic_cast<StudioMainComponent*> (getContentComponent());
                c != nullptr && c->isDirty())
            {
                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::WarningIcon, "Unsaved changes",
                    "The project has unsaved changes. Quit and discard them?",
                    "Quit", "Cancel", this,
                    juce::ModalCallbackFunction::create ([] (int result)
                    {
                        if (result == 1)
                            juce::JUCEApplication::getInstance()->quit();
                    }));
                return;
            }
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    std::unique_ptr<MainWindow> window;
};

} // namespace dmse_studio

START_JUCE_APPLICATION (dmse_studio::StudioApplication)
