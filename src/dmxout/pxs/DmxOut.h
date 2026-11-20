#pragma once
// The DMX outputs of an application, the same everywhere: what an output is (its settings, validated and saved as JSON), the
// routing of an application's universes onto the outputs, and the Sender that streams them on a thread of its own, steadily, whatever
// the application's frame rate. Art-Net and sACN. Settings are modelled on what lighting consoles and QLC+ offer: unicast or the
// protocol's group (Art-Net broadcast, sACN multicast), the network card, a port, a rate, an sACN priority and source name, full or
// partial frames, and a map from the application's universes to the network's. Qt Core and Network.
#include <pxs/Dmx.h>

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace pxs::dmx {

enum class Protocol { ArtNet, Sacn };
// Unicast: to each address. Group: Art-Net broadcasts on the card's network, sACN sends to each universe's multicast group
enum class Delivery { Unicast, Group };
// Full: 512 channels in every packet. Partial: only the channels the application hands over (its used ones): less traffic, for the
// receivers that take a short frame
enum class Transmission { Full, Partial };

struct Output {
    quint64 id = 0; // the application's: frames are addressed to it
    QString name;
    bool enabled = true;
    Protocol protocol = Protocol::ArtNet;
    Delivery delivery = Delivery::Group;
    QStringList destinations;  // Unicast: IPv4 addresses
    QString interfaceAddress;  // the local IPv4 address to send from (a network card); empty: the system routes
    int port = 0;              // 0: the protocol's (Art-Net 6454, sACN 5568)
    double rate = 40;          // frames per second and universe, 1 to maxRate()
    int priority = 100;        // sACN, 0..200
    QString sourceName = QStringLiteral("pxs"); // sACN
    Transmission transmission = Transmission::Full;
    // From the application's universes to the network's, see `route`: its universes firstSource... for `universeCount` (0: all
    // from the first) are sent as firstUniverse... Art-Net's first universe is 0, sACN's 1: `defaults` says
    int firstSourceUniverse = 1, firstUniverse = 0, universeCount = 0;

    // A new output of a protocol, with the settings that suit it
    static Output defaults(Protocol protocol);
    int effectivePort() const { return port > 0 ? port : protocol == Protocol::ArtNet ? artNetPort : sacnPort; }
    int highestUniverse() const { return protocol == Protocol::ArtNet ? 32767 : 63999; }
    double maxRate() const { return protocol == Protocol::ArtNet ? 44 : 120; } // Art-Net's limit; sACN has none, LED pixels want more
    // Why it cannot send, or empty: the rate, priority, port, card and addresses. The Sender opens an output only if this is empty.
    QString validate() const;
    // Why the universe map is not usable, or empty. An application that addresses network universes itself (`route` unused) has no
    // map to check.
    QString validateMapping() const;
    bool operator==(const Output &other) const;
    bool operator!=(const Output &other) const { return !(*this == other); }
    QJsonObject toJson() const;
    static Output fromJson(const QJsonObject &json); // what is missing takes the protocol's default
};

// The universes to send: by output and network universe, the levels (Partial: as many bytes as channels to send; Full: padded)
using Universes = std::map<std::pair<quint64, int>, QByteArray>;

// The application's universes (its numbering) onto each enabled output's: the output's range, shifted to its first network universe.
// An application that already knows each output's universes builds `Universes` itself and skips this.
Universes route(const std::map<int, QByteArray> &sourceUniverses, const std::vector<Output> &outputs);

struct OutputState {
    enum Level { Off, Ok, Error };
    Level level = Off;
    QString text;        // the error
    quint64 packets = 0; // sent since the output started
};

class Sender
{
public:
    Sender();
    ~Sender();
    Sender(const Sender &) = delete;
    Sender &operator=(const Sender &) = delete;

    // --- Any thread
    // The outputs as they are now (cheap when nothing changed): an output that changed or went off ends its sACN streams
    void follow(const std::vector<Output> &outputs);
    // The latest levels, sent at each output's rate until the next ones; `owned`: for sACN, the channels that are ours (a non-zero
    // byte), which get a per-channel priority about once a second (0xDD), 0 for the others so that a receiver takes them from
    // another source
    void submit(Universes frame, Universes owned = {});
    void stop(); // the sACN streams terminated, the thread joined; follow() starts it again
    OutputState state(quint64 output) const;
    // An sACN source's identity: random, or the application's own to keep it stable between runs
    QUuid cid() const;
    void setCid(const QUuid &cid);

private:
    struct Link;
    void run();
    void ensureThread();

    QUuid m_cid;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<Output> m_outputs;                 // (m_mutex)
    std::shared_ptr<const Universes> m_frame;      // (m_mutex)
    std::shared_ptr<const Universes> m_owned;      // (m_mutex)
    std::map<quint64, OutputState> m_states;       // (m_mutex)
    bool m_quit = false;                           // (m_mutex)
    bool m_changed = false;                        // outputs changed (m_mutex)
    std::thread m_thread;
};

} // namespace pxs::dmx
