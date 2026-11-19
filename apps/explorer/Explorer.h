#pragma once
// The explorer's model: the OSCQuery devices of the network (found, or typed in), a client for each one that is open, and the page of controls
// built from their parameters. It is the page's Backend (pxs/Server.h: the controls, as they are now, and what the page sets) and the
// ParameterSource of the controls that are views of a parameter (scope: the device's id), so a tile shows and sets a remote parameter.
#include <pxs/Client.h>
#include <pxs/ControlList.h>
#include <pxs/Discovery.h>
#include <pxs/Server.h>

#include <QObject>
#include <memory>
#include <vector>

class Explorer : public QObject, public pxs::controls::Backend, public pxs::controls::ParameterSource
{
    Q_OBJECT
public:
    struct Dev {
        quint64 id = 0;
        QString name, host;
        quint16 port = 0;
        bool online = false; // announced on the network, or typed in and reachable
        std::unique_ptr<pxs::oscquery::Client> client;
    };

    explicit Explorer(QObject *parent = nullptr);
    ~Explorer() override;

    const std::vector<std::unique_ptr<Dev>> &devices() const { return m_devices; }
    Dev *device(quint64 id) const;
    quint64 addDevice(const QString &name, const QString &host, quint16 port); // the same one again: the one known
    void openDevice(quint64 id); // connects; deviceReady() says when its tree is there
    bool startSearch() { return m_discovery.start(); }

    // A control that is a view of this parameter of this device (false: it has no such parameter, or not one a control can show)
    quint64 addControl(quint64 deviceId, const QString &path, quint64 group = 0);

    // The page: served on the loopback (and, when asked, to the network)
    bool serve(bool network, const QString &pin);
    QString pageUrl() const;
    QStringList networkUrls() const;
    pxs::controls::Server &server() { return *m_server; }

    // Backend
    std::vector<pxs::controls::Control> controls() const override;
    std::optional<pxs::controls::Control> control(quint64 id) const override;
    quint64 revision() const override { return m_revision; }
    QString title() const override { return QStringLiteral("OSCQuery Explorer"); }
    bool setValue(quint64 id, double value) override;
    bool setPoint(quint64 id, double x, double y) override;
    bool setPoints(quint64 id, const std::vector<double> &xy) override;
    bool setMulti(quint64 id, const std::vector<double> &values) override;
    bool setAdsr(quint64 id, const std::array<double, 4> &values) override;
    bool setColor(quint64 id, double r, double g, double b) override;
    bool setLayouts(const QHash<quint64, std::array<int, 4>> &places) override;
    bool update(quint64 id, const pxs::controls::Control &changed) override;
    bool remove(quint64 id) override;

    // ParameterSource: scope is a device's id
    bool info(quint64 scope, const QString &path, ParamInfo *out) const override;
    QJsonValue value(quint64 scope, const QString &path) const override;
    bool setNumber(quint64 scope, const QString &path, double v) override;
    bool setJson(quint64 scope, const QString &path, const QJsonValue &v) override;

signals:
    void devicesChanged();
    void deviceReady(quint64 id);
    void deviceTreeChanged(quint64 id);
    void deviceFailed(quint64 id, const QString &reason);
    void controlsChanged();

private:
    void changed() { ++m_revision; emit controlsChanged(); }
    void listenAll(quint64 deviceId);

    pxs::oscquery::Discovery m_discovery;
    std::vector<std::unique_ptr<Dev>> m_devices;
    std::vector<pxs::controls::Control> m_controls;
    quint64 m_nextControl = 1, m_nextDevice = 1, m_revision = 1;
    pxs::controls::Server *m_server = nullptr;
};
