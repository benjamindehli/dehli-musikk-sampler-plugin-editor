# dehli-musikk-sampler-plugin-editor

"DMSE Studio", a JUCE desktop app for authoring and editing [dehli-musikk-sampler-engine](https://github.com/benjamindehli/dehli-musikk-sampler-engine) plugins.

Studio embeds the real engine. Opening a plugin repository instantiates the actual `ManifestPluginProcessor` from the on disk assets, the manifest, images, impulse responses and sample pack, so the preview pane is the genuine plugin in both look and sound, with no compile step. Editing manipulates the engine's manifest model and saves through its writer. Building the distributable plugin is a final step that shells out to CMake.

## Why this works

The manifest is the single source of truth, designed for hand authoring, and the engine renders any UI tree and plays any manifest, so live WYSIWYG preview comes for free. The engine's resource lookup is a callback, so Studio answers it from disk files where a shipping plugin answers from embedded data. Sample import reuses the converter's transcode and pack code. A plugin is just assets plus a short CMakeLists, so creating a new one is a scaffold generator.

## What you can do

* **Open and preview.** Point Studio at a plugin repository. It hosts the real plugin editor with audio out and all MIDI inputs in, and a problems panel shows manifest lint warnings and dangling references. Reload and audio settings are one click away.
* **Model tree and inspector.** Browse the tree of modes, groups, samples, effects and UI nodes and edit their properties. Every edit hot reloads the preview while carrying over processor state. Whole model snapshots drive undo and redo, with dirty tracking and discard guards on open, reload and quit. Saving writes the split manifest folder.
* **Sample import and mapping.** Drag audio files onto a group, or import from the context menu. Files are losslessly transcoded into the assets folder and never trimmed, padded or resampled. Notes are auto mapped from file names, ranges can be auto spread across distinct roots, and round robin variants are detected from the file name pattern.
* **GUI designer.** A Design toggle overlays the hosted editor so you can select, drag, resize and nudge controls over the background, with snapping and live guides. UI edits take a fast path that swaps the mode's UI tree in place without re decoding samples.
* **Effects and bindings editor.** Add effects to modes and groups from the engine's effect types, and create parameter bindings through dropdowns that mirror the engine's compiled routes, with id resolved targets and kind filtered parameters.
* **Build and export.** A new plugin wizard scaffolds a fresh plugin repository and wires it into the superproject root CMake. Build streams the cmake output into the problems panel from a background thread, and Run launches the built Standalone. The full loop is New, import samples, design the GUI, bind, Build and Run the real plugin.

## Building

Studio is built as part of the [dehli-musikk-sample-plugins](https://github.com/benjamindehli/dehli-musikk-sample-plugins) superproject, which provides the JUCE targets and the engine library:

```
cmake --build build --target DmseStudio
```

The artefact lands under `build/dehli-musikk-sampler-plugin-editor/DmseStudio_artefacts/`. Drop a square `packaging/icon.png` and JUCE bakes it into the app bundle. A headless probe target, `DmseStudioProbe`, is used during development and is not shipped.

## Related projects

* [dehli-musikk-sampler-engine](https://github.com/benjamindehli/dehli-musikk-sampler-engine), the sampler engine Studio hosts and edits.
* [ds-plugin-converter](https://github.com/benjamindehli/ds-plugin-converter), which converts DecentSampler libraries into the engine's manifest and whose transcode code Studio reuses for sample import.

## License

This project is licensed under the GNU General Public License version 3. See [LICENSE](LICENSE). JUCE is licensed separately by its own terms.
