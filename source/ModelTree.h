#pragma once

// Tree view over the manifest model. Items reference nodes by INDEX PATH
// (NodeRef), never by pointer — every committed edit and every undo replaces the
// model's arrays, so pointers into it don't survive. The tree is rebuilt after
// structural changes; selection is preserved by re-resolving the NodeRef.

#include "BindingVocab.h"
#include <model/Manifest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace dmse_studio
{

/** An index path into the PresetLibrary. `a`/`b` are generic slots whose meaning
    depends on `kind` (e.g. group index / sample index). */
struct NodeRef
{
    enum class Kind
    {
        none, library, mode, amp, group, sample,
        effect,        // mode-level: a = effect
        groupEffect,   // a = group, b = effect
        sequences,     // summary leaf (count)
        uiRoot,        // mode's ui (width/height/cropTop/background)
        control,       // a = control (tab 0)
        controlBinding,// a = control, b = binding
        button,        // a = button
        menu,          // a = menu
        image,         // a = image
    };
    Kind kind = Kind::none;
    int mode = -1, a = -1, b = -1;

    bool operator== (const NodeRef& o) const
    {
        return kind == o.kind && mode == o.mode && a == o.a && b == o.b;
    }
};

class ModelTree : public juce::Component,
                  public juce::FileDragAndDropTarget
{
public:
    std::function<void (NodeRef)> onSelect;

    // Structural actions (milestone 3) — the Studio implements these.
    std::function<void (NodeRef, const juce::StringArray&)> onImportFiles;   // group (or child) + audio files
    std::function<void (int modeIndex)> onAddGroup;
    std::function<void (NodeRef)> onRemoveNode;      // sample / group / effect / binding
    std::function<void (NodeRef)> onSpreadRanges;    // group: stretch zones + pitch-track
    std::function<void (NodeRef, const juce::String& type)> onAddEffect;   // mode or group + type
    std::function<void (NodeRef)> onAddBinding;      // control → new default binding
    std::function<void (NodeRef, const juce::String& kind)> onAddWidget;    // uiRoot + knob/button/image

    ModelTree()
    {
        tree.setRootItemVisible (false);
        tree.setDefaultOpenness (false);
        tree.setColour (juce::TreeView::backgroundColourId, juce::Colour (0xff1e1f20));
        addAndMakeVisible (tree);
    }

    ~ModelTree() override { tree.setRootItem (nullptr); }

    void resized() override { tree.setBounds (getLocalBounds()); }

    // ── audio-file drag-drop (onto a group, or a sample inside one) ─────────
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
                || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac"))
                return true;
        return false;
    }

    void filesDropped (const juce::StringArray& files, int x, int y) override
    {
        if (! onImportFiles)
            return;
        juce::ignoreUnused (x);
        NodeRef target;
        if (auto* item = dynamic_cast<Item*> (tree.getItemAt (y)))
            target = item->ref;
        onImportFiles (target, files);
    }

    /** Rebuild from the model, keeping the previous selection when it still resolves. */
    void rebuild (const dm::PresetLibrary& lib)
    {
        const auto keep = selected;
        std::unique_ptr<juce::XmlElement> openness;
        if (tree.getRootItem() != nullptr)
            openness = tree.getOpennessState (true);   // keep expanded levels across rebuilds
        tree.setRootItem (nullptr);
        root = std::make_unique<Item> (this, NodeRef { NodeRef::Kind::library }, lib.library);

        for (int mi = 0; mi < lib.modes.size(); ++mi)
        {
            const auto& m = lib.modes.getReference (mi);
            auto* modeItem = new Item (this, { NodeRef::Kind::mode, mi }, m.name);
            root->addSubItem (modeItem);

            modeItem->addSubItem (new Item (this, { NodeRef::Kind::amp, mi }, "Amp envelope"));

            auto* groups = new Item (this, { NodeRef::Kind::none }, "Groups (" + juce::String (m.groups.size()) + ")");
            modeItem->addSubItem (groups);
            for (int gi = 0; gi < m.groups.size(); ++gi)
            {
                const auto& g = m.groups.getReference (gi);
                auto name = g.tags.isEmpty() ? "group " + juce::String (gi)
                                             : g.tags.joinIntoString (", ");
                auto* groupItem = new Item (this, { NodeRef::Kind::group, mi, gi }, name);
                groups->addSubItem (groupItem);
                for (int si = 0; si < g.samples.size(); ++si)
                    groupItem->addSubItem (new Item (this, { NodeRef::Kind::sample, mi, gi, si },
                                                     sampleName (g.samples.getReference (si))));
                for (int ei = 0; ei < g.effects.size(); ++ei)
                    groupItem->addSubItem (new Item (this, { NodeRef::Kind::groupEffect, mi, gi, ei },
                                                     "fx: " + g.effects.getReference (ei).type));
            }

            if (! m.effects.isEmpty())
            {
                auto* fx = new Item (this, { NodeRef::Kind::none }, "Effects (" + juce::String (m.effects.size()) + ")");
                modeItem->addSubItem (fx);
                for (int ei = 0; ei < m.effects.size(); ++ei)
                {
                    const auto& e = m.effects.getReference (ei);
                    fx->addSubItem (new Item (this, { NodeRef::Kind::effect, mi, ei },
                                              e.type + (e.id.isNotEmpty() ? "  [" + e.id + "]" : juce::String())));
                }
            }

            if (! m.sequences.isEmpty())
                modeItem->addSubItem (new Item (this, { NodeRef::Kind::sequences, mi },
                                                "Sequences (" + juce::String (m.sequences.size()) + ")"));

            auto* uiItem = new Item (this, { NodeRef::Kind::uiRoot, mi }, "UI");
            modeItem->addSubItem (uiItem);
            if (! m.ui.tabs.isEmpty())
            {
                const auto& tab = m.ui.tabs.getReference (0);
                for (int i = 0; i < tab.controls.size(); ++i)
                {
                    const auto& c = tab.controls.getReference (i);
                    auto* ctlItem = new Item (this, { NodeRef::Kind::control, mi, i },
                                              "knob: " + c.label);
                    uiItem->addSubItem (ctlItem);
                    for (int bi = 0; bi < c.bindings.size(); ++bi)
                        ctlItem->addSubItem (new Item (this, { NodeRef::Kind::controlBinding, mi, i, bi },
                                                       vocab::describeBinding (c.bindings.getReference (bi))));
                }
                for (int i = 0; i < tab.buttons.size(); ++i)
                    uiItem->addSubItem (new Item (this, { NodeRef::Kind::button, mi, i },
                                                  "button: " + buttonName (tab.buttons.getReference (i), i)));
                for (int i = 0; i < tab.menus.size(); ++i)
                    uiItem->addSubItem (new Item (this, { NodeRef::Kind::menu, mi, i },
                                                  "menu " + juce::String (i)));
                for (int i = 0; i < tab.images.size(); ++i)
                    uiItem->addSubItem (new Item (this, { NodeRef::Kind::image, mi, i },
                                                  "image: " + tab.images.getReference (i).image));
            }
        }

        tree.setRootItem (root.get());
        if (openness != nullptr)
            tree.restoreOpennessState (*openness, false);
        else if (auto* first = root->getSubItem (0))
            first->setOpen (true);   // first build: open the first mode

        selected = {};
        if (keep.kind != NodeRef::Kind::none)
            selectRef (*root, keep);
    }

private:
    juce::String sampleName (const dm::Sample& s) const
    {
        auto n = s.source.fromLastOccurrenceOf (":", false, false);
        if (s.loNote == s.hiNote) return n + "  (" + juce::String (s.loNote) + ")";
        return n + "  (" + juce::String (s.loNote) + "-" + juce::String (s.hiNote) + ")";
    }

    juce::String buttonName (const dm::Button& b, int index) const
    {
        if (b.id.isNotEmpty() && ! b.id.startsWith ("btn_")) return b.id;
        if (! b.states.isEmpty() && b.states.getReference (0).name.isNotEmpty())
            return b.states.getReference (0).name;
        return juce::String (index);
    }

    struct Item : juce::TreeViewItem
    {
        Item (ModelTree* o, NodeRef r, const juce::String& t) : owner (o), ref (r), text (t) {}

        bool mightContainSubItems() override { return getNumSubItems() > 0; }
        bool canBeSelected() const override  { return ref.kind != NodeRef::Kind::none; }
        int getItemHeight() const override   { return 20; }

        void paintItem (juce::Graphics& g, int w, int h) override
        {
            if (isSelected())
                g.fillAll (juce::Colour (0xff33506b));
            g.setColour (juce::Colour (0xffd8d9da));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawText (text, 4, 0, w - 4, h, juce::Justification::centredLeft);
        }

        juce::String getUniqueName() const override
        {
            // Openness/selection state is stored by unique-name path — key it on the
            // node's TEXT so it survives index shifts from structural edits.
            return juce::String ((int) ref.kind) + ":" + text;
        }

        void itemSelectionChanged (bool nowSelected) override
        {
            if (nowSelected && owner != nullptr)
            {
                owner->selected = ref;
                if (owner->onSelect)
                    owner->onSelect (ref);
            }
        }

        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            const bool audioFiles = [&files]
            {
                for (const auto& f : files)
                    if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif")
                        || f.endsWithIgnoreCase (".aiff") || f.endsWithIgnoreCase (".flac"))
                        return true;
                return false;
            }();
            return audioFiles && (ref.kind == NodeRef::Kind::group
                                  || ref.kind == NodeRef::Kind::sample
                                  || ref.kind == NodeRef::Kind::mode);
        }

        void filesDropped (const juce::StringArray& files, int) override
        {
            if (owner != nullptr && owner->onImportFiles)
                owner->onImportFiles (ref, files);
        }

        void itemClicked (const juce::MouseEvent& e) override
        {
            if (! e.mods.isPopupMenu() || owner == nullptr)
                return;
            juce::PopupMenu m;
            const auto k = ref.kind;
            if (k == NodeRef::Kind::group)
            {
                m.addItem (1, "Import samples...");
                m.addItem (4, "Auto-spread ranges (pitch-track the gaps)");
                juce::PopupMenu fx;
                const auto types = vocab::effectTypes();
                for (int i = 0; i < types.size(); ++i)
                    fx.addItem (100 + i, types[i]);
                m.addSubMenu ("Add effect", fx);
                m.addSeparator();
                m.addItem (3, "Remove group");
            }
            else if (k == NodeRef::Kind::mode)
            {
                m.addItem (2, "Add group");
                juce::PopupMenu fx;
                const auto types = vocab::effectTypes();
                for (int i = 0; i < types.size(); ++i)
                    fx.addItem (100 + i, types[i]);
                m.addSubMenu ("Add effect", fx);
            }
            else if (k == NodeRef::Kind::sample)
            {
                m.addItem (3, "Remove sample");
            }
            else if (k == NodeRef::Kind::effect || k == NodeRef::Kind::groupEffect)
            {
                m.addItem (3, "Remove effect");
            }
            else if (k == NodeRef::Kind::control)
            {
                m.addItem (5, "Add binding");
            }
            else if (k == NodeRef::Kind::controlBinding)
            {
                m.addItem (3, "Remove binding");
            }
            else if (k == NodeRef::Kind::uiRoot)
            {
                m.addItem (10, "Add knob (filmstrip image)...");
                m.addItem (11, "Add button (state images)...");
                m.addItem (12, "Add image...");
            }
            else if (k == NodeRef::Kind::button || k == NodeRef::Kind::menu
                     || k == NodeRef::Kind::image)
            {
                m.addItem (3, "Remove");
            }
            else
                return;

            auto* o = owner;
            const auto r = ref;
            m.showMenuAsync ({}, [o, r] (int result)
            {
                if (o == nullptr) return;
                if (result == 1 && o->onImportFiles)
                {
                    auto chooser = std::make_shared<juce::FileChooser> (
                        "Import samples", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
                    chooser->launchAsync (juce::FileBrowserComponent::openMode
                                          | juce::FileBrowserComponent::canSelectFiles
                                          | juce::FileBrowserComponent::canSelectMultipleItems,
                                          [o, r, chooser] (const juce::FileChooser& fc)
                    {
                        juce::StringArray paths;
                        for (const auto& f : fc.getResults())
                            paths.add (f.getFullPathName());
                        if (! paths.isEmpty() && o->onImportFiles)
                            o->onImportFiles (r, paths);
                    });
                }
                else if (result == 2 && o->onAddGroup)      o->onAddGroup (r.mode);
                else if (result == 3 && o->onRemoveNode)    o->onRemoveNode (r);
                else if (result == 4 && o->onSpreadRanges)  o->onSpreadRanges (r);
                else if (result == 5 && o->onAddBinding)    o->onAddBinding (r);
                else if (result == 10 && o->onAddWidget) o->onAddWidget (r, "knob");
                else if (result == 11 && o->onAddWidget) o->onAddWidget (r, "button");
                else if (result == 12 && o->onAddWidget) o->onAddWidget (r, "image");
                else if (result >= 100 && o->onAddEffect)
                    o->onAddEffect (r, vocab::effectTypes()[result - 100]);
            });
        }

        ModelTree* owner;
        NodeRef ref;
        juce::String text;
    };

    void selectRef (Item& item, const NodeRef& ref)
    {
        if (item.ref == ref)
        {
            item.setSelected (true, true);
            return;
        }
        for (int i = 0; i < item.getNumSubItems(); ++i)
            if (auto* sub = dynamic_cast<Item*> (item.getSubItem (i)))
                selectRef (*sub, ref);
    }

    juce::TreeView tree;
    std::unique_ptr<Item> root;
    NodeRef selected;
};

} // namespace dmse_studio
