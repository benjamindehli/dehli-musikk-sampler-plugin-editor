#pragma once

// A plugin repo opened for EDITING. The single source of truth after opening is
// the in-memory `model` (dm::PresetLibrary): the processor is always built from a
// serialization of that model (Assets::manifestJson), while images / IRs / the
// sample pack are served from the repo's asset files on disk. Saving writes the
// model back as the split manifest folder; undo/redo are whole-model snapshots
// (manifests are small JSON — snapshots are simpler and safer than per-field
// command objects).
//
// The processor is recreated on every committed edit ("hot reload"): its state
// (APVTS values, MIDI mappings, active mode) is carried across via
// get/setStateInformation, and the mode chooser is suppressed by confirming the
// restored mode programmatically.

#include <ManifestPluginProcessor.h>
#include <model/ManifestLoader.h>
#include <model/ManifestWriter.h>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <vector>

namespace dmse_studio
{

class PluginProject
{
public:
    /** Open `repoDir` (a plugin repo, or any folder holding assets/manifest/index.json). */
    static std::unique_ptr<PluginProject> open (const juce::File& repoDir, juce::String& error)
    {
        auto assetsDir = repoDir.getChildFile ("assets");
        if (! assetsDir.getChildFile ("manifest").getChildFile ("index.json").existsAsFile())
        {
            if (repoDir.getChildFile ("manifest").getChildFile ("index.json").existsAsFile())
                assetsDir = repoDir;   // pointed straight at an assets folder
            else
            {
                error = "No assets/manifest/index.json under " + repoDir.getFullPathName();
                return nullptr;
            }
        }

        auto p = std::unique_ptr<PluginProject> (new PluginProject());
        p->repoRoot  = repoDir;
        p->assetsDir = assetsDir;

        p->lint = dm::loadManifestFromFolder (assetsDir.getChildFile ("manifest"));
        if (! p->lint.ok)
        {
            error = "Manifest failed to load: " + p->lint.errors.joinIntoString ("; ");
            return nullptr;
        }
        p->model = p->lint.library;
        p->createProcessor();
        return p;
    }

    // ── The editable model ───────────────────────────────────────────────────
    dm::PresetLibrary& getModel()             { return model; }
    const dm::PresetLibrary& getModel() const { return model; }

    /** Call BEFORE mutating the model (snapshots for undo, clears redo). */
    void beginEdit()
    {
        undoStack.push_back (dm::writeManifestToJson (model, true));
        if (undoStack.size() > 100)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
    }

    /** Call AFTER mutating: marks dirty, refreshes lint, rebuilds the processor
        (state preserved). The caller re-attaches editor/player afterwards. */
    void commitEdit()
    {
        dirty = true;
        recreateProcessor();
    }

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }

    void undo()
    {
        if (undoStack.empty()) return;
        redoStack.push_back (dm::writeManifestToJson (model, true));
        restoreSnapshot (undoStack.back());
        undoStack.pop_back();
    }

    void redo()
    {
        if (redoStack.empty()) return;
        undoStack.push_back (dm::writeManifestToJson (model, true));
        restoreSnapshot (redoStack.back());
        redoStack.pop_back();
    }

    bool isDirty() const { return dirty; }

    /** Write the model back to the repo's split manifest folder. Refuses when a
        hand-authored partials/ folder exists (saving would inline it forever). */
    bool save (juce::String& error)
    {
        const auto manifestDir = assetsDir.getChildFile ("manifest");
        if (manifestDir.getChildFile ("partials").isDirectory())
        {
            error = "This manifest uses hand-authored partials/ ($use/$ref) - saving from the "
                    "Studio would permanently inline them. Edit it by hand instead.";
            return false;
        }
        if (! dm::writeSplitManifest (model, manifestDir))
        {
            error = "Could not write " + manifestDir.getFullPathName();
            return false;
        }
        dirty = false;
        return true;
    }

    // ── Hosting ──────────────────────────────────────────────────────────────
    dm::ManifestPluginProcessor& getProcessor() { return *processor; }
    const dm::ManifestParseResult& getLint() const { return lint; }
    juce::File getRepoRoot() const  { return repoRoot; }
    juce::File getSamplesDir() const { return assetsDir.getChildFile ("samples"); }
    juce::String getName() const    { return repoRoot.getFileName(); }

    ~PluginProject() = default;

private:
    PluginProject() = default;

    void restoreSnapshot (const juce::String& snapshot)
    {
        if (auto parsed = dm::loadManifestFromJson (snapshot); parsed.ok)
            model = parsed.library;
        dirty = true;
        recreateProcessor();
    }

    void createProcessor()
    {
        // The processor parses Assets::manifestJson (single-manifest fallback path);
        // findResource deliberately does NOT serve index.json, so the serialized
        // in-memory model is always what plays. Keep the UTF8 alive for its lifetime.
        manifestJsonUtf8 = dm::writeManifestToJson (model, true).toStdString();

        dm::ManifestPluginProcessor::Assets a;
        a.name             = getName();
        a.version          = "studio";
        a.manifestJson     = manifestJsonUtf8.c_str();
        a.manifestJsonSize = (int) manifestJsonUtf8.size();
        a.packFile         = assetsDir.getChildFile ("samples").getChildFile ("samples.pak");

        a.findResource = [this] (const juce::String& filename, int& sizeOut) -> const char*
        {
            const auto key = filename.toStdString();
            auto it = cache.find (key);
            if (it == cache.end())
            {
                const juce::File dirs[] = {
                    assetsDir.getChildFile ("images"),
                    assetsDir.getChildFile ("ir"),
                    assetsDir.getChildFile ("samples"),
                };
                juce::MemoryBlock mb;
                for (const auto& d : dirs)
                    if (auto f = d.getChildFile (filename); f.existsAsFile())
                    {
                        f.loadFileAsData (mb);
                        break;
                    }
                it = cache.emplace (key, std::move (mb)).first;   // empty = known missing
            }
            if (it->second.getSize() == 0) { sizeOut = 0; return nullptr; }
            sizeOut = (int) it->second.getSize();
            return static_cast<const char*> (it->second.getData());
        };

        processor = std::make_unique<dm::ManifestPluginProcessor> (std::move (a));
        processor->setEditorManagesWindow (false);   // the Studio's window is not the plugin's
    }

    void recreateProcessor()
    {
        // Refresh the lint from the edited model (same loader = same warnings).
        const auto json = dm::writeManifestToJson (model, true);
        lint = dm::loadManifestFromJson (json);
        cache.clear();   // imported samples appear on disk between reloads

        juce::MemoryBlock state;
        int modeIndex = 0;
        if (processor != nullptr)
        {
            processor->getStateInformation (state);
            modeIndex = processor->getActiveModeIndex();
        }

        processor.reset();   // caller must have detached editor + player first
        createProcessor();

        if (state.getSize() > 0)
            processor->setStateInformation (state.getData(), (int) state.getSize());
        // The Studio is an app, so the processor treats state restore like a fresh
        // standalone session and would show the mode chooser — confirm the mode we
        // were on instead (also triggers the first build after the restore).
        processor->confirmModeChoice (juce::jlimit (0, juce::jmax (0, model.modes.size() - 1),
                                                    modeIndex));
    }

    juce::File repoRoot, assetsDir;
    dm::PresetLibrary model;
    dm::ManifestParseResult lint;
    bool dirty = false;
    std::vector<juce::String> undoStack, redoStack;

    std::string manifestJsonUtf8;
    std::map<std::string, juce::MemoryBlock> cache;
    std::unique_ptr<dm::ManifestPluginProcessor> processor;
};

} // namespace dmse_studio
