#pragma once

// Property inspector for the node selected in the ModelTree. Builds a
// juce::PropertyPanel from small lambda-backed property components; every setter
// runs through beginEdit (undo snapshot) → mutate → commitEdit (dirty + hot
// reload), provided by the Studio. The inspector re-resolves the model node by
// NodeRef on EVERY access — the model's arrays are replaced by commits and undo.

#include "ModelTree.h"
#include "BindingVocab.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace dmse_studio
{

class Inspector : public juce::Component
{
public:
    /** The Studio provides model access + the edit transaction hooks. */
    std::function<dm::PresetLibrary&()> getModel;
    std::function<void()> beginEdit;    // undo snapshot
    std::function<void()> commitEdit;   // dirty + hot reload (+ tree/inspector refresh)

    Inspector()
    {
        title.setColour (juce::Label::textColourId, juce::Colour (0xfffdfeff));
        title.setFont (juce::Font (juce::FontOptions (14.0f).withStyle ("Bold")));
        addAndMakeVisible (title);
        addAndMakeVisible (panel);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds (r.removeFromTop (26).reduced (6, 2));
        panel.setBounds (r);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff1e1f20)); }

    /** Show the properties of `ref` (call again after commits to refresh values). */
    void show (NodeRef ref)
    {
        current = ref;
        panel.clear();
        juce::Array<juce::PropertyComponent*> props;

        switch (ref.kind)
        {
            case NodeRef::Kind::library:
            {
                title.setText ("Library", juce::dontSendNotification);
                props.add (text ("Name", [this] { return lib().library; },
                                 [this] (const juce::String& v) { lib().library = v; }));
                props.add (number ("Gain (dB)", [this] { return juce::String (lib().gainDb); },
                                   [this] (double v) { lib().gainDb = v; }));
                props.add (boolean ("Poly-save default", [this] { return lib().polySaveDefault; },
                                    [this] (bool v) { lib().polySaveDefault = v; }));
                break;
            }
            case NodeRef::Kind::mode:
            {
                title.setText ("Mode", juce::dontSendNotification);
                if (mode (ref) != nullptr)
                    props.add (text ("Name", [this, ref] { return mode (ref)->name; },
                                     [this, ref] (const juce::String& v) { mode (ref)->name = v; }));
                break;
            }
            case NodeRef::Kind::amp:
            {
                title.setText ("Amp envelope", juce::dontSendNotification);
                if (mode (ref) != nullptr)
                {
                    auto field = [this, ref] (const char* name, double dm::AmpEnvelope::* member)
                    {
                        return number (name,
                            [this, ref, member] { return juce::String (mode (ref)->amp.*member); },
                            [this, ref, member] (double v) { mode (ref)->amp.*member = v; });
                    };
                    props.add (field ("Attack (s)",  &dm::AmpEnvelope::attack));
                    props.add (field ("Decay (s)",   &dm::AmpEnvelope::decay));
                    props.add (field ("Sustain",     &dm::AmpEnvelope::sustain));
                    props.add (field ("Release (s)", &dm::AmpEnvelope::release));
                    props.add (field ("Volume",      &dm::AmpEnvelope::volume));
                    props.add (field ("Vel track",   &dm::AmpEnvelope::velTrack));
                }
                break;
            }
            case NodeRef::Kind::group:
            {
                title.setText ("Group", juce::dontSendNotification);
                if (group (ref) != nullptr)
                {
                    props.add (text ("Tags (comma-separated)",
                        [this, ref] { return group (ref)->tags.joinIntoString (", "); },
                        [this, ref] (const juce::String& v)
                        {
                            auto& g = *group (ref);
                            g.tags.clear();
                            for (auto t : juce::StringArray::fromTokens (v, ",", {}))
                                if (t.trim().isNotEmpty()) g.tags.add (t.trim());
                        }));
                    props.add (choice ("Trigger", { "attack", "release" },
                        [this, ref] { return group (ref)->trigger == "release" ? 1 : 0; },
                        [this, ref] (int i) { group (ref)->trigger = i == 1 ? "release" : "attack"; }));
                    props.add (optNumber ("Volume (empty = mode)",
                        [this, ref] { return group (ref)->volume; },
                        [this, ref] (std::optional<double> v) { group (ref)->volume = v; }));
                    props.add (optNumber ("Attack (s, empty = mode)",
                        [this, ref] { return group (ref)->attack; },
                        [this, ref] (std::optional<double> v) { group (ref)->attack = v; }));
                    props.add (optNumber ("Decay (s, empty = mode)",
                        [this, ref] { return group (ref)->decay; },
                        [this, ref] (std::optional<double> v) { group (ref)->decay = v; }));
                    props.add (optNumber ("Sustain (empty = mode)",
                        [this, ref] { return group (ref)->sustain; },
                        [this, ref] (std::optional<double> v) { group (ref)->sustain = v; }));
                    props.add (optNumber ("Release (s, empty = mode)",
                        [this, ref] { return group (ref)->release; },
                        [this, ref] (std::optional<double> v) { group (ref)->release = v; }));
                    props.add (optNumber ("Vel track (empty = mode)",
                        [this, ref] { return group (ref)->velTrack; },
                        [this, ref] (std::optional<double> v) { group (ref)->velTrack = v; }));

                    // Velocity layer range; 0..127 (the defaults) = no range stored.
                    auto velField = [this, ref] (const char* name, int dm::VelocityRange::* member)
                    {
                        return intNumber (name,
                            [this, ref, member]
                            { return group (ref)->velocity.value_or (dm::VelocityRange{}).*member; },
                            [this, ref, member] (int v)
                            {
                                auto& g = *group (ref);
                                auto range = g.velocity.value_or (dm::VelocityRange{});
                                range.*member = juce::jlimit (0, 127, v);
                                g.velocity = (range.lo == 0 && range.hi == 127)
                                                 ? std::nullopt : std::optional (range);
                            });
                    };
                    props.add (velField ("Velocity low",  &dm::VelocityRange::lo));
                    props.add (velField ("Velocity high", &dm::VelocityRange::hi));
                }
                break;
            }
            case NodeRef::Kind::sample:
            {
                title.setText ("Sample", juce::dontSendNotification);
                if (auto* s = sample (ref))
                {
                    props.add (readOnly ("Source", s->source));
                    props.add (intNumber ("Low note",  [this, ref] { return sample (ref)->loNote; },
                                          [this, ref] (int v) { sample (ref)->loNote = v; }));
                    props.add (intNumber ("High note", [this, ref] { return sample (ref)->hiNote; },
                                          [this, ref] (int v) { sample (ref)->hiNote = v; }));
                    props.add (intNumber ("Root note", [this, ref] { return sample (ref)->rootNote; },
                                          [this, ref] (int v) { sample (ref)->rootNote = v; }));
                    props.add (boolean ("Pitch key track", [this, ref] { return sample (ref)->pitchKeyTrack; },
                                        [this, ref] (bool v) { sample (ref)->pitchKeyTrack = v; }));
                    props.add (optNumber ("Volume (empty = 1)",
                        [this, ref] { return sample (ref)->volume; },
                        [this, ref] (std::optional<double> v) { sample (ref)->volume = v; }));
                    props.add (optInt ("Start (frames, empty = 0)",
                        [this, ref] { return sample (ref)->start; },
                        [this, ref] (std::optional<int> v) { sample (ref)->start = v; }));
                    props.add (optInt ("End (frames, empty = full)",
                        [this, ref] { return sample (ref)->end; },
                        [this, ref] (std::optional<int> v) { sample (ref)->end = v; }));
                    props.add (boolean ("Loop enabled", [this, ref] { return sample (ref)->loop.enabled; },
                                        [this, ref] (bool v) { sample (ref)->loop.enabled = v; }));
                    props.add (optInt ("Loop start",
                        [this, ref] { return sample (ref)->loop.start; },
                        [this, ref] (std::optional<int> v) { sample (ref)->loop.start = v; }));
                    props.add (optInt ("Loop end",
                        [this, ref] { return sample (ref)->loop.end; },
                        [this, ref] (std::optional<int> v) { sample (ref)->loop.end = v; }));
                    props.add (optInt ("Loop crossfade",
                        [this, ref] { return sample (ref)->loop.crossfade; },
                        [this, ref] (std::optional<int> v) { sample (ref)->loop.crossfade = v; }));
                }
                break;
            }
            case NodeRef::Kind::effect:
            case NodeRef::Kind::groupEffect:
            {
                title.setText (ref.kind == NodeRef::Kind::effect ? "Effect" : "Group effect",
                               juce::dontSendNotification);
                if (auto* e = effect (ref))
                {
                    props.add (readOnly ("Type", e->type + (e->ir.isNotEmpty() ? "  (IR: " + e->ir + ")" : juce::String())));
                    props.add (boolean ("Enabled", [this, ref] { return effect (ref)->enabled; },
                                        [this, ref] (bool v) { effect (ref)->enabled = v; }));
                    auto opt = [this, ref, &props] (const char* name, std::optional<double> dm::Effect::* member)
                    {
                        props.add (optNumber (name,
                            [this, ref, member] { return effect (ref)->*member; },
                            [this, ref, member] (std::optional<double> v) { effect (ref)->*member = v; }));
                    };
                    opt ("Frequency (Hz)", &dm::Effect::frequency);
                    opt ("Resonance",      &dm::Effect::resonance);
                    opt ("Gain (dB)",      &dm::Effect::gain);
                    opt ("Drive",          &dm::Effect::drive);
                    opt ("Mix",            &dm::Effect::mix);
                    opt ("Wet",            &dm::Effect::wet);
                    opt ("Output level",   &dm::Effect::outputLevel);
                    opt ("Rate (Hz)",      &dm::Effect::rate);
                    opt ("Depth",          &dm::Effect::depth);
                    opt ("Feedback",       &dm::Effect::feedback);
                }
                break;
            }
            case NodeRef::Kind::uiRoot:
            {
                title.setText ("UI", juce::dontSendNotification);
                if (auto* m = mode (ref))
                {
                    props.add (readOnly ("Background", m->ui.background));
                    props.add (intNumber ("Width",  [this, ref] { return mode (ref)->ui.width; },
                                          [this, ref] (int v) { mode (ref)->ui.width = v; }));
                    props.add (intNumber ("Height", [this, ref] { return mode (ref)->ui.height; },
                                          [this, ref] (int v) { mode (ref)->ui.height = v; }));
                    props.add (intNumber ("Crop top", [this, ref] { return mode (ref)->ui.cropTop; },
                                          [this, ref] (int v) { mode (ref)->ui.cropTop = v; }));
                }
                break;
            }
            case NodeRef::Kind::control:
            {
                title.setText ("Knob / fader", juce::dontSendNotification);
                if (control (ref) != nullptr)
                {
                    props.add (text ("Label", [this, ref] { return control (ref)->label; },
                                     [this, ref] (const juce::String& v) { control (ref)->label = v; }));
                    addRect (props, [this, ref] () -> dm::Rect& { return control (ref)->rect; });
                    props.add (optNumber ("Min", [this, ref] { return control (ref)->min; },
                                          [this, ref] (std::optional<double> v) { control (ref)->min = v; }));
                    props.add (optNumber ("Max", [this, ref] { return control (ref)->max; },
                                          [this, ref] (std::optional<double> v) { control (ref)->max = v; }));
                    props.add (optNumber ("Default value", [this, ref] { return control (ref)->value; },
                                          [this, ref] (std::optional<double> v) { control (ref)->value = v; }));
                    props.add (boolean ("Visible", [this, ref] { return control (ref)->visible; },
                                        [this, ref] (bool v) { control (ref)->visible = v; }));
                }
                break;
            }
            case NodeRef::Kind::button:
            {
                title.setText ("Button", juce::dontSendNotification);
                if (auto* b = button (ref))
                {
                    props.add (readOnly ("Id / states", b->id + "  (" + juce::String (b->states.size()) + " states)"));
                    addRect (props, [this, ref] () -> dm::Rect& { return button (ref)->rect; });
                    props.add (boolean ("Visible", [this, ref] { return button (ref)->visible; },
                                        [this, ref] (bool v) { button (ref)->visible = v; }));
                }
                break;
            }
            case NodeRef::Kind::menu:
            {
                title.setText ("Menu", juce::dontSendNotification);
                if (auto* m = menu (ref))
                {
                    props.add (readOnly ("Options", juce::String (m->options.size())));
                    addRect (props, [this, ref] () -> dm::Rect& { return menu (ref)->rect; });
                }
                break;
            }
            case NodeRef::Kind::image:
            {
                title.setText ("Image", juce::dontSendNotification);
                if (auto* im = image (ref))
                {
                    props.add (readOnly ("Asset", im->image));
                    addRect (props, [this, ref] () -> dm::Rect& { return image (ref)->rect; });
                    props.add (boolean ("Visible", [this, ref] { return image (ref)->visible; },
                                        [this, ref] (bool v) { image (ref)->visible = v; }));
                }
                break;
            }
            case NodeRef::Kind::controlBinding:
            {
                title.setText ("Binding", juce::dontSendNotification);
                if (auto* b = binding (ref))
                {
                    // Target kind first — it decides the other two dropdowns.
                    props.add (choice ("Target kind", vocab::targetNames(),
                        [this, ref] { return (int) vocab::targetOf (*binding (ref)); },
                        [this, ref] (int i)
                        {
                            auto& bd = *binding (ref);
                            const auto t = (vocab::Target) i;
                            // Reset the addressing fields for the new kind; the target
                            // and parameter dropdowns then take over.
                            bd.targetId.clear();
                            bd.identifier.clear();
                            bd.effectIndex.reset();
                            bd.groupIndex.reset();
                            bd.position.reset();
                            switch (t)
                            {
                                case vocab::Target::instrument: bd.type = "amp";       bd.level = "instrument"; break;
                                case vocab::Target::group:      bd.type = "amp";       bd.level = "group";      break;
                                case vocab::Target::tag:        bd.type = "amp";       bd.level = "tag";        break;
                                case vocab::Target::effect:     bd.type = "effect";    bd.level = "instrument"; break;
                                case vocab::Target::modulator:  bd.type = "modulator"; bd.level = "instrument"; break;
                            }
                            bd.parameter = vocab::paramsFor (t)[0];
                        }));

                    const auto kind = vocab::targetOf (*b);

                    // Target instance (not needed for instrument-level bindings).
                    if (kind == vocab::Target::group)
                    {
                        juce::StringArray names;
                        if (auto* m = mode (ref))
                            for (int i = 0; i < m->groups.size(); ++i)
                            {
                                const auto& g = m->groups.getReference (i);
                                names.add (juce::String (i) + ": "
                                           + (g.tags.isEmpty() ? g.uid : g.tags.joinIntoString (",")));
                            }
                        props.add (choice ("Group", names,
                            [this, ref] { return binding (ref)->groupIndex.value_or (0); },
                            [this, ref] (int i)
                            {
                                auto& bd = *binding (ref);
                                bd.groupIndex = i;
                                if (auto* m = mode (ref); m != nullptr && i < m->groups.size())
                                    bd.targetId = m->groups.getReference (i).uid;
                            }));
                    }
                    else if (kind == vocab::Target::effect)
                    {
                        juce::StringArray ids;
                        if (auto* m = mode (ref))
                            for (const auto& e : m->effects)
                                ids.add (e.id.isNotEmpty() ? e.id : e.type);
                        props.add (choice ("Effect", ids,
                            [this, ref] { return binding (ref)->effectIndex.value_or (0); },
                            [this, ref] (int i)
                            {
                                auto& bd = *binding (ref);
                                bd.effectIndex = i;
                                if (auto* m = mode (ref); m != nullptr && i < m->effects.size())
                                    bd.targetId = m->effects.getReference (i).id;
                            }));
                    }
                    else if (kind == vocab::Target::tag)
                    {
                        juce::StringArray tags;
                        if (auto* m = mode (ref))
                        {
                            for (const auto& t : m->tags)
                                tags.addIfNotAlreadyThere (t.name);
                            for (const auto& g : m->groups)
                                for (const auto& t : g.tags)
                                    tags.addIfNotAlreadyThere (t);
                        }
                        props.add (choice ("Tag", tags,
                            [this, ref, tags] { return juce::jmax (0, tags.indexOf (binding (ref)->identifier)); },
                            [this, ref, tags] (int i) { binding (ref)->identifier = tags[i]; }));
                    }
                    else if (kind == vocab::Target::modulator)
                    {
                        juce::StringArray ids;
                        if (auto* m = mode (ref))
                            for (int i = 0; i < m->modulators.size(); ++i)
                            {
                                const auto& lfo = m->modulators.getReference (i);
                                ids.add (lfo.id.isNotEmpty() ? lfo.id : "mod " + juce::String (i));
                            }
                        props.add (choice ("Modulator", ids,
                            [this, ref] { return binding (ref)->position.value_or (0); },
                            [this, ref] (int i)
                            {
                                auto& bd = *binding (ref);
                                bd.position = i;
                                if (auto* m = mode (ref); m != nullptr && i < m->modulators.size())
                                    bd.targetId = m->modulators.getReference (i).id;
                            }));
                    }

                    // Parameter, from the kind's vocabulary.
                    {
                        const auto params = vocab::paramsFor (kind);
                        props.add (choice ("Parameter", params,
                            [this, ref, params] { return juce::jmax (0, params.indexOf (binding (ref)->parameter)); },
                            [this, ref, params] (int i) { binding (ref)->parameter = params[i]; }));
                    }

                    props.add (optNumber ("Output min (control at min)",
                        [this, ref] { return binding (ref)->translationOutputMin; },
                        [this, ref] (std::optional<double> v)
                        {
                            auto& bd = *binding (ref);
                            bd.translationOutputMin = v;
                            bd.translation = (bd.translationOutputMin || bd.translationOutputMax)
                                                 ? "linear" : juce::String();
                        }));
                    props.add (optNumber ("Output max (control at max)",
                        [this, ref] { return binding (ref)->translationOutputMax; },
                        [this, ref] (std::optional<double> v)
                        {
                            auto& bd = *binding (ref);
                            bd.translationOutputMax = v;
                            bd.translation = (bd.translationOutputMin || bd.translationOutputMax)
                                                 ? "linear" : juce::String();
                        }));
                    props.add (boolean ("Reversed", [this, ref] { return binding (ref)->translationReversed; },
                                        [this, ref] (bool v) { binding (ref)->translationReversed = v; }));
                    props.add (optNumber ("Factor (empty = 1)",
                        [this, ref] { return binding (ref)->factor; },
                        [this, ref] (std::optional<double> v) { binding (ref)->factor = v; }));
                }
                break;
            }
            case NodeRef::Kind::sequences:
                title.setText ("Sequences", juce::dontSendNotification);
                props.add (readOnly ("Note", "Sequence editing arrives in a later milestone."));
                break;
            case NodeRef::Kind::none:
            default:
                title.setText ("", juce::dontSendNotification);
                break;
        }

        if (! props.isEmpty())
            panel.addProperties (props);
    }

    /** Refresh the current node's values (after commit/undo). */
    void refresh() { show (current); }

