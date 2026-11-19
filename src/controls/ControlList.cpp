#include "pxs/ControlList.h"

#include <algorithm>
#include <cmath>

namespace pxs::controls {

using Kind = Control::Kind;

const Control *find(const std::vector<Control> &list, quint64 id)
{
    for (const Control &c : list)
        if (c.id == id) return &c;
    return nullptr;
}

Control *find(std::vector<Control> &list, quint64 id)
{
    for (Control &c : list)
        if (c.id == id) return &c;
    return nullptr;
}

void keyAll(std::vector<Control> &list, const KeyFn &key)
{
    QStringList taken;
    for (const Control &c : list)
        if (!c.key.isEmpty()) taken << c.key;
    int index = 0;
    for (Control &c : list) {
        ++index;
        if (!c.key.isEmpty()) continue;
        c.key = key(c.name, taken, index);
        taken << c.key;
    }
}

// A control's group must be a Group of the list that is not itself nor inside it
void tidyGroups(std::vector<Control> &list)
{
    for (Control &c : list) {
        c.span = std::clamp(c.span, 1, 4);
        c.columns = std::clamp(c.columns, 1, 4);
        const Control *g = c.group ? find(list, c.group) : nullptr;
        bool ok = g && g->kind == Kind::Group && g->id != c.id;
        for (int depth = 0; ok && g && g->group && depth < 64; ++depth) { // up the chain: it must not come back to this one
            if (g->group == c.id) ok = false;
            g = find(list, g->group);
        }
        if (!ok) c.group = 0;
    }
}

// The points and the bars: their count, and each within the range
static void tidyArrays(Control &c)
{
    if (c.kind == Kind::Points) {
        c.count = std::clamp(c.count, 1, Control::kPointsMax);
        if (!(c.max > c.min)) c.max = c.min + 1;
        c.def = std::clamp(c.def, c.min, c.max);
        c.points.resize(size_t(c.count), {c.def, c.def});
        for (auto &q : c.points) q = {std::clamp(q[0], c.min, c.max), std::clamp(q[1], c.min, c.max)};
    }
    if (c.kind == Kind::Multi) {
        c.count = std::clamp(c.count, 2, Control::kMultiMax);
        if (!(c.max > c.min)) c.max = c.min + 1;
        c.def = std::clamp(c.def, c.min, c.max);
        c.multi.resize(size_t(c.count), c.def);
        for (double &v : c.multi) v = std::clamp(v, c.min, c.max);
    }
}

void sanitizeNew(Control &c)
{
    if (c.name.trimmed().isEmpty()) c.name = Control::kindName(c.kind);
    c.value = c.kind == Kind::Slider ? std::clamp(c.value, std::min(c.min, c.max), std::max(c.min, c.max)) : c.clamped(c.value);
    if (c.kind == Kind::Pad) {
        c.value2 = c.clamped(c.value2);
        c.def = std::clamp(c.def, c.min, c.max);
    }
    if (c.kind == Kind::Choice && c.choices.isEmpty()) c.choices = {QStringLiteral("A"), QStringLiteral("B")};
    if (c.kind == Kind::Adsr) {
        c.min = 0;
        c.max = std::clamp(c.max > 1 ? c.max : 2.0, 0.1, 60.0); // the longest of its times
    }
    tidyArrays(c);
}

void sanitizeChange(Control &c, const Control &old)
{
    if (c.kind == Kind::Slider || c.kind == Kind::Pad) {
        if (!(c.max > c.min)) c.max = c.min + 1;
        c.def = std::clamp(c.def, c.min, c.max);
    }
    if (c.kind == Kind::Choice && c.choices.isEmpty()) c.choices = old.choices;
    c.value = c.kind == old.kind ? c.clamped(c.value) : (c.kind == Kind::Slider || c.kind == Kind::Pad ? c.def : 0.0);
    c.value2 = c.kind == Kind::Pad ? (c.kind == old.kind ? c.clamped(c.value2) : c.def) : 0.0;
    tidyArrays(c);
    if (c.kind == Kind::Adsr) {
        c.min = 0;
        c.max = std::clamp(c.max, 0.1, 60.0);
        for (int i = 0; i < 4; ++i) c.adsr[size_t(i)] = std::clamp(c.adsr[size_t(i)], 0.0, i == 2 ? 1.0 : c.max);
    }
}

quint64 add(std::vector<Control> &list, quint64 &nextId, const Control &control, const KeyFn &key)
{
    if (int(list.size()) >= kMaxControls) return 0;
    Control c = control;
    c.id = nextId++;
    c.key.clear();
    sanitizeNew(c);
    const quint64 id = c.id;
    list.push_back(std::move(c));
    keyAll(list, key);
    tidyGroups(list);
    return id;
}

Updated update(std::vector<Control> &list, quint64 id, const Control &change, const KeyFn &key)
{
    Updated out;
    auto it = std::find_if(list.begin(), list.end(), [id](const Control &c) { return c.id == id; });
    if (it == list.end()) return out;
    out.found = true;
    Control c = change;
    c.id = id;
    c.key = it->key;
    c.name = c.name.trimmed();
    if (c.name.isEmpty()) c.name = it->name;
    if (c.name != it->name) {
        QStringList taken;
        for (const Control &other : list)
            if (other.id != id) taken << other.key;
        c.key = key(c.name, taken, int(it - list.begin()) + 1);
        out.keyChanged = c.key != it->key;
        out.oldKey = it->key;
        out.newKey = c.key;
    }
    sanitizeChange(c, *it);
    *it = c;
    tidyGroups(list);
    return out;
}

bool remove(std::vector<Control> &list, quint64 id)
{
    auto it = std::find_if(list.begin(), list.end(), [id](const Control &c) { return c.id == id; });
    if (it == list.end()) return false;
    const quint64 parent = it->group;
    list.erase(it);
    for (Control &c : list)
        if (c.group == id) c.group = parent;
    return true;
}

bool move(std::vector<Control> &list, quint64 id, int to)
{
    auto it = std::find_if(list.begin(), list.end(), [id](const Control &c) { return c.id == id; });
    if (it == list.end()) return false;
    const int from = int(it - list.begin());
    to = std::clamp(to, 0, int(list.size()) - 1);
    if (from == to) return true;
    Control c = *it;
    list.erase(it);
    list.insert(list.begin() + to, std::move(c));
    return true;
}

bool setLayouts(std::vector<Control> &list, const QHash<quint64, std::array<int, 4>> &places)
{
    bool changed = false;
    for (Control &c : list) {
        const auto it = places.constFind(c.id);
        if (it == places.cend()) continue;
        const int w = std::clamp(it.value()[2], 1, Control::kGridColumns), h = std::clamp(it.value()[3], 1, Control::kGridRowsMax);
        const int x = std::clamp(it.value()[0], 0, Control::kGridColumns - w), y = std::clamp(it.value()[1], 0, Control::kGridRowsMax);
        if (c.gx == x && c.gy == y && c.gw == w && c.gh == h) continue;
        c.gx = x;
        c.gy = y;
        c.gw = w;
        c.gh = h;
        changed = true;
    }
    return changed;
}

static const QString kSlash = QStringLiteral("/");

bool setValue(std::vector<Control> &list, quint64 id, double value, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind == Kind::Color || c->kind == Kind::Group || !std::isfinite(value)) return false;
    const double next = c->clamped(value);
    if (c->value != next) {
        c->value = next;
        if (feed) feed(c->kind == Kind::Pad ? QStringLiteral("x") : QString(), next);
    }
    return true;
}

