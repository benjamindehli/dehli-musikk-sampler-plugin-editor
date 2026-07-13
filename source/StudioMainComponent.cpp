#include "StudioMainComponent.h"
#include "SampleImport.h"
#include "BindingVocab.h"
#include "NewPluginWizard.h"

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
    newButton.onClick = [this] { confirmDiscardThen ([this] { newPluginWizard(); }); };
    addAndMakeVisible (newButton);

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

    buildButton.onClick = [this] { buildPlugin(); };
    addAndMakeVisible (buildButton);

    runButton.onClick = [this] { runStandalone(); };
    addAndMakeVisible (runButton);

    buildRunner.onOutput = [this] (const juce::String& text)
    {
        problems.moveCaretToEnd();
        problems.insertTextAtCaret (text);
    };
    buildRunner.onFinished = [this] (bool success)
    {
        problems.moveCaretToEnd();
        problems.insertTextAtCaret (success ? "\n== BUILD SUCCEEDED ==\n"
                                            : "\n== BUILD FAILED ==\n");
        updateToolbar();
    };

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

    designButton.setClickingTogglesState (true);
    designButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a5a7a));
    designButton.onClick = [this]
    {
        designer.setVisible (designButton.getToggleState() && project != nullptr);
        designer.toFront (false);
    };
    addAndMakeVisible (designButton);

    backgroundButton.onClick = [this] { chooseBackground(); };
    addAndMakeVisible (backgroundButton);

    pathLabel.setText ("No plugin open", juce::dontSendNotification);
    pathLabel.setColour (juce::Label::textColourId, juce::Colour (0xffb0b1b2));
    addAndMakeVisible (pathLabel);

    modelTree.onSelect = [this] (NodeRef ref) { inspector.show (ref); };
    modelTree.onImportFiles = [this] (NodeRef target, const juce::StringArray& files)
    {
        importSamples (target, files);
    };
    modelTree.onAddGroup = [this] (int modeIndex)
    {
        if (project == nullptr) return;
        auto& lib = project->getModel();
        if (modeIndex < 0 || modeIndex >= lib.modes.size()) return;
        project->beginEdit();
        dm::Group g;
        g.uid = "group_" + juce::String (lib.modes.getReference (modeIndex).groups.size());
        lib.modes.getReference (modeIndex).groups.add (std::move (g));
        applyCommittedEdit();
    };
    modelTree.onRemoveNode = [this] (NodeRef ref)
    {
        if (project == nullptr) return;
        auto& lib = project->getModel();
        if (ref.mode < 0 || ref.mode >= lib.modes.size()) return;
        auto& m = lib.modes.getReference (ref.mode);
        project->beginEdit();
        if (ref.kind == NodeRef::Kind::sample)
        {
            if (ref.a >= 0 && ref.a < m.groups.size()
                && ref.b >= 0 && ref.b < m.groups.getReference (ref.a).samples.size())
                m.groups.getReference (ref.a).samples.remove (ref.b);
        }
        else if (ref.kind == NodeRef::Kind::group)
        {
            if (ref.a >= 0 && ref.a < m.groups.size())
                m.groups.remove (ref.a);   // dangling bindings show up in the lint
        }
        else if (ref.kind == NodeRef::Kind::effect)
        {
            if (ref.a >= 0 && ref.a < m.effects.size())
                m.effects.remove (ref.a);
        }
        else if (ref.kind == NodeRef::Kind::groupEffect)
        {
            if (ref.a >= 0 && ref.a < m.groups.size()
                && ref.b >= 0 && ref.b < m.groups.getReference (ref.a).effects.size())
                m.groups.getReference (ref.a).effects.remove (ref.b);
        }
        else if (ref.kind == NodeRef::Kind::controlBinding)
        {
            if (! m.ui.tabs.isEmpty() && ref.a >= 0 && ref.a < m.ui.tabs.getReference (0).controls.size()
                && ref.b >= 0 && ref.b < m.ui.tabs.getReference (0).controls.getReference (ref.a).bindings.size())
                m.ui.tabs.getReference (0).controls.getReference (ref.a).bindings.remove (ref.b);
        }
        else if (! m.ui.tabs.isEmpty())
        {
            auto& tab = m.ui.tabs.getReference (0);
            if (ref.kind == NodeRef::Kind::control && ref.a >= 0 && ref.a < tab.controls.size())
                tab.controls.remove (ref.a);
            else if (ref.kind == NodeRef::Kind::button && ref.a >= 0 && ref.a < tab.buttons.size())
                tab.buttons.remove (ref.a);
            else if (ref.kind == NodeRef::Kind::menu && ref.a >= 0 && ref.a < tab.menus.size())
                tab.menus.remove (ref.a);
            else if (ref.kind == NodeRef::Kind::image && ref.a >= 0 && ref.a < tab.images.size())
                tab.images.remove (ref.a);
        }
        applyCommittedEdit();
    };
    modelTree.onAddEffect = [this] (NodeRef parent, const juce::String& type)
    {
        addEffect (parent, type);
    };
    modelTree.onAddWidget = [this] (NodeRef uiRoot, const juce::String& kind)
    {
        addWidget (uiRoot, kind);
    };
    modelTree.onAddBinding = [this] (NodeRef ctl)
    {
        if (project == nullptr) return;
        auto& lib = project->getModel();
        if (ctl.mode < 0 || ctl.mode >= lib.modes.size()
            || lib.modes.getReference (ctl.mode).ui.tabs.isEmpty())
            return;
        auto& tab = lib.modes.getReference (ctl.mode).ui.tabs.getReference (0);
        if (ctl.a < 0 || ctl.a >= tab.controls.size())
            return;

        project->beginEdit();
        dm::Binding b;
        b.type = "amp";
        b.level = "instrument";
        b.parameter = "AMP_VOLUME";
        b.translation = "linear";
        b.translationOutputMin = 0.0;
        b.translationOutputMax = 1.0;
        auto& c = tab.controls.getReference (ctl.a);
        c.bindings.add (std::move (b));
        const NodeRef newRef { NodeRef::Kind::controlBinding, ctl.mode, ctl.a, c.bindings.size() - 1 };
        applyCommittedEdit();
        inspector.show (newRef);   // jump straight into editing the fresh binding
    };
    modelTree.onSpreadRanges = [this] (NodeRef ref)
    {
        if (auto* g = resolveGroup (ref))
        {
            project->beginEdit();
            spreadRanges (*g);
            applyCommittedEdit();
        }
    };
    addAndMakeVisible (modelTree);

    inspector.getModel   = [this] () -> dm::PresetLibrary& { return project->getModel(); };
    inspector.beginEdit  = [this] { if (project != nullptr) project->beginEdit(); };
    inspector.commitEdit = [this] { applyCommittedEdit(); };
    addAndMakeVisible (inspector);

    editorHolder.onChildResized = [this] { layoutEditor(); };
    addAndMakeVisible (editorHolder);

    // GUI designer overlay: tracks the hosted editor's face on its own timer and
    // turns drags into rect commits through the UI-only hot path (no re-decode).
    designer.getMode = [this] () -> dm::Mode*
    {
        if (project == nullptr) return nullptr;
        auto& lib = project->getModel();
        const int i = project->getProcessor().getActiveModeIndex();
        return i >= 0 && i < lib.modes.size() ? &lib.modes.getReference (i) : nullptr;
    };
    designer.getModeIndex = [this]
    {
        return project != nullptr ? project->getProcessor().getActiveModeIndex() : 0;
    };
    designer.findFace = [this] () -> juce::Component*
    {
        if (pluginEditor == nullptr) return nullptr;
        std::function<juce::Component* (juce::Component&)> find =
            [&find] (juce::Component& c) -> juce::Component*
        {
            if (dynamic_cast<dm::ManifestUiComponent*> (&c) != nullptr)
                return &c;
            for (auto* child : c.getChildren())
                if (auto* hit = find (*child))
                    return hit;
            return nullptr;
        };
        return find (*pluginEditor);
    };
    designer.onSelect = [this] (NodeRef ref) { inspector.show (ref); };
    designer.onCommitRect = [this] (NodeRef ref, dm::Rect r)
    {
        if (project == nullptr) return;
        auto& lib = project->getModel();
        if (ref.mode < 0 || ref.mode >= lib.modes.size() || lib.modes.getReference (ref.mode).ui.tabs.isEmpty())
            return;
        auto& tab = lib.modes.getReference (ref.mode).ui.tabs.getReference (0);

        project->beginEdit();
        if (ref.kind == NodeRef::Kind::control && ref.a < tab.controls.size())
            tab.controls.getReference (ref.a).rect = r;
        else if (ref.kind == NodeRef::Kind::button && ref.a < tab.buttons.size())
            tab.buttons.getReference (ref.a).rect = r;
        else if (ref.kind == NodeRef::Kind::menu && ref.a < tab.menus.size())
            tab.menus.getReference (ref.a).rect = r;
        else if (ref.kind == NodeRef::Kind::image && ref.a < tab.images.size())
            tab.images.getReference (ref.a).rect = r;
        project->commitUiEdit (ref.mode);   // hot path: face rebuild only
        inspector.refresh();
        refreshProblems();
        updateToolbar();
    };
    designer.setVisible (false);
    editorHolder.addChildComponent (designer);

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
    designer.toFront (false);   // stays above a freshly attached editor
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

