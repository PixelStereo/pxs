#include "pxs/Dmx.h"

#include <QUuid>
#include <cstdio>

using namespace pxs::dmx;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main()
{
    // A frame goes out and comes back whole, for both protocols; any truncation of it is refused
    for (bool sacn : {false, true}) {
        Frame f;
        f.sacn = sacn;
        f.universe = sacn ? 63999 : 32767;
        f.sequence = 255;
        f.cid = QUuid::createUuid().toRfc4122();
        f.name = "Test";
        f.values = QByteArray(512, 0);
        f.values[0] = char(255);
        f.values[511] = 123;
        const QByteArray data = encode(f);
        Frame back;
        CHECK(decode(data, sacn, back));
        CHECK(back.values == f.values && back.universe == f.universe && back.sequence == f.sequence);
        if (sacn) CHECK(back.cid == f.cid && back.name == "Test" && back.priority == 100);
        for (int n = 0; n < data.size(); ++n) CHECK(!decode(data.left(n), sacn, back));
        // An Art-Net packet is not sACN, and the other way round
        CHECK(!decode(data, !sacn, back));
    }

    // Fewer values than 512 are padded
    Frame shortFrame;
    shortFrame.values = QByteArray(10, char(7));
    Frame padded;
    CHECK(decode(encode(shortFrame), false, padded) && padded.values.size() == 512 && padded.values[9] == 7 && padded.values[10] == 0);

    // Out of range: nothing is encoded
    Frame bad;
    bad.universe = -1;
    CHECK(encode(bad).isEmpty());
    bad.universe = 32768;
    CHECK(encode(bad).isEmpty());
    bad.sacn = true;
    bad.universe = 0;
    CHECK(encode(bad).isEmpty());
    bad.universe = 64000;
    CHECK(encode(bad).isEmpty());
    bad.universe = 1;
    bad.priority = 201;
    CHECK(encode(bad).isEmpty());

    // sACN: a source that stops, and the malformed flags
    Frame stop;
    stop.sacn = true;
    stop.universe = 1;
    stop.terminated = true;
    Frame stopped;
    CHECK(decode(encode(stop), true, stopped) && stopped.terminated);
    QByteArray data = encode(stop);
    data[123] = 3; // a length that does not match
    CHECK(!decode(data, true, stopped));
    data = encode(stop);
    data[112] = char(0x80); // a reserved flag
    CHECK(!decode(data, true, stopped));

    // Partial frames: only the channels asked for (Art-Net rounds up to an even number); an sACN per-channel priority keeps its start code
    for (bool sacn : {false, true}) {
        Frame f;
        f.sacn = sacn;
        f.universe = 1;
        f.values = QByteArray(512, char(9));
        f.length = 11;
        const QByteArray packet = encode(f);
        Frame back;
        CHECK(decode(packet, sacn, back) && back.length == (sacn ? 11 : 12) && back.values[10] == 9 && back.values[sacn ? 11 : 12] == 0);
        CHECK(packet.size() == (sacn ? 126 + 11 : 18 + 12));
        f.length = 1;
        CHECK(decode(encode(f), sacn, back) && back.length == (sacn ? 1 : 2));
        f.length = 9999;
        CHECK(decode(encode(f), sacn, back) && back.length == 512);
    }
    {
        Frame priorities;
        priorities.sacn = true;
        priorities.universe = 2;
        priorities.startCode = 0xDD;
        const QByteArray packet = encode(priorities);
        CHECK(quint8(packet[125]) == 0xDD);
        Frame ignored;
        CHECK(!decode(packet, true, ignored)); // a monitor does not read priorities as levels
    }

    // The multicast group of a universe
    CHECK(sacnGroup(1) == "239.255.0.1" && sacnGroup(514) == "239.255.2.2");

    // Arbitrary bytes never crash the decoder and never give a short frame
    quint32 state = 12345;
    auto next = [&state] { state = 1664525 * state + 1013904223; return state; };
    Frame out;
    for (int i = 0; i < 20000; ++i) {
        QByteArray noise(int(next() % 1024), 0);
        for (auto &byte : noise) byte = char(next() >> 24);
        if (decode(noise, false, out)) CHECK(out.values.size() == 512);
        if (decode(noise, true, out)) CHECK(out.values.size() == 512);
    }
    for (bool sacn : {false, true}) {
        Frame f;
        f.sacn = sacn;
        f.universe = 1;
        const QByteArray valid = encode(f);
        for (int i = 0; i < valid.size(); ++i) {
            QByteArray changed = valid;
            changed[i] = char(quint8(changed[i]) ^ 0xff);
            if (decode(changed, sacn, out)) CHECK(out.values.size() == 512);
        }
    }
    if (failures) return 1;
    std::printf("dmx passed\n");
    return 0;
}
