#pragma once

// What an application needs to be reachable like every other Pixel Stereo application: an OSC port (UDP) that applies
// what it receives to the tree, the OSCQuery HTTP side, and the announcement on the local network (mDNS). What the
// application says (a signal fired, a parameter that changed, see Tree::setOutput) goes back over UDP to the addresses
// that wrote to it and to those added with addTarget().

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

    // Who gets the feedback and the signals besides the senders (learned from what they send; switch off for none)
    void addTarget(const QHostAddress &address, quint16 port);
    void clearTargets();
    void setLearnSenders(bool on) { m_learn = on; }

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
    QList<QPair<QHostAddress, quint16>> m_targets;
    bool m_learn = true;
};

} // namespace pxs
