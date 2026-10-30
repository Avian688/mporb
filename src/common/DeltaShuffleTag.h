// SPDX-License-Identifier: LGPL-3.0-or-later
#ifndef INET_DELTA_SHUFFLE_TAG_H
#define INET_DELTA_SHUFFLE_TAG_H
#include "inet/common/TagBase.h"
namespace inet {
// TCP-header region telemetry; physical header size is abstracted.
class DeltaShuffleTag : public TagBase {
  public:
    double subflowRate = 0, connectionRate = 0; // payload bytes/s at transmission
    unsigned long long serial = 0; // new for each data transmission
    unsigned int hops = 0; // zero or one selected participating bottleneck
    int bottleneckId = -1; // must match the final INT winner, including plain PINT hops
    bool echoed = false;
    double meanConnectionRate = 0, rateAllowance = 0; // bytes/s, previous interval
    double packetMass = 0; // dimensionless, current interval
    virtual DeltaShuffleTag *dup() const override { return new DeltaShuffleTag(*this); }
    virtual void parsimPack(cCommBuffer *b) const override {
        TagBase::parsimPack(b);
        b->pack(subflowRate); b->pack(connectionRate); b->pack(serial);
        b->pack(hops); b->pack(bottleneckId); b->pack(echoed);
        b->pack(meanConnectionRate); b->pack(rateAllowance); b->pack(packetMass);
    }
    virtual void parsimUnpack(cCommBuffer *b) override {
        TagBase::parsimUnpack(b);
        b->unpack(subflowRate); b->unpack(connectionRate); b->unpack(serial);
        b->unpack(hops); b->unpack(bottleneckId); b->unpack(echoed);
        b->unpack(meanConnectionRate); b->unpack(rateAllowance); b->unpack(packetMass);
    }
};
}
#endif