juce::String StudioMainComponent::parsedTarget() const
{
    if (project == nullptr) return {};
    const auto text = project->getRepoRoot().getChildFile ("CMakeLists.txt").loadFileAsString();
    const auto i = text.indexOf ("dmse_add_plugin(");
    if (i < 0) return {};
    auto rest = text.substring (i + 16).trimStart();
    return rest.initialSectionContainingOnly (
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
}

juce::String StudioMainComponent::parsedProduct() const
{
    if (project == nullptr) return {};
    const auto text = project->getRepoRoot().getChildFile ("CMakeLists.txt").loadFileAsString();
    const auto i = text.indexOf ("PRODUCT_NAME");
    if (i < 0) return {};
    const auto q1 = text.indexOf (i, "\"");
    const auto q2 = q1 >= 0 ? text.indexOf (q1 + 1, "\"") : -1;
    return q1 >= 0 && q2 > q1 ? text.substring (q1 + 1, q2) : juce::String();
}

juce::File StudioMainComponent::standaloneArtefact() const
{
    if (project == nullptr) return {};
    const auto superRoot = project->getRepoRoot().getParentDirectory();
    const auto base = superRoot.getChildFile ("build")
                          .getChildFile (project->getRepoRoot().getFileName())
                          .getChildFile (parsedTarget() + "_artefacts");
    const auto product = parsedProduct();
    for (const auto& cfg : { juce::String ("Release"), juce::String ("Debug"), juce::String() })
    {
        auto dir = cfg.isNotEmpty() ? base.getChildFile (cfg) : base;
       #if JUCE_MAC
        auto app = dir.getChildFile ("Standalone").getChildFile (product + ".app");
       #else
        auto app = dir.getChildFile ("Standalone").getChildFile (product);
       #endif
        if (app.exists())
            return app;
    }
    return {};
}

void StudioMainComponent::buildPlugin()
{
    if (project == nullptr || buildRunner.isRunning())
        return;
    const auto target = parsedTarget();
    const auto superRoot = project->getRepoRoot().getParentDirectory();
    if (target.isEmpty()
        || ! superRoot.getChildFile ("dehli-musikk-sampler-engine").isDirectory())
    {
        problems.setText ("Build needs the plugin repo to sit inside the superproject "
                          "(with dehli-musikk-sampler-engine/) and its CMakeLists to call "
                          "dmse_add_plugin.");
        return;
    }

    problems.setText ("Building " + target + "...\n");
    buildRunner.start ({ "cmake -B build -DCMAKE_BUILD_TYPE=Release",
                         "cmake --build build --parallel 8 --target " + target + "_Standalone" },
                       superRoot);
    updateToolbar();
}

void StudioMainComponent::runStandalone()
{
    const auto app = standaloneArtefact();
    if (! app.exists())
    {
        problems.setText ("No built standalone found - build first.");
        return;
    }
    app.startAsProcess();
}

void StudioMainComponent::newPluginWizard()
{
    // 1. Superproject root (defaults next to the open project / the last used dir).
    auto initial = project != nullptr ? project->getRepoRoot().getParentDirectory()
                                      : (lastDir.exists() ? lastDir
                                         : juce::File::getSpecialLocation (juce::File::userHomeDirectory));
    chooser = std::make_unique<juce::FileChooser> ("Choose the superproject root "
                                                   "(contains dehli-musikk-sampler-engine/)", initial);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto superRoot = fc.getResult();
        if (! superRoot.isDirectory())
            return;

        // 2. Name / code / version.
        auto* aw = new juce::AlertWindow ("New plugin", "", juce::MessageBoxIconType::NoIcon, this);
        aw->addTextEditor ("product", "", "Product name");
        aw->addTextEditor ("code", "auto", "Plugin code (4 chars, 'auto' = derive)");
        aw->addTextEditor ("version", "0.1.0", "Version");
        aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        aw->enterModalState (true, juce::ModalCallbackFunction::create (
            [this, aw, superRoot] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owned (aw);
            if (result != 1)
                return;
            NewPluginSpec spec;
            spec.productName = aw->getTextEditorContents ("product").trim();
            if (spec.productName.isEmpty())
                return;
            spec.targetName = defaultTargetName (spec.productName);
            const auto code = aw->getTextEditorContents ("code").trim();
            spec.pluginCode = (code.isEmpty() || code == "auto") ? defaultPluginCode (spec.productName) : code;
            spec.version = aw->getTextEditorContents ("version").trim();
            if (spec.version.isEmpty())
                spec.version = "0.1.0";

            juce::String error;
            const auto repo = createNewPlugin (superRoot, spec, error);
            if (repo == juce::File())
            {
                problems.setText ("NEW PLUGIN FAILED: " + error);
                return;
            }
            openRepo (repo);
            problems.setText ("Created " + repo.getFullPathName()
                              + " and registered it in the superproject CMakeLists.\n"
                              + "Import samples onto the group tree, set a Background, then Build.\n\n"
                              + problems.getText());
        }), false);
    });
}

