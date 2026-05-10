#include "XmlMultisampleImporters.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace audiocity::engine
{
using xml_multi::Diagnostic;
using xml_multi::addDiagnostic;
using xml_multi::loadSampleAssetFromFile;
using xml_multi::parseMidiNoteText;
using xml_multi::resolveSamplePath;
using xml_multi::buildGenericSummary;

namespace
{
[[nodiscard]] ZoneLoopMode parseLoopText(const juce::String& raw, ZoneLoopMode fallback = ZoneLoopMode::noLoop)
{
    const auto l = raw.toLowerCase();
    if (l.isEmpty() || l == "off" || l == "none" || l == "no_loop" || l == "noloop")
        return ZoneLoopMode::noLoop;
    if (l == "sustain" || l == "until_release" || l == "untilrelease")
        return ZoneLoopMode::sustain;
    if (l == "loop" || l == "forward" || l == "continuous"
        || l == "ping_pong" || l == "ping-pong" || l == "pingpong"
        || l == "alternating" || l == "loop_continuous" || l == "loopcontinuous")
        return ZoneLoopMode::continuous;
    return fallback;
}

[[nodiscard]] juce::String readAttrCi(const juce::XmlElement& el, std::initializer_list<const char*> names,
                                       const juce::String& fallback = {})
{
    for (const auto* n : names)
    {
        const juce::String name(n);
        if (el.hasAttribute(name))
            return el.getStringAttribute(name);
    }
    // case-insensitive scan of present attributes
    for (int i = 0; i < el.getNumAttributes(); ++i)
    {
        const auto& aName = el.getAttributeName(i);
        for (const auto* n : names)
            if (aName.equalsIgnoreCase(n))
                return el.getAttributeValue(i);
    }
    return fallback;
}

[[nodiscard]] juce::XmlElement* firstChildCi(juce::XmlElement& parent, const char* tagName)
{
    for (auto* c : parent.getChildIterator())
        if (c->getTagName().equalsIgnoreCase(tagName))
            return c;
    return nullptr;
}

[[nodiscard]] std::vector<juce::XmlElement*> childrenCi(juce::XmlElement& parent, const char* tagName)
{
    std::vector<juce::XmlElement*> out;
    for (auto* c : parent.getChildIterator())
        if (c->getTagName().equalsIgnoreCase(tagName))
            out.push_back(c);
    return out;
}

// Read inner-text from a <Tag>value</Tag> XML form (Ableton-style nested values),
// also accepts the JUCE-style attribute "Value" if present.
[[nodiscard]] juce::String readNestedValue(juce::XmlElement* el, const juce::String& fallback = {})
{
    if (el == nullptr) return fallback;
    if (el->hasAttribute("Value")) return el->getStringAttribute("Value");
    const auto txt = el->getAllSubText();
    return txt.isEmpty() ? fallback : txt;
}
} // namespace

// =====================================================================================
// Akai MPC keygroup .xpm
// =====================================================================================
//   <MPCVObject>
//     <Program type="Keygroup">
//       <ProgramName>...</ProgramName>
//       <Instruments>
//         <Instrument number="1">
//           <LowNote>0</LowNote><HighNote>127</HighNote>
//           <Layers>
//             <Layer number="1">
//               <SampleFile>kick.wav</SampleFile>
//               <SampleName>kick</SampleName>
//               <RootNote>60</RootNote>
//               <SampleStart>0</SampleStart><SampleEnd>...</SampleEnd>
//               <LoopStart>...</LoopStart><LoopEnd>...</LoopEnd>
//               <Loop>True/False</Loop> or <PlayMode>Loop</PlayMode>
//               <VelStart>0</VelStart><VelEnd>127</VelEnd>
//               <Volume>1.0</Volume><Pan>0.5</Pan>
//               <FineTune>0</FineTune><CoarseTune>0</CoarseTune>
//             </Layer>
//           </Layers>
//         </Instrument>
//       </Instruments>
//     </Program>
//   </MPCVObject>
namespace mpc
{
ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "MPC .xpm not found: " + file.getFullPathName());
        return result;
    }

    auto xml = juce::parseXML(file);
    if (xml == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Could not parse XML in: " + file.getFullPathName());
        return result;
    }

    juce::XmlElement* program = nullptr;
    if (xml->getTagName().equalsIgnoreCase("MPCVObject"))
        program = firstChildCi(*xml, "Program");
    else if (xml->getTagName().equalsIgnoreCase("Program"))
        program = xml.get();

    if (program == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "MPC .xpm has no <Program> element");
        return result;
    }

    if (auto* nameEl = firstChildCi(*program, "ProgramName"))
        result.program.name = nameEl->getAllSubText().toStdString();
    if (result.program.name.empty())
        result.program.name = file.getFileNameWithoutExtension().toStdString();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();

    auto* instruments = firstChildCi(*program, "Instruments");
    if (instruments == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error, "MPC .xpm has no <Instruments> element");
        return result;
    }

    for (auto* inst : childrenCi(*instruments, "Instrument"))
    {
        Group g;
        g.name = "Instrument " + juce::String(inst->getIntAttribute("number", static_cast<int>(result.program.groups.size() + 1))).toStdString();

        const int instLow  = firstChildCi(*inst, "LowNote")  ? firstChildCi(*inst, "LowNote") ->getAllSubText().getIntValue() : 0;
        const int instHigh = firstChildCi(*inst, "HighNote") ? firstChildCi(*inst, "HighNote")->getAllSubText().getIntValue() : 127;
        g.keyRange = MidiRange::fromUnordered(instLow, instHigh);

        result.program.groups.push_back(g);
        const int groupIndex = static_cast<int>(result.program.groups.size() - 1);

        auto* layers = firstChildCi(*inst, "Layers");
        if (layers == nullptr) continue;

        for (auto* layer : childrenCi(*layers, "Layer"))
        {
            const auto sampleFile = firstChildCi(*layer, "SampleFile") ? firstChildCi(*layer, "SampleFile")->getAllSubText() : juce::String();
            const auto sampleName = firstChildCi(*layer, "SampleName") ? firstChildCi(*layer, "SampleName")->getAllSubText() : juce::String();
            auto refName = sampleFile;
            if (refName.isEmpty() && sampleName.isNotEmpty())
                refName = sampleName + ".wav";
            if (refName.isEmpty())
            {
                addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                              "MPC layer missing SampleFile/SampleName - skipped");
                continue;
            }

            const int rootNote = firstChildCi(*layer, "RootNote") ? juce::jlimit(0, 127, firstChildCi(*layer, "RootNote")->getAllSubText().getIntValue()) : 60;

            const auto resolved = resolveSamplePath(refName, folder);
            const int assetIndex = loadSampleAssetFromFile(fm, resolved, rootNote, result.program,
                                                           result.sampleDataByAsset, result.diagnostics, refName);
            if (assetIndex < 0) continue;

            Zone z;
            z.sampleAssetIndex = assetIndex;
            z.groupIndex = groupIndex;
            z.keyRange = g.keyRange;
            z.rootMidiNote = rootNote;

            const int velLow  = firstChildCi(*layer, "VelStart") ? firstChildCi(*layer, "VelStart")->getAllSubText().getIntValue() : 0;
            const int velHigh = firstChildCi(*layer, "VelEnd")   ? firstChildCi(*layer, "VelEnd")  ->getAllSubText().getIntValue() : 127;
            z.velocityRange = VelocityRange::fromUnordered(velLow, velHigh);

            if (auto* ss = firstChildCi(*layer, "SampleStart")) z.sampleStart = juce::jmax(0, ss->getAllSubText().getIntValue());
            if (auto* se = firstChildCi(*layer, "SampleEnd"))   { const auto v = se->getAllSubText().getIntValue(); z.sampleEndExclusive = v > 0 ? v : -1; }

            ZoneLoopMode mode = ZoneLoopMode::noLoop;
            if (auto* pm = firstChildCi(*layer, "PlayMode")) mode = parseLoopText(pm->getAllSubText());
            else if (auto* lp = firstChildCi(*layer, "Loop"))
            {
                const auto t = lp->getAllSubText().toLowerCase();
                if (t == "true" || t == "1") mode = ZoneLoopMode::continuous;
            }
            if (mode != ZoneLoopMode::noLoop)
            {
                if (auto* ls = firstChildCi(*layer, "LoopStart")) z.loopStart = juce::jmax(0, ls->getAllSubText().getIntValue());
                if (auto* le = firstChildCi(*layer, "LoopEnd"))   z.loopEndExclusive = le->getAllSubText().getIntValue();
                if (z.loopEndExclusive > z.loopStart) z.loopMode = mode;
            }

            if (auto* vol = firstChildCi(*layer, "Volume"))
            {
                const auto lin = (float) vol->getAllSubText().getDoubleValue();
                if (lin > 0.0f) z.gainDb = juce::Decibels::gainToDecibels(lin);
            }
            if (auto* pan = firstChildCi(*layer, "Pan"))
            {
                // MPC 0..1 (0.5 center)
                const auto p = (float) pan->getAllSubText().getDoubleValue();
                z.pan = juce::jlimit(-1.0f, 1.0f, (p - 0.5f) * 2.0f);
            }
            if (auto* ft = firstChildCi(*layer, "FineTune"))   z.tuneCents += (float) ft->getAllSubText().getDoubleValue();
            if (auto* ct = firstChildCi(*layer, "CoarseTune")) z.tuneCents += 100.0f * (float) ct->getAllSubText().getDoubleValue();

            result.program.zones.push_back(z);
        }
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "MPC .xpm produced no playable zones");

    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("MPC keygroup", r.program, r.diagnostics, imported);
}
} // namespace mpc

