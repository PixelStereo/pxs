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
        return std::string(utf8.constData(), std::size_t(utf8.size()));
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
        if (const auto *v = std::get_if<pxs::core::osc::Blob>(&value))
            return QByteArray(reinterpret_cast<const char *>(v->data()), qsizetype(v->size()));
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

// One segment: * ? [set] {alt,alt}. Matched in O(pattern × address) whatever the pattern: a backtracking matcher
// takes exponential time on runs of '*' ("/**************a"), and any machine on the network can send one (the
// fuzzer found it: a 1-minute run stuck for over 20 minutes). The pattern becomes tokens, then the positions of the
// address it can have reached are carried from one token to the next (two rows).
namespace {
struct Token {
    enum Kind { Char, Any, Star, Set, Alt } kind = Char;
    QChar c;
    QString set; // Set: its characters and ranges (a-z), without the '!'
    bool negate = false;
    QStringList alts; // Alt: literal alternatives (OSC 1.0: "{foo,bar}")
};

bool inSet(const Token &t, QChar ch)
{
    bool in = false;
    for (int k = 0; k < t.set.size(); ++k) {
        if (k + 2 < t.set.size() && t.set[k + 1] == '-') {
            in |= ch >= t.set[k] && ch <= t.set[k + 2];
            k += 2;
        } else {
            in |= ch == t.set[k];
        }
    }
    return in != t.negate;
}

// false: malformed ('[' or '{' never closed), which matches nothing
bool tokenize(const QString &p, std::vector<Token> &out)
{
    for (int pi = 0; pi < p.size(); ++pi) {
        const QChar c = p[pi];
        Token t;
        if (c == '*') {
            if (!out.empty() && out.back().kind == Token::Star) continue; // "**" is "*"
            t.kind = Token::Star;
        } else if (c == '?') {
            t.kind = Token::Any;
        } else if (c == '[') {
            const int end = p.indexOf(']', pi);
            if (end < 0) return false;
            t.kind = Token::Set;
            t.set = p.mid(pi + 1, end - pi - 1);
            t.negate = t.set.startsWith('!');
            if (t.negate) t.set.remove(0, 1);
            pi = end;
        } else if (c == '{') {
            const int end = p.indexOf('}', pi);
            if (end < 0) return false;
            t.kind = Token::Alt;
            t.alts = p.mid(pi + 1, end - pi - 1).split(',');
            pi = end;
        } else {
            t.c = c;
        }
        out.push_back(std::move(t));
    }
    return true;
}
} // namespace

static bool matchSeg(const QString &p, const QString &s)
{
    static const QString special = QStringLiteral("*?[{");
    if (std::none_of(p.begin(), p.end(), [](QChar c) { return special.contains(c); })) return p == s; // the usual case
    std::vector<Token> tokens;
    if (!tokenize(p, tokens)) return false;
    const int n = int(s.size());
    std::vector<char> at(size_t(n) + 1, 0), next(size_t(n) + 1, 0);
    at[0] = 1;
    for (const Token &t : tokens) {
        std::fill(next.begin(), next.end(), 0);
        bool any = false;
        if (t.kind == Token::Star) { // from the first position reached, every one after it
            bool on = false;
            for (int si = 0; si <= n; ++si) {
                on = on || at[size_t(si)];
                next[size_t(si)] = on;
                any = any || on;
            }
        } else {
            for (int si = 0; si <= n; ++si) {
                if (!at[size_t(si)]) continue;
                switch (t.kind) {
                case Token::Char:
                    if (si < n && s[si] == t.c) next[size_t(si) + 1] = any = true;
                    break;
                case Token::Any:
                    if (si < n) next[size_t(si) + 1] = any = true;
                    break;
                case Token::Set:
                    if (si < n && inSet(t, s[si])) next[size_t(si) + 1] = any = true;
                    break;
                case Token::Alt:
                    for (const QString &alt : t.alts)
                        if (QStringView(s).mid(si).startsWith(alt)) next[size_t(si) + size_t(alt.size())] = any = true;
                    break;
                case Token::Star:
                    break;
                }
            }
        }
        if (!any) return false;
        at.swap(next);
    }
    return at[size_t(n)];
}

bool match(const QString &pattern, const QString &address)
{
    // Bound work and allocations before tokenization or dynamic programming.
    // Valid OSC inputs fit comfortably within these operational safety caps.
    if (pattern.size() > kMaxOscPatternChars || address.size() > kMaxOscPatternChars)
        return false;
    const QStringList p = pattern.split('/'), a = address.split('/');
    if (p.size() != a.size()) return false;
    for (int k = 0; k < p.size(); ++k) {
        if (p[k].size() > kMaxOscSegmentChars || a[k].size() > kMaxOscSegmentChars)
            return false;
        if (!matchSeg(p[k], a[k])) return false;
    }
    return true;
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