void StudioMainComponent::addEffect (NodeRef parent, const juce::String& type)
{
    if (project == nullptr)
        return;

    // Convolution needs an impulse response: pick + transcode it first, then create.
    if (type == "convolution")
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose an impulse response",
                                                       juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, parent, type] (const juce::FileChooser& fc)
        {
            const auto src = fc.getResult();
            if (! src.existsAsFile() || project == nullptr)
                return;
            auto irDir = project->getRepoRoot().getChildFile ("assets").getChildFile ("ir");
            auto irInUse = [this] (const juce::String& stem)
            {
                const auto id = "ir:" + stem;
                for (const auto& m : project->getModel().modes)
                {
                    for (const auto& e : m.effects)
                        if (e.ir == id) return true;
                    for (const auto& g : m.groups)
                        for (const auto& e : g.effects)
                            if (e.ir == id) return true;
                }
                return false;
            };
            juce::String error;
            const auto imported = transcodeToFlac (src, irDir, irInUse, error);
            if (! imported)
            {
                problems.setText ("IR import failed: " + error);
                return;
            }
            addEffectNow (parent, type, "ir:" + imported->stem);
        });
        return;
    }
    addEffectNow (parent, type, {});
}

void StudioMainComponent::addEffectNow (NodeRef parent, const juce::String& type, const juce::String& irId)
{
    auto& lib = project->getModel();
    if (parent.mode < 0 || parent.mode >= lib.modes.size())
        return;
    auto& m = lib.modes.getReference (parent.mode);

    // Unique readable id across the mode's instrument + group chains (converter style).
    auto idTaken = [&m] (const juce::String& id)
    {
        for (const auto& e : m.effects)
            if (e.id == id) return true;
        for (const auto& g : m.groups)
            for (const auto& e : g.effects)
                if (e.id == id) return true;
        return false;
    };
    auto id = "fx_" + type;
    for (int n = 2; idTaken (id); ++n)
        id = "fx_" + type + "_" + juce::String (n);

    project->beginEdit();
    auto e = vocab::makeEffect (type, id);
    e.ir = irId;
    if (parent.kind == NodeRef::Kind::group && parent.a >= 0 && parent.a < m.groups.size())
        m.groups.getReference (parent.a).effects.add (std::move (e));
    else
        m.effects.add (std::move (e));
    applyCommittedEdit();
}

