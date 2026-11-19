#pragma once
// A client of one OSCQuery device: reads its description (HOST_INFO, then the tree), follows the values it is asked to
// (LISTEN over the device's WebSocket; without WebSockets, or on a device that does not push, the values are asked for a few times a
// second), and writes to it over OSC (UDP, to the OSC_PORT its HOST_INFO gives). The tree and the values are in tree().
#include "RemoteTree.h"

#include <OscCodec.h>

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUdpSocket>

#ifdef PXS_HAS_WEBSOCKETS
class QWebSocket;
#endif

namespace pxs::oscquery {

class Client : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Connecting, Ready, Failed };

    explicit Client(QObject *parent = nullptr);
    ~Client() override;

    // `host`: a name or an address; the device's OSCQuery HTTP port. The tree arrives with ready()
    void open(const QString &host, quint16 httpPort);
    void close();
    State state() const { return m_state; }
    const RemoteTree &tree() const { return m_tree; }
    QString name() const { return m_name; }        // HOST_INFO NAME
    QString host() const { return m_host; }
    quint16 oscPort() const { return m_oscPort; }
    bool pushes() const { return m_pushes; }       // the device sends the values it is asked to listen to (else they are polled)

    // The values of these addresses are followed: valueChanged() says each change
    void listen(const QString &path);
    void ignore(const QString &path);
    // Writes: the arguments of a node, over OSC (false: no such leaf, not writable, or not ready)
    bool write(const QString &path, const QJsonArray &values);
    void refresh(); // the tree again

    // The values of a node's arguments as OSC arguments of its type tags, and back (public for tests)
    static osc::Message toMessage(const RemoteNode &node, const QJsonArray &values);
    static QJsonArray toJson(const osc::Message &message);

signals:
    void ready();                                                  // the tree is here
    void treeChanged();                                            // and it changed
    void valueChanged(const QString &path, const QJsonArray &value);
    void failed(const QString &reason);

private:
    void get(const QString &target, std::function<void(const QJsonDocument &)> done, std::function<void()> missing = {});
    void fail(const QString &reason);
    void openSocket(quint16 port);
    void poll();
    void onPacket(const QByteArray &packet);

    QNetworkAccessManager m_net;
    QUdpSocket m_udp;
    QTimer m_poll;
    State m_state = State::Idle;
    RemoteTree m_tree;
    QString m_host, m_name;
    quint16 m_httpPort = 0, m_oscPort = 0;
    bool m_pushes = false;
    int m_generation = 0; // answers of an earlier open() are dropped
    QSet<QString> m_listening;
    QHostAddress m_address;
#ifdef PXS_HAS_WEBSOCKETS
    QWebSocket *m_socket = nullptr;
#endif
};

} // namespace pxs::oscquery