bool setPoint(std::vector<Control> &list, quint64 id, double x, double y, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Pad) return false;
    const double v[2] = {x, y};
    for (int axis = 0; axis < 2; ++axis) {
        if (!std::isfinite(v[axis])) continue;
        const double next = c->clamped(v[axis]);
        double &field = axis == 0 ? c->value : c->value2;
        if (field == next) continue;
        field = next;
        if (feed) feed(axis == 0 ? QStringLiteral("x") : QStringLiteral("y"), next);
    }
    return true;
}

bool setPoints(std::vector<Control> &list, quint64 id, const std::vector<double> &xy, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Points) return false;
    if (int(c->points.size()) != c->count) c->points.resize(size_t(c->count), {c->def, c->def});
    for (size_t i = 0; i < xy.size() && i < c->points.size() * 2; ++i) {
        if (!std::isfinite(xy[i])) continue;
        const double next = std::clamp(xy[i], std::min(c->min, c->max), std::max(c->min, c->max));
        double &field = c->points[i / 2][i % 2];
        if (field == next) continue;
        field = next;
        if (feed) feed(QStringLiteral("p%1/%2").arg(i / 2 + 1).arg(i % 2 == 0 ? 'x' : 'y'), next);
    }
    return true;
}

bool setMulti(std::vector<Control> &list, quint64 id, const std::vector<double> &values, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Multi) return false;
    if (int(c->multi.size()) != c->count) c->multi.resize(size_t(c->count), c->def);
    for (size_t i = 0; i < values.size() && i < c->multi.size(); ++i) {
        if (!std::isfinite(values[i])) continue;
        const double next = std::clamp(values[i], std::min(c->min, c->max), std::max(c->min, c->max));
        if (c->multi[i] == next) continue;
        c->multi[i] = next;
        if (feed) feed(QString::number(i + 1), next);
    }
    return true;
}

