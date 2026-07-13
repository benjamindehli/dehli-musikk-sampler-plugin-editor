#pragma once

// Sample import helpers: lossless WAV/AIFF → FLAC transcode into the repo's
// assets/samples/ (the engine's loose-FLAC fallback picks new files up on the
// next hot reload), note parsing from file names, and optional range spreading.
//
// House rule (the user hand-edits samples): NEVER trim, pad or resample — write
// exactly the frames the reader reports.

#include <model/Manifest.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <optional>

namespace dmse_studio
{

struct ImportedSample
{
    juce::String stem;        // FLAC file stem → sample id "flac:<stem>"
    juce::int64 frames = 0;
    double sampleRate = 0.0;
};

/** Losslessly transcode one audio file to `<destDir>/<stem>.flac`. The stem is
    uniquified only against stems the MODEL still references (`stemInUse`) — a
    stale file from an earlier/undone import is simply overwritten, never dodged
    (dodging created junk names like "..._02_3" that then mis-mapped). */
inline std::optional<ImportedSample> transcodeToFlac (const juce::File& source,
                                                      const juce::File& destDir,
                                                      const std::function<bool (const juce::String&)>& stemInUse,
                                                      juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (source));
    if (reader == nullptr)
    {
        error = "cannot read " + source.getFullPathName();
        return std::nullopt;
    }

    destDir.createDirectory();
    auto stem = source.getFileNameWithoutExtension();
    for (int n = 2; stemInUse && stemInUse (stem); ++n)
        stem = source.getFileNameWithoutExtension() + "_" + juce::String (n);
    auto out = destDir.getChildFile (stem + ".flac");
    out.deleteFile();   // stale orphan from an earlier attempt → overwrite

    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
    {
        error = "cannot write " + out.getFullPathName();
        return std::nullopt;
    }

    int bits = (int) reader->bitsPerSample;
    if (bits != 16 && bits != 24)
        bits = 24;   // float / odd depths stored as 24-bit FLAC (audio unchanged in range)

    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        flac.createWriterFor (stream.get(), reader->sampleRate, reader->numChannels, bits, {}, 0));
    if (writer == nullptr)
    {
        error = "cannot create FLAC writer for " + out.getFullPathName();
        return std::nullopt;
    }
    stream.release();   // writer owns the stream now

    const bool ok = writer->writeFromAudioReader (*reader, 0, reader->lengthInSamples);
    writer.reset();     // flush + close
    if (! ok)
    {
        error = "FLAC encode failed for " + out.getFullPathName();
        out.deleteFile();
        return std::nullopt;
    }
    return ImportedSample { stem, reader->lengthInSamples, reader->sampleRate };
}

struct ParsedSampleName
{
    std::optional<int> note;         // MIDI note
    std::optional<int> roundRobin;   // 1-based round-robin slot (seqPosition)
};

/** Parse "{name}_{note}[_{rr}]" stems: a trailing digits token is the round-robin
    slot when the token before it parses as a note (e.g. "Line_Medium_3B_02" → note
    3B = 71, slot 2); otherwise trailing digits are a plain MIDI note. */
inline ParsedSampleName parseSampleName (const juce::String& stem);

/** Parse a MIDI note from the end of a file stem. Accepts note names in the
    C-2 = 0 convention used across the libraries ("Bass_1C" → 36, "Pad_A#3" → 70,
    also "Db2" flats) or a plain trailing MIDI number ("060", "36"). */
inline std::optional<int> parseNoteFromName (const juce::String& stem)
{
    // Note-name form: optional octave BEFORE the letter (the house "1C" style) or
    // after it ("C1", "A#3", "Db-1").
    static const int semis[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G

    auto tail = stem.fromLastOccurrenceOf ("_", false, false);
    if (tail.isEmpty()) tail = stem;

    auto tryNoteName = [] (const juce::String& t) -> std::optional<int>
    {
        // <octave><letter><#|b>  e.g. "1C", "3A#"
        for (int i = 0; i < t.length(); ++i)
        {
            const auto c = (juce::juce_wchar) juce::CharacterFunctions::toUpperCase (t[i]);
            if (c >= 'A' && c <= 'G')
            {
                const auto octText = t.substring (0, i);
                auto rest = t.substring (i + 1);
                int semi = semis[c - 'A'];
                if (rest.startsWith ("#")) { ++semi; rest = rest.substring (1); }
                else if (rest.startsWithIgnoreCase ("b")) { --semi; rest = rest.substring (1); }

                juce::String octAfter = rest;
                const auto oct = octText.isNotEmpty() ? octText : octAfter;
                if (oct.isEmpty() || ! oct.retainCharacters ("-0123456789").equalsIgnoreCase (oct))
                    return std::nullopt;
                const int midi = (oct.getIntValue() + 2) * 12 + semi;   // C-2 = 0
                if (midi >= 0 && midi <= 127)
                    return midi;
                return std::nullopt;
            }
            if (! juce::CharacterFunctions::isDigit (t[i]) && t[i] != '-')
                return std::nullopt;   // leading chunk must be the octave digits
        }
        return std::nullopt;
    };

    if (auto n = tryNoteName (tail))
        return n;

    // Plain trailing number = MIDI note.
    const auto digits = tail.retainCharacters ("0123456789");
    if (digits == tail && digits.isNotEmpty())
    {
        const int midi = digits.getIntValue();
        if (midi >= 0 && midi <= 127)
            return midi;
    }
    return std::nullopt;
}

inline ParsedSampleName parseSampleName (const juce::String& stem)
{
    juce::StringArray tokens;
    tokens.addTokens (stem, "_", {});
    tokens.removeEmptyStrings();

    // Rightmost token that parses as a note NAME (must contain a letter — a plain
    // number is never treated as the name here) carries the note; the digits token
    // right after it, if any, is the round-robin slot. Anything further is junk
    // (e.g. uniquifier suffixes) and is ignored.
    for (int i = tokens.size() - 1; i >= 0; --i)
    {
        if (! tokens[i].containsAnyOf ("ABCDEFGabcdefg"))
            continue;
        if (const auto note = parseNoteFromName (tokens[i]); note.has_value())
        {
            std::optional<int> rr;
            if (i + 1 < tokens.size())
            {
                const auto& next = tokens[i + 1];
                if (next.isNotEmpty() && next.retainCharacters ("0123456789") == next)
                    rr = juce::jmax (1, next.getIntValue());
            }
            return { note, rr };
        }
    }

    // No note-name token: a plain trailing MIDI number still counts as the note.
    return { parseNoteFromName (stem), std::nullopt };
}

/** Spread a group's per-note zones to cover the gaps between them: each sample's
    range extends to the midpoint towards its neighbours (first stretches down to 0,
    last up to 127) and pitch-key-tracking is enabled so stretched keys re-pitch.
    Explicit user action — never applied automatically. */
inline void spreadRanges (dm::Group& group)
{
    if (group.samples.isEmpty())
        return;

    // Spread by DISTINCT ROOT NOTES: round-robin variants share a root and must all
    // get the same range (splitting between equal roots inverted the zones).
    juce::Array<int> roots;
    for (const auto& s : group.samples)
        roots.addIfNotAlreadyThere (s.rootNote);
    std::sort (roots.begin(), roots.end());

    for (auto& s : group.samples)
    {
        const int i = roots.indexOf (s.rootNote);
        s.loNote = i == 0 ? 0 : (roots[i - 1] + roots[i]) / 2 + 1;
        s.hiNote = i == roots.size() - 1 ? 127 : (roots[i] + roots[i + 1]) / 2;
        s.pitchKeyTrack = true;
    }
}

} // namespace dmse_studio
