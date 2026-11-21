#include "OscCodec.h"

#include <pxs/core/osc/codec.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace osc {
namespace {

pxs::core::osc::Value toCore(char type, const QVariant &value)
{
    using pxs::core::osc::Blob;
    using pxs::core::osc::Value;
    switch (type) {
    case 'i': return std::int32_t(value.toInt());
    case 'h':
    case 't': return std::int64_t(value.toLongLong());
    case 'f': return float(value.toFloat());
    case 'd': return value.toDouble();
    case 's':
    case 'S': {
        const QByteArray utf8 = value.toString().toUtf8();
        const char *data = utf8.constData();
        return std::string(data ? data : "", std::size_t(utf8.size()));
    }
    case 'b': {
        const QByteArray bytes = value.toByteArray();
        Blob blob(std::size_t(bytes.size()));
        if (!blob.empty()) std::copy_n(reinterpret_cast<const std::uint8_t *>(bytes.constData()), blob.size(), blob.begin());
        return blob;
    }
    case 'c': {
        const QString text = value.toString();
        return std::uint32_t(text.isEmpty() ? 0 : text.at(0).unicode());
    }
    case 'r':
    case 'm': return std::uint32_t(value.toLongLong());
    case 'T': return true;
    case 'F': return false;
    default: return Value{};
    }
}

QVariant fromCore(char type, const pxs::core::osc::Value &value)
{
    switch (type) {
    case 'i': if (const auto *v = std::get_if<std::int32_t>(&value)) return int(*v); break;
    case 'h': if (const auto *v = std::get_if<std::int64_t>(&value)) return qlonglong(*v); break;
    case 'f': if (const auto *v = std::get_if<float>(&value)) return double(*v); break;
    case 'd': if (const auto *v = std::get_if<double>(&value)) return *v; break;
    case 's':
    case 'S':
        if (const auto *v = std::get_if<std::string>(&value)) return QString::fromUtf8(v->data(), qsizetype(v->size()));
        break;
    case 'b':
        if (const auto *v = std::get_if<pxs::core::osc::Blob>(&value)) {
            if (v->empty()) return QByteArray();
            return QByteArray(reinterpret_cast<const char *>(v->data()), qsizetype(v->size()));
        }
        break;
    case 'c':
        if (const auto *v = std::get_if<std::uint32_t>(&value)) return QString(QChar(char16_t(*v & 0xffff)));
        break;
    case 'r':
    case 'm':
        if (const auto *v = std::get_if<std::uint32_t>(&value)) return qlonglong(*v);
        break;
    case 't': if (const auto *v = std::get_if<std::int64_t>(&value)) return qlonglong(*v); break;
    case 'T': return true;
    case 'F': return false;
    default: return {};
    }
    return {};
}

} // namespace

QByteArray encode(const Message &message)
{
    pxs::core::osc::Message portable;
    portable.address = message.address.toUtf8().toStdString();
    portable.types = message.types.toLatin1().toStdString();
    portable.args.reserve(std::size_t(message.types.size()));
    for (qsizetype i = 0; i < message.types.size(); ++i)
        portable.args.push_back(toCore(message.types.at(i).toLatin1(), message.args.value(i)));
    const auto bytes = pxs::core::osc::encode(portable);
    return QByteArray(reinterpret_cast<const char *>(bytes.data()), qsizetype(bytes.size()));
}

bool decode(const QByteArray &packet, std::vector<Message> &out)
{
    const auto *first = reinterpret_cast<const std::uint8_t *>(packet.constData());
    pxs::core::osc::Bytes bytes;
    if (!packet.isEmpty()) bytes.assign(first, first + packet.size());
    std::vector<pxs::core::osc::Message> portable;
    if (!pxs::core::osc::decode(bytes, portable)) return false;
    for (const auto &item : portable) {
        Message message;
        message.address = QString::fromUtf8(item.address.data(), qsizetype(item.address.size()));
        message.types = QString::fromLatin1(item.types.data(), qsizetype(item.types.size()));
        for (std::size_t i = 0; i < item.types.size(); ++i)
            message.args << fromCore(item.types[i], item.args[i]);
        out.push_back(std::move(message));
    }
    return true;
}

bool match(const QString &pattern, const QString &address)
{
    const QByteArray p = pattern.toUtf8();
    const QByteArray a = address.toUtf8();
    const char *pData = p.constData();
    const char *aData = a.constData();
    return pxs::core::osc::match(std::string(pData ? pData : "", std::size_t(p.size())),
                                 std::string(aData ? aData : "", std::size_t(a.size())));
}

QString safeName(const QString &name)
{
    QString s = name.trimmed();
    for (QChar &c : s)
        if (c.isSpace() || QStringLiteral("#*,/?[]{}!").contains(c)) c = '_';
    return s.isEmpty() ? QStringLiteral("_") : s;
}

QString SegmentNamer::take(const QString &scope, const QString &name)
{
    const QString base = safeName(name);
    const QString key = scope + QLatin1Char('/') + base;
    int k = m_used.value(key);
    QString seg = k ? base + QStringLiteral("_%1").arg(k + 1) : base;
    while (m_taken.contains(scope + QLatin1Char('/') + seg)) seg = base + QStringLiteral("_%1").arg(++k + 1);
    m_used[key] = k + 1;
    m_taken.insert(scope + QLatin1Char('/') + seg);
    return seg;
}

QStringList uniqueSegments(const QStringList &names)
{
    QStringList out;
    SegmentNamer namer;
    for (const QString &n : names) out << namer.take(QString(), n);
    return out;
}

} // namespace osc
