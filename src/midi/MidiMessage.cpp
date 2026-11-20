#include "pxs/Midi.h"

#include <algorithm>
#include <cmath>

namespace pxs::midi {

namespace {
uint8_t data7(double v) { return uint8_t(std::clamp(int(std::lround(v * 127.0)), 0, 127)); }
uint8_t channelByte(int channel) { return uint8_t(std::clamp(channel, 1, 16) - 1); }
uint8_t number7(int n) { return uint8_t(std::clamp(n, 0, 127)); }
} // namespace

bool parse(const uint8_t *m, size_t n, InteractionEvent &e)
{
    if (!m || n < 2) return false;
    const int status = m[0] & 0xF0;
    switch (status) {
    case 0xB0:
        if (n < 3) return false;
        e.message = MidiMessageKind::ControlChange;
        e.number = m[1] & 0x7F;
        e.value = (m[2] & 0x7F) / 127.0;
        break;
    case 0x90:
    case 0x80:
        if (n < 3) return false;
        e.message = MidiMessageKind::Note;
        e.number = m[1] & 0x7F;
        e.value = status == 0x90 ? (m[2] & 0x7F) / 127.0 : 0.0; // note on at velocity 0: off
        break;
    case 0xE0:
        if (n < 3) return false;
        e.message = MidiMessageKind::PitchBend;
        e.number = 0;
        e.value = (((m[2] & 0x7F) << 7) | (m[1] & 0x7F)) / 16383.0;
        break;
    case 0xC0:
        e.message = MidiMessageKind::ProgramChange;
        e.number = m[1] & 0x7F;
        e.value = 1.0; // a program chosen: up (then down at once, by the caller)
        break;
    default: return false;
    }
    e.source = InteractionSource::Midi;
    e.channel = (m[0] & 0x0F) + 1;
    return true;
}

std::vector<uint8_t> controlChange(int channel, int controller, double value)
{
    return {uint8_t(0xB0 | channelByte(channel)), number7(controller), data7(value)};
}
std::vector<uint8_t> noteOn(int channel, int note, double velocity)
{
    return {uint8_t(0x90 | channelByte(channel)), number7(note), data7(velocity)};
}
std::vector<uint8_t> noteOff(int channel, int note)
{
    return {uint8_t(0x80 | channelByte(channel)), number7(note), 0};
}
std::vector<uint8_t> pitchBend(int channel, double value)
{
    const int v = std::clamp(int(std::lround(value * 16383.0)), 0, 16383);
    return {uint8_t(0xE0 | channelByte(channel)), uint8_t(v & 0x7F), uint8_t(v >> 7)};
}
std::vector<uint8_t> programChange(int channel, int program)
{
    return {uint8_t(0xC0 | channelByte(channel)), number7(program)};
}

void learn(Interaction &it, const InteractionEvent &e)
{
    it.source = InteractionSource::Midi;
    it.midiPort = e.port;
    it.midiChannel = e.channel;
    it.midiMessage = e.message;
    it.midiNumber = e.message == MidiMessageKind::PitchBend ? 0 : e.number;
}

Inputs::Inputs(QObject *parent) : QObject(parent)
{
    m_scan.setInterval(2000);
    connect(&m_scan, &QTimer::timeout, this, &Inputs::rescan);
}

Inputs::~Inputs() { m_inputs.clear(); }

void Inputs::setWanted(bool on)
{
    if (on == m_wanted) return;
    m_wanted = on;
    if (!on) {
        m_scan.stop();
        m_inputs.clear();
        m_names.clear();
        m_tried.clear();
        return;
    }
    m_scan.start();
    rescan();
}

void Inputs::rescan()
{
    if (!m_wanted) return;
    const QStringList names = inputNames();
    // A port that failed to open is not tried again every scan (that would close and reopen all the others, losing their
    // messages), only when the list of devices changes
    if (names == m_tried) return;
    m_inputs.clear();
    m_names.clear();
    for (const QString &name : names) {
        auto in = std::make_unique<Input>();
        QString err;
        const bool ok = in->open(name, [this, name](const uint8_t *m, size_t n) {
            InteractionEvent e;
            e.port = name;
            if (!parse(m, n, e) || !m_callback) return;
            m_callback(e);
            if (e.message == MidiMessageKind::ProgramChange) { // an action: up, then down
                e.value = 0.0;
                m_callback(e);
            }
        }, &err);
        if (!ok) continue;
        m_inputs.push_back(std::move(in));
        m_names << name;
    }
    m_tried = names;
}

} // namespace pxs::midi