bool setAdsr(std::vector<Control> &list, quint64 id, const std::array<double, 4> &adsr, const Feed &feed)
{
    Control *c = find(list, id);
    if (!c || c->kind != Kind::Adsr) return false;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(adsr[size_t(i)])) continue;
        const double next = std::clamp(adsr[size_t(i)], 0.0, i == 2 ? 1.0 : std::max(0.1, c->max));
        double &field = c->adsr[size_t(i)];
        if (field == next) continue;
        field = next;
        if (feed) feed(QLatin1String(Control::kAdsrKeys[i]), next);
    }
    return true;
}

bool setColor(Control &c, double r, double g, double b, const Feed &feed)
{
    const double v[3] = {r, g, b};
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(v[i])) continue;
        const double next = std::clamp(v[i], 0.0, 1.0);
        if (c.rgb[size_t(i)] == next) continue;
        c.rgb[size_t(i)] = next;
        if (feed) feed(QString(QLatin1Char("rgb"[i])), next);
    }
    return true;
}

std::optional<Control> forParameter(const ParameterSource &source, quint64 scope, const QString &path, quint64 group)
{
    ParamInfo info;
    if (!source.info(scope, path, &info) || info.type == ParamInfo::Type::Text) return std::nullopt;
    const bool writable = info.access & ParamInfo::Write;
    // What cannot be set can be seen: a number, a switch or a choice that is read only is a view (a meter, a lamp, a readout)
    if (!writable && (info.colorComponent >= 0 || info.type == ParamInfo::Type::Trigger)) return std::nullopt;
    QString target = path; // what the control drives: the parameter, or a whole color
    Control c;
    c.group = group;
    c.name = info.name();
    const QJsonValue now = source.value(scope, path);
    if (info.colorComponent >= 0 && path.size() > 2 && path[path.size() - 2] == QLatin1Char('/')) { // a color's channel: the color
        target = path.left(path.size() - 2);
        c.kind = Kind::Color;
        c.name = info.label.section(QStringLiteral(" › "), -1).section(QLatin1Char(' '), 0, -2).trimmed();
        for (int k = 0; k < 3; ++k) {
            const QJsonValue v = source.value(scope, target + QLatin1Char('/') + QLatin1Char("rgb"[k]));
            if (v.isDouble()) c.rgb[size_t(k)] = std::clamp(v.toDouble(), 0.0, 1.0);
        }
    } else if (info.type == ParamInfo::Type::Bool) {
        c.kind = Kind::Toggle;
        c.value = now.toBool() ? 1 : 0;
    } else if (info.type == ParamInfo::Type::Choice) {
        c.kind = Kind::Choice;
        c.choices = info.choices;
        c.value = std::max(0, int(info.choices.indexOf(now.toString())));
    } else if (info.type == ParamInfo::Type::Trigger) {
        c.kind = Kind::Button;
    } else {
        c.kind = Kind::Slider;
        c.min = info.min;
        c.max = info.max > info.min ? info.max : info.min + 1;
        c.step = info.type == ParamInfo::Type::Int ? std::max(1.0, info.step) : std::max(1e-6, info.step);
        c.def = std::clamp(info.defaultValue.isValid() ? info.defaultValue.toDouble() : c.min, c.min, c.max);
        c.value = std::clamp(now.toDouble(c.def), c.min, c.max);
        const QString unit = info.label.section(QLatin1Char('('), -1);
        if (info.label.endsWith(QLatin1Char(')')) && unit.size() < 8) c.unit = QLatin1Char(' ') + unit.chopped(1); // "Rotation (°)" -> " °"
    }
    if (c.name.isEmpty()) c.name = info.name();
    c.boundScope = scope;
    c.boundParam = target;
    if (!writable) c.widget = c.kind == Kind::Slider ? QStringLiteral("meter") : c.kind == Kind::Toggle ? QStringLiteral("led") : QStringLiteral("readout");
    return c;
}

