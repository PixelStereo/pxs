#pragma once
// MIDI: the ports of the machine (RtMidi: CoreMIDI, Windows MM, ALSA), the messages that come in as an interaction's event
// (and the ones that go out), and the inputs an application listens to while a MIDI interaction needs them or while it
// learns. Ports exist only when the application gives pxs an RtMidi (see CMakeLists.txt: PXS_RTMIDI_TARGET); without one
// `available()` is false, the lists are empty, and everything about messages still works.
#include "Interactions.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace pxs::midi {

bool available();          // built with a MIDI backend
bool virtualPorts();       // ports of its own can be made (macOS, Linux)
void setClientName(const QString &name); // what other software sees of us; before the first port (default "pxs")
QStringList outputNames(); // the ports to send to (not our own virtual ones)
QStringList inputNames();  // the ports to receive from

class Output
{
public:
    Output();
    ~Output();
    bool open(const QString &name, QString *err); // empty: the first port
    // A port of its own other software reads from, named so (macOS, Linux; false elsewhere)
    bool openVirtual(const QString &name, QString *err);
    void send(const std::vector<uint8_t> &msg);
    QString name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

class Input
{
public:
    using Callback = std::function<void(const uint8_t *msg, size_t len)>; // its thread
    Input();
    ~Input();
    bool open(const QString &name, Callback cb, QString *err); // empty: the first port
    QString name() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// --- Messages ---------------------------------------------------------------------------------------------------------

// A message as an interaction's event (source MIDI; channel 1..16; value 0..1): control change, note on and off (a note on at
// velocity 0 is an off), pitch bend, program change (value 1: a program chosen, then the caller sends 0). The port is the
// caller's to fill. False for anything else (clock, SysEx, aftertouch…) or a message too short.
bool parse(const uint8_t *msg, size_t len, InteractionEvent &event);
// The bytes of a message (channel 1..16, number 0..127, value 0..1)
std::vector<uint8_t> controlChange(int channel, int controller, double value);
std::vector<uint8_t> noteOn(int channel, int note, double velocity);
std::vector<uint8_t> noteOff(int channel, int note);
std::vector<uint8_t> pitchBend(int channel, double value);
std::vector<uint8_t> programChange(int channel, int program);
// MIDI learn: makes the interaction listen for what the event is (its port, channel, kind and number)
void learn(Interaction &interaction, const InteractionEvent &event);

// --- Inputs -----------------------------------------------------------------------------------------------------------

// Every input of the machine, open while wanted (a MIDI interaction exists, or the application is learning), looked for
// again every few seconds so that a controller plugged in is heard. The callback runs on the port's thread; a program
// change comes as two events, up then down.
class Inputs : public QObject
{
public:
    using Callback = std::function<void(const InteractionEvent &)>;
    explicit Inputs(QObject *parent = nullptr);
    ~Inputs() override;
    void setCallback(Callback callback) { m_callback = std::move(callback); }
    void setWanted(bool on); // on the owner's thread
    bool wanted() const { return m_wanted; }
    QStringList open() const { return m_names; } // the ports it listens to
    void rescan();

private:
    Callback m_callback;
    bool m_wanted = false;
    QTimer m_scan;
    std::vector<std::unique_ptr<Input>> m_inputs;
    QStringList m_names, m_tried;
};

} // namespace pxs::midi