// =====================================================================================
// 1010music Bento / blackbox preset.xml
// =====================================================================================
//   <document>
//     <session>
//       <cell type="sample" filename="kick.wav" rootnote="60" lonote="48" hinote="72"
//             playmode="loop" loopstart="..." loopend="..." samstart="..." samend="..."
//             gaindb="0" pan="0" fine="0" coarse="0" lovel="0" hivel="127" />
//     </session>
//   </document>
// We accept either <cell> elements directly or any descendant carrying a `filename`
// attribute and a sample-like role.
namespace bento
{
ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "1010music preset not found: " + file.getFullPathName());
        return result;
    }

    auto xml = juce::parseXML(file);
    if (xml == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Could not parse XML in: " + file.getFullPathName());
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();
    result.program.name = file.getFileNameWithoutExtension().toStdString();

    Group g;
    g.name = "Cells";
    result.program.groups.push_back(g);
    const int groupIndex = 0;

    // Walk every descendant element looking for a usable filename attr.
    std::vector<juce::XmlElement*> stack { xml.get() };
    while (!stack.empty())
    {
        auto* el = stack.back(); stack.pop_back();
        for (auto* c : el->getChildIterator())
            stack.push_back(c);

        const auto fname = readAttrCi(*el, { "filename", "sample", "samplefile", "file", "path" });
        if (fname.isEmpty()) continue;
        if (!fname.endsWithIgnoreCase(".wav") && !fname.endsWithIgnoreCase(".aif") && !fname.endsWithIgnoreCase(".aiff"))
            continue;

        const int rootNote = parseMidiNoteText(readAttrCi(*el, { "rootnote", "root", "rootnoteid" }), 60);
        const auto resolved = resolveSamplePath(fname, folder);
        const int assetIndex = loadSampleAssetFromFile(fm, resolved, rootNote, result.program,
                                                       result.sampleDataByAsset, result.diagnostics, fname);
        if (assetIndex < 0) continue;

        Zone z;
        z.sampleAssetIndex = assetIndex;
        z.groupIndex = groupIndex;
        z.rootMidiNote = rootNote;
        z.keyRange = MidiRange::fromUnordered(
            parseMidiNoteText(readAttrCi(*el, { "lonote", "lokey", "low", "lowkey" }, juce::String(rootNote)), rootNote),
            parseMidiNoteText(readAttrCi(*el, { "hinote", "hikey", "high", "highkey" }, juce::String(rootNote)), rootNote));
        z.velocityRange = VelocityRange::fromUnordered(
            readAttrCi(*el, { "lovel", "lowvelocity", "lowvel" }, "0").getIntValue(),
            readAttrCi(*el, { "hivel", "highvelocity", "highvel" }, "127").getIntValue());
        z.sampleStart = juce::jmax(0, readAttrCi(*el, { "samstart", "samplestart", "start" }, "0").getIntValue());
        const auto sse = readAttrCi(*el, { "samend", "sampleend", "end" }, "0").getIntValue();
        z.sampleEndExclusive = sse > 0 ? sse : -1;

        const auto pm = readAttrCi(*el, { "playmode", "loopmode", "mode" });
        const auto mode = parseLoopText(pm);
        if (mode != ZoneLoopMode::noLoop)
        {
            const int ls = readAttrCi(*el, { "loopstart" }, "-1").getIntValue();
            const int le = readAttrCi(*el, { "loopend" }, "-1").getIntValue();
            if (ls >= 0 && le > ls)
            {
                z.loopStart = ls;
                z.loopEndExclusive = le;
                z.loopMode = mode;
            }
        }

        z.gainDb = (float) readAttrCi(*el, { "gaindb", "gain" }, "0").getDoubleValue();
        z.pan = juce::jlimit(-1.0f, 1.0f, (float) readAttrCi(*el, { "pan" }, "0").getDoubleValue());
        z.tuneCents = (float) readAttrCi(*el, { "fine" }, "0").getDoubleValue()
                    + 100.0f * (float) readAttrCi(*el, { "coarse" }, "0").getDoubleValue();

        result.program.zones.push_back(z);
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "1010music preset produced no playable zones");

    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("1010music preset", r.program, r.diagnostics, imported);
}
} // namespace bento