juce::String StudioMainComponent::importImageAsset (const juce::File& src, juce::String& error)
{
    auto imagesDir = project->getRepoRoot().getChildFile ("assets").getChildFile ("images");
    imagesDir.createDirectory();
    auto idInUse = [this] (const juce::String& stem)
    {
        const auto id = "img:" + stem;
        for (const auto& m : project->getModel().modes)
        {
            if (m.ui.background == id) return true;
            for (const auto& tab : m.ui.tabs)
            {
                for (const auto& im : tab.images)  if (im.image == id) return true;
                for (const auto& c : tab.controls) if (c.skin && c.skin->image == id) return true;
                for (const auto& b : tab.buttons)
                    for (const auto& st : b.states)
                        if (st.mainImage == id || st.hoverImage == id || st.clickImage == id)
                            return true;
            }
        }
        return false;
    };
    auto stem = src.getFileNameWithoutExtension();
    for (int n = 2; idInUse (stem); ++n)
        stem = src.getFileNameWithoutExtension() + "_" + juce::String (n);
    const auto dest = imagesDir.getChildFile (stem + "." + src.getFileExtension().trimCharactersAtStart ("."));
    dest.deleteFile();
    if (! src.copyFileTo (dest))
    {
        error = "could not copy " + src.getFileName() + " into assets/images";
        return {};
    }
    return stem;
}

