#pragma once

// What an application needs to be reachable like every other Pixel Stereo application: an OSC port (UDP) that applies
// what it receives to the tree, the OSCQuery HTTP side, and the announcement on the local network (mDNS). What the
// application says goes back over UDP, see Tree::setOutput:
//   a parameter that changed (the feedback) -> the senders that wrote to it, and the targets
//   a signal fired                          -> the targets only (a stream: nobody gets it by merely writing a parameter)

#include <pxs/MdnsResponder.h>
#include <pxs/QueryServer.h>

#include <QHostAddress>
#include <QUdpSocket>

namespace pxs {

class OscService : public QObject {
    Q_OBJECT
public:
    OscService(Tree *tree, const QString &name, QObject *parent = nullptr);
    ~OscService() override;

    // oscPort/httpPort 0: any free port. False (with a log message) when one cannot be opened.
    bool start(quint16 oscPort, quint16 httpPort, bool announce = true);
    void stop();
    bool isRunning() const { return m_udp.state() == QAbstractSocket::BoundState; }
    quint16 oscPort() const { return m_udp.localPort(); }
    quint16 httpPort() const { return m_http.port(); }

    // Where the signals go (and the feedback too): the application's own destinations
    void addTarget(const QHostAddress &address, quint16 port);
    void setTargets(const QList<QPair<QHostAddress, quint16>> &targets) { m_targets = targets; }
    void clearTargets();
    // The feedback also goes to whoever wrote to the application, on the port it wrote from (on by default)
    void setLearnSenders(bool on) { m_learn = on; if (!on) m_learned.clear(); }

    QueryServer &query() { return m_http; }

signals:
    void messageReceived(const osc::Message &message, int accepted); // after it was applied to the tree
    void logMessage(const QString &text);

private:
    void onReadyRead();
    void send(const osc::Message &message);

    Tree *m_tree;
    QString m_name;
    QUdpSocket m_udp;
    QueryServer m_http;
    MdnsResponder m_mdns;
    QList<QPair<QHostAddress, quint16>> m_targets, m_learned;
    bool m_learn = true;
};

} // namespace pxs