// =====================================================================================
// TAL Sampler .talsmpl
// =====================================================================================
//   <tal>
//     <programs>
//       <program programname="...">
//         <multisample>
//           <sample url="kick.wav" rootkey="60" startnote="48" endnote="72"
//                   startvelocity="0" endvelocity="127"
//                   startsample="0" endsample="..." loopstart="..." loopend="..."
//                   loopactive="1" volume="1.0" pan="0.5" finetune="0" tune="0" />
//         </multisample>
//       </program>
//     </programs>
//   </tal>
namespace talsmpl
{
ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "TAL Sampler preset not found: " + file.getFullPathName());
        return result;
    }
    auto xml = juce::parseXML(file);
    if (xml == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Could not parse XML in: " + file.getFullPathName());
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();
    result.program.name = file.getFileNameWithoutExtension().toStdString();

    // Walk descendants and pull every <sample> element.
    std::vector<juce::XmlElement*> stack { xml.get() };
    Group defaultGroup;
    defaultGroup.name = "Default";
    result.program.groups.push_back(defaultGroup);
    const int groupIndex = 0;

    while (!stack.empty())
    {
        auto* el = stack.back(); stack.pop_back();
        for (auto* c : el->getChildIterator())
            stack.push_back(c);

        if (!el->getTagName().equalsIgnoreCase("sample"))
            continue;

        const auto url = readAttrCi(*el, { "url", "path", "file", "filename", "samplepath" });
        if (url.isEmpty()) continue;

        const int rootNote = parseMidiNoteText(readAttrCi(*el, { "rootkey", "rootnote", "root" }, "60"), 60);
        const auto resolved = resolveSamplePath(url, folder);
        const int assetIndex = loadSampleAssetFromFile(fm, resolved, rootNote, result.program,
                                                       result.sampleDataByAsset, result.diagnostics, url);
        if (assetIndex < 0) continue;

        Zone z;
        z.sampleAssetIndex = assetIndex;
        z.groupIndex = groupIndex;
        z.rootMidiNote = rootNote;
        z.keyRange = MidiRange::fromUnordered(
            parseMidiNoteText(readAttrCi(*el, { "startnote", "lonote", "lowkey" }, juce::String(rootNote)), rootNote),
            parseMidiNoteText(readAttrCi(*el, { "endnote", "hinote", "highkey" }, juce::String(rootNote)), rootNote));
        z.velocityRange = VelocityRange::fromUnordered(
            readAttrCi(*el, { "startvelocity", "lovel", "lowvel" }, "0").getIntValue(),
            readAttrCi(*el, { "endvelocity", "hivel", "highvel" }, "127").getIntValue());

        z.sampleStart = juce::jmax(0, readAttrCi(*el, { "startsample", "start" }, "0").getIntValue());
        const auto sse = readAttrCi(*el, { "endsample", "end" }, "0").getIntValue();
        z.sampleEndExclusive = sse > 0 ? sse : -1;

        const bool loopActive = readAttrCi(*el, { "loopactive", "loopenabled" }, "0").getIntValue() != 0
                              || parseLoopText(readAttrCi(*el, { "loopmode", "playmode" })) != ZoneLoopMode::noLoop;
        if (loopActive)
        {
            const int ls = readAttrCi(*el, { "loopstart" }, "-1").getIntValue();
            const int le = readAttrCi(*el, { "loopend" }, "-1").getIntValue();
            if (ls >= 0 && le > ls)
            {
                z.loopStart = ls;
                z.loopEndExclusive = le;
                z.loopMode = ZoneLoopMode::continuous;
            }
        }

        const auto vol = (float) readAttrCi(*el, { "volume", "gain" }, "1.0").getDoubleValue();
        if (vol > 0.0f) z.gainDb = juce::Decibels::gainToDecibels(vol);
        const auto pan = (float) readAttrCi(*el, { "pan" }, "0.5").getDoubleValue();
        z.pan = juce::jlimit(-1.0f, 1.0f, (pan - 0.5f) * 2.0f);
        z.tuneCents = (float) readAttrCi(*el, { "finetune" }, "0").getDoubleValue()
                    + 100.0f * (float) readAttrCi(*el, { "tune", "coarsetune" }, "0").getDoubleValue();

        result.program.zones.push_back(z);
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "TAL Sampler preset produced no playable zones");

    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("TAL Sampler", r.program, r.diagnostics, imported);
}
} // namespace talsmpl