void StudioMainComponent::addWidget (NodeRef uiRoot, const juce::String& kind)
{
    if (project == nullptr)
        return;
    const juce::String title = kind == "knob"   ? "Choose the knob's filmstrip image (square frames stacked vertically)"
                             : kind == "button" ? "Choose the button's state images (one per state, e.g. off then on)"
                                                : "Choose the image";
    chooser = std::make_unique<juce::FileChooser> (title, juce::File(), "*.png;*.jpg;*.jpeg");
    int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    if (kind == "button")
        chooserFlags |= juce::FileBrowserComponent::canSelectMultipleItems;

    chooser->launchAsync (chooserFlags, [this, uiRoot, kind] (const juce::FileChooser& fc)
    {
        if (project == nullptr || fc.getResults().isEmpty())
            return;
        auto& lib = project->getModel();
        if (uiRoot.mode < 0 || uiRoot.mode >= lib.modes.size())
            return;
        auto& m = lib.modes.getReference (uiRoot.mode);
        if (m.ui.tabs.isEmpty())
            m.ui.tabs.add (dm::Tab{});
        auto& tab = m.ui.tabs.getReference (0);

        project->beginEdit();
        juce::StringArray log;

        if (kind == "knob")
        {
            juce::String error;
            const auto stem = importImageAsset (fc.getResult(), error);
            if (stem.isEmpty()) { problems.setText ("ADD KNOB FAILED: " + error); return; }

            const auto img = juce::ImageFileFormat::loadFrom (fc.getResult());
            int frames = 31;
            if (img.isValid() && img.getWidth() > 0 && img.getHeight() % img.getWidth() == 0)
                frames = img.getHeight() / img.getWidth();   // square frames stacked vertically
            else
                log.add ("could not auto-detect the frame count (image height is not a multiple "
                         "of its width) - defaulted to 31; fix it in the inspector if needed");

            dm::Control c;
            c.label = "Knob " + juce::String (tab.controls.size() + 1);
            c.rect  = { 40, 40, 60, 60 };
            c.min = 0.0; c.max = 1.0; c.value = 1.0;
            c.style = "custom_skin_vertical_drag";
            dm::CustomSkin skin;
            skin.image = "img:" + stem;
            skin.numFrames = frames;
            skin.orientation = "vertical";
            c.skin = skin;
            tab.controls.add (std::move (c));
            log.add ("added knob (" + juce::String (frames) + " frames). Rename it in the "
                     "inspector, place it in Design mode, then Add binding to make it do something.");
        }
        else if (kind == "button")
        {
            auto files = fc.getResults();
            files.sort();   // "..._off" sorts before "..._on"
            dm::Button b;
            b.id = "btn_new_" + juce::String (tab.buttons.size());
            b.style = "image";
            b.value = 0;
            juce::Rectangle<int> size;
            for (const auto& f : files)
            {
                juce::String error;
                const auto stem = importImageAsset (f, error);
                if (stem.isEmpty()) { log.add ("skipped " + f.getFileName() + ": " + error); continue; }
                if (size.isEmpty())
                    if (auto img = juce::ImageFileFormat::loadFrom (f); img.isValid())
                        size = { img.getWidth(), img.getHeight() };
                dm::ButtonState st;
                st.name = f.getFileNameWithoutExtension();
                st.mainImage = st.hoverImage = st.clickImage = "img:" + stem;
                b.states.add (std::move (st));
            }
            if (b.states.isEmpty()) { problems.setText ("ADD BUTTON FAILED: no images imported"); return; }
            b.rect = { 40, 40, size.isEmpty() ? 60 : size.getWidth(), size.isEmpty() ? 30 : size.getHeight() };
            tab.buttons.add (std::move (b));
            log.add ("added button with " + juce::String (tab.buttons.getReference (tab.buttons.size() - 1).states.size())
                     + " state(s) (files sorted by name = state order). Place it in Design mode.");
        }
        else   // plain image
        {
            juce::String error;
            const auto stem = importImageAsset (fc.getResult(), error);
            if (stem.isEmpty()) { problems.setText ("ADD IMAGE FAILED: " + error); return; }
            const auto img = juce::ImageFileFormat::loadFrom (fc.getResult());
            dm::UiImage im;
            im.id = "uiimg_" + juce::String (tab.images.size());
            im.image = "img:" + stem;
            im.rect = { 40, 40, img.isValid() ? img.getWidth() : 100, img.isValid() ? img.getHeight() : 100 };
            tab.images.add (std::move (im));
            log.add ("added image at its natural size. Place it in Design mode.");
        }

        applyCommittedEdit();
        problems.setText (log.joinIntoString ("\n") + "\n\n" + problems.getText());
    });
}

