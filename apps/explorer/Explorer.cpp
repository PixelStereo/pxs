#include "Explorer.h"

#include <QNetworkInterface>

using namespace pxs;
using pxs::controls::Control;

namespace {
// control/<key>: lower case, letters and digits, the rest "_"; numbered when taken
QString makeKey(const QString &name, const QStringList &taken, int index)
{
    QString key;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 128) key += c;
        else if (!key.isEmpty() && !key.endsWith(QLatin1Char('_'))) key += QLatin1Char('_');
    }
    while (key.endsWith(QLatin1Char('_'))) key.chop(1);
    if (key.isEmpty()) key = QStringLiteral("p%1").arg(index);
    QString unique = key;
    for (int n = 2; taken.contains(unique); ++n) unique = QStringLiteral("%1_%2").arg(key).arg(n);
    return unique;
}
} // namespace

Explorer::Explorer(QObject *parent) : QObject(parent), m_discovery(this)
{
    m_server = new controls::Server(this, this);
    m_server->setName(QStringLiteral("Explorer"));
    m_server->setPageTitle(QStringLiteral("OSCQuery Explorer"));
    connect(&m_discovery, &oscquery::Discovery::deviceFound, this, [this](const oscquery::Device &d) {
        Dev *dev = device(addDevice(d.name, d.address.toString(), d.port));
        dev->online = true;
        emit devicesChanged();
    });
    connect(&m_discovery, &oscquery::Discovery::deviceLost, this, [this](const oscquery::Device &d) {
        for (auto &dev : m_devices)
            if (dev->name == d.name) dev->online = false;
        emit devicesChanged();
    });
}

Explorer::~Explorer()
{
    m_server->stop();
}

Explorer::Dev *Explorer::device(quint64 id) const
{
    for (const auto &d : m_devices)
        if (d->id == id) return d.get();
    return nullptr;
}

quint64 Explorer::addDevice(const QString &name, const QString &host, quint16 port)
{
    for (const auto &d : m_devices)
        if (d->name == name || (d->host == host && d->port == port)) {
            d->host = host; // the same device, announced again (maybe elsewhere)
            d->port = port;
            return d->id;
        }
    auto dev = std::make_unique<Dev>();
    dev->id = m_nextDevice++;
    dev->name = name;
    dev->host = host;
    dev->port = port;
    const quint64 id = dev->id;
    m_devices.push_back(std::move(dev));
    emit devicesChanged();
    return id;
}

void Explorer::openDevice(quint64 id)
{
    Dev *d = device(id);
    if (!d) return;
    if (d->client && d->client->state() != oscquery::Client::State::Failed) {
        if (d->client->state() == oscquery::Client::State::Ready) emit deviceReady(id);
        return;
    }
    d->client = std::make_unique<oscquery::Client>();
    oscquery::Client *c = d->client.get();
    connect(c, &oscquery::Client::ready, this, [this, id] {
        if (Dev *dev = device(id)) {
            if (!dev->client->name().isEmpty() && dev->name.isEmpty()) dev->name = dev->client->name();
            dev->online = true;
        }
        listenAll(id);
        changed(); // the ranges and choices of its views
        emit deviceReady(id);
        emit devicesChanged();
    });
    connect(c, &oscquery::Client::treeChanged, this, [this, id] {
        changed();
        emit deviceTreeChanged(id);
    });
    connect(c, &oscquery::Client::failed, this, [this, id](const QString &why) {
        if (Dev *dev = device(id)) dev->online = false;
        emit deviceFailed(id, why);
        emit devicesChanged();
    });
    c->open(d->host, d->port);
}

void Explorer::listenAll(quint64 deviceId)
{
    Dev *d = device(deviceId);
    if (!d || !d->client) return;
    for (const Control &c : m_controls) {
        if (c.boundScope != deviceId || c.boundParam.isEmpty()) continue;
        if (c.kind == Control::Kind::Color) continue;
        d->client->listen(c.boundParam);
    }
}

quint64 Explorer::addControl(quint64 deviceId, const QString &path, quint64 group)
{
    Dev *d = device(deviceId);
    if (!d || !d->client || d->client->state() != oscquery::Client::State::Ready) return 0;
    std::optional<Control> c = controls::forParameter(*this, deviceId, path, group);
    if (!c) return 0;
    const quint64 id = controls::add(m_controls, m_nextControl, *c, makeKey);
    if (!id) return 0;
    d->client->listen(path);
    changed();
    return id;
}

