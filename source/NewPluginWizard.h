#pragma once

// "New plugin" scaffolding: creates a plugin repo inside the superproject root
// (CMakeLists with dmse_add_plugin, a minimal one-mode manifest written through
// the engine's own writer, the house .gitignore, packaging/ for a future
// icon.png) and registers it in the root CMakeLists so the superproject builds
// it. Returns the new repo dir, or an error.

#include <model/Manifest.h>
#include <model/ManifestWriter.h>
#include <juce_core/juce_core.h>

namespace dmse_studio
{

struct NewPluginSpec
{
    juce::String productName;   // "My Instrument"
    juce::String targetName;    // "MyInstrument" (CMake target)
    juce::String pluginCode;    // 4 chars, e.g. "Myin"
    juce::String version = "0.1.0";
};

inline juce::String defaultTargetName (const juce::String& product)
{
    juce::String t;
    for (auto c : product)
        if (juce::CharacterFunctions::isLetterOrDigit (c))
            t << c;
    return t.isNotEmpty() ? t : "NewPlugin";
}

inline juce::String defaultPluginCode (const juce::String& product)
{
    auto letters = product.retainCharacters ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    auto code = letters.substring (0, 4);
    while (code.length() < 4)
        code << "x";
    // House style: first char uppercase, rest lowercase.
    return code.substring (0, 1).toUpperCase() + code.substring (1).toLowerCase();
}

inline juce::File createNewPlugin (const juce::File& superRoot, const NewPluginSpec& spec,
                                   juce::String& error)
{
    if (! superRoot.getChildFile ("dehli-musikk-sampler-engine").isDirectory()
        || ! superRoot.getChildFile ("CMakeLists.txt").existsAsFile())
    {
        error = "Not the superproject root (needs dehli-musikk-sampler-engine/ and CMakeLists.txt): "
                + superRoot.getFullPathName();
        return {};
    }
    if (spec.pluginCode.length() != 4)
    {
        error = "Plugin code must be exactly 4 characters.";
        return {};
    }

    const auto dirName = spec.targetName.toLowerCase() + "-plugin";
    auto repo = superRoot.getChildFile (dirName);
    if (repo.exists())
    {
        error = "Already exists: " + repo.getFullPathName();
        return {};
    }
    repo.createDirectory();

    // CMakeLists — the same thin wrapper as the 13 existing products.
    repo.getChildFile ("CMakeLists.txt").replaceWithText (
        "# " + spec.productName + " — dehli-musikk-sampler-engine plugin. Everything (formats,\n"
        "# entry point, assets, packaging metadata) comes from dmse_add_plugin — see the\n"
        "# engine repo's cmake/DmsePlugin.cmake.\n"
        "\n"
        "dmse_add_plugin(" + spec.targetName + "\n"
        "    PRODUCT_NAME \"" + spec.productName + "\"\n"
        "    PLUGIN_CODE  " + spec.pluginCode + "\n"
        "    VERSION      " + spec.version + "\n"
        ")\n");

    // House .gitignore: paid audio + build output stay out of the public repo.
    repo.getChildFile (".gitignore").replaceWithText (
        ".DS_Store\n\n"
        "# Paid sample audio + IRs — never commit\n"
        "/assets/samples/\n"
        "/assets/ir/\n\n"
        "# DecentSampler source audio (if a library folder is added later)\n"
        "/DecentSampler/Samples\n"
        "/DecentSampler/IR\n\n"
        "# Build output\n"
        "build/\ncmake-build-*/\nout/\n\n"
        "# Packaging output + secrets\n"
        "*.pkg\n*.dmg\n*-Setup.exe\n*.tar.gz\npackaging/linux/build/\n"
        "*.p8\n*.p12\n*.cer\n");

    repo.getChildFile ("packaging").createDirectory();   // drop packaging/icon.png here

    // Minimal valid manifest (one silent mode) via the engine's own writer, so the
    // Studio and the plugin load it immediately; samples/UI arrive through editing.
    dm::PresetLibrary lib;
    lib.schema  = dm::kManifestSchemaVersion;
    lib.format  = "dmse-manifest";
    lib.library = spec.productName;
    dm::Mode mode;
    mode.name = "Main";
    mode.amp.sustain = 1.0;
    mode.ui.width = 812;
    mode.ui.height = 375;
    mode.ui.tabs.add (dm::Tab{});
    lib.modes.add (std::move (mode));

    const auto manifestDir = repo.getChildFile ("assets").getChildFile ("manifest");
    if (! dm::writeSplitManifest (lib, manifestDir))
    {
        error = "Could not write the manifest under " + manifestDir.getFullPathName();
        return {};
    }
    repo.getChildFile ("assets").getChildFile ("images").createDirectory();
    repo.getChildFile ("assets").getChildFile ("samples").createDirectory();
    repo.getChildFile ("assets").getChildFile ("ir").createDirectory();

    // Register in the superproject build (idempotent).
    auto rootCmake = superRoot.getChildFile ("CMakeLists.txt");
    auto text = rootCmake.loadFileAsString();
    if (! text.contains ("add_subdirectory(" + dirName + ")"))
        rootCmake.appendText ("\nadd_subdirectory(" + dirName + ")\n");

    return repo;
}

} // namespace dmse_studio
