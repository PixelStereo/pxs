#include "pxs/DmxNet.h"

#include <QCoreApplication>
#include <QThread>
#include <cstdio>

using namespace pxs::dmx;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static QByteArray sacn(int universe, int priority, const QByteArray &cid, quint8 sequence, const QByteArray &levels, bool terminated = false)
{
    Frame f;
    f.sacn = true;
    f.universe = universe;
    f.priority = priority;
    f.cid = cid;
    f.sequence = sequence;
    f.values = levels;
    f.terminated = terminated;
    return encode(f);
}
static QByteArray artnet(int universe, quint8 sequence, const QByteArray &levels)
{
    Frame f;
    f.universe = universe;
    f.sequence = sequence;
    f.values = levels;
    return encode(f);
}
static QByteArray levels(std::initializer_list<std::pair<int, int>> set)
{
    QByteArray v(512, 0);
    for (auto [channel, level] : set) v[channel - 1] = char(level);
    return v;
}
static quint8 at(const QByteArray &v, int channel) { return quint8(v[channel - 1]); }

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QHostAddress a("10.0.0.1"), b("10.0.0.2");
    const QByteArray cidA(16, 'a'), cidB(16, 'b');

    // HTP: the highest level of each channel among the sources of a universe
    {
        Monitor m;
        CHECK(m.ingest(artnet(0, 1, levels({{1, 100}, {2, 10}})), a, 6454, false));
        CHECK(m.ingest(artnet(0, 1, levels({{1, 50}, {2, 200}})), b, 6454, false));
        bool live = false;
        QByteArray htp = m.merged(false, 0, MergeMode::HTP, false, &live);
        CHECK(live && at(htp, 1) == 100 && at(htp, 2) == 200);
        // Two senders of one address are told apart by port; another universe is another merge
        CHECK(m.ingest(artnet(0, 1, levels({{3, 77}})), a, 6455, false));
        CHECK(at(m.merged(false, 0, MergeMode::HTP, false), 3) == 77);
        CHECK(m.merged(false, 1, MergeMode::HTP, false).isEmpty());
        CHECK(m.streams().size() == 3);
    }

    // LTP: the last source to change a channel wins, whatever the levels
    {
        Monitor m;
        m.ingest(artnet(0, 1, levels({{1, 200}})), a, 6454, false);
        m.ingest(artnet(0, 1, levels({{1, 10}})), b, 6454, false); // b changes channel 1 after a did
        CHECK(at(m.merged(false, 0, MergeMode::LTP, false), 1) == 10);
        m.ingest(artnet(0, 2, levels({{1, 90}})), a, 6454, false); // a changes it again
        CHECK(at(m.merged(false, 0, MergeMode::LTP, false), 1) == 90);
    }

    // sACN: only the sources of the highest priority count, then the merge applies to them
    {
        Monitor m;
        m.ingest(sacn(5, 100, cidA, 1, levels({{1, 255}})), a, 5568, true);
        m.ingest(sacn(5, 150, cidB, 1, levels({{1, 20}})), b, 5568, true);
        CHECK(at(m.merged(true, 5, MergeMode::HTP, false), 1) == 20);
        m.ingest(sacn(5, 150, QByteArray(16, 'c'), 1, levels({{1, 60}})), a, 5568, true);
        CHECK(at(m.merged(true, 5, MergeMode::HTP, false), 1) == 60);
        CHECK(m.merged(false, 5, MergeMode::HTP, false).isEmpty()); // Art-Net universe 5 is another thing
    }

    // Sequence: an old packet is dropped and counted; a source that stops is forgotten; garbage is counted
    {
        Monitor m;
        QStringList lines;
        QObject::connect(&m, &Monitor::logged, [&](const QString &line) { lines << line; });
        CHECK(m.ingest(sacn(1, 100, cidA, 10, levels({{1, 5}})), a, 5568, true));
        CHECK(!m.ingest(sacn(1, 100, cidA, 9, levels({{1, 6}})), a, 5568, true));
        CHECK(m.stats().reordered == 1 && at(m.merged(true, 1, MergeMode::HTP, false), 1) == 5);
        CHECK(m.ingest(sacn(1, 100, cidA, 11, levels({{1, 7}})), a, 5568, true));
        CHECK(at(m.merged(true, 1, MergeMode::HTP, false), 1) == 7);
        m.ingest(sacn(1, 100, cidA, 12, levels({}), true), a, 5568, true);
        CHECK(m.streams().isEmpty() && m.merged(true, 1, MergeMode::HTP, false).isEmpty());
        CHECK(!m.ingest(QByteArray("not dmx"), a, 5568, true) && m.stats().invalid == 1);
        CHECK(m.stats().datagrams == 5 && m.stats().received == 2);
        CHECK(!lines.isEmpty() && lines.first().startsWith("RX · sACN · U1") && lines.last().startsWith("RX unsupported"));
    }

    // A source that is not heard any more is not live: the merge goes quiet, the stream is still listed
    {
        Monitor m;
        m.ingest(artnet(0, 1, levels({{1, 9}})), a, 6454, false);
        CHECK(!m.merged(false, 0, MergeMode::HTP, false).isEmpty());
        QThread::msleep(Monitor::staleMs + 100);
        bool live = true;
        CHECK(m.merged(false, 0, MergeMode::HTP, false, &live).isEmpty() && !live && m.streams().size() == 1);
    }

    // The application's own frames are told by its CID (sACN) and left out of the external merge
    {
        Transmitter t;
        Monitor m;
        m.setTransmitter(&t);
        m.setOwnLabel("Test output");
        m.ingest(sacn(2, 100, t.cid(), 1, levels({{1, 80}})), a, 5568, true);
        m.ingest(sacn(2, 100, cidB, 1, levels({{1, 30}})), b, 5568, true);
        CHECK(at(m.merged(true, 2, MergeMode::HTP, false), 1) == 80);
        CHECK(at(m.merged(true, 2, MergeMode::HTP, true), 1) == 30);
        int own = 0;
        for (const Stream &s : m.streams())
            if (s.own) { ++own; CHECK(s.frame.name == "Test output"); }
        CHECK(own == 1);
    }

    // The number of sources is bounded
    {
        Monitor m;
        for (int i = 0; i < Monitor::maxStreams + 10; ++i) {
            QByteArray cid(16, 0);
            cid[0] = char(i & 255);
            cid[1] = char(i >> 8);
            m.ingest(sacn(1, 100, cid, 1, levels({})), a, 5568, true);
        }
        CHECK(m.streams().size() == Monitor::maxStreams);
    }

    // On the loopback: the Transmitter's Art-Net frame reaches the Monitor, which knows it is its own
    {
        QNetworkInterface loopback;
        for (const auto &n : QNetworkInterface::allInterfaces())
            if (n.flags().testFlag(QNetworkInterface::IsLoopBack) && n.flags().testFlag(QNetworkInterface::IsUp)) loopback = n;
        if (!loopback.isValid()) {
            std::printf("dmxnet: no loopback interface, network part skipped\n");
        } else {
            Transmitter t;
            t.setPorts(46454, 45568);
            Monitor m;
            m.setPorts(46454, 45568);
            m.setTransmitter(&t);
            QString error;
            CHECK(m.start(loopback, &error));
            CHECK(t.start(loopback, &error));
            Frame f;
            f.universe = 3;
            f.values = levels({{7, 42}});
            CHECK(t.send(f, QHostAddress::LocalHost, &error));
            for (int i = 0; i < 100 && m.streams().isEmpty(); ++i) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
            CHECK(m.streams().size() == 1);
            if (!m.streams().isEmpty()) {
                const Stream &s = m.streams().first();
                CHECK(s.own && s.frame.universe == 3 && at(s.frame.values, 7) == 42);
                CHECK(at(m.merged(false, 3, MergeMode::HTP, false), 7) == 42);
                CHECK(m.merged(false, 3, MergeMode::HTP, true).isEmpty());
            }
            // The sequence is the Transmitter's, from 1
            CHECK(t.lastSequence() == 1);
            CHECK(t.send(f, QHostAddress::LocalHost, &error) && t.lastPort() == 46454 && t.lastSequence() == 2);
            CHECK(!t.send(Frame{.universe = -4}, QHostAddress::LocalHost, &error) && !error.isEmpty());
            m.stop();
            t.stop();
            CHECK(!m.listening() && !t.active());
        }
    }

    if (failures) return 1;
    std::printf("dmxnet passed\n");
    return 0;
}