// A number of a pad's axis, 0..1 over its parameter's range
static bool axis(const ParameterSource &source, quint64 scope, const QString &path, ParamInfo *info, double *lo, double *hi)
{
    if (!source.info(scope, path, info) || !info->isNumber()) return false;
    *lo = info->min;
    *hi = info->max > info->min ? info->max : info->min + 1;
    return true;
}

std::optional<Control> forParameters(const ParameterSource &source, quint64 scope, const QString &pathX, const QString &pathY, quint64 group)
{
    ParamInfo x, y;
    double lo, hi;
    if (!axis(source, scope, pathX, &x, &lo, &hi) || !axis(source, scope, pathY, &y, &lo, &hi)) return std::nullopt;
    if (!(x.access & ParamInfo::Write) || !(y.access & ParamInfo::Write) || pathX == pathY) return std::nullopt;
    Control c;
    c.kind = Kind::Pad;
    c.group = group;
    c.name = x.name() + QStringLiteral(" / ") + y.name();
    c.min = 0;
    c.max = 1;
    c.step = 0.001;
    c.def = 0.5;
    c.boundScope = scope;
    c.boundParam = pathX;
    c.boundParam2 = pathY;
    fillBound(c, source);
    return c;
}

bool setBoundPoint(const Control &c, ParameterSource &source, double x, double y)
{
    ParamInfo ix, iy;
    double xlo, xhi, ylo, yhi;
    if (c.kind != Kind::Pad || c.boundParam2.isEmpty() || !axis(source, c.boundScope, c.boundParam, &ix, &xlo, &xhi) ||
        !axis(source, c.boundScope, c.boundParam2, &iy, &ylo, &yhi))
        return false;
    bool any = false;
    if (std::isfinite(x) && (ix.access & ParamInfo::Write)) {
        any |= source.setNumber(c.boundScope, c.boundParam, xlo + std::clamp(x, 0.0, 1.0) * (xhi - xlo));
        source.touched(c.boundScope, c.boundParam);
    }
    if (std::isfinite(y) && (iy.access & ParamInfo::Write)) {
        any |= source.setNumber(c.boundScope, c.boundParam2, ylo + std::clamp(y, 0.0, 1.0) * (yhi - ylo));
        source.touched(c.boundScope, c.boundParam2);
    }
    return any;
}