// The page

bool Explorer::serve(bool network, const QString &pin)
{
    m_server->stop();
    return m_server->start(0, pin, network ? QString() : QStringLiteral("127.0.0.1"));
}

QString Explorer::pageUrl() const
{
    return m_server->isRunning() ? QStringLiteral("http://127.0.0.1:%1/control").arg(m_server->port()) : QString();
}

QStringList Explorer::networkUrls() const { return m_server->urls(); }

// Backend

std::vector<Control> Explorer::controls() const
{
    std::vector<Control> list = m_controls;
    for (Control &c : list)
        if (!c.boundParam.isEmpty()) controls::fillBound(c, *this);
    return list;
}

std::optional<Control> Explorer::control(quint64 id) const
{
    const Control *c = controls::find(m_controls, id);
    if (!c) return std::nullopt;
    Control out = *c;
    if (!out.boundParam.isEmpty()) controls::fillBound(out, *this);
    return out;
}

bool Explorer::setValue(quint64 id, double value)
{
    const Control *c = controls::find(m_controls, id);
    if (!c || c->kind == Control::Kind::Color || c->kind == Control::Kind::Group || !std::isfinite(value)) return false;
    if (!c->boundParam.isEmpty()) return controls::setBound(*c, *this, value);
    return controls::setValue(m_controls, id, value, {});
}

bool Explorer::setPoint(quint64 id, double x, double y) { return controls::setPoint(m_controls, id, x, y, {}); }
bool Explorer::setPoints(quint64 id, const std::vector<double> &xy) { return controls::setPoints(m_controls, id, xy, {}); }
bool Explorer::setMulti(quint64 id, const std::vector<double> &v) { return controls::setMulti(m_controls, id, v, {}); }
bool Explorer::setAdsr(quint64 id, const std::array<double, 4> &v) { return controls::setAdsr(m_controls, id, v, {}); }

bool Explorer::setColor(quint64 id, double r, double g, double b)
{
    Control *c = controls::find(m_controls, id);
    if (!c || c->kind != Control::Kind::Color) return false;
    return c->boundParam.isEmpty() ? controls::setColor(*c, r, g, b, {}) : controls::setBoundColor(*c, *this, r, g, b);
}

bool Explorer::setLayouts(const QHash<quint64, std::array<int, 4>> &places)
{
    const bool did = controls::setLayouts(m_controls, places);
    if (did) changed();
    return did;
}

bool Explorer::update(quint64 id, const Control &change)
{
    if (!controls::update(m_controls, id, change, makeKey).found) return false;
    changed();
    return true;
}

bool Explorer::remove(quint64 id)
{
    const Control *c = controls::find(m_controls, id);
    if (!c) return false;
    const quint64 scope = c->boundScope;
    const QString path = c->boundParam;
    if (!controls::remove(m_controls, id)) return false;
    if (scope && !path.isEmpty()) { // nothing else shows it: not listened to any more
        const bool used = std::any_of(m_controls.begin(), m_controls.end(), [&](const Control &o) { return o.boundScope == scope && o.boundParam == path; });
        if (!used)
            if (Dev *d = device(scope); d && d->client) d->client->ignore(path);
    }
    changed();
    return true;
}

// ParameterSource

bool Explorer::info(quint64 scope, const QString &path, ParamInfo *out) const
{
    const Dev *d = device(scope);
    if (!d || !d->client) return false;
    const oscquery::RemoteNode *n = d->client->tree().find(path);
    return n && n->toParamInfo(out);
}

QJsonValue Explorer::value(quint64 scope, const QString &path) const
{
    const Dev *d = device(scope);
    const oscquery::RemoteNode *n = d && d->client ? d->client->tree().find(path) : nullptr;
    return n && !n->value.isEmpty() ? n->value[0] : QJsonValue(QJsonValue::Undefined);
}

bool Explorer::setNumber(quint64 scope, const QString &path, double v)
{
    Dev *d = device(scope);
    return d && d->client && d->client->write(path, QJsonArray{v});
}

bool Explorer::setJson(quint64 scope, const QString &path, const QJsonValue &v)
{
    Dev *d = device(scope);
    if (!d || !d->client) return false;
    const oscquery::RemoteNode *n = d->client->tree().find(path);
    if (n && n->type == QLatin1String("N")) return d->client->write(path, {});
    return d->client->write(path, QJsonArray{v});
}
