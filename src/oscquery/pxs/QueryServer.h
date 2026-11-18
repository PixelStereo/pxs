#pragma once

// The HTTP side of OSCQuery, answered from a pxs::Tree: GET <osc path>[?VALUE|?TYPE|...] and GET /?HOST_INFO, in JSON.
// Status: 200, 204 (the attribute has no value), 400, 404, 405. Read-only; CORS open so that a web page can ask.

#include <pxs/Tree.h>

#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace pxs {

class QueryServer : public QObject {
    Q_OBJECT
public:
    explicit QueryServer(const Tree *tree, QObject *parent = nullptr);

    bool start(quint16 port); // 0: any free port
    void stop();
    bool isListening() const { return m_server.isListening(); }
    quint16 port() const { return m_server.serverPort(); }

    void setName(const QString &name) { m_name = name; }
    void setOscPort(quint16 port) { m_oscPort = port; }
    void setExtensions(const QJsonObject &extensions) { m_extensions = extensions; }

    // The HTTP answer to a request line target, public for tests
    struct Response {
        int status = 200;
        QByteArray body;
    };
    Response answer(const QString &method, const QByteArray &target) const;

signals:
    void logMessage(const QString &text);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);

    const Tree *m_tree;
    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QString m_name = QStringLiteral("pxs");
    quint16 m_oscPort = 0;
    QJsonObject m_extensions = Tree::defaultExtensions();
};

} // namespace pxs
