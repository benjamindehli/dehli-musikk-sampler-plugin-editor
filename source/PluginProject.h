#pragma once

// A plugin repo opened from disk. Locates the converter-generated assets folder,
// serves its files to the engine through Assets::findResource (the same callback
// the shipping plugins answer from embedded BinaryData), and instantiates a REAL
// ManifestPluginProcessor — so the Studio previews exactly what ships.
//
// Resources are looked up by basename (the engine's convention: BinaryData
// flattens paths, so the manifest references files by name only). Loaded blobs
// are cached here and must outlive the processor.

#include <ManifestPluginProcessor.h>
#include <model/ManifestLoader.h>
#include <juce_core/juce_core.h>
#include <map>
#include <memory>

namespace dmse_studio
{

class PluginProject
{
public:
    /** Try to open `repoDir` (a plugin repo or any folder holding an `assets/`
        with manifest/index.json). Returns null + `error` on failure. */
    static std::unique_ptr<PluginProject> open (const juce::File& repoDir, juce::String& error)
    {
        auto assetsDir = repoDir.getChildFile ("assets");
        if (! assetsDir.getChildFile ("manifest").getChildFile ("index.json").existsAsFile())
        {
            // Allow pointing straight at an assets folder too.
            if (repoDir.getChildFile ("manifest").getChildFile ("index.json").existsAsFile())
                assetsDir = repoDir;
            else
            {
                error = "No assets/manifest/index.json under " + repoDir.getFullPathName();
                return nullptr;
            }
        }

        auto p = std::unique_ptr<PluginProject> (new PluginProject());
        p->repoRoot  = repoDir;
        p->assetsDir = assetsDir;

        // Lint pass: the same loader the engine uses, but keeping warnings/errors
        // for the Studio's problems panel.
        p->lint = dm::loadManifestFromFolder (assetsDir.getChildFile ("manifest"));
        if (! p->lint.ok)
        {
            error = "Manifest failed to load: " + p->lint.errors.joinIntoString ("; ");
            return nullptr;
        }

        p->createProcessor();
        return p;
    }

    dm::ManifestPluginProcessor& getProcessor() { return *processor; }
    const dm::ManifestParseResult& getLint() const { return lint; }
    juce::File getRepoRoot() const  { return repoRoot; }
    juce::String getName() const    { return repoRoot.getFileName(); }

    ~PluginProject() = default;

private:
    PluginProject() = default;

    void createProcessor()
    {
        dm::ManifestPluginProcessor::Assets a;
        a.name    = getName();
        a.version = "studio";
        a.packFile = assetsDir.getChildFile ("samples").getChildFile ("samples.pak");

        // Serve any asset by basename from the repo's asset folders, caching the
        // bytes so the returned pointers stay valid for the processor's lifetime.
        a.findResource = [this] (const juce::String& filename, int& sizeOut) -> const char*
        {
            const auto key = filename.toStdString();
            auto it = cache.find (key);
            if (it == cache.end())
            {
                const juce::File dirs[] = {
                    assetsDir.getChildFile ("manifest"),
                    assetsDir.getChildFile ("manifest").getChildFile ("modes"),
                    assetsDir.getChildFile ("manifest").getChildFile ("partials"),
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
                it = cache.emplace (key, std::move (mb)).first;   // empty block = "known missing"
            }
            if (it->second.getSize() == 0)
            {
                sizeOut = 0;
                return nullptr;
            }
            sizeOut = (int) it->second.getSize();
            return static_cast<const char*> (it->second.getData());
        };

        processor = std::make_unique<dm::ManifestPluginProcessor> (std::move (a));
    }

    juce::File repoRoot, assetsDir;
    dm::ManifestParseResult lint;
    std::map<std::string, juce::MemoryBlock> cache;
    std::unique_ptr<dm::ManifestPluginProcessor> processor;
};

} // namespace dmse_studio