// =====================================================================================
// CWITEC TX16Wx .txprog
// =====================================================================================
//   <program name="...">
//     <group name="...">
//       <region sample="kick.wav" key="60" lokey="48" hikey="72"
//               lovel="0" hivel="127" rootkey="60"
//               start="0" end="..." loopstart="..." loopend="..." loop="forward"
//               volume="0" pan="0" tune="0" />
//     </group>
//   </program>
namespace tx16wx
{
ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "TX16Wx preset not found: " + file.getFullPathName());
        return result;
    }
    auto xml = juce::parseXML(file);
    if (xml == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Could not parse XML in: " + file.getFullPathName());
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();

    juce::XmlElement* program = xml->getTagName().equalsIgnoreCase("program") ? xml.get() : firstChildCi(*xml, "program");
    if (program == nullptr) program = xml.get();

    if (program->hasAttribute("name"))
        result.program.name = program->getStringAttribute("name").toStdString();
    if (result.program.name.empty())
        result.program.name = file.getFileNameWithoutExtension().toStdString();

    auto handleRegion = [&](juce::XmlElement& region, int groupIndex)
    {
        const auto sample = readAttrCi(region, { "sample", "file", "path", "filename" });
        if (sample.isEmpty()) return;
        const int rootNote = parseMidiNoteText(readAttrCi(region, { "rootkey", "root", "rootnote" }, "60"), 60);
        const auto resolved = resolveSamplePath(sample, folder);
        const int assetIndex = loadSampleAssetFromFile(fm, resolved, rootNote, result.program,
                                                       result.sampleDataByAsset, result.diagnostics, sample);
        if (assetIndex < 0) return;

        Zone z;
        z.sampleAssetIndex = assetIndex;
        z.groupIndex = groupIndex;
        z.rootMidiNote = rootNote;

        const auto singleKey = readAttrCi(region, { "key" });
        if (singleKey.isNotEmpty())
        {
            const auto k = parseMidiNoteText(singleKey, rootNote);
            z.keyRange = MidiRange::single(k);
        }
        else
        {
            z.keyRange = MidiRange::fromUnordered(
                parseMidiNoteText(readAttrCi(region, { "lokey", "lonote", "lowkey" }, juce::String(rootNote)), rootNote),
                parseMidiNoteText(readAttrCi(region, { "hikey", "hinote", "highkey" }, juce::String(rootNote)), rootNote));
        }
        z.velocityRange = VelocityRange::fromUnordered(
            readAttrCi(region, { "lovel" }, "0").getIntValue(),
            readAttrCi(region, { "hivel" }, "127").getIntValue());

        z.sampleStart = juce::jmax(0, readAttrCi(region, { "start" }, "0").getIntValue());
        const auto se = readAttrCi(region, { "end" }, "0").getIntValue();
        z.sampleEndExclusive = se > 0 ? se : -1;

        const auto mode = parseLoopText(readAttrCi(region, { "loop", "loopmode" }));
        if (mode != ZoneLoopMode::noLoop)
        {
            const int ls = readAttrCi(region, { "loopstart" }, "-1").getIntValue();
            const int le = readAttrCi(region, { "loopend" }, "-1").getIntValue();
            if (ls >= 0 && le > ls)
            {
                z.loopStart = ls;
                z.loopEndExclusive = le;
                z.loopMode = mode;
            }
        }

        z.gainDb = (float) readAttrCi(region, { "volume", "gain" }, "0").getDoubleValue();
        z.pan = juce::jlimit(-1.0f, 1.0f, (float) readAttrCi(region, { "pan" }, "0").getDoubleValue());
        z.tuneCents = (float) readAttrCi(region, { "tune", "tuning", "finetune" }, "0").getDoubleValue();

        result.program.zones.push_back(z);
    };

    auto groups = childrenCi(*program, "group");
    if (groups.empty())
    {
        Group g; g.name = "Default";
        result.program.groups.push_back(g);
        for (auto* r : childrenCi(*program, "region"))
            handleRegion(*r, 0);
    }
    else
    {
        for (auto* g : groups)
        {
            Group group;
            group.name = g->getStringAttribute("name", "Group " + juce::String(static_cast<int>(result.program.groups.size() + 1))).toStdString();
            result.program.groups.push_back(group);
            const int gi = static_cast<int>(result.program.groups.size() - 1);
            for (auto* r : childrenCi(*g, "region"))
                handleRegion(*r, gi);
        }
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "TX16Wx preset produced no playable zones");
    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("TX16Wx", r.program, r.diagnostics, imported);
}
} // namespace tx16wx

