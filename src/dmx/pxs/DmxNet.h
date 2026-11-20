#pragma once
// DMX on the network, above the frames of Dmx.h: a Monitor that listens to Art-Net and sACN and keeps what every source sends,
// the merge of those sources into the level of a universe (sACN priority first, then HTP or LTP), and a Transmitter that sends
// a universe and tells the Monitor which frames are the application's own. From DMXPulse. Qt Core and Network.
#include "Dmx.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QMap>
#include <QNetworkInterface>
#include <QObject>
#include <QSet>
#include <QUdpSocket>
#include <array>

namespace pxs::dmx {

enum class MergeMode { HTP = 0, LTP = 1 }; // highest level wins; last source to change a channel wins

// What one source sends: an Art-Net sender is its address and port, an sACN source is its CID, each on its universe
struct Stream {
    Frame frame;           // the last one
    QString ip;            // "192.168.1.20:6454" (Art-Net), "192.168.1.20" (sACN)
    qint64 seen = 0;       // Monitor::elapsed() when it came
    quint64 packets = 0;
    bool own = false;      // sent by this application's Transmitter
    std::array<quint64, channelCount> changed{}; // when each channel last changed (an arrival counter)
};

class Transmitter : public QObject
{
    Q_OBJECT
public:
    explicit Transmitter(QObject *parent = nullptr);
    void setPorts(quint16 artNet, quint16 sacn) { m_artPort = artNet; m_sacnPort = sacn; }
    QByteArray cid() const { return m_cid; } // an sACN source's identity; random until set
    void setCid(const QByteArray &cid) { m_cid = cid.left(16); }
    // Binds to the interface's first IPv4 address (multicast out of that interface, TTL 1, looped back to this machine)
    bool start(const QNetworkInterface &network, QString *error);
    void stop();
    bool active() const { return m_socket.state() == QAbstractSocket::BoundState; }
    // Art-Net: to `destination` (unicast or broadcast). sACN: to the universe's multicast group. The sequence number is
    // the Transmitter's; the CID is set here. A terminated sACN frame ends the stream (sent three times by `end`).
    bool send(Frame frame, const QHostAddress &destination, QString *error);
    bool end(Frame frame, QString *error); // sACN: the stream of that universe stops
    quint8 lastSequence() const { return m_sequence; } // the number of the last frame sent
    QHostAddress lastTarget() const { return m_lastTarget; }
    quint16 lastPort() const { return m_lastPort; }       // where the last frame went
    QHostAddress localAddress() const { return m_socket.localAddress(); }
    quint16 localPort() const { return m_socket.localPort(); }

private:
    QUdpSocket m_socket;
    QNetworkInterface m_network;
    QByteArray m_cid;
    quint8 m_sequence = 0;
    quint16 m_artPort = artNetPort, m_sacnPort = sacnPort;
    QHostAddress m_lastTarget;
    quint16 m_lastPort = 0;
};

class Monitor : public QObject
{
    Q_OBJECT
public:
    explicit Monitor(QObject *parent = nullptr);
    void setPorts(quint16 artNet, quint16 sacn) { m_artPort = artNet; m_sacnPort = sacn; }
    // The Transmitter whose frames are the application's own: its CID, or its address and port, mark a stream `own`
    void setTransmitter(const Transmitter *transmitter) { m_transmitter = transmitter; }
    // The name an own stream goes by in the source list and the log ("DMXPulse output")
    void setOwnLabel(const QString &label) { m_ownLabel = label; }

    // Listens on that interface: Art-Net on its port (the loopback only on a loopback interface, plus the interface's own
    // address, so that local unicast is not lost to another application), sACN on its port. Several applications can share
    // the ports. False with the reason when a socket cannot be bound.
    bool start(const QNetworkInterface &network, QString *error);
    void stop();
    bool listening() const { return m_sacn.state() == QAbstractSocket::BoundState; }
    // The sACN universe to receive (its multicast group is joined, the one before is left; 0: none)
    bool listenTo(int sacnUniverse, QString *error);
    QString artNetAddress() const { return m_art.localAddress().toString(); }

    const QMap<QString, Stream> &streams() const { return m_streams; }
    qint64 elapsed() const { return m_clock.elapsed(); }
    static constexpr qint64 staleMs = 2500;       // a stream not heard for that long is not live
    static constexpr qint64 forgetMs = 60000;     // not heard for that long: forgotten
    static constexpr int maxStreams = 2048;
    bool live(const Stream &stream) const { return elapsed() - stream.seen < staleMs; }
    void prune(); // forgets the old streams

    // The level of a universe as the sources that are live make it: sACN priority first (only the highest priority's sources
    // count), then HTP or LTP. Empty without a live source; `live` says whether there is one. `externalOnly` leaves out the
    // application's own.
    QByteArray merged(bool sacn, int universe, MergeMode mode, bool externalOnly, bool *live = nullptr) const;

    struct Stats {
        quint64 datagrams = 0, received = 0, invalid = 0, otherInterface = 0, reordered = 0;
    };
    Stats stats() const { return m_stats; }

    // One datagram, as the sockets give it (public for tests and for an application that reads its own sockets). `interface`
    // is the one it arrived on (0: unknown). True when it was taken in.
    bool ingest(const QByteArray &data, const QHostAddress &from, quint16 port, bool sacn, uint interfaceIndex = 0);

signals:
    void event(const QString &line);   // one line for the application's log: "RX · Art-Net · U0 · 192… · seq 3 · 1=255"
    void problem(const QString &text); // something that does not stop it (a local unicast listener unavailable…)

private:
    void drain(QUdpSocket *socket, bool sacn);
    QUdpSocket m_art, m_artUnicast, m_sacn;
    QNetworkInterface m_network;
    QMap<QString, Stream> m_streams;
    QElapsedTimer m_clock;
    QSet<int> m_groups;
    quint64 m_arrival = 0;
    quint16 m_artPort = artNetPort, m_sacnPort = sacnPort;
    const Transmitter *m_transmitter = nullptr;
    QString m_ownLabel = QStringLiteral("output");
    Stats m_stats;
    QSet<QUdpSocket *> m_draining;
};

} // namespace pxs::dmx