void StudioMainComponent::chooseBackground()
{
    if (project == nullptr)
        return;
    chooser = std::make_unique<juce::FileChooser> ("Choose a background image",
                                                   juce::File(), "*.png;*.jpg;*.jpeg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        const auto src = fc.getResult();
        if (! src.existsAsFile() || project == nullptr)
            return;

        // Copy into assets/images, uniquified against ids the model references.
        auto imagesDir = project->getRepoRoot().getChildFile ("assets").getChildFile ("images");
        imagesDir.createDirectory();
        auto idInUse = [this] (const juce::String& stem)
        {
            const auto id = "img:" + stem;
            for (const auto& m : project->getModel().modes)
            {
                if (m.ui.background == id) return true;
                for (const auto& tab : m.ui.tabs)
                    for (const auto& im : tab.images)
                        if (im.image == id) return true;
            }
            return false;
        };
        auto stem = src.getFileNameWithoutExtension();
        for (int n = 2; idInUse (stem); ++n)
            stem = src.getFileNameWithoutExtension() + "_" + juce::String (n);
        const auto dest = imagesDir.getChildFile (stem + "." + src.getFileExtension().trimCharactersAtStart ("."));
        dest.deleteFile();
        if (! src.copyFileTo (dest))
        {
            problems.setText ("Could not copy the image into assets/images.");
            return;
        }

        const int mi = project->getProcessor().getActiveModeIndex();
        auto& lib = project->getModel();
        if (mi < 0 || mi >= lib.modes.size())
            return;
        project->beginEdit();
        lib.modes.getReference (mi).ui.background = "img:" + stem;
        applyCommittedEdit();   // full reload: a NEW image asset must enter the cache

        // Visible confirmation — if the background ever fails to render again, this
        // line says which link broke (id in the model vs decodable file on disk).
        const auto check = project->getProcessor().loadImage ("img:" + stem);
        problems.setText ("Background set to img:" + stem + " - image "
                          + (check.isValid()
                                 ? juce::String (check.getWidth()) + "x" + juce::String (check.getHeight()) + " OK"
                                 : juce::String ("FAILED TO LOAD (check assets/images)"))
                          + "\n\n" + problems.getText());
    });
}