// =====================================================================================
// Korg wavestate / modwave .korgmultisample (ZIP container with multisample.xml + WAVs)
// =====================================================================================
namespace korgmulti
{
ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg multisample not found: " + file.getFullPathName());
        return result;
    }

    juce::ZipFile zip(file);
    if (zip.getNumEntries() == 0)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg multisample is empty or not a valid ZIP: " + file.getFullPathName());
        return result;
    }

    int manifestIndex = -1;
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry(i);
        if (e == nullptr) continue;
        const auto base = juce::File::createFileWithoutCheckingPath(e->filename).getFileName();
        if (base.equalsIgnoreCase("multisample.xml")
            || base.endsWithIgnoreCase(".KorgMultiSample.xml")
            || base.endsWithIgnoreCase(".korgmultisample"))
        {
            manifestIndex = i;
            break;
        }
    }
    if (manifestIndex < 0)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg multisample missing manifest XML inside container");
        return result;
    }

    std::unique_ptr<juce::InputStream> mstream(zip.createStreamForEntry(manifestIndex));
    if (mstream == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Could not open Korg multisample manifest");
        return result;
    }

    auto xml = juce::parseXML(mstream->readEntireStreamAsString());
    if (xml == nullptr)
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg multisample manifest is invalid XML");
        return result;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    result.program.name = readAttrCi(*xml, { "name" }, file.getFileNameWithoutExtension()).toStdString();

    Group g; g.name = "Default";
    result.program.groups.push_back(g);
    const int groupIndex = 0;

    auto findEntryByName = [&](const juce::String& name) -> int
    {
        if (name.isEmpty()) return -1;
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry(i);
            if (e != nullptr && e->filename == name) return i;
        }
        const auto base = juce::File::createFileWithoutCheckingPath(name).getFileName();
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            const auto* e = zip.getEntry(i);
            if (e == nullptr) continue;
            if (juce::File::createFileWithoutCheckingPath(e->filename).getFileName().equalsIgnoreCase(base))
                return i;
        }
        return -1;
    };

    auto loadFromZipEntry = [&](int entryIndex, int rootNote, const juce::String& humanLabel) -> int
    {
        if (entryIndex < 0)
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                          "Korg multisample missing audio entry: " + humanLabel);
            return -1;
        }
        std::unique_ptr<juce::InputStream> raw(zip.createStreamForEntry(entryIndex));
        if (raw == nullptr) return -1;
        juce::MemoryBlock blob;
        raw->readIntoMemoryBlock(blob);
        std::unique_ptr<juce::AudioFormatReader> reader(
            fm.createReaderFor(std::make_unique<juce::MemoryInputStream>(blob, true)));
        if (reader == nullptr)
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                          "Korg multisample unsupported audio: " + humanLabel);
            return -1;
        }
        SampleAsset asset;
        asset.sourcePath = humanLabel.toStdString();
        asset.displayName = juce::File::createFileWithoutCheckingPath(humanLabel).getFileName().toStdString();
        asset.lengthSamples = static_cast<int>(juce::jlimit<juce::int64>(0, std::numeric_limits<int>::max(), reader->lengthInSamples));
        asset.numChannels = static_cast<int>(reader->numChannels);
        asset.sampleRateHz = reader->sampleRate;
        asset.rootMidiNote = rootNote;
        asset.bitDepth = static_cast<int>(reader->bitsPerSample);
        if (!asset.hasAudio())
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                          "Korg multisample sample empty: " + humanLabel);
            return -1;
        }
        juce::AudioBuffer<float> buf(asset.numChannels, asset.lengthSamples);
        if (!reader->read(&buf, 0, asset.lengthSamples, 0, true, true))
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                          "Korg multisample decode failed: " + humanLabel);
            return -1;
        }
        result.program.sampleAssets.push_back(asset);
        result.sampleDataByAsset.push_back(std::move(buf));
        return static_cast<int>(result.program.sampleAssets.size() - 1);
    };

    // Walk descendants for <sample> elements with file refs.
    std::vector<juce::XmlElement*> stack { xml.get() };
    while (!stack.empty())
    {
        auto* el = stack.back(); stack.pop_back();
        for (auto* c : el->getChildIterator()) stack.push_back(c);

        if (!el->getTagName().equalsIgnoreCase("sample") && !el->getTagName().equalsIgnoreCase("smpl"))
            continue;

        const auto fname = readAttrCi(*el, { "file", "filename", "url", "path", "samplefile" });
        if (fname.isEmpty()) continue;

        const int rootNote = parseMidiNoteText(readAttrCi(*el, { "rootkey", "root", "rootnote", "originalpitch" }, "60"), 60);
        const int entryIdx = findEntryByName(fname);
        const int assetIndex = loadFromZipEntry(entryIdx, rootNote, fname);
        if (assetIndex < 0) continue;

        Zone z;
        z.sampleAssetIndex = assetIndex;
        z.groupIndex = groupIndex;
        z.rootMidiNote = rootNote;

        const auto loKey = readAttrCi(*el, { "lokey", "low", "lonote", "lowkey", "bottomkey" });
        const auto hiKey = readAttrCi(*el, { "hikey", "high", "hinote", "highkey", "topkey" });
        z.keyRange = MidiRange::fromUnordered(
            parseMidiNoteText(loKey, rootNote),
            parseMidiNoteText(hiKey, rootNote));

        z.velocityRange = VelocityRange::fromUnordered(
            readAttrCi(*el, { "lovel", "lowvelocity" }, "0").getIntValue(),
            readAttrCi(*el, { "hivel", "highvelocity" }, "127").getIntValue());

        z.sampleStart = juce::jmax(0, readAttrCi(*el, { "start", "samplestart" }, "0").getIntValue());
        const auto se = readAttrCi(*el, { "end", "sampleend" }, "0").getIntValue();
        z.sampleEndExclusive = se > 0 ? se : -1;

        const auto mode = parseLoopText(readAttrCi(*el, { "loop", "loopmode", "playmode" }));
        if (mode != ZoneLoopMode::noLoop)
        {
            const int ls = readAttrCi(*el, { "loopstart" }, "-1").getIntValue();
            const int le = readAttrCi(*el, { "loopend" }, "-1").getIntValue();
            if (ls >= 0 && le > ls) { z.loopStart = ls; z.loopEndExclusive = le; z.loopMode = mode; }
        }

        z.gainDb = (float) readAttrCi(*el, { "gain", "volume" }, "0").getDoubleValue();
        z.pan = juce::jlimit(-1.0f, 1.0f, (float) readAttrCi(*el, { "pan" }, "0").getDoubleValue());
        z.tuneCents = (float) readAttrCi(*el, { "tune", "finetune" }, "0").getDoubleValue();

        result.program.zones.push_back(z);
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Korg multisample produced no playable zones");
    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("Korg multisample", r.program, r.diagnostics, imported);
}
} // namespace korgmulti

