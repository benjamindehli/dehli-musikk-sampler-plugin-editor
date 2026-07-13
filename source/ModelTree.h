#pragma once

// Tree view over the manifest model. Items reference nodes by INDEX PATH
// (NodeRef), never by pointer — every committed edit and every undo replaces the
// model's arrays, so pointers into it don't survive. The tree is rebuilt after
// structural changes; selection is preserved by re-resolving the NodeRef.

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

class ModelTree : public juce::Component
{
public:
    std::function<void (NodeRef)> onSelect;

    ModelTree()
    {
        tree.setRootItemVisible (false);
        tree.setDefaultOpenness (false);
        tree.setColour (juce::TreeView::backgroundColourId, juce::Colour (0xff1e1f20));
        addAndMakeVisible (tree);
    }

    ~ModelTree() override { tree.setRootItem (nullptr); }

    void resized() override { tree.setBounds (getLocalBounds()); }

    /** Rebuild from the model, keeping the previous selection when it still resolves. */
    void rebuild (const dm::PresetLibrary& lib)
    {
        const auto keep = selected;
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
                    uiItem->addSubItem (new Item (this, { NodeRef::Kind::control, mi, i },
                                                  "knob: " + tab.controls.getReference (i).label));
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
        if (auto* first = root->getSubItem (0))
            first->setOpen (true);

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

        void itemSelectionChanged (bool nowSelected) override
        {
            if (nowSelected && owner != nullptr)
            {
                owner->selected = ref;
                if (owner->onSelect)
                    owner->onSelect (ref);
            }
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
