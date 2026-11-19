#pragma once
// The server of a controls page: a small web server an application starts so that a phone, a tablet, a browser or one of its own windows
// can lay out and play its controls (ControlsPage.h), live both ways. HTTP and WebSocket by hand, nothing else installed.
//
// What it is, and is not: it moves the controls the application gives it (Backend) and nothing else. It asks a code (the "PIN": 4 to 32
// letters or digits, chosen by the operator, or none), compared in constant time; a machine that gets it wrong eight times is refused for a
// minute. A WebSocket is accepted only from the page this server served (same origin); the page's script and style are the only code its
// Content-Security-Policy allows. No encryption: the PIN and the values cross the network in clear, as OSC does.
//
// Protocol (text frames, JSON), for those who want their own client:
//   GET /info          {"pin":true|false}: whether a code is asked
//   GET /ws?pin=<PIN>  upgrade to a WebSocket (no pin= when none is asked)
//   server -> {"t":"controls","title":..,"show":true|false,"list":[Control::wireJson()…]}   at connection, and when a control is added, removed or shaped
//   server -> {"t":"values","v":{"<id>":<number>|[numbers]}}              what changed
//   client -> {"t":"set","id":"<id>","v":<number>}   {"t":"color","id":..,"rgb":[r,g,b]}   {"t":"point","id":..,"xy":[x,y]}
//             {"t":"points","id":..,"v":[x1,y1,x2,y2…]}   {"t":"multi","id":..,"v":[…]}   {"t":"adsr","id":..,"v":[a,d,s,r]}
//   client -> {"t":"layout","items":{"<id>":{"x","y","w","h"}}}   {"t":"edit","id":..,"props":{…}} (Control::applyProps)   {"t":"remove","id":..}
// An application adds its own pages and messages through the hooks (servePage, message, tick).
#include "Control.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <array>
#include <optional>
#include <vector>

class QTcpServer;
class QTcpSocket;

namespace pxs::controls {

// What the server asks of the application: its controls, as they are now, and what the page sets. Every setter says whether something
// changed. Called on the server's thread: an application with another thread guards its own state.
class Backend
{
public:
    virtual ~Backend() = default;
    virtual std::vector<Control> controls() const = 0; // with their values as they are now
    virtual std::optional<Control> control(quint64 id) const = 0;
    virtual quint64 revision() const = 0;              // changes with the list or a definition, not with a value
    virtual QString title() const = 0;
    virtual bool setValue(quint64 id, double value) = 0;
    virtual bool setPoint(quint64 id, double x, double y) = 0;
    virtual bool setPoints(quint64 id, const std::vector<double> &xy) = 0;
    virtual bool setMulti(quint64 id, const std::vector<double> &values) = 0;
    virtual bool setAdsr(quint64 id, const std::array<double, 4> &values) = 0;
    virtual bool setColor(quint64 id, double r, double g, double b) = 0;
    virtual bool setLayouts(const QHash<quint64, std::array<int, 4>> &places) = 0; // id -> x, y, w, h
    virtual bool update(quint64 id, const Control &changed) = 0;                    // a control's settings changed on the page
    virtual bool remove(quint64 id) = 0;
};

class Server : public QObject
{
    Q_OBJECT
public:
    explicit Server(Backend *backend = nullptr, QObject *parent = nullptr);
    ~Server() override;

    // Port 0: chosen by the system. `pin` empty: no code asked. `listen`: the interface's address ("": all). False with status() when it cannot
    bool start(quint16 port, const QString &pin, const QString &listen = QString());
    void stop();
    bool isRunning() const;
    quint16 port() const;
    QString status() const { return m_status; }
    int clientCount() const;
    virtual QStringList urls() const; // where to point a phone: one address per network interface
    static QString makePin(int digits = 6);
    static bool pinValid(const QString &pin); // 4 to 32 letters or digits (an empty one is "no code": start() takes it)
    void setName(const QString &name) { m_name = name; }           // what the status calls it ("Remote")
    void setPageTitle(const QString &title) { m_pageTitle = title; } // the page's title in a browser's tab

    // The controls page as served, and the policy that lets only its code run
    static QByteArray page(const QString &title);
    static QByteArray policy(const QByteArray &script, const QByteArray &style);

signals:
    void clientsChanged(int count);
    void layoutEdited(); // a client laid the page out, set a tile or removed a control: the application is modified
    void controlMoved(); // a control was set from a client

protected:
    // Hooks: another page of the application (true: served as `html` under `csp`), another message of a client (true: taken), what to do
    // every 50 ms before the controls, a client gone
    virtual bool hasShowPage() const { return false; } // the application serves a /show page: the page links to it
    virtual bool servePage(const QString &path, QByteArray *html, QByteArray *csp);
    virtual bool message(QTcpSocket *client, const QString &type, const QJsonObject &o);
    virtual void tick();
    virtual void clientGone(QTcpSocket *client);
    void setBackend(Backend *backend) { m_backend = backend; } // for an application that makes its backend after the server
    void sendText(QTcpSocket *client, const QByteArray &json) const;
    QList<QTcpSocket *> websockets() const;
    void leaveControls(QTcpSocket *client); // this client has pages of its own: the controls are not pushed to it
    static QByteArray response(int status, const QByteArray &body, const QByteArray &type, const QByteArray &extra = {});

private:
    struct Client {
        QTcpSocket *socket = nullptr;
        QByteArray buffer;
        bool websocket = false;
        bool ownPages = false;                 // see leaveControls
        quint64 controlsSeen = ~0ull;
        QHash<quint64, std::vector<double>> sent; // the values the client has
        double budget = 300;                   // messages it may still send this second
    };
    struct Attempts {
        int failures = 0;
        qint64 until = 0; // ms since epoch
    };
    void onConnection();
    void onData(QTcpSocket *s);
    void onFrame(Client &c, int opcode, const QByteArray &payload);
    void push();
    void send(QTcpSocket *s, const QByteArray &frame) const;
    QByteArray controlsMessage(const std::vector<Control> &controls) const;
    bool authorized(const QByteArray &presented) const;

    Backend *m_backend;
    QTcpServer *m_server = nullptr;
    QHash<QTcpSocket *, Client> m_clients;
    QTimer m_timer;
    QString m_pin, m_status, m_name = QStringLiteral("Remote"), m_pageTitle = QStringLiteral("Controls");
    QByteArray m_csp;
    QHash<QString, Attempts> m_attempts; // by address
};

} // namespace pxs::controls
