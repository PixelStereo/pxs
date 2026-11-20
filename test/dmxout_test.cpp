#include "pxs/DmxOut.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QUdpSocket>
#include <cstdio>
#include <thread>

using namespace pxs::dmx;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

// The datagrams a socket gets in `ms`
static QList<QByteArray> listen(QUdpSocket &socket, int ms, int atLeast = 1)
{
    QList<QByteArray> got;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms && got.size() < 1000) {
        if (socket.waitForReadyRead(20))
            while (socket.hasPendingDatagrams()) got << socket.receiveDatagram().data();
        else if (got.size() >= atLeast && clock.elapsed() > ms / 2)
            break;
    }
    return got;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // The defaults suit each protocol; what is not valid is said
    {
        const Output art = Output::defaults(Protocol::ArtNet), sacn = Output::defaults(Protocol::Sacn);
        CHECK(art.firstUniverse == 0 && sacn.firstUniverse == 1 && art.effectivePort() == 6454 && sacn.effectivePort() == 5568);
        CHECK(art.validate().isEmpty() && sacn.validate().isEmpty());
        Output o = art;
        o.rate = 60;
        CHECK(!o.validate().isEmpty());
        o = art;
        o.delivery = Delivery::Unicast;
        CHECK(!o.validate().isEmpty()); // no address
        o.destinations = {"192.168.1.20", "nope"};
        CHECK(o.validate().contains("nope"));
        o.destinations = {"192.168.1.20"};
        CHECK(o.validate().isEmpty());
        o.interfaceAddress = "not an address";
        CHECK(!o.validate().isEmpty());
        o = sacn;
        o.priority = 201;
        CHECK(!o.validate().isEmpty());
        o = art;
        o.firstUniverse = 32767;
        o.universeCount = 2;
        CHECK(!o.validate().isEmpty()); // goes past Art-Net's last universe
        o = sacn;
        o.firstUniverse = 0;
        CHECK(!o.validate().isEmpty());
    }

    // JSON round trip, and what is missing takes the protocol's default
    {
        Output o = Output::defaults(Protocol::Sacn);
        o.id = 7;
        o.name = "Stage";
        o.enabled = false;
        o.delivery = Delivery::Unicast;
        o.destinations = {"10.0.0.5", "10.0.0.6"};
        o.interfaceAddress = "10.0.0.2";
        o.port = 5570;
        o.rate = 30;
        o.priority = 150;
        o.sourceName = "Console";
        o.transmission = Transmission::Partial;
        o.firstSourceUniverse = 3;
        o.firstUniverse = 20;
        o.universeCount = 4;
        CHECK(Output::fromJson(o.toJson()) == o);
        const Output art = Output::fromJson(QJsonObject{{"protocol", "artnet"}});
        CHECK(art.firstUniverse == 0 && art.delivery == Delivery::Group && art.rate == 40 && art.priority == 100);
        CHECK(Output::fromJson(QJsonObject{{"rate", 500}, {"priority", 900}}).rate == 44);
    }

    // Routing: the application's universes shifted onto each output's range
    {
        Output a = Output::defaults(Protocol::ArtNet), b = Output::defaults(Protocol::Sacn);
        a.id = 1; // patch universes 1.. to Art-Net 0..
        b.id = 2;
        b.firstSourceUniverse = 3;
        b.universeCount = 2;
        b.firstUniverse = 22; // patch 3, 4 to sACN 22, 23
        Output off = a;
        off.id = 3;
        off.enabled = false;
        const std::map<int, QByteArray> sources{{1, QByteArray(512, 1)}, {2, QByteArray(512, 2)}, {3, QByteArray(512, 3)}, {4, QByteArray(512, 4)}, {5, QByteArray(512, 5)}};
        const Universes routed = route(sources, {a, b, off});
        CHECK(routed.size() == 5 + 2);
        CHECK(routed.at({1, 0})[0] == 1 && routed.at({1, 4})[0] == 5);
        CHECK(routed.at({2, 22})[0] == 3 && routed.at({2, 23})[0] == 4 && !routed.count({2, 24}) && !routed.count({2, 20}));
        CHECK(!routed.count({3, 0}));
    }

    // The sender: Art-Net unicast to the loopback, at its rate, with its own sequence; partial frames; state
    {
        QUdpSocket receiver;
        CHECK(receiver.bind(QHostAddress::LocalHost, 46464));
        Output o = Output::defaults(Protocol::ArtNet);
        o.id = 1;
        o.delivery = Delivery::Unicast;
        o.destinations = {"127.0.0.1"};
        o.interfaceAddress = "127.0.0.1";
        o.port = 46464;
        o.rate = 20;
        Sender sender;
        sender.follow({o});
        QByteArray levels(512, 0);
        levels[0] = char(200);
        sender.submit({{{1, 0}, levels}, {{1, 1}, levels}, {{2, 0}, levels}}); // output 2 is not ours: nothing for it
        const QList<QByteArray> got = listen(receiver, 1200, 8);
        CHECK(got.size() >= 8 && got.size() <= 60); // about 20 per second and universe, two universes
        int universe0 = 0, universe1 = 0;
        quint8 last = 0;
        bool ordered = true;
        for (const QByteArray &packet : got) {
            Frame f;
            CHECK(decode(packet, false, f) && f.length == 512 && quint8(f.values[0]) == 200);
            if (f.universe == 0) {
                ++universe0;
                if (last && f.sequence != quint8(last % 255 + 1)) ordered = false;
                last = f.sequence;
            }
            if (f.universe == 1) ++universe1;
        }
        CHECK(universe0 > 2 && universe1 > 2 && ordered); // a sequence of its own for each universe, 1..255
        CHECK(sender.state(1).level == OutputState::Ok && sender.state(1).packets >= quint64(got.size()));
        CHECK(sender.state(2).level == OutputState::Off);

        // Partial: only the channels handed over, in an even number
        o.transmission = Transmission::Partial;
        sender.follow({o});
        sender.submit({{{1, 0}, QByteArray(11, 5)}});
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        while (receiver.hasPendingDatagrams()) receiver.receiveDatagram(); // what was in flight
        const QList<QByteArray> partial = listen(receiver, 600, 2);
        CHECK(!partial.isEmpty());
        for (const QByteArray &packet : partial) {
            Frame f;
            CHECK(decode(packet, false, f) && f.length == 12 && f.universe == 0); // 11 rounded up to an even 12
        }
        // A bad output is an error, not a crash
        Output bad = o;
        bad.rate = 99;
        sender.follow({bad});
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(sender.state(1).level == OutputState::Error && !sender.state(1).text.isEmpty());
        sender.stop();
    }

    // sACN: the identity, the per-channel priorities of the channels that are ours, and the end of the stream
    {
        QUdpSocket receiver;
        CHECK(receiver.bind(QHostAddress::LocalHost, 46465));
        Output o = Output::defaults(Protocol::Sacn);
        o.id = 5;
        o.delivery = Delivery::Unicast;
        o.destinations = {"127.0.0.1"};
        o.interfaceAddress = "127.0.0.1";
        o.port = 46465;
        o.rate = 10;
        o.priority = 120;
        o.sourceName = "Test console";
        Sender sender;
        const QUuid cid = QUuid::createUuid();
        sender.setCid(cid);
        CHECK(sender.cid() == cid);
        sender.follow({o});
        QByteArray levels(512, 0), ours(512, 0);
        levels[2] = char(99);
        ours[2] = 1; // channel 3 is ours, the others are not
        sender.submit({{{5, 4}, levels}}, {{{5, 4}, ours}});
        const QList<QByteArray> got = listen(receiver, 1500, 4);
        int levelsSeen = 0, prioritiesSeen = 0;
        for (const QByteArray &packet : got) {
            if (packet.size() > 125 && quint8(packet[125]) == 0xDD) {
                ++prioritiesSeen;
                CHECK(quint8(packet[126 + 2]) == 120 && quint8(packet[126 + 3]) == 0); // ours at the priority, the rest at 0
                continue;
            }
            Frame f;
            CHECK(decode(packet, true, f) && f.universe == 4 && f.priority == 120 && f.name == "Test console" && f.cid == cid.toRfc4122());
            CHECK(!f.terminated && quint8(f.values[2]) == 99);
            ++levelsSeen;
        }
        CHECK(levelsSeen >= 3 && prioritiesSeen >= 1);
        // Switched off: the stream is ended (three terminated frames), not just left to time out
        while (receiver.hasPendingDatagrams()) receiver.receiveDatagram();
        sender.follow({});
        const QList<QByteArray> end = listen(receiver, 600, 3);
        int terminated = 0;
        for (const QByteArray &packet : end) {
            Frame f;
            if (decode(packet, true, f) && f.terminated) ++terminated;
        }
        CHECK(terminated >= 3);
        sender.stop();
    }

    if (failures) return 1;
    std::printf("dmxout passed\n");
    return 0;
}