dm::Group* StudioMainComponent::resolveGroup (NodeRef ref)
{
    if (project == nullptr)
        return nullptr;
    auto& lib = project->getModel();
    if (ref.mode < 0 || ref.mode >= lib.modes.size())
        return nullptr;
    auto& m = lib.modes.getReference (ref.mode);
    const int gi = (ref.kind == NodeRef::Kind::group
                    || ref.kind == NodeRef::Kind::sample
                    || ref.kind == NodeRef::Kind::groupEffect) ? ref.a : -1;
    return gi >= 0 && gi < m.groups.size() ? &m.groups.getReference (gi) : nullptr;
}

void StudioMainComponent::importSamples (NodeRef target, const juce::StringArray& files)
{
    if (project == nullptr)
        return;

    // Resolve the destination group: the dropped-on / right-clicked group, else a
    // mode's only group, else give up with a hint.
    auto* g = resolveGroup (target);
    if (g == nullptr && target.mode >= 0 && target.mode < project->getModel().modes.size())
    {
        auto& m = project->getModel().modes.getReference (target.mode);
        if (m.groups.size() == 1)
            g = &m.groups.getReference (0);
    }
    if (g == nullptr)
    {
        problems.setText ("Import: drop the files onto a specific GROUP in the tree "
                          "(or right-click a group and choose Import samples...).");
        return;
    }

    project->beginEdit();
    juce::StringArray log;
    int imported = 0;
    bool anyRoundRobin = false;
    for (const auto& path : files)
    {
        const juce::File f (path);
        juce::String error;
        auto stemInUse = [this] (const juce::String& stem)
        {
            const auto id = "flac:" + stem;
            for (const auto& m : project->getModel().modes)
                for (const auto& grp : m.groups)
                    for (const auto& smp : grp.samples)
                        if (smp.source == id)
                            return true;
            return false;
        };
        const auto result = transcodeToFlac (f, project->getSamplesDir(), stemInUse, error);
        if (! result)
        {
            log.add ("FAILED " + f.getFileName() + ": " + error);
            continue;
        }
        dm::Sample s;
        s.source       = "flac:" + result->stem;
        s.sampleRate   = result->sampleRate;
        s.lengthFrames = (int) result->frames;
        const auto parsed = parseSampleName (result->stem);
        s.loNote = s.hiNote = s.rootNote = parsed.note.value_or (60);
        s.pitchKeyTrack = false;
        s.seqPosition = parsed.roundRobin;
        anyRoundRobin = anyRoundRobin || parsed.roundRobin.has_value();
        g->samples.add (std::move (s));
        ++imported;
        log.add ("imported " + result->stem + " -> note " + juce::String (parsed.note.value_or (60))
                 + (parsed.roundRobin ? "  rr slot " + juce::String (*parsed.roundRobin) : juce::String())
                 + (parsed.note ? juce::String() : juce::String ("  (no note in name - placed at 60)")));
    }

    // Files carried round-robin slots → make the group actually cycle them.
    if (anyRoundRobin && ! g->roundRobin.has_value())
    {
        dm::RoundRobin rr;
        rr.mode = "round_robin";
        g->roundRobin = rr;
        log.add ("group set to round_robin (file names carry rr slots)");
    }
    applyCommittedEdit();

    // Show the import summary on top of the refreshed lint.
    problems.setText ("Imported " + juce::String (imported) + "/" + juce::String (files.size())
                      + " file(s) into the group. Right-click the group for "
                      + juce::String::fromUTF8 ("\u201c") + "Auto-spread ranges"
                      + juce::String::fromUTF8 ("\u201d") + " if this is a sparse multi-sample.\n"
                      + log.joinIntoString ("\n") + "\n\n" + problems.getText());
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
    designButton.setEnabled (open);
    backgroundButton.setEnabled (open);
    buildButton.setEnabled (open && ! buildRunner.isRunning());
    runButton.setEnabled (open && standaloneArtefact().exists());
    if (! open)
    {
        designButton.setToggleState (false, juce::dontSendNotification);
        designer.setVisible (false);
    }
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
    for (auto* b : { &newButton, &openButton, &saveButton, &undoButton, &redoButton, &reloadButton,
                     &designButton, &backgroundButton, &buildButton, &runButton, &audioButton })
    {
        b->setBounds (top.removeFromLeft (b == &backgroundButton ? 110
                                          : b == &openButton || b == &newButton ? 74 : 62));
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
