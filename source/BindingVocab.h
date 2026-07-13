#pragma once

// The engine's binding + effect vocabulary, as data the Studio's dropdowns are
// built from. Kept in ONE place and matching the engine's compiled routes
// (params/ManifestParameters.cpp compileRoute + FxChain kinds) — offering only
// what the engine actually honours.

#include <model/Manifest.h>
#include <juce_core/juce_core.h>

namespace dmse_studio::vocab
{

/** What a binding points at — determines both the target dropdown and the
    parameter list. */
enum class Target { instrument, group, tag, effect, modulator };

inline juce::StringArray targetNames()
{
    return { "Instrument", "Group", "Tag", "Effect", "Modulator" };
}

inline juce::StringArray paramsFor (Target t)
{
    switch (t)
    {
        case Target::instrument:
            return { "AMP_VOLUME", "ENV_ATTACK", "ENV_DECAY", "ENV_SUSTAIN", "ENV_RELEASE",
                     "ENV_ATTACK_CURVE", "ENV_DECAY_CURVE", "ENV_RELEASE_CURVE",
                     "AMP_VEL_TRACK", "SEQ_PLAYBACK_RATE" };
        case Target::group:
            return { "AMP_VOLUME", "ENV_ATTACK", "ENV_DECAY", "ENV_SUSTAIN", "ENV_RELEASE",
                     "PAN", "GROUP_TUNING", "AMP_VEL_TRACK", "ENABLED" };
        case Target::tag:
            return { "TAG_VOLUME", "TAG_ENABLED" };
        case Target::effect:
            return { "FX_MIX", "FX_FILTER_FREQUENCY", "FX_FILTER_RESONANCE", "FX_DRIVE",
                     "FX_OUTPUT_LEVEL", "LEVEL", "FX_MOD_RATE", "FX_MOD_DEPTH",
                     "FX_FEEDBACK", "ENABLED" };
        case Target::modulator:
            return { "MOD_AMOUNT", "FREQUENCY" };
    }
    return {};
}

/** Classify an existing binding for the editor (mirrors how the engine routes). */
inline Target targetOf (const dm::Binding& b)
{
    if (b.type == "modulator")   return Target::modulator;
    if (b.level == "tag" || b.identifier.isNotEmpty()) return Target::tag;
    if (b.type == "effect" || b.parameter.startsWith ("FX_")
        || b.effectIndex.has_value())                  return Target::effect;
    if (b.level == "group" || b.groupIndex.has_value()) return Target::group;
    return Target::instrument;
}

/** Effect types the engine renders (manifest `type` strings). */
inline juce::StringArray effectTypes()
{
    return { "lowpass", "highpass", "gain", "wave_shaper", "convolution", "chorus", "phaser" };
}

/** A new effect of `type` with sensible neutral defaults. `id` must be unique
    within the mode (the caller uniquifies). Convolution needs `ir` set afterwards. */
inline dm::Effect makeEffect (const juce::String& type, const juce::String& id)
{
    dm::Effect e;
    e.type = type;
    e.id = id;
    e.enabled = true;
    if (type == "lowpass")          { e.frequency = 20000.0; e.resonance = 0.707; }
    else if (type == "highpass")    { e.frequency = 20.0; e.resonance = 0.707; }
    else if (type == "gain")        { e.gain = 0.0; }
    else if (type == "wave_shaper") { e.drive = 1.0; e.outputLevel = 1.0; }
    else if (type == "convolution") { e.mix = 0.5; }
    else if (type == "chorus")      { e.rate = 1.0; e.depth = 0.2; e.feedback = 0.0; e.mix = 0.5; }
    else if (type == "phaser")      { e.rate = 0.5; e.depth = 0.5; e.feedback = 0.0; e.mix = 0.5; }
    return e;
}

/** Short human description of a binding for the tree ("AMP_VOLUME @ grp_a"). */
inline juce::String describeBinding (const dm::Binding& b)
{
    juce::String target;
    switch (targetOf (b))
    {
        case Target::instrument: target = "instrument"; break;
        case Target::group:      target = b.targetId.isNotEmpty() ? b.targetId
                                        : "group " + juce::String (b.groupIndex.value_or (0)); break;
        case Target::tag:        target = "tag " + b.identifier; break;
        case Target::effect:     target = b.targetId.isNotEmpty() ? b.targetId
                                        : "fx " + juce::String (b.effectIndex.value_or (0)); break;
        case Target::modulator:  target = b.targetId.isNotEmpty() ? b.targetId
                                        : "mod " + juce::String (b.position.value_or (0)); break;
    }
    return b.parameter + " @ " + target;
}

} // namespace dmse_studio::vocab