// =====================================================================================
// Ableton Sampler / Simpler preset .adv (device preset) and .adg (device group)
// =====================================================================================
//   Both file types are gzip-compressed XML. Decompress with juce::GZIPDecompressorInputStream.
//   The interesting payload lives under <MultiSampler>/<Player>/<MultiSampleMap>/<SampleParts>
//   (.adv) or nested inside a GroupDevicePreset rack (.adg). We walk the entire tree
//   for <MultiSamplePart> elements so both layouts work.
//
//   Each <MultiSamplePart> typically contains:
//     <Name Value="..."/>
//     <KeyRange><Min Value="0"/><Max Value="127"/>...</KeyRange>
//     <VelocityRange><Min Value="1"/><Max Value="127"/>...</VelocityRange>
//     <RootKey Value="60"/><Detune Value="0"/><TuneScale Value="100"/>
//     <Volume Value="1"/> <Panorama Value="0"/>
//     <SampleStart Value="..."/><SampleEnd Value="..."/>
//     <SustainLoop>
//       <Mode Value="0|1|2|3"/>           (0 none, 1 forward, 2 ping-pong, 3 release)
//       <Start .../><End .../>
//     </SustainLoop>
//     <SampleRef><FileRef>
//       <Path Value="C:/abs/path/to/file.wav"/>
//       <Name Value="file.wav"/>
//       <RelativePath><RelativePathElement Dir="Samples"/>...</RelativePath>
//     </FileRef></SampleRef>
namespace ableton
{
namespace
{
[[nodiscard]] juce::String readValueChild(juce::XmlElement* parent, const char* name, const juce::String& fallback = {})
{
    if (parent == nullptr) return fallback;
    auto* el = firstChildCi(*parent, name);
    if (el == nullptr) return fallback;
    if (el->hasAttribute("Value")) return el->getStringAttribute("Value");
    const auto t = el->getAllSubText();
    return t.isEmpty() ? fallback : t;
}

[[nodiscard]] std::unique_ptr<juce::XmlElement> parseGzipXml(const juce::File& file,
                                                             std::vector<Diagnostic>& diagnostics)
{
    juce::FileInputStream raw(file);
    if (!raw.openedOk())
    {
        addDiagnostic(diagnostics, Diagnostic::Severity::error,
                      "Could not open: " + file.getFullPathName());
        return {};
    }
    juce::GZIPDecompressorInputStream gz(&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    juce::MemoryBlock mem;
    {
        juce::MemoryOutputStream mos(mem, false);
        if (mos.writeFromInputStream(gz, -1) <= 0)
        {
            addDiagnostic(diagnostics, Diagnostic::Severity::error,
                          "Could not gzip-decompress: " + file.getFullPathName());
            return {};
        }
    }
    auto xmlText = juce::String::fromUTF8(static_cast<const char*>(mem.getData()), (int) mem.getSize());
    auto xml = juce::parseXML(xmlText);
    if (xml == nullptr)
    {
        addDiagnostic(diagnostics, Diagnostic::Severity::error,
                      "Decompressed payload is not valid XML: " + file.getFullPathName());
        return {};
    }
    return xml;
}

[[nodiscard]] juce::String readAbletonSamplePath(juce::XmlElement* part)
{
    if (part == nullptr) return {};
    auto* sampleRef = firstChildCi(*part, "SampleRef");
    if (sampleRef == nullptr) return {};
    auto* fileRef = firstChildCi(*sampleRef, "FileRef");
    if (fileRef == nullptr) return {};

    // 1) Absolute path is the most reliable.
    if (auto* p = firstChildCi(*fileRef, "Path"))
    {
        const auto v = p->getStringAttribute("Value");
        if (v.isNotEmpty()) return v;
    }
    // 2) Some recent Live versions store an absolute string under <RelativePath Value=>
    //    when the path type means "absolute".
    if (auto* rp = firstChildCi(*fileRef, "RelativePath"))
    {
        const auto v = rp->getStringAttribute("Value");
        if (v.isNotEmpty()) return v;
        // 3) Otherwise rebuild from <RelativePathElement Dir=...> + <Name Value=...>
        juce::StringArray parts;
        for (auto* e : rp->getChildIterator())
            if (e->getTagName().equalsIgnoreCase("RelativePathElement"))
                parts.add(e->getStringAttribute("Dir"));
        const auto name = readValueChild(fileRef, "Name");
        if (name.isNotEmpty())
        {
            parts.add(name);
            return parts.joinIntoString("/");
        }
    }
    // 4) Fall back to the bare file name.
    const auto name = readValueChild(fileRef, "Name");
    if (name.isNotEmpty()) return name;
    return readValueChild(fileRef, "File");
}

[[nodiscard]] ZoneLoopMode mapAbletonLoopMode(int v)
{
    switch (v)
    {
        case 0: return ZoneLoopMode::noLoop;
        case 1: return ZoneLoopMode::continuous;     // forward
        case 2: return ZoneLoopMode::continuous;     // ping-pong (treated as continuous)
        case 3: return ZoneLoopMode::sustain;        // release / sustain loop variant
        default: return ZoneLoopMode::noLoop;
    }
}

[[nodiscard]] std::vector<juce::XmlElement*> findAllByTag(juce::XmlElement& root, const char* tag)
{
    std::vector<juce::XmlElement*> out;
    std::vector<juce::XmlElement*> stack { &root };
    while (!stack.empty())
    {
        auto* el = stack.back(); stack.pop_back();
        if (el->getTagName().equalsIgnoreCase(tag)) out.push_back(el);
        for (auto* c : el->getChildIterator())
            stack.push_back(c);
    }
    return out;
}
} // namespace

ImportResult importFile(const juce::File& file)
{
    ImportResult result;
    if (!file.existsAsFile())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Ableton preset not found: " + file.getFullPathName());
        return result;
    }

