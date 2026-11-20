#pragma once
// DMX over the network: the frames of Art-Net (DMX data, OpDmx) and sACN (ANSI E1.31), encoded and decoded. Qt Core only: the
// sockets, the merging of sources and what a channel drives stay in the application.
#include <QByteArray>
#include <QString>

namespace pxs::dmx {

constexpr int channelCount = 512;

struct Frame {
    bool sacn = false;      // sACN, else Art-Net
    int universe = 0;       // Art-Net: 0..32767 (zero based); sACN: 1..63999
    int priority = 100;     // sACN: 0..200
    quint8 sequence = 0;
    QByteArray cid, values; // sACN: the sender's 16-byte id; the levels (512 after a decode)
    QString name;           // sACN: the source's name
    bool terminated = false; // sACN: the source stops sending this universe
};

// The packet of a frame; empty when the universe or the priority is out of range. Fewer than 512 values are padded with zeros.
QByteArray encode(const Frame &frame);
// What a packet carries, checked in full (lengths, flags, vectors); false for anything that is not DMX of that protocol.
// `sacn` says which protocol to expect: the port it arrived on tells it.
bool decode(const QByteArray &data, bool sacn, Frame &frame);

constexpr quint16 artNetPort = 6454;
constexpr quint16 sacnPort = 5568;
// Where an sACN universe is sent: the multicast group 239.255.<high>.<low>
QString sacnGroup(int universe);

} // namespace pxs::dmx
