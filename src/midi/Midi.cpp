#include "pxs/Midi.h"

#ifdef PXS_HAS_RTMIDI
#include <RtMidi.h>

#include <QRegularExpression>

namespace {
QByteArray &clientName()
{
    static QByteArray name = "pxs"; // the name other software sees
    return name;
}
} // namespace
#define kClient clientName().constData()

// Errors are told by the calls' results; RtMidi's warnings (no port…) are not printed
static void quiet(RtMidiError::Type, const std::string &, void *) {}
// An error a call reports through the callback only (a virtual port: isPortOpen() stays false even when it is made)
static void record(RtMidiError::Type t, const std::string &m, void *error)
{
    if (t != RtMidiError::WARNING && t != RtMidiError::DEBUG_WARNING) *static_cast<std::string *>(error) = m;
}

template <class Port> static std::unique_ptr<Port> makePort()
{
    auto p = std::make_unique<Port>(RtMidi::UNSPECIFIED, kClient);
    p->setErrorCallback(&quiet);
    return p;
}

template <class Port> static QStringList portNames()
{
    QStringList out;
    try {
        auto p = makePort<Port>();
        const unsigned n = p->getPortCount();
        for (unsigned i = 0; i < n; ++i) {
            const QString name = QString::fromStdString(p->getPortName(i));
            if (!name.isEmpty() && !name.startsWith(QLatin1String(kClient) + QLatin1Char(':'))) out << name; // not ours
        }
    } catch (const RtMidiError &) {
    }
    return out;
}

// ALSA names its ports "Client:Port 20:0": the numbers change from a boot to the next, the names do not
static QString baseName(const QString &port)
{
    static const QRegularExpression numbers(QStringLiteral("\\s+\\d+:\\d+$"));
    return QString(port).remove(numbers);
}

// The port of that name (empty: the first one; else the same name but its numbers), -1 when there is none
template <class Port> static int portIndex(Port &p, const QString &name)
{
    const unsigned n = p.getPortCount();
    if (n == 0) return -1;
    if (name.isEmpty()) return 0;
    for (unsigned i = 0; i < n; ++i)
        if (QString::fromStdString(p.getPortName(i)) == name) return int(i);
    for (unsigned i = 0; i < n; ++i)
        if (baseName(QString::fromStdString(p.getPortName(i))) == baseName(name)) return int(i);
    return -1;
}

// What the user reads: no MIDI system (Linux without the ALSA sequencer), else RtMidi's words
static QString message(const RtMidiError &e)
{
    if (e.getType() == RtMidiError::DRIVER_ERROR) return QStringLiteral("no MIDI system on this machine");
    return QString::fromStdString(e.getMessage());
}

bool pxs::midi::available() { return true; }
void pxs::midi::setClientName(const QString &name) { clientName() = name.toUtf8(); }

bool pxs::midi::virtualPorts()
{
#if defined(__APPLE__) || defined(__linux__)
    return true;
#else
    return false;
#endif
}
QStringList pxs::midi::outputNames() { return portNames<RtMidiOut>(); }
QStringList pxs::midi::inputNames() { return portNames<RtMidiIn>(); }

struct pxs::midi::Output::Impl {
    std::unique_ptr<RtMidiOut> port;
    QString name;
};

pxs::midi::Output::Output() : d(std::make_unique<Impl>()) {}
pxs::midi::Output::~Output() = default;

bool pxs::midi::Output::open(const QString &name, QString *err)
{
    try {
        d->port = makePort<RtMidiOut>();
        const int i = portIndex(*d->port, name);
        if (i < 0) {
            if (err) *err = name.isEmpty() ? QStringLiteral("no MIDI output") : QStringLiteral("MIDI output \"%1\" not found").arg(name);
            d->port.reset();
            return false;
        }
        d->name = QString::fromStdString(d->port->getPortName(unsigned(i)));
        d->port->openPort(unsigned(i), kClient);
        if (!d->port->isPortOpen()) {
            if (err) *err = QStringLiteral("MIDI output \"%1\" refused").arg(d->name);
            d->port.reset();
            return false;
        }
        return true;
    } catch (const RtMidiError &e) {
        if (err) *err = message(e);
        d->port.reset();
        return false;
    }
}

