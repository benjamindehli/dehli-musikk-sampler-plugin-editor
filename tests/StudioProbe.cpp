// Headless probe: opens a plugin repo through the Studio's PluginProject and
// reports whether the manifest background image resolves and decodes.
#include "../source/PluginProject.h"
#include <ui/ManifestUiComponent.h>
#include <juce_events/juce_events.h>
#include <iostream>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (argc < 2) { std::cout << "usage: probe <plugin-repo>\n"; return 2; }

    juce::String error;
    auto project = dmse_studio::PluginProject::open (juce::File (argv[1]), error);
    if (project == nullptr) { std::cout << "OPEN FAILED: " << error << "\n"; return 1; }

    auto& lib = project->getModel();
    std::cout << "modes: " << lib.modes.size() << "\n";
    for (const auto& m : lib.modes)
    {
        std::cout << "mode '" << m.name << "' background id: '" << m.ui.background << "'\n";
        if (m.ui.background.isNotEmpty())
        {
            auto img = project->getProcessor().loadImage (m.ui.background);
            std::cout << "  loadImage → " << (img.isValid()
                        ? juce::String (img.getWidth()) + "x" + juce::String (img.getHeight())
                        : juce::String ("INVALID")) << "\n";
        }
    }
    // Also probe the first attempt's id explicitly.
    auto img1 = project->getProcessor().loadImage ("img:background");
    std::cout << "img:background → " << (img1.isValid() ? "valid" : "INVALID") << "\n";

    // Render the actual FACE component off-screen and sample a pixel: grey
    // 0xff222222 = the "no background" fallback.
    if (! lib.modes.isEmpty())
    {
        auto& mode = lib.modes.getReference (0);
        auto& proc = project->getProcessor();
        dm::ManifestUiComponent face (mode.ui,
            [&proc] (const juce::String& id) { return proc.loadImage (id); });
        face.setSize (juce::jmax (1, mode.ui.width), juce::jmax (1, mode.ui.height - mode.ui.cropTop));
        auto snap = face.createComponentSnapshot (face.getLocalBounds());
        const auto px = snap.getPixelAt (snap.getWidth() / 2, snap.getHeight() / 2);
        std::cout << "face centre pixel: " << px.toDisplayString (true)
                  << (px == juce::Colour (0xff222222) ? "  (GREY FALLBACK - background NOT rendered)"
                                                      : "  (background rendered)") << "\n";
    }
    // ── Full live-flow replication: set the background via the Studio's exact
    // edit calls, then render the face from the PROCESSOR's active mode (what the
    // real editor uses), not from the model.
    if (argc >= 3 && juce::String (argv[2]) == "editflow")
    {
        auto& p = *project;
        p.beginEdit();
        p.getModel().modes.getReference (0).ui.background = "img:background";
        p.commitEdit();

        const auto* am = p.getProcessor().getActiveMode();
        std::cout << "processor active mode: " << (am != nullptr ? "ok" : "NULL") << "\n";

        // Round-trip the model the way createProcessor does and show any errors.
        const auto json = dm::writeManifestToJson (p.getModel(), true);
        auto parsed = dm::loadManifestFromJson (json);
        std::cout << "serialized round-trip ok: " << (parsed.ok ? "yes" : "NO") << "\n";
        for (const auto& e : parsed.errors)
            std::cout << "  parse error: " << e << "\n";
        std::cout << "serialized json head: " << json.substring (0, 200) << "\n";
        if (am != nullptr)
        {
            std::cout << "processor-side background id: '" << am->ui.background << "'\n";
            auto& proc = p.getProcessor();
            dm::ManifestUiComponent face (am->ui,
                [&proc] (const juce::String& id) { return proc.loadImage (id); });
            face.setSize (juce::jmax (1, am->ui.width), juce::jmax (1, am->ui.height - am->ui.cropTop));
            auto snap = face.createComponentSnapshot (face.getLocalBounds());
            const auto px = snap.getPixelAt (snap.getWidth() / 2, snap.getHeight() / 2);
            std::cout << "editflow face centre pixel: " << px.toDisplayString (true)
                      << (px == juce::Colour (0xff222222) ? "  (GREY - BUG REPRODUCED)"
                                                          : "  (background rendered)") << "\n";
        }
    }
    return 0;
}