    auto xml = parseGzipXml(file, result.diagnostics);
    if (xml == nullptr) return result;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    const auto folder = file.getParentDirectory();
    result.program.name = file.getFileNameWithoutExtension().toStdString();

    Group g;
    g.name = "Sampler";
    result.program.groups.push_back(g);
    const int groupIndex = 0;

    const auto parts = findAllByTag(*xml, "MultiSamplePart");
    if (parts.empty())
    {
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "No <MultiSamplePart> elements found in Ableton preset");
        return result;
    }

    for (auto* part : parts)
    {
        const auto rawPath = readAbletonSamplePath(part);
        if (rawPath.isEmpty())
        {
            addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                          "Ableton MultiSamplePart has no SampleRef path - skipped");
            continue;
        }

        const int rootNote = juce::jlimit(0, 127, readValueChild(part, "RootKey", "60").getIntValue());

        const auto resolved = resolveSamplePath(rawPath, folder);
        const int assetIndex = loadSampleAssetFromFile(fm, resolved, rootNote, result.program,
                                                       result.sampleDataByAsset, result.diagnostics, rawPath);
        if (assetIndex < 0) continue;

        Zone z;
        z.sampleAssetIndex = assetIndex;
        z.groupIndex = groupIndex;
        z.rootMidiNote = rootNote;