bool pxs::midi::Output::openVirtual(const QString &name, QString *err)
{
    if (!virtualPorts()) {
        if (err) *err = QStringLiteral("no virtual MIDI ports on this system");
        return false;
    }
    try {
        d->port = makePort<RtMidiOut>();
        // RtMidi never counts a virtual port as open (isPortOpen() is about connections to others' ports): a failure
        // is only reported to the error callback
        std::string failed;
        d->port->setErrorCallback(&record, &failed);
        d->port->openVirtualPort(name.toStdString());
        d->port->setErrorCallback(&quiet);
        if (!failed.empty()) {
            if (err) *err = QStringLiteral("cannot make the virtual port \"%1\": %2").arg(name, QString::fromStdString(failed));
            d->port.reset();
            return false;
        }
        d->name = name;
        return true;
    } catch (const RtMidiError &e) {
        if (err) *err = message(e);
        d->port.reset();
        return false;
    }
}

void pxs::midi::Output::send(const std::vector<uint8_t> &msg)
{
    if (!d->port) return;
    try {
        d->port->sendMessage(msg.data(), msg.size());
    } catch (const RtMidiError &) {
    }
}

QString pxs::midi::Output::name() const { return d->name; }

struct pxs::midi::Input::Impl {
    std::unique_ptr<RtMidiIn> port;
    Callback cb;
    QString name;
    static void received(double, std::vector<unsigned char> *msg, void *self)
    {
        auto *impl = static_cast<Impl *>(self);
        if (msg && !msg->empty() && impl->cb) impl->cb(msg->data(), msg->size());
    }
};

pxs::midi::Input::Input() : d(std::make_unique<Impl>()) {}
pxs::midi::Input::~Input()
{
    if (d->port) {
        try {
            d->port->cancelCallback();
            d->port->closePort();
        } catch (const RtMidiError &) {
        }
    }
}

bool pxs::midi::Input::open(const QString &name, Callback cb, QString *err)
{
    try {
        d->port = makePort<RtMidiIn>();
        const int i = portIndex(*d->port, name);
        if (i < 0) {
            if (err) *err = name.isEmpty() ? QStringLiteral("no MIDI input") : QStringLiteral("MIDI input \"%1\" not found").arg(name);
            d->port.reset();
            return false;
        }
        d->cb = std::move(cb);
        d->port->openPort(unsigned(i), kClient);
        d->port->ignoreTypes(false, false, true); // SysEx (MTC full frames) and time code kept, active sensing not
        d->port->setCallback(&Impl::received, d.get());
        d->name = QString::fromStdString(d->port->getPortName(unsigned(i)));
        return true;
    } catch (const RtMidiError &e) {
        if (err) *err = message(e);
        d->port.reset();
        return false;
    }
}

QString pxs::midi::Input::name() const { return d->name; }

#else // no MIDI backend

bool pxs::midi::available() { return false; }
void pxs::midi::setClientName(const QString &) {}
bool pxs::midi::virtualPorts() { return false; }
QStringList pxs::midi::outputNames() { return {}; }
QStringList pxs::midi::inputNames() { return {}; }
struct pxs::midi::Output::Impl {};
pxs::midi::Output::Output() : d(std::make_unique<Impl>()) {}
pxs::midi::Output::~Output() = default;
bool pxs::midi::Output::open(const QString &, QString *err)
{
    if (err) *err = QStringLiteral("built without MIDI");
    return false;
}
bool pxs::midi::Output::openVirtual(const QString &, QString *err)
{
    if (err) *err = QStringLiteral("built without MIDI");
    return false;
}
void pxs::midi::Output::send(const std::vector<uint8_t> &) {}
QString pxs::midi::Output::name() const { return {}; }
struct pxs::midi::Input::Impl {};
pxs::midi::Input::Input() : d(std::make_unique<Impl>()) {}
pxs::midi::Input::~Input() = default;
bool pxs::midi::Input::open(const QString &, Callback, QString *err)
{
    if (err) *err = QStringLiteral("built without MIDI");
    return false;
}
QString pxs::midi::Input::name() const { return {}; }

#endif