private:
    // ── model resolution by index path (bounds-checked, may return null) ─────
    dm::PresetLibrary& lib() { return getModel(); }

    dm::Mode* mode (NodeRef r)
    {
        auto& l = lib();
        return r.mode >= 0 && r.mode < l.modes.size() ? &l.modes.getReference (r.mode) : nullptr;
    }
    dm::Group* group (NodeRef r)
    {
        if (auto* m = mode (r))
            if (r.a >= 0 && r.a < m->groups.size())
                return &m->groups.getReference (r.a);
        return nullptr;
    }
    dm::Sample* sample (NodeRef r)
    {
        if (auto* g = group (r))
            if (r.b >= 0 && r.b < g->samples.size())
                return &g->samples.getReference (r.b);
        return nullptr;
    }
    dm::Effect* effect (NodeRef r)
    {
        if (r.kind == NodeRef::Kind::groupEffect)
        {
            if (auto* g = group (r))
                if (r.b >= 0 && r.b < g->effects.size())
                    return &g->effects.getReference (r.b);
            return nullptr;
        }
        if (auto* m = mode (r))
            if (r.a >= 0 && r.a < m->effects.size())
                return &m->effects.getReference (r.a);
        return nullptr;
    }
    dm::Control* control (NodeRef r)
    {
        if (auto* m = mode (r))
            if (! m->ui.tabs.isEmpty() && r.a >= 0 && r.a < m->ui.tabs.getReference (0).controls.size())
                return &m->ui.tabs.getReference (0).controls.getReference (r.a);
        return nullptr;
    }
    dm::Button* button (NodeRef r)
    {
        if (auto* m = mode (r))
            if (! m->ui.tabs.isEmpty() && r.a >= 0 && r.a < m->ui.tabs.getReference (0).buttons.size())
                return &m->ui.tabs.getReference (0).buttons.getReference (r.a);
        return nullptr;
    }
    dm::Menu* menu (NodeRef r)
    {
        if (auto* m = mode (r))
            if (! m->ui.tabs.isEmpty() && r.a >= 0 && r.a < m->ui.tabs.getReference (0).menus.size())
                return &m->ui.tabs.getReference (0).menus.getReference (r.a);
        return nullptr;
    }
    dm::Binding* binding (NodeRef r)
    {
        if (auto* m = mode (r))
            if (! m->ui.tabs.isEmpty() && r.a >= 0 && r.a < m->ui.tabs.getReference (0).controls.size())
            {
                auto& c = m->ui.tabs.getReference (0).controls.getReference (r.a);
                if (r.b >= 0 && r.b < c.bindings.size())
                    return &c.bindings.getReference (r.b);
            }
        return nullptr;
    }

    dm::UiImage* image (NodeRef r)
    {
        if (auto* m = mode (r))
            if (! m->ui.tabs.isEmpty() && r.a >= 0 && r.a < m->ui.tabs.getReference (0).images.size())
                return &m->ui.tabs.getReference (0).images.getReference (r.a);
        return nullptr;
    }

    // ── lambda-backed property components ───────────────────────────────────
    void applyMutation (std::function<void()> mutate)
    {
        if (beginEdit) beginEdit();
        mutate();
        // DEFER the commit: it rebuilds this panel (and the hosted processor), which
        // destroys the property component whose setter is still on the stack —
        // committing synchronously is a use-after-free when the call returns.
        juce::MessageManager::callAsync (
            [safe = juce::Component::SafePointer<Inspector> (this)]
            {
                if (safe != nullptr && safe->commitEdit)
                    safe->commitEdit();
            });
    }

    struct TextProp : juce::TextPropertyComponent
    {
        TextProp (const juce::String& name,
                  std::function<juce::String()> get, std::function<void (const juce::String&)> set,
                  Inspector& o)
            : juce::TextPropertyComponent (name, 512, false), getter (std::move (get)),
              setter (std::move (set)), owner (o) {}
        juce::String getText() const override { return getter(); }
        void setText (const juce::String& t) override
        {
            if (t == getter()) return;
            owner.applyMutation ([this, t] { setter (t); });
        }
        std::function<juce::String()> getter;
        std::function<void (const juce::String&)> setter;
        Inspector& owner;
    };

    struct BoolProp : juce::BooleanPropertyComponent
    {
        BoolProp (const juce::String& name, std::function<bool()> get, std::function<void (bool)> set,
                  Inspector& o)
            : juce::BooleanPropertyComponent (name, "on", "off"),
              getter (std::move (get)), setter (std::move (set)), owner (o) {}
        bool getState() const override { return getter(); }
        void setState (bool v) override
        {
            if (v == getter()) return;
            owner.applyMutation ([this, v] { setter (v); });
        }
        std::function<bool()> getter;
        std::function<void (bool)> setter;
        Inspector& owner;
    };

    struct ChoiceProp : juce::ChoicePropertyComponent
    {
        ChoiceProp (const juce::String& name, const juce::StringArray& options,
                    std::function<int()> get, std::function<void (int)> set, Inspector& o)
            : juce::ChoicePropertyComponent (name), getter (std::move (get)),
              setter (std::move (set)), owner (o)
        {
            choices = options;
        }
        int getIndex() const override { return getter(); }
        void setIndex (int i) override
        {
            if (i == getter()) return;
            owner.applyMutation ([this, i] { setter (i); });
        }
        std::function<int()> getter;
        std::function<void (int)> setter;
        Inspector& owner;
    };

    juce::PropertyComponent* text (const juce::String& name,
                                   std::function<juce::String()> get,
                                   std::function<void (const juce::String&)> set)
    {
        return new TextProp (name, std::move (get), std::move (set), *this);
    }

    juce::PropertyComponent* readOnly (const juce::String& name, const juce::String& value)
    {
        auto* p = new TextProp (name, [value] { return value; }, [] (const juce::String&) {}, *this);
        p->setEnabled (false);
        return p;
    }

    juce::PropertyComponent* number (const juce::String& name,
                                     std::function<juce::String()> get, std::function<void (double)> set)
    {
        return new TextProp (name, std::move (get),
                             [set = std::move (set)] (const juce::String& t) { set (t.getDoubleValue()); },
                             *this);
    }

    juce::PropertyComponent* intNumber (const juce::String& name,
                                        std::function<int()> get, std::function<void (int)> set)
    {
        return new TextProp (name,
                             [get = std::move (get)] { return juce::String (get()); },
                             [set = std::move (set)] (const juce::String& t) { set (t.getIntValue()); },
                             *this);
    }

    juce::PropertyComponent* optNumber (const juce::String& name,
                                        std::function<std::optional<double>()> get,
                                        std::function<void (std::optional<double>)> set)
    {
        return new TextProp (name,
            [get = std::move (get)] { auto v = get(); return v ? juce::String (*v) : juce::String(); },
            [set = std::move (set)] (const juce::String& t)
            { set (t.trim().isEmpty() ? std::nullopt : std::optional<double> (t.getDoubleValue())); },
            *this);
    }

    juce::PropertyComponent* optInt (const juce::String& name,
                                     std::function<std::optional<int>()> get,
                                     std::function<void (std::optional<int>)> set)
    {
        return new TextProp (name,
            [get = std::move (get)] { auto v = get(); return v ? juce::String (*v) : juce::String(); },
            [set = std::move (set)] (const juce::String& t)
            { set (t.trim().isEmpty() ? std::nullopt : std::optional<int> (t.getIntValue())); },
            *this);
    }

    juce::PropertyComponent* boolean (const juce::String& name,
                                      std::function<bool()> get, std::function<void (bool)> set)
    {
        return new BoolProp (name, std::move (get), std::move (set), *this);
    }

    juce::PropertyComponent* choice (const juce::String& name, const juce::StringArray& options,
                                     std::function<int()> get, std::function<void (int)> set)
    {
        return new ChoiceProp (name, options, std::move (get), std::move (set), *this);
    }

    void addRect (juce::Array<juce::PropertyComponent*>& props, std::function<dm::Rect&()> rect)
    {
        auto field = [this, rect] (const char* name, int dm::Rect::* member)
        {
            return intNumber (name,
                [rect, member] { return rect().*member; },
                [rect, member] (int v) { rect().*member = v; });
        };
        props.add (field ("X",      &dm::Rect::x));
        props.add (field ("Y",      &dm::Rect::y));
        props.add (field ("Width",  &dm::Rect::width));
        props.add (field ("Height", &dm::Rect::height));
    }

    juce::Label title;
    juce::PropertyPanel panel;
    NodeRef current;
};

} // namespace dmse_studio