void fillBound(Control &c, const ParameterSource &source)
{
    if (c.kind == Kind::Pad && !c.boundParam2.isEmpty()) {
        ParamInfo ix, iy;
        double xlo, xhi, ylo, yhi;
        if (!axis(source, c.boundScope, c.boundParam, &ix, &xlo, &xhi) || !axis(source, c.boundScope, c.boundParam2, &iy, &ylo, &yhi)) return;
        c.min = 0;
        c.max = 1;
        const QJsonValue vx = source.value(c.boundScope, c.boundParam), vy = source.value(c.boundScope, c.boundParam2);
        if (vx.isDouble()) c.value = std::clamp((vx.toDouble() - xlo) / (xhi - xlo), 0.0, 1.0);
        if (vy.isDouble()) c.value2 = std::clamp((vy.toDouble() - ylo) / (yhi - ylo), 0.0, 1.0);
        return;
    }
    if (c.kind == Kind::Color) {
        for (int k = 0; k < 3; ++k) {
            const QJsonValue v = source.value(c.boundScope, c.boundParam + QLatin1Char('/') + QLatin1Char("rgb"[k]));
            if (v.isDouble()) c.rgb[size_t(k)] = std::clamp(v.toDouble(), 0.0, 1.0);
        }
        return;
    }
    ParamInfo info;
    if (!source.info(c.boundScope, c.boundParam, &info)) return;
    const QJsonValue now = source.value(c.boundScope, c.boundParam);
    switch (c.kind) {
    case Kind::Slider:
        if (!info.isNumber()) return;
        c.min = info.min;
        c.max = info.max > info.min ? info.max : info.min + 1;
        c.step = info.type == ParamInfo::Type::Int ? std::max(1.0, info.step) : std::max(1e-6, info.step);
        c.value = std::clamp(now.toDouble(), std::min(c.min, c.max), std::max(c.min, c.max));
        break;
    case Kind::Toggle: c.value = now.toBool() ? 1.0 : 0.0; break;
    case Kind::Choice:
        if (!info.choices.isEmpty()) c.choices = info.choices;
        c.value = std::max<qsizetype>(0, c.choices.indexOf(now.toString()));
        break;
    default: break;
    }
}

bool setBound(const Control &c, ParameterSource &source, double value)
{
    ParamInfo info;
    if (!source.info(c.boundScope, c.boundParam, &info) || !(info.access & ParamInfo::Write)) return false;
    bool ok = false;
    switch (c.kind) {
    case Kind::Slider: ok = info.isNumber() && source.setNumber(c.boundScope, c.boundParam, value); break;
    case Kind::Toggle: ok = source.setJson(c.boundScope, c.boundParam, value >= 0.5); break;
    case Kind::Choice: {
        const QStringList &keys = info.choices;
        const int k = std::clamp(int(std::lround(value)), 0, int(keys.size()) - 1);
        ok = !keys.isEmpty() && source.setJson(c.boundScope, c.boundParam, keys[k]);
        break;
    }
    case Kind::Button: ok = value >= 0.5 && source.setJson(c.boundScope, c.boundParam, true); break; // a trigger: sent when pressed
    default: break;
    }
    if (ok) source.touched(c.boundScope, c.boundParam);
    return ok;
}

bool setBoundColor(const Control &c, ParameterSource &source, double r, double g, double b)
{
    const double v[3] = {r, g, b};
    bool any = false;
    for (int k = 0; k < 3; ++k) {
        const QString path = c.boundParam + QLatin1Char('/') + QLatin1Char("rgb"[k]);
        if (!std::isfinite(v[k]) || !source.setNumber(c.boundScope, path, std::clamp(v[k], 0.0, 1.0))) continue;
        any = true;
        source.touched(c.boundScope, path);
    }
    return any;
}

} // namespace pxs::controls
