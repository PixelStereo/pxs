#include "OscCodec.h"

#include <QCoreApplication>
#include <cstdio>

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    osc::Message message{"/layer/1/opacity", "f", {0.5}};
    std::vector<osc::Message> decoded;
    CHECK(osc::decode(osc::encode(message), decoded));
    CHECK(decoded.size() == 1);
    if (decoded.size() == 1) {
        CHECK(decoded[0].address == message.address);
        CHECK(decoded[0].types == "f");
        CHECK(decoded[0].args.size() == 1 && qAbs(decoded[0].args[0].toDouble() - 0.5) < 1e-6);
    }
    // A truncated or hostile packet is refused, never read past its end
    std::vector<osc::Message> refused;
    CHECK(!osc::decode(QByteArray("/x\0\0,f\0\0\x3f", 9), refused));
    CHECK(osc::match("/layer/*/opacity", "/layer/1/opacity"));
    CHECK(!osc::match("/layer/[0-3]/opacity", "/layer/7/opacity"));
    CHECK(osc::match("/{a,b}/x", "/b/x"));
    return failures ? 1 : 0;
}