        if (auto* kr = firstChildCi(*part, "KeyRange"))
        {
            const int lo = readValueChild(kr, "Min", "0").getIntValue();
            const int hi = readValueChild(kr, "Max", "127").getIntValue();
            z.keyRange = MidiRange::fromUnordered(lo, hi);
        }
        else
        {
            z.keyRange = MidiRange::single(rootNote);
        }

        if (auto* vr = firstChildCi(*part, "VelocityRange"))
        {
            const int lo = readValueChild(vr, "Min", "1").getIntValue();
            const int hi = readValueChild(vr, "Max", "127").getIntValue();
            z.velocityRange = VelocityRange::fromUnordered(lo, hi);
        }

        const auto sampleStart = readValueChild(part, "SampleStart");
        if (sampleStart.isNotEmpty()) z.sampleStart = juce::jmax(0, sampleStart.getIntValue());
        const auto sampleEnd = readValueChild(part, "SampleEnd");
        if (sampleEnd.isNotEmpty())
        {
            const auto v = sampleEnd.getIntValue();
            z.sampleEndExclusive = v > 0 ? v : -1;
        }

        if (auto* loop = firstChildCi(*part, "SustainLoop"))
        {
            const int mode = readValueChild(loop, "Mode", "0").getIntValue();
            const auto loopMode = mapAbletonLoopMode(mode);
            const int ls = readValueChild(loop, "Start", "0").getIntValue();
            const int le = readValueChild(loop, "End", "-1").getIntValue();
            if (loopMode != ZoneLoopMode::noLoop && le > ls)
            {
                z.loopStart = juce::jmax(0, ls);
                z.loopEndExclusive = le;
                z.loopMode = loopMode;
            }
        }

        const auto vol = readValueChild(part, "Volume");
        if (vol.isNotEmpty())
        {
            const auto lin = (float) vol.getDoubleValue();
            if (lin > 0.0f) z.gainDb = juce::Decibels::gainToDecibels(lin);
        }
        const auto pan = readValueChild(part, "Panorama");
        if (pan.isNotEmpty()) z.pan = juce::jlimit(-1.0f, 1.0f, (float) pan.getDoubleValue());

        const auto detune = readValueChild(part, "Detune");
        if (detune.isNotEmpty()) z.tuneCents += (float) detune.getDoubleValue();
        const auto tuneScale = readValueChild(part, "TuneScale");
        if (tuneScale.isNotEmpty())
        {
            // TuneScale 100 = chromatic, 0 = pinned to root. Audiocity has no per-zone
            // tracking parameter, so just record a diagnostic when it's unusual.
            const auto v = (int) tuneScale.getDoubleValue();
            if (v != 100)
                addDiagnostic(result.diagnostics, Diagnostic::Severity::warning,
                              "Ableton TuneScale=" + juce::String(v) + " not modeled - assuming chromatic tracking");
        }

        result.program.zones.push_back(z);
    }

    if (result.program.zones.empty())
        addDiagnostic(result.diagnostics, Diagnostic::Severity::error,
                      "Ableton preset produced no playable zones");

    return result;
}

juce::String buildImportSummary(const ImportResult& r, const bool imported)
{
    return buildGenericSummary("Ableton sampler preset", r.program, r.diagnostics, imported);
}
} // namespace ableton
} // namespace audiocity::engine
